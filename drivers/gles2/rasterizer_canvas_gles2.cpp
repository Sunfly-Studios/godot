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

_FORCE_INLINE_ static uint32_t _indices_to_primitives(RS::PrimitiveType p_primitive, uint32_t p_indices) {
    return 0;
}

RID RasterizerCanvasGLES2::light_create() {
    return RID();
}

void RasterizerCanvasGLES2::light_set_texture(RID p_rid, RID p_texture) {

}

void RasterizerCanvasGLES2::light_set_use_shadow(RID p_rid, bool p_enable) {

}

void RasterizerCanvasGLES2::light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) {

}

void RasterizerCanvasGLES2::light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) {

}

void RasterizerCanvasGLES2::render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) {

}

RID RasterizerCanvasGLES2::occluder_polygon_create() {
    return RID();
}

void RasterizerCanvasGLES2::occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) {

}

void RasterizerCanvasGLES2::occluder_polygon_set_cull_mode(RID p_occluder, RS::CanvasOccluderPolygonCullMode p_mode) {

}

void RasterizerCanvasGLES2::set_shadow_texture_size(int p_size) {

}

bool RasterizerCanvasGLES2::free(RID p_rid) {
	return true;
}

void RasterizerCanvasGLES2::initialize() {
	print_verbose("GLES2: Initializing Canvas Renderer");

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

void RasterizerCanvasGLES2::update() {

}

void RasterizerCanvasGLES2::_bind_canvas_texture(RID p_texture, RS::CanvasItemTextureFilter p_base_filter, RS::CanvasItemTextureRepeat p_base_repeat) {
	if (p_texture == RID()) {
		state.current_tex = RID();
		state.current_tex_is_rt = false;
		state.texpixel_size = Size2(1, 1);

		glActiveTexture(GL_TEXTURE0);
		RID white_tex_rid = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
		GLES2::Texture *tex_white = GLES2::TextureStorage::get_singleton()->get_texture(white_tex_rid);
		
		if (tex_white) {
			glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
		}
		return;
	}

	GLES2::Texture *tex = GLES2::TextureStorage::get_singleton()->get_texture(p_texture);
	if (tex) {
		state.current_tex = p_texture;
		state.current_tex_is_rt = tex->is_render_target;
		state.texpixel_size = Size2(1.0 / tex->width, 1.0 / tex->height);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex->tex_id);

		// TODO(GLES2): Apply p_base_filter and p_base_repeat via glTexParameteri here.
	}
}

void RasterizerCanvasGLES2::_set_canvas_uniforms() {
	// These rely on the generated canvas.glsl.gen.h Uniforms enum and version_set_uniform
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::PROJECTION_MATRIX, state.uniforms.projection_matrix, data.canvas_shader_default_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::MODELVIEW_MATRIX, state.uniforms.modelview_matrix, data.canvas_shader_default_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::EXTRA_MATRIX, state.uniforms.extra_matrix, data.canvas_shader_default_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::FINAL_MODULATE, state.uniforms.final_modulate, data.canvas_shader_default_version, state.mode_variant, state.specialization);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::TIME, state.time, data.canvas_shader_default_version, state.mode_variant, state.specialization);

	if (state.render_target != RID()) {
		GLES2::RenderTarget *rt = GLES2::TextureStorage::get_singleton()->get_render_target(state.render_target);
		if (rt) {
			Vector2 screen_pixel_size;
			screen_pixel_size.x = 1.0 / rt->size.width;
			screen_pixel_size.y = 1.0 / rt->size.height;
			state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SCREEN_PIXEL_SIZE, screen_pixel_size, data.canvas_shader_default_version, state.mode_variant, state.specialization);
		}
	}

	// TODO(GLES2): Add skeleton and lighting uniforms back in later once basic rects work.
}

