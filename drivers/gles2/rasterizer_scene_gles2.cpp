/**************************************************************************/
/*  rasterizer_scene_gles2.cpp                                            */
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

#include "rasterizer_scene_gles2.h"

#include "drivers/gles2/effects/copy_effects.h"
#include "drivers/gles2/effects/feed_effects.h"
#include "rasterizer_gles2.h"
#include "storage/config.h"
#include "storage/mesh_storage.h"
#include "storage/particles_storage.h"
#include "storage/texture_storage.h"

#include "core/config/project_settings.h"
#include "core/templates/sort_array.h"
#include "servers/camera/camera_feed.h"
#include "servers/camera_server.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"

#ifdef GLES2_ENABLED

RasterizerSceneGLES2 *RasterizerSceneGLES2::singleton = nullptr;

/* STATIC */

static GLuint _init_radiance_texture_gles2(int p_size, int p_mipmaps, String p_name) {
	GLuint tex;
	glGenTextures(1, &tex);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_init_radiance_texture_gles2: glGenTextures");

	glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

	int size = p_size;
	int total_size = 0;
	for (int mip = 0; mip <= p_mipmaps; mip++) {
		for (int i = 0; i < 6; i++) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		}
		total_size += size * size * 4 * 6;
		size = MAX(1, size >> 1);
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_init_radiance_texture_gles2: glTexImage2D");

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, p_mipmaps > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	GLES2::Utilities::get_singleton()->texture_allocated_data(tex, total_size, p_name);

	return tex;
}

static const Vector3 view_normals[6] = {
	Vector3(+1, 0, 0), Vector3(-1, 0, 0), Vector3(0, +1, 0),
	Vector3(0, -1, 0), Vector3(0, 0, +1), Vector3(0, 0, -1)
};
static const Vector3 view_up[6] = {
	Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, +1),
	Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0)
};
static constexpr GLenum prim[5] = { GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP };

/* STATIC BATCH API HELPERS */

static _FORCE_INLINE_ void _batch_decode_multimesh_instance(const float *p_data, RS::MultimeshTransformFormat p_format, bool p_uses_colors, uint32_t p_color_offset, Transform3D &r_xform, Color &r_color) {
	if (p_format == RS::MULTIMESH_TRANSFORM_3D) {
		r_xform.basis.rows[0][0] = p_data[0];
		r_xform.basis.rows[0][1] = p_data[1];
		r_xform.basis.rows[0][2] = p_data[2];
		r_xform.origin.x = p_data[3];
		r_xform.basis.rows[1][0] = p_data[4];
		r_xform.basis.rows[1][1] = p_data[5];
		r_xform.basis.rows[1][2] = p_data[6];
		r_xform.origin.y = p_data[7];
		r_xform.basis.rows[2][0] = p_data[8];
		r_xform.basis.rows[2][1] = p_data[9];
		r_xform.basis.rows[2][2] = p_data[10];
		r_xform.origin.z = p_data[11];
	} else {
		r_xform.basis.rows[0][0] = p_data[0];
		r_xform.basis.rows[0][1] = p_data[1];
		r_xform.origin.x = p_data[3];
		r_xform.basis.rows[1][0] = p_data[4];
		r_xform.basis.rows[1][1] = p_data[5];
		r_xform.origin.y = p_data[7];
	}

	if (p_uses_colors) {
		const float *cdata = p_data + p_color_offset;
		r_color = Color(cdata[0], cdata[1], cdata[2], cdata[3]);
	} else {
		r_color = Color(1.0, 1.0, 1.0, 1.0);
	}
}

static _FORCE_INLINE_ void _batch_fill_vertex_depth(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepth &r_bv, const uint8_t *p_v_ptr, uint64_t p_format, bool p_is_2d, const Transform3D &p_write_xform) {
	if (p_is_2d) {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], 0.0f);
	} else {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], f[2]);
	}

	r_bv.instance_xform0.set(p_write_xform.basis.rows[0][0], p_write_xform.basis.rows[0][1], p_write_xform.basis.rows[0][2], p_write_xform.origin.x);
	r_bv.instance_xform1.set(p_write_xform.basis.rows[1][0], p_write_xform.basis.rows[1][1], p_write_xform.basis.rows[1][2], p_write_xform.origin.y);
	r_bv.instance_xform2.set(p_write_xform.basis.rows[2][0], p_write_xform.basis.rows[2][1], p_write_xform.basis.rows[2][2], p_write_xform.origin.z);
}

static _FORCE_INLINE_ void _batch_fill_vertex_depth_alpha(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepthAlpha &r_bv, const uint8_t *p_v_ptr, const uint8_t *p_a_ptr, uint64_t p_format, bool p_is_2d, bool p_is_compressed, const Transform3D &p_write_xform) {
	if (p_is_2d) {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], 0.0f);
	} else {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], f[2]);
	}

	if (p_a_ptr) {
		int attr_offset = (p_format & RS::ARRAY_FORMAT_COLOR) ? 4 : 0;
		if (p_format & RS::ARRAY_FORMAT_TEX_UV) {
			if (p_is_compressed) {
				const uint16_t *uv_s = (const uint16_t *)(p_a_ptr + attr_offset);
				r_bv.uv.set(Math::half_to_float(uv_s[0]), Math::half_to_float(uv_s[1]));
			} else {
				const float *uv_f = (const float *)(p_a_ptr + attr_offset);
				r_bv.uv.set(uv_f[0], uv_f[1]);
			}
		} else {
			r_bv.uv.set(0.0f, 0.0f);
		}
	} else {
		r_bv.uv.set(0.0f, 0.0f);
	}

	r_bv.instance_xform0.set(p_write_xform.basis.rows[0][0], p_write_xform.basis.rows[0][1], p_write_xform.basis.rows[0][2], p_write_xform.origin.x);
	r_bv.instance_xform1.set(p_write_xform.basis.rows[1][0], p_write_xform.basis.rows[1][1], p_write_xform.basis.rows[1][2], p_write_xform.origin.y);
	r_bv.instance_xform2.set(p_write_xform.basis.rows[2][0], p_write_xform.basis.rows[2][1], p_write_xform.basis.rows[2][2], p_write_xform.origin.z);
}

static _FORCE_INLINE_ void _batch_fill_vertex(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3D &r_bv, const uint8_t *p_v_ptr, const uint8_t *p_n_ptr, const uint8_t *p_a_ptr, uint64_t p_format, bool p_is_2d, bool p_is_compressed, const Transform3D &p_write_xform) {
	if (p_is_2d) {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], 0.0f);
	} else {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], f[2]);
	}

	if (p_format & RS::ARRAY_FORMAT_NORMAL) {
		const uint16_t *us = (const uint16_t *)p_n_ptr;
		r_bv.normal.set(us[0] / 65535.0f, us[1] / 65535.0f, 0.0f);
	} else {
		r_bv.normal.set(0.5f, 1.0f, 0.0f); // Octahedral encoding for (0, 1, 0)
	}

	if (p_a_ptr) {
		int attr_offset = 0;
		if (p_format & RS::ARRAY_FORMAT_COLOR) {
			r_bv.color.r = p_a_ptr[attr_offset + 0];
			r_bv.color.g = p_a_ptr[attr_offset + 1];
			r_bv.color.b = p_a_ptr[attr_offset + 2];
			r_bv.color.a = p_a_ptr[attr_offset + 3];
			attr_offset += 4;
		} else {
			r_bv.color.set_white();
		}

		if (p_format & RS::ARRAY_FORMAT_TEX_UV) {
			if (p_is_compressed) {
				const uint16_t *uv_s = (const uint16_t *)(p_a_ptr + attr_offset);
				r_bv.uv.set(Math::half_to_float(uv_s[0]), Math::half_to_float(uv_s[1]));
			} else {
				const float *uv_f = (const float *)(p_a_ptr + attr_offset);
				r_bv.uv.set(uv_f[0], uv_f[1]);
			}
		} else {
			r_bv.uv.set(0.0f, 0.0f);
		}
	} else {
		r_bv.color.set_white();
		r_bv.uv.set(0.0f, 0.0f);
	}

	r_bv.instance_xform0.set(p_write_xform.basis.rows[0][0], p_write_xform.basis.rows[0][1], p_write_xform.basis.rows[0][2], p_write_xform.origin.x);
	r_bv.instance_xform1.set(p_write_xform.basis.rows[1][0], p_write_xform.basis.rows[1][1], p_write_xform.basis.rows[1][2], p_write_xform.origin.y);
	r_bv.instance_xform2.set(p_write_xform.basis.rows[2][0], p_write_xform.basis.rows[2][1], p_write_xform.basis.rows[2][2], p_write_xform.origin.z);
}

static _FORCE_INLINE_ void _batch_fill_vertex_instanced(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DInstanced &r_bv, const uint8_t *p_v_ptr, const uint8_t *p_n_ptr, const uint8_t *p_a_ptr, uint64_t p_format, bool p_is_2d, bool p_is_compressed, const Transform3D &p_write_xform, const Color &p_inst_color) {
	if (p_is_2d) {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], 0.0f);
	} else {
		const float *f = (const float *)p_v_ptr;
		r_bv.pos.set(f[0], f[1], f[2]);
	}

	if (p_format & RS::ARRAY_FORMAT_NORMAL) {
		const uint16_t *us = (const uint16_t *)p_n_ptr;
		r_bv.normal.set(us[0] / 65535.0f, us[1] / 65535.0f, 0.0f);
	} else {
		r_bv.normal.set(0.5f, 1.0f, 0.0f);
	}

	if (p_a_ptr) {
		int attr_offset = 0;
		if (p_format & RS::ARRAY_FORMAT_COLOR) {
			r_bv.color.r = p_a_ptr[attr_offset + 0];
			r_bv.color.g = p_a_ptr[attr_offset + 1];
			r_bv.color.b = p_a_ptr[attr_offset + 2];
			r_bv.color.a = p_a_ptr[attr_offset + 3];
			attr_offset += 4;
		} else {
			r_bv.color.set_white();
		}

		if (p_format & RS::ARRAY_FORMAT_TEX_UV) {
			if (p_is_compressed) {
				const uint16_t *uv_s = (const uint16_t *)(p_a_ptr + attr_offset);
				r_bv.uv.set(Math::half_to_float(uv_s[0]), Math::half_to_float(uv_s[1]));
			} else {
				const float *uv_f = (const float *)(p_a_ptr + attr_offset);
				r_bv.uv.set(uv_f[0], uv_f[1]);
			}
		} else {
			r_bv.uv.set(0.0f, 0.0f);
		}
	} else {
		r_bv.color.set_white();
		r_bv.uv.set(0.0f, 0.0f);
	}

	r_bv.instance_xform0.set(p_write_xform.basis.rows[0][0], p_write_xform.basis.rows[0][1], p_write_xform.basis.rows[0][2], p_write_xform.origin.x);
	r_bv.instance_xform1.set(p_write_xform.basis.rows[1][0], p_write_xform.basis.rows[1][1], p_write_xform.basis.rows[1][2], p_write_xform.origin.y);
	r_bv.instance_xform2.set(p_write_xform.basis.rows[2][0], p_write_xform.basis.rows[2][1], p_write_xform.basis.rows[2][2], p_write_xform.origin.z);
	r_bv.instance_color_custom_data.set(p_inst_color);
}

void RasterizerSceneGLES2::initialize() {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	GLES2::Config *config = GLES2::Config::get_singleton();

	batch_initialize();

	cull_argument.set_page_pool(&cull_argument_pool);

	{
		String global_defines;
		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
		global_defines += "\n#define MAX_LIGHT_DATA_STRUCTS " + itos(config->max_renderable_lights) + "\n";
		global_defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(MAX_DIRECTIONAL_LIGHTS) + "\n";
		global_defines += "\n#define MAX_FORWARD_LIGHTS " + itos(config->max_lights_per_object) + "\n";
		global_defines += "\n#define MAX_ROUGHNESS_LOD " + itos(sky_globals.roughness_layers - 1) + ".0\n";
		if (config->force_vertex_shading) {
			global_defines += "\n#define USE_VERTEX_LIGHTING\n";
		}
		if (!config->multi_bounce_occlusion) {
			global_defines += "\n#define MULTI_BOUNCE_OCCLUSION_DISABLED\n";
		}
		material_storage->shaders.scene_shader.initialize(global_defines);
		scene_globals.shader_default_version = material_storage->shaders.scene_shader.version_create();
		material_storage->shaders.scene_shader.version_bind_shader(scene_globals.shader_default_version, SceneShaderGLES2::MODE_COLOR);
	}

	{
		// Setup Lights
		config->max_renderable_lights = MIN(config->max_renderable_lights, 256);
		config->max_lights_per_object = MIN(config->max_lights_per_object, config->max_renderable_lights);

		scene_state.omni_lights = memnew_arr(LightData, config->max_renderable_lights);
		scene_state.omni_light_sort = memnew_arr(InstanceSort<GLES2::LightInstance>, config->max_renderable_lights);
		scene_state.spot_lights = memnew_arr(LightData, config->max_renderable_lights);
		scene_state.spot_light_sort = memnew_arr(InstanceSort<GLES2::LightInstance>, config->max_renderable_lights);
		scene_state.directional_lights = memnew_arr(DirectionalLightData, MAX_DIRECTIONAL_LIGHTS);

		scene_state.positional_shadows = memnew_arr(ShadowData, config->max_renderable_lights * 2);
		scene_state.directional_shadows = memnew_arr(DirectionalShadowData, MAX_DIRECTIONAL_LIGHTS);
	}

	{
		// Initialize Sky stuff
		sky_globals.roughness_layers = GLOBAL_GET("rendering/reflections/sky_reflections/roughness_layers");

		String global_defines;
		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
		global_defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(sky_globals.max_directional_lights) + "\n";
		material_storage->shaders.sky_shader.initialize(global_defines);
		sky_globals.shader_default_version = material_storage->shaders.sky_shader.version_create();
	}

	{
		sky_globals.default_shader = material_storage->shader_allocate();
		sky_globals.max_directional_lights = 4;
		sky_globals.directional_lights = memnew_arr(DirectionalLightData, sky_globals.max_directional_lights);
		ERR_FAIL_NULL(sky_globals.directional_lights);
		sky_globals.last_frame_directional_lights = memnew_arr(DirectionalLightData, sky_globals.max_directional_lights);
		if (unlikely(!sky_globals.last_frame_directional_lights)) {
			memdelete_arr(sky_globals.directional_lights);
			sky_globals.directional_lights = nullptr;
			ERR_FAIL_MSG("Out of memory in initialisation of RasterizerSceneGLES3");
		}
		sky_globals.last_frame_directional_light_count = sky_globals.max_directional_lights + 1;

		material_storage->shader_initialize(sky_globals.default_shader);

		material_storage->shader_set_code(sky_globals.default_shader, R"(
// Default sky shader.

shader_type sky;

void sky() {
	COLOR = vec3(0.0);
}
)");
		sky_globals.default_material = material_storage->material_allocate();
		material_storage->material_initialize(sky_globals.default_material);

		material_storage->material_set_shader(sky_globals.default_material, sky_globals.default_shader);
	}
	{
		sky_globals.fog_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(sky_globals.fog_shader);

		material_storage->shader_set_code(sky_globals.fog_shader, R"(
// Default clear color sky shader.

shader_type sky;

uniform vec4 clear_color;

void sky() {
	COLOR = clear_color.rgb;
}
)");
		sky_globals.fog_material = material_storage->material_allocate();
		material_storage->material_initialize(sky_globals.fog_material);

		material_storage->material_set_shader(sky_globals.fog_material, sky_globals.fog_shader);
	}

	{
		constexpr float qv[6] = {
			-1.0f, -1.0f,
			 3.0f, -1.0f,
			-1.0f,  3.0f,
		};

		glGenBuffers(1, &sky_globals.screen_triangle);
		glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, qv, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// _draw_sky handles fallback internally
		sky_globals.screen_triangle_array = 0;

		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::initialize: sky screen triangle setup");
	}

	{
		// Default 3D material and shader
		scene_globals.default_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(scene_globals.default_shader);
		material_storage->shader_set_code(scene_globals.default_shader, R"(
// Default 3D material shader (Compatibility).

shader_type spatial;

void vertex() {
	ROUGHNESS = 0.8;
}

void fragment() {
	ALBEDO = vec3(0.6);
	ROUGHNESS = 0.8;
	METALLIC = 0.2;
}
)");
		scene_globals.default_material = material_storage->material_allocate();
		material_storage->material_initialize(scene_globals.default_material);
		material_storage->material_set_shader(scene_globals.default_material, scene_globals.default_shader);
	}

	{
		// Overdraw material and shader
		scene_globals.overdraw_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(scene_globals.overdraw_shader);
		material_storage->shader_set_code(scene_globals.overdraw_shader, R"(
// 3D editor Overdraw debug draw mode shader (Compatibility).

shader_type spatial;

render_mode blend_add, unshaded, fog_disabled;

void fragment() {
	ALBEDO = vec3(0.4, 0.8, 0.8);
	ALPHA = 0.2;
}
)");
		scene_globals.overdraw_material = material_storage->material_allocate();
		material_storage->material_initialize(scene_globals.overdraw_material);
		material_storage->material_set_shader(scene_globals.overdraw_material, scene_globals.overdraw_shader);
	}

	// MultiMesh may read from color when color is disabled, so make sure that the color defaults to white instead of black.
	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
}

