/**************************************************************************/
/*  rasterizer_canvas_gles2.cpp                                           */
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

#include "rasterizer_canvas_gles2.h"

#ifdef GLES2_ENABLED

#include "rasterizer_gles2.h"
#include "rasterizer_scene_gles2.h"

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

void RasterizerCanvasGLES2::_update_transform_2d_to_mat4(const Transform2D &p_transform, float *p_mat4) {
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

void RasterizerCanvasGLES2::_update_transform_2d_to_mat2x4(const Transform2D &p_transform, float *p_mat2x4) {
	p_mat2x4[0] = p_transform.columns[0][0];
	p_mat2x4[1] = p_transform.columns[1][0];
	p_mat2x4[2] = 0;
	p_mat2x4[3] = p_transform.columns[2][0];

	p_mat2x4[4] = p_transform.columns[0][1];
	p_mat2x4[5] = p_transform.columns[1][1];
	p_mat2x4[6] = 0;
	p_mat2x4[7] = p_transform.columns[2][1];
}

void RasterizerCanvasGLES2::_update_transform_2d_to_mat2x3(const Transform2D &p_transform, float *p_mat2x3) {
	p_mat2x3[0] = p_transform.columns[0][0];
	p_mat2x3[1] = p_transform.columns[0][1];
	p_mat2x3[2] = p_transform.columns[1][0];
	p_mat2x3[3] = p_transform.columns[1][1];
	p_mat2x3[4] = p_transform.columns[2][0];
	p_mat2x3[5] = p_transform.columns[2][1];
}

void RasterizerCanvasGLES2::_update_transform_to_mat4(const Transform3D &p_transform, float *p_mat4) {
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

RID RasterizerCanvasGLES2::light_create() {
	CanvasLight canvas_light;
	return canvas_light_owner.make_rid(canvas_light);
}

void RasterizerCanvasGLES2::light_set_texture(RID p_rid, RID p_texture) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);
	if (cl->texture == p_texture) {
		return;
	}

	cl->texture = p_texture;
}

void RasterizerCanvasGLES2::light_set_use_shadow(RID p_rid, bool p_enable) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(cl);

	cl->shadow.enabled = p_enable;
}

void RasterizerCanvasGLES2::light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_COND(!cl || !cl->shadow.enabled);

	if (!state.shadow_fb) {
		glGenFramebuffers(1, &state.shadow_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, state.shadow_fb);

		glGenTextures(1, &state.shadow_texture);
		glBindTexture(GL_TEXTURE_2D, state.shadow_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.shadow_texture_size, 2048, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.shadow_texture, 0);

		glGenRenderbuffers(1, &state.shadow_depth_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, state.shadow_depth_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, state.shadow_texture_size, 2048);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, state.shadow_depth_buffer);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			ERR_PRINT("GLES2: Canvas shadow framebuffer not complete!");
		}
		GL_CHECK_ERROR("GLES2::Canvas::light_update_shadow: init fbo");
	}

	cl->shadow.z_far = p_far;
	cl->shadow.y_offset = float(p_shadow_index * 2 + 1) / 2048.0f; // Locked to the 2048 physical height

	GLES2::TextureStorage::get_singleton()->bind_framebuffer(state.shadow_fb);

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);

	glEnable(GL_SCISSOR_TEST);
	glScissor(0, p_shadow_index * 2, state.shadow_texture_size, 2);

	// Pure white pushes the un-occluded depth distance to 1.0
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	RasterizerGLES2::clear_depth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);

	CanvasOcclusionShaderGLES2::ShaderVariant variant = CanvasOcclusionShaderGLES2::MODE_SHADOW_RGBA;
	bool success = shadow_render.shader.version_bind_shader(shadow_render.shader_version, variant);
	if (!success) {
		GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		return;
	}

	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::Z_FAR, p_far, shadow_render.shader_version, variant, 0);

	Projection projection;
	{
		constexpr real_t FOV = 90;
		real_t nearp = p_near;
		real_t farp = p_far;
		constexpr real_t ASPECT = 1.0;

		real_t ymax = nearp * Math::tan(Math::deg_to_rad(FOV * 0.5));
		real_t ymin = -ymax;
		real_t xmin = ymin * ASPECT;
		real_t xmax = ymax * ASPECT;

		projection.set_frustum(xmin, xmax, ymin, ymax, nearp, farp);
	}

	Projection projections[4] = {
		projection * Projection(Vector4(0, 0, -1, 0), Vector4(1, 0, 0, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),
		projection * Projection(Vector4(-1, 0, 0, 0), Vector4(0, 0, -1, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),
		projection * Projection(Vector4(0, 0, 1, 0), Vector4(-1, 0, 0, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1)),
		projection * Projection(Vector4(1, 0, 0, 0), Vector4(0, 0, 1, 0), Vector4(0, -1, 0, 0), Vector4(0, 0, 0, 1))
	};
	static Vector2 directions[4] = {
		Vector2(1, 0), Vector2(0, 1),
		Vector2(-1, 0), Vector2(0, -1)
	};

	for (int i = 0; i < 4; i++) {
		int viewport_width = state.shadow_texture_size / 4;
		glViewport(viewport_width * i, p_shadow_index * 2, viewport_width, 2);

		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::PROJECTION, projections[i], shadow_render.shader_version, variant, 0);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::DIRECTION, directions[i], shadow_render.shader_version, variant, 0);

		LightOccluderInstance *instance = p_occluders;
		while (instance) {
			if (instance->light_mask & p_light_mask) {
				OccluderPolygon *occluder = occluder_polygon_owner.get_or_null(instance->occluder);
				if (occluder && occluder->line_point_count > 0 && occluder->vertex_array != 0) {
					if (unlikely(occluder->context_generation != GLES2::Config::get_singleton()->context_generation)) {
						instance = instance->next;
						continue; // Zombie VBO, skip drawing
					}

					if (occluder->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
						glDisable(GL_CULL_FACE);
					} else {
						glEnable(GL_CULL_FACE);
						glCullFace((occluder->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_CLOCKWISE) ? GL_FRONT : GL_BACK);
					}

					// Godot 4 Space calculation
					Transform2D modelview = p_light_xform * instance->xform_cache;

					Color mv1(modelview.columns[0][0], modelview.columns[1][0], 0, modelview.columns[2][0]);
					Color mv2(modelview.columns[0][1], modelview.columns[1][1], 0, modelview.columns[2][1]);
					shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::MODELVIEW1, mv1, shadow_render.shader_version, variant, 0);
					shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::MODELVIEW2, mv2, shadow_render.shader_version, variant, 0);

					glBindBuffer(GL_ARRAY_BUFFER, occluder->vertex_buffer);
					glEnableVertexAttribArray(RS::ARRAY_VERTEX);
					glVertexAttribPointer(RS::ARRAY_VERTEX, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, occluder->index_buffer);

					glDrawElements(GL_TRIANGLES, occluder->line_point_count, GL_UNSIGNED_SHORT, nullptr);
					GL_CHECK_ERROR("GLES2::Canvas::light_update_shadow: glDrawElements");
				}
			}
			instance = instance->next;
		}
	}

	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glCullFace(GL_BACK);

	GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
}

void RasterizerCanvasGLES2::light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) {
	CanvasLight *cl = canvas_light_owner.get_or_null(p_rid);
	ERR_FAIL_COND(!cl || !cl->shadow.enabled);

	if (!state.shadow_fb) {
		glGenFramebuffers(1, &state.shadow_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, state.shadow_fb);

		glGenTextures(1, &state.shadow_texture);
		glBindTexture(GL_TEXTURE_2D, state.shadow_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.shadow_texture_size, 2048, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.shadow_texture, 0);

		glGenRenderbuffers(1, &state.shadow_depth_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, state.shadow_depth_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, state.shadow_texture_size, 2048);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, state.shadow_depth_buffer);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			ERR_PRINT("GLES2: Canvas shadow framebuffer not complete!");
		}
		GL_CHECK_ERROR("GLES2::Canvas::light_update_directional_shadow: init fbo");
	}

	Vector2 light_dir = p_light_xform.columns[1].normalized();
	Vector2 center = p_clip_rect.get_center();

	float to_edge_distance = ABS(light_dir.dot(p_clip_rect.get_support(-light_dir)) - light_dir.dot(center));
	Vector2 from_pos = center - light_dir * (to_edge_distance + p_cull_distance);
	float distance = to_edge_distance * 2.0f + p_cull_distance;
	float half_size = p_clip_rect.size.length() * 0.5f;

	cl->shadow.z_far = distance;
	cl->shadow.y_offset = float(p_shadow_index * 2 + 1) / 2048.0f; // Scale against our fixed 2048 texture height

	Transform2D to_light_xform;
	to_light_xform.columns[2] = from_pos;
	to_light_xform.columns[1] = light_dir;
	to_light_xform.columns[0] = -light_dir.orthogonal();
	to_light_xform.invert();

	GLES2::TextureStorage::get_singleton()->bind_framebuffer(state.shadow_fb);
	glViewport(0, p_shadow_index * 2, state.shadow_texture_size, 2);

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);

	glEnable(GL_SCISSOR_TEST);
	glScissor(0, p_shadow_index * 2, state.shadow_texture_size, 2);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f); // 1.0 pushes the clear distance to Z_FAR

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	RasterizerGLES2::clear_depth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);
	RS::CanvasOccluderPolygonCullMode cull_mode = RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED;

	CanvasOcclusionShaderGLES2::ShaderVariant variant = CanvasOcclusionShaderGLES2::MODE_SHADOW_RGBA;
	bool success = shadow_render.shader.version_bind_shader(shadow_render.shader_version, variant);
	if (!success) {
		GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		return;
	}

	Projection projection;
	projection.set_orthogonal(-half_size, half_size, -0.5f, 0.5f, 0.0f, distance);
	projection = projection * Projection(Transform3D().looking_at(Vector3(0, 1, 0), Vector3(0, 0, -1)).affine_inverse());

	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::PROJECTION, projection, shadow_render.shader_version, variant, 0);
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::DIRECTION, Vector2(0.0f, 1.0f), shadow_render.shader_version, variant, 0);
	shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::Z_FAR, distance, shadow_render.shader_version, variant, 0);

	LightOccluderInstance *instance = p_occluders;

	while (instance) {
		OccluderPolygon *occluder = occluder_polygon_owner.get_or_null(instance->occluder);

		if (!occluder || occluder->vertex_array == 0 || !(p_light_mask & instance->light_mask)) {
			instance = instance->next;
			continue;
		}

		if (unlikely(occluder->context_generation != GLES2::Config::get_singleton()->context_generation)) {
			instance = instance->next;
			continue; // Zombie VBO, skip drawing
		}

		Transform2D modelview = to_light_xform * instance->xform_cache;

		Color mv1(modelview.columns[0][0], modelview.columns[1][0], 0, modelview.columns[2][0]);
		Color mv2(modelview.columns[0][1], modelview.columns[1][1], 0, modelview.columns[2][1]);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::MODELVIEW1, mv1, shadow_render.shader_version, variant, 0);
		shadow_render.shader.version_set_uniform(CanvasOcclusionShaderGLES2::MODELVIEW2, mv2, shadow_render.shader_version, variant, 0);

		if (occluder->cull_mode != cull_mode) {
			if (occluder->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
				glDisable(GL_CULL_FACE);
			} else {
				if (cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED) {
					glEnable(GL_CULL_FACE);
				}
				glCullFace(occluder->cull_mode == RS::CANVAS_OCCLUDER_POLYGON_CULL_CLOCKWISE ? GL_FRONT : GL_BACK);
			}
			cull_mode = occluder->cull_mode;
		}

		glBindBuffer(GL_ARRAY_BUFFER, occluder->vertex_buffer);
		glEnableVertexAttribArray(RS::ARRAY_VERTEX);
		glVertexAttribPointer(RS::ARRAY_VERTEX, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, occluder->index_buffer);
		glDrawElements(GL_TRIANGLES, occluder->line_point_count, GL_UNSIGNED_SHORT, nullptr);
		GL_CHECK_ERROR("GLES2::Canvas::light_update_directional_shadow: glDrawElements");

		instance = instance->next;
	}

	Transform2D to_shadow;
	to_shadow.columns[0].x = 1.0f / -(half_size * 2.0f);
	to_shadow.columns[2].x = 0.5f;

	cl->shadow.directional_xform = to_shadow * to_light_xform;

	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glCullFace(GL_BACK);

	GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
}