void RasterizerCanvasGLES2::canvas_begin(RID p_to_render_target, bool p_to_backbuffer) {
	batch_canvas_begin();

	state.render_target = p_to_render_target;
	state.specialization = 0;
	state.mode_variant = CanvasShaderGLES2::ShaderVariant::MODE_QUAD;

	GLES2::RenderTarget *render_target = GLES2::TextureStorage::get_singleton()->get_render_target(p_to_render_target);

	if (render_target) {
		render_target->was_used = true;
	}

	// Bind the correct Framebuffer
	if (p_to_backbuffer) {
		glBindFramebuffer(GL_FRAMEBUFFER, render_target ? render_target->backbuffer_fbo : 0);
		// TODO(GLES2): Likely need to implement the config->max_texture_image_units logic later
		// to do screen-reading shaders.
		glActiveTexture(GL_TEXTURE0);
		GLES2::Texture *tex = GLES2::TextureStorage::get_singleton()->get_texture(default_canvas_texture);
		if (tex) {
			glBindTexture(GL_TEXTURE_2D, tex->tex_id);
		}
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, render_target ? render_target->fbo : 0);
		glActiveTexture(GL_TEXTURE0);
		if (render_target) {
			glBindTexture(GL_TEXTURE_2D, render_target->backbuffer);
		}
	}

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
		render_target_size = GLES2::TextureStorage::get_singleton()->render_target_get_size(p_to_render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}
	glViewport(0, 0, render_target_size.x, render_target_size.y);

	reset_canvas();

	// Bind the default white texture so untextured items draw correctly
	glActiveTexture(GL_TEXTURE0);
	RID white_tex_rid = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
	GLES2::Texture *tex_white = GLES2::TextureStorage::get_singleton()->get_texture(white_tex_rid);

	if (tex_white) {
		glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
	}

	glVertexAttrib4f(RS::ARRAY_COLOR, 1, 1, 1, 1);
	glDisableVertexAttribArray(RS::ARRAY_COLOR);

	// Calculate Projection Matrix
	// Godot 4 seems to expect 3D transforms even in 2D.
	Transform3D canvas_transform;
	if (state.render_target != RID() && render_target) {
		float csy = 1.0;
		// TODO(GLES2): Handle RENDER_TARGET_VFLIP if needed
		Size2i rt_size = render_target->size;
		Transform3D translate(Basis(), Vector3(-(rt_size.width / 2.0f), -(rt_size.height / 2.0f), 0.0f));
		Transform3D scale(Basis().scaled(Vector3(2.0f / rt_size.width, csy * -2.0f / rt_size.height, 1.0f)), Vector3());
		canvas_transform = scale * translate;
	} else {
		Vector2 ssize = DisplayServer::get_singleton()->window_get_size();
		Transform3D translate(Basis(), Vector3(-(ssize.width / 2.0f), -(ssize.height / 2.0f), 0.0f));
		Transform3D scale(Basis().scaled(Vector3(2.0f / ssize.width, -2.0f / ssize.height, 1.0f)), Vector3());
		canvas_transform = scale * translate;
	}

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

	for (int i = 0; i < RS::ARRAY_MAX; i++) {
		glDisableVertexAttribArray(i);
	}

	Size2 render_target_size;
	if (state.render_target != RID()) {
		render_target_size = GLES2::TextureStorage::get_singleton()->render_target_get_size(state.render_target);
	} else {
		render_target_size = DisplayServer::get_singleton()->window_get_size();
	}

	glViewport(0, 0, render_target_size.x, render_target_size.y);
	glScissor(0, 0, render_target_size.x, render_target_size.y);

	state.using_skeleton = false;
	state.using_ninepatch = false;
}