RenderGeometryInstance *RasterizerSceneGLES2::geometry_instance_create(RID p_base) {
	RS::InstanceType type = RSG::utilities->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RS::INSTANCE_GEOMETRY_MASK), nullptr);

	GeometryInstanceGLES2 *ginstance = geometry_instance_alloc.alloc();
	ginstance->data = memnew(GeometryInstanceGLES2::Data);

	ERR_FAIL_NULL_V(ginstance->data, nullptr);

	ginstance->data->base = p_base;
	ginstance->data->base_type = type;
	ginstance->data->dependency_tracker.userdata = ginstance;
	ginstance->data->dependency_tracker.changed_callback = _geometry_instance_dependency_changed;
	ginstance->data->dependency_tracker.deleted_callback = _geometry_instance_dependency_deleted;

	ginstance->_mark_dirty();

	return ginstance;
}

uint32_t RasterizerSceneGLES2::geometry_instance_get_pair_mask() {
	return ((1 << RS::INSTANCE_LIGHT) | (1 << RS::INSTANCE_REFLECTION_PROBE));
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::pair_light_instances(const RID *p_light_instances, uint32_t p_light_instance_count) {
	GLES2::Config *config = GLES2::Config::get_singleton();

	paired_omni_light_count = 0;
	paired_spot_light_count = 0;
	paired_omni_lights.clear();
	paired_spot_lights.clear();

	for (uint32_t i = 0; i < p_light_instance_count; i++) {
		RS::LightType type = GLES2::LightStorage::get_singleton()->light_instance_get_type(p_light_instances[i]);
		switch (type) {
			case RS::LIGHT_OMNI: {
				if (paired_omni_light_count < (uint32_t)config->max_lights_per_object) {
					paired_omni_lights.push_back(p_light_instances[i]);
					paired_omni_light_count++;
				}
			} break;
			case RS::LIGHT_SPOT: {
				if (paired_spot_light_count < (uint32_t)config->max_lights_per_object) {
					paired_spot_lights.push_back(p_light_instances[i]);
					paired_spot_light_count++;
				}
			} break;
			default:
				break;
		}
	}
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) {
	paired_reflection_probes.clear();

	for (uint32_t i = 0; i < p_reflection_probe_instance_count; i++) {
		paired_reflection_probes.push_back(p_reflection_probe_instances[i]);
	}
}

void RasterizerSceneGLES2::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_geometry_instance);
	ERR_FAIL_NULL(ginstance);
	GeometryInstanceSurface *surf = ginstance->surface_caches;
	while (surf) {
		GeometryInstanceSurface *next = surf->next;
		geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	memdelete(ginstance->data);
	geometry_instance_alloc.free(ginstance);
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::_mark_dirty() {
	if (dirty_list_element.in_list()) {
		return;
	}

	// Clear surface caches
	GeometryInstanceSurface *surf = surface_caches;
	while (surf) {
		GeometryInstanceSurface *next = surf->next;
		RasterizerSceneGLES2::get_singleton()->geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	surface_caches = nullptr;

	RasterizerSceneGLES2::get_singleton()->geometry_instance_dirty_list.add(&dirty_list_element);
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) {

}

void RasterizerSceneGLES2::GeometryInstanceGLES2::set_lightmap_capture(const Color *p_sh9) {

}

RasterizerSceneGLES2::MultiMeshInstanceData RasterizerSceneGLES2::_get_multimesh_data(const GeometryInstanceSurface *p_surface) {
	MultiMeshInstanceData mm;
	if (p_surface->owner && p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH) {
		GLES2::MultiMesh *multimesh = GLES2::MeshStorage::get_singleton()->get_multimesh(p_surface->owner->data->base);
		if (multimesh) {
			mm.data = multimesh->data_cache.ptr();
			mm.stride = multimesh->stride_cache;
			mm.color_offset = multimesh->color_offset_cache;
			mm.uses_colors = multimesh->uses_colors;
			mm.format = multimesh->xform_format;
		}
	}
	return mm;
}

void RasterizerSceneGLES2::_update_dirty_geometry_instances() {
	while (geometry_instance_dirty_list.first()) {
		_geometry_instance_update(geometry_instance_dirty_list.first()->self());
	}
}

void RasterizerSceneGLES2::_geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker) {
	switch (p_notification) {
		case Dependency::DEPENDENCY_CHANGED_MATERIAL:
		case Dependency::DEPENDENCY_CHANGED_MESH:
		case Dependency::DEPENDENCY_CHANGED_PARTICLES:
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH:
		case Dependency::DEPENDENCY_CHANGED_SKELETON_DATA: {
			static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
			static_cast<GeometryInstanceGLES2 *>(p_tracker->userdata)->data->dirty_dependencies = true;
		} break;
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES: {
			GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_tracker->userdata);
			if (ginstance->data->base_type == RS::INSTANCE_MULTIMESH) {
				ginstance->instance_count = GLES2::MeshStorage::get_singleton()->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		default: {
			//rest of notifications of no interest
		} break;
	}
}

void RasterizerSceneGLES2::_geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker) {
	static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
	static_cast<GeometryInstanceGLES2 *>(p_tracker->userdata)->data->dirty_dependencies = true;
}

void RasterizerSceneGLES2::_geometry_instance_add_surface_with_material(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh) {

}

void RasterizerSceneGLES2::_geometry_instance_add_surface_with_material_chain(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material_data, RID p_mat_src, RID p_mesh) {

}

void RasterizerSceneGLES2::_geometry_instance_add_surface(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, RID p_material, RID p_mesh) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	RID m_src = ginstance->data->material_override.is_valid() ? ginstance->data->material_override : p_material;
	GLES2::SceneMaterialData *material_data = nullptr;

	if (m_src.is_valid()) {
		material_data = static_cast<GLES2::SceneMaterialData *>(material_storage->material_get_data(m_src, RS::SHADER_SPATIAL));
		if (!material_data || !material_data->shader_data->valid) {
			material_data = nullptr;
		}
	}

	if (!material_data) {
		material_data = static_cast<GLES2::SceneMaterialData *>(material_storage->material_get_data(scene_globals.default_material, RS::SHADER_SPATIAL));
		m_src = scene_globals.default_material;
	}

	if (!material_data) {
		return;
	}

	GeometryInstanceSurface *surf = geometry_instance_surface_alloc.alloc();

	bool has_read_screen_alpha = material_data->shader_data->uses_screen_texture || material_data->shader_data->uses_depth_texture || material_data->shader_data->uses_normal_texture;
	bool has_base_alpha = ((material_data->shader_data->uses_alpha && !material_data->shader_data->uses_alpha_clip) || has_read_screen_alpha);
	bool has_blend_alpha = material_data->shader_data->uses_blend_alpha;
	bool has_alpha = has_base_alpha || has_blend_alpha;

	uint32_t flags = 0;
	if (material_data->shader_data->uses_screen_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_SCREEN_TEXTURE;
	}
	if (material_data->shader_data->uses_depth_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_DEPTH_TEXTURE;
	}
	if (material_data->shader_data->uses_normal_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_NORMAL_TEXTURE;
	}

	if (has_alpha || has_read_screen_alpha || material_data->shader_data->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_DISABLED || material_data->shader_data->depth_test == GLES2::SceneShaderData::DEPTH_TEST_DISABLED) {
		flags |= GeometryInstanceSurface::FLAG_PASS_ALPHA;
		if (material_data->shader_data->uses_depth_prepass_alpha && !(material_data->shader_data->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_DISABLED || material_data->shader_data->depth_test == GLES2::SceneShaderData::DEPTH_TEST_DISABLED)) {
			flags |= GeometryInstanceSurface::FLAG_PASS_DEPTH;
		}
	} else {
		flags |= GeometryInstanceSurface::FLAG_PASS_OPAQUE;
		flags |= GeometryInstanceSurface::FLAG_PASS_DEPTH;
	}

	surf->flags = flags;
	surf->shader = material_data->shader_data;
	surf->material = material_data;
	surf->surface = GLES2::MeshStorage::get_singleton()->mesh_get_surface(p_mesh, p_surface);
	surf->primitive = GLES2::MeshStorage::get_singleton()->mesh_surface_get_primitive(surf->surface);
	surf->surface_index = p_surface;

	surf->owner = ginstance;
	surf->next = ginstance->surface_caches;
	ginstance->surface_caches = surf;

	surf->sort.sort_key1 = 0;
	surf->sort.sort_key2 = 0;
	surf->sort.priority = material_data->priority;
}

void RasterizerSceneGLES2::_geometry_instance_update(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_geometry_instance);
	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();
	GLES2::ParticlesStorage *particles_storage = GLES2::ParticlesStorage::get_singleton();

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_begin();
	}

	switch (ginstance->data->base_type) {
		case RS::INSTANCE_MESH: {
			const RID *materials = nullptr;
			uint32_t surface_count;
			RID mesh = ginstance->data->base;

			materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
			if (materials) {
				const RID *inst_materials = ginstance->data->surface_materials.ptr();
				uint32_t surf_mat_count = ginstance->data->surface_materials.size();

				for (uint32_t j = 0; j < surface_count; j++) {
					RID material = (j < surf_mat_count && inst_materials[j].is_valid()) ? inst_materials[j] : materials[j];
					_geometry_instance_add_surface(ginstance, j, material, mesh);
				}
			}
			ginstance->instance_count = -1;
		} break;
		case RS::INSTANCE_MULTIMESH: {
			RID mesh = mesh_storage->multimesh_get_mesh(ginstance->data->base);
			if (mesh.is_valid()) {
				const RID *materials = nullptr;
				uint32_t surface_count;

				materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t j = 0; j < surface_count; j++) {
						_geometry_instance_add_surface(ginstance, j, materials[j], mesh);
					}
				}
				ginstance->instance_count = mesh_storage->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		case RS::INSTANCE_PARTICLES: {
			int draw_passes = particles_storage->particles_get_draw_passes(ginstance->data->base);
			for (int j = 0; j < draw_passes; j++) {
				RID mesh = particles_storage->particles_get_draw_pass_mesh(ginstance->data->base, j);
				if (!mesh.is_valid()) {
					continue;
				}
				const RID *materials = nullptr;
				uint32_t surface_count;
				materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t k = 0; k < surface_count; k++) {
						_geometry_instance_add_surface(ginstance, k, materials[k], mesh);
					}
				}
			}
			ginstance->instance_count = particles_storage->particles_get_amount(ginstance->data->base);
		} break;
		default: {
		}
	}

	ginstance->store_transform_cache = true;

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_end();
		ginstance->data->dirty_dependencies = false;
	}

	ginstance->dirty_list_element.remove_from_list();
}

/* SKY API */

void RasterizerSceneGLES2::_free_sky_data(Sky *p_sky) {
	if (p_sky->radiance != 0) {
		GLES2::Utilities::get_singleton()->texture_free_data(p_sky->radiance);
		p_sky->radiance = 0;
		GLES2::Utilities::get_singleton()->texture_free_data(p_sky->raw_radiance);
		p_sky->raw_radiance = 0;
		glDeleteFramebuffers(1, &p_sky->radiance_framebuffer);
		p_sky->radiance_framebuffer = 0;
	}
}

RID RasterizerSceneGLES2::sky_allocate() {
	return sky_owner.allocate_rid();
}

void RasterizerSceneGLES2::sky_initialize(RID p_rid) {
	sky_owner.initialize_rid(p_rid);
}

void RasterizerSceneGLES2::sky_set_radiance_size(RID p_sky, int p_radiance_size) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);
	ERR_FAIL_COND_MSG(p_radiance_size < 32 || p_radiance_size > 2048, "Sky radiance size must be between 32 and 2048");

	if (sky->radiance_size == p_radiance_size) {
		return; // No need to update
	}

	sky->radiance_size = p_radiance_size;

	_free_sky_data(sky);
	_invalidate_sky(sky);
}

void RasterizerSceneGLES2::sky_set_mode(RID p_sky, RS::SkyMode p_mode) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);

	if (sky->mode == p_mode) {
		return;
	}

	sky->mode = p_mode;
	_invalidate_sky(sky);
}

void RasterizerSceneGLES2::sky_set_material(RID p_sky, RID p_material) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);

	if (sky->material == p_material) {
		return;
	}

	sky->material = p_material;
	_invalidate_sky(sky);
}

float RasterizerSceneGLES2::sky_get_baked_exposure(RID p_sky) const {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL_V(sky, 1.0);

	return sky->baked_exposure;
}

void RasterizerSceneGLES2::_invalidate_sky(Sky *p_sky) {
	if (!p_sky->dirty) {
		p_sky->dirty = true;
		p_sky->dirty_list = dirty_sky_list;
		dirty_sky_list = p_sky;
	}
}

void RasterizerSceneGLES2::_update_dirty_skys() {
	Sky *sky = dirty_sky_list;

	while (sky) {
		if (sky->radiance == 0) {
			sky->mipmap_count = Image::get_image_required_mipmaps(sky->radiance_size, sky->radiance_size, Image::FORMAT_RGBA8) - 1;
			// Left uninitialized, will attach a texture at render time
			glGenFramebuffers(1, &sky->radiance_framebuffer);
			sky->radiance = _init_radiance_texture_gles2(sky->radiance_size, sky->mipmap_count, "Sky radiance texture");
			sky->raw_radiance = _init_radiance_texture_gles2(sky->radiance_size, sky->mipmap_count, "Sky raw radiance texture");
		}

		sky->reflection_dirty = true;
		sky->processing_layer = 0;

		Sky *next = sky->dirty_list;
		sky->dirty_list = nullptr;
		sky->dirty = false;
		sky = next;
	}

	dirty_sky_list = nullptr;
}