void RasterizerCanvasGLES2::render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) {

}

RID RasterizerCanvasGLES2::occluder_polygon_create() {
	OccluderPolygon occluder;
	return occluder_polygon_owner.make_rid(occluder);
}

void RasterizerCanvasGLES2::occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) {
	OccluderPolygon *oc = occluder_polygon_owner.get_or_null(p_occluder);
	ERR_FAIL_NULL(oc);

	int point_count = p_points.size();
	if (point_count < 2) {
		oc->line_point_count = 0;
		return;
	}

	uint32_t current_gen = GLES2::Config::get_singleton()->context_generation;
	if (unlikely(oc->context_generation != current_gen)) {
		oc->vertex_buffer = 0;
		oc->index_buffer = 0;
		oc->context_generation = current_gen;
	}

	int line_count = p_closed ? point_count : point_count - 1;

	Vector<Vector3> geometry;
	geometry.resize(line_count * 4); // 4 verts forming a quad per line
	Vector<uint16_t> indices;
	indices.resize(line_count * 6); // 6 indices (2 triangles) per line

	constexpr float POLY_HEIGHT = 16384.0f; // Godot 4 shadow extrusion constant

	for (int i = 0; i < line_count; i++) {
		Vector2 a = p_points[i];
		Vector2 b = p_points[(i + 1) % point_count];

		int v_ofs = i * 4;
		geometry.write[v_ofs + 0] = Vector3(a.x, a.y, POLY_HEIGHT);
		geometry.write[v_ofs + 1] = Vector3(b.x, b.y, POLY_HEIGHT);
		geometry.write[v_ofs + 2] = Vector3(b.x, b.y, -POLY_HEIGHT);
		geometry.write[v_ofs + 3] = Vector3(a.x, a.y, -POLY_HEIGHT);

		int i_ofs = i * 6;
		indices.write[i_ofs + 0] = v_ofs + 0;
		indices.write[i_ofs + 1] = v_ofs + 1;
		indices.write[i_ofs + 2] = v_ofs + 2;
		indices.write[i_ofs + 3] = v_ofs + 2;
		indices.write[i_ofs + 4] = v_ofs + 3;
		indices.write[i_ofs + 5] = v_ofs + 0;
	}

	oc->line_point_count = line_count * 6;

	if (oc->vertex_buffer == 0) {
		glGenBuffers(1, &oc->vertex_buffer);
	}
	if (oc->index_buffer == 0) {
		glGenBuffers(1, &oc->index_buffer);
	}

	glBindBuffer(GL_ARRAY_BUFFER, oc->vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, geometry.size() * sizeof(Vector3), geometry.ptr(), GL_STATIC_DRAW);
	GL_CHECK_ERROR("GLES2::Canvas::occluder_polygon_set_shape: VBO upload");

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oc->index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.ptr(), GL_STATIC_DRAW);
	GL_CHECK_ERROR("GLES2::Canvas::occluder_polygon_set_shape: IBO upload");

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	oc->vertex_array = 1; // Mark as valid
}

void RasterizerCanvasGLES2::occluder_polygon_set_cull_mode(RID p_occluder, RS::CanvasOccluderPolygonCullMode p_mode) {
	OccluderPolygon *oc = occluder_polygon_owner.get_or_null(p_occluder);
	ERR_FAIL_NULL(oc);
	oc->cull_mode = p_mode;
}

void RasterizerCanvasGLES2::set_shadow_texture_size(int p_size) {
	p_size = next_power_of_2(p_size);
	if (p_size == state.shadow_texture_size) {
		return;
	}

	state.shadow_texture_size = p_size;

	if (state.shadow_fb != 0) {
		glDeleteFramebuffers(1, &state.shadow_fb);
		state.shadow_fb = 0;
	}
	if (state.shadow_texture != 0) {
		glDeleteTextures(1, &state.shadow_texture);
		state.shadow_texture = 0;
	}
	if (state.shadow_depth_buffer != 0) {
		glDeleteRenderbuffers(1, &state.shadow_depth_buffer);
		state.shadow_depth_buffer = 0;
	}
	GL_CHECK_ERROR("GLES2::Canvas::set_shadow_texture_size: cleanup old buffers");
}

bool RasterizerCanvasGLES2::free(RID p_rid) {
	return true;
}

void RasterizerCanvasGLES2::initialize() {
	print_verbose("GLES2: Initializing Canvas Renderer");

	// Quad buffer
	{
		glGenBuffers(1, &data.canvas_quad_vertices);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers quad");
		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);

		const constexpr float qv[8] = {
			0, 0,
			0, 1,
			1, 1,
			1, 0
		};

		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, data.canvas_quad_vertices, sizeof(float) * 8, qv, GL_STATIC_DRAW, "Canvas Quad");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data quad");
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	// Polygon buffer
	{
		uint32_t poly_size = GLOBAL_DEF("rendering/limits/buffers/canvas_polygon_buffer_size_kb", 128);
		poly_size = MAX(poly_size, (uint32_t)128); // minimum 128k
		poly_size *= 1024;

		glGenBuffers(1, &data.polygon_buffer);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers poly");
		glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, data.polygon_buffer, poly_size, nullptr, GL_DYNAMIC_DRAW, "Canvas Polygon Buffer");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data poly");
		data.polygon_buffer_size = poly_size;
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		uint32_t index_size = GLOBAL_DEF("rendering/limits/buffers/canvas_polygon_index_buffer_size_kb", 128);
		index_size = MAX(index_size, (uint32_t)128);
		index_size *= 1024;

		glGenBuffers(1, &data.polygon_index_buffer);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers poly index");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer, index_size, nullptr, GL_DYNAMIC_DRAW, "Canvas Polygon Index Buffer");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data poly index");
		data.polygon_index_buffer_size = index_size;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	// Ninepatch buffers
	{
		glGenBuffers(1, &data.ninepatch_vertices);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers ninepatch");
		glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, data.ninepatch_vertices, sizeof(float) * (16 + 16) * 2, nullptr, GL_DYNAMIC_DRAW, "Canvas Ninepatch Vertices");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data ninepatch");
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glGenBuffers(1, &data.ninepatch_elements);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers ninepatch index");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);

#define _EIDX(y, x) (y * 4 + x)
		constexpr uint8_t elems[3 * 2 * 9] = {
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

		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements, sizeof(elems), elems, GL_STATIC_DRAW, "Canvas Ninepatch Elements");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data ninepatch index");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	_set_texture_rect_mode(true);

	state.using_light = nullptr;
	state.using_skeleton = false;

	// Don't just assume maxium attributes for the engine
	// and instead query from the hardware.
	GLint max_attribs = 8;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_attribs);
	GL_CHECK_ERROR("GLES2::Canvas::initialize: glGetIntegerv GL_MAX_VERTEX_ATTRIBS");
	maximum_attributes = MIN((int)RS::ARRAY_MAX, max_attribs);

	// Batcher Initialisation
	batch_initialize();

	if (bdata.vertex_buffer_size_bytes) {
		// Reserve space for dynamic batched vertices
		glGenBuffers(1, &bdata.gl_vertex_buffer);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers batcher");
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer, bdata.vertex_buffer_size_bytes, nullptr, GL_DYNAMIC_DRAW, "Canvas Batcher Vertices");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data batcher");
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// Pre-fill index buffer.
		// The indices never need to change so they are static.
		glGenBuffers(1, &bdata.gl_index_buffer);
		GL_CHECK_ERROR("GLES2::Canvas::initialize: glGenBuffers batcher index");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);

		Vector<uint16_t> indices;
		indices.resize(bdata.index_buffer_size_units);

		for (unsigned int q = 0; q < bdata.max_quads; q++) {
			int i_pos = q * 6; // 6 inds per quad
			int q_pos = q * 4; // 4 verts per quad
			indices.write[i_pos] = q_pos;
			indices.write[i_pos + 1] = q_pos + 1;
			indices.write[i_pos + 2] = q_pos + 2;
			indices.write[i_pos + 3] = q_pos;
			indices.write[i_pos + 4] = q_pos + 2;
			indices.write[i_pos + 5] = q_pos + 3;

			// GLES2 restricts us to 16-bit indices!
#ifdef DEBUG_ENABLED
			CRASH_COND_MSG((q_pos + 3) > 65535, "GLES2 Canvas Batcher: Too many vertices for 16-bit indices!");
#endif
		}

		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer, bdata.index_buffer_size_bytes, indices.ptr(), GL_STATIC_DRAW, "Canvas Batcher Indices");
		GL_CHECK_ERROR("GLES2::Canvas::initialize: buffer_allocate_data batcher index");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	shadow_render.shader.initialize();
	shadow_render.shader_version = shadow_render.shader.version_create();
}

