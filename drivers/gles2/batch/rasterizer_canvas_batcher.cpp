/**************************************************************************/
/*  rasterizer_canvas_batcher.cpp                                         */
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

#include "rasterizer_canvas_batcher.h"

#ifdef GLES2_ENABLED

#include "core/config/project_settings.h"
#include "drivers/gles2/rasterizer_canvas_gles2.h"

RasterizerCanvasGLES2 *RasterizerCanvasBatcher::get_this() {
	return static_cast<RasterizerCanvasGLES2 *>(this);
}

const RasterizerCanvasGLES2 *RasterizerCanvasBatcher::get_this() const {
	return static_cast<const RasterizerCanvasGLES2 *>(this);
}

void RasterizerCanvasBatcher::batch_constructor() {
	bdata.settings_use_batching = false; // TODO(GLES2): Force batching on for GLES2 when it works.
	use_nvidia_rect_workaround = false;
}

void RasterizerCanvasBatcher::batch_initialize() {
	bdata.settings_use_batching = GLOBAL_GET("rendering/2d/batching/use_batching");
	bdata.settings_max_join_item_commands = 16;
	bdata.settings_colored_vertex_format_threshold = 0.5f;
	bdata.settings_batch_buffer_num_verts = 16384;
	bdata.settings_scissor_lights = true;
	bdata.settings_scissor_threshold = 0.0f;
	bdata.settings_item_reordering_lookahead = 4;
	bdata.settings_ninepatch_mode = 0;

	bdata.settings_uv_contract = false;
	bdata.settings_uv_contract_amount = 0.0f;

	const int max_possible_quads = (65536 / 4) - 1;
	const int min_possible_quads = 8;

	int max_quads = bdata.settings_batch_buffer_num_verts / 4;
	max_quads = CLAMP(max_quads, min_possible_quads, max_possible_quads);

	uint32_t sizeof_batch_vert = sizeof(BatchVertex);

	bdata.max_quads = max_quads;
	bdata.vertex_buffer_size_units = max_quads * 4;
	bdata.index_buffer_size_units = max_quads * 6;

	const int max_verts = bdata.vertex_buffer_size_units;

	bdata.vertex_buffer_size_bytes = max_verts * sizeof_batch_vert;
	bdata.index_buffer_size_bytes = bdata.index_buffer_size_units * 2; // 16 bit inds

	// Pre-allocate the arrays
	bdata.vertices.create(max_verts);
	bdata.unit_vertices.create(max_verts, sizeof(BatchVertexLarge));

	bdata.light_angles.create(max_verts);
	bdata.vertex_colors.create(max_verts);
	bdata.vertex_modulates.create(max_verts);
	bdata.vertex_transforms.create(max_verts);

	bdata.batches.create(1024);
	bdata.batches_temp.create(bdata.batches.max_size());
	bdata.batch_textures.create(32);
}

void RasterizerCanvasBatcher::batch_canvas_begin() {
	bdata.frame_string = "";
}

void RasterizerCanvasBatcher::batch_canvas_render_items_begin(const Color &p_modulate, RendererCanvasRender::Light *p_light, const Transform2D &p_base_transform) {
	if (bdata.settings_flash_batching) {
		bdata.settings_use_batching = (Engine::get_singleton()->get_frames_drawn() % 2) == 0;
	}

	if (!bdata.settings_use_batching) {
		return;
	}

	_calculate_scissor_threshold_area();

	// Set up render item state for all Z-indexes
	_render_item_state.reset();
	_render_item_state.item_group_modulate = p_modulate;
	_render_item_state.item_group_light = p_light;
	_render_item_state.item_group_base_transform = p_base_transform;
	_render_item_state.light_region.reset();
	_render_item_state.join_batch_break = false;

	bdata.join_across_z_indices = true;

	int light_count = 0;
	while (p_light) {
		light_count++;
		// If there are Z-ranged lights, prevent joining across Z-indices to avoid visual artifacts
		if ((p_light->z_min != RS::CANVAS_ITEM_Z_MIN) || (p_light->z_max != RS::CANVAS_ITEM_Z_MAX)) {
			bdata.join_across_z_indices = false;
		}
		p_light = p_light->next_ptr;
	}

	if (light_count > 64) {
		_render_item_state.light_region.too_many_lights = true;
	}
}

void RasterizerCanvasBatcher::batch_canvas_render_items(RendererCanvasRender::Item *p_item_list, int p_z, const Color &p_modulate, RendererCanvasRender::Light *p_light, const Transform2D &p_base_transform) {
	if (bdata.settings_use_batching) {
		// Stage 1: Record items for sorting and joining. Rendering happens in _end().
		record_items(p_item_list, p_z);
	} else {
		// Legacy immediate render fallback
		get_this()->canvas_render_items_implementation(p_item_list, p_z, p_modulate, p_light, p_base_transform);
	}
}

void RasterizerCanvasBatcher::batch_canvas_end() {

}

void RasterizerCanvasBatcher::batch_canvas_render_items_end() {
	if (!bdata.settings_use_batching) {
		return;
	}

	join_sorted_items();

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
	if (bdata.diagnose_frame) {
		bdata.frame_string += "items\n";
	}
#endif

	// Stage 2: Trigger the actual rendering of the batched items.
	// We pass nullptr for the item list because the batcher already holds the joined items.
	get_this()->canvas_render_items_implementation(nullptr, 0, _render_item_state.item_group_modulate, _render_item_state.item_group_light, _render_item_state.item_group_base_transform);

	// Reset arrays for the next frame
	bdata.items_joined.reset();
	bdata.item_refs.reset();
	bdata.sort_items.reset();
}

void RasterizerCanvasBatcher::record_items(RendererCanvasRender::Item *p_item_list, int p_z) {
	while (p_item_list) {
		// Request a new sort item
		BSortItem *s = bdata.sort_items.request();
		
		// If the array is full, grow it and request again
		if (!s) {
			bdata.sort_items.grow();
			s = bdata.sort_items.request();
#ifdef DEBUG_ENABLED
			CRASH_COND_MSG(!s, "GLES2 Canvas Batcher: Failed to allocate BSortItem even after growing array.");
#endif
		}

		s->item = p_item_list;
		s->z_index = p_z;

		p_item_list = p_item_list->next;
	}
}

void RasterizerCanvasBatcher::join_sorted_items() {

}

void RasterizerCanvasBatcher::_calculate_scissor_threshold_area() {

}

void RasterizerCanvasBatcher::render_joined_item_commands(const BItemJoined &p_bij, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, GLES2::Material *p_material, bool p_lit) {
}

void RasterizerCanvasBatcher::_legacy_canvas_item_render_commands(RendererCanvasRender::Item* p_item, RendererCanvasRender::Item* p_current_clip, bool& r_reclip, GLES2::CanvasMaterialData* p_material) {
	if (!p_item || !p_item->commands) {
		return;
	}

	// Reset the batch list for this single item
	bdata.batches.reset();

	// Create a single "unbatched" default batch
	Batch *batch = _batch_request_new();
	batch->type = BatcherEnums::BT_DEFAULT;
	batch->first_command = p_item->commands;
	batch->num_commands = godot4_commands_count(p_item->commands);

	get_this()->state.uniforms.modelview_matrix = p_item->final_transform;
	get_this()->state.uniforms.final_modulate = p_item->final_modulate;

	// Send this directly to the OpenGL execution loop
	get_this()->render_batches(nullptr, p_current_clip, r_reclip, p_material);
}

#endif // GLES2_ENABLED