void RasterizerSceneGLES2::_setup_sky(const RenderDataGLES2 *p_render_data, const PagedArray<RID> &p_lights, const Projection &p_projection, const Transform3D &p_transform, const Size2i p_screen_size) {
	GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	if (!is_environment(p_render_data->environment)) {
		return;
	}

	GLES2::SkyMaterialData *material = nullptr;
	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_render_data->environment));

	RID sky_material;
	GLES2::SkyShaderData *shader_data = nullptr;

	if (sky) {
		sky_material = sky->material;

		if (sky_material.is_valid()) {
			material = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material || !material->shader_data->valid) {
				material = nullptr;
			}
		}

		if (!material) {
			sky_material = sky_globals.default_material;
			material = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}

		ERR_FAIL_COND(!material);

		shader_data = material->shader_data;
		ERR_FAIL_COND(!shader_data);

		if (shader_data->uses_time && time - sky->prev_time > 0.00001) {
			sky->prev_time = time;
			sky->reflection_dirty = true;
			RenderingServerDefault::redraw_request();
		}

		if (material != sky->prev_material) {
			sky->prev_material = material;
			sky->reflection_dirty = true;
		}

		if (material->uniform_set_updated) {
			material->uniform_set_updated = false;
			sky->reflection_dirty = true;
		}

		if (!p_transform.origin.is_equal_approx(sky->prev_position) && shader_data->uses_position) {
			sky->prev_position = p_transform.origin;
			sky->reflection_dirty = true;
		}

		if (shader_data->uses_light) {
			sky_globals.directional_light_count = 0;
			for (int i = 0; i < (int)p_lights.size(); i++) {
				GLES2::LightInstance *li = GLES2::LightStorage::get_singleton()->get_light_instance(p_lights[i]);
				if (!li) {
					continue;
				}
				RID base = li->light;

				ERR_CONTINUE(base.is_null());

				RS::LightType type = light_storage->light_get_type(base);
				if (type == RS::LIGHT_DIRECTIONAL && light_storage->light_directional_get_sky_mode(base) != RS::LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_ONLY) {
					DirectionalLightData &sky_light_data = sky_globals.directional_lights[sky_globals.directional_light_count];
					Transform3D light_transform = li->transform;
					Vector3 world_direction = light_transform.basis.xform(Vector3(0, 0, 1)).normalized();

					sky_light_data.direction[0] = world_direction.x;
					sky_light_data.direction[1] = world_direction.y;
					sky_light_data.direction[2] = world_direction.z;

					float sign = light_storage->light_is_negative(base) ? -1 : 1;
					sky_light_data.energy = sign * light_storage->light_get_param(base, RS::LIGHT_PARAM_ENERGY);

					if (is_using_physical_light_units()) {
						sky_light_data.energy *= light_storage->light_get_param(base, RS::LIGHT_PARAM_INTENSITY);
					} else {
						sky_light_data.energy *= Math_PI;
					}

					if (p_render_data->camera_attributes.is_valid()) {
						sky_light_data.energy *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
					}

					Color linear_col = light_storage->light_get_color(base);
					sky_light_data.color[0] = linear_col.r;
					sky_light_data.color[1] = linear_col.g;
					sky_light_data.color[2] = linear_col.b;

					sky_light_data.enabled = true;

					float angular_diameter = light_storage->light_get_param(base, RS::LIGHT_PARAM_SIZE);
					if (angular_diameter > 0.0) {
						angular_diameter = Math::tan(Math::deg_to_rad(angular_diameter));
					} else {
						angular_diameter = 0.0;
					}
					sky_light_data.size = angular_diameter;
					sky_globals.directional_light_count++;
					if (sky_globals.directional_light_count >= sky_globals.max_directional_lights) {
						break;
					}
				}
			}

			// Check whether the directional_light_buffer changes
			bool light_data_dirty = false;

			// Light buffer is dirty if we have fewer or more lights
			// If we have fewer lights, make sure that old lights are disabled
			if (sky_globals.directional_light_count != sky_globals.last_frame_directional_light_count) {
				light_data_dirty = true;
				for (uint32_t i = sky_globals.directional_light_count; i < sky_globals.max_directional_lights; i++) {
					sky_globals.directional_lights[i].enabled = false;
					sky_globals.last_frame_directional_lights[i].enabled = false;
				}
			}

			if (!light_data_dirty) {
				for (uint32_t i = 0; i < sky_globals.directional_light_count; i++) {
					if (sky_globals.directional_lights[i].direction[0] != sky_globals.last_frame_directional_lights[i].direction[0] ||
							sky_globals.directional_lights[i].direction[1] != sky_globals.last_frame_directional_lights[i].direction[1] ||
							sky_globals.directional_lights[i].direction[2] != sky_globals.last_frame_directional_lights[i].direction[2] ||
							sky_globals.directional_lights[i].energy != sky_globals.last_frame_directional_lights[i].energy ||
							sky_globals.directional_lights[i].color[0] != sky_globals.last_frame_directional_lights[i].color[0] ||
							sky_globals.directional_lights[i].color[1] != sky_globals.last_frame_directional_lights[i].color[1] ||
							sky_globals.directional_lights[i].color[2] != sky_globals.last_frame_directional_lights[i].color[2] ||
							sky_globals.directional_lights[i].enabled != sky_globals.last_frame_directional_lights[i].enabled ||
							sky_globals.directional_lights[i].size != sky_globals.last_frame_directional_lights[i].size) {
						light_data_dirty = true;
						break;
					}
				}
			}

			if (light_data_dirty) {
				for (uint32_t i = 0; i < sky_globals.max_directional_lights; i++) {
					sky_globals.last_frame_directional_lights[i] = sky_globals.directional_lights[i];
				}
				sky_globals.last_frame_directional_light_count = sky_globals.directional_light_count;
				sky->reflection_dirty = true;
			}
		}

		if (!sky->radiance) {
			_invalidate_sky(sky);
			_update_dirty_skys();
		}
	}
}

void RasterizerSceneGLES2::_draw_sky(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier, float p_luminance_multiplier, bool p_use_multiview, bool p_flip_y, bool p_apply_color_adjustments_in_post) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	Sky *sky = is_environment(p_env) ? sky_owner.get_or_null(environment_get_sky(p_env)) : nullptr;
	GLES2::SkyMaterialData *material_data = nullptr;
	RID sky_material;

	RS::EnvironmentBG background = RS::ENV_BG_CLEAR_COLOR;
	if (is_environment(p_env)) {
		background = environment_get_background(p_env);
	}

	if (sky) {
		sky_material = sky->material;
		if (sky_material.is_valid()) {
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material_data || !material_data->shader_data->valid) {
				material_data = nullptr;
			}
		}

		if (!material_data) {
			sky_material = sky_globals.default_material;
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}
	} else if (background == RS::ENV_BG_CLEAR_COLOR || background == RS::ENV_BG_COLOR || p_env.is_null()) {
		sky_material = sky_globals.fog_material;
		material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
	}

	if (!material_data) {
		return;
	}

	GLES2::SkyShaderData *shader_data = material_data->shader_data;
	if (!shader_data) {
		return;
	}

	uint64_t sky_spec = 0;
	if (p_flip_y) {
		sky_spec |= SkyShaderGLES2::USE_INVERTED_Y;
	}
	if (!p_apply_color_adjustments_in_post) {
		sky_spec |= SkyShaderGLES2::APPLY_TONEMAPPING;
	}

	bool success = material_storage->shaders.sky_shader.version_bind_shader(shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	if (!success) {
		return;
	}

	material_data->bind_uniforms();

	Projection camera;
	if (p_env.is_valid() && environment_get_sky_custom_fov(p_env)) {
		float near_plane = p_projection.get_z_near();
		float far_plane = p_projection.get_z_far();
		float aspect = p_projection.get_aspect();
		camera.set_perspective(environment_get_sky_custom_fov(p_env), aspect, near_plane, far_plane);
	} else {
		camera = p_projection;
	}

	Projection correction;
	correction.set_depth_correction(p_flip_y, true, false);
	camera = correction * camera;

	Basis sky_transform;
	if (p_env.is_valid()) {
		sky_transform = environment_get_sky_orientation(p_env);
		sky_transform.invert();
	}
	sky_transform = sky_transform * p_transform.basis;

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::ORIENTATION, sky_transform, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::PROJECTION, camera.columns[2][0], camera.columns[0][0], camera.columns[2][1], camera.columns[1][1], shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::POSITION, p_transform.origin, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::TIME, (float)time, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::LUMINANCE_MULTIPLIER, p_luminance_multiplier, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::EXPOSURE, scene_state.tonemap_ubo.exposure, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::WHITE, scene_state.tonemap_ubo.white, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);

	if (p_env.is_valid()) {
		Color fog_color = environment_get_fog_light_color(p_env).srgb_to_linear() * environment_get_fog_light_energy(p_env);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_ENABLED, environment_get_fog_enabled(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_AERIAL_PERSPECTIVE, environment_get_fog_aerial_perspective(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_LIGHT_COLOR, fog_color, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_SUN_SCATTER, environment_get_fog_sun_scatter(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_DENSITY, environment_get_fog_density(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	} else {
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_ENABLED, false, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	}

	_bind_sky_directional_lights(shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::Z_FAR, p_projection.get_z_far(), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);

	// Clean-up from opaque geometry passes
	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(false);
	glDepthFunc(GL_GEQUAL);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glDepthFunc");

	scene_state.enable_gl_blend(false);
	scene_state.set_gl_cull_mode(RS::CULL_MODE_BACK);

	if (sky_globals.screen_triangle_array != 0) {
		glBindVertexArray(sky_globals.screen_triangle_array);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glBindVertexArray");
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glBindBuffer");

		glEnableVertexAttribArray(RS::ARRAY_VERTEX);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glEnableVertexAttribArray");

		glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glVertexAttribPointer");
	}

	glDrawArrays(GL_TRIANGLES, 0, 3);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glDrawArrays");

	if (sky_globals.screen_triangle_array == 0) {
		glDisableVertexAttribArray(RS::ARRAY_VERTEX);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: glDisableVertexAttribArray");

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: unbind GL_ARRAY_BUFFER");
	} else {
		glBindVertexArray(0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_draw_sky: unbind glBindVertexArray");
	}
}

void RasterizerSceneGLES2::_update_sky_radiance(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	if (!is_environment(p_env)) {
		return;
	}

	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_env));
	if (!sky) {
		return;
	}

	GLES2::SkyMaterialData *material_data = nullptr;
	RID sky_material;

	RS::EnvironmentBG background = environment_get_background(p_env);

	if (sky) {
		sky_material = sky->material;
		if (sky_material.is_valid()) {
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material_data || !material_data->shader_data->valid) {
				material_data = nullptr;
			}
		}

		if (!material_data) {
			sky_material = sky_globals.default_material;
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}
	} else if (background == RS::ENV_BG_CLEAR_COLOR || background == RS::ENV_BG_COLOR) {
		sky_material = sky_globals.fog_material;
		material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
	}

	if (!material_data) {
		return;
	}

	GLES2::SkyShaderData *shader_data = material_data->shader_data;
	if (!shader_data) {
		return;
	}

	bool update_single_frame = sky->mode == RS::SKY_MODE_REALTIME || sky->mode == RS::SKY_MODE_QUALITY;
	RS::SkyMode sky_mode = sky->mode;

	if (sky_mode == RS::SKY_MODE_AUTOMATIC) {
		if ((shader_data->uses_time || shader_data->uses_position) && sky->radiance_size == 256) {
			update_single_frame = true;
			sky_mode = RS::SKY_MODE_REALTIME;
		} else if (shader_data->uses_light || shader_data->ubo_size > 0) {
			update_single_frame = false;
			sky_mode = RS::SKY_MODE_INCREMENTAL;
		} else {
			update_single_frame = true;
			sky_mode = RS::SKY_MODE_QUALITY;
		}
	}

	if (sky->processing_layer == 0 && sky_mode == RS::SKY_MODE_INCREMENTAL) {
		// On the first frame after creating sky, rebuild in single frame
		update_single_frame = true;
		sky_mode = RS::SKY_MODE_QUALITY;
	}

	int max_processing_layer = sky->mipmap_count;

	// Update radiance cubemap
	if (sky->reflection_dirty && (sky->processing_layer >= max_processing_layer || update_single_frame)) {
		Projection cm;
		cm.set_perspective(90, 1, 0.01, 10.0);
		Projection correction;
		correction.set_depth_correction(true, true, false);
		cm = correction * cm;

		uint64_t sky_spec = 0;
		bool success = material_storage->shaders.sky_shader.version_bind_shader(shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		if (!success) {
			return;
		}

		material_data->bind_uniforms();

		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::POSITION, p_transform.origin, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::TIME, (float)time, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::PROJECTION, cm.columns[2][0], cm.columns[0][0], cm.columns[2][1], cm.columns[1][1], shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::LUMINANCE_MULTIPLIER, p_sky_energy_multiplier, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);

		_bind_sky_directional_lights(shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);

		GLint prev_viewport[4];
		glGetIntegerv(GL_VIEWPORT, prev_viewport);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glGetIntegerv GL_VIEWPORT");

		GLint prev_fbo;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glGetIntegerv GL_FRAMEBUFFER_BINDING");

		glViewport(0, 0, sky->radiance_size, sky->radiance_size);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glViewport");

		glBindFramebuffer(GL_FRAMEBUFFER, sky->radiance_framebuffer);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glBindFramebuffer");

		scene_state.reset_gl_state();
		scene_state.set_gl_cull_mode(RS::CULL_MODE_DISABLED);
		scene_state.enable_gl_blend(false);

		if (sky_globals.screen_triangle_array != 0) {
			glBindVertexArray(sky_globals.screen_triangle_array);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glBindVertexArray");
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glBindBuffer");
			glEnableVertexAttribArray(RS::ARRAY_VERTEX);
			glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glVertexAttribPointer");
		}

		for (int i = 0; i < 6; i++) {
			Basis local_view = Basis::looking_at(view_normals[i], view_up[i]);
			material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::ORIENTATION, local_view, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, sky->radiance, 0);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glFramebufferTexture2D");
			glDrawArrays(GL_TRIANGLES, 0, 3);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glDrawArrays");
		}

		if (sky_globals.screen_triangle_array == 0) {
			glDisableVertexAttribArray(RS::ARRAY_VERTEX);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: cleanup VBO");
		} else {
			glBindVertexArray(0);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: cleanup VAO");
		}

		// Fast path generation for cubemap lods natively
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: glGenerateMipmap");

		sky->processing_layer = 1;
		sky->baked_exposure = p_sky_energy_multiplier;
		sky->reflection_dirty = false;

		glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
		glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_update_sky_radiance: restore GL state");

	} else {
		if (sky_mode == RS::SKY_MODE_INCREMENTAL && sky->processing_layer < max_processing_layer) {
			sky->processing_layer++;
		}
	}
}

Ref<Image> RasterizerSceneGLES2::sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	if (!sky) {
		return Ref<Image>();
	}

	_update_dirty_skys();

	if (sky->radiance == 0) {
		return Ref<Image>();
	}

	GLES2::CopyEffects *copy_effects = GLES2::CopyEffects::get_singleton();
	if (!copy_effects) {
		return Ref<Image>();
	}

	GLES2::Config *config = GLES2::Config::get_singleton();

	GLuint rad_tex = 0;
	glGenTextures(1, &rad_tex);
	glBindTexture(GL_TEXTURE_2D, rad_tex);

	bool use_float = config->float_texture_supported;
	if (use_float) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_size.width, p_size.height, 0, GL_RGBA, GL_FLOAT, nullptr);
		GLES2::Utilities::get_singleton()->texture_allocated_data(rad_tex, p_size.width * p_size.height * 16, "Temp sky panorama");
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_size.width, p_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		GLES2::Utilities::get_singleton()->texture_allocated_data(rad_tex, p_size.width * p_size.height * 4, "Temp sky panorama");
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: glTexImage2D");

	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: glGetIntegerv GL_FRAMEBUFFER_BINDING");

	GLuint rad_fbo = 0;
	glGenFramebuffers(1, &rad_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, rad_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rad_tex, 0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: glFramebufferTexture2D");

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: glBindTexture");

	GLint prev_viewport[4];
	glGetIntegerv(GL_VIEWPORT, prev_viewport);
	glViewport(0, 0, p_size.width, p_size.height);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: glViewport");

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: glClear");

	copy_effects->copy_cube_to_panorama(p_bake_irradiance ? float(sky->mipmap_count) : 0.0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: copy_cube_to_panorama");

	glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
	glDeleteFramebuffers(1, &rad_fbo);
	glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::sky_bake_panorama: restore GL state");

	RID tex_rid = GLES2::TextureStorage::get_singleton()->texture_allocate();
	{
		GLES2::Texture texture;
		texture.width = p_size.width;
		texture.height = p_size.height;
		texture.alloc_width = p_size.width;
		texture.alloc_height = p_size.height;
		texture.format = use_float ? Image::FORMAT_RGBAF : Image::FORMAT_RGBA8;
		texture.real_format = use_float ? Image::FORMAT_RGBAF : Image::FORMAT_RGBA8;
		texture.gl_format_cache = GL_RGBA;
		texture.gl_type_cache = use_float ? GL_FLOAT : GL_UNSIGNED_BYTE;
		texture.type = GLES2::Texture::TYPE_2D;
		texture.target = GL_TEXTURE_2D;
		texture.active = true;
		texture.tex_id = rad_tex;
		texture.is_render_target = true; // HACK: Prevent TextureStorage from retaining a cached copy of the texture.
		GLES2::TextureStorage::get_singleton()->texture_2d_initialize_from_texture(tex_rid, texture);
	}

	Ref<Image> img = GLES2::TextureStorage::get_singleton()->texture_2d_get(tex_rid);
	GLES2::Utilities::get_singleton()->texture_free_data(rad_tex);

	GLES2::Texture *texture = GLES2::TextureStorage::get_singleton()->get_texture(tex_rid);
	if (texture) {
		texture->is_render_target = false; // HACK: Avoid an error when freeing the texture.
		texture->tex_id = 0;
	}
	GLES2::TextureStorage::get_singleton()->texture_free(tex_rid);

	if (img.is_valid()) {
		for (int i = 0; i < p_size.width; i++) {
			for (int j = 0; j < p_size.height; j++) {
				Color c = img->get_pixel(i, j);
				c.r *= p_energy;
				c.g *= p_energy;
				c.b *= p_energy;
				img->set_pixel(i, j, c);
			}
		}
	}
	return img;
}

/* ENVIRONMENT API */

void RasterizerSceneGLES2::environment_glow_set_use_bicubic_upscale(bool p_enable) {
	glow_bicubic_upscale = p_enable;
}

void RasterizerSceneGLES2::environment_set_ssr_roughness_quality(RS::EnvironmentSSRRoughnessQuality p_quality) {
}

void RasterizerSceneGLES2::environment_set_ssao_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ssao_quality = p_quality;
}