void RasterizerCanvasGLES2::reset_canvas() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DITHER);
	glEnable(GL_BLEND);

	bool transparent_rt = state.render_target != RID() && GLES2::TextureStorage::get_singleton()->get_render_target(state.render_target)->is_transparent;

	if (transparent_rt) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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

	// Kick off the batcher loops
	canvas_render_items_begin(p_modulate, p_light_list, p_canvas_transform);
	
	// TODO(GLES2): Hardcoding p_z to 0 for now until Z-layer sorting is fully ported
	canvas_render_items_internal(p_item_list, 0, p_modulate, p_light_list, p_canvas_transform);
	
	canvas_render_items_end();

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

	// Bind default white texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	RID white_tex_rid = GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
	GLES2::Texture *tex_white = GLES2::TextureStorage::get_singleton()->get_texture(white_tex_rid);

	if (tex_white) {
		glBindTexture(GL_TEXTURE_2D, tex_white->tex_id);
	}

	if (bdata.settings_use_batching) {
		// Render using the batched result
		int num_joined_items = bdata.items_joined.size();
		for (int j = 0; j < num_joined_items; j++) {
			bool reclip = false;
			// TODO(GLES2): Pass nullptr for material and false for lit right now.
			// Handle actual materials inside the prefill step.
			render_joined_item_commands(bdata.items_joined[j], ris.current_clip, reclip, nullptr, false);
		}
	} else {
		// Legacy / Immediate render fallback
		while (p_item_list) {
			Item *ci = p_item_list;
			bool reclip = false;
			_legacy_canvas_item_render_commands(ci, ris.current_clip, reclip, nullptr);
			p_item_list = p_item_list->next;
		}
	}

	// Clean up scissor test if it was left enabled by a clip
	if (ris.current_clip) {
		glDisable(GL_SCISSOR_TEST);
	}
}

void RasterizerCanvasGLES2::canvas_render_items_internal(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform) {
	batch_canvas_render_items(p_item_list, p_z, p_modulate, p_light, p_base_transform);
}

void RasterizerCanvasGLES2::canvas_render_items_end() {
	batch_canvas_render_items_end();
}

void RasterizerCanvasGLES2::_batch_upload_buffers() {
	// TODO(GLES2): Implement dynamic vertex buffers (automatically uploaded to the GPU)
}

