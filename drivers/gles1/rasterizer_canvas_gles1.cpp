/**************************************************************************/
/*  rasterizer_canvas_gles1.cpp                                           */
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

#include "rasterizer_canvas_gles1.h"

#ifdef GLES1_ENABLED

#include "rasterizer_gles1.h"
#include "rasterizer_scene_gles1.h"

#include "drivers/gles_common/error_macros.h"

#include "core/config/project_settings.h"
#include "core/math/geometry_2d.h"
#include "core/math/transform_interpolator.h"
#include "core/os/os.h"
#include "servers/display_server.h"
#include "servers/rendering/rendering_server_default.h"
#include "storage/config.h"
#include "storage/material_storage.h"
#include "storage/mesh_storage.h"
#include "storage/particles_storage.h"
#include "storage/texture_storage.h"

void RasterizerCanvasGLES1::_update_transform_2d_to_mat4(const Transform2D &p_transform, float *p_mat4) {
	p_mat4[0] = p_transform.columns[0][0];
	p_mat4[1] = p_transform.columns[0][1];
	p_mat4[2] = 0;
	p_mat4[3] = 0;
	p_mat4[4] = p_transform.columns[1][0];
	p_mat4[5] = p_transform.columns[1][1];
	p_mat4[6] = 0;
	p_mat4[7] = 0;
	p_mat4[8] = 0;
	p_mat4[9] = 0;
	p_mat4[10] = 1;
	p_mat4[11] = 0;
	p_mat4[12] = p_transform.columns[2][0];
	p_mat4[13] = p_transform.columns[2][1];
	p_mat4[14] = 0;
	p_mat4[15] = 1;
}

void RasterizerCanvasGLES1::_update_transform_2d_to_mat2x4(const Transform2D &p_transform, float *p_mat2x4) {
	p_mat2x4[0] = p_transform.columns[0][0];
	p_mat2x4[1] = p_transform.columns[1][0];
	p_mat2x4[2] = 0;
	p_mat2x4[3] = p_transform.columns[2][0];

	p_mat2x4[4] = p_transform.columns[0][1];
	p_mat2x4[5] = p_transform.columns[1][1];
	p_mat2x4[6] = 0;
	p_mat2x4[7] = p_transform.columns[2][1];
}

void RasterizerCanvasGLES1::_update_transform_2d_to_mat2x3(const Transform2D &p_transform, float *p_mat2x3) {
	p_mat2x3[0] = p_transform.columns[0][0];
	p_mat2x3[1] = p_transform.columns[0][1];
	p_mat2x3[2] = p_transform.columns[1][0];
	p_mat2x3[3] = p_transform.columns[1][1];
	p_mat2x3[4] = p_transform.columns[2][0];
	p_mat2x3[5] = p_transform.columns[2][1];
}

void RasterizerCanvasGLES1::_update_transform_to_mat4(const Transform3D &p_transform, float *p_mat4) {
	p_mat4[0] = p_transform.basis.rows[0][0];
	p_mat4[1] = p_transform.basis.rows[1][0];
	p_mat4[2] = p_transform.basis.rows[2][0];
	p_mat4[3] = 0;
	p_mat4[4] = p_transform.basis.rows[0][1];
	p_mat4[5] = p_transform.basis.rows[1][1];
	p_mat4[6] = p_transform.basis.rows[2][1];
	p_mat4[7] = 0;
	p_mat4[8] = p_transform.basis.rows[0][2];
	p_mat4[9] = p_transform.basis.rows[1][2];
	p_mat4[10] = p_transform.basis.rows[2][2];
	p_mat4[11] = 0;
	p_mat4[12] = p_transform.origin.x;
	p_mat4[13] = p_transform.origin.y;
	p_mat4[14] = p_transform.origin.z;
	p_mat4[15] = 1;
}

RID RasterizerCanvasGLES1::light_create() {
	CanvasLight canvas_light;
	return canvas_light_owner.make_rid(canvas_light);
}

void RasterizerCanvasGLES1::light_set_texture(RID p_rid, RID p_texture) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	if (cl->texture == p_texture) {
		return;
	}

	cl->texture = p_texture;
}

void RasterizerCanvasGLES1::light_set_use_shadow(RID p_rid, bool p_enable) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);

	cl->shadow.enabled = p_enable;
}

void RasterizerCanvasGLES1::light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	ERR_FAIL_COND(!cl->shadow.enabled);

	cl->shadow.z_far = p_far;
	cl->shadow.shadow_volumes.clear();
	cl->shadow.light_to_world = p_light_xform.affine_inverse();

	LightOccluderInstance *instance = p_occluders;
	while (instance) {
		if (instance->light_mask & p_light_mask) {
			OccluderPolygon *oc = occluder_polygon_owner.get_or_null(instance->occluder);
			if (oc && oc->lines.size() > 0) {
				Transform2D xform = p_light_xform * instance->xform_cache;

				for (int i = 0; i < oc->lines.size(); i += 2) {
					Vector2 p1 = xform.xform(oc->lines[i]);
					Vector2 p2 = xform.xform(oc->lines[i + 1]);

					if (p1 == p2) {
						continue;
					}

					if (oc->cull_mode != RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
						Vector2 dir = (p2 - p1).normalized();
						Vector2 normal(-dir.y, dir.x);
						Vector2 center = (p1 + p2) * 0.5f;
						float d = normal.dot(center);

						bool front_facing = d < 0;
						if (oc->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_CLOCKWISE && !front_facing) {
							continue;
						}
						if (oc->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_COUNTER_CLOCKWISE && front_facing) {
							continue;
						}
					}

					Vector2 p1_ext = p1 + p1.normalized() * p_far;
					Vector2 p2_ext = p2 + p2.normalized() * p_far;

					cl->shadow.shadow_volumes.push_back(static_cast<float>(p1.x));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p1.y));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p2.x));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p2.y));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p2_ext.x));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p2_ext.y));

					cl->shadow.shadow_volumes.push_back(static_cast<float>(p1.x));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p1.y));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p2_ext.x));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p2_ext.y));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p1_ext.x));
					cl->shadow.shadow_volumes.push_back(static_cast<float>(p1_ext.y));
				}
			}
		}
		instance = instance->next;
	}
}

void RasterizerCanvasGLES1::light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	ERR_FAIL_COND(!cl->shadow.enabled);

	cl->shadow.directional_shadow_volumes.clear();
	Vector2 light_dir = p_light_xform.columns[1].normalized();

	LightOccluderInstance *instance = p_occluders;
	while (instance) {
		if (instance->light_mask & p_light_mask) {
			OccluderPolygon *oc = occluder_polygon_owner.get_or_null(instance->occluder);
			if (oc && oc->lines.size() > 0) {
				Transform2D xform = instance->xform_cache;

				for (int i = 0; i < oc->lines.size(); i += 2) {
					Vector2 p1 = xform.xform(oc->lines[i]);
					Vector2 p2 = xform.xform(oc->lines[i + 1]);

					if (p1 == p2) {
						continue;
					}

					if (oc->cull_mode != RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
						Vector2 dir = (p2 - p1).normalized();
						Vector2 normal(-dir.y, dir.x);
						float d = normal.dot(light_dir);

						bool front_facing = d < 0;
						if (oc->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_CLOCKWISE && !front_facing) {
							continue;
						}
						if (oc->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_COUNTER_CLOCKWISE && front_facing) {
							continue;
						}
					}

					Vector2 p1_ext = p1 + light_dir * p_cull_distance;
					Vector2 p2_ext = p2 + light_dir * p_cull_distance;

					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p1.x));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p1.y));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p2.x));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p2.y));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p2_ext.x));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p2_ext.y));

					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p1.x));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p1.y));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p2_ext.x));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p2_ext.y));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p1_ext.x));
					cl->shadow.directional_shadow_volumes.push_back(static_cast<float>(p1_ext.y));
				}
			}
		}
		instance = instance->next;
	}
}

void RasterizerCanvasGLES1::render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) {

}

RID RasterizerCanvasGLES1::occluder_polygon_create() {
	OccluderPolygon occluder;
	return occluder_polygon_owner.make_rid(occluder);
}

void RasterizerCanvasGLES1::occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) {
	OccluderPolygon *oc = occluder_polygon_owner.get_or_null(p_occluder);
	ERR_FAIL_NULL(oc);

	int point_count = p_points.size();
	if (point_count < 2) {
		oc->line_point_count = 0;
		oc->lines.clear();
		return;
	}

	int line_count = p_closed ? point_count : point_count - 1;
	oc->lines.clear();
	oc->lines.resize(line_count * 2);

	for (int i = 0; i < line_count; i++) {
		oc->lines.write[i * 2 + 0] = p_points[i];
		oc->lines.write[i * 2 + 1] = p_points[(i + 1) % point_count];
	}

	oc->line_point_count = line_count * 2;
	oc->vertex_array = 1; // Mark as valid
}

void RasterizerCanvasGLES1::occluder_polygon_set_cull_mode(RID p_occluder, RS::CanvasOccluderPolygonCullMode p_mode) {
	OccluderPolygon *oc = occluder_polygon_owner.get_or_null(p_occluder);
	ERR_FAIL_NULL(oc);
	oc->cull_mode = p_mode;
}

void RasterizerCanvasGLES1::set_shadow_texture_size(int p_size) {
	// Nop
}

bool RasterizerCanvasGLES1::free(RID p_rid) {
	if (canvas_light_owner.owns(p_rid)) {
		CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
		if (cl && cl->directional_tex_id != 0) {
			glDeleteTextures(1, &cl->directional_tex_id);
			cl->directional_tex_id = 0;
		}
		canvas_light_owner.free(p_rid);
		return true;
	} else if (occluder_polygon_owner.owns(p_rid)) {
		occluder_polygon_set_shape(p_rid, Vector<Vector2>(), false);
		occluder_polygon_owner.free(p_rid);
	}

	return true;
}

#define _EIDX(y, x) (y * 4 + x)
static constexpr uint16_t ninepatch_elems[3 * 2 * 9] = {
	// first row
	_EIDX(0, 0), _EIDX(0, 1), _EIDX(1, 1),
	_EIDX(1, 1), _EIDX(1, 0), _EIDX(0, 0),

	_EIDX(0, 1), _EIDX(0, 2), _EIDX(1, 2),
	_EIDX(1, 2), _EIDX(1, 1), _EIDX(0, 1),

	_EIDX(0, 2), _EIDX(0, 3), _EIDX(1, 3),
	_EIDX(1, 3), _EIDX(1, 2), _EIDX(0, 2),

	// second row
	_EIDX(1, 0), _EIDX(1, 1), _EIDX(2, 1),
	_EIDX(2, 1), _EIDX(2, 0), _EIDX(1, 0),

	// center field
	_EIDX(1, 2), _EIDX(1, 3), _EIDX(2, 3),
	_EIDX(2, 3), _EIDX(2, 2), _EIDX(1, 2),

	// third row
	_EIDX(2, 0), _EIDX(2, 1), _EIDX(3, 1),
	_EIDX(3, 1), _EIDX(3, 0), _EIDX(2, 0),

	_EIDX(2, 1), _EIDX(2, 2), _EIDX(3, 2),
	_EIDX(3, 2), _EIDX(3, 1), _EIDX(2, 1),

	_EIDX(2, 2), _EIDX(2, 3), _EIDX(3, 3),
	_EIDX(3, 3), _EIDX(3, 2), _EIDX(2, 2),

	// center field
	_EIDX(1, 1), _EIDX(1, 2), _EIDX(2, 2),
	_EIDX(2, 2), _EIDX(2, 1), _EIDX(1, 1)
};
#undef _EIDX

void RasterizerCanvasGLES1::initialize() {
	print_verbose("GLES1: Initializing Canvas Renderer");

	// Quad buffer
	{
		data.canvas_quad_vertices = 0;
		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &data.canvas_quad_vertices);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers quad");
		}

		if (data.canvas_quad_vertices != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);

			const constexpr float qv[8] = {
				0, 0,
				0, 1,
				1, 1,
				1, 0
			};

			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, data.canvas_quad_vertices, sizeof(float) * 8, qv, GL_STATIC_DRAW, "Canvas Quad");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data quad");
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT("GLES1: Failed to generate canvas_quad_vertices VBO. Using client memory fallback.");
#endif 
		}
	}

	// Polygon buffer
	{
		uint32_t poly_size = GLOBAL_DEF("rendering/limits/buffers/canvas_polygon_buffer_size_kb", 128);
		poly_size = MAX(poly_size, (uint32_t)128); // minimum 128k
		poly_size *= 1024;

		data.polygon_buffer = 0;
		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &data.polygon_buffer);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers poly");
		}

		if (data.polygon_buffer != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, data.polygon_buffer, poly_size, nullptr, GL_DYNAMIC_DRAW, "Canvas Polygon Buffer");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data poly");
			data.polygon_buffer_size = poly_size;
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT("GLES1: Failed to generate polygon_buffer VBO. Using client memory fallback.");
#endif
		}

		uint32_t index_size = GLOBAL_DEF("rendering/limits/buffers/canvas_polygon_index_buffer_size_kb", 128);
		index_size = MAX(index_size, (uint32_t)128);
		index_size *= 1024;

		data.polygon_index_buffer = 0;

		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &data.polygon_index_buffer);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers poly index");
		}

		if (data.polygon_index_buffer != 0) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer, index_size, nullptr, GL_DYNAMIC_DRAW, "Canvas Polygon Index Buffer");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data poly index");
			data.polygon_index_buffer_size = index_size;
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	}

	// Ninepatch buffers
	{
		data.ninepatch_vertices = 0;

		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &data.ninepatch_vertices);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers ninepatch");
		}

		if (data.ninepatch_vertices != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, data.ninepatch_vertices, sizeof(float) * (16 + 16) * 2, nullptr, GL_DYNAMIC_DRAW, "Canvas Ninepatch Vertices");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data ninepatch");
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		data.ninepatch_elements = 0;

		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &data.ninepatch_elements);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers ninepatch index");
		}

		if (data.ninepatch_elements != 0) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements, sizeof(ninepatch_elems), ninepatch_elems, GL_STATIC_DRAW, "Canvas Ninepatch Elements");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data ninepatch index");
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	}

	// Light vector map (for point light normals)
	{
		data.light_vector_tex = 0;
		glGenTextures(1, &data.light_vector_tex);
		glBindTexture(GL_TEXTURE_2D, data.light_vector_tex);

		constexpr int LV_SIZE = 128;
		Vector<uint8_t> lv_data;
		lv_data.resize(LV_SIZE * LV_SIZE * 4);
		uint8_t *lv_ptr = lv_data.ptrw();
		ERR_FAIL_NULL(lv_ptr);

		for (int y = 0; y < LV_SIZE; y++) {
			for (int x = 0; x < LV_SIZE; x++) {
				float px = 1.0f - (x / (float)(LV_SIZE - 1)) * 2.0f;
				float py = 1.0f - (y / (float)(LV_SIZE - 1)) * 2.0f;
				float dist_sq = px * px + py * py;
				float pz = dist_sq < 1.0f ? Math::sqrt(1.0f - dist_sq) : 0.0f;

				Vector3 v(px, py, pz);
				if (dist_sq > 1.0f) {
					v = Vector3(0, 0, 1);
				} else {
					v.normalize();
				}

				int idx = (y * LV_SIZE + x) * 4;
				lv_ptr[idx + 0] = (uint8_t)((v.x * 0.5f + 0.5f) * 255.0f);
				lv_ptr[idx + 1] = (uint8_t)((v.y * 0.5f + 0.5f) * 255.0f);
				lv_ptr[idx + 2] = (uint8_t)((v.z * 0.5f + 0.5f) * 255.0f);
				lv_ptr[idx + 3] = 255;
			}
		}

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LV_SIZE, LV_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, lv_data.ptr());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		GL_CHECK_ERROR("GLES1::Canvas::initialize: light vector map generation");
	}

	_set_texture_rect_mode(true);

	state.using_light = nullptr;
	state.using_skeleton = false;

	maximum_attributes = RS::ARRAY_MAX;
	shadow_render.shader.initialize();
	shadow_render.shader_version = shadow_render.shader.version_create();

	// Configure limits for this driver.
	limit_settings.light_multiplier = static_cast<float>(GLOBAL_GET("rendering/gl_classic/light_vibrancy_multiplier"));

	// Batcher Initialisation
	batch_initialize();

	if (bdata.vertex_buffer_size_bytes && bdata.index_buffer_size_units > 0) {
		bdata.gl_vertex_buffer = 0;

		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &bdata.gl_vertex_buffer);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers batcher");
		}

		if (bdata.gl_vertex_buffer != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer, bdata.vertex_buffer_size_bytes, nullptr, GL_DYNAMIC_DRAW, "Canvas Batcher Vertices");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data batcher");
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT("GLES1: Failed to generate Batcher VBOs. Using client memory fallback.");
#endif
		}

		bdata.gl_index_buffer = 0;

		if (!GLES1::Config::get_singleton()->is_android_emulator) {
			glGenBuffers(1, &bdata.gl_index_buffer);
			GL_CHECK_ERROR("GLES1::Canvas::initialize: glGenBuffers batcher index");
		}

		if (bdata.gl_index_buffer != 0) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);

			Vector<uint16_t> indices;
			indices.resize(bdata.index_buffer_size_units);
			uint16_t *idx_ptr = indices.ptrw();
			ERR_FAIL_NULL(idx_ptr);

			for (unsigned int q = 0; q < bdata.max_quads; q++) {
				int i_pos = q * 6;
				int q_pos = q * 4;

				ERR_FAIL_COND(static_cast<uint32_t>(i_pos + 5) >= bdata.index_buffer_size_units);

				idx_ptr[i_pos] = q_pos;
				idx_ptr[i_pos + 1] = q_pos + 1;
				idx_ptr[i_pos + 2] = q_pos + 2;
				idx_ptr[i_pos + 3] = q_pos;
				idx_ptr[i_pos + 4] = q_pos + 2;
				idx_ptr[i_pos + 5] = q_pos + 3;

#ifdef DEBUG_ENABLED
				CRASH_COND_MSG((q_pos + 3) > 65535, "GLES1 Canvas Batcher: Too many vertices for 16-bit indices!");
#endif
			}

			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer, bdata.index_buffer_size_bytes, indices.ptr(), GL_STATIC_DRAW, "Canvas Batcher Indices");
			GL_CHECK_ERROR("GLES1::Canvas::initialize: buffer_allocate_data batcher index");
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
	}
}