void RasterizerSceneGLES2::environment_set_ssil_quality(RS::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_ray_count(RS::EnvironmentSDFGIRayCount p_ray_count) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_frames_to_converge(RS::EnvironmentSDFGIFramesToConverge p_frames) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_frames_to_update_light(RS::EnvironmentSDFGIFramesToUpdateLight p_update) {
}

void RasterizerSceneGLES2::environment_set_volumetric_fog_volume_size(int p_size, int p_depth) {
}

void RasterizerSceneGLES2::environment_set_volumetric_fog_filter_active(bool p_enable) {
}

Ref<Image> RasterizerSceneGLES2::environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) {
	ERR_FAIL_COND_V(p_env.is_null(), Ref<Image>());

	RS::EnvironmentBG environment_background = environment_get_background(p_env);

	if (environment_background == RS::ENV_BG_CAMERA_FEED || environment_background == RS::ENV_BG_CANVAS || environment_background == RS::ENV_BG_KEEP) {
		return Ref<Image>(); // Nothing to bake.
	}

	RS::EnvironmentAmbientSource ambient_source = environment_get_ambient_source(p_env);

	bool use_ambient_light = false;
	bool use_cube_map = false;
	if (ambient_source == RS::ENV_AMBIENT_SOURCE_BG && (environment_background == RS::ENV_BG_CLEAR_COLOR || environment_background == RS::ENV_BG_COLOR)) {
		use_ambient_light = true;
	} else {
		use_cube_map = (ambient_source == RS::ENV_AMBIENT_SOURCE_BG && environment_background == RS::ENV_BG_SKY) || ambient_source == RS::ENV_AMBIENT_SOURCE_SKY;
		use_ambient_light = use_cube_map || ambient_source == RS::ENV_AMBIENT_SOURCE_COLOR;
	}

	use_cube_map = use_cube_map || (environment_background == RS::ENV_BG_SKY && environment_get_sky(p_env).is_valid());

	Color ambient_color;
	float ambient_color_sky_mix = 0.0;
	if (use_ambient_light) {
		ambient_color_sky_mix = environment_get_ambient_sky_contribution(p_env);
		const float ambient_energy = environment_get_ambient_light_energy(p_env);
		ambient_color = environment_get_ambient_light(p_env);
		ambient_color = ambient_color.srgb_to_linear();
		ambient_color.r *= ambient_energy;
		ambient_color.g *= ambient_energy;
		ambient_color.b *= ambient_energy;
	}

	if (use_cube_map) {
		Ref<Image> panorama = sky_bake_panorama(environment_get_sky(p_env), environment_get_bg_energy_multiplier(p_env), p_bake_irradiance, p_size);
		if (use_ambient_light) {
			for (int x = 0; x < p_size.width; x++) {
				for (int y = 0; y < p_size.height; y++) {
					panorama->set_pixel(x, y, ambient_color.lerp(panorama->get_pixel(x, y), ambient_color_sky_mix));
				}
			}
		}
		return panorama;
	} else {
		const float bg_energy_multiplier = environment_get_bg_energy_multiplier(p_env);
		Color panorama_color = ((environment_background == RS::ENV_BG_CLEAR_COLOR) ? RSG::texture_storage->get_default_clear_color() : environment_get_bg_color(p_env));
		panorama_color = panorama_color.srgb_to_linear();
		panorama_color.r *= bg_energy_multiplier;
		panorama_color.g *= bg_energy_multiplier;
		panorama_color.b *= bg_energy_multiplier;

		if (use_ambient_light) {
			panorama_color = ambient_color.lerp(panorama_color, ambient_color_sky_mix);
		}

		Ref<Image> panorama = Image::create_empty(p_size.width, p_size.height, false, Image::FORMAT_RGBAF);
		panorama->fill(panorama_color);
		return panorama;
	}
}

void RasterizerSceneGLES2::positional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.positional_shadow_quality = p_quality;
}

void RasterizerSceneGLES2::directional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.directional_shadow_quality = p_quality;
}

RID RasterizerSceneGLES2::fog_volume_instance_create(RID p_fog_volume) {
	return RID();
}

void RasterizerSceneGLES2::fog_volume_instance_set_transform(RID p_fog_volume_instance, const Transform3D &p_transform) {
}

void RasterizerSceneGLES2::fog_volume_instance_set_active(RID p_fog_volume_instance, bool p_active) {
}

RID RasterizerSceneGLES2::fog_volume_instance_get_volume(RID p_fog_volume_instance) const {
	return RID();
}

Vector3 RasterizerSceneGLES2::fog_volume_instance_get_position(RID p_fog_volume_instance) const {
	return Vector3();
}

RID RasterizerSceneGLES2::voxel_gi_instance_create(RID p_voxel_gi) {
	return RID();
}

void RasterizerSceneGLES2::voxel_gi_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform) {
}

bool RasterizerSceneGLES2::voxel_gi_needs_update(RID p_probe) const {
	return false;
}

void RasterizerSceneGLES2::voxel_gi_update(RID p_probe, bool p_update_light_instances, const Vector<RID> &p_light_instances, const PagedArray<RenderGeometryInstance *> &p_dynamic_objects) {
}

void RasterizerSceneGLES2::voxel_gi_set_quality(RS::VoxelGIQuality) {
}

/* BATCH API */

void RasterizerSceneGLES2::scene_render_items_implementation(GeometryInstanceSurface **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	for (int i = 0; i < p_count; i++) {
		_render_single_item_immediate(p_surfaces[i]);
	}
}

void RasterizerSceneGLES2::_batch_get_hardware_limits(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchLimits &r_limits) {
	GLint max_vectors = 0;
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &max_vectors);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_get_hardware_limits: glGetIntegerv GL_MAX_VERTEX_UNIFORM_VECTORS");
	r_limits.max_matrix_palette_vectors = max_vectors > 0 ? max_vectors : 256;

	r_limits.max_vertices_per_buffer = 65536;
	r_limits.max_indices_per_buffer = 65536 * 2;
}

void RasterizerSceneGLES2::_batch_get_instance_geometry_capacity(const GeometryInstanceSurface *p_surface, uint32_t &r_vertex_count, uint32_t &r_index_count) {
	if (!p_surface->surface) {
		r_vertex_count = 0;
		r_index_count = 0;
		return;
	}

	uint64_t format = 0;
	Vector<uint8_t> v_data;
	Vector<uint8_t> a_data;
	Vector<uint8_t> i_data;

	GLES2::MeshStorage::get_singleton()->surface_get_batch_data(p_surface->surface, format, r_vertex_count, r_index_count, v_data, a_data, i_data);

	// Ensure unindexed geometry does not starve the index buffer requirement.
	if (r_index_count == 0) {
		r_index_count = r_vertex_count;
	}
}

float RasterizerSceneGLES2::_batch_get_item_depth(const GeometryInstanceSurface *p_surface, const Transform3D &p_camera_transform) {
	// Planar depth: Dot product of the camera's look vector
	// and the vector to the object's origin
	Vector3 look_vector = -p_camera_transform.basis.get_column(2);
	Vector3 to_object = p_surface->owner->transform.origin - p_camera_transform.origin;
	return look_vector.dot(to_object) - p_surface->owner->sorting_offset;
}

uint64_t RasterizerSceneGLES2::_batch_get_state_hash(const GeometryInstanceSurface *p_surface) {
	uint64_t hash = 0;

	// Bits 63-52: Shader version (12 bits)
	uint64_t shader_id = p_surface->shader ? p_surface->shader->version.get_id() : 0;
	hash |= (shader_id & 0xFFF) << 52;

	// Bits 51-32: Material ID (texture bindings, uniform blocks, 20 bits)
	uint64_t mat_id = p_surface->material ? static_cast<uint64_t>((uintptr_t)p_surface->material >> 4) : 0;
	hash |= (mat_id & 0xFFFFF) << 32;

	// Bits 15-0: Mesh surface ID / primitive topology (16 bits)
	uint64_t surface_id = p_surface->surface_index;
	uint64_t primitive = p_surface->primitive;
	hash |= ((surface_id & 0xFF) << 8); // Surface ID in 15-8
	hash |= (primitive & 0xFF); // Primitive type in 7-0

	// Bits 31-16: Combined light cache hash (16 bits)
	if (p_surface->owner && bdata.pass_mode == PASS_MODE_COLOR) {
		uint64_t light_hash = 0;
		for (uint32_t i = 0; i < p_surface->owner->omni_light_gl_cache.size(); i++) {
			light_hash = (light_hash * 31) + p_surface->owner->omni_light_gl_cache[i];
		}
		for (uint32_t i = 0; i < p_surface->owner->spot_light_gl_cache.size(); i++) {
			light_hash = (light_hash * 31) + p_surface->owner->spot_light_gl_cache[i];
		}
		hash |= (light_hash & 0xFFFF) << 16;
	}

	return hash;
}

GLES2::SceneMaterialData *RasterizerSceneGLES2::_batch_get_material_data(const GeometryInstanceSurface *p_surface) {
	return p_surface->material;
}

void RasterizerSceneGLES2::_batch_fill_instance_geometry(const GeometryInstanceSurface *p_surface, RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3D *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index) {
	if (!p_surface || !p_surface->surface) {
		return;
	}

	uint64_t format = 0;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
	Vector<uint8_t> v_data;
	Vector<uint8_t> a_data;
	Vector<uint8_t> i_data;

	GLES2::MeshStorage::get_singleton()->surface_get_batch_data(p_surface->surface, format, vertex_count, index_count, v_data, a_data, i_data);

	if (v_data.is_empty() || vertex_count == 0) {
		return;
	}

	bool is_2d = format & RS::ARRAY_FLAG_USE_2D_VERTICES;
	bool is_compressed = format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;

	int pos_stride = is_2d ? (sizeof(float) * 2) : (sizeof(float) * 3);
	int normal_offset = pos_stride * vertex_count;
	int normal_tangent_stride = 0;
	if (format & RS::ARRAY_FORMAT_NORMAL) {
		if (is_compressed) {
			normal_tangent_stride = 4;
		} else {
			normal_tangent_stride = (format & RS::ARRAY_FORMAT_TANGENT) ? 8 : 4;
		}
	}

	int attr_stride = (a_data.size() > 0 && vertex_count > 0) ? (a_data.size() / vertex_count) : 0;

	const uint8_t *v_read_pos = v_data.ptr();
	const uint8_t *v_read_norm = v_data.ptr() + normal_offset;
	const uint8_t *a_read = a_data.is_empty() ? nullptr : a_data.ptr();

	Transform3D world_xform = p_surface->owner->transform;
	Transform3D write_xform = p_use_hardware_transform ? Transform3D() : world_xform;

	if (bdata.fvf == BatcherEnums::FVF_DEPTH_ONLY) {
		RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepth *bvs_depth = (RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepth *)r_bvs;
		for (uint32_t i = 0; i < vertex_count; i++) {
			const uint8_t *p_ptr = v_read_pos + i * pos_stride;
			_batch_fill_vertex_depth(bvs_depth[i], p_ptr, format, is_2d, write_xform);
		}
	} else if (bdata.fvf == BatcherEnums::FVF_DEPTH_ALPHA) {
		RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepthAlpha *bvs_alpha = (RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepthAlpha *)r_bvs;
		for (uint32_t i = 0; i < vertex_count; i++) {
			const uint8_t *p_ptr = v_read_pos + i * pos_stride;
			const uint8_t *a_ptr = a_read ? (a_read + i * attr_stride) : nullptr;
			_batch_fill_vertex_depth_alpha(bvs_alpha[i], p_ptr, a_ptr, format, is_2d, is_compressed, write_xform);
		}
	} else {
		for (uint32_t i = 0; i < vertex_count; i++) {
			const uint8_t *p_ptr = v_read_pos + i * pos_stride;
			const uint8_t *n_ptr = v_read_norm + i * normal_tangent_stride;
			const uint8_t *a_ptr = a_read ? (a_read + i * attr_stride) : nullptr;

			_batch_fill_vertex(r_bvs[i], p_ptr, n_ptr, a_ptr, format, is_2d, is_compressed, write_xform);
		}
	}

	if (r_inds && index_count > 0 && !i_data.is_empty()) {
		bool is_16 = vertex_count <= 65536;
		const uint8_t *i_read = i_data.ptr();

		for (uint32_t i = 0; i < index_count; i++) {
			uint32_t idx = is_16 ? ((uint16_t *)i_read)[i] : ((uint32_t *)i_read)[i];
			r_inds[i] = (uint16_t)(idx + p_start_vert);
		}
	} else if (r_inds) {
		// Auto-generate sequential indices for unindexed meshes.
		for (uint32_t i = 0; i < vertex_count; i++) {
			r_inds[i] = (uint16_t)(i + p_start_vert);
		}
	}
}

