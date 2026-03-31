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

_FORCE_INLINE_ static uint32_t _indices_to_primitives(RS::PrimitiveType p_primitive, uint32_t p_indices) {
    return 0;
}

RID RasterizerCanvasGLES1::light_create() {
    return RID();
}

void RasterizerCanvasGLES1::light_set_texture(RID p_rid, RID p_texture) {

}

void RasterizerCanvasGLES1::light_set_use_shadow(RID p_rid, bool p_enable) {

}

void RasterizerCanvasGLES1::light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) {

}

void RasterizerCanvasGLES1::light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) {

}

void RasterizerCanvasGLES1::render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) {

}

RID RasterizerCanvasGLES1::occluder_polygon_create() {
    return RID();
}

void RasterizerCanvasGLES1::occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) {

}

void RasterizerCanvasGLES1::occluder_polygon_set_cull_mode(RID p_occluder, RS::CanvasOccluderPolygonCullMode p_mode) {

}

void RasterizerCanvasGLES1::set_shadow_texture_size(int p_size) {

}

bool RasterizerCanvasGLES1::free(RID p_rid) {
	return true;
}

void RasterizerCanvasGLES1::initialize() {
	print_verbose("GLES1: Initializing Canvas Renderer");

	// Quad buffer
	{
		glGenBuffers(1, &data.canvas_quad_vertices);
		glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);

		const float qv[8] = {
			0, 0,
			0, 1,
			1, 1,
			1, 0
		};

		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, qv, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	// Polygon buffer
	{
		uint32_t poly_size = GLOBAL_DEF("rendering/limits/buffers/canvas_polygon_buffer_size_kb", 128);
		poly_size = MAX(poly_size, (uint32_t)128); // minimum 128k
		poly_size *= 1024;

		glGenBuffers(1, &data.polygon_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);
		glBufferData(GL_ARRAY_BUFFER, poly_size, nullptr, GL_DYNAMIC_DRAW);
		data.polygon_buffer_size = poly_size;
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		uint32_t index_size = GLOBAL_DEF("rendering/limits/buffers/canvas_polygon_index_buffer_size_kb", 128);
		index_size = MAX(index_size, (uint32_t)128);
		index_size *= 1024;

		glGenBuffers(1, &data.polygon_index_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_size, nullptr, GL_DYNAMIC_DRAW);
		data.polygon_index_buffer_size = index_size;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	// Ninepatch buffers
	{
		glGenBuffers(1, &data.ninepatch_vertices);
		glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (16 + 16) * 2, nullptr, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glGenBuffers(1, &data.ninepatch_elements);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);

#define _EIDX(y, x) (y * 4 + x)
		uint8_t elems[3 * 2 * 9] = {
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

		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elems), elems, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	_set_texture_rect_mode(true);

	state.using_light = nullptr;
	state.using_skeleton = false;

	// Batcher Initialisation
	batch_initialize();

	if (bdata.vertex_buffer_size_bytes) {
		// Reserve space for dynamic batched vertices
		glGenBuffers(1, &bdata.gl_vertex_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, bdata.vertex_buffer_size_bytes, nullptr, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// Pre-fill index buffer.
		// The indices never need to change so they are static.
		glGenBuffers(1, &bdata.gl_index_buffer);
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

		glBufferData(GL_ELEMENT_ARRAY_BUFFER, bdata.index_buffer_size_bytes, indices.ptr(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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
	if (state.current_tex == p_texture && state.current_filter == p_base_filter && state.current_repeat == p_base_repeat) {
		return;
	}

	// Resolve CanvasTexture
	GLES1::CanvasTexture *ct = nullptr;
	GLES1::Texture *t = GLES1::TextureStorage::get_singleton()->get_texture(p_texture);

	if (t) {
		// If it's a raw texture, it might have an embedded CanvasTexture
		if (!t->canvas_texture) {
			// This is based on the GLES3 code, but tbh I find it somewhat icky, since we don't create a RID and I also can't see how or when this is destroyed.
			t->canvas_texture = memnew(GLES1::CanvasTexture);
			t->canvas_texture->diffuse = p_texture;
		}

		ct = t->canvas_texture;
		if (t->render_target) {
			t->render_target->used_in_frame = true;
		}
	} else {
		// It's a genuine CanvasTexture RID
		ct = GLES1::TextureStorage::get_singleton()->get_canvas_texture(p_texture);
	}

	if (!ct) {
		// Completely invalid texture, bind white safely
		RID white_tex = GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
		_bind_canvas_texture(white_tex, p_base_filter, p_base_repeat);
		return;
	}

	state.current_tex_is_rt = false;

	// Resolve overrides from CanvasTexture
	RS::CanvasItemTextureFilter filter = ct->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? ct->texture_filter : p_base_filter;
	RS::CanvasItemTextureRepeat repeat = ct->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? ct->texture_repeat : p_base_repeat;

	// Bind Diffuse (Texture Unit 0)
	glActiveTexture(GL_TEXTURE0);
	GLES1::Texture *texture = GLES1::TextureStorage::get_singleton()->get_texture(ct->diffuse);

	if (!texture || texture->tex_id == 0 || texture->width == 0 || texture->height == 0) {
		// Something _really_ went wrong here if we got here.
		GLES1::Texture *tex_2d_bg = GLES1::TextureStorage::get_singleton()->get_texture(
			GLES1::TextureStorage::get_singleton()->texture_gl_get_default(
				GLES1::DEFAULT_GL_TEXTURE_WHITE
			)
		);
		if (tex_2d_bg) {
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, tex_2d_bg->tex_id);
			state.texpixel_size = Size2(1.0 / tex_2d_bg->width, 1.0 / tex_2d_bg->height);
			state.texture_size = Size2i(tex_2d_bg->width, tex_2d_bg->height);
		}
	} else {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, texture->tex_id);

		if (GLES1::Config::get_singleton()->support_texture_env_combine) {
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		}

		// Check if this texture is NPOT
		bool is_npot = false;
		unsigned int tex_w = static_cast<unsigned int>(texture->width);
		unsigned int tex_h = static_cast<unsigned int>(texture->height);
		if (tex_w != next_power_of_2(texture->width) || tex_h != next_power_of_2(texture->height)) {
			is_npot = true;
		}

		// Setup Filters
		GLenum min_filter = GL_LINEAR;
		GLenum mag_filter = GL_LINEAR;
		GLenum wrap = GL_REPEAT;

		if (p_base_filter == RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST) {
			min_filter = GL_NEAREST;
			mag_filter = GL_NEAREST;
		}

		if (p_base_repeat == RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED || is_npot) {
			// NPOT textures must be clamped to edge in GLES1,
			// no exceptions.
			wrap = GL_CLAMP_TO_EDGE;
		}

		// Force the parameters directly to the driver
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

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

	GLint max_units = GLES1::Config::get_singleton()->max_texture_image_units;

	// Normal Map
	glActiveTexture(GL_TEXTURE0 + max_units - 6);
	GLES1::Texture *normal_map = GLES1::TextureStorage::get_singleton()->get_texture(ct->normal_map);
	if (!normal_map) {
		GLES1::Texture *tex_norm = GLES1::TextureStorage::get_singleton()->get_texture(GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_NORMAL));
		if (tex_norm) {
			glBindTexture(GL_TEXTURE_2D, tex_norm->tex_id);
		}
		state.normal_used = false;
	} else {
		glBindTexture(GL_TEXTURE_2D, normal_map->tex_id);
		state.normal_used = true;
		state.current_tex_is_rt = state.current_tex_is_rt || normal_map->render_target;
	}

	// Specular Map
	glActiveTexture(GL_TEXTURE0 + max_units - 7);
	GLES1::Texture *specular_map = GLES1::TextureStorage::get_singleton()->get_texture(ct->specular);
	if (!specular_map) {
		GLES1::Texture *tex_spec = GLES1::TextureStorage::get_singleton()->get_texture(GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE));
		if (tex_spec) {
			glBindTexture(GL_TEXTURE_2D, tex_spec->tex_id);
		}
	} else {
		glBindTexture(GL_TEXTURE_2D, specular_map->tex_id);
		state.current_tex_is_rt = state.current_tex_is_rt || specular_map->render_target;
	}

	// Reset Active Texture to 0 so we don't
	// accidentally draw geometry to the specular unit
	glActiveTexture(GL_TEXTURE0);

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

	if (state.render_target != RID()) {
		GLES1::RenderTarget *rt = GLES1::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt) {
			Vector2 screen_pixel_size;
			screen_pixel_size.x = 1.0 / rt->size.width;
			screen_pixel_size.y = 1.0 / rt->size.height;
			state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SCREEN_PIXEL_SIZE, screen_pixel_size, state.shader_version, state.mode_variant, state.specialization);
		}
	}

	// TODO(GLES2): Only added Skeletons for now,
	// lighting will come way later (in Phase 3, probably).
	if (state.using_skeleton) {
		state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SKELETON_TRANSFORM, state.skeleton_transform, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SKELETON_TRANSFORM_INVERSE, state.skeleton_transform_inverse, state.shader_version, state.mode_variant, state.specialization);
		state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SKELETON_TEXTURE_SIZE, state.skeleton_texture_size, state.shader_version, state.mode_variant, state.specialization);
	}
}