void RasterizerCanvasGLES1::update() {

}

void RasterizerCanvasGLES1::_bind_canvas_texture(RID p_texture, RS::CanvasItemTextureFilter p_base_filter, RS::CanvasItemTextureRepeat p_base_repeat) {
	if (p_texture.is_null()) {
		// Fallback to default white if passed null
		p_texture = GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
	}

	// Cache Check (Prevents redundant state changes)
	if (!state.state_dirty && state.current_tex == p_texture && state.current_filter == p_base_filter && state.current_repeat == p_base_repeat) {
		return;
	}

	// Clear the flag and reset RT tracking
	state.state_dirty = false;
	state.current_tex_is_rt = false;

	// Resolve CanvasTexture
	GLES1::CanvasTexture *ct = nullptr;
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	ERR_FAIL_NULL(texture_storage);

	GLES1::Texture *t = texture_storage->get_texture(p_texture);

	if (t) {
		// If it's a raw texture, it might have an embedded CanvasTexture
		if (!t->canvas_texture) {
			t->canvas_texture = memnew(GLES1::CanvasTexture);
			ERR_FAIL_NULL(t->canvas_texture);
			t->canvas_texture->diffuse = p_texture;
		}
		ct = t->canvas_texture;
	} else {
		// It's a genuine CanvasTexture RID
		ct = texture_storage->get_canvas_texture(p_texture);
	}

	if (!ct) {
		// Completely invalid texture, bind white safely
		RID white_tex = texture_storage->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
		_bind_canvas_texture(white_tex, p_base_filter, p_base_repeat);
		return;
	}

	// Resolve overrides from CanvasTexture
	RS::CanvasItemTextureFilter filter = ct->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? ct->texture_filter : p_base_filter;
	RS::CanvasItemTextureRepeat repeat = ct->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? ct->texture_repeat : p_base_repeat;

	// Diffuse
	GLES1::Texture *diffuse_tex = texture_storage->texture_bind_and_validate(ct->diffuse, GL_TEXTURE0, filter, repeat);

	if (diffuse_tex) {
		state.current_tex_is_rt = diffuse_tex->render_target != nullptr;

		// Calculate pixel size for the shader
		float w = diffuse_tex->width > 0 ? diffuse_tex->width : 1024.0f;
		float h = diffuse_tex->height > 0 ? diffuse_tex->height : 1024.0f;
		state.texpixel_size = Size2(1.0 / w, 1.0 / h);
		state.texture_size = Size2i(w, h);
	}

	GLint max_units = GLES1::Config::get_singleton()->max_texture_units;

	bool has_normal = (state.using_light && max_units >= 4 && ct->normal_map.is_valid());
	state.normal_used = has_normal;

	if (has_normal) {
		GLES1::Texture *normal_map = texture_storage->get_texture(ct->normal_map);
		state.current_tex_is_rt = state.current_tex_is_rt || (normal_map && normal_map->render_target);

		// TU0: Normal Map (Pass-through)
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		if (normal_map) {
			glBindTexture(GL_TEXTURE_2D, normal_map->tex_id);
		}
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);

		// TU1: Light Vector Map + DOT3
		glActiveTexture(GL_TEXTURE0 + 1);
		glEnable(GL_TEXTURE_2D);

		if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
			CanvasLight *cl = canvas_light_owner.get_or_null(state.using_light->light_internal);
			ERR_FAIL_NULL(cl);

			uint32_t current_gen = GLES1::Config::get_singleton()->context_generation;
			if (cl->directional_tex_id == 0 || cl->context_generation != current_gen) {
				if (cl->directional_tex_id != 0) {
					glDeleteTextures(1, &cl->directional_tex_id);
				}
				glGenTextures(1, &cl->directional_tex_id);
				cl->context_generation = current_gen;

				glBindTexture(GL_TEXTURE_2D, cl->directional_tex_id);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, cl->directional_pixel);
				GL_CHECK_ERROR("GLES1::Canvas::_bind_canvas_texture: create directional 1x1 texture");
			} else {
				glBindTexture(GL_TEXTURE_2D, cl->directional_tex_id);
			}
		} else {
			glBindTexture(GL_TEXTURE_2D, data.light_vector_tex);
		}

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);

		// Ensure previous light scaling doesn't leak into the normal calculation
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
		glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
		GL_CHECK_ERROR("GLES1::Canvas::_bind_canvas_texture: TU1 normal map combiners");

		// TU2: Diffuse Map + Modulate
		glActiveTexture(GL_TEXTURE0 + 2);
		glEnable(GL_TEXTURE_2D);
		if (diffuse_tex) {
			glBindTexture(GL_TEXTURE_2D, diffuse_tex->tex_id);
		}
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);

		// TU3: Enabled later for Light Attenuation
		glActiveTexture(GL_TEXTURE0 + 3);
		glEnable(GL_TEXTURE_2D);
	} else {
		// Standard No-Normal Setup
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		if (diffuse_tex) {
			glBindTexture(GL_TEXTURE_2D, diffuse_tex->tex_id);
		}
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		if (state.using_light) {
			glActiveTexture(GL_TEXTURE0 + 1);
			glEnable(GL_TEXTURE_2D);

			// Restore Light Base Setup
			RID light_tex_rid = state.using_light->texture;
			GLES1::CanvasTexture *light_ct = texture_storage->get_canvas_texture(light_tex_rid);
			if (light_ct) {
				light_tex_rid = light_ct->diffuse;
			}

			GLES1::Texture *light_tex = texture_storage->get_texture(light_tex_rid);
			if (light_tex) {
				glBindTexture(GL_TEXTURE_2D, light_tex->tex_id);
			} else {
				RID white_tex_rid = texture_storage->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
				GLES1::Texture *white_tex = texture_storage->get_texture(white_tex_rid);
				if (white_tex) {
					glBindTexture(GL_TEXTURE_2D, white_tex->tex_id);
				} else {
					glBindTexture(GL_TEXTURE_2D, 0);
				}
			}

			// Clamp to edge so unpadded point lights don't bleed across the canvas 
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

			if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
			} else {
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
			}
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

			// Prevent alpha squaring during text and directional passes
			if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
			} else {
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
			}
			GL_CHECK_ERROR("GLES1::Canvas::_bind_canvas_texture: TU1 alpha fallback combiners");

			float rgb_scale = 1.0f;
			if (state.using_light->energy > 2.0f) {
				rgb_scale = 4.0f;
			} else if (state.using_light->energy > 1.0f) {
				rgb_scale = 2.0f;
			}
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, rgb_scale);
			glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);

			if (max_units >= 3) {
				glActiveTexture(GL_TEXTURE0 + 2);
				glDisable(GL_TEXTURE_2D);
			}
			if (max_units >= 4) {
				glActiveTexture(GL_TEXTURE0 + 3);
				glDisable(GL_TEXTURE_2D);
			}
		}
	}
	GL_CHECK_ERROR("GLES1::Canvas::_bind_canvas_texture: apply normal combiners");

	// Specular Map
	if (max_units >= 5) {
		glActiveTexture(GL_TEXTURE0 + 4);
		GL_CHECK_ERROR("GLES1::Canvas::_bind_canvas_texture: glActiveTexture specular map");
		GLES1::Texture *specular_map = GLES1::TextureStorage::get_singleton()->get_texture(ct->specular);
		if (!specular_map) {
			GLES1::Texture *tex_spec = GLES1::TextureStorage::get_singleton()->get_texture(GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE));
			if (tex_spec) {
				glBindTexture(GL_TEXTURE_2D, tex_spec->tex_id);
			} else {
				glBindTexture(GL_TEXTURE_2D, 0);
			}
		} else {
			glBindTexture(GL_TEXTURE_2D, specular_map->tex_id);
			state.current_tex_is_rt = state.current_tex_is_rt || specular_map->render_target;
		}
		glDisable(GL_TEXTURE_2D);
	}

	// Reset Active Texture to 0 so we don't
	// accidentally draw geometry to the specular unit
	glActiveTexture(GL_TEXTURE0);
	GL_CHECK_ERROR("GLES1::Canvas::_bind_canvas_texture: reset glActiveTexture 0");

	// Update State Tracker
	state.current_tex = p_texture;
	state.current_filter = filter;
	state.current_repeat = repeat;
}

void RasterizerCanvasGLES1::_set_canvas_uniforms() {
	if (state.shader_version.is_null()) {
		return;
	}

	// These rely on the generated canvas.glsl.gen.h Uniforms enum and version_set_uniform
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::PROJECTION_MATRIX, state.uniforms.projection_matrix, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::MODELVIEW_MATRIX, state.uniforms.modelview_matrix, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::EXTRA_MATRIX, state.uniforms.extra_matrix, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::FINAL_MODULATE, state.uniforms.final_modulate, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::TIME, state.time, state.shader_version, state.mode_variant, state.specialization);

	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::FINAL_MODULATE, state.uniforms.final_modulate, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::TIME, state.time, state.shader_version, state.mode_variant, state.specialization);

	if (state.using_light) {
		RendererCanvasRender::Light *light = state.using_light;
		if (light) {
			Color light_color = light->color * light->energy;
			state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_COLOR, light_color, state.shader_version, state.mode_variant, state.specialization);
			state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_OUTSIDE_ALPHA, 0.0f, state.shader_version, state.mode_variant, state.specialization);

			// Inject texture matrix projection manually
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			Transform2D canvas_modelview = state.uniforms.modelview_matrix * state.uniforms.extra_matrix;

			Transform2D light_matrix;
			if (light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				light_matrix = Transform2D(); // Identity
			} else {
				light_matrix = light->light_shader_xform.affine_inverse();
			}

			if (max_units >= 2) {
				Transform2D final_light_matrix = light_matrix * canvas_modelview;
				GLfloat tex_matrix[16] = {};
				_update_transform_2d_to_mat4(final_light_matrix, tex_matrix);

				glActiveTexture(GL_TEXTURE0 + 1);
				glMatrixMode(GL_TEXTURE);

				glLoadMatrixf(tex_matrix);

				glMatrixMode(GL_MODELVIEW);

				if (max_units >= 4 && state.normal_used) {
					glActiveTexture(GL_TEXTURE0 + 3);
					glMatrixMode(GL_TEXTURE);
					glLoadMatrixf(tex_matrix); // Light attenuation must follow the world shape
					glMatrixMode(GL_MODELVIEW);
				}
				glActiveTexture(GL_TEXTURE0);
			}

			if (light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				Vector2 light_dir = light->xform_cache.columns[1].normalized();
				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_POS, -light_dir, state.shader_version, state.mode_variant, state.specialization);

				if (state.normal_used) {
					CanvasLight *cl = canvas_light_owner.get_or_null(light->light_internal);
					ERR_FAIL_NULL(cl);

					Transform2D item_inverse = canvas_modelview.affine_inverse();
					Vector2 local_light_dir = item_inverse.basis_xform(light_dir).normalized();

					// Ensure a tiny minimum baseline so flat sprites aren't completely invisible.
					float z_height = MAX((float)light->height, 0.1f);
					Vector3 l_dir = Vector3(-local_light_dir.x, -local_light_dir.y, z_height).normalized();

					uint8_t enc_x = (uint8_t)((l_dir.x * 0.5f + 0.5f) * 255.0f);
					uint8_t enc_y = (uint8_t)((l_dir.y * 0.5f + 0.5f) * 255.0f);
					uint8_t enc_z = (uint8_t)((l_dir.z * 0.5f + 0.5f) * 255.0f);

					if (cl->directional_pixel[0] != enc_x || cl->directional_pixel[1] != enc_y || cl->directional_pixel[2] != enc_z || cl->directional_tex_id == 0) {
						cl->last_light_dir = local_light_dir;

						cl->directional_pixel[0] = enc_x;
						cl->directional_pixel[1] = enc_y;
						cl->directional_pixel[2] = enc_z;
						cl->directional_pixel[3] = 255;

						if (cl->directional_tex_id != 0) {
							glActiveTexture(GL_TEXTURE0 + 1);
							glBindTexture(GL_TEXTURE_2D, cl->directional_tex_id);
							glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cl->directional_pixel);
							GL_CHECK_ERROR("GLES1::Canvas::_set_canvas_uniforms: update directional 1x1 texture");
							glActiveTexture(GL_TEXTURE0);
						}
					}
				}

				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_HEIGHT, light->height, state.shader_version, state.mode_variant, state.specialization);
			} else {
				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_MATRIX, light->light_shader_xform.affine_inverse(), state.shader_version, state.mode_variant, state.specialization);
				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_MATRIX_INVERSE, light->light_shader_xform, state.shader_version, state.mode_variant, state.specialization);

				Transform2D light_local = light->xform_cache.affine_inverse();
				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_LOCAL_MATRIX, light_local, state.shader_version, state.mode_variant, state.specialization);

				Vector2 light_pos = light->xform_cache.get_origin();
				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_POS, light_pos, state.shader_version, state.mode_variant, state.specialization);

				state.canvas_shader->version_set_uniform(CanvasShaderGLES1::LIGHT_HEIGHT, light->height, state.shader_version, state.mode_variant, state.specialization);
			}
		}
	}

	if (state.render_target != RID()) {
		GLES1::RenderTarget *rt = GLES1::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt) {
			Vector2 screen_pixel_size;
			screen_pixel_size.x = 1.0 / rt->size.width;
			screen_pixel_size.y = 1.0 / rt->size.height;
			state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SCREEN_PIXEL_SIZE, screen_pixel_size, state.shader_version, state.mode_variant, state.specialization);
		}
	}

	if (state.using_skeleton) {
		state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SKELETON_TRANSFORM, state.skeleton_transform, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SKELETON_TRANSFORM_INVERSE, state.skeleton_transform_inverse, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SKELETON_TEXTURE_SIZE, state.skeleton_texture_size, state.shader_version, state.mode_variant, state.specialization);
	}
}