void RasterizerSceneGLES2::_batch_fill_multimesh_geometry(const GeometryInstanceSurface *p_surface, RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DInstanced *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index) {
	if (!p_surface || !p_surface->surface || p_surface->owner->instance_count <= 0) {
		return;
	}

	uint64_t format = 0;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
	Vector<uint8_t> v_data;
	Vector<uint8_t> a_data;
	Vector<uint8_t> i_data;

	GLES2::MeshStorage::get_singleton()->surface_get_batch_data(p_surface->surface, format, vertex_count, index_count, v_data, a_data, i_data);

	if (v_data.is_empty() || vertex_count == 0) {
		return;
	}

	bool is_2d = format & RS::ARRAY_FLAG_USE_2D_VERTICES;
	bool is_compressed = format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;

	int pos_stride = is_2d ? (sizeof(float) * 2) : (sizeof(float) * 3);
	int normal_offset = pos_stride * vertex_count;
	int normal_tangent_stride = 0;
	if (format & RS::ARRAY_FORMAT_NORMAL) {
		if (is_compressed) {
			normal_tangent_stride = 4;
		} else {
			normal_tangent_stride = (format & RS::ARRAY_FORMAT_TANGENT) ? 8 : 4;
		}
	}

	int attr_stride = (a_data.size() > 0 && vertex_count > 0) ? (a_data.size() / vertex_count) : 0;

	const uint8_t *v_read_pos = v_data.ptr();
	const uint8_t *v_read_norm = v_data.ptr() + normal_offset;
	const uint8_t *a_read = a_data.is_empty() ? nullptr : a_data.ptr();

	int instances = p_surface->owner->instance_count;
	MultiMeshInstanceData mm = _get_multimesh_data(p_surface);

	uint32_t bvs_idx = 0;
	Transform3D owner_transform = p_surface->owner->transform;

	if (bdata.fvf == BatcherEnums::FVF_DEPTH_ONLY) {
		RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepth *bvs_depth = (RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepth *)r_bvs;
		for (int inst = 0; inst < instances; inst++) {
			Transform3D xform;
			Color inst_color = Color(1, 1, 1, 1);
			if (p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH && mm.data) {
				_batch_decode_multimesh_instance(mm.data + (inst * mm.stride), mm.format, mm.uses_colors, mm.color_offset, xform, inst_color);
			} else if (p_surface->owner->data->base_type == RS::INSTANCE_PARTICLES) {
				xform = Transform3D();
			}
			Transform3D write_xform = p_use_hardware_transform ? xform : (owner_transform * xform);

			for (uint32_t i = 0; i < vertex_count; i++) {
				const uint8_t *p_ptr = v_read_pos + i * pos_stride;
				_batch_fill_vertex_depth(bvs_depth[bvs_idx++], p_ptr, format, is_2d, write_xform);
			}
		}
	} else if (bdata.fvf == BatcherEnums::FVF_DEPTH_ALPHA) {
		RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepthAlpha *bvs_alpha = (RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepthAlpha *)r_bvs;
		for (int inst = 0; inst < instances; inst++) {
			Transform3D xform;
			Color inst_color = Color(1, 1, 1, 1);
			if (p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH && mm.data) {
				_batch_decode_multimesh_instance(mm.data + (inst * mm.stride), mm.format, mm.uses_colors, mm.color_offset, xform, inst_color);
			} else if (p_surface->owner->data->base_type == RS::INSTANCE_PARTICLES) {
				xform = Transform3D();
			}
			Transform3D write_xform = p_use_hardware_transform ? xform : (owner_transform * xform);

			for (uint32_t i = 0; i < vertex_count; i++) {
				const uint8_t *p_ptr = v_read_pos + i * pos_stride;
				const uint8_t *a_ptr = a_read ? (a_read + i * attr_stride) : nullptr;
				_batch_fill_vertex_depth_alpha(bvs_alpha[bvs_idx++], p_ptr, a_ptr, format, is_2d, is_compressed, write_xform);
			}
		}
	} else {
		for (int inst = 0; inst < instances; inst++) {
			Transform3D xform;
			Color inst_color = Color(1, 1, 1, 1);

			if (p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH && mm.data) {
				_batch_decode_multimesh_instance(mm.data + (inst * mm.stride), mm.format, mm.uses_colors, mm.color_offset, xform, inst_color);
			} else if (p_surface->owner->data->base_type == RS::INSTANCE_PARTICLES) {
				xform = Transform3D();
			}

			Transform3D write_xform = p_use_hardware_transform ? xform : (owner_transform * xform);

			for (uint32_t i = 0; i < vertex_count; i++) {
				const uint8_t *p_ptr = v_read_pos + i * pos_stride;
				const uint8_t *n_ptr = v_read_norm + i * normal_tangent_stride;
				const uint8_t *a_ptr = a_read ? (a_read + i * attr_stride) : nullptr;

				_batch_fill_vertex_instanced(r_bvs[bvs_idx], p_ptr, n_ptr, a_ptr, format, is_2d, is_compressed, write_xform, inst_color);
				bvs_idx++;
			}
		}
	}

	if (r_inds && index_count > 0 && !i_data.is_empty()) {
		bool is_16 = vertex_count <= 65536;
		const uint8_t *i_read = i_data.ptr();

		uint32_t inds_idx = 0;
		for (int inst = 0; inst < instances; inst++) {
			for (uint32_t i = 0; i < index_count; i++) {
				uint32_t idx = is_16 ? ((uint16_t *)i_read)[i] : ((uint32_t *)i_read)[i];
				r_inds[inds_idx++] = (uint16_t)(idx + p_start_vert + (inst * vertex_count));
			}
		}
	} else if (r_inds) {
		// Auto-generate sequential indices for unindexed meshes.
		uint32_t inds_idx = 0;
		for (int inst = 0; inst < instances; inst++) {
			for (uint32_t i = 0; i < vertex_count; i++) {
				r_inds[inds_idx++] = (uint16_t)(i + p_start_vert + (inst * vertex_count));
			}
		}
	}
}

void RasterizerSceneGLES2::_batch_upload_buffers() {
	if (!bdata.gl_vertex_buffer) {
		glGenBuffers(1, &bdata.gl_vertex_buffer);
		glGenBuffers(1, &bdata.gl_instanced_vertex_buffer);
		glGenBuffers(1, &bdata.gl_index_buffer);
	}

	int bytes_to_upload = bdata.total_verts * bdata.unit_vertices.get_unit_size_bytes();

	if (bdata.fvf == BatcherEnums::FVF_INSTANCED) {
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_instanced_vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, bytes_to_upload, bdata.unit_vertices.get_data(), GL_DYNAMIC_DRAW);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_upload_buffers: glBufferData ARRAY_BUFFER INSTANCED");
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, bytes_to_upload, bdata.unit_vertices.get_data(), GL_DYNAMIC_DRAW);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_upload_buffers: glBufferData ARRAY_BUFFER");
	}

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, bdata.total_indices * sizeof(uint16_t), bdata.indices.get_data(), GL_DYNAMIC_DRAW);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_upload_buffers: glBufferData ELEMENT_ARRAY_BUFFER");
}

void RasterizerSceneGLES2::_batch_bind_material(GLES2::SceneMaterialData *p_material_data, const Transform3D &p_world_transform, bool p_transparent) {
	if (p_material_data) {
		SceneShaderGLES2::ShaderVariant variant = SceneShaderGLES2::MODE_COLOR;
		if (bdata.fvf == BatcherEnums::FVF_INSTANCED || (bdata.fvf == BatcherEnums::FVF_REGULAR && p_world_transform == Transform3D())) {
			variant = SceneShaderGLES2::MODE_COLOR_INSTANCING;
		}

		if (p_material_data->shader_data) {
			GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_bind_shader(p_material_data->shader_data->version, variant, 0);
		}

		p_material_data->bind_uniforms();

		if (p_material_data->shader_data) {
			_bind_scene_camera_uniforms(p_material_data->shader_data->version, variant, 0);
			GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WORLD_TRANSFORM, p_world_transform, p_material_data->shader_data->version, variant, 0);

			if (bdata.batches.size() > 0 && _render_item_state.curr_batch) {
				GeometryInstanceSurface *first_surf = static_cast<GeometryInstanceSurface *>(bdata.sort_items[_render_item_state.curr_batch->first_item_index].item);
				if (first_surf && first_surf->owner) {
					int omni_count = MIN((int)first_surf->owner->omni_light_gl_cache.size(), 8);
					GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::OMNI_LIGHT_COUNT, omni_count, p_material_data->shader_data->version, variant, 0);
					for (int i = 0; i < omni_count; i++) {
						uint32_t gl_id = first_surf->owner->omni_light_gl_cache[i];
						int base_idx = SceneShaderGLES2::OMNI_LIGHTS_DATA_0_POSITION_INV_RADIUS + (i * 4);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(scene_state.omni_lights[gl_id].position[0], scene_state.omni_lights[gl_id].position[1], scene_state.omni_lights[gl_id].position[2], scene_state.omni_lights[gl_id].inv_radius), p_material_data->shader_data->version, variant, 0);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(scene_state.omni_lights[gl_id].direction[0], scene_state.omni_lights[gl_id].direction[1], scene_state.omni_lights[gl_id].direction[2], scene_state.omni_lights[gl_id].size), p_material_data->shader_data->version, variant, 0);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), Vector4(scene_state.omni_lights[gl_id].color[0], scene_state.omni_lights[gl_id].color[1], scene_state.omni_lights[gl_id].color[2], scene_state.omni_lights[gl_id].attenuation), p_material_data->shader_data->version, variant, 0);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 3), Vector4(scene_state.omni_lights[gl_id].inv_spot_attenuation, scene_state.omni_lights[gl_id].cos_spot_angle, scene_state.omni_lights[gl_id].specular_amount, scene_state.omni_lights[gl_id].shadow_opacity), p_material_data->shader_data->version, variant, 0);
					}

					int spot_count = MIN((int)first_surf->owner->spot_light_gl_cache.size(), 8);
					GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SPOT_LIGHT_COUNT, spot_count, p_material_data->shader_data->version, variant, 0);
					for (int i = 0; i < spot_count; i++) {
						uint32_t gl_id = first_surf->owner->spot_light_gl_cache[i];
						int base_idx = SceneShaderGLES2::SPOT_LIGHTS_DATA_0_POSITION_INV_RADIUS + (i * 4);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(scene_state.spot_lights[gl_id].position[0], scene_state.spot_lights[gl_id].position[1], scene_state.spot_lights[gl_id].position[2], scene_state.spot_lights[gl_id].inv_radius), p_material_data->shader_data->version, variant, 0);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(scene_state.spot_lights[gl_id].direction[0], scene_state.spot_lights[gl_id].direction[1], scene_state.spot_lights[gl_id].direction[2], scene_state.spot_lights[gl_id].size), p_material_data->shader_data->version, variant, 0);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), Vector4(scene_state.spot_lights[gl_id].color[0], scene_state.spot_lights[gl_id].color[1], scene_state.spot_lights[gl_id].color[2], scene_state.spot_lights[gl_id].attenuation), p_material_data->shader_data->version, variant, 0);
						GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 3), Vector4(scene_state.spot_lights[gl_id].inv_spot_attenuation, scene_state.spot_lights[gl_id].cos_spot_angle, scene_state.spot_lights[gl_id].specular_amount, scene_state.spot_lights[gl_id].shadow_opacity), p_material_data->shader_data->version, variant, 0);
					}
				}
			}
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_material: light data upload");

			scene_state.set_gl_cull_mode(p_material_data->shader_data->cull_mode);
			scene_state.enable_gl_depth_test(p_material_data->shader_data->depth_test == GLES2::SceneShaderData::DEPTH_TEST_ENABLED);

			if (p_material_data->shader_data->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_OPAQUE) {
				scene_state.enable_gl_depth_draw(!p_transparent);
			} else {
				scene_state.enable_gl_depth_draw(p_material_data->shader_data->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_ALWAYS);
			}
		}
	}
}

void RasterizerSceneGLES2::_batch_render_items(GLES2::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::Batch3D &p_batch, bool p_transparent) {
	bool use_hardware_transform = (p_batch.num_items == 1);
	GeometryInstanceSurface *first_surf = static_cast<GeometryInstanceSurface *>(bdata.sort_items[p_batch.first_item_index].item);

	Transform3D world_xform;
	if (use_hardware_transform && bdata.fvf != BatcherEnums::FVF_INSTANCED) {
		world_xform = first_surf->owner->transform;
	}

	_batch_bind_material(p_material_data, world_xform, p_transparent);
	_batch_render_generic(p_primitive, 0, bdata.total_indices, true);
}

void RasterizerSceneGLES2::_batch_render_generic(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, bool p_has_color) {
	if (bdata.indices.size() == 0) {
		return;
	}

	if (p_count == 0) {
		p_count = bdata.indices.size();
	}

	bool is_instanced = bdata.fvf == BatcherEnums::FVF_INSTANCED;
	bool is_depth_only = bdata.fvf == BatcherEnums::FVF_DEPTH_ONLY;
	bool is_depth_alpha = bdata.fvf == BatcherEnums::FVF_DEPTH_ALPHA;

	uint32_t stride = bdata.unit_vertices.get_unit_size_bytes();

	glBindBuffer(GL_ARRAY_BUFFER, is_instanced ? bdata.gl_instanced_vertex_buffer : bdata.gl_vertex_buffer);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_generic: glBindBuffer ARRAY_BUFFER");

#define BATCH_INSTANCED_OFFSET_OF(pointer) offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DInstanced, pointer)
#define BATCH_OFFSET_OF(pointer) offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3D, pointer)
#define BATCH_DEPTH_OFFSET_OF(pointer) offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepth, pointer)
#define BATCH_DEPTH_ALPHA_OFFSET_OF(pointer) offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::BatchVertex3DDepthAlpha, pointer)

	glEnableVertexAttribArray(RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 3, GL_FLOAT, GL_FALSE, stride, (void *)0); // pos is always at offset 0

	if (!is_depth_only && !is_depth_alpha) {
		glEnableVertexAttribArray(RS::ARRAY_NORMAL);
		glVertexAttribPointer(RS::ARRAY_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_OFFSET_OF(normal));

		glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_OFFSET_OF(uv));

		if (p_has_color) {
			glEnableVertexAttribArray(RS::ARRAY_COLOR);
			glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)BATCH_OFFSET_OF(color));
		} else {
			glDisableVertexAttribArray(RS::ARRAY_COLOR);
			glVertexAttrib4f(RS::ARRAY_COLOR, 0.6f, 0.6f, 0.6f, 1.0f);
		}
	} else if (is_depth_alpha) {
		glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_ALPHA_OFFSET_OF(uv));
		glDisableVertexAttribArray(RS::ARRAY_NORMAL);
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_NORMAL);
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
	}

	// Provide transform slots for both regular batching and multimesh instancing
	glEnableVertexAttribArray(12); // instance_xform0
	glEnableVertexAttribArray(13); // instance_xform1
	glEnableVertexAttribArray(14); // instance_xform2

	if (is_depth_only) {
		glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_OFFSET_OF(instance_xform0));
		glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_OFFSET_OF(instance_xform1));
		glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_OFFSET_OF(instance_xform2));
	} else if (is_depth_alpha) {
		glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_ALPHA_OFFSET_OF(instance_xform0));
		glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_ALPHA_OFFSET_OF(instance_xform1));
		glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_DEPTH_ALPHA_OFFSET_OF(instance_xform2));
	} else {
		glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_OFFSET_OF(instance_xform0));
		glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_OFFSET_OF(instance_xform1));
		glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, stride, (void *)BATCH_OFFSET_OF(instance_xform2));
	}

	if (is_instanced) {
		glEnableVertexAttribArray(15); // instance_color_custom_data
		glVertexAttribPointer(15, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)BATCH_INSTANCED_OFFSET_OF(instance_color_custom_data));
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_generic: glVertexAttribPointers setup");

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_generic: glBindBuffer ELEMENT_ARRAY_BUFFER");

	GLenum primitive_gl = prim[int(p_primitive)];

	glDrawElements(primitive_gl, p_count, GL_UNSIGNED_SHORT, (void *)(p_offset * sizeof(uint16_t)));

	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_generic: glDrawElements");

#undef BATCH_OFFSET_OF
#undef BATCH_INSTANCED_OFFSET_OF
#undef BATCH_DEPTH_OFFSET_OF
#undef BATCH_DEPTH_ALPHA_OFFSET_OF

	// Unbind
	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glDisableVertexAttribArray(RS::ARRAY_NORMAL);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	glDisableVertexAttribArray(RS::ARRAY_COLOR);
	glDisableVertexAttribArray(12);
	glDisableVertexAttribArray(13);
	glDisableVertexAttribArray(14);

	if (is_instanced) {
		glDisableVertexAttribArray(15);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_generic: state cleanup");
}

