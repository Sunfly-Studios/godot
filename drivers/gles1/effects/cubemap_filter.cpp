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

#ifdef GLES1_ENABLED

#include "cubemap_filter.h"

#include "drivers/gles1/storage/texture_storage.h"
#include "core/config/project_settings.h"

using namespace GLES1;

CubemapFilter *CubemapFilter::singleton = nullptr;

CubemapFilter::CubemapFilter() {
	singleton = this;
}

CubemapFilter::~CubemapFilter() {
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

static Vector3 texelCoordToVec(Vector2 uv, int faceID) {
	Vector3 faceUvVectors[6][3]{};
	faceUvVectors[1][0] = Vector3(0.0, 0.0, 1.0);
	faceUvVectors[1][1] = Vector3(0.0, -1.0, 0.0);
	faceUvVectors[1][2] = Vector3(-1.0, 0.0, 0.0);
	faceUvVectors[0][0] = Vector3(0.0, 0.0, -1.0);
	faceUvVectors[0][1] = Vector3(0.0, -1.0, 0.0);
	faceUvVectors[0][2] = Vector3(1.0, 0.0, 0.0);
	faceUvVectors[3][0] = Vector3(1.0, 0.0, 0.0);
	faceUvVectors[3][1] = Vector3(0.0, 0.0, -1.0);
	faceUvVectors[3][2] = Vector3(0.0, -1.0, 0.0);
	faceUvVectors[2][0] = Vector3(1.0, 0.0, 0.0);
	faceUvVectors[2][1] = Vector3(0.0, 0.0, 1.0);
	faceUvVectors[2][2] = Vector3(0.0, 1.0, 0.0);
	faceUvVectors[5][0] = Vector3(-1.0, 0.0, 0.0);
	faceUvVectors[5][1] = Vector3(0.0, -1.0, 0.0);
	faceUvVectors[5][2] = Vector3(0.0, 0.0, -1.0);
	faceUvVectors[4][0] = Vector3(1.0, 0.0, 0.0);
	faceUvVectors[4][1] = Vector3(0.0, -1.0, 0.0);
	faceUvVectors[4][2] = Vector3(0.0, 0.0, 1.0);

	Vector3 result = (faceUvVectors[faceID][0] * uv.x) + (faceUvVectors[faceID][1] * uv.y) + faceUvVectors[faceID][2];
	return result.normalized();
}

static Color sample_cubemap(const LocalVector<uint8_t> *faces, int base_size, Vector3 L) {
	Vector3 abs_L = L.abs();
	int face = 0;
	Vector2 uv;
	if (abs_L.x >= abs_L.y && abs_L.x >= abs_L.z) {
		face = L.x > 0 ? 0 : 1;
		uv.x = L.x > 0 ? -L.z : L.z;
		uv.y = -L.y;
		uv /= abs_L.x;
	} else if (abs_L.y >= abs_L.x && abs_L.y >= abs_L.z) {
		face = L.y > 0 ? 2 : 3;
		uv.x = L.x;
		uv.y = L.y > 0 ? L.z : -L.z;
		uv /= abs_L.y;
	} else {
		face = L.z > 0 ? 4 : 5;
		uv.x = L.z > 0 ? L.x : -L.x;
		uv.y = -L.y;
		uv /= abs_L.z;
	}
	uv = uv * 0.5 + Vector2(0.5, 0.5);
	int px = CLAMP((int)(uv.x * base_size), 0, base_size - 1);
	int py = CLAMP((int)(uv.y * base_size), 0, base_size - 1);
	int idx = (py * base_size + px) * 4;
	return Color(faces[face][idx] / 255.0, faces[face][idx + 1] / 255.0, faces[face][idx + 2] / 255.0, faces[face][idx + 3] / 255.0);
}

void CubemapFilter::filter_radiance(GLuint p_source_cubemap, GLuint p_dest_cubemap, GLuint p_dest_framebuffer, int p_source_size, int p_mipmap_count, int p_layer) {
	if (!GLES1_CONFIG->support_fbo) {
		return;
	}
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();

	texture_storage->bind_framebuffer(p_dest_framebuffer);

	if (p_layer == 0) {
		// Fast path: Just copy the base level by reading pixels and re-uploading via FBO bounds
		LocalVector<uint8_t> pixels;
		pixels.resize(p_source_size * p_source_size * 4);
		for (int i = 0; i < 6; i++) {
			glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, p_source_cubemap, 0);
			glReadPixels(0, 0, p_source_size, p_source_size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.ptr());

			glBindTexture(GL_TEXTURE_CUBE_MAP, p_dest_cubemap);
			glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, 0, 0, p_source_size, p_source_size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.ptr());
		}

		if (GLES1_CONFIG->support_generate_mipmap) {
			glBindTexture(GL_TEXTURE_CUBE_MAP, p_dest_cubemap);
			glGenerateMipmapOES(GL_TEXTURE_CUBE_MAP_OES);
			GL_CHECK_ERROR("GLES1::CubemapFilter::filter_radiance: glGenerateMipmapOES");
		}

		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
		texture_storage->bind_framebuffer_system();
		return;
	}

	int size = p_source_size >> p_layer;

	// Read base layer for CPU sampling
	LocalVector<uint8_t> base_cubemap[6];
	for (int i = 0; i < 6; i++) {
		base_cubemap[i].resize(p_source_size * p_source_size * 4);
		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, p_source_cubemap, 0);
		glReadPixels(0, 0, p_source_size, p_source_size, GL_RGBA, GL_UNSIGNED_BYTE, base_cubemap[i].ptr());
	}

	const uint32_t sample_counts[5] = { 1, ggx_samples / 16, ggx_samples / 8, ggx_samples / 4, ggx_samples };
	uint32_t sample_count = sample_counts[MIN(4, p_layer)];

	float roughness = float(p_layer) / (p_mipmap_count - 1);
	roughness *= roughness;
	float roughness4 = roughness * roughness;
	roughness4 *= roughness4;

	LocalVector<uint8_t> dest_pixels;
	dest_pixels.resize(size * size * 4);

	for (int face_id = 0; face_id < 6; face_id++) {
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				Vector2 uv = Vector2(x + 0.5f, y + 0.5f) / float(size);
				uv = uv * 2.0f - Vector2(1.0f, 1.0f);

				Vector3 N = texelCoordToVec(uv, face_id);
				Vector3 UpVector = Math::abs(N.z) < 0.999f ? Vector3(0.0, 0.0, 1.0) : Vector3(1.0, 0.0, 0.0);
				Vector3 T_x = UpVector.cross(N).normalized();
				Vector3 T_y = N.cross(T_x);
				Vector3 T_z = N;

				Color sum;
				float weight = 0.0f;

				for (uint32_t i = 0; i < sample_count; i++) {
					Vector2 xi = hammersley(i, sample_count);
					Vector3 dir = importance_sample_GGX(xi, roughness4);
					Vector3 light_vec = (2.0f * dir.z * dir - Vector3(0.0f, 0.0f, 1.0f));

					if (light_vec.z <= 0.0f) {
						continue;
					}

					Vector3 L = T_x * light_vec.x + T_y * light_vec.y + T_z * light_vec.z;
					L.normalize();

					Color val = sample_cubemap(base_cubemap, p_source_size, L);
					val = val.srgb_to_linear();

					sum += val * light_vec.z;
					weight += light_vec.z;
				}

				if (weight > 0.0f) {
					sum.r /= weight;
					sum.g /= weight;
					sum.b /= weight;
					sum.a /= weight;
				}
				sum = sum.linear_to_srgb();

				int idx = (y * size + x) * 4;
				dest_pixels[idx] = static_cast<uint8_t>(CLAMP(sum.r * 255.0f, 0.0f, 255.0f));
				dest_pixels[idx + 1] = static_cast<uint8_t>(CLAMP(sum.g * 255.0f, 0.0f, 255.0f));
				dest_pixels[idx + 2] = static_cast<uint8_t>(CLAMP(sum.b * 255.0f, 0.0f, 255.0f));
				dest_pixels[idx + 3] = 255;
			}
		}

		glBindTexture(GL_TEXTURE_CUBE_MAP, p_dest_cubemap);
		glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face_id, p_layer, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, dest_pixels.ptr());
	}

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	texture_storage->bind_framebuffer_system();
	GL_CHECK_ERROR("GLES1::CubemapFilter::filter_radiance: CPU Fallback completed");
}

#endif // GLES1_ENABLED