void RasterizerCanvasGLES1::canvas_begin(RID p_to_render_target, bool p_to_backbuffer) {
	if (unlikely(is_context_lost())) {
		WARN_PRINT("VBO Context loss at canvas_begin. Rebuilding.");
		force_context_recovery();
	}
	batch_canvas_begin();

	state.render_target = p_to_render_target;
	state.specialization = 0;
	state.mode_variant = CanvasShaderGLES1::ShaderVariant::MODE_QUAD;
	state.shader_version = data.canvas_shader_default_version;

	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	ERR_FAIL_NULL(texture_storage);

	GLES1::RenderTarget *render_target = texture_storage->get_render_target(p_to_render_target);
	GLES1::Config *config = GLES1::Config::get_singleton();
	ERR_FAIL_NULL(config);

	if (render_target) {
		render_target->was_used = true;
	}

	// Determine a safe texture unit for the screen/backbuffer.
	// We prefer texture unit 3, but fallback to 1 (sacrificing normal maps) if limited to 2 units.
	GLenum screen_tex_unit = GL_TEXTURE0;
	if (config->max_texture_units >= 4) {
		screen_tex_unit = GL_TEXTURE3;
	} else if (config->max_texture_units >= 2) {
		screen_tex_unit = GL_TEXTURE1;
	}

	if (render_target && render_target->fbo != 0) {
		if (glIsFramebufferOES(render_target->fbo) == GL_FALSE) {
			print_verbose("GLES1: Dead FBO detected. Forcing recreation.");
			render_target->fbo = 0; 
			texture_storage->render_target_set_size(p_to_render_target, render_target->size.width, render_target->size.height, render_target->view_count);
		}
	}

	// Bind the correct Framebuffer
	if (p_to_backbuffer) {
		texture_storage->bind_framebuffer(render_target ? render_target->backbuffer_fbo : 0);
		GL_CHECK_ERROR("GLES1::Canvas::canvas_begin: bind backbuffer fbo");
		texture_storage->texture_bind_and_validate(
			default_canvas_texture,
			screen_tex_unit,
			RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST,
			RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED
		);
	} else {
		texture_storage->bind_framebuffer(render_target ? render_target->fbo : 0);
		GL_CHECK_ERROR("GLES1::Canvas::canvas_begin: bind fbo");
		glActiveTexture(screen_tex_unit);
		if (render_target) {
			glBindTexture(GL_TEXTURE_2D, render_target->backbuffer);
		}
	}

	// Reset the active texture back to 0 immediately after,
	// otherwise, the very next draw call may accidentally bind
	// its diffuse texture to the screen texture unit.
	glActiveTexture(GL_TEXTURE0);

	state.transparent_render = (render_target && render_target->is_transparent) || p_to_backbuffer;

	// Set blending and clear buffers if needed
	if (state.transparent_render) {
		if (config->support_blend_func_separate) {
			glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		} else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	} else {
		if (config->support_blend_func_separate) {
			glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		} else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_CHECK_ERROR("GLES1::Canvas::canvas_begin: glColorMask clear");

	if (render_target && render_target->clear_requested) {
		const Color &col = render_target->clear_color;
		glClearColor(col.r, col.g, col.b, render_target->is_transparent ? col.a : 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		GL_CHECK_ERROR("GLES1::Canvas::canvas_begin: clear requested");
		render_target->clear_requested = false;
	}

	// Set Viewport dimensions
	Size2 render_target_size;
	if (render_target) {
		render_target_size = texture_storage->render_target_get_size(p_to_render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}
	GLsizei vp_w = MAX(0, (int)render_target_size.x);
	GLsizei vp_h = MAX(0, (int)render_target_size.y);

	glViewport(0, 0, vp_w, vp_h);
	GL_CHECK_ERROR("GLES1::Canvas::canvas_begin: glViewport");

	reset_canvas();

	// Bind the default white texture so untextured items draw correctly
	RID white_tex_rid = texture_storage->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
	texture_storage->texture_bind_and_validate(
		white_tex_rid,
		GL_TEXTURE0,
		RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST,
		RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED
	);

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glDisableClientState(GL_COLOR_ARRAY);

	// Calculate Projection Matrix
	// Godot 4 seems to expect 3D transforms even in 2D.
	Transform3D canvas_transform;

	if (state.render_target != RID() && render_target) {
		render_target_size = render_target->size;
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}

	// In Godot 4, the default Y-scale for OpenGL is positive 2.0 when rendering to FBOs.
	// We must flip it to -2.0 if we are rendering directly to the screen (no FBO support),
	// or if there is an overridden color texture (like XR).
	float y_scale = 2.0f;

	if (p_to_backbuffer || !render_target || render_target->direct_to_screen) {
		y_scale = -2.0f;
	} else if (state.render_target != RID()) {
		RID override_color = texture_storage->render_target_get_override_color(state.render_target);
		
		if (override_color.is_valid()) {
			y_scale = -2.0f;
		}
	}

	float rt_w = MAX(1.0f, render_target_size.width);
	float rt_h = MAX(1.0f, render_target_size.height);

	Transform3D translate(Basis(), Vector3(-(rt_w / 2.0f), -(rt_h / 2.0f), 0.0f));
	Transform3D scale(Basis().scaled(Vector3(2.0f / rt_w, y_scale / rt_h, 1.0f)), Vector3());
	canvas_transform = scale * translate;

	state.uniforms.projection_matrix = canvas_transform;
	state.uniforms.final_modulate = Color(1, 1, 1, 1);
	state.uniforms.modelview_matrix = Transform2D();
	state.uniforms.extra_matrix = Transform2D();

	// Bind the shader before setting uniforms
	state.canvas_shader->version_bind_shader(data.canvas_shader_default_version, state.mode_variant, state.specialization);
	_set_canvas_uniforms();
}

void RasterizerCanvasGLES1::canvas_end() {
	batch_canvas_end();

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);

	// Sterilize secondary TU leak
	GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
	for (int i = 1; i < max_units; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
		glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
		glDisable(GL_TEXTURE_2D);
		glClientActiveTexture(GL_TEXTURE0 + i);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	glActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
	glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
	glClientActiveTexture(GL_TEXTURE0);

	glDisable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	GL_CHECK_ERROR("GLES1::Canvas::canvas_end: disable states");

	Size2 render_target_size;
	if (state.render_target != RID()) {
		render_target_size = GLES1::TextureStorage::get_singleton()->render_target_get_size(state.render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}
	GLsizei vp_w = MAX(0, (int)render_target_size.x);
	GLsizei vp_h = MAX(0, (int)render_target_size.y);

	glViewport(0, 0, vp_w, vp_h);
	glScissor(0, 0, vp_w, vp_h);
	glDisable(GL_SCISSOR_TEST);
	GL_CHECK_ERROR("GLES1::Canvas::canvas_end: reset viewport/scissor");

	state.using_skeleton = false;
	state.using_ninepatch = false;
	state.using_light = nullptr;
}

void RasterizerCanvasGLES1::reset_canvas() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LESS);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glDisable(GL_LIGHTING);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_TEXTURE_2D);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_CHECK_ERROR("GLES1::Canvas::reset_canvas: states");

	// Eradicate client state leakage
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);

	// Force sterilise secondary texture units
	// and their matrices
	GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
	for (int i = 1; i < max_units; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glDisable(GL_TEXTURE_2D);

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
		glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);

		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);

		glClientActiveTexture(GL_TEXTURE0 + i);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	glActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
	glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
	glClientActiveTexture(GL_TEXTURE0);
	GL_CHECK_ERROR("GLES1::Canvas::reset_canvas: disable client states and matrices");

	if (state.transparent_render) {
		if (GLES1::Config::get_singleton()->support_blend_func_separate) {
			glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		} else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	} else {
		if (GLES1::Config::get_singleton()->support_blend_func_separate) {
			glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		} else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}
	GL_CHECK_ERROR("GLES1::Canvas::reset_canvas: blend func");

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES1::Canvas::reset_canvas: unbind buffers");

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	GL_CHECK_ERROR("GLES1::Canvas::reset_canvas: reset color");

	state.state_dirty = true;
}

void RasterizerCanvasGLES1::set_debug_redraw(bool p_enabled, double p_time, const Color &p_color) {
	if (p_enabled) {
		WARN_PRINT_ONCE("Debug CanvasItem Redraw is not available yet when using the GL Classic backend.");
	}
}

uint32_t RasterizerCanvasGLES1::get_pipeline_compilations(RS::PipelineSource p_source) {
	return 0;
}

void RasterizerCanvasGLES1::canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_light_list, const Transform2D &p_canvas_transform, RS::CanvasItemTextureFilter p_default_filter, RS::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingMethod::RenderInfo *r_render_info) {
	// Cache the default texture filtering and repeat settings for this pass
	state.default_filter = p_default_filter;
	state.default_repeat = p_default_repeat;

	// Bind the FBO, clears it, and set Projection matrix.
	canvas_begin(p_to_render_target, false);

	// ==========================================
	// The base rendering pass
	// ==========================================
	canvas_render_items_begin(p_modulate, nullptr, p_canvas_transform);

	Item *current_item = p_item_list;

	// Do Z ordering sequentially based on
	// canvas/clip boundaries.
	int current_z = 0;

	while (current_item) {
		Item *next_item = current_item->next;

		// Break the list temporarily to feed it cleanly into the batcher
		current_item->next = nullptr;

		canvas_render_items_internal(current_item, current_z, p_modulate, nullptr, p_canvas_transform);

		// Restore the chain for any subsequent passes
		current_item->next = next_item;
		current_item = next_item;
		current_z++;
	}

	canvas_render_items_end();

	// ==========================================
	// The directional light pass
	// ==========================================
	Light *current_directional_light = p_directional_light_list;
	while (current_directional_light) {
		if (current_directional_light->mode != RS::CANVAS_LIGHT_MODE_DIRECTIONAL || !current_directional_light->enabled) {
			current_directional_light = current_directional_light->directional_next_ptr;
			continue;
		}

		// Save the chain
		Light *next_light_ptr = current_directional_light->directional_next_ptr;
		current_directional_light->directional_next_ptr = nullptr;

		// Get the light colour and modulate
		float multiplier = limit_settings.light_multiplier;
		float energy = current_directional_light->energy;
		float rgb_scale = 1.0f;
		if (energy > 2.0f) {
			rgb_scale = 4.0f;
		} else if (energy > 1.0f) {
			rgb_scale = 2.0f;
		}

		Color light_modulate = p_modulate;
		light_modulate.r *= ((current_directional_light->color.r * energy) / rgb_scale) * multiplier;
		light_modulate.g *= ((current_directional_light->color.g * energy) / rgb_scale) * multiplier;
		light_modulate.b *= ((current_directional_light->color.b * energy) / rgb_scale) * multiplier;

		canvas_render_items_begin(light_modulate, current_directional_light, p_canvas_transform);

		current_item = p_item_list;
		current_z = 0;

		while (current_item) {
			if (current_item->light_mask & current_directional_light->item_mask) {
				Item *next_item = current_item->next;
				current_item->next = nullptr;

				canvas_render_items_internal(current_item, current_z, light_modulate, current_directional_light, p_canvas_transform);

				current_item->next = next_item;
			}
			current_item = current_item->next;
			current_z++;
		}

		canvas_render_items_end();

		// Restore the chain
		current_directional_light->directional_next_ptr = next_light_ptr;
		current_directional_light = next_light_ptr;
	}

	// ==========================================
	// The point light pass
	// ==========================================
	Light *current_light = p_light_list;
	while (current_light) {
		if (current_light->mode != RS::CANVAS_LIGHT_MODE_POINT || !current_light->enabled) {
			current_light = current_light->next_ptr;
			continue;
		}

		// Save the chain
		Light *next_light_ptr = current_light->next_ptr;
		current_light->next_ptr = nullptr;

		// Get the light colour and modulate
		float multiplier = limit_settings.light_multiplier;
		float energy = current_light->energy;
		float rgb_scale = 1.0f;
		if (energy > 2.0f) {
			rgb_scale = 4.0f;
		} else if (energy > 1.0f) {
			rgb_scale = 2.0f;
		}

		Color light_modulate = p_modulate;
		light_modulate.r *= ((current_light->color.r * energy) / rgb_scale) * multiplier;
		light_modulate.g *= ((current_light->color.g * energy) / rgb_scale) * multiplier;
		light_modulate.b *= ((current_light->color.b * energy) / rgb_scale) * multiplier;

		canvas_render_items_begin(light_modulate, current_light, p_canvas_transform);

		current_item = p_item_list;
		current_z = 0;

		while (current_item) {
			// Intersection and Mask Test
			if (current_item->light_mask & current_light->item_mask) {
				Rect2 light_rect = current_light->rect_cache;
				Rect2 item_rect = current_item->global_rect_cache;

				if (light_rect.intersects(item_rect)) {
					Item *next_item = current_item->next;
					current_item->next = nullptr;

					canvas_render_items_internal(current_item, current_z, light_modulate, current_light, p_canvas_transform);

					current_item->next = next_item;
				}
			}

			current_item = current_item->next;
			current_z++;
		}

		canvas_render_items_end();

		// Restore the chain
		current_light->next_ptr = next_light_ptr;
		current_light = next_light_ptr;
	}

	canvas_end();
}

void RasterizerCanvasGLES1::canvas_render_items_begin(const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
	batch_canvas_render_items_begin(p_modulate, p_light, p_base_transform);
}