void RasterizerSceneGLES2::_render_single_item_immediate(const GeometryInstanceSurface *p_surface) {
	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	GLES2::SceneShaderData *shader = p_surface->shader;
	if (!shader || !p_surface->surface || !p_surface->owner) {
		return;
	}

	uint64_t item_hash = _batch_get_state_hash(p_surface);

	if (item_hash != _render_item_state.current_state_hash) {
		// Bind shader
		bool success = material_storage->shaders.scene_shader.version_bind_shader(shader->version, SceneShaderGLES2::MODE_COLOR, 0);
		if (!success) {
			return;
		}

		if (p_surface->material) {
			p_surface->material->bind_uniforms();
		}

		// Push camera state
		_bind_scene_camera_uniforms(shader->version, SceneShaderGLES2::MODE_COLOR, 0);

		scene_state.set_gl_cull_mode(shader->cull_mode);
		scene_state.enable_gl_depth_test(shader->depth_test == GLES2::SceneShaderData::DEPTH_TEST_ENABLED);

		if (shader->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_OPAQUE) {
			scene_state.enable_gl_depth_draw(!(p_surface->flags & GeometryInstanceSurface::FLAG_PASS_ALPHA));
		} else {
			scene_state.enable_gl_depth_draw(shader->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_ALWAYS);
		}

		if (p_surface->owner) {
			int omni_count = MIN((int)p_surface->owner->omni_light_gl_cache.size(), 8);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::OMNI_LIGHT_COUNT, omni_count, shader->version, SceneShaderGLES2::MODE_COLOR, 0);
			for (int i = 0; i < omni_count; i++) {
				uint32_t gl_id = p_surface->owner->omni_light_gl_cache[i];
				int base_idx = SceneShaderGLES2::OMNI_LIGHTS_DATA_0_POSITION_INV_RADIUS + (i * 4);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(scene_state.omni_lights[gl_id].position[0], scene_state.omni_lights[gl_id].position[1], scene_state.omni_lights[gl_id].position[2], scene_state.omni_lights[gl_id].inv_radius), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(scene_state.omni_lights[gl_id].direction[0], scene_state.omni_lights[gl_id].direction[1], scene_state.omni_lights[gl_id].direction[2], scene_state.omni_lights[gl_id].size), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), Vector4(scene_state.omni_lights[gl_id].color[0], scene_state.omni_lights[gl_id].color[1], scene_state.omni_lights[gl_id].color[2], scene_state.omni_lights[gl_id].attenuation), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 3), Vector4(scene_state.omni_lights[gl_id].inv_spot_attenuation, scene_state.omni_lights[gl_id].cos_spot_angle, scene_state.omni_lights[gl_id].specular_amount, scene_state.omni_lights[gl_id].shadow_opacity), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
			}

			int spot_count = MIN((int)p_surface->owner->spot_light_gl_cache.size(), 8);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SPOT_LIGHT_COUNT, spot_count, shader->version, SceneShaderGLES2::MODE_COLOR, 0);
			for (int i = 0; i < spot_count; i++) {
				uint32_t gl_id = p_surface->owner->spot_light_gl_cache[i];
				int base_idx = SceneShaderGLES2::SPOT_LIGHTS_DATA_0_POSITION_INV_RADIUS + (i * 4);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(scene_state.spot_lights[gl_id].position[0], scene_state.spot_lights[gl_id].position[1], scene_state.spot_lights[gl_id].position[2], scene_state.spot_lights[gl_id].inv_radius), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(scene_state.spot_lights[gl_id].direction[0], scene_state.spot_lights[gl_id].direction[1], scene_state.spot_lights[gl_id].direction[2], scene_state.spot_lights[gl_id].size), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), Vector4(scene_state.spot_lights[gl_id].color[0], scene_state.spot_lights[gl_id].color[1], scene_state.spot_lights[gl_id].color[2], scene_state.spot_lights[gl_id].attenuation), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 3), Vector4(scene_state.spot_lights[gl_id].inv_spot_attenuation, scene_state.spot_lights[gl_id].cos_spot_angle, scene_state.spot_lights[gl_id].specular_amount, scene_state.spot_lights[gl_id].shadow_opacity), shader->version, SceneShaderGLES2::MODE_COLOR, 0);
			}
		} else {
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::OMNI_LIGHT_COUNT, 0, shader->version, SceneShaderGLES2::MODE_COLOR, 0);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SPOT_LIGHT_COUNT, 0, shader->version, SceneShaderGLES2::MODE_COLOR, 0);
		}
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_single_item_immediate: light data upload");

		_render_item_state.current_state_hash = item_hash;
		_render_item_state.current_material_data = p_surface->material;
	}

	// Upload world transform
	Transform3D world_transform = p_surface->owner->transform;
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WORLD_TRANSFORM, world_transform, shader->version, SceneShaderGLES2::MODE_COLOR, 0);

	// Retrieve VAO/VBOs
	GLuint vertex_array_gl = 0;
	mesh_storage->mesh_surface_get_vertex_arrays_and_format(p_surface->surface, shader->vertex_input_mask, vertex_array_gl);

	if (vertex_array_gl == 0) {
		return;
	}
	glBindVertexArray(vertex_array_gl);

	GLuint index_array_gl = mesh_storage->mesh_surface_get_index_buffer(p_surface->surface, p_surface->lod_index);
	bool use_index_buffer = index_array_gl != 0;
	if (use_index_buffer) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_array_gl);
	}

	GLenum primitive_gl = prim[int(p_surface->primitive)];

	// We must chunk the draw if its a multimesh
	int drawn_count = mesh_storage->mesh_surface_get_vertices_drawn_count(p_surface->surface);
	GLenum index_type = use_index_buffer ? mesh_storage->mesh_surface_get_index_type(p_surface->surface) : 0;

	int instances = p_surface->owner->instance_count > 0 ? p_surface->owner->instance_count : 1;
	bool is_multimesh = p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH;

	MultiMeshInstanceData mm = _get_multimesh_data(p_surface);
	Transform3D owner_transform = p_surface->owner->transform;

	if (mm.uses_colors) {
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
	}

	// Draw
	Color inst_color(1.0f, 1.0f, 1.0f, 1.0f);
	for (int inst = 0; inst < instances; inst++) {
		Transform3D xform;

		if (is_multimesh && mm.data) {
			_batch_decode_multimesh_instance(mm.data + (inst * mm.stride), mm.format, mm.uses_colors, mm.color_offset, xform, inst_color);
		} else if (p_surface->owner->data->base_type == RS::INSTANCE_PARTICLES) {
			xform = Transform3D();
		}

		Transform3D write_xform = owner_transform * xform;

		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WORLD_TRANSFORM, write_xform, shader->version, SceneShaderGLES2::MODE_COLOR, 0);

		if (mm.uses_colors) {
			glVertexAttrib4f(RS::ARRAY_COLOR, inst_color.r, inst_color.g, inst_color.b, inst_color.a);
		}

		if (use_index_buffer) {
			glDrawElements(primitive_gl, drawn_count, index_type, 0);
		} else {
			glDrawArrays(primitive_gl, 0, drawn_count);
		}
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_single_item_immediate: glDrawElements/Arrays bypass loop");

	// Unbind state
	if (vertex_array_gl != 0) {
		glBindVertexArray(0);
	}
	if (use_index_buffer) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
}

template <RasterizerSceneGLES2::RenderListType p_render_list, RasterizerSceneGLES2::PassMode p_pass_mode>
void RasterizerSceneGLES2::_fill_render_list(const RenderDataGLES2 *p_render_data, bool p_append) {
	if constexpr (p_render_list == RENDER_LIST_OPAQUE) {
		scene_state.used_screen_texture = false;
		scene_state.used_normal_texture = false;
		scene_state.used_depth_texture = false;
	}

	Plane near_plane;
	if (p_render_data->cam_orthogonal) {
		near_plane = Plane(-p_render_data->cam_transform.basis.get_column(Vector3::AXIS_Z), p_render_data->cam_transform.origin);
		near_plane.d += p_render_data->cam_projection.get_z_near();
	}
	float z_max = p_render_data->cam_projection.get_z_far() - p_render_data->cam_projection.get_z_near();

	RenderList *rl = &render_list[p_render_list];
	_update_dirty_geometry_instances();

	if (!p_append) {
		rl->clear();
		if constexpr (p_render_list == RENDER_LIST_OPAQUE) {
			render_list[RENDER_LIST_ALPHA].clear();
		}
	}

	for (int i = 0; i < (int)p_render_data->instances->size(); i++) {
		GeometryInstanceGLES2 *inst = static_cast<GeometryInstanceGLES2 *>((*p_render_data->instances)[i]);

		Vector3 center = inst->transform.origin;
		if (p_render_data->cam_orthogonal) {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.get_support(-near_plane.normal);
			}
			inst->depth = near_plane.distance_to(center) - inst->sorting_offset;
		} else {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.position + (inst->transformed_aabb.size * 0.5);
			}
			inst->depth = p_render_data->cam_transform.origin.distance_to(center) - inst->sorting_offset;
		}

		uint32_t depth_layer = CLAMP(int(inst->depth * 16 / z_max), 0, 15);

		if (p_pass_mode == PASS_MODE_COLOR) {
			inst->light_passes.clear();
			inst->spot_light_gl_cache.clear();
			inst->omni_light_gl_cache.clear();
			inst->reflection_probes_local_transform_cache.clear();
			inst->reflection_probe_rid_cache.clear();

			uint64_t current_frame = RSG::rasterizer->get_frame_number();
			GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();

			if (inst->paired_omni_light_count) {
				for (uint32_t j = 0; j < inst->paired_omni_light_count; j++) {
					RID light_instance = inst->paired_omni_lights[j];
					if (light_storage->light_instance_get_render_pass(light_instance) != current_frame) {
						continue;
					}
					inst->omni_light_gl_cache.push_back((uint32_t)light_storage->light_instance_get_gl_id(light_instance));
				}
			}

			if (inst->paired_spot_light_count) {
				for (uint32_t j = 0; j < inst->paired_spot_light_count; j++) {
					RID light_instance = inst->paired_spot_lights[j];
					if (light_storage->light_instance_get_render_pass(light_instance) != current_frame) {
						continue;
					}
					inst->spot_light_gl_cache.push_back((uint32_t)light_storage->light_instance_get_gl_id(light_instance));
				}
			}
		}

		GeometryInstanceSurface *surf = inst->surface_caches;

		while (surf) {
			surf->lod_index = 0; // TODO(GLES2): Simple stub for LOD

			if constexpr (p_pass_mode == PASS_MODE_COLOR) {
				if (surf->flags & GeometryInstanceSurface::FLAG_PASS_OPAQUE) {
					rl->add_element(surf);
				}
				if (surf->flags & GeometryInstanceSurface::FLAG_PASS_ALPHA) {
					render_list[RENDER_LIST_ALPHA].add_element(surf);
				}

				if (surf->flags & GeometryInstanceSurface::FLAG_USES_SCREEN_TEXTURE) {
					scene_state.used_screen_texture = true;
				}
				if (surf->flags & GeometryInstanceSurface::FLAG_USES_NORMAL_TEXTURE) {
					scene_state.used_normal_texture = true;
				}
				if (surf->flags & GeometryInstanceSurface::FLAG_USES_DEPTH_TEXTURE) {
					scene_state.used_depth_texture = true;
				}
			}

			surf->sort.depth_layer = depth_layer;
			surf = surf->next;
		}
	}
}

// Needs to be called after _setup_lights so that directional_light_count is accurate.
void RasterizerSceneGLES2::_setup_environment(const RenderDataGLES2 *p_render_data, bool p_no_fog, const Size2i &p_screen_size, bool p_flip_y, const Color &p_default_bg_color, bool p_pancake_shadows, float p_shadow_bias) {
	// Zero-out the ubo state.
	::new (&scene_state.ubo) SceneState::UBO{};

	scene_state.ubo.directional_light_count = p_render_data->directional_light_count - p_render_data->directional_shadow_count;

	Projection correction;
	correction.set_depth_correction(p_flip_y, true, false);
	Projection projection = correction * p_render_data->cam_projection;

	GLES2::MaterialStorage::store_camera(projection, scene_state.ubo.projection_matrix);
	GLES2::MaterialStorage::store_camera(projection.inverse(), scene_state.ubo.inv_projection_matrix);
	GLES2::MaterialStorage::store_transform(p_render_data->cam_transform, scene_state.ubo.inv_view_matrix);
	GLES2::MaterialStorage::store_transform(p_render_data->inv_cam_transform, scene_state.ubo.view_matrix);

	scene_state.ubo.camera_visible_layers = p_render_data->camera_visible_layers;

	scene_state.ubo.z_far = p_render_data->z_far;
	scene_state.ubo.z_near = p_render_data->z_near;

	scene_state.ubo.viewport_size[0] = p_screen_size.x;
	scene_state.ubo.viewport_size[1] = p_screen_size.y;

	Size2 screen_pixel_size = Vector2(1.0, 1.0) / Size2(MAX(1, p_screen_size.x), MAX(1, p_screen_size.y));
	scene_state.ubo.screen_pixel_size[0] = screen_pixel_size.x;
	scene_state.ubo.screen_pixel_size[1] = screen_pixel_size.y;

	scene_state.ubo.time = time;

	scene_state.tonemap_ubo.exposure = 1.0;
	scene_state.tonemap_ubo.white = 1.0;

	if (is_environment(p_render_data->environment)) {
		scene_state.tonemap_ubo.exposure = environment_get_exposure(p_render_data->environment);
		scene_state.tonemap_ubo.white = environment_get_white(p_render_data->environment);
	}

	if (p_render_data->camera_attributes.is_valid()) {
		scene_state.tonemap_ubo.exposure *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
	}

	if (is_environment(p_render_data->environment)) {
		Sky *sky = sky_owner.get_or_null(environment_get_sky(p_render_data->environment));

		glActiveTexture(GL_TEXTURE0 + GLES2_CONFIG->max_texture_image_units - 2);
		if (sky && sky->radiance != 0) {
			glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
		} else {
			glBindTexture(GL_TEXTURE_CUBE_MAP, GLES2::TextureStorage::get_singleton()->texture_get_texid(GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_CUBEMAP_BLACK)));
		}
		glActiveTexture(GL_TEXTURE0);

		RS::EnvironmentBG env_bg = environment_get_background(p_render_data->environment);
		RS::EnvironmentAmbientSource ambient_src = environment_get_ambient_source(p_render_data->environment);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(p_render_data->environment);

		scene_state.ubo.ambient_light_color_energy[3] = bg_energy_multiplier;
		scene_state.ubo.ambient_color_sky_mix = environment_get_ambient_sky_contribution(p_render_data->environment);
		scene_state.ubo.fog_enabled = environment_get_fog_enabled(p_render_data->environment);
		scene_state.ubo.fog_density = environment_get_fog_density(p_render_data->environment);
		Color fog_color = environment_get_fog_light_color(p_render_data->environment).srgb_to_linear();
		scene_state.ubo.fog_light_color[0] = fog_color.r;
		scene_state.ubo.fog_light_color[1] = fog_color.g;
		scene_state.ubo.fog_light_color[2] = fog_color.b;

		if (ambient_src == RS::ENV_AMBIENT_SOURCE_BG && (env_bg == RS::ENV_BG_CLEAR_COLOR || env_bg == RS::ENV_BG_COLOR)) {
			Color color = env_bg == RS::ENV_BG_CLEAR_COLOR ? p_default_bg_color : environment_get_bg_color(p_render_data->environment);
			color = color.srgb_to_linear();

			scene_state.ubo.ambient_light_color_energy[0] = color.r * bg_energy_multiplier;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * bg_energy_multiplier;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * bg_energy_multiplier;
			scene_state.ubo.use_ambient_light = 1;
			scene_state.ubo.use_ambient_cubemap = 0;
		} else {
			float energy = environment_get_ambient_light_energy(p_render_data->environment);
			Color color = environment_get_ambient_light(p_render_data->environment);
			color = color.srgb_to_linear();
			scene_state.ubo.ambient_light_color_energy[0] = color.r * energy;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * energy;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * energy;

			Basis sky_transform = environment_get_sky_orientation(p_render_data->environment);
			sky_transform = sky_transform.inverse() * p_render_data->cam_transform.basis;
			scene_state.ubo.radiance_inverse_xform[0] = sky_transform.rows[0][0];
			scene_state.ubo.radiance_inverse_xform[1] = sky_transform.rows[1][0];
			scene_state.ubo.radiance_inverse_xform[2] = sky_transform.rows[2][0];
			scene_state.ubo.radiance_inverse_xform[3] = 0.0;
			scene_state.ubo.radiance_inverse_xform[4] = sky_transform.rows[0][1];
			scene_state.ubo.radiance_inverse_xform[5] = sky_transform.rows[1][1];
			scene_state.ubo.radiance_inverse_xform[6] = sky_transform.rows[2][1];
			scene_state.ubo.radiance_inverse_xform[7] = 0.0;
			scene_state.ubo.radiance_inverse_xform[8] = sky_transform.rows[0][2];
			scene_state.ubo.radiance_inverse_xform[9] = sky_transform.rows[1][2];
			scene_state.ubo.radiance_inverse_xform[10] = sky_transform.rows[2][2];
			scene_state.ubo.radiance_inverse_xform[11] = 0.0;

			scene_state.ubo.use_ambient_cubemap = ((ambient_src == RS::ENV_AMBIENT_SOURCE_BG && env_bg == RS::ENV_BG_SKY) || ambient_src == RS::ENV_AMBIENT_SOURCE_SKY) ? 1 : 0;
			scene_state.ubo.use_ambient_light = (scene_state.ubo.use_ambient_cubemap || ambient_src == RS::ENV_AMBIENT_SOURCE_COLOR) ? 1 : 0;
		}

		//specular
		RS::EnvironmentReflectionSource ref_src = environment_get_reflection_source(p_render_data->environment);
		if ((ref_src == RS::ENV_REFLECTION_SOURCE_BG && env_bg == RS::ENV_BG_SKY) || ref_src == RS::ENV_REFLECTION_SOURCE_SKY) {
			scene_state.ubo.use_reflection_cubemap = 1;
		} else {
			scene_state.ubo.use_reflection_cubemap = 0;
		}
	} else {
		glActiveTexture(GL_TEXTURE0 + GLES2_CONFIG->max_texture_image_units - 2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, GLES2::TextureStorage::get_singleton()->texture_get_texid(GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_CUBEMAP_BLACK)));
		glActiveTexture(GL_TEXTURE0);

		scene_state.ubo.use_ambient_light = 1;
		scene_state.ubo.use_ambient_cubemap = 0;
		Color linear_bg = p_default_bg_color.srgb_to_linear();
		scene_state.ubo.ambient_light_color_energy[0] = linear_bg.r;
		scene_state.ubo.ambient_light_color_energy[1] = linear_bg.g;
		scene_state.ubo.ambient_light_color_energy[2] = linear_bg.b;
		scene_state.ubo.ambient_light_color_energy[3] = 1.0f;
		scene_state.ubo.fog_enabled = false;
	}
}