void RasterizerCanvasGLES1::canvas_begin(RID p_to_render_target, bool p_to_backbuffer) {
	batch_canvas_begin();

	state.render_target = p_to_render_target;
	state.specialization = 0;
	state.mode_variant = CanvasShaderGLES1::ShaderVariant::MODE_QUAD;

	GLES1::RenderTarget *render_target = GLES1::TextureStorage::get_singleton()->get_render_target(p_to_render_target);
	GLES1::Config *config = GLES1::Config::get_singleton();

	if (render_target) {
		render_target->was_used = true;
	}

	// Bind the correct Framebuffer
	if (p_to_backbuffer) {
		glBindFramebuffer(GL_FRAMEBUFFER, render_target ? render_target->backbuffer_fbo : 0);
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 4);
		GLES1::Texture *tex = GLES1::TextureStorage::get_singleton()->get_texture(default_canvas_texture);
		if (tex) {
			glBindTexture(GL_TEXTURE_2D, tex->tex_id);
		}
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, render_target ? render_target->fbo : 0);
		glActiveTexture(GL_TEXTURE0 + config->max_texture_image_units - 4);
		if (render_target) {
			glBindTexture(GL_TEXTURE_2D, render_target->backbuffer);
		}
	}

	// Reset the active texture back to 0 immediately after,
	// otherwise, the very next draw call may accidentally bind
	// its diffuse texture to the screen texture unit.
	glActiveTexture(GL_TEXTURE0);

	// Set blending and clear buffers if needed
	if ((render_target && render_target->is_transparent) || p_to_backbuffer) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	}

	if (render_target && render_target->clear_requested) {
		const Color &col = render_target->clear_color;
		glClearColor(col.r, col.g, col.b, render_target->is_transparent ? col.a : 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		render_target->clear_requested = false;
	}

	// Set Viewport dimensions
	Size2 render_target_size;
	if (render_target) {
		render_target_size = GLES1::TextureStorage::get_singleton()->render_target_get_size(p_to_render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}
	glViewport(0, 0, render_target_size.x, render_target_size.y);

	reset_canvas();

	// Bind the default white texture so untextured items draw correctly
	glActiveTexture(GL_TEXTURE0);
	RID white_tex_rid = GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
	GLES1::Texture *tex_white = GLES1::TextureStorage::get_singleton()->get_texture(white_tex_rid);

	if (tex_white) {
		glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
	}

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

	// In Godot 4, the default Y-scale for OpenGL is positive 2.0.
	// We only flip it to -2.0 if there is an overridden color texture (like XR).
	float y_scale = 2.0f;
	if (state.render_target != RID()) {
		RID override_color = GLES1::TextureStorage::get_singleton()->render_target_get_override_color(state.render_target);

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

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	glDisable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	Size2 render_target_size;
	if (state.render_target != RID()) {
		render_target_size = GLES1::TextureStorage::get_singleton()->render_target_get_size(state.render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}

	glViewport(0, 0, render_target_size.x, render_target_size.y);
	glScissor(0, 0, render_target_size.x, render_target_size.y);

	state.using_skeleton = false;
	state.using_ninepatch = false;
}

void RasterizerCanvasGLES1::reset_canvas() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DITHER);
	glEnable(GL_BLEND);

	bool transparent_rt = state.render_target != RID() && GLES1::TextureStorage::get_singleton()->get_render_target(state.render_target)->is_transparent;

	if (transparent_rt) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES1::set_debug_redraw(bool p_enabled, double p_time, const Color &p_color) {
	if (p_enabled) {
		WARN_PRINT_ONCE("Debug CanvasItem Redraw is not available yet when using the GL Legacy backend.");
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

	// Kick off the batcher loops
	canvas_render_items_begin(p_modulate, p_light_list, p_canvas_transform);
	
	// TODO(GLES2): Hardcoding p_z to 0 for now until Z-layer sorting is fully ported
	canvas_render_items_internal(p_item_list, 0, p_modulate, p_light_list, p_canvas_transform);
	
	canvas_render_items_end();

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

	// Bind default white texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	RID white_tex_rid = GLES1::TextureStorage::get_singleton()->texture_gl_get_default(GLES1::DEFAULT_GL_TEXTURE_WHITE);
	GLES1::Texture *tex_white = GLES1::TextureStorage::get_singleton()->get_texture(white_tex_rid);

	if (tex_white) {
		glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
	}

	bool reclip = false;
	bool time_used = false;

	if (bdata.settings_use_batching) {
		// Render using the batched result
		int num_joined_items = bdata.items_joined.size();
		for (int j = 0; j < num_joined_items; j++) {
			const RasterizerCanvasBatcherGLES1::BItemJoined &joined_item = bdata.items_joined[j];
			Item *first_item = bdata.item_refs[joined_item.first_item_ref].item;

			// Make sure we set current clip for scissor testing.
			if (ris.current_clip != first_item->final_clip_owner || reclip) {
				ris.current_clip = first_item->final_clip_owner;
				if (ris.current_clip) {
					glEnable(GL_SCISSOR_TEST);
					int x = ris.current_clip->final_clip_rect.position.x;
					int y = ris.current_clip->final_clip_rect.position.y;
					int w = ris.current_clip->final_clip_rect.size.x;
					int h = ris.current_clip->final_clip_rect.size.y;
					glScissor(x, y, w, h);
				} else {
					glDisable(GL_SCISSOR_TEST);
				}
				reclip = false;
			}

			// Extract the CanvasMaterialData
			GLES1::CanvasMaterialData *mat_data = nullptr;
			if (first_item && first_item->material.is_valid()) {
				GLES1::Material *material = GLES1::MaterialStorage::get_singleton()->get_material(first_item->material);
				if (material && material->data) {
					mat_data = static_cast<GLES1::CanvasMaterialData *>(material->data);

					if (mat_data->shader_data && mat_data->shader_data->uses_time) {
						time_used = true;
					}
				}
			}

			// Pass the joined_item and the extracted material into the render step
			render_joined_item_commands(joined_item, ris.current_clip, reclip, mat_data, false);
		}
	} else {
		// Legacy / Immediate render fallback
		Item *ci = p_item_list;
		while (ci) {
			if (ris.current_clip != ci->final_clip_owner || reclip) {
				ris.current_clip = ci->final_clip_owner;
				if (ris.current_clip) {
					glEnable(GL_SCISSOR_TEST);
					int x = ris.current_clip->final_clip_rect.position.x;
					int y = ris.current_clip->final_clip_rect.position.y;
					int w = ris.current_clip->final_clip_rect.size.x;
					int h = ris.current_clip->final_clip_rect.size.y;
					glScissor(x, y, w, h);
				} else {
					glDisable(GL_SCISSOR_TEST);
				}
				reclip = false;
			}

			// Extract the CanvasMaterialData for legacy rendering too.
			GLES1::CanvasMaterialData *mat_data = nullptr;
			if (ci->material.is_valid()) {
				GLES1::Material *material = GLES1::MaterialStorage::get_singleton()->get_material(ci->material);
				if (material && material->data) {
					mat_data = static_cast<GLES1::CanvasMaterialData *>(material->data);
				}

				if (mat_data && mat_data->shader_data && mat_data->shader_data->uses_time) {
					time_used = true;
				}
			}

			_legacy_canvas_item_render_commands(ci, ris.current_clip, reclip, mat_data);
			ci = ci->next;
		}
	}

	// Clean up scissor test if it was left enabled by a clip
	if (ris.current_clip && !reclip) {
		glDisable(GL_SCISSOR_TEST);
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

void RasterizerCanvasGLES1::_batch_upload_buffers() {
	if (bdata.vertices.size() == 0) {
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);

	if (bdata.fvf == BatcherEnums::FVF_UNBATCHED || bdata.fvf == BatcherEnums::FVF_REGULAR) {
		uint32_t buffer_bytes = bdata.vertices.size() * sizeof(BatchVertex);
		glBufferSubData(GL_ARRAY_BUFFER, 0, buffer_bytes, bdata.vertices.get_data());
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
		uint32_t buffer_bytes = bdata.vertices.size() * sizeof_vert;
		ERR_FAIL_COND_MSG(buffer_bytes > bdata.vertex_buffer_size_bytes, "GLES1: Batch vertex buffer payload exceeds VRAM allocation! Aborting upload.");
		glBufferSubData(GL_ARRAY_BUFFER, 0, buffer_bytes, bdata.unit_vertices.get_data());
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES1::_batch_render_generic(const Batch &p_batch, GLES1::CanvasMaterialData *p_material) {
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

	_set_texture_rect_mode(false, use_light_angles, use_modulate, use_large_verts);

	const BatchTex &tex = bdata.batch_textures[p_batch.batch_texture_id];

	if (tex.tile_mode == BatchTex::TILE_FORCE_REPEAT) {
		state.specialization |= CanvasShaderGLES1::USE_FORCE_REPEAT;
	}

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
	if (rebind) {
		_set_canvas_uniforms();
	}
	if (p_material) {
		p_material->bind_uniforms();
	}

	// Determine Texture Filter and Repeat from the Batch FVF flags
	RS::CanvasItemTextureRepeat repeat = (tex.tile_mode != BatchTex::TILE_OFF) ? RS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED : state.default_repeat;
	RS::CanvasItemTextureFilter filter = state.default_filter;

	_bind_canvas_texture(tex.RID_texture, filter, repeat);

	// Bind the massive dynamic buffer
	glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)offsetof(BatchVertex, pos));

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)offsetof(BatchVertex, uv));

	if (!colored_verts) {
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(p_batch.color.r, p_batch.color.g, p_batch.color.b, p_batch.color.a);
	} else {
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_FLOAT, sizeof_vert, (const void *)(uintptr_t)offsetof(BatchVertexColored, col));
	}

	// Send Pixel Size to the shader (using the state updated by _bind_canvas_texture)
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	switch (p_batch.type) {
		case BatcherEnums::BT_RECT: {
			// A quad is 4 vertices. The index buffer has 6 indices per quad (2 triangles).
			// 6 indices * 2 bytes (GL_UNSIGNED_SHORT) = 12 bytes per quad.
			// first_vert / 4 * 12 == first_vert * 3. Lawnjelly's math is brilliant.
			int64_t offset = p_batch.first_vert * 3;
			int num_elements = p_batch.num_commands * 6;
			glDrawElements(GL_TRIANGLES, num_elements, GL_UNSIGNED_SHORT, (const void *)(uintptr_t)offset);
		} break;
		case BatcherEnums::BT_POLY: {
			int64_t offset = p_batch.first_vert;
			int num_elements = p_batch.num_commands;
			glDrawArrays(GL_TRIANGLES, offset, num_elements);
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
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES1::render_batches(Item::Command *const *p_commands, Item *p_current_clip, bool &r_reclip, GLES1::CanvasMaterialData *p_material) {
	int num_batches = bdata.batches.size();

	Transform2D base_extra = state.uniforms.extra_matrix;

	for (int batch_num = 0; batch_num < num_batches; batch_num++) {
		const Batch &batch = bdata.batches[batch_num];

		// Reset specialization
		state.specialization = 0;
		state.mode_variant = CanvasShaderGLES1::ShaderVariant::MODE_QUAD;
		state.shader_version = data.canvas_shader_default_version;

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
					state.uniforms.final_modulate = batch.item->final_modulate;
					state.uniforms.extra_matrix = Transform2D();
				}

				Item::Command *command = batch.first_command;
				for (uint32_t i = 0; i < batch.num_commands && command != nullptr; i++, command = command->next) {
					switch (command->type) {
						case Item::Command::TYPE_RECT: {
							Item::CommandRect *r = static_cast<Item::CommandRect *>(command);

							// Clean state
							// (so that rubbish/garbage doesn't ruin stuff later)
							glDisableClientState(GL_COLOR_ARRAY);
							glDisableClientState(GL_TEXTURE_COORD_ARRAY);
							glColor4fv(r->modulate.components);

							// Texture binding
							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}
							if (r->flags & CANVAS_RECT_TILE) {
								repeat = RS::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED;
							}

							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
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
							glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
							glEnableClientState(GL_VERTEX_ARRAY);
							glVertexPointer(2, GL_FLOAT, 0, nullptr);

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
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::DST_RECT, Color(dst_rect.position.x, dst_rect.position.y, dst_rect.size.x, dst_rect.size.y), state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::SRC_RECT, Color(src_rect.position.x, src_rect.position.y, src_rect.size.x, src_rect.size.y), state.shader_version, state.mode_variant, state.specialization);

							glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

							// Cleanup
							glBindBuffer(GL_ARRAY_BUFFER, 0);
							state.specialization &= ~(CanvasShaderGLES1::USE_FORCE_REPEAT);
						} break;

						case Item::Command::TYPE_NINEPATCH: {
							Item::CommandNinePatch *np = static_cast<Item::CommandNinePatch *>(command);

							_set_texture_rect_mode(false);

							bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							glDisableClientState(GL_COLOR_ARRAY);
							glColor4fv(np->color.components);

							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}
							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = p_current_clip->texture_filter;
							}

							_bind_canvas_texture(np->texture, filter, repeat);

							if (state.texpixel_size == Size2(0.0, 0.0)) {
								continue;
							}

							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

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

							float buffer[16 * 2 + 16 * 2] = {};
							{
								// first row

								buffer[(0 * 4 * 4) + 0] = np->rect.position.x;
								buffer[(0 * 4 * 4) + 1] = np->rect.position.y;

								buffer[(0 * 4 * 4) + 2] = source.position.x * state.texpixel_size.x;
								buffer[(0 * 4 * 4) + 3] = source.position.y * state.texpixel_size.y;

								buffer[(0 * 4 * 4) + 4] = np->rect.position.x + np->margin[SIDE_LEFT] * screen_scale;
								buffer[(0 * 4 * 4) + 5] = np->rect.position.y;

								buffer[(0 * 4 * 4) + 6] = (source.position.x + np->margin[SIDE_LEFT]) * state.texpixel_size.x;
								buffer[(0 * 4 * 4) + 7] = source.position.y * state.texpixel_size.y;

								buffer[(0 * 4 * 4) + 8] = np->rect.position.x + np->rect.size.x - np->margin[SIDE_RIGHT] * screen_scale;
								buffer[(0 * 4 * 4) + 9] = np->rect.position.y;

								buffer[(0 * 4 * 4) + 10] = (source.position.x + source.size.x - np->margin[SIDE_RIGHT]) * state.texpixel_size.x;
								buffer[(0 * 4 * 4) + 11] = source.position.y * state.texpixel_size.y;

								buffer[(0 * 4 * 4) + 12] = np->rect.position.x + np->rect.size.x;
								buffer[(0 * 4 * 4) + 13] = np->rect.position.y;

								buffer[(0 * 4 * 4) + 14] = (source.position.x + source.size.x) * state.texpixel_size.x;
								buffer[(0 * 4 * 4) + 15] = source.position.y * state.texpixel_size.y;

								// second row

								buffer[(1 * 4 * 4) + 0] = np->rect.position.x;
								buffer[(1 * 4 * 4) + 1] = np->rect.position.y + np->margin[SIDE_TOP] * screen_scale;

								buffer[(1 * 4 * 4) + 2] = source.position.x * state.texpixel_size.x;
								buffer[(1 * 4 * 4) + 3] = (source.position.y + np->margin[SIDE_TOP]) * state.texpixel_size.y;

								buffer[(1 * 4 * 4) + 4] = np->rect.position.x + np->margin[SIDE_LEFT] * screen_scale;
								buffer[(1 * 4 * 4) + 5] = np->rect.position.y + np->margin[SIDE_TOP] * screen_scale;

								buffer[(1 * 4 * 4) + 6] = (source.position.x + np->margin[SIDE_LEFT]) * state.texpixel_size.x;
								buffer[(1 * 4 * 4) + 7] = (source.position.y + np->margin[SIDE_TOP]) * state.texpixel_size.y;

								buffer[(1 * 4 * 4) + 8] = np->rect.position.x + np->rect.size.x - np->margin[SIDE_RIGHT] * screen_scale;
								buffer[(1 * 4 * 4) + 9] = np->rect.position.y + np->margin[SIDE_TOP] * screen_scale;

								buffer[(1 * 4 * 4) + 10] = (source.position.x + source.size.x - np->margin[SIDE_RIGHT]) * state.texpixel_size.x;
								buffer[(1 * 4 * 4) + 11] = (source.position.y + np->margin[SIDE_TOP]) * state.texpixel_size.y;

								buffer[(1 * 4 * 4) + 12] = np->rect.position.x + np->rect.size.x;
								buffer[(1 * 4 * 4) + 13] = np->rect.position.y + np->margin[SIDE_TOP] * screen_scale;

								buffer[(1 * 4 * 4) + 14] = (source.position.x + source.size.x) * state.texpixel_size.x;
								buffer[(1 * 4 * 4) + 15] = (source.position.y + np->margin[SIDE_TOP]) * state.texpixel_size.y;

								// third row

								buffer[(2 * 4 * 4) + 0] = np->rect.position.x;
								buffer[(2 * 4 * 4) + 1] = np->rect.position.y + np->rect.size.y - np->margin[SIDE_BOTTOM] * screen_scale;

								buffer[(2 * 4 * 4) + 2] = source.position.x * state.texpixel_size.x;
								buffer[(2 * 4 * 4) + 3] = (source.position.y + source.size.y - np->margin[SIDE_BOTTOM]) * state.texpixel_size.y;

								buffer[(2 * 4 * 4) + 4] = np->rect.position.x + np->margin[SIDE_LEFT] * screen_scale;
								buffer[(2 * 4 * 4) + 5] = np->rect.position.y + np->rect.size.y - np->margin[SIDE_BOTTOM] * screen_scale;

								buffer[(2 * 4 * 4) + 6] = (source.position.x + np->margin[SIDE_LEFT]) * state.texpixel_size.x;
								buffer[(2 * 4 * 4) + 7] = (source.position.y + source.size.y - np->margin[SIDE_BOTTOM]) * state.texpixel_size.y;

								buffer[(2 * 4 * 4) + 8] = np->rect.position.x + np->rect.size.x - np->margin[SIDE_RIGHT] * screen_scale;
								buffer[(2 * 4 * 4) + 9] = np->rect.position.y + np->rect.size.y - np->margin[SIDE_BOTTOM] * screen_scale;

								buffer[(2 * 4 * 4) + 10] = (source.position.x + source.size.x - np->margin[SIDE_RIGHT]) * state.texpixel_size.x;
								buffer[(2 * 4 * 4) + 11] = (source.position.y + source.size.y - np->margin[SIDE_BOTTOM]) * state.texpixel_size.y;

								buffer[(2 * 4 * 4) + 12] = np->rect.position.x + np->rect.size.x;
								buffer[(2 * 4 * 4) + 13] = np->rect.position.y + np->rect.size.y - np->margin[SIDE_BOTTOM] * screen_scale;

								buffer[(2 * 4 * 4) + 14] = (source.position.x + source.size.x) * state.texpixel_size.x;
								buffer[(2 * 4 * 4) + 15] = (source.position.y + source.size.y - np->margin[SIDE_BOTTOM]) * state.texpixel_size.y;

								// fourth row

								buffer[(3 * 4 * 4) + 0] = np->rect.position.x;
								buffer[(3 * 4 * 4) + 1] = np->rect.position.y + np->rect.size.y;

								buffer[(3 * 4 * 4) + 2] = source.position.x * state.texpixel_size.x;
								buffer[(3 * 4 * 4) + 3] = (source.position.y + source.size.y) * state.texpixel_size.y;

								buffer[(3 * 4 * 4) + 4] = np->rect.position.x + np->margin[SIDE_LEFT] * screen_scale;
								buffer[(3 * 4 * 4) + 5] = np->rect.position.y + np->rect.size.y;

								buffer[(3 * 4 * 4) + 6] = (source.position.x + np->margin[SIDE_LEFT]) * state.texpixel_size.x;
								buffer[(3 * 4 * 4) + 7] = (source.position.y + source.size.y) * state.texpixel_size.y;

								buffer[(3 * 4 * 4) + 8] = np->rect.position.x + np->rect.size.x - np->margin[SIDE_RIGHT] * screen_scale;
								buffer[(3 * 4 * 4) + 9] = np->rect.position.y + np->rect.size.y;

								buffer[(3 * 4 * 4) + 10] = (source.position.x + source.size.x - np->margin[SIDE_RIGHT]) * state.texpixel_size.x;
								buffer[(3 * 4 * 4) + 11] = (source.position.y + source.size.y) * state.texpixel_size.y;

								buffer[(3 * 4 * 4) + 12] = np->rect.position.x + np->rect.size.x;
								buffer[(3 * 4 * 4) + 13] = np->rect.position.y + np->rect.size.y;

								buffer[(3 * 4 * 4) + 14] = (source.position.x + source.size.x) * state.texpixel_size.x;
								buffer[(3 * 4 * 4) + 15] = (source.position.y + source.size.y) * state.texpixel_size.y;
							}

							glBindBuffer(GL_ARRAY_BUFFER, data.ninepatch_vertices);
							glBufferData(GL_ARRAY_BUFFER, sizeof(float) * (16 + 16) * 2, buffer, GL_DYNAMIC_DRAW);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ninepatch_elements);

							glEnableClientState(GL_VERTEX_ARRAY);
							glEnableClientState(GL_TEXTURE_COORD_ARRAY);
							glDisableClientState(GL_COLOR_ARRAY);

							glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), nullptr);
							glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), (const void *)(sizeof(float) * 2));

							glDrawElements(GL_TRIANGLES, 18 * 3 - (np->draw_center ? 0 : 6), GL_UNSIGNED_BYTE, nullptr);

							glBindBuffer(GL_ARRAY_BUFFER, 0);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						} break;

						case Item::Command::TYPE_CLIP_IGNORE: {
							Item::CommandClipIgnore *ci = static_cast<Item::CommandClipIgnore *>(command);
							if (p_current_clip) {
								if (ci->ignore != r_reclip) {
									if (ci->ignore) {
										glDisable(GL_SCISSOR_TEST);
										r_reclip = true;
									} else {
										glEnable(GL_SCISSOR_TEST);
										int x = p_current_clip->final_clip_rect.position.x;
										int y = p_current_clip->final_clip_rect.position.y;
										int w = p_current_clip->final_clip_rect.size.x;
										int h = p_current_clip->final_clip_rect.size.y;
										glScissor(x, y, w, h);
										r_reclip = false;
									}
								}
							}
						} break;

						case Item::Command::TYPE_POLYGON: {
							Item::CommandPolygon *polygon = static_cast<Item::CommandPolygon *>(command);

							if (polygon->primitive == RS::PRIMITIVE_TRIANGLES) {
								_legacy_draw_poly_triangles(polygon, p_material);
							}
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

							_set_canvas_uniforms();

							if (rebind && p_material) {
								p_material->bind_uniforms();
							}

							// Setup Texture, Filter, and Repeat
							RS::CanvasItemTextureRepeat repeat = state.default_repeat;
							if (p_current_clip && p_current_clip->texture_repeat != RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT) {
								repeat = p_current_clip->texture_repeat;
							}
							RS::CanvasItemTextureFilter filter = state.default_filter;
							if (p_current_clip && p_current_clip->texture_filter != RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT) {
								filter = p_current_clip->texture_filter;
							}

							_bind_canvas_texture(mesh_cmd->texture, filter, repeat);
							if (state.texpixel_size != Size2(0.0, 0.0)) {
								state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							}

							// Fetch and Draw Mesh Data
							GLES1::Mesh *mesh_data = GLES1::MeshStorage::get_singleton()->get_mesh(mesh_cmd->mesh);
							if (mesh_data) {
								// Loop using the double pointer array
								for (uint32_t j = 0; j < mesh_data->surface_count; j++) {
									GLES1::Mesh::Surface *s = mesh_data->surfaces[j];

									glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);

									if (s->index_count > 0) {
										glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
									}

									// Setup vertex attributes from the cached Version struct
									if (s->version_count > 0 && s->versions) {
										GLES1::Mesh::Surface::Version *v = &s->versions[0];

										for (int k = 0; k < RS::ARRAY_MAX - 1; k++) {
											if (v->attribs[k].enabled) {
												if (k == RS::ARRAY_VERTEX) {
													glEnableClientState(GL_VERTEX_ARRAY);
													glVertexPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
												} else if (k == RS::ARRAY_COLOR) {
													glEnableClientState(GL_COLOR_ARRAY);
													glColorPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
												} else if (k == RS::ARRAY_TEX_UV) {
													glEnableClientState(GL_TEXTURE_COORD_ARRAY);
													glTexCoordPointer(v->attribs[k].size, v->attribs[k].type, v->attribs[k].stride, (const void *)(uintptr_t)v->attribs[k].offset);
												}
											} else {
												if (k == RS::ARRAY_VERTEX) {
													glDisableClientState(GL_VERTEX_ARRAY);
												} else if (k == RS::ARRAY_COLOR) {
													glDisableClientState(GL_COLOR_ARRAY);
													glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
												} else if (k == RS::ARRAY_TEX_UV) {
													glDisableClientState(GL_TEXTURE_COORD_ARRAY);
												}
											}
										}
									}

									// Draw call using the correct index count and vertex count
									if (s->index_count > 0) {
										bool needs_32_bit = s->vertex_count >= (1 << 16);

										if (unlikely(needs_32_bit && !GLES1::Config::get_singleton()->support_32_bits_indices)) {
											ERR_PRINT_ONCE("GLES1: Device does not support 32-bit indices for large 2D meshes. Skipping draw.");
										} else {
											GLenum index_type = needs_32_bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
											glDrawElements(s->primitive, s->index_count, index_type, nullptr);
										}
									} else {
										glDrawArrays(s->primitive, 0, s->vertex_count);
									}

									// Clean up attributes
									glDisableClientState(GL_VERTEX_ARRAY);
									glDisableClientState(GL_TEXTURE_COORD_ARRAY);
									glDisableClientState(GL_COLOR_ARRAY);
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
							state.canvas_shader->version_set_uniform(CanvasShaderGLES1::EXTRA_MATRIX, state.uniforms.extra_matrix, state.shader_version, state.mode_variant, state.specialization);
						} break;

						default: {
							// MULTIMESH, PARTICLES, etc.
							print_verbose("NOT IMPLEMENTED COMMAND TYPE:");
							print_verbose(command->type);
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
	glBufferData(GL_ARRAY_BUFFER, data.polygon_buffer_size, nullptr, GL_DYNAMIC_DRAW);

	uint32_t offset = 0;

	// Vertices
	glBufferSubData(GL_ARRAY_BUFFER, offset, vertex_size, p_vertices);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
	offset += vertex_size;

	// Colors
	if (p_colors) {
		glBufferSubData(GL_ARRAY_BUFFER, offset, color_size, p_colors);
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
		offset += color_size;
	} else {
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// UVs
	if (p_uvs) {
		glBufferSubData(GL_ARRAY_BUFFER, offset, uv_size, p_uvs);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
		offset += uv_size;
	} else {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	// For Gizmos, we often draw Lines, Triangles, or Points depending on p_points
	GLenum draw_mode = (p_points == 2) ? GL_LINES : ((p_points == 3) ? GL_TRIANGLES : GL_TRIANGLE_FAN);
	glDrawArrays(draw_mode, 0, p_points);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES1::_legacy_draw_primitive(Item::CommandPrimitive *p_pr, GLES1::CanvasMaterialData *p_material) {
	_set_texture_rect_mode(false);

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
	if (rebind) {
		_set_canvas_uniforms();
	}
	if (p_material) {
		p_material->bind_uniforms();
	}

	ERR_FAIL_COND(p_pr->point_count < 1);

	_bind_canvas_texture(p_pr->texture, state.default_filter, state.default_repeat);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
	_draw_gui_primitive(p_pr->point_count, p_pr->points, p_pr->colors, p_pr->uvs);
}

void RasterizerCanvasGLES1::_legacy_draw_line(Item::CommandPrimitive *p_pr, GLES1::CanvasMaterialData *p_material) {
	// Godot 4 uses TYPE_PRIMITIVE with 2 points to draw lines
	_legacy_draw_primitive(p_pr, p_material);
}

void RasterizerCanvasGLES1::_legacy_draw_poly_triangles(Item::CommandPolygon *p_poly, GLES1::CanvasMaterialData *p_material) {
	if (!polygon_cache.has(p_poly->polygon.polygon_id)) {
		return;
	}

	const PolyData &pd = polygon_cache[p_poly->polygon.polygon_id];
	if (pd.points.is_empty()) {
		return;
	}

	_set_texture_rect_mode(false);

	bool rebind = state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization);
	_set_canvas_uniforms();

	if (rebind && p_material) {
		p_material->bind_uniforms();
	}

	// Bind texture and get pixel size
	_bind_canvas_texture(p_poly->texture, state.default_filter, state.default_repeat);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES1::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	// Setup vertex attributes
	glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);

	uint32_t points_size = pd.points.size() * sizeof(Vector2);
	uint32_t uvs_size = pd.uvs.size() * sizeof(Vector2);

	// Only allocate VBO space for colors if there is actually one color per vertex
	uint32_t colors_size = (pd.colors.size() > 1) ? (pd.colors.size() * sizeof(Color)) : 0;

	// Orphan the buffer and upload new data
	uint32_t total_size = points_size + uvs_size + colors_size;
	if (total_size > data.polygon_buffer_size) {
		data.polygon_buffer_size = next_power_of_2(total_size);
	}
	glBufferData(GL_ARRAY_BUFFER, data.polygon_buffer_size, nullptr, GL_DYNAMIC_DRAW);

	uint32_t offset = 0;

	// Points
	glBufferSubData(GL_ARRAY_BUFFER, offset, points_size, pd.points.ptr());
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
	offset += points_size;

	// UVs
	if (uvs_size > 0) {
		glBufferSubData(GL_ARRAY_BUFFER, offset, uvs_size, pd.uvs.ptr());
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
		offset += uvs_size;
	} else {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	// Colors
	if (pd.colors.size() == 1) {
		// Flat colored polygon (e.g., editor backgrounds)
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(pd.colors[0].r, pd.colors[0].g, pd.colors[0].b, pd.colors[0].a);
	} else if (pd.colors.size() > 1) {
		// Vertex colored polygon
		glBufferSubData(GL_ARRAY_BUFFER, offset, colors_size, pd.colors.ptr());
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_FLOAT, 0, (const void *)(uintptr_t)offset);
	} else {
		// No color
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Default white
	}

	// Draw
	if (!pd.indices.is_empty()) {
		int index_count = pd.indices.size();

		// GLES1 strictly requires 16-bit indices.
		if (unlikely(pd.points.size() > 65535)) {
			ERR_PRINT_ONCE("GLES1: Cannot draw polygon with more than 65535 vertices due to 16-bit index hardware limits.");
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

		glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer_size, nullptr, GL_DYNAMIC_DRAW);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_size, indices_16.ptr());

		glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_SHORT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	} else {
		// Draw arrays if no indices
		glDrawArrays(GL_TRIANGLES, 0, pd.points.size());
	}

	// Cleanup
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterizerCanvasGLES1::_set_texture_rect_mode(bool p_texture_rect, bool p_light_angle, bool p_modulate, bool p_large_vertex) {
	// Switch the shader variant between Quad and Texture Rect
	state.mode_variant = p_texture_rect ? CanvasShaderGLES1::ShaderVariant::MODE_TEXTURE_RECT : CanvasShaderGLES1::ShaderVariant::MODE_QUAD;

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

	// TODO(GLES2): Add Lighting and Shadow specializations
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

RasterizerCanvasGLES1 *RasterizerCanvasGLES1::singleton = nullptr;

RasterizerCanvasGLES1 *RasterizerCanvasGLES1::get_singleton() {
	return singleton;
}

RasterizerCanvasGLES1::RasterizerCanvasGLES1() {
	singleton = this;

	initialize();

	state.canvas_shader = &GLES1::MaterialStorage::get_singleton()->shaders.canvas_shader;
	data.canvas_shader_default_version = state.canvas_shader->default_version;

	// Allocate the default white texture for untextured polygons/rects
	default_canvas_texture = GLES1::TextureStorage::get_singleton()->canvas_texture_allocate();
	GLES1::TextureStorage::get_singleton()->canvas_texture_initialize(default_canvas_texture);

	batch_constructor();
}

RasterizerCanvasGLES1::~RasterizerCanvasGLES1() {
	singleton = nullptr;

	GLES1::TextureStorage::get_singleton()->canvas_texture_free(default_canvas_texture);

	// Free buffers
	GLES1::Utilities::get_singleton()->buffer_free_data(data.canvas_quad_vertices);
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