void RasterizerCanvasGLES1::canvas_render_items_implementation(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
	RenderItemState ris;
	ris.item_group_z = p_z;
	ris.item_group_modulate = p_modulate;
	ris.item_group_light = p_light;
	ris.item_group_base_transform = p_base_transform;
	ris.current_clip = nullptr; // Explicitly ensure clip is null at the start

	// Ensure skeleton usage is off by default for the pass
	state.using_skeleton = false;

	// Reset texture state
	state.current_tex = RID();
	state.current_tex_is_rt = false;

	// Force baseline 2D state for the pass
	// Call reset_canvas() instead of hardcoding the blend func,
	// because reset_canvas respects transparent RenderTargets (sub-windows).
	reset_canvas();

	state.using_light = p_light;
	state.using_shadow = false;

	GLES1::Config *config = GLES1::Config::get_singleton();
	ERR_FAIL_NULL(config);

	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	ERR_FAIL_NULL(texture_storage);

	GLint max_units = config->max_texture_units;

	if (p_light) {
		bool support_subtract = config->support_blend_subtract;
		bool support_separate = config->support_blend_func_separate;

		if (support_subtract) {
			glBlendEquationOES(GL_FUNC_ADD_OES); // Default
		}

		switch (p_light->blend_mode) {
			case RS::CANVAS_LIGHT_BLEND_MODE_ADD:
				if (support_separate) {
					glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
				} else {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				}
				break;
			case RS::CANVAS_LIGHT_BLEND_MODE_SUB:
				if (support_subtract) {
					glBlendEquationOES(GL_FUNC_REVERSE_SUBTRACT_OES);
				}
				if (support_separate) {
					glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
				} else {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				}
				break;
			case RS::CANVAS_LIGHT_BLEND_MODE_MIX:
				if (support_separate) {
					glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
				} else {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				}
				break;
		}
		GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: light blend setup");

		// Bind light texture
		if (max_units >= 2) {
			glActiveTexture(GL_TEXTURE0 + 1);

			RID light_tex_rid = p_light->texture;
			GLES1::CanvasTexture *light_ct = texture_storage->get_canvas_texture(light_tex_rid);
			if (light_ct) {
				light_tex_rid = light_ct->diffuse;
			}

			GLES1::Texture *light_tex = texture_storage->get_texture(light_tex_rid);

			if (light_tex) {
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, light_tex->tex_id);
			} else {
				RID white_tex_rid = texture_storage->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
				GLES1::Texture *white_tex = texture_storage->get_texture(white_tex_rid);
				if (white_tex) {
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, white_tex->tex_id);
				} else {
					glDisable(GL_TEXTURE_2D);
				}
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

			// Multiply the underlying color by the light texture's RGB
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
			glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

			// Modulate the alpha to enforce the light's shape/gradient for the hardware blender
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
			glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

			// Artificially boost the energy directly on the RGB channel
			float rgb_scale = 1.0f;
			if (p_light->energy > 2.0f) {
				rgb_scale = 4.0f;
			} else if (p_light->energy > 1.0f) {
				rgb_scale = 2.0f;
			}

			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, rgb_scale);
			glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);

			if (max_units >= 4) {
				glActiveTexture(GL_TEXTURE0 + 3);
				if (light_tex) {
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, light_tex->tex_id);
				} else {
					RID white_tex_rid = texture_storage->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
					GLES1::Texture *white_tex = texture_storage->get_texture(white_tex_rid);
					if (white_tex) {
						glEnable(GL_TEXTURE_2D);
						glBindTexture(GL_TEXTURE_2D, white_tex->tex_id);
					} else {
						glDisable(GL_TEXTURE_2D);
					}
				}

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

				if (p_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					// Directional Lights lack a spot texture, so we replace it with
					// the primary color to apply the light's energy and color.
					glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
				} else {
					glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
				}
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

				if (p_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_PRIMARY_COLOR);
				} else {
					glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
				}
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

				glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, rgb_scale);
				glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
			}

			glActiveTexture(GL_TEXTURE0);
			GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: light texture bind");
		}

		state.using_shadow = p_light->use_shadow;
		if (max_units >= 3) {
			glActiveTexture(GL_TEXTURE0 + 2);
			glDisable(GL_TEXTURE_2D);
		}
		glActiveTexture(GL_TEXTURE0);

		if (state.using_shadow) {
			CanvasLight *cl = canvas_light_owner.get_or_null(p_light->light_internal);
			if (cl && (cl->shadow.shadow_volumes.size() > 0 || cl->shadow.directional_shadow_volumes.size() > 0)) {
				glDisableClientState(GL_COLOR_ARRAY);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				glDisableClientState(GL_NORMAL_ARRAY);
				glDisable(GL_TEXTURE_2D);

				// Prevent the active light texture combiners
				// from bleeding into the shadow extrusion pass
				if (max_units >= 2) {
					glActiveTexture(GL_TEXTURE0 + 1);
					glDisable(GL_TEXTURE_2D);
				}
				if (max_units >= 3) {
					glActiveTexture(GL_TEXTURE0 + 2);
					glDisable(GL_TEXTURE_2D);
				}
				if (max_units >= 4) {
					glActiveTexture(GL_TEXTURE0 + 3);
					glDisable(GL_TEXTURE_2D);
				}
				glActiveTexture(GL_TEXTURE0);

				glEnable(GL_DEPTH_TEST);
				glDepthMask(GL_TRUE);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				RasterizerGLES1::clear_depth(1.0f);
				glClear(GL_DEPTH_BUFFER_BIT);

				// Only write into the depth buffer, squash Z closer to near plane
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthFunc(GL_ALWAYS);
				glDepthRangef(0.0f, 0.1f);

				// Safeguard for devices that ignore glColorMask during depth passes
				glBlendFunc(GL_ZERO, GL_ONE);

				glMatrixMode(GL_MODELVIEW);
				glPushMatrix();
				glEnableClientState(GL_VERTEX_ARRAY);
				glBindBuffer(GL_ARRAY_BUFFER, 0); // Ensure client pointers
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

				if (config->is_android_emulator) {
					// Emulator bug: glColorMask is ignored on FBOs.
					// Force the geometry to be fully transparent.
					glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
				}

				// Clear IBOs
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: shadow state setup");

				if (p_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					GLfloat mat[16] = {};
					_update_transform_2d_to_mat4(Transform2D(), mat);
					glLoadMatrixf(mat);

					if (cl->shadow.directional_shadow_volumes.size() > 0) {
						glVertexPointer(2, GL_FLOAT, 0, cl->shadow.directional_shadow_volumes.ptr());
						glDrawArrays(GL_TRIANGLES, 0, cl->shadow.directional_shadow_volumes.size() / 2);
						GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: draw directional shadow");
					}
				} else {
					Transform2D vol_xform = cl->shadow.light_to_world;
					GLfloat mat[16] = {};
					_update_transform_2d_to_mat4(vol_xform, mat);
					glLoadMatrixf(mat);

					if (cl->shadow.shadow_volumes.size() > 0) {
						glVertexPointer(2, GL_FLOAT, 0, cl->shadow.shadow_volumes.ptr());
						glDrawArrays(GL_TRIANGLES, 0, cl->shadow.shadow_volumes.size() / 2);
						GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: draw point shadow");
					}
				}

				glPopMatrix();
				glDisableClientState(GL_VERTEX_ARRAY);

				// Setup for the light item geometry pass
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

				// Restore the correct blending state wiped out by the shadow guard
				if (support_separate) {
					if (p_light->blend_mode == RS::CANVAS_LIGHT_BLEND_MODE_MIX) {
						glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					} else {
						glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
					}
				} else {
					if (p_light->blend_mode == RS::CANVAS_LIGHT_BLEND_MODE_MIX) {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE);
					}
				}
				GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: restore light blend");

				// Base geometry does not write depth
				glDepthMask(GL_FALSE);

				// Geometry depth (0.75) must be less than buffer depth (0.05/1.0)
				glDepthFunc(GL_LESS);

				// Squash geometry Z farther back
				glDepthRangef(0.5f, 1.0f);
			} else {
				glDisable(GL_DEPTH_TEST);
			}
		} else {
			glDisable(GL_DEPTH_TEST);
		}
	} else {
		// Disable upper texture units during the base geometry pass
		if (max_units >= 2) {
			glActiveTexture(GL_TEXTURE0 + 1);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
			glDisable(GL_TEXTURE_2D);
		}
		if (max_units >= 3) {
			glActiveTexture(GL_TEXTURE0 + 2);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
			glDisable(GL_TEXTURE_2D);
		}
		if (max_units >= 4) {
			glActiveTexture(GL_TEXTURE0 + 3);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
			glDisable(GL_TEXTURE_2D);
		}
		GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: sterilize upper texture units");
	}

	// Bind default white texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	RID white_tex_rid = texture_storage->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
	texture_storage->texture_bind_and_validate(
		white_tex_rid,
		GL_TEXTURE0,
		RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST,
		RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED
	);
	GL_CHECK_ERROR("GLES1::Canvas::canvas_render_items_implementation: bind GL_TEXTURE0/GL_TEXTURE_2D");

	bool reclip = false;
	bool time_used = false;

	if (bdata.settings_use_batching) {
		// Render using the batched result
		int num_joined_items = bdata.items_joined.size();
		for (int j = 0; j < num_joined_items; j++) {
			const BItemJoined &joined_item = bdata.items_joined[j];
			Item *first_item = bdata.item_refs[joined_item.first_item_ref].item;

			if (!first_item) {
				continue;
			}

			bool light_scissor_enabled = false;
			if (p_light && bdata.settings_scissor_lights) {
				// Use the cached transform and rect directly
				light_scissor_enabled = _light_scissor_begin(
					joined_item.bounding_rect,
					p_light->xform_cache,
					p_light->rect_cache,
					0
				);
			}

			// Make sure we set current clip for scissor testing.
			if (!light_scissor_enabled && (ris.current_clip != first_item->final_clip_owner || reclip)) {
				ris.current_clip = first_item->final_clip_owner;
				if (ris.current_clip) {
					int x = ris.current_clip->final_clip_rect.position.x;
					int y = ris.current_clip->final_clip_rect.position.y;
					int w = MAX(0, (int)ris.current_clip->final_clip_rect.size.x);
					int h = MAX(0, (int)ris.current_clip->final_clip_rect.size.y);
					gl_enable_scissor(x, y, w, h);
					GL_CHECK_ERROR("GLES1::Canvas::render_items: glScissor setup");
				} else {
					gl_disable_scissor();
					GL_CHECK_ERROR("GLES1::Canvas::render_items: glScissor disable");
				}
				reclip = false;
			}

			// Extract the CanvasMaterialData
			GLES1::CanvasMaterialData *mat_data = nullptr;
			if (first_item->material.is_valid()) {
				GLES1::Material *material = GLES1::MaterialStorage::get_singleton()->get_material(first_item->material);
				if (material && material->data) {
					mat_data = static_cast<GLES1::CanvasMaterialData *>(material->data);

					if (mat_data->shader_data && mat_data->shader_data->uses_time) {
						time_used = true;
					}
				}
			}

			// Pass the joined_item and the extracted material into the render step
			render_joined_item_commands(joined_item, ris.current_clip, reclip, mat_data, state.using_light != nullptr);

			// Clean up light scissor if we hijacked it for this batch
			if (light_scissor_enabled) {
				reclip = true;
				gl_disable_scissor();
			}
		}
	} else {
		// Legacy / Immediate render fallback
		Item *ci = p_item_list;
		while (ci) {
			bool light_scissor_enabled = false;
			if (p_light && bdata.settings_scissor_lights) {
				light_scissor_enabled = _light_scissor_begin(
					ci->global_rect_cache,
					p_light->xform_cache,
					p_light->rect_cache,
					0
				);
			}

			if (!light_scissor_enabled && (ris.current_clip != ci->final_clip_owner || reclip)) {
				ris.current_clip = ci->final_clip_owner;
				if (ris.current_clip) {
					int x = ris.current_clip->final_clip_rect.position.x;
					int y = ris.current_clip->final_clip_rect.position.y;
					int w = MAX(0, (int)ris.current_clip->final_clip_rect.size.x);
					int h = MAX(0, (int)ris.current_clip->final_clip_rect.size.y);
					gl_enable_scissor(x, y, w, h);
					GL_CHECK_ERROR("GLES1::Canvas::render_items: glScissor setup");
				} else {
					gl_disable_scissor();
					GL_CHECK_ERROR("GLES1::Canvas::render_items: glScissor disable");
				}
				reclip = false;
			}

			// Extract the CanvasMaterialData for legacy rendering too.
			GLES1::CanvasMaterialData *mat_data = nullptr;
			if (ci->material.is_valid()) {
				GLES1::Material *material = GLES1::MaterialStorage::get_singleton()->get_material(ci->material);
				if (material && material->data) {
					mat_data = static_cast<GLES1::CanvasMaterialData *>(material->data);

					if (mat_data->shader_data && mat_data->shader_data->uses_time) {
						time_used = true;
					}
				}
			}

			_legacy_canvas_item_render_commands(ci, ris.current_clip, reclip, mat_data);

			if (light_scissor_enabled) {
				reclip = true;
				gl_disable_scissor();
			}

			ci = ci->next;
		}
	}

	// Clean up scissor test if it was left enabled by a clip
	if (ris.current_clip && !reclip) {
		gl_disable_scissor();
	}

	if (state.using_shadow) {
		glDisable(GL_DEPTH_TEST);
		glDepthRangef(0.0f, 1.0f);
	}

	if (time_used) {
		RenderingServerDefault::redraw_request();
	}
}

void RasterizerCanvasGLES1::canvas_render_items_internal(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
	batch_canvas_render_items(p_item_list, p_z, p_modulate, p_light, p_base_transform);
}

void RasterizerCanvasGLES1::canvas_render_items_end() {
	batch_canvas_render_items_end();
}

void RasterizerCanvasGLES1::gl_enable_scissor(int p_x, int p_y, int p_width, int p_height) const {
	glEnable(GL_SCISSOR_TEST);

	if (GLES1::Config::get_singleton()->is_gl_less_than_15) {
		// Because we don't have the luxuries of VBOs and FBOs
		// on 1.4 or lower, the axies of everything is flipped.
		float matrix_y_scale = state.uniforms.projection_matrix.basis[1][1];

		if (matrix_y_scale < 0.0f) {
			// Negative Y scale means top-down rendering (Screen/XR/FBO Fallback).
			// We derive the viewport height from the projection matrix Y scale
			// (matrix_y_scale = -2.0 / window_h  =>  window_h = -2.0 / matrix_y_scale)
			int window_h = Math::round(-2.0f / matrix_y_scale);
			int flipped_y = window_h - (p_y + p_height);
			glScissor(p_x, flipped_y, p_width, p_height);
		}
	} else {
		glScissor(p_x, p_y, p_width, p_height);
	}

	GL_CHECK_ERROR("GLES1::Canvas::gl_enable_scissor: glScissor");
}

void RasterizerCanvasGLES1::gl_disable_scissor() const {
	glDisable(GL_SCISSOR_TEST);
}

void RasterizerCanvasGLES1::_bind_quad_buffer() const {
	if (likely(data.canvas_quad_vertices != 0)) {
		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, nullptr);
		glTexCoordPointer(2, GL_FLOAT, 0, nullptr);

		if (state.using_light) {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			if (state.normal_used && max_units >= 4) {
				glClientActiveTexture(GL_TEXTURE0 + 1);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, nullptr);

				glClientActiveTexture(GL_TEXTURE0 + 2);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, nullptr);

				glClientActiveTexture(GL_TEXTURE0 + 3);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
			} else {
				if (max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				if (max_units >= 4) {
					glClientActiveTexture(GL_TEXTURE0 + 3);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}

				if (max_units >= 2) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
				}
				if (state.using_shadow && max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
				}
			}
			glClientActiveTexture(GL_TEXTURE0);
		} else {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			for (int i = 1; i < max_units; i++) {
				glClientActiveTexture(GL_TEXTURE0 + i);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE0);
		}
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

		static constexpr float qv[8] = {
			0, 0,
			0, 1,
			1, 1,
			1, 0
		};

		glVertexPointer(2, GL_FLOAT, 0, qv);
		glTexCoordPointer(2, GL_FLOAT, 0, qv);

		if (state.using_light) {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			if (state.normal_used && max_units >= 4) {
				glClientActiveTexture(GL_TEXTURE0 + 1);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, qv);

				glClientActiveTexture(GL_TEXTURE0 + 2);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, qv);

				glClientActiveTexture(GL_TEXTURE0 + 3);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, 0, qv);
			} else {
				if (max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				if (max_units >= 4) {
					glClientActiveTexture(GL_TEXTURE0 + 3);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}

				if (max_units >= 2) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, qv);
				}
				if (state.using_shadow && max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, qv);
				}
			}
			glClientActiveTexture(GL_TEXTURE0);
		} else {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			for (int i = 1; i < max_units; i++) {
				glClientActiveTexture(GL_TEXTURE0 + i);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE0);
		}
	}
}

_FORCE_INLINE_ bool RasterizerCanvasGLES1::_buffer_orphan_and_upload(unsigned int p_buffer_size_bytes, unsigned int p_offset_bytes, unsigned int p_data_size_bytes, const void *p_data, GLenum p_target, GLenum p_usage, bool p_optional_orphan) const {
	ERR_FAIL_COND_V((p_offset_bytes + p_data_size_bytes) > p_buffer_size_bytes, false);

	if (!p_optional_orphan) {
		if (GLES1::Config::get_singleton()->is_android_emulator && p_offset_bytes == 0 && p_buffer_size_bytes == p_data_size_bytes) {
			// Workaround: Buggy emulators crash or race on standard orphaning.
			// Passing the exact size and data directly to glBufferData forces a safe internal reallocation.
			glBufferData(p_target, p_buffer_size_bytes, p_data, p_usage);

			if (unlikely(glGetError() == GL_OUT_OF_MEMORY)) {
				return false; // Fast fail
			}
			return true;
		}

		glBufferData(p_target, p_buffer_size_bytes, nullptr, p_usage);
#ifdef RASTERIZER_EXTRA_CHECKS
		// fill with garbage off the end of the array
		if (p_buffer_size_bytes) {
			unsigned int start = p_offset_bytes + p_data_size_bytes;
			unsigned int end = start + 1024;
			if (end < p_buffer_size_bytes) {
				uint8_t *garbage = SAFE_ALLOCA_ARRAY(uint8_t, 1024);
				for (int n = 0; n < 1024; n++) {
					garbage[n] = Math::random(0, 255);
				}
				glBufferSubData(p_target, start, 1024, garbage);
			}
		}
#endif
	}

	glBufferSubData(p_target, p_offset_bytes, p_data_size_bytes, p_data);
	if (unlikely(glGetError() == GL_OUT_OF_MEMORY)) {
		return false; // Fast fail
	}
	return true;
}

void RasterizerCanvasGLES1::_batch_upload_buffers() {
	if (bdata.vertices.size() == 0 || bdata.gl_vertex_buffer == 0) {
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
	GL_CHECK_ERROR("GLES1::Canvas::batch_upload: glBindBuffer");

	// Determine payload size and data pointer.
	uint32_t buffer_bytes = 0;
	const void *data_ptr = nullptr;

	if (bdata.fvf == BatcherEnums::FVF_UNBATCHED || bdata.fvf == BatcherEnums::FVF_REGULAR) {
		buffer_bytes = bdata.vertices.size() * sizeof(BatchVertex);
		data_ptr = bdata.vertices.get_data();
	} else {
		// We are using an upgraded FVF format, upload the translated unit_vertices
		int sizeof_vert = 0;
		switch (bdata.fvf) {
			case BatcherEnums::FVF_COLOR:
				sizeof_vert = sizeof(BatchVertexColored);
				break;
			case BatcherEnums::FVF_LIGHT_ANGLE:
				sizeof_vert = sizeof(BatchVertexLightAngled);
				break;
			case BatcherEnums::FVF_MODULATED:
				sizeof_vert = sizeof(BatchVertexModulated);
				break;
			case BatcherEnums::FVF_LARGE:
				sizeof_vert = sizeof(BatchVertexLarge);
				break;
			default:
				break;
		}
		buffer_bytes = bdata.vertices.size() * sizeof_vert;
		ERR_FAIL_COND_MSG(buffer_bytes > bdata.vertex_buffer_size_bytes, "GLES1: Batch vertex buffer payload exceeds VRAM allocation! Aborting upload.");
		data_ptr = bdata.unit_vertices.get_data();
	}

	uint32_t alloc_size = bdata.vertex_buffer_size_bytes;
	if (GLES1::Config::get_singleton()->is_android_emulator) {
		alloc_size = buffer_bytes; // Emulator workaround: shrink allocation
	}

	// We ignore the result here and always unbind to clean
	// the state.
	_buffer_orphan_and_upload(alloc_size, 0, buffer_bytes, data_ptr, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES1::_batch_render_generic(const Batch &p_batch, GLES1::CanvasMaterialData *p_material) {
	ERR_FAIL_COND(p_batch.num_commands <= 0);

	const bool use_light_angles = bdata.use_light_angles;
	const bool use_modulate = bdata.use_modulate;
	const bool use_large_verts = bdata.use_large_verts;
	const bool colored_verts = (
		bdata.use_colored_vertices ||
		use_light_angles ||
		use_modulate ||
		use_large_verts
	);

	int sizeof_vert = 0;

	switch (bdata.fvf) {
		case BatcherEnums::FVF_UNBATCHED:
			return;
		case BatcherEnums::FVF_REGULAR:
			sizeof_vert = sizeof(BatchVertex);
			break;
		case BatcherEnums::FVF_COLOR:
			sizeof_vert = sizeof(BatchVertexColored);
			break;
		case BatcherEnums::FVF_LIGHT_ANGLE:
			sizeof_vert = sizeof(BatchVertexLightAngled);
			break;
		case BatcherEnums::FVF_MODULATED:
			sizeof_vert = sizeof(BatchVertexModulated);
			break;
		case BatcherEnums::FVF_LARGE:
			sizeof_vert = sizeof(BatchVertexLarge);
			break;
		default:
			break;
	}
	
	ERR_FAIL_COND_MSG(sizeof_vert == 0, "GLES1 batcher: Invalid FVF format size! Aborting draw");
	_set_texture_rect_mode(false, use_light_angles, use_modulate, use_large_verts);

	const BatchTex &tex = bdata.batch_textures[p_batch.batch_texture_id];

	if (tex.tile_mode == BatchTex::TILE_FORCE_REPEAT) {
		state.specialization |= CanvasShaderGLES1::USE_FORCE_REPEAT;
	}

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

	// Determine Texture Filter and Repeat from the Batch FVF flags
	RS::CanvasItemTextureRepeat repeat = state.default_repeat;
	if (p_batch.item && p_batch.item->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
		repeat = p_batch.item->texture_repeat;
	}
	if (tex.tile_mode != BatchTex::TILE_OFF) {
		repeat = RS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED;
	}

	RS::CanvasItemTextureFilter filter = state.default_filter;
	if (p_batch.item && p_batch.item->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
		filter = p_batch.item->texture_filter;
	}

	_bind_canvas_texture(tex.RID_texture, filter, repeat);
	glEnable(GL_TEXTURE_2D);

	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	bool use_vbo = bdata.gl_vertex_buffer != 0 && bdata.gl_index_buffer != 0;

	if (likely(use_vbo)) {
		// Bind the massive dynamic buffer
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	GL_CHECK_ERROR("GLES1::Canvas::batch_render_generic: bind VBO/IBO");

	uint64_t pointer_offset = p_batch.first_vert * sizeof_vert;
	const void *base_ptr = nullptr;

	if (!use_vbo) {
		if (bdata.fvf == BatcherEnums::FVF_UNBATCHED || bdata.fvf == BatcherEnums::FVF_REGULAR) {
			base_ptr = (const void *)bdata.vertices.get_data();
		} else {
			base_ptr = (const void *)bdata.unit_vertices.get_data();
		}
		ERR_FAIL_NULL(base_ptr);
		pointer_offset += (uintptr_t)base_ptr;
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, pos)));

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, uv)));

	if (state.using_light) {
		GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
		if (state.normal_used && max_units >= 4) {
			if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				glClientActiveTexture(GL_TEXTURE0 + 1);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, uv)));

				glClientActiveTexture(GL_TEXTURE0 + 2);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, uv)));

				glClientActiveTexture(GL_TEXTURE0 + 3);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, uv)));
			} else {
				glClientActiveTexture(GL_TEXTURE0 + 1);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, pos)));

				glClientActiveTexture(GL_TEXTURE0 + 2);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, uv)));

				glClientActiveTexture(GL_TEXTURE0 + 3);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, pos)));
			}
		} else {
			if (max_units >= 3) {
				glClientActiveTexture(GL_TEXTURE0 + 2);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			if (max_units >= 4) {
				glClientActiveTexture(GL_TEXTURE0 + 3);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}

			if (max_units >= 2) {
				glClientActiveTexture(GL_TEXTURE0 + 1);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, pos)));
			}
			if (state.using_shadow && max_units >= 3) {
				glClientActiveTexture(GL_TEXTURE0 + 2);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertex, pos)));
			}
		}
		glClientActiveTexture(GL_TEXTURE0);
		GL_CHECK_ERROR("GLES1::Canvas::batch_render_generic: light texcoord pointer projection");
	} else {
		GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
		for (int i = 1; i < max_units; i++) {
			glClientActiveTexture(GL_TEXTURE0 + i);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
		glClientActiveTexture(GL_TEXTURE0);
	}

	if (!colored_verts) {
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(
			p_batch.color.r / 255.0f,
			p_batch.color.g / 255.0f,
			p_batch.color.b / 255.0f,
			p_batch.color.a / 255.0f
		);
	} else {
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof_vert, (const void *)(uintptr_t)(pointer_offset + offsetof(BatchVertexColored, col)));
	}
	GL_CHECK_ERROR("GLES1::Canvas::batch_render_generic: setup client states");

	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	switch (p_batch.type) {
		case BatcherEnums::BT_RECT: {
			int num_elements = p_batch.num_commands * 6;
			if (likely(use_vbo)) {
				glDrawElements(GL_TRIANGLES, num_elements, GL_UNSIGNED_SHORT, nullptr);
			} else {
				// Allocate and generate indices locally
				Vector<uint16_t> client_indices;
				client_indices.resize(num_elements);
				uint16_t *idx_ptr = client_indices.ptrw();
				ERR_FAIL_NULL(idx_ptr);
				for (uint32_t q = 0; q < p_batch.num_commands; q++) {
					uint16_t i_pos = static_cast<uint16_t>(q * 6);
					uint16_t q_pos = static_cast<uint16_t>(q * 4);
					idx_ptr[i_pos] = q_pos;
					idx_ptr[i_pos + 1] = q_pos + 1;
					idx_ptr[i_pos + 2] = q_pos + 2;
					idx_ptr[i_pos + 3] = q_pos;
					idx_ptr[i_pos + 4] = q_pos + 2;
					idx_ptr[i_pos + 5] = q_pos + 3;
				}
				glDrawElements(GL_TRIANGLES, num_elements, GL_UNSIGNED_SHORT, idx_ptr);
			}
			GL_CHECK_ERROR("GLES1::Canvas::batch_render_generic: glDrawElements (BT_RECT)");
		} break;
		case BatcherEnums::BT_POLY: {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			int num_elements = p_batch.num_commands;
			glDrawArrays(GL_TRIANGLES, 0, num_elements);
			GL_CHECK_ERROR("GLES1::Canvas::batch_render_generic: glDrawArrays (BT_POLY)");
		} break;
		default:
			break;
	}

	// Cleanup
	if (tex.tile_mode == BatchTex::TILE_FORCE_REPEAT) {
		state.specialization &= ~(CanvasShaderGLES1::USE_FORCE_REPEAT);
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_TEXTURE_2D);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	// Restore baseline color if the vertex array messed it up
	if (colored_verts) {
		glColor4f(
			state.uniforms.final_modulate.r,
			state.uniforms.final_modulate.g,
			state.uniforms.final_modulate.b,
			state.uniforms.final_modulate.a
		);
	}
	GL_CHECK_ERROR("GLES1::Canvas::batch_render_generic: cleanup");
}