// Puts lights into Uniform Buffers. Needs to be called before _fill_list as this caches the index of each light in the Uniform Buffer
void RasterizerSceneGLES2::_setup_lights(const RenderDataGLES2 *p_render_data, bool p_using_shadows, uint32_t &r_directional_light_count, uint32_t &r_omni_light_count, uint32_t &r_spot_light_count, uint32_t &r_directional_shadow_count) {
	GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();
	GLES2::Config *config = GLES2::Config::get_singleton();

	const Transform3D inverse_transform = p_render_data->inv_cam_transform;
	const PagedArray<RID> &lights = *p_render_data->lights;

	r_directional_light_count = 0;
	r_omni_light_count = 0;
	r_spot_light_count = 0;
	r_directional_shadow_count = 0;

	int num_lights = lights.size();

	for (int i = 0; i < num_lights; i++) {
		GLES2::LightInstance *li = GLES2::LightStorage::get_singleton()->get_light_instance(lights[i]);
		if (!li) {
			continue;
		}
		RID base = li->light;

		ERR_CONTINUE(base.is_null());

		RS::LightType type = light_storage->light_get_type(base);
		switch (type) {
			case RS::LIGHT_DIRECTIONAL: {
				if (r_directional_light_count >= RendererSceneRender::MAX_DIRECTIONAL_LIGHTS || light_storage->light_directional_get_sky_mode(base) == RS::LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY) {
					continue;
				}

				DirectionalLightData &light_data = scene_state.directional_lights[r_directional_light_count];
				Transform3D light_transform = li->transform;
				Vector3 direction = inverse_transform.basis.xform(light_transform.basis.xform(Vector3(0, 0, 1))).normalized();

				light_data.direction[0] = direction.x;
				light_data.direction[1] = direction.y;
				light_data.direction[2] = direction.z;
				light_data.bake_mode = light_storage->light_get_bake_mode(base);

				float sign = light_storage->light_is_negative(base) ? -1 : 1;
				light_data.energy = sign * light_storage->light_get_param(base, RS::LIGHT_PARAM_ENERGY);

				if (is_using_physical_light_units()) {
					light_data.energy *= light_storage->light_get_param(base, RS::LIGHT_PARAM_INTENSITY);
				} else {
					light_data.energy *= Math_PI;
				}

				if (p_render_data->camera_attributes.is_valid()) {
					light_data.energy *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
				}

				Color linear_col = light_storage->light_get_color(base).srgb_to_linear();
				light_data.color[0] = linear_col.r;
				light_data.color[1] = linear_col.g;
				light_data.color[2] = linear_col.b;

				float size = light_storage->light_get_param(base, RS::LIGHT_PARAM_SIZE);
				light_data.size = 1.0f - Math::cos(Math::deg_to_rad(size));

				light_data.specular = light_storage->light_get_param(base, RS::LIGHT_PARAM_SPECULAR);
				light_data.shadow_opacity = 0.0f; // Shadows not fully implemented in this block yet

				r_directional_light_count++;
			} break;
			case RS::LIGHT_OMNI: {
				if (r_omni_light_count >= (uint32_t)config->max_renderable_lights) {
					continue;
				}
				const real_t distance = p_render_data->cam_transform.origin.distance_to(li->transform.origin);

				if (light_storage->light_is_distance_fade_enabled(li->light)) {
					const float fade_begin = light_storage->light_get_distance_fade_begin(li->light);
					const float fade_length = light_storage->light_get_distance_fade_length(li->light);

					if (distance > fade_begin) {
						if (distance > fade_begin + fade_length) {
							continue;
						}
					}
				}

				scene_state.omni_light_sort[r_omni_light_count].instance = li;
				scene_state.omni_light_sort[r_omni_light_count].depth = distance;
				r_omni_light_count++;
			} break;
			case RS::LIGHT_SPOT: {
				if (r_spot_light_count >= (uint32_t)config->max_renderable_lights) {
					continue;
				}
				const real_t distance = p_render_data->cam_transform.origin.distance_to(li->transform.origin);

				if (light_storage->light_is_distance_fade_enabled(li->light)) {
					const float fade_begin = light_storage->light_get_distance_fade_begin(li->light);
					const float fade_length = light_storage->light_get_distance_fade_length(li->light);

					if (distance > fade_begin) {
						if (distance > fade_begin + fade_length) {
							continue;
						}
					}
				}

				scene_state.spot_light_sort[r_spot_light_count].instance = li;
				scene_state.spot_light_sort[r_spot_light_count].depth = distance;
				r_spot_light_count++;
			} break;
		}
		li->last_pass = RSG::rasterizer->get_frame_number();
	}

	if (r_omni_light_count) {
		SortArray<InstanceSort<GLES2::LightInstance>> sorter{};
		sorter.sort(scene_state.omni_light_sort, r_omni_light_count);
	}

	if (r_spot_light_count) {
		SortArray<InstanceSort<GLES2::LightInstance>> sorter{};
		sorter.sort(scene_state.spot_light_sort, r_spot_light_count);
	}

	for (uint32_t i = 0; i < (r_omni_light_count + r_spot_light_count); i++) {
		uint32_t index = (i < r_omni_light_count) ? i : i - r_omni_light_count;
		LightData &light_data = (i < r_omni_light_count) ? scene_state.omni_lights[index] : scene_state.spot_lights[index];
		RS::LightType type = (i < r_omni_light_count) ? RS::LIGHT_OMNI : RS::LIGHT_SPOT;
		GLES2::LightInstance *li = (i < r_omni_light_count) ? scene_state.omni_light_sort[index].instance : scene_state.spot_light_sort[index].instance;
		RID base = li->light;

		li->gl_id = index;

		Transform3D light_transform = li->transform;
		Vector3 pos = inverse_transform.xform(light_transform.origin);

		light_data.position[0] = pos.x;
		light_data.position[1] = pos.y;
		light_data.position[2] = pos.z;
		light_data.bake_mode = light_storage->light_get_bake_mode(base);

		float radius = MAX(0.001f, light_storage->light_get_param(base, RS::LIGHT_PARAM_RANGE));
		light_data.inv_radius = 1.0f / radius;

		Vector3 direction = inverse_transform.basis.xform(light_transform.basis.xform(Vector3(0, 0, -1))).normalized();

		light_data.direction[0] = direction.x;
		light_data.direction[1] = direction.y;
		light_data.direction[2] = direction.z;
		light_data.size = light_storage->light_get_param(base, RS::LIGHT_PARAM_SIZE);

		float sign = light_storage->light_is_negative(base) ? -1 : 1;
		Color linear_col = light_storage->light_get_color(base).srgb_to_linear();
		
		real_t distance = (i < r_omni_light_count) ? scene_state.omni_light_sort[index].depth : scene_state.spot_light_sort[index].depth;
		float fade = 1.0;
		float shadow_opacity_fade = 1.0;

		if (light_storage->light_is_distance_fade_enabled(base)) {
			float fade_begin = light_storage->light_get_distance_fade_begin(base);
			float fade_shadow = light_storage->light_get_distance_fade_shadow(base);
			float fade_length = light_storage->light_get_distance_fade_length(base);

			if (distance > fade_begin) {
				fade = Math::smoothstep(0.0f, 1.0f, 1.0f - float(distance - fade_begin) / fade_length);
			}
			if (distance > fade_shadow) {
				shadow_opacity_fade = Math::smoothstep(0.0f, 1.0f, 1.0f - float(distance - fade_shadow) / fade_length);
			}
		}

		float energy = sign * light_storage->light_get_param(base, RS::LIGHT_PARAM_ENERGY) * fade;

		if (is_using_physical_light_units()) {
			energy *= light_storage->light_get_param(base, RS::LIGHT_PARAM_INTENSITY);
			if (type == RS::LIGHT_OMNI) {
				energy *= 1.0f / (Math_PI * 4.0f);
			} else {
				energy *= 1.0f / Math_PI;
			}
		} else {
			energy *= Math_PI;
		}

		if (p_render_data->camera_attributes.is_valid()) {
			energy *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
		}

		light_data.color[0] = linear_col.r * energy;
		light_data.color[1] = linear_col.g * energy;
		light_data.color[2] = linear_col.b * energy;

		light_data.attenuation = light_storage->light_get_param(base, RS::LIGHT_PARAM_ATTENUATION);
		light_data.inv_spot_attenuation = 1.0f / light_storage->light_get_param(base, RS::LIGHT_PARAM_SPOT_ATTENUATION);
		float spot_angle = light_storage->light_get_param(base, RS::LIGHT_PARAM_SPOT_ANGLE);
		light_data.cos_spot_angle = Math::cos(Math::deg_to_rad(spot_angle));
		light_data.specular_amount = light_storage->light_get_param(base, RS::LIGHT_PARAM_SPECULAR) * 2.0f;
		light_data.shadow_opacity = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_OPACITY) * shadow_opacity_fade;
	}

	scene_state.omni_light_count = r_omni_light_count;
	scene_state.spot_light_count = r_spot_light_count;
	scene_state.ubo.directional_light_count = r_directional_light_count;
}

// Render shadows
void RasterizerSceneGLES2::_render_shadows(const RenderDataGLES2 *p_render_data, const Size2i &p_viewport_size) {

}

void RasterizerSceneGLES2::_render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, RenderingMethod::RenderInfo *p_render_info, const Size2i &p_viewport_size, const Transform3D &p_main_cam_transform) {

}

/* BINDING UTILITIES */

void RasterizerSceneGLES2::_bind_sky_directional_lights(RID p_version, SkyShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHT_COUNT, (int)sky_globals.directional_light_count, p_version, p_variant, p_spec_constants);
	for (uint32_t i = 0; i < sky_globals.directional_light_count; i++) {
		const DirectionalLightData &light = sky_globals.directional_lights[i];
		Vector4 dir_energy(light.direction[0], light.direction[1], light.direction[2], light.energy);
		Vector4 col_size(light.color[0], light.color[1], light.color[2], light.size);
		int32_t enabled = light.enabled ? 1 : 0;

		switch (i) {
			case 0:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_DIRECTION_ENERGY, dir_energy, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_COLOR_SIZE, col_size, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_ENABLED, enabled, p_version, p_variant, p_spec_constants);
				break;
			case 1:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_1_DIRECTION_ENERGY, dir_energy, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_1_COLOR_SIZE, col_size, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_1_ENABLED, enabled, p_version, p_variant, p_spec_constants);
				break;
			case 2:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_2_DIRECTION_ENERGY, dir_energy, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_2_COLOR_SIZE, col_size, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_2_ENABLED, enabled, p_version, p_variant, p_spec_constants);
				break;
			case 3:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_3_DIRECTION_ENERGY, dir_energy, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_3_COLOR_SIZE, col_size, p_version, p_variant, p_spec_constants);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_3_ENABLED, enabled, p_version, p_variant, p_spec_constants);
				break;
		}
	}
}

void RasterizerSceneGLES2::_bind_scene_camera_uniforms(RID p_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	Projection proj;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			proj.columns[i][j] = scene_state.ubo.projection_matrix[i * 4 + j];
		}
	}

	Projection inv_proj;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			inv_proj.columns[i][j] = scene_state.ubo.inv_projection_matrix[i * 4 + j];
		}
	}

	Transform3D view;
	_gl_reconstruct_view_matrix(view);

	Transform3D inv_view;
	_gl_reconstruct_view_matrix(inv_view);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::PROJECTION_MATRIX, proj, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::INV_PROJECTION_MATRIX, inv_proj, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::VIEW_MATRIX, view, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::INV_VIEW_MATRIX, inv_view, p_version, p_variant, p_spec_constants);

	Vector2 vp_size(scene_state.ubo.viewport_size[0], scene_state.ubo.viewport_size[1]);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::VIEWPORT_SIZE, vp_size, p_version, p_variant, p_spec_constants);

	Vector2 screen_pixel_size(scene_state.ubo.screen_pixel_size[0], scene_state.ubo.screen_pixel_size[1]);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SCREEN_PIXEL_SIZE, screen_pixel_size, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::TIME, (float)scene_state.ubo.time, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::AMBIENT_LIGHT_COLOR_ENERGY, Color(scene_state.ubo.ambient_light_color_energy[0], scene_state.ubo.ambient_light_color_energy[1], scene_state.ubo.ambient_light_color_energy[2], scene_state.ubo.ambient_light_color_energy[3]), p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::AMBIENT_COLOR_SKY_MIX, scene_state.ubo.ambient_color_sky_mix, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_AMBIENT_LIGHT, (bool)scene_state.ubo.use_ambient_light, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_AMBIENT_CUBEMAP, (bool)scene_state.ubo.use_ambient_cubemap, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_REFLECTION_CUBEMAP, (bool)scene_state.ubo.use_reflection_cubemap, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_REFLECTION_CUBEMAP, (bool)scene_state.ubo.use_reflection_cubemap, p_version, p_variant, p_spec_constants);

	Basis radiance_inverse_xform;
	radiance_inverse_xform.rows[0][0] = scene_state.ubo.radiance_inverse_xform[0];
	radiance_inverse_xform.rows[1][0] = scene_state.ubo.radiance_inverse_xform[1];
	radiance_inverse_xform.rows[2][0] = scene_state.ubo.radiance_inverse_xform[2];
	radiance_inverse_xform.rows[0][1] = scene_state.ubo.radiance_inverse_xform[4];
	radiance_inverse_xform.rows[1][1] = scene_state.ubo.radiance_inverse_xform[5];
	radiance_inverse_xform.rows[2][1] = scene_state.ubo.radiance_inverse_xform[6];
	radiance_inverse_xform.rows[0][2] = scene_state.ubo.radiance_inverse_xform[8];
	radiance_inverse_xform.rows[1][2] = scene_state.ubo.radiance_inverse_xform[9];
	radiance_inverse_xform.rows[2][2] = scene_state.ubo.radiance_inverse_xform[10];

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::RADIANCE_INVERSE_XFORM, radiance_inverse_xform, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_LIGHT_COUNT, (int)scene_state.ubo.directional_light_count, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Z_FAR, scene_state.ubo.z_far, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Z_NEAR, scene_state.ubo.z_near, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_ENABLED, (bool)scene_state.ubo.fog_enabled, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_DENSITY, scene_state.ubo.fog_density, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_HEIGHT, scene_state.ubo.fog_height, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_HEIGHT_DENSITY, scene_state.ubo.fog_height_density, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_LIGHT_COLOR, Vector3(scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2]), p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_SUN_SCATTER, scene_state.ubo.fog_sun_scatter, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_AERIAL_PERSPECTIVE, scene_state.ubo.fog_aerial_perspective, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_LIGHT_COLOR, Vector3(scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2]), p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_SUN_SCATTER, scene_state.ubo.fog_sun_scatter, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_AERIAL_PERSPECTIVE, scene_state.ubo.fog_aerial_perspective, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::EXPOSURE, scene_state.tonemap_ubo.exposure, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WHITE, scene_state.tonemap_ubo.white, p_version, p_variant, p_spec_constants);

	if (!(p_spec_constants & SceneShaderGLES2::DISABLE_LIGHT_DIRECTIONAL)) {
		for (uint32_t i = 0; i < scene_state.ubo.directional_light_count && i < 8; i++) {
			int base_idx = SceneShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_DIRECTION_ENERGY + (i * 3);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(scene_state.directional_lights[i].direction[0], scene_state.directional_lights[i].direction[1], scene_state.directional_lights[i].direction[2], scene_state.directional_lights[i].energy), p_version, p_variant, p_spec_constants);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(scene_state.directional_lights[i].color[0], scene_state.directional_lights[i].color[1], scene_state.directional_lights[i].color[2], scene_state.directional_lights[i].size), p_version, p_variant, p_spec_constants);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), 1, p_version, p_variant, p_spec_constants);
		}
	}
}