void RasterizerCanvasGLES2::update() {

}

void RasterizerCanvasGLES2::_bind_canvas_texture(RID p_texture, RS::CanvasItemTextureFilter p_base_filter, RS::CanvasItemTextureRepeat p_base_repeat) {
	if (p_texture.is_null()) {
		// Fallback to default white if passed null
		p_texture = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
	}

	// Guarantee GL_TEXTURE0 is active before validating the cache
	glActiveTexture(GL_TEXTURE0);

	// Cache Check (Prevents redundant state changes)
	if (state.current_tex == p_texture && state.current_filter == p_base_filter && state.current_repeat == p_base_repeat) {
		return;
	}

	// Resolve CanvasTexture
	GLES2::CanvasTexture *ct = nullptr;
	GLES2::Texture *t = GLES2::TextureStorage::get_singleton()->get_texture(p_texture);

	if (t) {
		// If it's a raw texture, it might have an embedded CanvasTexture
		if (!t->canvas_texture) {
			t->canvas_texture = memnew(GLES2::CanvasTexture);
			ERR_FAIL_NULL(t->canvas_texture);
			t->canvas_texture->diffuse = p_texture;
		}

		ct = t->canvas_texture;
		if (t->render_target) {
			t->render_target->used_in_frame = true;
		}
	} else {
		// It's a genuine CanvasTexture RID
		ct = GLES2::TextureStorage::get_singleton()->get_canvas_texture(p_texture);
	}

	if (!ct) {
		// Completely invalid texture, bind white safely
		RID white_tex = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
		_bind_canvas_texture(white_tex, p_base_filter, p_base_repeat);
		return;
	}

	state.current_tex_is_rt = false;

	// Resolve overrides from CanvasTexture
	RS::CanvasItemTextureFilter filter = ct->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? ct->texture_filter : p_base_filter;
	RS::CanvasItemTextureRepeat repeat = ct->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? ct->texture_repeat : p_base_repeat;

	// Bind Diffuse (Texture Unit 0)
	glActiveTexture(GL_TEXTURE0);
	GL_CHECK_ERROR("GLES2::Canvas::_bind_canvas_texture: glActiveTexture 0");
	GLES2::Texture *texture = GLES2::TextureStorage::get_singleton()->get_texture(ct->diffuse);

	if (!texture || texture->tex_id == 0 || texture->width == 0 || texture->height == 0) {
		// Something went very wrong if we're here.
		GLES2::Texture *tex_2d_bg = GLES2::TextureStorage::get_singleton()->get_texture(
			GLES2::TextureStorage::get_singleton()->texture_gl_get_default(
				GLES2::DEFAULT_GL_TEXTURE_WHITE
			)
		);
		if (tex_2d_bg) {
			glBindTexture(GL_TEXTURE_2D, tex_2d_bg->tex_id);

			// Clamp fallback state
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			state.texpixel_size = Size2(1.0 / tex_2d_bg->width, 1.0 / tex_2d_bg->height);
			state.texture_size = Size2i(tex_2d_bg->width, tex_2d_bg->height);
		}
	} else {
		glBindTexture(GL_TEXTURE_2D, texture->tex_id);
		GL_CHECK_ERROR("GLES2::Canvas::_bind_canvas_texture: glBindTexture GL_TEXTURE_2D");

		// Setup Filters
		GLenum min_filter = GL_LINEAR;
		GLenum mag_filter = GL_LINEAR;
		GLenum wrap_s = GL_CLAMP_TO_EDGE;
		GLenum wrap_t = GL_CLAMP_TO_EDGE;

		switch (filter) {
			case RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST:
				min_filter = GL_NEAREST;
				mag_filter = GL_NEAREST;
				break;
			case RS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR:
				min_filter = GL_LINEAR;
				mag_filter = GL_LINEAR;
				break;
			case RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS:
			case RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC:
				min_filter = texture->mipmaps > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
				mag_filter = GL_NEAREST;
				break;
			case RS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS:
			case RS::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC:
				min_filter = texture->mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
				mag_filter = GL_LINEAR;
				break;
			default:
				break;
		}

		switch (repeat) {
			case RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED:
				wrap_s = GL_CLAMP_TO_EDGE;
				wrap_t = GL_CLAMP_TO_EDGE;
				break;
			case RS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED:
				wrap_s = GL_REPEAT;
				wrap_t = GL_REPEAT;
				break;
			case RS::CANVAS_ITEM_TEXTURE_REPEAT_MIRROR:
				wrap_s = GL_MIRRORED_REPEAT;
				wrap_t = GL_MIRRORED_REPEAT;
				break;
			default:
				break;
		}

		bool is_npot = (
			texture->alloc_width & (texture->alloc_width - 1)
		) != 0 || (texture->alloc_height & (texture->alloc_height - 1)) != 0;
		if (is_npot && !GLES2::Config::get_singleton()->support_npot_repeat_mipmap) {
			wrap_s = GL_CLAMP_TO_EDGE;
			wrap_t = GL_CLAMP_TO_EDGE;

			if (min_filter == GL_NEAREST_MIPMAP_NEAREST || min_filter == GL_NEAREST_MIPMAP_LINEAR) {
				min_filter = GL_NEAREST;
			} else if (min_filter == GL_LINEAR_MIPMAP_NEAREST || min_filter == GL_LINEAR_MIPMAP_LINEAR) {
				min_filter = GL_LINEAR;
			}
		}

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
		GL_CHECK_ERROR("GLES2::Canvas::_bind_canvas_texture: glTexParameteri");

		if (texture->render_target) {
			texture->render_target->used_in_frame = true;
			state.current_tex_is_rt = true;
		}

		// Safe division to prevent black screen NaNs if width is uninitialized
		float w = texture->width > 0 ? texture->width : 1024.0f;
		float h = texture->height > 0 ? texture->height : 1024.0f;
		state.texpixel_size = Size2(1.0 / w, 1.0 / h);
		state.texture_size = Size2i(w, h);
	}

	GLint max_units = GLES2::Config::get_singleton()->max_texture_image_units;

	// Normal Map
	glActiveTexture(GL_TEXTURE0 + max_units - 2);
	GL_CHECK_ERROR("GLES2::Canvas::_bind_canvas_texture: glActiveTexture normal map");
	GLES2::Texture *normal_map = GLES2::TextureStorage::get_singleton()->get_texture(ct->normal_map);
	if (!normal_map) {
		GLES2::Texture *tex_norm = GLES2::TextureStorage::get_singleton()->get_texture(GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_NORMAL));
		if (tex_norm) {
			glBindTexture(GL_TEXTURE_2D, tex_norm->tex_id);
		} else {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		state.normal_used = false;
	} else {
		glBindTexture(GL_TEXTURE_2D, normal_map->tex_id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		state.normal_used = true;
		state.current_tex_is_rt = state.current_tex_is_rt || normal_map->render_target;
	}

	// Specular Map
	glActiveTexture(GL_TEXTURE0 + max_units - 1);
	GL_CHECK_ERROR("GLES2::Canvas::_bind_canvas_texture: glActiveTexture specular map");
	GLES2::Texture *specular_map = GLES2::TextureStorage::get_singleton()->get_texture(ct->specular);
	if (!specular_map) {
		GLES2::Texture *tex_spec = GLES2::TextureStorage::get_singleton()->get_texture(GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE));
		if (tex_spec) {
			glBindTexture(GL_TEXTURE_2D, tex_spec->tex_id);
		} else {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	} else {
		glBindTexture(GL_TEXTURE_2D, specular_map->tex_id);
		state.current_tex_is_rt = state.current_tex_is_rt || specular_map->render_target;
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

void RasterizerCanvasGLES2::_set_canvas_uniforms() {
	if (state.shader_version.is_null()) {
		return;
	}

	// These rely on the generated canvas.glsl.gen.h Uniforms enum and version_set_uniform
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::PROJECTION_MATRIX, state.uniforms.projection_matrix, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::MODELVIEW_MATRIX, state.uniforms.modelview_matrix, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::EXTRA_MATRIX, state.uniforms.extra_matrix, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::FINAL_MODULATE, state.uniforms.final_modulate, state.shader_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::TIME, state.time, state.shader_version, state.mode_variant, state.specialization);

	if (state.using_light) {
		RendererCanvasRender::Light *light = state.using_light;
		if (!light) {
			return;
		}

		Color light_color = light->color * light->energy;
		state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_COLOR, light_color, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_OUTSIDE_ALPHA, 0.0f, state.shader_version, state.mode_variant, state.specialization);

		if (light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
			// =====================================
			// Directional light uniforms
			// =====================================
			Vector2 light_dir = light->xform_cache.columns[1].normalized();
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_POS, -light_dir, state.shader_version, state.mode_variant, state.specialization);

			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_HEIGHT, light->height, state.shader_version, state.mode_variant, state.specialization);

			// Dummy matrices
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_MATRIX, Transform2D(), state.shader_version, state.mode_variant, state.specialization);
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_MATRIX_INVERSE, Transform2D(), state.shader_version, state.mode_variant, state.specialization);
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_LOCAL_MATRIX, Transform2D(), state.shader_version, state.mode_variant, state.specialization);

			if (state.using_shadow) {
				CanvasLight *cl = canvas_light_owner.get_or_null(light->light_internal);
				if (cl) {
					state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_MATRIX, cl->shadow.directional_xform, state.shader_version, state.mode_variant, state.specialization);
					state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_Y_OFS, cl->shadow.y_offset, state.shader_version, state.mode_variant, state.specialization);
					state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_ZFAR_INV, 1.0f / cl->shadow.z_far, state.shader_version, state.mode_variant, state.specialization);
				}

				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_SHADOW_COLOR, light->shadow_color, state.shader_version, state.mode_variant, state.specialization);

				float shadow_pixel_size = (1.0f / state.shadow_texture_size) * (1.0f + light->shadow_smooth);
				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOWPIXEL_SIZE, shadow_pixel_size, state.shader_version, state.mode_variant, state.specialization);
				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_GRADIENT, light->shadow_smooth, state.shader_version, state.mode_variant, state.specialization);
			}
		} else {
			// =====================================
			// Omni / Point light uniforms
			// =====================================
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_MATRIX, light->light_shader_xform.affine_inverse(), state.shader_version, state.mode_variant, state.specialization);
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_MATRIX_INVERSE, light->light_shader_xform, state.shader_version, state.mode_variant, state.specialization);

			Transform2D light_local = light->xform_cache.affine_inverse();
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_LOCAL_MATRIX, light_local, state.shader_version, state.mode_variant, state.specialization);

			Vector2 light_pos = light->xform_cache.get_origin();
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_POS, light_pos, state.shader_version, state.mode_variant, state.specialization);

			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_HEIGHT, light->height, state.shader_version, state.mode_variant, state.specialization);

			if (state.using_shadow) {
				Transform2D shadow_matrix = light->xform_cache.affine_inverse();
				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_MATRIX, shadow_matrix, state.shader_version, state.mode_variant, state.specialization);

				CanvasLight *cl = canvas_light_owner.get_or_null(light->light_internal);
				if (cl) {
					state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_Y_OFS, cl->shadow.y_offset, state.shader_version, state.mode_variant, state.specialization);
					state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_ZFAR_INV, 1.0f / cl->shadow.z_far, state.shader_version, state.mode_variant, state.specialization);
				}

				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::LIGHT_SHADOW_COLOR, light->shadow_color, state.shader_version, state.mode_variant, state.specialization);

				float shadow_pixel_size = (1.0f / state.shadow_texture_size) * (1.0f + light->shadow_smooth);
				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOWPIXEL_SIZE, shadow_pixel_size, state.shader_version, state.mode_variant, state.specialization);
				state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SHADOW_GRADIENT, light->shadow_smooth, state.shader_version, state.mode_variant, state.specialization);
			}
		}
	}

	if (state.render_target != RID()) {
		GLES2::RenderTarget *rt = GLES2::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt) {
			Vector2 screen_pixel_size;
			screen_pixel_size.x = 1.0 / rt->size.width;
			screen_pixel_size.y = 1.0 / rt->size.height;
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SCREEN_PIXEL_SIZE, screen_pixel_size, state.shader_version, state.mode_variant, state.specialization);
		}
	}

	if (state.using_skeleton) {
		state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SKELETON_TRANSFORM, state.skeleton_transform, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SKELETON_TRANSFORM_INVERSE, state.skeleton_transform_inverse, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SKELETON_TEXTURE_SIZE, state.skeleton_texture_size, state.shader_version, state.mode_variant, state.specialization);
	}
}