void RasterizerCanvasGLES1::render_batches(Item::Command *const *p_commands, Item *p_current_clip, bool &r_reclip, GLES1::CanvasMaterialData *p_material) {
	int num_batches = bdata.batches.size();
	bool skipping = false;
	Transform2D base_extra = state.uniforms.extra_matrix;

	// Extract and apply the CanvasItemMaterial blend mode
	bool transparent_rt = false;
	if (state.render_target != RID()) {
		GLES1::RenderTarget *rt = GLES1::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt && rt->is_transparent) {
			transparent_rt = true;
		}
	} else {
		transparent_rt = true; // Backbuffer
	}

	GLES1::CanvasShaderData::BlendMode blend_mode = GLES1::CanvasShaderData::BLEND_MODE_MIX;
	if (p_material && p_material->shader_data) {
		blend_mode = p_material->shader_data->blend_mode;
	}

	// Blend modes
	if (!state.using_light) {
		set_gl_blend_mode(blend_mode, transparent_rt);
		GL_CHECK_ERROR("GLES1::Canvas::render_batches: set_gl_blend_mode");
	}

	for (int batch_num = 0; batch_num < num_batches; batch_num++) {
		const Batch &batch = bdata.batches[batch_num];

		// Reset specialization
		state.specialization = 0;
		state.mode_variant = CanvasShaderGLES1::ShaderVariant::MODE_QUAD;
		state.shader_version = data.canvas_shader_default_version;

		if (state.using_skeleton) {
			state.specialization |= CanvasShaderGLES1::USE_SKELETON;
		}

		if (state.using_light) {
			state.specialization |= CanvasShaderGLES1::USE_LIGHTING;

			if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				state.specialization |= CanvasShaderGLES1::USE_DIRECTIONAL_LIGHT;
			}

#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			if (state.using_shadow) {
				switch (state.using_light->shadow_filter) {
					case RS::CANVAS_LIGHT_FILTER_PCF13:
					case RS::CANVAS_LIGHT_FILTER_PCF5:
					case RS::CANVAS_LIGHT_FILTER_MAX:
						WARN_PRINT_ONCE_ED("Shadow filters for lights other than None (Fast) are not supported in the Classic renderer.");
					break;
					default:
						break;
				}
			}