void RasterizerSceneGLES2::render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, const RenderSDFGIUpdateData *p_sdfgi_update_data, RenderingMethod::RenderInfo *r_render_info) {
	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();
	RENDER_TIMESTAMP("Setup 3D Scene");

	bool is_reflection_probe = p_reflection_probe.is_valid();

	Ref<RenderSceneBuffersGLES2> rb = p_render_buffers;
	ERR_FAIL_COND(rb.is_null());

	GLES2::RenderTarget *rt = nullptr;
	if (!is_reflection_probe) {
		rt = texture_storage->get_render_target(rb->render_target);
		ERR_FAIL_NULL(rt);
	}

	RenderDataGLES2 render_data;
	{
		render_data.render_buffers = rb;
		if (rt) {
			render_data.transparent_bg = rt->is_transparent;
		}
		render_data.cam_transform = p_camera_data->main_transform;
		render_data.inv_cam_transform = render_data.cam_transform.affine_inverse();
		render_data.cam_projection = p_camera_data->main_projection;
		render_data.cam_orthogonal = p_camera_data->is_orthogonal;
		render_data.camera_visible_layers = p_camera_data->visible_layers;

		render_data.instances = &p_instances;
		render_data.lights = &p_lights;
		render_data.environment = p_environment;
		render_data.camera_attributes = p_camera_attributes;
		render_data.reflection_probe = p_reflection_probe;
		render_data.reflection_probe_pass = p_reflection_probe_pass;
	}

	Color clear_color = texture_storage->get_default_clear_color();
	if (rb.is_valid()) {
		clear_color = texture_storage->render_target_get_clear_request_color(rb->render_target);
	}
	Size2i screen_size(1, 1);

	if (rb.is_valid()) {
		screen_size.width = rb->internal_size.width;
		screen_size.height = rb->internal_size.height;
	}

	bool reverse_cull = render_data.cam_transform.basis.determinant() < 0;
	bool use_wireframe = get_debug_draw_mode() == RS::VIEWPORT_DEBUG_DRAW_WIREFRAME;
	bool draw_sky = false;
	bool draw_sky_fog_only = false;
	bool keep_color = false;
	float sky_energy_multiplier = 1.0;
	bool flip_y = !is_reflection_probe;

	if (rt && rt->overridden.color.is_valid()) {
		// If we've overridden the render target's color texture, then don't render upside down.
		// We're probably rendering directly to an XR device.
		flip_y = false;
	}
	if (!flip_y) {
		reverse_cull = !reverse_cull;
	}

	if (get_debug_draw_mode() == RS::VIEWPORT_DEBUG_DRAW_OVERDRAW) {
		clear_color = Color(0, 0, 0, 1);
	} else if (render_data.environment.is_valid()) {
		RS::EnvironmentBG bg_mode = environment_get_background(render_data.environment);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(render_data.environment);
		bg_energy_multiplier *= environment_get_bg_intensity(render_data.environment);

		if (render_data.camera_attributes.is_valid()) {
			bg_energy_multiplier *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(render_data.camera_attributes);
		}

		switch (bg_mode) {
			case RS::ENV_BG_CLEAR_COLOR: {
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
				if (environment_get_fog_enabled(render_data.environment)) {
					draw_sky_fog_only = true;
					GLES2::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));
				}
			} break;
			case RS::ENV_BG_COLOR: {
				clear_color = environment_get_bg_color(render_data.environment);
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
				if (environment_get_fog_enabled(render_data.environment)) {
					draw_sky_fog_only = true;
					GLES2::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));
				}
			} break;
			case RS::ENV_BG_SKY: {
				draw_sky = true;
			} break;
			case RS::ENV_BG_CANVAS:
			case RS::ENV_BG_KEEP: {
				keep_color = true;
			} break;
			default: {
			}
		}

		if (draw_sky || draw_sky_fog_only || environment_get_reflection_source(render_data.environment) == RS::ENV_REFLECTION_SOURCE_SKY || environment_get_ambient_source(render_data.environment) == RS::ENV_AMBIENT_SOURCE_SKY) {
			RENDER_TIMESTAMP("Setup Sky");
			Projection projection = render_data.cam_projection;
			if (render_data.reflection_probe.is_valid()) {
				Projection correction;
				correction.set_depth_correction(true, true, false);
				projection = correction * render_data.cam_projection;
			}

			sky_energy_multiplier *= bg_energy_multiplier;

			_setup_sky(&render_data, *render_data.lights, projection, render_data.cam_transform, screen_size);

			if (environment_get_sky(render_data.environment).is_valid()) {
				if (environment_get_reflection_source(render_data.environment) == RS::ENV_REFLECTION_SOURCE_SKY || environment_get_ambient_source(render_data.environment) == RS::ENV_AMBIENT_SOURCE_SKY || (environment_get_reflection_source(render_data.environment) == RS::ENV_REFLECTION_SOURCE_BG && environment_get_background(render_data.environment) == RS::ENV_BG_SKY)) {
					_update_sky_radiance(render_data.environment, projection, render_data.cam_transform, sky_energy_multiplier);
				}
			} else {
				draw_sky = false;
			}
		}
	} else {
		draw_sky = true;
		GLES2::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));

		Projection sky_proj = render_data.cam_projection;
		_setup_sky(&render_data, *render_data.lights, sky_proj, render_data.cam_transform, screen_size);
	}

	// TODO(GLES2): Implement these.
	//_render_shadows(&render_data, screen_size);
	_setup_lights(&render_data, true, render_data.directional_light_count, render_data.omni_light_count, render_data.spot_light_count, render_data.directional_shadow_count);

	_setup_environment(&render_data, false, screen_size, flip_y, clear_color, false);

	_fill_render_list<RENDER_LIST_OPAQUE, PASS_MODE_COLOR>(&render_data);
	render_list[RENDER_LIST_OPAQUE].sort_by_key();
	render_list[RENDER_LIST_ALPHA].sort_by_reverse_depth_and_priority();

	scene_state.reset_gl_state();

	if (rb.is_valid()) {
		GLES2::TextureStorage::get_singleton()->bind_framebuffer(rb->get_render_fbo());
		glViewport(0, 0, screen_size.width, screen_size.height);
	}

	scene_state.enable_gl_depth_draw(true);

	if (!keep_color) {
		glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
		// Reverse-Z, far clipping plane is 0.0
		RasterizerGLES2::clear_depth(0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::render_scene: glClear");
	} else {
		RasterizerGLES2::clear_depth(0.0f);
		glClear(GL_DEPTH_BUFFER_BIT);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::render_scene: glClear");
	}

	if (rb.is_valid()) {
		GLES2::TextureStorage::get_singleton()->render_target_disable_clear_request(rb->render_target);
	}

	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(true);

	// Near (1.0) is less than far (0.0)
	glDepthFunc(GL_GEQUAL);

	uint64_t spec_constant_base_flags = 0;

	// Opaque Pass
	scene_state.enable_gl_blend(false);
	RenderListParameters render_list_params(render_list[RENDER_LIST_OPAQUE].elements.ptr(), render_list[RENDER_LIST_OPAQUE].elements.size(), reverse_cull, spec_constant_base_flags, use_wireframe);
	_render_list_template<PASS_MODE_COLOR>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_OPAQUE].elements.size());

	scene_state.enable_gl_depth_draw(false);

	// Sky background pass
	if (draw_sky) {
		scene_state.enable_gl_depth_test(true);
		scene_state.enable_gl_blend(false);
		scene_state.set_gl_cull_mode(RS::CULL_MODE_BACK);
		_draw_sky(render_data.environment, render_data.cam_projection, render_data.cam_transform, sky_energy_multiplier, 1.0, false, flip_y, false);
	}

	// Transparent Pass
	scene_state.enable_gl_blend(true);
	glBlendEquation(GL_FUNC_ADD);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::render_scene: glBlendEquation");

	if (render_data.transparent_bg) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::render_scene: glBlendFuncSeparate");

	RenderListParameters render_list_params_alpha(render_list[RENDER_LIST_ALPHA].elements.ptr(), render_list[RENDER_LIST_ALPHA].elements.size(), reverse_cull, spec_constant_base_flags, use_wireframe);
	_render_list_template<PASS_MODE_COLOR_TRANSPARENT>(&render_list_params_alpha, &render_data, 0, render_list[RENDER_LIST_ALPHA].elements.size(), true);

	// Rescue the 3D scene from the internal buffer if it was used.
	_render_post_processing(&render_data);

	// Clean up GL state
	scene_state.reset_gl_state();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (GLES2_CONFIG->support_vao) {
		glBindVertexArray(0);
	}
	GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::render_scene: reset_gl_state cleanup");
}

void RasterizerSceneGLES2::_render_post_processing(const RenderDataGLES2 *p_render_data) {
	Ref<RenderSceneBuffersGLES2> rb = p_render_data->render_buffers;
	if (rb.is_null() || rb->internal3d.color == 0) {
		return; // We rendered directly to the RenderTarget; no blit required.
	}

	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();

	// Bind the actual final render target FBO
	GLuint rt_fbo = texture_storage->render_target_get_fbo(rb->render_target);
	texture_storage->bind_framebuffer(rt_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_post_processing: bind_framebuffer");

	// Set up pure blitting state
	glViewport(0, 0, rb->target_size.width, rb->target_size.height);
	scene_state.enable_gl_blend(false);
	scene_state.enable_gl_depth_test(false);
	scene_state.set_gl_cull_mode(RS::CULL_MODE_DISABLED);
	scene_state.enable_gl_scissor_test(false);
	scene_state.enable_gl_depth_draw(false);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_post_processing: glColorMask");

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, rb->internal3d.color);

	if (rb->scaling_3d_mode == RS::VIEWPORT_SCALING_3D_MODE_BILINEAR) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	GLES2::CopyEffects::get_singleton()->copy_to_rect(Rect2(0, 0, 1, 1));

	glBindTexture(GL_TEXTURE_2D, 0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_post_processing: copy_to_rect");
}

template <RasterizerSceneGLES2::PassMode p_pass_mode>
void RasterizerSceneGLES2::_render_list_template(RenderListParameters *p_params, const RenderDataGLES2 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass) {
	if (p_from_element >= p_to_element) {
		return;
	}

	int count = p_to_element - p_from_element;
	GeometryInstanceSurface **surfaces = &p_params->elements[p_from_element];

	glFrontFace(p_params->reverse_cull ? GL_CW : GL_CCW);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_list_template: glFrontFace");

	// Dispatch batch processor or immediate drawer
	batch_scene_render_items(surfaces, count, p_render_data->cam_transform, p_alpha_pass, p_pass_mode);

	// Clean-up
	glFrontFace(GL_CCW);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_list_template: glFrontFace restore");
}

void RasterizerSceneGLES2::render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) {
}

void RasterizerSceneGLES2::render_particle_collider_heightfield(RID p_collider, const Transform3D &p_transform, const PagedArray<RenderGeometryInstance *> &p_instances) {

}

void RasterizerSceneGLES2::_render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, GLuint p_framebuffer, const Rect2i &p_region) {

}

void RasterizerSceneGLES2::set_time(double p_time, double p_step) {
	time = p_time;
	time_step = p_step;
}

void RasterizerSceneGLES2::set_debug_draw_mode(RS::ViewportDebugDraw p_debug_draw) {
	debug_draw = p_debug_draw;
}

Ref<RenderSceneBuffers> RasterizerSceneGLES2::render_buffers_create() {
	Ref<RenderSceneBuffersGLES2> rb;
	rb.instantiate();
	ERR_FAIL_COND_V(rb.is_null(), Ref<RenderSceneBuffers>());
	return rb;
}

void RasterizerSceneGLES2::_render_buffers_debug_draw(Ref<RenderSceneBuffersGLES2> p_render_buffers, RID p_shadow_atlas, GLuint p_fbo) {
}

void RasterizerSceneGLES2::gi_set_use_half_resolution(bool p_enable) {
}

void RasterizerSceneGLES2::screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_curve) {
}

bool RasterizerSceneGLES2::screen_space_roughness_limiter_is_active() const {
	return false;
}

void RasterizerSceneGLES2::sub_surface_scattering_set_quality(RS::SubSurfaceScatteringQuality p_quality) {
}

void RasterizerSceneGLES2::sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) {
}

TypedArray<Image> RasterizerSceneGLES2::bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) {
    return TypedArray<Image>();
}

bool RasterizerSceneGLES2::free(RID p_rid) {
	if (is_environment(p_rid)) {
		environment_free(p_rid);
	} else if (sky_owner.owns(p_rid)) {
		Sky *sky = sky_owner.get_or_null(p_rid);
		ERR_FAIL_NULL_V(sky, false);
		_free_sky_data(sky);
		sky_owner.free(p_rid);
	} else if (GLES2::LightStorage::get_singleton()->owns_light_instance(p_rid)) {
		GLES2::LightStorage::get_singleton()->light_instance_free(p_rid);
	} else if (RSG::camera_attributes->owns_camera_attributes(p_rid)) {
		//not much to delete, just free it
		RSG::camera_attributes->camera_attributes_free(p_rid);
	} else if (is_compositor(p_rid)) {
		compositor_free(p_rid);
	} else if (is_compositor_effect(p_rid)) {
		compositor_effect_free(p_rid);
	} else {
		return false;
	}
	return true;
}

void RasterizerSceneGLES2::update() {
	_update_dirty_skys();
}

void RasterizerSceneGLES2::sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) {
}

void RasterizerSceneGLES2::decals_set_filter(RS::DecalFilter p_filter) {
}

void RasterizerSceneGLES2::light_projectors_set_filter(RS::LightProjectorFilter p_filter) {
}

void RasterizerSceneGLES2::lightmaps_set_bicubic_filter(bool p_enable) {
	lightmap_bicubic_upscale = p_enable;
}

RasterizerSceneGLES2::RasterizerSceneGLES2() {
	singleton = this;

	initialize();

	batch_constructor();
}

RasterizerSceneGLES2::~RasterizerSceneGLES2() {
	singleton = nullptr;

	if (scene_state.ubo_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(scene_state.ubo_buffer);
	}

	// Scene Shader
	GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_free(scene_globals.shader_default_version);
	RSG::material_storage->material_free(scene_globals.default_material);
	RSG::material_storage->shader_free(scene_globals.default_shader);

	// Sky Shader
	memdelete_arr(scene_state.directional_lights);
	memdelete_arr(scene_state.omni_lights);
	memdelete_arr(scene_state.spot_lights);
	memdelete_arr(scene_state.omni_light_sort);
	memdelete_arr(scene_state.spot_light_sort);
	memdelete_arr(scene_state.positional_shadows);
	memdelete_arr(scene_state.directional_shadows);

	// Overdraw Shader
	RSG::material_storage->material_free(scene_globals.overdraw_material);
	RSG::material_storage->shader_free(scene_globals.overdraw_shader);

	// Sky Shader
	GLES2::MaterialStorage::get_singleton()->shaders.sky_shader.version_free(sky_globals.shader_default_version);
	RSG::material_storage->material_free(sky_globals.default_material);
	RSG::material_storage->shader_free(sky_globals.default_shader);
	RSG::material_storage->material_free(sky_globals.fog_material);
	RSG::material_storage->shader_free(sky_globals.fog_shader);
	memdelete_arr(sky_globals.directional_lights);
	memdelete_arr(sky_globals.last_frame_directional_lights);

	if (sky_globals.screen_triangle != 0) {
		glDeleteBuffers(1, &sky_globals.screen_triangle);
		glDeleteVertexArrays(1, &sky_globals.screen_triangle_array);
	}
}

#endif // GLES2_ENABLED