void RasterizerCanvasGLES2::render_batches(Item::Command *const *p_commands, Item *p_current_clip, bool &r_reclip, GLES2::CanvasMaterialData *p_material) {
	int num_batches = bdata.batches.size();

	for (int batch_num = 0; batch_num < num_batches; batch_num++) {
		const Batch &batch = bdata.batches[batch_num];

		// Reset specialization
		state.specialization = 0;
		state.mode_variant = CanvasShaderGLES2::ShaderVariant::MODE_QUAD;
		state.shader_version = data.canvas_shader_default_version;

		if (p_material && p_material->shader_data) {
			if (p_material->shader_data->version.is_valid() && p_material->shader_data->valid) {
				state.shader_version = p_material->shader_data->version;
			}
		}

		switch (batch.type) {
			case BatcherEnums::BT_DEFAULT: {
				Item::Command *command = batch.first_command;
				for (uint32_t i = 0; i < batch.num_commands; i++, command = command->next) {
					switch (command->type) {
						case Item::Command::TYPE_RECT: {
							Item::CommandRect *r = static_cast<Item::CommandRect *>(command);

							glDisableVertexAttribArray(RS::ARRAY_COLOR);
							glVertexAttrib4fv(RS::ARRAY_COLOR, r->modulate.components);

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

							bool upside_down = r->flags & CANVAS_RECT_FLIP_V;
							if (state.current_tex_is_rt) {
								upside_down = true;
							}

							_set_texture_rect_mode(true);

							if (state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization)) {
								_set_canvas_uniforms();
								if (p_material) {
									p_material->bind_uniforms();
								}
							}

							// Bind the default quad buffer
							glBindBuffer(GL_ARRAY_BUFFER, data.canvas_quad_vertices);
							glEnableVertexAttribArray(RS::ARRAY_VERTEX);
							glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

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
								src_rect.size.x *= -1;
							}
							if (upside_down) {
								src_rect.size.y *= -1;
							}
							if (r->flags & CANVAS_RECT_TRANSPOSE) {
								dst_rect.size.x *= -1;
							}

							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::DST_RECT, Color(dst_rect.position.x, dst_rect.position.y, dst_rect.size.x, dst_rect.size.y), state.shader_version, state.mode_variant, state.specialization);
							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::SRC_RECT, Color(src_rect.position.x, src_rect.position.y, src_rect.size.x, src_rect.size.y), state.shader_version, state.mode_variant, state.specialization);

							glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

							glBindBuffer(GL_ARRAY_BUFFER, 0);

							state.specialization &= ~(CanvasShaderGLES2::USE_FORCE_REPEAT);
						} break;

						case Item::Command::TYPE_NINEPATCH: {
							Item::CommandNinePatch *np = static_cast<Item::CommandNinePatch *>(command);

							_set_texture_rect_mode(false);
							if (state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization)) {
								_set_canvas_uniforms();
								if (p_material) {
									p_material->bind_uniforms();
								}
							}

							glDisableVertexAttribArray(RS::ARRAY_COLOR);
							glVertexAttrib4fv(RS::ARRAY_COLOR, np->color.components);

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

							state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

							Rect2 source = np->source;
							if (source.size.x == 0 && source.size.y == 0) {
								source.size.x = state.texture_size.x;
								source.size.y = state.texture_size.y;
							}

							float screen_scale = 1.0;
							if ((bdata.settings_ninepatch_mode == 1) && (source.size.x != 0) && (source.size.y != 0)) {
								screen_scale = MIN(np->rect.size.x / source.size.x, np->rect.size.y / source.size.y);
								screen_scale = MIN(1.0, screen_scale);
							}

							float buffer[16 * 2 + 16 * 2];
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

							glEnableVertexAttribArray(RS::ARRAY_VERTEX);
							glEnableVertexAttribArray(RS::ARRAY_TEX_UV);

							glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
							glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(sizeof(float) * 2));

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
										Size2i rt_size = GLES2::TextureStorage::get_singleton()->get_render_target(state.render_target)->size;
										int y = rt_size.height - (p_current_clip->final_clip_rect.position.y + p_current_clip->final_clip_rect.size.y);
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

						default: {
							// TODO(GLES2): Add support for TYPE_LINE, TYPE_POLYGON, TYPE_MESH, etc.
							print_verbose("What did Godot meant by this?");
						} break;
					}
				}
			} break;

			default:
				// TODO(GLES2): Accelerated batches (BT_RECT, BT_POLY) go here.
				break;
		}
	}
}