#endif
		}

		if (p_material && p_material->shader_data) {
			if (p_material->shader_data->version.is_valid() && p_material->shader_data->valid) {
				state.shader_version = p_material->shader_data->version;
			} else {
				state.shader_version = state.canvas_shader->default_version;
			}
		}

		switch (batch.type) {
			case BatcherEnums::BT_DEFAULT: {
				Transform2D prev_matrix = state.uniforms.modelview_matrix;
				Color prev_modulate = state.uniforms.final_modulate;

				if (batch.item) {
					state.uniforms.modelview_matrix = batch.item->final_transform;
					state.uniforms.final_modulate = batch.item->final_modulate * _render_item_state.item_group_modulate;
					state.uniforms.extra_matrix = Transform2D();
				} else {
					state.uniforms.modelview_matrix = _render_item_state.item_group_base_transform;
					state.uniforms.final_modulate = _render_item_state.item_group_modulate;
					state.uniforms.extra_matrix = Transform2D();
				}

				Item::Command *command = batch.first_command;
				for (uint32_t i = 0; i < batch.num_commands && command != nullptr; i++, command = command->next) {
					if (skipping && command->type != Item::Command::TYPE_ANIMATION_SLICE) {
						command = command->next;
						continue;
					}

					switch (command->type) {
						case Item::Command::TYPE_RECT: {
							Item::CommandRect *r = static_cast<Item::CommandRect *>(command);

							// Clean state
							// (so that rubbish/garbage doesn't ruin stuff later)
							glDisableClientState(GL_COLOR_ARRAY);
							glDisableClientState(GL_TEXTURE_COORD_ARRAY);

							// Texture binding
							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (batch.item && batch.item->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = batch.item->texture_repeat;
							} else if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}

							if (r->flags & CANVAS_RECT_TILE) {
								repeat = RS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED;
							}

							// Texture filtering
							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (batch.item && batch.item->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = batch.item->texture_filter;
							} else if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = p_current_clip->texture_filter;
							}

							_bind_canvas_texture(r->texture, filter, repeat);

							// Shader setup
							_set_texture_rect_mode(true);

							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

							// Must be called every single draw in immediate mode to update the matrices.
							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							// Get the color now after the uniforms to not
							// overwrite it
							Color combined_color = r->modulate * state.uniforms.final_modulate;
							glColor4f(combined_color.r, combined_color.g, combined_color.b, combined_color.a);

							// Bind the default quad buffer
							_bind_quad_buffer();

							// Calculate rects
							Rect2 src_rect = (r->flags & CANVAS_RECT_REGION) ? Rect2(r->source.position * state.texpixel_size, r->source.size * state.texpixel_size) : Rect2(0, 0, 1, 1);
							Rect2 dst_rect = Rect2(r->rect.position, r->rect.size);

							if (dst_rect.size.width < 0) {
								dst_rect.position.x += dst_rect.size.width;
								dst_rect.size.width *= -1;
							}
							if (dst_rect.size.height < 0) {
								dst_rect.position.y += dst_rect.size.height;
								dst_rect.size.height *= -1;
							}

							if (r->flags & CANVAS_RECT_FLIP_H) {
								// Shift origin right, then flip.
								src_rect.position.x += src_rect.size.x;
								src_rect.size.x *= -1;
							}

							if (r->flags & CANVAS_RECT_TRANSPOSE) {
								dst_rect.size.x *= -1; // Encoded into DST_RECT.z for the shader to interpret
							}

							// Push uniforms now
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

							// Protect stack
							glMatrixMode(GL_MODELVIEW);
							glPushMatrix();
							glMatrixMode(GL_TEXTURE);
							glPushMatrix();

							if (state.using_light) {
								GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
								if (max_units >= 2) {
									glActiveTexture(GL_TEXTURE0 + 1);
									glMatrixMode(GL_TEXTURE);
									glPushMatrix();
									glTranslatef(dst_rect.position.x, dst_rect.position.y, 0.0f);
									glScalef(Math::abs(dst_rect.size.x), Math::abs(dst_rect.size.y), 1.0f);
									if (dst_rect.size.x < 0) {
										constexpr GLfloat transpose[16] = { 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
										glMultMatrixf(transpose);
									}
									glActiveTexture(GL_TEXTURE0);
								}
								if (state.normal_used && max_units >= 4) {
									glActiveTexture(GL_TEXTURE0 + 3);
									glMatrixMode(GL_TEXTURE);
									glPushMatrix();
									glTranslatef(dst_rect.position.x, dst_rect.position.y, 0.0f);
									glScalef(Math::abs(dst_rect.size.x), Math::abs(dst_rect.size.y), 1.0f);
									if (dst_rect.size.x < 0) {
										constexpr GLfloat transpose[16] = { 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
										glMultMatrixf(transpose);
									}
									glActiveTexture(GL_TEXTURE0);
								} else if (state.using_shadow && max_units >= 3) {
									glActiveTexture(GL_TEXTURE0 + 2);
									glMatrixMode(GL_TEXTURE);
									glPushMatrix();
									glTranslatef(dst_rect.position.x, dst_rect.position.y, 0.0f);
									glScalef(Math::abs(dst_rect.size.x), Math::abs(dst_rect.size.y), 1.0f);
									if (dst_rect.size.x < 0) {
										constexpr GLfloat transpose[16] = { 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
										glMultMatrixf(transpose);
									}
									glActiveTexture(GL_TEXTURE0);
								}
								glMatrixMode(GL_TEXTURE); // return to GL_TEXTURE0 matrix mode expectation
							}

							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::DST_RECT, Color(dst_rect.position.x, dst_rect.position.y, dst_rect.size.x, dst_rect.size.y), state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SRC_RECT, Color(src_rect.position.x, src_rect.position.y, src_rect.size.x, src_rect.size.y), state.shader_version, state.mode_variant, state.specialization);

							glEnable(GL_TEXTURE_2D);
							glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
							GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_RECT glDrawArrays");

							// Cleanup and restore
							if (state.using_light) {
								GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
								if (max_units >= 2) {
									glActiveTexture(GL_TEXTURE0 + 1);
									glMatrixMode(GL_TEXTURE);
									glPopMatrix();
									glActiveTexture(GL_TEXTURE0);
								}
								if (state.normal_used && max_units >= 4) {
									glActiveTexture(GL_TEXTURE0 + 3);
									glMatrixMode(GL_TEXTURE);
									glPopMatrix();
									glActiveTexture(GL_TEXTURE0);
								} else if (state.using_shadow && max_units >= 3) {
									glActiveTexture(GL_TEXTURE0 + 2);
									glMatrixMode(GL_TEXTURE);
									glPopMatrix();
									glActiveTexture(GL_TEXTURE0);
								}
								glMatrixMode(GL_TEXTURE);
							}

							glMatrixMode(GL_TEXTURE);
							glPopMatrix();
							glMatrixMode(GL_MODELVIEW);
							glPopMatrix();

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glDisableClientState(GL_VERTEX_ARRAY);
							glDisableClientState(GL_TEXTURE_COORD_ARRAY);
							glDisable(GL_TEXTURE_2D);
							
							state.specialization &= ~(CanvasShaderGLES1::USE_FORCE_REPEAT);
						} break;

						case Item::Command::TYPE_NINEPATCH: {
							Item::CommandNinePatch *np = static_cast<Item::CommandNinePatch *>(command);
							_set_texture_rect_mode(false);

							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

							glDisableClientState(GL_COLOR_ARRAY);
							Color combined_color = np->color * state.uniforms.final_modulate;
							glColor4f(combined_color.r, combined_color.g, combined_color.b, combined_color.a);

							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (batch.item && batch.item->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = batch.item->texture_repeat;
							} else if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}

							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (batch.item && batch.item->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = batch.item->texture_filter;
							} else if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = p_current_clip->texture_filter;
							}

							_bind_canvas_texture(np->texture, filter, repeat);

							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}
							glEnable(GL_TEXTURE_2D);

							if (state.texpixel_size == Size2(0.0, 0.0)) {
								continue;
							}

							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

							Rect2 source = np->source;
							if (source.size.x == 0 && source.size.y == 0) {
								source.size.x = state.texture_size.x;
								source.size.y = state.texture_size.y;
							}

							float screen_scale = 1.0;
							if ((bdata.settings_ninepatch_mode == 1) && !Math::is_zero_approx(source.size.x) && !Math::is_zero_approx(source.size.y)) {
								screen_scale = MIN(np->rect.size.x / source.size.x, np->rect.size.y / source.size.y);
								screen_scale = MIN(1.0, screen_scale);
							}

  							// Margin clamping
							float draw_margin_left = np->margin[SIDE_LEFT] * screen_scale;
							float draw_margin_right = np->margin[SIDE_RIGHT] * screen_scale;
							float draw_margin_top = np->margin[SIDE_TOP] * screen_scale;
							float draw_margin_bottom = np->margin[SIDE_BOTTOM] * screen_scale;

							if (draw_margin_left + draw_margin_right > np->rect.size.x) {
								float ratio = np->rect.size.x / (draw_margin_left + draw_margin_right);
								draw_margin_left *= ratio;
								draw_margin_right *= ratio;
							}

							if (draw_margin_top + draw_margin_bottom > np->rect.size.y) {
								float ratio = np->rect.size.y / (draw_margin_top + draw_margin_bottom);
								draw_margin_top *= ratio;
								draw_margin_bottom *= ratio;
							}

							float tex_margin_left = np->margin[SIDE_LEFT];
							float tex_margin_right = np->margin[SIDE_RIGHT];
							float tex_margin_top = np->margin[SIDE_TOP];
							float tex_margin_bottom = np->margin[SIDE_BOTTOM];

							// NPOT bleed inset
							constexpr float EPS = 0.01f;
							float u[4] = {
								static_cast<float>(source.position.x),
								static_cast<float>(source.position.x + tex_margin_left + (tex_margin_left > 0 ? EPS : 0)),
								static_cast<float>(source.position.x + source.size.x - tex_margin_right - (tex_margin_right > 0 ? EPS : 0)),
								static_cast<float>(source.position.x + source.size.x)
							};
							float v[4] = {
								static_cast<float>(source.position.y),
								static_cast<float>(source.position.y + tex_margin_top + (tex_margin_top > 0 ? EPS : 0)),
								static_cast<float>(source.position.y + source.size.y - tex_margin_bottom - (tex_margin_bottom > 0 ? EPS : 0)),
								static_cast<float>(source.position.y + source.size.y)
							};
							float x[4] = {
								static_cast<float>(np->rect.position.x),
								static_cast<float>(np->rect.position.x + draw_margin_left),
								static_cast<float>(np->rect.position.x + np->rect.size.x - draw_margin_right),
								static_cast<float>(np->rect.position.x + np->rect.size.x)
							};
							float y[4] = {
								static_cast<float>(np->rect.position.y),
								static_cast<float>(np->rect.position.y + draw_margin_top),
								static_cast<float>(np->rect.position.y + np->rect.size.y - draw_margin_bottom),
								static_cast<float>(np->rect.position.y + np->rect.size.y)
							};

							float buffer[16 * 2 + 16 * 2] = {};
							for (int row = 0; row < 4; row++) {
								for (int col = 0; col < 4; col++) {
									int idx = (row * 4 + col) * 4;
									buffer[idx + 0] = x[col];
									buffer[idx + 1] = y[row];
									buffer[idx + 2] = u[col] * static_cast<float>(state.texpixel_size.x);
									buffer[idx + 3] = v[row] * static_cast<float>(state.texpixel_size.y);
								}
							}

							if (likely(data.ninepatch_vertices != 0 && data.ninepatch_elements != 0)) {
								glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);
								constexpr uint32_t buffer_size = sizeof(float) * (16 + 16) * 2;
								bool upload_success = _buffer_orphan_and_upload(buffer_size, 0, buffer_size, buffer, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
								if (!check_orphan_success(upload_success)) {
									return;
								}

								glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);

								glEnableClientState(GL_VERTEX_ARRAY);
								glEnableClientState(GL_TEXTURE_COORD_ARRAY);
								glDisableClientState(GL_COLOR_ARRAY);

								glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), nullptr);
								glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (const void *)(sizeof(float) * 2));

								if (state.using_light) {
									GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
									if (state.normal_used && max_units >= 4) {
										if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
											glClientActiveTexture(GL_TEXTURE0 + 1);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (const void *)(sizeof(float) * 2));

											glClientActiveTexture(GL_TEXTURE0 + 2);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (const void *)(sizeof(float) * 2));

											glClientActiveTexture(GL_TEXTURE0 + 3);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (const void *)(sizeof(float) * 2));
										} else {
											glClientActiveTexture(GL_TEXTURE0 + 1);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), nullptr);

											glClientActiveTexture(GL_TEXTURE0 + 2);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (const void *)(sizeof(float) * 2));

											glClientActiveTexture(GL_TEXTURE0 + 3);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), nullptr);
										}
									} else {
										if (max_units >= 3) {
											glClientActiveTexture(GL_TEXTURE0 + 2);
											glDisableClientState(GL_TEXTURE_COORD_ARRAY);
										}
										if (max_units >= 4) {
											glClientActiveTexture(GL_TEXTURE0 + 3);
											glDisableClientState(GL_TEXTURE_COORD_ARRAY);
										}

										if (max_units >= 2) {
											glClientActiveTexture(GL_TEXTURE0 + 1);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), nullptr);
										}
										if (state.using_shadow && max_units >= 3) {
											glClientActiveTexture(GL_TEXTURE0 + 2);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), nullptr);
										}
									}
									glClientActiveTexture(GL_TEXTURE0);
								} else {
									GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
									for (int ii = 1; ii < max_units; ii++) {
										glClientActiveTexture(GL_TEXTURE0 + ii);
										glDisableClientState(GL_TEXTURE_COORD_ARRAY);
									}
									glClientActiveTexture(GL_TEXTURE0);
								}
								GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_NINEPATCH client states");

								glDrawElements(GL_TRIANGLES, 18 * 3 - (np->draw_center ? 0 : 6), GL_UNSIGNED_SHORT, nullptr);
								GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_NINEPATCH glDrawElements");
							} else {
								glBindBuffer(GL_ARRAY_BUFFER, 0);
								glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

								glEnableClientState(GL_VERTEX_ARRAY);
								glEnableClientState(GL_TEXTURE_COORD_ARRAY);
								glDisableClientState(GL_COLOR_ARRAY);

								glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), buffer);
								glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer + 2);

								if (state.using_light) {
									GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
									if (state.normal_used && max_units >= 4) {
										if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
											glClientActiveTexture(GL_TEXTURE0 + 1);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer + 2);

											glClientActiveTexture(GL_TEXTURE0 + 2);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer + 2);

											glClientActiveTexture(GL_TEXTURE0 + 3);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer + 2);
										} else {
											glClientActiveTexture(GL_TEXTURE0 + 1);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer);

											glClientActiveTexture(GL_TEXTURE0 + 2);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer + 2);

											glClientActiveTexture(GL_TEXTURE0 + 3);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer);
										}
									} else {
										if (max_units >= 3) {
											glClientActiveTexture(GL_TEXTURE0 + 2);
											glDisableClientState(GL_TEXTURE_COORD_ARRAY);
										}
										if (max_units >= 4) {
											glClientActiveTexture(GL_TEXTURE0 + 3);
											glDisableClientState(GL_TEXTURE_COORD_ARRAY);
										}

										if (max_units >= 2) {
											glClientActiveTexture(GL_TEXTURE0 + 1);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer);
										}
										if (state.using_shadow && max_units >= 3) {
											glClientActiveTexture(GL_TEXTURE0 + 2);
											glEnableClientState(GL_TEXTURE_COORD_ARRAY);
											glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), buffer);
										}
									}
									glClientActiveTexture(GL_TEXTURE0);
								} else {
									GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
									for (int ii = 1; ii < max_units; ii++) {
										glClientActiveTexture(GL_TEXTURE0 + ii);
										glDisableClientState(GL_TEXTURE_COORD_ARRAY);
									}
									glClientActiveTexture(GL_TEXTURE0);
								}
								GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_NINEPATCH client states (fallback)");

								glDrawElements(GL_TRIANGLES, 18 * 3 - (np->draw_center ? 0 : 6), GL_UNSIGNED_SHORT, ninepatch_elems);
								GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_NINEPATCH glDrawElements (fallback)");
							}

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
							glDisableClientState(GL_VERTEX_ARRAY);
							glDisableClientState(GL_TEXTURE_COORD_ARRAY);
							glDisable(GL_TEXTURE_2D);
						} break;

						case Item::Command::TYPE_CLIP_IGNORE: {
							Item::CommandClipIgnore *ci = static_cast<Item::CommandClipIgnore *>(command);

							if (p_current_clip) {
								if (ci->ignore != r_reclip) {
									if (ci->ignore) {
										gl_disable_scissor();
										r_reclip = true;
									} else {
										int x = p_current_clip->final_clip_rect.position.x;
										int y = p_current_clip->final_clip_rect.position.y;
										int w = MAX(0, (int)p_current_clip->final_clip_rect.size.x);
										int h = MAX(0, (int)p_current_clip->final_clip_rect.size.y);
										gl_enable_scissor(x, y, w, h);
										GL_CHECK_ERROR("GLES1::Canvas::render_batches: Item::Command::TYPE_CLIP_IGNORE: glScissor");
										r_reclip = false;
									}
								}
							}
						} break;

						case Item::Command::TYPE_POLYGON: {
							Item::CommandPolygon *polygon = static_cast<Item::CommandPolygon *>(command);
							_legacy_draw_polygon(polygon, p_material);
						} break;

						case Item::Command::TYPE_PRIMITIVE: {
							Item::CommandPrimitive *pr = static_cast<Item::CommandPrimitive *>(command);

							switch (pr->point_count) {
								case 2: {
									_legacy_draw_line(pr, p_material);
								} break;
								default: {
									_legacy_draw_primitive(pr, p_material);
								} break;
							}
						} break;

						case Item::Command::TYPE_MESH: {
							Item::CommandMesh *mesh_cmd = static_cast<Item::CommandMesh *>(command);
							_set_texture_rect_mode(false);

							// Bind Shader and stack the item's material (if any)
							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

							// CommandMesh has its own transform and modulate that stack with the item's baseline
							Transform2D prev_transform_extra = state.uniforms.extra_matrix;
							Color prev_colour_module = state.uniforms.final_modulate;

							state.uniforms.extra_matrix = state.uniforms.extra_matrix * mesh_cmd->transform;
							state.uniforms.final_modulate = state.uniforms.final_modulate * mesh_cmd->modulate;

							// Setup Texture, Filter, and Repeat
							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (batch.item && batch.item->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = batch.item->texture_repeat;
							} else if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}

							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (batch.item && batch.item->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = batch.item->texture_filter;
							} else if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = p_current_clip->texture_filter;
							}

							_bind_canvas_texture(mesh_cmd->texture, filter, repeat);

							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							glEnable(GL_TEXTURE_2D);

							if (state.texpixel_size != Size2(0.0, 0.0)) {
								state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							}

							// Fetch and Draw Mesh Data
							GLES1::Mesh *mesh_data = GLES1::MeshStorage::get_singleton()->get_mesh(mesh_cmd->mesh);
							if (mesh_data) {
								bool apply_tu1 = (
									!state.using_light &&
									GLES1::Config::get_singleton()->max_texture_units >= 2 &&
									GLES1::Config::get_singleton()->support_texture_env_combine
								);
								if (apply_tu1) {
									RID white_tex_rid = GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
									GLES1::TextureStorage::get_singleton()->texture_bind_and_validate(
										white_tex_rid,
										GL_TEXTURE1,
										state.default_filter,
										state.default_repeat
									);

									glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
									glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
									glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
									glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
									glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
									glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
									glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_CONSTANT);
									GL_CHECK_ERROR("GLES1::Canvas::render_batches: Item::Command::TYPE_MESH: tu1 glTexEnvi");

									float env_color[4] = {
										state.uniforms.final_modulate.r,
										state.uniforms.final_modulate.g,
										state.uniforms.final_modulate.b,
										state.uniforms.final_modulate.a
									};
									glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, env_color);
									GL_CHECK_ERROR("GLES1::Canvas::render_batches: Item::Command::TYPE_MESH: tu1 glTexEnvfv");
									glActiveTexture(GL_TEXTURE0);
								}

								// Loop using the double pointer array
								for (uint32_t j = 0; j < mesh_data->surface_count; j++) {
									GLES1::Mesh::Surface *s = mesh_data->surfaces[j];
									uint32_t context_generation = GLES1::Config::get_singleton()->context_generation;
									if (unlikely(!s)) {
										continue;
									}

									// Context loss detection
									if (unlikely(s->context_generation != context_generation)) {
										s->vertex_buffer = 0;
										s->attribute_buffer = 0;
										s->index_buffer = 0;

										if (s->versions) {
											for (uint32_t v = 0; v < s->version_count; v++) {
												s->versions[v].vertex_array = 0;
											}
										}

										s->context_generation = context_generation;
									}

									bool use_vertex_vbo = s->vertex_buffer != 0;
									bool use_attr_vbo = s->attribute_buffer != 0;
									bool use_index_vbo = s->index_buffer != 0;

									if (s->index_count > 0) {
										if (likely(use_index_vbo)) {
											glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
										} else {
											glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
										}
									} else {
										glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
									}
									GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MESH bind VBO/IBO");

									bool surface_has_colors = false;

									// Setup vertex attributes from the cached Version struct
									if (likely(s->version_count > 0 && s->versions)) {
										GLES1::Mesh::Surface::Version *v = &s->versions[0];

										if (unlikely(!v)) {
											continue;
										}

										for (int k = 0; k < maximum_attributes; k++) {
											if (v->attribs[k].enabled) {
												if (k == RS::ARRAY_VERTEX) {
													if (likely(use_vertex_vbo)) {
														// Positions live in the vertex buffer
														glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
														glEnableClientState(GL_VERTEX_ARRAY);
														glVertexPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
													} else {
														glBindBuffer(GL_ARRAY_BUFFER, 0);
														glEnableClientState(GL_VERTEX_ARRAY);
														ERR_FAIL_COND(s->vertex_buffer_fallback.is_empty());
														glVertexPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, s->vertex_buffer_fallback.ptr() + v->attribs[k].offset);
													}
												} else if (k == RS::ARRAY_COLOR) {
													surface_has_colors = true;
													if (likely(use_attr_vbo)) {
														// Colors live in the attribute buffer
														glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
														glEnableClientState(GL_COLOR_ARRAY);
														glColorPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
													} else {
														glBindBuffer(GL_ARRAY_BUFFER, 0);
														glEnableClientState(GL_COLOR_ARRAY);
														ERR_FAIL_COND(s->attribute_buffer_fallback.is_empty());
														glColorPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, s->attribute_buffer_fallback.ptr() + v->attribs[k].offset);
													}
												} else if (k == RS::ARRAY_TEX_UV) {
													if (likely(use_attr_vbo)) {
														// UVs live in the attribute buffer
														glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
														glEnableClientState(GL_TEXTURE_COORD_ARRAY);
														glTexCoordPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
													} else {
														glBindBuffer(GL_ARRAY_BUFFER, 0);
														glEnableClientState(GL_TEXTURE_COORD_ARRAY);
														ERR_FAIL_COND(s->attribute_buffer_fallback.is_empty());
														glTexCoordPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, s->attribute_buffer_fallback.ptr() + v->attribs[k].offset);
													}
												}
											} else {
												if (k == RS::ARRAY_VERTEX) {
													glDisableClientState(GL_VERTEX_ARRAY);
												} else if (k == RS::ARRAY_COLOR) {
													glDisableClientState(GL_COLOR_ARRAY);
													Color c = state.uniforms.final_modulate;
													glColor4f(c.r, c.g, c.b, c.a);
												} else if (k == RS::ARRAY_TEX_UV) {
													glDisableClientState(GL_TEXTURE_COORD_ARRAY);
												}
											}
										}
										GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MESH setup client pointers");
									}

									// Setup TU1 specifically for this surface
									if (apply_tu1) {
										glActiveTexture(GL_TEXTURE1);
										if (surface_has_colors) {
											glEnable(GL_TEXTURE_2D);
										} else {
											glDisable(GL_TEXTURE_2D);
										}
										glActiveTexture(GL_TEXTURE0);
									}

									// Draw call using the correct index count and vertex count
									GLenum gl_primitive = get_gl_primitive_type(s->primitive);
									if (s->index_count > 0) {
										bool needs_32_bit = s->vertex_count >= (1 << 16);

										if (unlikely(needs_32_bit && !GLES1::Config::get_singleton()->support_32_bits_indices)) {
											ERR_PRINT_ONCE("GLES1: Device does not support 32-bit indices for large 2D meshes. Skipping draw.");
										} else {
											GLenum index_type = needs_32_bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
											if (likely(use_index_vbo)) {
												glDrawElements(gl_primitive, s->index_count, index_type, nullptr);
											} else {
												ERR_FAIL_COND(s->index_buffer_fallback.is_empty());
												glDrawElements(gl_primitive, s->index_count, index_type, s->index_buffer_fallback.ptr());
											}
											GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MESH glDrawElements");
										}
									} else {
										glDrawArrays(gl_primitive, 0, s->vertex_count);
										GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MESH glDrawArrays");
									}

									// Clean up attributes
									glDisableClientState(GL_VERTEX_ARRAY);
									glDisableClientState(GL_TEXTURE_COORD_ARRAY);
									glDisableClientState(GL_COLOR_ARRAY);
								}

								// Clean up texture trick
								if (apply_tu1) {
									glActiveTexture(GL_TEXTURE1);
									glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
									glDisable(GL_TEXTURE_2D);
									glActiveTexture(GL_TEXTURE0);
								}
							}

							// Restore state
							state.uniforms.extra_matrix = prev_transform_extra;
							state.uniforms.final_modulate = prev_colour_module;

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
							glDisable(GL_TEXTURE_2D);
						} break;

						case Item::Command::TYPE_MULTIMESH: {
							Item::CommandMultiMesh *mmesh = static_cast<Item::CommandMultiMesh *>(command);

							GLES1::MultiMesh *multi_mesh = GLES1::MeshStorage::get_singleton()->get_multimesh(mmesh->multimesh);
							if (!multi_mesh || multi_mesh->data_cache.is_empty()) {
								break;
							}

							GLES1::Mesh *mesh_data = GLES1::MeshStorage::get_singleton()->get_mesh(multi_mesh->mesh);
							if (!mesh_data) {
								break;
							}

							_set_texture_rect_mode(false);

							state.specialization |= CanvasShaderGLES1::USE_INSTANCING;
							if (multi_mesh->uses_custom_data) {
								state.specialization |= CanvasShaderGLES1::USE_INSTANCE_CUSTOM;
							}

							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (batch.item && batch.item->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = batch.item->texture_repeat;
							} else if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}

							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (batch.item && batch.item->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = batch.item->texture_filter;
							} else if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = p_current_clip->texture_filter;
							}

							_bind_canvas_texture(mmesh->texture, filter, repeat);
							glEnable(GL_TEXTURE_2D);

							if (state.texpixel_size != Size2(0.0, 0.0)) {
								state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							}

							int amount = multi_mesh->visible_instances;
							if (amount == -1) {
								amount = multi_mesh->instances;
							}

							if (amount <= 0) {
								break;
							}

							uint32_t stride = multi_mesh->stride_cache;
							uint32_t color_ofs = multi_mesh->color_offset_cache;
							uint32_t custom_data_ofs = multi_mesh->custom_data_offset_cache;

							const float *base_buffer = multi_mesh->data_cache.ptr();
							ERR_FAIL_NULL(base_buffer);

							Transform2D mesh_base_extra = state.uniforms.extra_matrix;
							Color base_modulate = state.uniforms.final_modulate;

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							bool apply_tu1 = (
								!state.using_light &&
								GLES1::Config::get_singleton()->max_texture_units >= 2 &&
								GLES1::Config::get_singleton()->support_texture_env_combine
							);
							if (apply_tu1) {
								RID white_tex_rid = GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
								GLES1::TextureStorage::get_singleton()->texture_bind_and_validate(
									white_tex_rid,
									GL_TEXTURE1,
									state.default_filter,
									state.default_repeat
								);

								glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
								glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
								glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
								glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
								glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
								glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
								glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_CONSTANT);
								GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MULTIMESH tu1 glTexEnvi");
								glActiveTexture(GL_TEXTURE0);
							}

							for (uint32_t j = 0; j < mesh_data->surface_count; j++) {
								GLES1::Mesh::Surface *s = mesh_data->surfaces[j];
								uint32_t context_generation = GLES1::Config::get_singleton()->context_generation;
								if (unlikely(!s)) {
									continue;
								}

								// Context loss detection
								if (unlikely(s->context_generation != context_generation)) {
									s->vertex_buffer = 0;
									s->attribute_buffer = 0;
									s->index_buffer = 0;

									if (s->versions) {
										for (uint32_t v = 0; v < s->version_count; v++) {
											s->versions[v].vertex_array = 0;
										}
									}

									s->context_generation = context_generation;
								}

								bool use_vertex_vbo = s->vertex_buffer != 0;
								bool use_attr_vbo = s->attribute_buffer != 0;
								bool use_index_vbo = s->index_buffer != 0;

								if (s->index_count > 0) {
									if (likely(use_index_vbo)) {
										glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
									} else {
										glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
									}
								} else {
									glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
								}
								GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MULTIMESH bind VBO/IBO");

								bool surface_has_colors = false;

								// Set up client pointers
								if (likely(s->version_count > 0 && s->versions)) {
									GLES1::Mesh::Surface::Version *v = &s->versions[0];

									if (unlikely(!v)) {
										continue;
									}

									for (int k = 0; k < maximum_attributes; k++) {
										if (v->attribs[k].enabled) {
											if (k == RS::ARRAY_VERTEX) {
												if (likely(use_vertex_vbo)) {
													glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
													glEnableClientState(GL_VERTEX_ARRAY);
													glVertexPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
												} else {
													glBindBuffer(GL_ARRAY_BUFFER, 0);
													glEnableClientState(GL_VERTEX_ARRAY);
													ERR_FAIL_COND(s->vertex_buffer_fallback.is_empty());
													glVertexPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, s->vertex_buffer_fallback.ptr() + v->attribs[k].offset);
												}
											} else if (k == RS::ARRAY_COLOR) {
												// If the MultiMesh drives the color, we must suppress
												// the mesh's innate vertex colors
												// so glColor4f takes priority over the pipeline.
												if (multi_mesh->uses_colors) {
													glDisableClientState(GL_COLOR_ARRAY);
												} else {
													surface_has_colors = true;
													if (likely(use_attr_vbo)) {
														glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
														glEnableClientState(GL_COLOR_ARRAY);
														glColorPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
													} else {
														glBindBuffer(GL_ARRAY_BUFFER, 0);
														glEnableClientState(GL_COLOR_ARRAY);
														ERR_FAIL_COND(s->attribute_buffer_fallback.is_empty());
														glColorPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, s->attribute_buffer_fallback.ptr() + v->attribs[k].offset);
													}
												}
											} else if (k == RS::ARRAY_TEX_UV) {
												if (likely(use_attr_vbo)) {
													glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
													glEnableClientState(GL_TEXTURE_COORD_ARRAY);
													glTexCoordPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
												} else {
													glBindBuffer(GL_ARRAY_BUFFER, 0);
													glEnableClientState(GL_TEXTURE_COORD_ARRAY);
													ERR_FAIL_COND(s->attribute_buffer_fallback.is_empty());
													glTexCoordPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, s->attribute_buffer_fallback.ptr() + v->attribs[k].offset);
												}
											}
										} else {
											if (k == RS::ARRAY_VERTEX) {
												glDisableClientState(GL_VERTEX_ARRAY);
											} else if (k == RS::ARRAY_COLOR) {
												glDisableClientState(GL_COLOR_ARRAY);
											} else if (k == RS::ARRAY_TEX_UV) {
												glDisableClientState(GL_TEXTURE_COORD_ARRAY);
											}
										}
									}
									GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MULTIMESH setup client pointers");
								}

								// Setup TU1 specifically for this surface
								if (apply_tu1) {
									glActiveTexture(GL_TEXTURE1);
									if (surface_has_colors) {
										glEnable(GL_TEXTURE_2D);
									} else {
										glDisable(GL_TEXTURE_2D);
									}
									glActiveTexture(GL_TEXTURE0);
								}

								GLenum gl_primitive = get_gl_primitive_type(s->primitive);
								bool needs_32_bit = s->vertex_count >= (1 << 16);
								GLenum index_type = needs_32_bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

								// Instancing
								for (int k = 0; k < amount; k++) {
									const float *buffer = base_buffer + k * stride;

									if (unlikely(!buffer)) {
										continue;
									}

									// Transform matrix
									Transform2D instance_xform;
									instance_xform.columns[0][0] = buffer[0]; // X.x
									instance_xform.columns[1][0] = buffer[1]; // Y.x
									instance_xform.columns[2][0] = buffer[3]; // Origin.x
									instance_xform.columns[0][1] = buffer[4]; // X.y
									instance_xform.columns[1][1] = buffer[5]; // Y.y
									instance_xform.columns[2][1] = buffer[7]; // Origin.y

									// Modulate / Color
									if (multi_mesh->uses_colors) {
										const float *color_data = buffer + color_ofs;

										if (unlikely(!color_data)) {
											continue;
										}

										Color instance_color(color_data[0], color_data[1], color_data[2], color_data[3]);
										state.uniforms.final_modulate = base_modulate * instance_color;
									} else {
										state.uniforms.final_modulate = base_modulate;
									}

									// Custom data (particles animation UV slicing)
									if (multi_mesh->uses_custom_data && p_material) {
										int h_frames = MAX(1, p_material->particles_anim_h_frames);
										int v_frames = MAX(1, p_material->particles_anim_v_frames);

										if (h_frames * v_frames > 1) {
											const float *custom_data = buffer + custom_data_ofs;

											if (unlikely(!custom_data)) {
												continue;
											}

											// CPUParticles2D packs the animation phase (0.0 to 1.0) into custom.z (index 2)
											float anim_phase = custom_data[2];

											bool loop = p_material->particles_anim_loop;

											float total_frames = (float)(h_frames * v_frames);
											float particle_frame = Math::floor(anim_phase * total_frames);

											if (!loop) {
												particle_frame = CLAMP(particle_frame, 0.0f, total_frames - 1.0f);
											} else {
												particle_frame = Math::fmod(particle_frame, total_frames);
											}

											float offset_x = Math::fmod(particle_frame, (float)h_frames) / (float)h_frames;
											float offset_y = Math::floor((particle_frame + 0.5f) / (float)h_frames) / (float)v_frames;

											// Hijack the texture matrix to slice the sprite sheet per instance
											glMatrixMode(GL_TEXTURE);
											glLoadIdentity();
											glTranslatef(offset_x, offset_y, 0.0f);
											glScalef(1.0f / (float)h_frames, 1.0f / (float)v_frames, 1.0f);
											glMatrixMode(GL_MODELVIEW);

											// The base MultiMesh quad is sized for the full atlas.
											// So, it needs to physically scale down the instance matrix
											// so the quad matches a single frame's size.
											instance_xform.columns[0][0] /= (float)h_frames;
											instance_xform.columns[0][1] /= (float)h_frames;
											instance_xform.columns[1][0] /= (float)v_frames;
											instance_xform.columns[1][1] /= (float)v_frames;
										}
									}

									state.uniforms.extra_matrix = mesh_base_extra * instance_xform;

									// Re upload uniforms per instance to route the new matrices
									_set_canvas_uniforms();

									// Update TU1's color only if it's active
									if (apply_tu1 && surface_has_colors) {
										glActiveTexture(GL_TEXTURE1);
										float env_color[4] = {
											state.uniforms.final_modulate.r,
											state.uniforms.final_modulate.g,
											state.uniforms.final_modulate.b,
											state.uniforms.final_modulate.a
										};
										glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, env_color);
										GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MULTIMESH tu1 glTexEnvfv");
										glActiveTexture(GL_TEXTURE0);
									}

									// Always issue the modulate fallback so it pushes glColor4f
									glColor4f(state.uniforms.final_modulate.r, state.uniforms.final_modulate.g, state.uniforms.final_modulate.b, state.uniforms.final_modulate.a);

									if (s->index_count > 0) {
										if (unlikely(needs_32_bit && !GLES1::Config::get_singleton()->support_32_bits_indices)) {
											ERR_PRINT_ONCE("GLES1: Device does not support 32-bit indices for large MultiMeshes.");
										} else {
											if (likely(use_index_vbo)) {
												glDrawElements(gl_primitive, s->index_count, index_type, nullptr);
											} else {
												ERR_FAIL_COND(s->index_buffer_fallback.is_empty());
												glDrawElements(gl_primitive, s->index_count, index_type, s->index_buffer_fallback.ptr());
											}
											GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MULTIMESH glDrawElements");
										}
									} else {
										glDrawArrays(gl_primitive, 0, s->vertex_count);
										GL_CHECK_ERROR("GLES1::Canvas::render_batches: TYPE_MULTIMESH glDrawArrays");
									}
								}

								// Clean up attributes
								glDisableClientState(GL_VERTEX_ARRAY);
								glDisableClientState(GL_TEXTURE_COORD_ARRAY);
								glDisableClientState(GL_COLOR_ARRAY);
							}

							state.specialization &= ~CanvasShaderGLES1::USE_INSTANCING;
							state.specialization &= ~CanvasShaderGLES1::USE_INSTANCE_CUSTOM;

							// Restore matrices/colors
							state.uniforms.extra_matrix = mesh_base_extra;
							state.uniforms.final_modulate = base_modulate;

							_set_canvas_uniforms();

							// Reset the texture matrix
							glMatrixMode(GL_TEXTURE);
							glLoadIdentity();
							glMatrixMode(GL_MODELVIEW);

							// Clean up tu1
							if (apply_tu1) {
								glActiveTexture(GL_TEXTURE1);
								glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
								glDisable(GL_TEXTURE_2D);
								glActiveTexture(GL_TEXTURE0);
							}

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
							glDisable(GL_TEXTURE_2D);
						} break;

						case Item::Command::TYPE_TRANSFORM: {
							Item::CommandTransform *transform = static_cast<Item::CommandTransform *>(command);
							state.uniforms.extra_matrix = transform->xform;

							// Reload the base modelview matrix so glMultMatrixf
							// doesn't stack onto previous extra_matrix states
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::MODELVIEW_MATRIX, state.uniforms.modelview_matrix, state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::EXTRA_MATRIX, state.uniforms.extra_matrix, state.shader_version, state.mode_variant, state.specialization);
						} break;

						case Item::Command::TYPE_ANIMATION_SLICE: {
							const Item::CommandAnimationSlice *as = static_cast<const Item::CommandAnimationSlice *>(command);

							double current_time = RSG::rasterizer->get_total_time();
							double local_time = Math::fposmod(current_time - as->offset, as->animation_length);
							skipping = !(local_time >= as->slice_begin && local_time < as->slice_end);

							RenderingServerDefault::redraw_request(); // animation visible means redraw request
						} break;

						case Item::Command::TYPE_PARTICLES:
						case Item::Command::TYPE_CALLBACK:
						default: {
							// Not supported
						} break;
					}
				}
				
				state.uniforms.modelview_matrix = prev_matrix;
				state.uniforms.extra_matrix = base_extra;
				state.uniforms.final_modulate = prev_modulate;
			} break;

			case BatcherEnums::BT_RECT:
			case BatcherEnums::BT_POLY: {
				_batch_render_generic(batch, p_material);
			} break;

			default:
				break;
		}
	}
}

