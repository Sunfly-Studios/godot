/**************************************************************************/
/*  cubemap_filter.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifdef GLES2_ENABLED

#include "cubemap_filter.h"

#include "drivers/gles2/storage/texture_storage.h"
#include "core/config/project_settings.h"

using namespace GLES2;

CubemapFilter *CubemapFilter::singleton = nullptr;

CubemapFilter::CubemapFilter() {
	singleton = this;

	// Same as Compatibility (TODO: what would happen if its lower for Legacy?)
	ggx_samples = 4 * uint32_t(GLOBAL_GET("rendering/reflections/sky_reflections/ggx_samples"));

	{
		String defines;
		defines += "\n#define MAX_SAMPLE_COUNT " + itos(ggx_samples) + "\n";
		cubemap_filter.shader.initialize(defines);
		cubemap_filter.shader_version = cubemap_filter.shader.version_create();
	}

	{ // Screen Triangle.
		glGenBuffers(1, &screen_triangle);
		glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);

		constexpr float qv[6] = {
			-1.0f, -1.0f, -1.0f,
			3.0f, 3.0f, -1.0f,
		};

		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, qv, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind
		GL_CHECK_ERROR("GLES2::CubemapFilter::CubemapFilter: glBufferData");
	}
}

CubemapFilter::~CubemapFilter() {
	glDeleteBuffers(1, &screen_triangle);
	cubemap_filter.shader.version_free(cubemap_filter.shader_version);
	singleton = nullptr;
}

// Helper functions for IBL filtering

static Vector3 importance_sample_GGX(Vector2 xi, float roughness4) {
	// Compute distribution direction
	float phi = 2.0 * Math_PI * xi.x;
	float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (roughness4 - 1.0) * xi.y));
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	// Convert to spherical direction
	Vector3 half_vector;
	half_vector.x = sin_theta * cos(phi);
	half_vector.y = sin_theta * sin(phi);
	half_vector.z = cos_theta;

	return half_vector;
}

static float distribution_GGX(float NdotH, float roughness4) {
	float NdotH2 = NdotH * NdotH;
	float denom = (NdotH2 * (roughness4 - 1.0) + 1.0);
	denom = Math_PI * denom * denom;

	return roughness4 / denom;
}

static float radical_inverse_vdC(uint32_t bits) {
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
	bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
	bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
	bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);

	return float(bits) * 2.3283064365386963e-10;
}

static Vector2 hammersley(uint32_t i, uint32_t N) {
	return Vector2(float(i) / float(N), radical_inverse_vdC(i));
}

void CubemapFilter::filter_radiance(GLuint p_source_cubemap, GLuint p_dest_cubemap, GLuint p_dest_framebuffer, int p_source_size, int p_mipmap_count, int p_layer) {
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, p_source_cubemap);
	glBindFramebuffer(GL_FRAMEBUFFER, p_dest_framebuffer);

	CubemapFilterShaderGLES2::ShaderVariant mode = CubemapFilterShaderGLES2::MODE_DEFAULT;

	if (p_layer == 0) {
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		GL_CHECK_ERROR("GLES2::CubemapFilter::filter_radiance: glGenerateMipmap");
		// Copy over base layer without filtering.
		mode = CubemapFilterShaderGLES2::MODE_COPY;
	}

	int size = p_source_size >> p_layer;
	glViewport(0, 0, size, size);

	// Bind standard VBO array buffer instead of relying on VAOs natively for GLES2
	glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
	glEnableVertexAttribArray(RS::ARRAY_VERTEX);

	bool success = cubemap_filter.shader.version_bind_shader(cubemap_filter.shader_version, mode);
	if (!success) {
		glDisableVertexAttribArray(RS::ARRAY_VERTEX);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		return;
	}

	if (p_layer > 0) {
		const uint32_t sample_counts[5] = { 1, ggx_samples / 16, ggx_samples / 8, ggx_samples / 4, ggx_samples };
		uint32_t sample_count = sample_counts[MIN(4, p_layer)];

		float roughness = float(p_layer) / (p_mipmap_count - 1);
		roughness *= roughness; // Convert to non-perceptual roughness.
		float roughness4 = roughness * roughness;
		roughness4 *= roughness4;

		float solid_angle_texel = 4.0 * Math_PI / float(6 * size * size);

		LocalVector<float> sample_directions;
		sample_directions.resize(4 * sample_count);

		uint32_t index = 0;
		float weight = 0.0;
		for (uint32_t i = 0; i < sample_count; i++) {
			Vector2 xi = hammersley(i, sample_count);
			Vector3 dir = importance_sample_GGX(xi, roughness4);
			Vector3 light_vec = (2.0 * dir.z * dir - Vector3(0.0, 0.0, 1.0));

			if (light_vec.z <= 0.0) {
				continue;
			}

			sample_directions[index * 4] = light_vec.x;
			sample_directions[index * 4 + 1] = light_vec.y;
			sample_directions[index * 4 + 2] = light_vec.z;

			float D = distribution_GGX(dir.z, roughness4);
			float pdf = D * dir.z / (4.0 * dir.z) + 0.0001;

			float solid_angle_sample = 1.0 / (float(sample_count) * pdf + 0.0001);

			float mip_level = MAX(0.5 * log2(solid_angle_sample / solid_angle_texel) + float(MAX(1, p_layer - 3)), 1.0);

			sample_directions[index * 4 + 3] = mip_level;
			weight += light_vec.z;
			index++;
		}

		glUniform4fv(cubemap_filter.shader.version_get_uniform(CubemapFilterShaderGLES2::SAMPLE_DIRECTIONS_MIP, cubemap_filter.shader_version, mode), sample_count, sample_directions.ptr());
		cubemap_filter.shader.version_set_uniform(CubemapFilterShaderGLES2::WEIGHT, weight, cubemap_filter.shader_version, mode);
		cubemap_filter.shader.version_set_uniform(CubemapFilterShaderGLES2::SAMPLE_COUNT, (int32_t)index, cubemap_filter.shader_version, mode);
	}

	for (int i = 0; i < 6; i++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, p_dest_cubemap, p_layer);
#ifdef DEBUG_ENABLED
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			WARN_PRINT("Could not bind sky radiance face: " + itos(i) + ", status: " + GLES2::TextureStorage::get_singleton()->get_framebuffer_error(status));
		}
#endif
		cubemap_filter.shader.version_set_uniform(CubemapFilterShaderGLES2::FACE_ID, i, cubemap_filter.shader_version, mode);

		glDrawArrays(GL_TRIANGLES, 0, 3);
		GL_CHECK_ERROR("GLES2::CubemapFilter::filter_radiance: glDrawArrays");
	}

	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, GLES2::TextureStorage::system_fbo);
	GL_CHECK_ERROR("GLES2::CubemapFilter::filter_radiance: glBindFramebuffer system_fbo");
}

#endif // GLES2_ENABLED