void RasterizerCanvasGLES2::_legacy_draw_poly_triangles(Item::CommandPolygon *p_poly, GLES2::CanvasMaterialData *p_material) {
	if (!polygon_cache.has(p_poly->polygon.polygon_id)) {
		return;
	}

	const PolyData &pd = polygon_cache[p_poly->polygon.polygon_id];
	if (pd.points.is_empty()) {
		return;
	}

	_set_texture_rect_mode(false);

	if (state.canvas_shader->version_bind_shader(state.shader_version, state.mode_variant, state.specialization)) {
		_set_canvas_uniforms();
		// TODO(GLES2): Material uniform binding goes here (if ready).
	}

	// Bind texture and get pixel size
	_bind_canvas_texture(p_poly->texture, state.default_filter, state.default_repeat);
	state.canvas_shader->version_set_uniform(CanvasShaderGLES2::COLOR_TEXPIXEL_SIZE, state.texpixel_size, state.shader_version, state.mode_variant, state.specialization);

	// Setup vertex attributes
	glBindBuffer(GL_ARRAY_BUFFER, data.polygon_buffer);

	uint32_t points_size = pd.points.size() * sizeof(Vector2);
	uint32_t uvs_size = pd.uvs.size() * sizeof(Vector2);

	// Only allocate VBO space for colors if there is actually one color per vertex
	uint32_t colors_size = (pd.colors.size() > 1) ? (pd.colors.size() * sizeof(Color)) : 0;

	// Orphan the buffer and upload new data
	glBufferData(GL_ARRAY_BUFFER, points_size + uvs_size + colors_size, nullptr, GL_DYNAMIC_DRAW);

	uint32_t offset = 0;

	// Points
	glBufferSubData(GL_ARRAY_BUFFER, offset, points_size, pd.points.ptr());
	glEnableVertexAttribArray(RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
	offset += points_size;

	// UVs
	if (uvs_size > 0) {
		glBufferSubData(GL_ARRAY_BUFFER, offset, uvs_size, pd.uvs.ptr());
		glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
		offset += uvs_size;
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	}

	// Colors
	if (pd.colors.size() == 1) {
		// Flat colored polygon (e.g., editor backgrounds)
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(RS::ARRAY_COLOR, pd.colors[0].r, pd.colors[0].g, pd.colors[0].b, pd.colors[0].a);
	} else if (pd.colors.size() > 1) {
		// Vertex colored polygon
		glBufferSubData(GL_ARRAY_BUFFER, offset, colors_size, pd.colors.ptr());
		glEnableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_FLOAT, GL_FALSE, 0, (const void *)(uintptr_t)offset);
	} else {
		// No color
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f); // Default white
	}

	// Draw
	if (!pd.indices.is_empty()) {
		// Upload indices
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.polygon_index_buffer);
		uint32_t index_size = pd.indices.size() * sizeof(int);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_size, pd.indices.ptr(), GL_DYNAMIC_DRAW);

		glDrawElements(GL_TRIANGLES, pd.indices.size(), GL_UNSIGNED_INT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	} else {
		// Draw arrays if no indices
		glDrawArrays(GL_TRIANGLES, 0, pd.points.size());
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
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

	// TODO(GLES2): Add Lighting and Shadow specializations
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

RasterizerCanvasGLES2 *RasterizerCanvasGLES2::singleton = nullptr;

RasterizerCanvasGLES2 *RasterizerCanvasGLES2::get_singleton() {
	return singleton;
}

RasterizerCanvasGLES2::RasterizerCanvasGLES2() {
	singleton = this;

	// The Canvas Shader is often managed by MaterialStorage,
	// but since we generated it locally, we'll instantiate it directly for the state.
	state.canvas_shader = memnew(CanvasShaderGLES2);
	state.canvas_shader->initialize();
	data.canvas_shader_default_version = state.canvas_shader->version_create();

	// Allocate the default white texture for untextured polygons/rects
	default_canvas_texture = GLES2::TextureStorage::get_singleton()->canvas_texture_allocate();
	GLES2::TextureStorage::get_singleton()->canvas_texture_initialize(default_canvas_texture);

	batch_constructor();
}

RasterizerCanvasGLES2::~RasterizerCanvasGLES2() {
	singleton = nullptr;

	state.canvas_shader->version_free(data.canvas_shader_default_version);
	memdelete(state.canvas_shader);

	GLES2::TextureStorage::get_singleton()->canvas_texture_free(default_canvas_texture);

	// Free buffers
	glDeleteBuffers(1, &data.canvas_quad_vertices);
	glDeleteBuffers(1, &data.ninepatch_vertices);
	glDeleteBuffers(1, &data.ninepatch_elements);
	glDeleteBuffers(1, &data.polygon_buffer);
	glDeleteBuffers(1, &data.polygon_index_buffer);

	// Free batcher buffers
	if (bdata.gl_vertex_buffer != 0) {
		glDeleteBuffers(1, &bdata.gl_vertex_buffer);
	}
	if (bdata.gl_index_buffer != 0) {
		glDeleteBuffers(1, &bdata.gl_index_buffer);
	}
}

#endif // GLES3_ENABLED