void RasterizerCanvasGLES2::canvas_begin(RID p_to_render_target, bool p_to_backbuffer) {
	if (unlikely(is_context_lost())) {
		WARN_PRINT("VBO Context loss at canvas_begin. Rebuilding.");
		force_context_recovery();
	}
	batch_canvas_begin();

	state.render_target = p_to_render_target;
	state.specialization = 0;
	state.mode_variant = CanvasShaderGLES2::ShaderVariant::MODE_QUAD;

	// Reset the tracker back to the default shader
	state.shader_version = data.canvas_shader_default_version;

	GLES2::RenderTarget *render_target = GLES2::TextureStorage::get_singleton()->get_render_target(p_to_render_target);
	GLES2::Config *config = GLES2::Config::get_singleton();

	if (render_target) {
		render_target->was_used = true;
	}

	// Bind the correct Framebuffer
	if (p_to_backbuffer) {
		GLES2::TextureStorage::get_singleton()->bind_framebuffer(render_target ? render_target->backbuffer_fbo : 0);
		GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: bind backbuffer fbo");
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 4);
		GLES2::Texture *tex = GLES2::TextureStorage::get_singleton()->get_texture(default_canvas_texture);
		if (tex) {
			glBindTexture(GL_TEXTURE_2D, tex->tex_id);
		}
		GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: bind backbuffer tex");
	} else {
		GLES2::TextureStorage::get_singleton()->bind_framebuffer(render_target ? render_target->fbo : 0);
		GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: bind fbo");
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 4);
		if (render_target) {
			glBindTexture(GL_TEXTURE_2D, render_target->backbuffer);
		}
		GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: bind fbo tex");
	}

	// Reset the active texture back to 0 immediately after,
	// otherwise, the very next draw call may accidentally bind
	// its diffuse texture to the screen texture unit.
	glActiveTexture(GL_TEXTURE0);
	GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: reset glActiveTexture");

	// Set blending and clear buffers if needed
	if ((render_target && render_target->is_transparent) || p_to_backbuffer) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	}
	GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: glBlendFuncSeparate");

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: glColorMask");

	if (render_target && render_target->clear_requested) {
		const Color &col = render_target->clear_color;
		glClearColor(col.r, col.g, col.b, render_target->is_transparent ? col.a : 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: clear requested");
		render_target->clear_requested = false;
	}

	// Set Viewport dimensions
	Size2 render_target_size;
	if (render_target) {
		render_target_size = GLES2::TextureStorage::get_singleton()->render_target_get_size(p_to_render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}
	GLsizei vp_w = MAX(0, (int)render_target_size.x);
	GLsizei vp_h = MAX(0, (int)render_target_size.y);

	glViewport(0, 0, vp_w, vp_h);
	GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: glViewport");

	reset_canvas();

	// Bind the default white texture so untextured items draw correctly
	glActiveTexture(GL_TEXTURE0);
	RID white_tex_rid = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
	GLES2::Texture *tex_white = GLES2::TextureStorage::get_singleton()->get_texture(white_tex_rid);

	if (tex_white) {
		glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
	}
	GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: bind white tex");

	// Purge all lingering vertex attribute states
	for (int i = 0; i < maximum_attributes; i++) {
		glDisableVertexAttribArray(i);
	}

	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	glVertexAttrib1f(RS::ARRAY_TANGENT, 0.0f); // Light Angle
	glVertexAttrib4f(RS::ARRAY_TEX_UV2, 1.0f, 1.0f, 1.0f, 1.0f); // Modulate
	glVertexAttrib2f(RS::ARRAY_CUSTOM0, 0.0f, 0.0f); // Large Vertex Transform Translate
	glVertexAttrib4f(RS::ARRAY_CUSTOM1, 1.0f, 0.0f, 0.0f, 1.0f); // Large Vertex Transform Basis
	GL_CHECK_ERROR("GLES2::Canvas::canvas_begin: reset attribs");
	
	// Calculate Projection Matrix
	// Godot 4 seems to expect 3D transforms even in 2D.
	Transform3D canvas_transform;

	if (state.render_target != RID() && render_target) {
		render_target_size = render_target->size;
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}

	// In Godot 4, the default Y-scale for OpenGL is positive 2.0.
	// We only flip it to -2.0 if there is an overridden color texture (like XR).
	float y_scale = 2.0f;
	if (state.render_target != RID()) {
		RID override_color = GLES2::TextureStorage::get_singleton()->render_target_get_override_color(state.render_target);

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

void RasterizerCanvasGLES2::canvas_end() {
	batch_canvas_end();

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::Canvas::canvas_end: disable states");

	for (int i = 0; i < maximum_attributes; i++) {
		glDisableVertexAttribArray(i);
	}

	Size2 render_target_size;
	if (state.render_target != RID()) {
		render_target_size = GLES2::TextureStorage::get_singleton()->render_target_get_size(state.render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}
	GLsizei vp_w = MAX(0, (int)render_target_size.x);
	GLsizei vp_h = MAX(0, (int)render_target_size.y);

	glViewport(0, 0, vp_w, vp_h);
	glScissor(0, 0, vp_w, vp_h);

	glDisable(GL_SCISSOR_TEST);
	GL_CHECK_ERROR("GLES2::Canvas::canvas_end: reset viewport/scissor");

	state.using_skeleton = false;
	state.using_ninepatch = false;
	state.using_light = nullptr;
}

void RasterizerCanvasGLES2::reset_canvas() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LESS);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_CHECK_ERROR("GLES2::Canvas::reset_canvas: states");

	bool transparent_rt = false;
	if (state.render_target != RID()) {
		GLES2::RenderTarget *rt = GLES2::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt && rt->is_transparent) {
			transparent_rt = true;
		}
	} else {
		// The backbuffer (RID == 0) requires separate blend functions to
		// preserve the alpha channel for the OS window compositor.
		transparent_rt = true;
	}

	if (transparent_rt) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	GL_CHECK_ERROR("GLES2::Canvas::reset_canvas: blend func");

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::Canvas::reset_canvas: unbind buffers");
}

void RasterizerCanvasGLES2::set_debug_redraw(bool p_enabled, double p_time, const Color &p_color) {
	if (p_enabled) {
		WARN_PRINT_ONCE("Debug CanvasItem Redraw is not available yet when using the GL Legacy backend.");
	}
}

uint32_t RasterizerCanvasGLES2::get_pipeline_compilations(RS::PipelineSource p_source) {
	return 0;
}

void RasterizerCanvasGLES2::canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_light_list, const Transform2D &p_canvas_transform, RS::CanvasItemTextureFilter p_default_filter, RS::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingMethod::RenderInfo *r_render_info) {
	// Cache the default texture filtering and repeat settings for this pass
	state.default_filter = p_default_filter;
	state.default_repeat = p_default_repeat;

	// Bind the FBO, clears it, and set Projection matrix.
	canvas_begin(p_to_render_target, false);

	// ==========================================
	// The base rendering pass
	// ==========================================
	// We render the entire scene first without any lighting.
	// Passing nullptr for the light prevents the base geometry
	// from being erroneously clipped by light scissoring logic.
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

		canvas_render_items_begin(p_modulate, current_directional_light, p_canvas_transform);

		current_item = p_item_list;
		current_z = 0;

		while (current_item) {
			// Directional lights affect the whole canvas;
			// only check the mask
			if (current_item->light_mask & current_directional_light->item_mask) {
				Item *next_item = current_item->next;
				current_item->next = nullptr;

				canvas_render_items_internal(current_item, current_z, p_modulate, current_directional_light, p_canvas_transform);

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

		canvas_render_items_begin(p_modulate, current_light, p_canvas_transform);

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

					canvas_render_items_internal(current_item, current_z, p_modulate, current_light, p_canvas_transform);

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

void RasterizerCanvasGLES2::canvas_render_items_begin(const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
	batch_canvas_render_items_begin(p_modulate, p_light, p_base_transform);
}

void RasterizerCanvasGLES2::canvas_render_items_implementation(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
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

	if (p_light) {
		// Enable separate equations
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		switch (p_light->blend_mode) {
			case RS::CANVAS_LIGHT_BLEND_MODE_ADD:
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
				break;
			case RS::CANVAS_LIGHT_BLEND_MODE_SUB:
				glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
				break;
			case RS::CANVAS_LIGHT_BLEND_MODE_MIX:
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
				break;
		}
		GL_CHECK_ERROR("GLES2::Canvas::canvas_render_items_implementation: light blend setup");

		// Bind light texture
		glActiveTexture(GL_TEXTURE0 + GLES2::Config::get_singleton()->max_texture_image_units - 6);
		GLES2::Texture *light_tex = GLES2::TextureStorage::get_singleton()->get_texture(p_light->texture);

		if (light_tex) {
			glBindTexture(GL_TEXTURE_2D, light_tex->tex_id);
		} else {
			// Fallback to default white if texture is invalid
			RID white_tex_rid = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
			GLES2::Texture *tex_white = GLES2::TextureStorage::get_singleton()->get_texture(white_tex_rid);
			glBindTexture(GL_TEXTURE_2D, tex_white ? tex_white->tex_id : 0);
		}

		// Clamp the light texture so edges do not bleed
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GL_CHECK_ERROR("GLES2::Canvas::canvas_render_items_implementation: light texture bind");

		// Bind the shadow texture
		state.using_shadow = p_light->use_shadow;
		if (state.using_shadow && state.shadow_texture != 0) {
			glActiveTexture(GL_TEXTURE0 + GLES2::Config::get_singleton()->max_texture_image_units - 5);
			glBindTexture(GL_TEXTURE_2D, state.shadow_texture);

			// Standard nearest filter for the RGBA depth map
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			GL_CHECK_ERROR("GLES2::Canvas::canvas_render_items_implementation: shadow texture bind");
		}
	}

	// Bind default white texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	RID white_tex_rid = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
	GLES2::Texture *tex_white = GLES2::TextureStorage::get_singleton()->get_texture(white_tex_rid);

	if (tex_white) {
		glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
	}
	GL_CHECK_ERROR("GLES2::Canvas::canvas_render_items_implementation: bind GL_TEXTURE0/GL_TEXTURE_2D");

	bool reclip = false;
	bool time_used = false;

	if (bdata.settings_use_batching) {
		// Render using the batched result
		int num_joined_items = bdata.items_joined.size();
		for (int j = 0; j < num_joined_items; j++) {
			const BItemJoined &joined_item = bdata.items_joined[j];
			Item *first_item = bdata.item_refs[joined_item.first_item_ref].item;

			if (unlikely(!first_item)) {
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
					GL_CHECK_ERROR("GLES2::Canvas::render_items: glScissor setup");
				} else {
					gl_disable_scissor();
					GL_CHECK_ERROR("GLES2::Canvas::render_items: glScissor disable");
				}
				reclip = false;
			}

			// Extract the CanvasMaterialData
			GLES2::CanvasMaterialData *mat_data = nullptr;
			if (first_item->material.is_valid()) {
				GLES2::Material *material = GLES2::MaterialStorage::get_singleton()->get_material(first_item->material);
				if (material && material->data) {
					mat_data = static_cast<GLES2::CanvasMaterialData *>(material->data);
					GLES2::CanvasShaderData *shader_data = mat_data->shader_data;

					if (shader_data) {
						if (shader_data->uses_time) {
							time_used = true;
						}
						if (shader_data->uses_screen_texture) {
							GLES2::TextureStorage::get_singleton()->render_target_copy_to_back_buffer(state.render_target, Rect2i(), shader_data->uses_screen_texture_mipmaps);
						}
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
					GL_CHECK_ERROR("GLES2::Canvas::render_items: glScissor setup");
				} else {
					gl_disable_scissor();
					GL_CHECK_ERROR("GLES2::Canvas::render_items: glScissor disable");
				}
				reclip = false;
			}

			// Extract the CanvasMaterialData for legacy rendering too.
			GLES2::CanvasMaterialData *mat_data = nullptr;
			if (ci->material.is_valid()) {
				GLES2::Material *material = GLES2::MaterialStorage::get_singleton()->get_material(ci->material);
				if (material && material->data) {
					mat_data = static_cast<GLES2::CanvasMaterialData *>(material->data);
					GLES2::CanvasShaderData *shader_data = mat_data->shader_data;

					if (shader_data) {
						if (shader_data->uses_time) {
							time_used = true;
						}
						if (shader_data->uses_screen_texture) {
							GLES2::TextureStorage::get_singleton()->render_target_copy_to_back_buffer(state.render_target, Rect2i(), shader_data->uses_screen_texture_mipmaps);
						}
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

	if (time_used) {
		RenderingServerDefault::redraw_request();
	}
}

void RasterizerCanvasGLES2::canvas_render_items_internal(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
	batch_canvas_render_items(p_item_list, p_z, p_modulate, p_light, p_base_transform);
}

void RasterizerCanvasGLES2::canvas_render_items_end() {
	batch_canvas_render_items_end();
}

void RasterizerCanvasGLES2::gl_enable_scissor(int p_x, int p_y, int p_width, int p_height) const {
	glEnable(GL_SCISSOR_TEST);
	glScissor(p_x, p_y, p_width, p_height);
}

void RasterizerCanvasGLES2::gl_disable_scissor() const {
	glDisable(GL_SCISSOR_TEST);
}

void RasterizerCanvasGLES2::_bind_quad_buffer() const {
	glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
	glEnableVertexAttribArray(RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
}

_FORCE_INLINE_ bool RasterizerCanvasGLES2::_buffer_orphan_and_upload(unsigned int p_buffer_size_bytes, unsigned int p_offset_bytes, unsigned int p_data_size_bytes, const void *p_data, GLenum p_target, GLenum p_usage, bool p_optional_orphan) const {
	ERR_FAIL_COND_V((p_offset_bytes + p_data_size_bytes) > p_buffer_size_bytes, false);

	if (!p_optional_orphan) {
		if (GLES2::Config::get_singleton()->is_android_emulator && p_offset_bytes == 0 && p_buffer_size_bytes == p_data_size_bytes) {
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
				uint8_t *garbage = SAFE_ALLOCA_ARRAY(uint8_t, 1024); // (uint8_t *)alloca(1024);
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

void RasterizerCanvasGLES2::_batch_upload_buffers() {
	if (bdata.vertices.size() == 0) {
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
	GL_CHECK_ERROR("GLES2::Canvas::batch_upload: glBindBuffer");

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
		ERR_FAIL_COND_MSG(buffer_bytes > bdata.vertex_buffer_size_bytes, "GLES2: Batch vertex buffer payload exceeds VRAM allocation! Aborting upload.");
		data_ptr = bdata.unit_vertices.get_data();
	}

	uint32_t alloc_size = bdata.vertex_buffer_size_bytes;
	if (GLES2::Config::get_singleton()->is_android_emulator) {
		alloc_size = buffer_bytes; // Emulator workaround: shrink allocation
	}
	_buffer_orphan_and_upload(alloc_size, 0, buffer_bytes, data_ptr, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);

	// Cleanup
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES2::_batch_render_generic(const Batch &p_batch, GLES2::CanvasMaterialData *p_material) {
	ERR_FAIL_COND(p_batch.num_commands <= 0);

	const bool use_light_angles = bdata.use_light_angles;
	const bool use_modulate = bdata.use_modulate;
	const bool use_large_verts = bdata.use_large_verts;
	const bool colored_verts = (
		bdata.use_colored_vertices || use_light_angles ||
		use_modulate || use_large_verts
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

	const BatchTex &tex = bdata.batch_textures[p_batch.batch_texture_id];

	if (tex.tile_mode == BatchTex::TILE_FORCE_REPEAT) {
		state.specialization |= CanvasShaderGLES2::USE_FORCE_REPEAT;
	}

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

	_set_texture_rect_mode(false, use_light_angles, use_modulate, use_large_verts);

	if (tex.tile_mode == BatchTex::TILE_FORCE_REPEAT) {
		state.specialization |= CanvasShaderGLES2::USE_FORCE_REPEAT;
	}

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	uint64_t pointer_offset = p_batch.first_vert * sizeof_vert;

	// Bind the massive dynamic buffer
	glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);
	GL_CHECK_ERROR("GLES2::Canvas::batch_render_generic: bind VBO/IBO");

	glEnableVertexAttribArray(RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertex, pos)));

	glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
	glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertex, uv)));

	if (!colored_verts) {
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(
			RS::ARRAY_COLOR,
			p_batch.color.r / 255.0f,
			p_batch.color.g / 255.0f,
			p_batch.color.b / 255.0f,
			p_batch.color.a / 255.0f
		);
	} else {
		glEnableVertexAttribArray(RS::ARRAY_COLOR);
		// BatchVertexColored is our baseline for colored geometry
		glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertexColored, col)));
	}

	if (use_light_angles) {
		glEnableVertexAttribArray(RS::ARRAY_TANGENT); // attrib:2 is light_angle
		glVertexAttribPointer(RS::ARRAY_TANGENT, 1, GL_FLOAT, GL_FALSE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertexLightAngled, light_angle)));
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TANGENT);
		glVertexAttrib1f(RS::ARRAY_TANGENT, 0.0f);
	}

	if (use_modulate) {
		glEnableVertexAttribArray(RS::ARRAY_TEX_UV2); // attrib:5 is modulate_attrib
		glVertexAttribPointer(RS::ARRAY_TEX_UV2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertexModulated, modulate)));
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV2);
		glVertexAttrib4f(RS::ARRAY_TEX_UV2, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (use_large_verts) {
		glEnableVertexAttribArray(RS::ARRAY_CUSTOM0); // attrib:6 is translate_attrib
		glVertexAttribPointer(RS::ARRAY_CUSTOM0, 2, GL_FLOAT, GL_FALSE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertexLarge, transform)));

		glEnableVertexAttribArray(RS::ARRAY_CUSTOM1); // attrib:7 is basis_attrib
		glVertexAttribPointer(RS::ARRAY_CUSTOM1, 4, GL_FLOAT, GL_FALSE, sizeof_vert, (const void *)(pointer_offset + offsetof(BatchVertexLarge, transform) + (sizeof(float) * 2)));
	} else {
		glDisableVertexAttribArray(RS::ARRAY_CUSTOM0);
		glVertexAttrib2f(RS::ARRAY_CUSTOM0, 0.0f, 0.0f);
		glDisableVertexAttribArray(RS::ARRAY_CUSTOM1);
		glVertexAttrib4f(RS::ARRAY_CUSTOM1, 1.0f, 0.0f, 0.0f, 1.0f);
	}

	GL_CHECK_ERROR("GLES2::Canvas::batch_render_generic: setup client states");

	// Send Pixel Size to the shader (using the state updated by _bind_canvas_texture)
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	switch (p_batch.type) {
		case BatcherEnums::BT_RECT: {
			// Offset is now handled by the AttribPointers. Start at index 0.
			int num_elements = p_batch.num_commands * 6;
			glDrawElements(GL_TRIANGLES, num_elements, GL_UNSIGNED_SHORT, nullptr);
			GL_CHECK_ERROR("GLES2::Canvas::batch_render_generic: glDrawElements (BT_RECT)");
		} break;
		case BatcherEnums::BT_POLY: {
			// Unbind the index buffer before an array draw.
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

			// Start at vertex 0.
			int num_elements = p_batch.num_commands;
			glDrawArrays(GL_TRIANGLES, 0, num_elements);
			GL_CHECK_ERROR("GLES2::Canvas::batch_render_generic: glDrawArrays (BT_POLY)");
		} break;
		default:
			break;
	}

	// Cleanup
	if (tex.tile_mode == BatchTex::TILE_FORCE_REPEAT) {
		state.specialization &= ~(CanvasShaderGLES2::USE_FORCE_REPEAT);
	}

	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	glDisableVertexAttribArray(RS::ARRAY_COLOR);
	glDisableVertexAttribArray(RS::ARRAY_TANGENT);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV2);
	glDisableVertexAttribArray(RS::ARRAY_CUSTOM0);
	glDisableVertexAttribArray(RS::ARRAY_CUSTOM1);
	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::Canvas::batch_render_generic: cleanup");
}