void RasterizerCanvasGLES1::_draw_gui_primitive(int p_points, const Vector2 *p_vertices, const Color *p_colors, const Vector2 *p_uvs, const float *p_light_angles) {
	if (p_points <= 0) {
		return;
	}
	glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);

	uint32_t vertex_size = p_points * 2 * sizeof(float);
	uint32_t color_size = p_colors ? p_points * sizeof(Color) : 0;
	uint32_t uv_size = p_uvs ? p_points * 2 * sizeof(float) : 0;
	uint32_t total_size = vertex_size + color_size + uv_size;

	bool use_vbo = data.polygon_buffer != 0;

	if (use_vbo) {
		// Lower VRAM fragmentation by only growing the buffer.
		// If it fits, we pass the tracked size with
		// nullptr to perform a zero-cost buffer orphan.
		if (total_size > data.polygon_buffer_size) {
			data.polygon_buffer_size = next_power_of_2(total_size);
		}
		uint32_t offset = 0;

		// Vertices
#ifdef REAL_T_IS_DOUBLE
		Vector<float> vtx_f;
		vtx_f.resize(p_points * 2);
		for (int i = 0; i < p_points; i++) {
			vtx_f.write[i * 2 + 0] = static_cast<float>(p_vertices[i].x);
			vtx_f.write[i * 2 + 1] = static_cast<float>(p_vertices[i].y);
		}
		bool upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, vertex_size, vtx_f.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
#else
		bool upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, vertex_size, p_vertices, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
#endif
		if (!check_orphan_success(upload_success)) {
			return;
		}

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);

		uint32_t uv_start_offset = offset + vertex_size + color_size;
		if (state.using_light) {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			if (state.normal_used && max_units >= 4) {
				if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, p_uvs ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, p_uvs ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, p_uvs ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);
				} else {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, p_uvs ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
				}
			} else {
				if (max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				if (max_units >= 4) {
					glClientActiveTexture(GL_TEXTURE0 + 3);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}

				if (max_units >= 2) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
				}
				if (state.using_shadow && max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
				}
			}
			glClientActiveTexture(GL_TEXTURE0);
		} else {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			for (int i = 1; i < max_units; i++) {
				glClientActiveTexture(GL_TEXTURE0 + i);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE0);
		}
		offset += vertex_size;

		// Colors
		if (p_colors) {
			upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, color_size, p_colors, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
			if (!check_orphan_success(upload_success)) {
				return;
			}
			glEnableClientState(GL_COLOR_ARRAY);
			glColorPointer(4, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
			offset += color_size;
		} else {
			glDisableClientState(GL_COLOR_ARRAY);
			Color c = state.uniforms.final_modulate;
			glColor4f(c.r, c.g, c.b, c.a);
		}

		// UVs
		if (p_uvs) {
#ifdef REAL_T_IS_DOUBLE
			Vector<float> uv_f;
			uv_f.resize(p_points * 2);
			for (int i = 0; i < p_points; i++) {
				uv_f.write[i * 2 + 0] = static_cast<float>(p_uvs[i].x);
				uv_f.write[i * 2 + 1] = static_cast<float>(p_uvs[i].y);
			}
			upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, uv_size, uv_f.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
#else
			upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, uv_size, p_uvs, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
#endif
			if (!check_orphan_success(upload_success)) {
				return;
			}

			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
			offset += uv_size;

			// Ensure texture mapping is on
			glEnable(GL_TEXTURE_2D);
		} else {
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glDisable(GL_TEXTURE_2D);
		}
		GL_CHECK_ERROR("GLES1::Canvas::_draw_gui_primitive: buffer subdata and pointers");
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		ERR_FAIL_NULL(p_vertices);

#ifdef REAL_T_IS_DOUBLE
		// Convert vertices to float array
		Vector<float> vtx_f;
		vtx_f.resize(p_points * 2);
		for (int i = 0; i < p_points; i++) {
			vtx_f.write[i * 2 + 0] = static_cast<float>(p_vertices[i].x);
			vtx_f.write[i * 2 + 1] = static_cast<float>(p_vertices[i].y);
		}
		const float *vert_ptr = vtx_f.ptr();
		glVertexPointer(2, GL_FLOAT, 0, vert_ptr);
#else
		const Vector2 *vert_ptr = p_vertices;
#endif

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, vert_ptr);

		// UV temporary
#ifdef REAL_T_IS_DOUBLE
		Vector<float> uv_f;
		const float *uv_ptr = nullptr;
		if (p_uvs) {
			uv_f.resize(p_points * 2);
			for (int i = 0; i < p_points; i++) {
				uv_f.write[i * 2 + 0] = static_cast<float>(p_uvs[i].x);
				uv_f.write[i * 2 + 1] = static_cast<float>(p_uvs[i].y);
			}
			uv_ptr = uv_f.ptr();
		}
#else
		const Vector2 *uv_ptr = p_uvs;
#endif

		if (state.using_light) {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			if (state.normal_used && max_units >= 4) {
				if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : vert_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : vert_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : vert_ptr);
				} else {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, vert_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : vert_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, vert_ptr);
				}
			} else {
				if (max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				if (max_units >= 4) {
					glClientActiveTexture(GL_TEXTURE0 + 3);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}

				if (max_units >= 2) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, vert_ptr);
				}
				if (state.using_shadow && max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, vert_ptr);
				}
			}
			glClientActiveTexture(GL_TEXTURE0);
		} else {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			for (int i = 1; i < max_units; i++) {
				glClientActiveTexture(GL_TEXTURE0 + i);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE0);
		}

		if (p_colors) {
			glEnableClientState(GL_COLOR_ARRAY);
			glColorPointer(4, GL_FLOAT, 0, p_colors);
		} else {
			glDisableClientState(GL_COLOR_ARRAY);
			Color c = state.uniforms.final_modulate;
			glColor4f(c.r, c.g, c.b, c.a);
		}

		if (p_uvs) {
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr);
			glEnable(GL_TEXTURE_2D);
		} else {
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glDisable(GL_TEXTURE_2D);
		}
	}

	// For Gizmos, we often draw Lines, Triangles, or Points depending on p_points
	GLenum draw_mode = GL_INVALID_ENUM;
	
	if (p_points == 1) {
		draw_mode = GL_POINTS;
	} else if (p_points == 2) {
		draw_mode = GL_LINES;
	} else if (p_points == 3) {
		draw_mode = GL_TRIANGLES;
	} else {
		draw_mode = GL_TRIANGLE_FAN;
	}

	glDrawArrays(draw_mode, 0, p_points);
	GL_CHECK_ERROR("GLES1::Canvas::_draw_gui_primitive: glDrawArrays");

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_TEXTURE_2D);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES1::Canvas::_draw_gui_primitive: glBindBuffer");
}