void RasterizerCanvasGLES2::render_batches(Item::Command *const *p_commands, Item *p_current_clip, bool &r_reclip, GLES2::CanvasMaterialData *p_material) {
	int num_batches = bdata.batches.size();
	bool skipping = false;
	Transform2D base_extra = state.uniforms.extra_matrix;

	// Extract and apply the CanvasItemMaterial blend mode
	bool transparent_rt = false;
	if (state.render_target != RID()) {
		GLES2::RenderTarget *rt = GLES2::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt && rt->is_transparent) {
			transparent_rt = true;
		}
	} else {
		transparent_rt = true; // Backbuffer
	}

	// Blend modes
	if (!state.using_light) {
		GLES2::CanvasShaderData::BlendMode blend_mode = GLES2::CanvasShaderData::BLEND_MODE_MIX;
		if (p_material && p_material->shader_data) {
			blend_mode = p_material->shader_data->blend_mode;
		}
		set_gl_blend_mode(blend_mode, transparent_rt);
	}

	for (int batch_num = 0; batch_num < num_batches; batch_num++) {
		const Batch &batch = bdata.batches[batch_num];

		// Reset specialization
		state.specialization = 0;
		state.mode_variant = CanvasShaderGLES2::ShaderVariant::MODE_QUAD;
		state.shader_version = data.canvas_shader_default_version;

		if (state.using_skeleton) {
			state.specialization |= CanvasShaderGLES2::USE_SKELETON;
		}

		if (state.using_light) {
			state.specialization |= CanvasShaderGLES2::USE_LIGHTING;
			if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
				state.specialization |= CanvasShaderGLES2::USE_DIRECTIONAL_LIGHT;
			}
			if (state.using_shadow) {
				state.specialization |= CanvasShaderGLES2::USE_SHADOWS;
				state.specialization |= CanvasShaderGLES2::USE_RGBA_SHADOWS;

				RS::CanvasLightShadowFilter shadow_filter = state.using_light->shadow_filter;

				// Cap shadow filtering to PCF5 for web or android emulators.
				if (OS::get_singleton()->has_feature("web") || GLES2::Config::get_singleton()->is_android_emulator) {
					if (shadow_filter == RS::CANVAS_LIGHT_FILTER_PCF13) {
						shadow_filter = RS::CANVAS_LIGHT_FILTER_PCF5;
					}
				}

				switch (shadow_filter) {
					case RS::CANVAS_LIGHT_FILTER_NONE:
						state.specialization |= CanvasShaderGLES2::SHADOW_FILTER_NEAREST;
						break;
					case RS::CANVAS_LIGHT_FILTER_PCF5:
						state.specialization |= CanvasShaderGLES2::SHADOW_FILTER_PCF5;
						break;
					case RS::CANVAS_LIGHT_FILTER_PCF13:
						state.specialization |= CanvasShaderGLES2::SHADOW_FILTER_PCF13;
						break;
					default:
						state.specialization |= CanvasShaderGLES2::SHADOW_FILTER_NEAREST;
						break;
				}
			}
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
					if (state.using_light) {
						state.uniforms.final_modulate = batch.item->final_modulate;
					} else {
						state.uniforms.final_modulate = batch.item->final_modulate * _render_item_state.item_group_modulate;
					}
					state.uniforms.extra_matrix = Transform2D();
				} else {
					state.uniforms.modelview_matrix = _render_item_state.item_group_base_transform;
					if (state.using_light) {
						state.uniforms.final_modulate = Color(1.0f, 1.0f, 1.0f, 1.0f);
					} else {
						state.uniforms.final_modulate = _render_item_state.item_group_modulate;
					}
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
							glDisableVertexAttribArray(RS::ARRAY_COLOR);
							glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
							glVertexAttrib4fv(RS::ARRAY_COLOR, r->modulate.components);

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
							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::DST_RECT, Color(dst_rect.position.x, dst_rect.position.y, dst_rect.size.x, dst_rect.size.y), state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SRC_RECT, Color(src_rect.position.x, src_rect.position.y, src_rect.size.x, src_rect.size.y), state.shader_version, state.mode_variant, state.specialization);

							glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
							GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_RECT glDrawArrays");

							// Cleanup
							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glDisableVertexAttribArray(RS::ARRAY_VERTEX);
							glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
							state.specialization &= ~(CanvasShaderGLES2::USE_FORCE_REPEAT);
						} break;

						case Item::Command::TYPE_NINEPATCH: {
							Item::CommandNinePatch *np = static_cast<Item::CommandNinePatch *>(command);

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
							_set_texture_rect_mode(false);

							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							glDisableVertexAttribArray(RS::ARRAY_COLOR);
							glVertexAttrib4fv(RS::ARRAY_COLOR, np->color.components);

							if (state.texpixel_size == Size2(0.0, 0.0)) {
								continue;
							}

							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

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
								source.position.x,
								source.position.x + tex_margin_left + (tex_margin_left > 0 ? EPS : 0),
								source.position.x + source.size.x - tex_margin_right - (tex_margin_right > 0 ? EPS : 0),
								source.position.x + source.size.x
							};
							float v[4] = {
								source.position.y,
								source.position.y + tex_margin_top + (tex_margin_top > 0 ? EPS : 0),
								source.position.y + source.size.y - tex_margin_bottom - (tex_margin_bottom > 0 ? EPS : 0),
								source.position.y + source.size.y
							};
							float x[4] = {
								np->rect.position.x,
								np->rect.position.x + draw_margin_left,
								np->rect.position.x + np->rect.size.x - draw_margin_right,
								np->rect.position.x + np->rect.size.x
							};
							float y[4] = {
								np->rect.position.y,
								np->rect.position.y + draw_margin_top,
								np->rect.position.y + np->rect.size.y - draw_margin_bottom,
								np->rect.position.y + np->rect.size.y
							};

							float buffer[16 * 2 + 16 * 2] = {};
							for (int row = 0; row < 4; row++) {
								for (int col = 0; col < 4; col++) {
									int idx = (row * 4 + col) * 4;
									buffer[idx + 0] = x[col];
									buffer[idx + 1] = y[row];
									buffer[idx + 2] = u[col] * state.texpixel_size.x;
									buffer[idx + 3] = v[row] * state.texpixel_size.y;
								}
							}

							glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);
							constexpr uint32_t buffer_size = sizeof(float) * (16 + 16) * 2;
							bool upload_sucess = _buffer_orphan_and_upload(buffer_size, 0, buffer_size, buffer, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
							if (!check_orphan_success(upload_sucess)) {
								return;
							}

							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);

							glEnableVertexAttribArray(RS::ARRAY_VERTEX);
							glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
							glDisableVertexAttribArray(RS::ARRAY_COLOR);

							glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
							glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(sizeof(float) * 2));
							GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_NINEPATCH client states");

							glDrawElements(GL_TRIANGLES, 18 * 3 - (np->draw_center ? 0 : 6), GL_UNSIGNED_BYTE, nullptr);
							GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_NINEPATCH glDrawElements");

							glDisableVertexAttribArray(RS::ARRAY_VERTEX);
							glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
							glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						} break;

						case Item::Command::TYPE_MULTIMESH: {
							Item::CommandMultiMesh *mmesh = static_cast<Item::CommandMultiMesh *>(command);
							GLES2::MultiMesh *multi_mesh = GLES2::MeshStorage::get_singleton()->get_multimesh(mmesh->multimesh);
							if (!multi_mesh || multi_mesh->data_cache.is_empty()) {
								break;
							}

							GLES2::Mesh *mesh_data = GLES2::MeshStorage::get_singleton()->get_mesh(multi_mesh->mesh);
							if (!mesh_data) {
								break;
							}

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
							_set_texture_rect_mode(false);

							// The enums will now exist and compile!
							state.specialization |= CanvasShaderGLES2::USE_INSTANCING;
							if (multi_mesh->uses_custom_data) {
								state.specialization |= CanvasShaderGLES2::USE_INSTANCE_CUSTOM;
							}

							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

							if (state.texpixel_size != Size2(0.0, 0.0)) {
								state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
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

							state.uniforms.extra_matrix = mesh_base_extra;
							state.uniforms.final_modulate = base_modulate;
							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							for (uint32_t j = 0; j < mesh_data->surface_count; j++) {
								GLES2::Mesh::Surface *s = mesh_data->surfaces[j];
								if (unlikely(!s)) {
									continue;
								}

								glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
								if (s->index_count > 0) {
									glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
								}
								GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MULTIMESH bind VBO/IBO");

								if (s->version_count > 0 && s->versions) {
									GLES2::Mesh::Surface::Version *v = &s->versions[0];

									if (unlikely(!v)) {
										continue;
									}

									for (int k = 0; k < maximum_attributes; k++) {
										if (v->attribs[k].enabled) {
											if (k == RS::ARRAY_VERTEX) {
												glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
											} else {
												glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
											}

											GLboolean normalize = v->attribs[k].normalized;
											if (k == RS::ARRAY_COLOR && v->attribs[k].type == GL_UNSIGNED_BYTE) {
												normalize = GL_TRUE;
											}

											glEnableVertexAttribArray(k);
											glVertexAttribPointer(k, v->attribs[k].size, v->attribs[k].type, normalize, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
										} else {
											glDisableVertexAttribArray(k);
											if (k == RS::ARRAY_COLOR) {
												glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
											}
										}
									}
								}
								GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MULTIMESH setup client pointers");

								GLenum gl_primitive = get_gl_primitive_type(s->primitive);
								bool needs_32_bit = s->vertex_count >= (1 << 16);
								GLenum index_type = needs_32_bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

								for (int k = 0; k < amount; k++) {
									const float *buffer = base_buffer + k * stride;

									if (unlikely(!buffer)) {
										continue;
									}

									// Godot 4 natively packs 2D MultiMesh transforms in ROW-MAJOR layout
									if (maximum_attributes > 8) {
										glVertexAttrib4f(8, buffer[0], buffer[1], 0.0f, buffer[3]);
									}

									if (maximum_attributes > 9) {
										glVertexAttrib4f(9, buffer[4], buffer[5], 0.0f, buffer[7]);
									}
									
									if (maximum_attributes > 10) {
										glVertexAttrib4f(10, 0.0f, 0.0f, 1.0f, 0.0f);
									}
									
									if (maximum_attributes > 11) {
										if (multi_mesh->uses_colors) {
											const float *color_data = buffer + color_ofs;
											glVertexAttrib4f(11, color_data[0], color_data[1], color_data[2], color_data[3]);
										} else {
											glVertexAttrib4f(11, 1.0f, 1.0f, 1.0f, 1.0f);
										}                                        
									}
									
									if (maximum_attributes > 12) {
										if (multi_mesh->uses_custom_data) {
											const float *custom_data = buffer + custom_data_ofs;
											glVertexAttrib4f(12, custom_data[0], custom_data[1], custom_data[2], custom_data[3]);
										} else {
											glVertexAttrib4f(12, 0.0f, 0.0f, 0.0f, 0.0f);
										}
									}


									if (s->index_count > 0) {
										if (unlikely(needs_32_bit && !GLES2::Config::get_singleton()->support_32_bits_indices)) {
											ERR_PRINT_ONCE("GLES2: Device does not support 32-bit indices for large MultiMeshes.");
										} else {
											glDrawElements(gl_primitive, s->index_count, index_type, nullptr);
											GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MULTIMESH glDrawElements");
										}
									} else {
										glDrawArrays(gl_primitive, 0, s->vertex_count);
										GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MULTIMESH glDrawArrays");
									}
								}

								for (int k = 0; k < maximum_attributes; k++) {
									glDisableVertexAttribArray(k);
									if (k == RS::ARRAY_COLOR) {
										glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
									}
								}
							}

							// Restore specializations back to normal for the next items
							state.specialization &= ~CanvasShaderGLES2::USE_INSTANCING;
							state.specialization &= ~CanvasShaderGLES2::USE_INSTANCE_CUSTOM;

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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
										GL_CHECK_ERROR("GLES2::Canvas::render_batches: Item::Command::TYPE_CLIP_IGNORE: glScissor");
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
							_set_texture_rect_mode(false);

							// Bind Shader and stack the item's material (if any)
							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);

							// CommandMesh has its own transform and modulate that stack with the item's baseline
							Transform2D prev_transform_extra = state.uniforms.extra_matrix;
							Color prev_colour_module = state.uniforms.final_modulate;

							state.uniforms.extra_matrix = state.uniforms.extra_matrix * mesh_cmd->transform;
							state.uniforms.final_modulate = state.uniforms.final_modulate * mesh_cmd->modulate;

							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							if (state.texpixel_size != Size2(0.0, 0.0)) {
								state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							}

							// Fetch and Draw Mesh Data
							GLES2::Mesh *mesh_data = GLES2::MeshStorage::get_singleton()->get_mesh(mesh_cmd->mesh);
							if (mesh_data) {
								// Loop using the double pointer array
								for (uint32_t j = 0; j < mesh_data->surface_count; j++) {
									GLES2::Mesh::Surface *s = mesh_data->surfaces[j];

									if (unlikely(!s)) {
										continue;
									}

									glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);

									if (s->index_count > 0) {
										glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
									}
									GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MESH bind VBO/IBO");

									// Setup vertex attributes from the cached Version struct
									if (s->version_count > 0 && s->versions) {
										GLES2::Mesh::Surface::Version *v = &s->versions[0];

										if (unlikely(!v)) {
											continue;
										}

										for (int k = 0; k < maximum_attributes; k++) {
											if (v->attribs[k].enabled) {
												if (k == RS::ARRAY_VERTEX) {
													glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
												} else {
													glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
												}

												GLboolean normalize = v->attribs[k].normalized;
												if (k == RS::ARRAY_COLOR && v->attribs[k].type == GL_UNSIGNED_BYTE) {
													normalize = GL_TRUE;
												}

												glEnableVertexAttribArray(k);
												glVertexAttribPointer(k, v->attribs[k].size, v->attribs[k].type, normalize, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
											} else {
												glDisableVertexAttribArray(k);
												if (k == RS::ARRAY_COLOR) {
													glVertexAttrib4f(k, 1.0f, 1.0f, 1.0f, 1.0f);
												}
											}
										}
										GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MESH setup client pointers");
									}

									// Draw call using the correct index count and vertex count
									GLenum gl_primitive = get_gl_primitive_type(s->primitive);
									if (s->index_count > 0) {
										bool needs_32_bit = s->vertex_count >= (1 << 16);

										if (unlikely(needs_32_bit && !GLES2::Config::get_singleton()->support_32_bits_indices)) {
											ERR_PRINT_ONCE("GLES2: Device does not support 32-bit indices for large 2D meshes. Skipping draw.");
										} else {
											GLenum index_type = needs_32_bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
											glDrawElements(gl_primitive, s->index_count, index_type, nullptr);
											GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MESH glDrawElements");
										}
									} else {
										glDrawArrays(gl_primitive, 0, s->vertex_count);
										GL_CHECK_ERROR("GLES2::Canvas::render_batches: TYPE_MESH glDrawArrays");
									}

									// Clean up attributes
									for (int k = 0; k < maximum_attributes; k++) {
										glDisableVertexAttribArray(k);
										if (k == RS::ARRAY_COLOR) {
											glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
										}
									}
								}
							}

							// Restore state
							state.uniforms.extra_matrix = prev_transform_extra;
							state.uniforms.final_modulate = prev_colour_module;

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						} break;

						case Item::Command::TYPE_TRANSFORM: {
							Item::CommandTransform *transform = static_cast<Item::CommandTransform *>(command);

							state.uniforms.extra_matrix = transform->xform;
							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::EXTRA_MATRIX, state.uniforms.extra_matrix, state.shader_version, state.mode_variant, state.specialization);
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

void RasterizerCanvasGLES2::_draw_gui_primitive(int p_points, const Vector2 *p_vertices, const Color *p_colors, const Vector2 *p_uvs, const float *p_light_angles) {
	if (unlikely(p_points <= 0)) {
		return;
	}
	glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);

	uint32_t vertex_size = p_points * sizeof(Vector2);
	uint32_t color_size = p_colors ? p_points * sizeof(Color) : 0;
	uint32_t uv_size = p_uvs ? p_points * sizeof(Vector2) : 0;
	uint32_t total_size = vertex_size + color_size + uv_size;

	// Lower VRAM fragmentation by only growing the buffer.
	// If it fits, we pass the tracked size with
	// nullptr to perform a zero-cost buffer orphan.
	if (total_size > data.polygon_buffer_size) {
		data.polygon_buffer_size = next_power_of_2(total_size);
	}
	uint32_t offset = 0;

	// Vertices
	bool upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, vertex_size, p_vertices, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
	if (!check_orphan_success(upload_success)) {
		return;
	}

	glEnableVertexAttribArray(RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
	offset += vertex_size;

	// Colors
	if (p_colors) {
		upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, color_size, p_colors, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
		if (!check_orphan_success(upload_success)) {
			return;
		}

		glEnableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
		offset += color_size;
	} else {
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	// UVs
	if (p_uvs) {
		upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, uv_size, p_uvs, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
		if (!check_orphan_success(upload_success)) {
			return;
		}

		glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
		offset += uv_size;
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	}
	GL_CHECK_ERROR("GLES2::Canvas::draw_gui: buffer subdata and pointers");

	// For Gizmos, we often draw Lines, Triangles, or Points depending on p_points
	GLenum draw_mode = (p_points == 2) ? GL_LINES : ((p_points == 3) ? GL_TRIANGLES : GL_TRIANGLE_FAN);
	glDrawArrays(draw_mode, 0, p_points);

	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	glDisableVertexAttribArray(RS::ARRAY_COLOR);
	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::Canvas::draw_gui: glDrawArrays");
}

void RasterizerCanvasGLES2::_legacy_draw_primitive(Item::CommandPrimitive *p_pr, GLES2::CanvasMaterialData *p_material) {
	ERR_FAIL_COND(p_pr->point_count < 1);

	_bind_canvas_texture(p_pr->texture, state.default_filter, state.default_repeat);

	_set_texture_rect_mode(false);

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
	_draw_gui_primitive(p_pr->point_count, p_pr->points, p_pr->colors, p_pr->uvs);
}

void RasterizerCanvasGLES2::_legacy_draw_line(Item::CommandPrimitive *p_pr, GLES2::CanvasMaterialData *p_material) {
	// Godot 4 uses TYPE_PRIMITIVE with 2 points to draw lines
	_legacy_draw_primitive(p_pr, p_material);
}

void RasterizerCanvasGLES2::_legacy_draw_polygon(Item::CommandPolygon *p_poly, GLES2::CanvasMaterialData *p_material) {
	if (!polygon_cache.has(p_poly->polygon.polygon_id)) {
		return;
	}

	const PolyData &pd = polygon_cache[p_poly->polygon.polygon_id];
	if (pd.points.is_empty()) {
		return;
	}

	// Bind texture and get pixel size
	_bind_canvas_texture(p_poly->texture, state.default_filter, state.default_repeat);

	_set_texture_rect_mode(false);

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXTURE_PIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	// Setup vertex attributes
	glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);

	uint32_t points_count = pd.points.size();
	uint32_t points_size = points_count * sizeof(Vector2);
	uint32_t uvs_size = pd.uvs.size() * sizeof(Vector2);

	// Only allocate VBO space for colors if there is actually one color per vertex
	bool use_vertex_colors = pd.colors.size() > 1;
	uint32_t colors_size = use_vertex_colors ? (points_count * sizeof(Color)) : 0;

	// Orphan the buffer and upload new data
	uint32_t total_size = points_size + uvs_size + colors_size;
	if (total_size > data.polygon_buffer_size) {
		data.polygon_buffer_size = next_power_of_2(total_size);
	}
	uint32_t offset = 0;

	// Points
	bool upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, points_size, pd.points.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
	if (!check_orphan_success(upload_success)) {
		return;
	}

	glEnableVertexAttribArray(RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
	offset += points_size;

	// UVs
	if (uvs_size > 0) {
		upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, uvs_size, pd.uvs.ptr(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
		if (!check_orphan_success(upload_success)) {
			return;
		}

		glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
		offset += uvs_size;
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	}
	GL_CHECK_ERROR("GLES2::Canvas::_legacy_draw_polygon: buffer subdata and pointers");

	// Colors
	if (pd.colors.size() == 1) {
		// Flat colored polygon (e.g., editor backgrounds)
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(RS::ARRAY_COLOR, pd.colors[0].r, pd.colors[0].g, pd.colors[0].b, pd.colors[0].a);
	} else if (use_vertex_colors) {
		// Vertex colored polygon
		Color *precalced_colors = SAFE_ALLOCA_ARRAY(Color, points_count);
		if (precalced_colors) {
			int num_colors_specified = MIN((int)pd.colors.size(), (int)points_count);
			Color vcol = pd.colors[0];

			for (int n = 0; n < num_colors_specified; n++) {
				precalced_colors[n] = pd.colors[n];
			}

			// Pad the missing vertices
			for (int n = num_colors_specified; n < (int)points_count; n++) {
				precalced_colors[n] = vcol;
			}

			upload_success = _buffer_orphan_and_upload(data.polygon_buffer_size, offset, colors_size, precalced_colors, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, true);
			if (!check_orphan_success(upload_success)) {
				return;
			}

			glEnableVertexAttribArray(RS::ARRAY_COLOR);
			glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
		}
	} else {
		// No color
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f); // Default white
	}

	// Map primitive type
	GLenum gl_primitive = get_gl_primitive_type(p_poly->primitive);

	// Draw
	if (!pd.indices.is_empty()) {
		int index_count = pd.indices.size();

		// GLES2 strictly requires 16-bit indices.
		if (unlikely(pd.points.size() > 65535)) {
			ERR_PRINT_ONCE("GLES2: Cannot draw polygon with more than 65535 vertices due to 16-bit index hardware limits.");
			glDisableVertexAttribArray(RS::ARRAY_VERTEX);
			glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
			glDisableVertexAttribArray(RS::ARRAY_COLOR);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			return;
		}

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer);

		Vector<uint16_t> indices_16;
		indices_16.resize(index_count);

		for (int i = 0; i < index_count; i++) {
			indices_16.write[i] = (uint16_t)pd.indices[i];
		}

		uint32_t index_size = index_count * sizeof(uint16_t);

		if (index_size > data.polygon_index_buffer_size) {
			data.polygon_index_buffer_size = next_power_of_2(index_size);
		}

		upload_success = _buffer_orphan_and_upload(data.polygon_index_buffer_size, 0, index_size, indices_16.ptr(), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW, false);
		if (!check_orphan_success(upload_success)) {
			return;
		}
		glDrawElements(gl_primitive, index_count, GL_UNSIGNED_SHORT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	} else {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		// Draw arrays if no indices
		// Godot passes PRIMITIVE_TRIANGLES, but un-indexed polygons must be fanned.
		if (gl_primitive == GL_TRIANGLES) {
			glDrawArrays(GL_TRIANGLE_FAN, 0, pd.points.size());
			GL_CHECK_ERROR("GLES2::Canvas::_legacy_draw_polygon: glDrawArrays (GL_TRIANGLE_FAN)");
		} else {
			glDrawArrays(gl_primitive, 0, pd.points.size());
			GL_CHECK_ERROR("GLES2::Canvas::_legacy_draw_polygon: glDrawArrays (Primitive)");
		}
	}

	// Cleanup
	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	glDisableVertexAttribArray(RS::ARRAY_COLOR);
	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::Canvas::_legacy_draw_polygon: cleanup");
}

void RasterizerCanvasGLES2::_set_texture_rect_mode(bool p_texture_rect, bool p_light_angle, bool p_modulate, bool p_large_vertex) {
	// Switch the shader variant between Quad and Texture Rect
	state.mode_variant = p_texture_rect ? CanvasShaderGLES2::ShaderVariant::MODE_TEXTURE_RECT : CanvasShaderGLES2::ShaderVariant::MODE_QUAD;

	uint64_t spec = 0;
	if (p_light_angle) {
		spec |= CanvasShaderGLES2::USE_ATTRIB_LIGHT_ANGLE;
	}
	if (p_modulate) {
		spec |= CanvasShaderGLES2::USE_ATTRIB_MODULATE;
	}
	if (p_large_vertex) {
		spec |= CanvasShaderGLES2::USE_ATTRIB_LARGE_VERTEX;
	}
	if (state.using_skeleton) {
		spec |= CanvasShaderGLES2::USE_SKELETON;
	}
	if (state.normal_used) {
		spec |= CanvasShaderGLES2::USE_DEFAULT_NORMAL;
	}
	if (state.using_light) {
		spec |= CanvasShaderGLES2::USE_LIGHTING;
		if (state.using_light->mode == RS::CANVAS_LIGHT_MODE_DIRECTIONAL) {
			spec |= CanvasShaderGLES2::USE_DIRECTIONAL_LIGHT;
		}
		if (state.using_shadow) {
			spec |= CanvasShaderGLES2::USE_SHADOWS;
			spec |= CanvasShaderGLES2::USE_RGBA_SHADOWS;

			RS::CanvasLightShadowFilter shadow_filter = state.using_light->shadow_filter;

			// Cap shadow filtering to PCF5 for web or android emulators.
			if (OS::get_singleton()->has_feature("web") || GLES2::Config::get_singleton()->is_android_emulator) {
				if (shadow_filter == RS::CANVAS_LIGHT_FILTER_PCF13) {
					shadow_filter = RS::CANVAS_LIGHT_FILTER_PCF5;
				}
			}

			switch (shadow_filter) {
				case RS::CANVAS_LIGHT_FILTER_NONE:
					spec |= CanvasShaderGLES2::SHADOW_FILTER_NEAREST;
					break;
				case RS::CANVAS_LIGHT_FILTER_PCF5:
					spec |= CanvasShaderGLES2::SHADOW_FILTER_PCF5;
					break;
				case RS::CANVAS_LIGHT_FILTER_PCF13:
					spec |= CanvasShaderGLES2::SHADOW_FILTER_PCF13;
					break;
				default:
					spec |= CanvasShaderGLES2::SHADOW_FILTER_NEAREST;
					break;
			}
		}
	}

	state.specialization = spec;
}

RendererCanvasRender::PolygonID RasterizerCanvasGLES2::request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, const Vector<int> &p_bones, const Vector<float> &p_weights) {
	PolygonID id = next_polygon_id++;
	PolyData pd;
	pd.indices = p_indices;
	pd.points = p_points;
	pd.colors = p_colors;
	pd.uvs = p_uvs;

	polygon_cache[id] = pd;
	return id;
}

void RasterizerCanvasGLES2::free_polygon(PolygonID p_polygon) {
	polygon_cache.erase(p_polygon);
}

void RasterizerCanvasGLES2::set_time(double p_time) {
	state.time = p_time;
}

bool RasterizerCanvasGLES2::is_context_lost() const {
	if (data.canvas_quad_vertices != 0) {
		// Honest drivers will flag this immediately.
		if (glIsBuffer(data.canvas_quad_vertices) == GL_FALSE) {
			return true;
		}

		// Liar drivers lie about glIsBuffer after silent context loss.
		// We perform a single deliberate bind and check for GL_INVALID_OPERATION.
		while (glGetError() != GL_NO_ERROR); // Clear stale errors

		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
		bool is_zombie = (glGetError() == GL_INVALID_OPERATION);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		return is_zombie;
	}

	return false;
}

void RasterizerCanvasGLES2::force_context_recovery() {
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

	// Sterilize global shadow framebuffers
	state.shadow_texture = 0;
	state.shadow_depth_buffer = 0;
	state.shadow_fb = 0;

	initialize();
	reset_canvas();

	// Broadcast the death of the context globally.
	GLES2::Config::get_singleton()->context_generation++;
}

RasterizerCanvasGLES2 *RasterizerCanvasGLES2::singleton = nullptr;

RasterizerCanvasGLES2 *RasterizerCanvasGLES2::get_singleton() {
	return singleton;
}

RasterizerCanvasGLES2::RasterizerCanvasGLES2() {
	singleton = this;

	initialize();

	state.canvas_shader = &GLES2::MaterialStorage::get_singleton()->shaders.canvas_shader;
	data.canvas_shader_default_version = state.canvas_shader->default_version;

	// Allocate the default white texture for untextured polygons/rects
	default_canvas_texture = GLES2::TextureStorage::get_singleton()->canvas_texture_allocate();
	GLES2::TextureStorage::get_singleton()->canvas_texture_initialize(default_canvas_texture);

	batch_constructor();
}

RasterizerCanvasGLES2::~RasterizerCanvasGLES2() {
	singleton = nullptr;

	GLES2::TextureStorage::get_singleton()->canvas_texture_free(default_canvas_texture);

	// Shaders
	shadow_render.shader.version_free(shadow_render.shader_version);

	// Free buffers
	GLES2::Utilities::get_singleton()->buffer_free_data(data.canvas_quad_vertices);
	GLES2::Utilities::get_singleton()->buffer_free_data(data.ninepatch_vertices);
	GLES2::Utilities::get_singleton()->buffer_free_data(data.ninepatch_elements);
	GLES2::Utilities::get_singleton()->buffer_free_data(data.polygon_buffer);
	GLES2::Utilities::get_singleton()->buffer_free_data(data.polygon_index_buffer);

	// Free batcher buffers
	if (bdata.gl_vertex_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(bdata.gl_vertex_buffer);
	}
	if (bdata.gl_index_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(bdata.gl_index_buffer);
	}
}

#endif // GLES2_ENABLED