void RasterizerCanvasGLES1::_legacy_draw_primitive(Item::CommandPrimitive *p_pr, GLES1::CanvasMaterialData *p_material) {
	_set_texture_rect_mode(false);

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

	ERR_FAIL_COND(p_pr->point_count < 1);

	_bind_canvas_texture(p_pr->texture, state.default_filter, state.default_repeat);

	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	if (p_pr->texture.is_valid()) {
		glEnable(GL_TEXTURE_2D);
	} else {
		glDisable(GL_TEXTURE_2D);
	}

	// Bake the colors before sending them down the pipeline
	Color *baked_colors = SAFE_ALLOCA_ARRAY(Color, p_pr->point_count);
	if (likely(baked_colors)) {
		for (uint32_t i = 0; i < p_pr->point_count; i++) {
			baked_colors[i] = p_pr->colors[i] * state.uniforms.final_modulate;
		}
		_draw_gui_primitive(p_pr->point_count, p_pr->points, baked_colors, p_pr->uvs);
	} else {
		_draw_gui_primitive(p_pr->point_count, p_pr->points, p_pr->colors, p_pr->uvs);
	}

	glDisable(GL_TEXTURE_2D);
	GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_primitive: cleanup");
}

void RasterizerCanvasGLES1::_legacy_draw_line(Item::CommandPrimitive *p_pr, GLES1::CanvasMaterialData *p_material) {
	// Godot 4 uses TYPE_PRIMITIVE with 2 points to draw lines
	_legacy_draw_primitive(p_pr, p_material);
}

void RasterizerCanvasGLES1::_legacy_draw_polygon(Item::CommandPolygon *p_poly, GLES1::CanvasMaterialData *p_material) {
	if (!polygon_cache.has(p_poly->polygon.polygon_id)) {
		return;
	}

	const PolyData &pd = polygon_cache[p_poly->polygon.polygon_id];
	if (pd.points.is_empty()) {
		return;
	}

	_set_texture_rect_mode(false);

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

	// Bind texture and get pixel size
	_bind_canvas_texture(p_poly->texture, state.default_filter, state.default_repeat);

	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
	glEnable(GL_TEXTURE_2D);

	uint32_t points_count = pd.points.size();
	uint32_t points_size = points_count * 2 * sizeof(float);
	uint32_t uvs_size = pd.uvs.size() * 2 * sizeof(float);

	// Only allocate VBO space for colors if there is actually one color per vertex
	bool use_vertex_colors = pd.colors.size() > 1;
	uint32_t colors_size = use_vertex_colors ? (points_count * sizeof(Color)) : 0;

	bool use_vbo = data.polygon_buffer != 0 && data.polygon_index_buffer != 0;

	if (use_vbo) {
		// Setup vertex attributes
		glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);

		// Orphan the buffer and upload new data
		uint32_t total_size = points_size + uvs_size + colors_size;
		if (total_size > data.polygon_buffer_size) {
			data.polygon_buffer_size = next_power_of_2(total_size);
		}
		uint32_t offset = 0;

		// Points
#ifdef REAL_T_IS_DOUBLE
		Vector<float> pts_f;
		pts_f.resize(points_count * 2);
		for (uint32_t i = 0; i < points_count; i++) {
			pts_f.write[i * 2 + 0] = static_cast<float>(pd.points[i].x);
			pts_f.write[i * 2 + 1] = static_cast<float>(pd.points[i].y);
		}
		bool upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, points_size, pts_f.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
#else
		bool upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, points_size, pd.points.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
#endif
		if (!check_orphan_success(upload_success)) {
			return;
		}

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);

		uint32_t uv_start_offset = offset + points_size + colors_size;

		if (state.using_light) {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			if (state.normal_used && max_units >= 4) {
				if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uvs_size > 0 ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uvs_size > 0 ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uvs_size > 0 ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);
				} else {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uvs_size > 0 ? (const void *)(uintptr_t)uv_start_offset : (const void *)(uintptr_t)offset);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
				}
			} else {
				if (max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				if (max_units >= 4) {
					glClientActiveTexture(GL_TEXTURE0 + 3);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}

				if (max_units >= 2) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
				}
				if (state.using_shadow && max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
				}
			}
			glClientActiveTexture(GL_TEXTURE0);
		} else {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			for (int i = 1; i < max_units; i++) {
				glClientActiveTexture(GL_TEXTURE0 + i);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE0);
		}

		offset += points_size;

		// UVs
		if (uvs_size > 0) {
#ifdef REAL_T_IS_DOUBLE
			Vector<float> uv_f;
			uv_f.resize(pd.uvs.size() * 2);
			for (int i = 0; i < pd.uvs.size(); i++) {
				uv_f.write[i * 2 + 0] = static_cast<float>(pd.uvs[i].x);
				uv_f.write[i * 2 + 1] = static_cast<float>(pd.uvs[i].y);
			}
			upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, uvs_size, uv_f.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
#else
			upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, uvs_size, pd.uvs.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
#endif
			if (!check_orphan_success(upload_success)) {
				return;
			}

			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
			offset += uvs_size;
		} else {
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
		GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_polygon: buffer subdata and pointers");

		// Colors
		if (pd.colors.size() == 1) {
			// Flat colored polygon (e.g., editor backgrounds)
			glDisableClientState(GL_COLOR_ARRAY);
			Color combined_color = pd.colors[0] * state.uniforms.final_modulate;
			glColor4f(combined_color.r, combined_color.g, combined_color.b, combined_color.a);
		} else if (use_vertex_colors) {
			// Vertex colored polygon
			Color *precalced_colors = SAFE_ALLOCA_ARRAY(Color, points_count);

			if (likely(precalced_colors)) {
				int num_colors_specified = MIN((int)pd.colors.size(), (int)points_count);
				Color vcol = pd.colors[0] * state.uniforms.final_modulate;

				for (int n = 0; n < num_colors_specified; n++) {
					precalced_colors[n] = pd.colors[n] * state.uniforms.final_modulate;
				}
				// Pad the missing vertices
				for (int n = num_colors_specified; n < (int)points_count; n++) {
					precalced_colors[n] = vcol;
				}

				upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, colors_size, precalced_colors, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
				if (!check_orphan_success(upload_success)) {
					return;
				}

				glEnableClientState(GL_COLOR_ARRAY);
				glColorPointer(4, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
			}
		} else {
			// Default modulate color
			glDisableClientState(GL_COLOR_ARRAY);
			Color c = state.uniforms.final_modulate;
			glColor4f(c.r, c.g, c.b, c.a);
		}
		GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_polygon: color subdata and pointers");
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);

#ifdef REAL_T_IS_DOUBLE
		Vector<float> pts_f;
		pts_f.resize(points_count * 2);
		for (uint32_t i = 0; i < points_count; i++) {
			pts_f.write[i * 2 + 0] = static_cast<float>(pd.points[i].x);
			pts_f.write[i * 2 + 1] = static_cast<float>(pd.points[i].y);
		}
		const float *pts_ptr = pts_f.ptr();
#else
		const Point2 *pts_ptr = pd.points.ptr();
#endif

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, pts_ptr);

#ifdef REAL_T_IS_DOUBLE
		Vector<float> uv_f;
		const float *uv_ptr = nullptr;
		if (uvs_size > 0) {
			uv_f.resize(pd.uvs.size() * 2);
			for (int i = 0; i < pd.uvs.size(); i++) {
				uv_f.write[i * 2 + 0] = static_cast<float>(pd.uvs[i].x);
				uv_f.write[i * 2 + 1] = static_cast<float>(pd.uvs[i].y);
			}
			uv_ptr = uv_f.ptr();
		}
#else
		const Point2 *uv_ptr = pd.uvs.ptr();
#endif

		if (state.using_light) {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			if (state.normal_used && max_units >= 4) {
				if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : pts_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : pts_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : pts_ptr);
				} else {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, pts_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr ? uv_ptr : pts_ptr);

					glClientActiveTexture(GL_TEXTURE0 + 3);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, pts_ptr);
				}
			} else {
				if (max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				if (max_units >= 4) {
					glClientActiveTexture(GL_TEXTURE0 + 3);
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}

				if (max_units >= 2) {
					glClientActiveTexture(GL_TEXTURE0 + 1);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, pts_ptr);
				}
				if (state.using_shadow && max_units >= 3) {
					glClientActiveTexture(GL_TEXTURE0 + 2);
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, 0, pts_ptr);
				}
			}
			glClientActiveTexture(GL_TEXTURE0);
		} else {
			GLint max_units = GLES1::Config::get_singleton()->max_texture_units;
			for (int i = 1; i < max_units; i++) {
				glClientActiveTexture(GL_TEXTURE0 + i);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE0);
		}

		if (uvs_size > 0) {
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(2, GL_FLOAT, 0, uv_ptr);
		} else {
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}

		if (pd.colors.size() == 1) {
			glDisableClientState(GL_COLOR_ARRAY);
			Color combined_color = pd.colors[0] * state.uniforms.final_modulate;
			glColor4f(combined_color.r, combined_color.g, combined_color.b, combined_color.a);
		} else if (use_vertex_colors) {
			Color *precalced_colors = SAFE_ALLOCA_ARRAY(Color, points_count);

			if (likely(precalced_colors)) {
				int num_colors_specified = MIN((int)pd.colors.size(), (int)points_count);
				Color vcol = pd.colors[0] * state.uniforms.final_modulate;

				for (int n = 0; n < num_colors_specified; n++) {
					precalced_colors[n] = pd.colors[n] * state.uniforms.final_modulate;
				}
				for (int n = num_colors_specified; n < (int)points_count; n++) {
					precalced_colors[n] = vcol;
				}

				glEnableClientState(GL_COLOR_ARRAY);
				glColorPointer(4, GL_FLOAT, 0, precalced_colors);
			}
		} else {
			glDisableClientState(GL_COLOR_ARRAY);
			Color c = state.uniforms.final_modulate;
			glColor4f(c.r, c.g, c.b, c.a);
		}
		GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_polygon: color subdata and pointers");
	}

	// Map primitive type
	GLenum gl_primitive = get_gl_primitive_type(p_poly->primitive);

	// Draw
	if (!pd.indices.is_empty()) {
		int index_count = pd.indices.size();

		// GLES1 strictly requires 16-bit indices.
		if (unlikely(pd.points.size() > 65535)) {
			ERR_PRINT_ONCE("GLES1: Cannot draw polygon with more than 65535 vertices due to 16-bit index hardware limits.");
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			return;
		}

		Vector<uint16_t> indices_16;
		indices_16.resize(index_count);

		for (int i = 0; i < index_count; i++) {
			indices_16.write[i] = (uint16_t)pd.indices[i];
		}

		if (use_vbo) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer);

			uint32_t index_size = index_count * sizeof(uint16_t);

			if (index_size > data.polygon_index_buffer_size) {
				data.polygon_index_buffer_size = next_power_of_2(index_size);
			}

			bool upload_success = _buffer_orphan_and_upload(data.polygon_index_buffer_size, 0, index_size, indices_16.ptr(), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
			if (!check_orphan_success(upload_success)) {
				return;
			}

			glDrawElements(gl_primitive, index_count, GL_UNSIGNED_SHORT, nullptr);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		} else {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glDrawElements(gl_primitive, index_count, GL_UNSIGNED_SHORT, indices_16.ptr());
		}
	} else {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		// Draw arrays if no indices
		// Godot passes PRIMITIVE_TRIANGLES, but un-indexed polygons must be fanned.
		if (gl_primitive == GL_TRIANGLES) {
			glDrawArrays(GL_TRIANGLE_FAN, 0, pd.points.size());
			GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_polygon: glDrawArrays (GL_TRIANGLE_FAN)");
		} else {
			glDrawArrays(gl_primitive, 0, pd.points.size());
			GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_polygon: glDrawArrays (Primitive)");
		}
	}

	// Cleanup
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_TEXTURE_2D);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES1::Canvas::_legacy_draw_polygon: cleanup");
}

void RasterizerCanvasGLES1::_set_texture_rect_mode(bool p_texture_rect, bool p_light_angle, bool p_modulate, bool p_large_vertex) {
	// Switch the shader variant between Quad and Texture Rect
	state.mode_variant = p_texture_rect ? CanvasShaderGLES1::ShaderVariant::MODE_TEXTURE_RECT : CanvasShaderGLES1::ShaderVariant::MODE_QUAD;

	if (!p_texture_rect) {
		// Reset the texture matrix
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}

	uint64_t spec = 0;
	if (p_light_angle) {
		spec |= CanvasShaderGLES1::USE_ATTRIB_LIGHT_ANGLE;
	}
	if (p_modulate) {
		spec |= CanvasShaderGLES1::USE_ATTRIB_MODULATE;
	}
	if (p_large_vertex) {
		spec |= CanvasShaderGLES1::USE_ATTRIB_LARGE_VERTEX;
	}
	if (state.using_skeleton) {
		spec |= CanvasShaderGLES1::USE_SKELETON;
	}

	if (state.using_light) {
		spec |= CanvasShaderGLES1::USE_LIGHTING;

		if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
			state.specialization |= CanvasShaderGLES1::USE_DIRECTIONAL_LIGHT;
		}
	}

	state.specialization = spec;
}

RendererCanvasRender::PolygonID RasterizerCanvasGLES1::request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, const Vector<int> &p_bones, const Vector<float> &p_weights) {
	PolygonID id = next_polygon_id++;
	PolyData pd;
	pd.indices = p_indices;
	pd.points = p_points;
	pd.colors = p_colors;
	pd.uvs = p_uvs;

	polygon_cache[id] = pd;
	return id;
}

void RasterizerCanvasGLES1::free_polygon(PolygonID p_polygon) {
	polygon_cache.erase(p_polygon);
}

void RasterizerCanvasGLES1::set_time(double p_time) {
	state.time = p_time;
}

bool RasterizerCanvasGLES1::is_context_lost() const {
	if (data.canvas_quad_vertices != 0) {
		// Honest drivers will flag this immediately.
		if (glIsBuffer(data.canvas_quad_vertices) == GL_FALSE) {
			return true;
		}

		// Liar drivers lie about glIsBuffer after silent context loss.
		// We perform a single deliberate bind and check for GL_INVALID_OPERATION.
		// Since this only runs at frame/canvas start,
		// the pipeline sync cost is negligible.
		while (glGetError() != GL_NO_ERROR); // Clear stale errors

		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
		bool is_zombie = (glGetError() == GL_INVALID_OPERATION);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		return is_zombie;
	}

	// If it's 0, it hasn't been initialized yet.
	return false;
}

void RasterizerCanvasGLES1::force_context_recovery() {
	// Wipe dead handles to 0 so glGenBuffers doesn't complain,
	// then re-initialize the core VBOs.
	data.canvas_quad_vertices = 0;
	data.polygon_buffer = 0;
	data.polygon_index_buffer = 0;
	data.ninepatch_vertices = 0;
	data.ninepatch_elements = 0;

	// Sterilize the batching buffers
	bdata.gl_vertex_buffer = 0;
	bdata.gl_index_buffer = 0;

	initialize();
	reset_canvas();

	// Broadcast the death of the context globally.
	GLES1::Config::get_singleton()->context_generation++;
}

RasterizerCanvasGLES1 *RasterizerCanvasGLES1::singleton = nullptr;

RasterizerCanvasGLES1 *RasterizerCanvasGLES1::get_singleton() {
	return singleton;
}

RasterizerCanvasGLES1::RasterizerCanvasGLES1() {
	singleton = this;

	batch_constructor();

	initialize();

	state.canvas_shader = &GLES1::MaterialStorage::get_singleton()->shaders.canvas_shader;
	data.canvas_shader_default_version = state.canvas_shader->default_version;

	// Allocate the default white texture for untextured polygons/rects
	default_canvas_texture = GLES1::TextureStorage::get_singleton()->canvas_texture_allocate();
	GLES1::TextureStorage::get_singleton()->canvas_texture_initialize(default_canvas_texture);
}

RasterizerCanvasGLES1::~RasterizerCanvasGLES1() {
	singleton = nullptr;

	GLES1::TextureStorage::get_singleton()->canvas_texture_free(default_canvas_texture);

	// Shaders
	if (shadow_render.shader_version.is_valid()) {
		shadow_render.shader.version_free(shadow_render.shader_version);
	}

	// Free buffers
	GLES1::Utilities::get_singleton()->buffer_free_data(data.canvas_quad_vertices);
	GLES1::Utilities::get_singleton()->buffer_free_data(data.ninepatch_vertices);
	GLES1::Utilities::get_singleton()->buffer_free_data(data.ninepatch_elements);
	GLES1::Utilities::get_singleton()->buffer_free_data(data.polygon_buffer);
	GLES1::Utilities::get_singleton()->buffer_free_data(data.polygon_index_buffer);

	// Free batcher buffers
	if (bdata.gl_vertex_buffer != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(bdata.gl_vertex_buffer);
	}
	if (bdata.gl_index_buffer != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(bdata.gl_index_buffer);
	}
}

#endif // GLES1_ENABLED
