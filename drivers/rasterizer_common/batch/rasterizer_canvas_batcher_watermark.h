/**************************************************************************/
/*  rasterizer_canvas_batcher_watermark.h                                 */
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

#ifndef RASTERIZER_CANVAS_BATCHER_WATERMARK
#define RASTERIZER_CANVAS_BATCHER_WATERMARK

#include "core/math/vector2.h"
#include "core/typedefs.h"

// Stage 1 (Joining) rollback watermark.
// Snapshots items_joined, item_refs, and the active BItemJoined before appending
// a sorted CanvasItem so an allocation failure rolls back the partial group cleanly
// without leaving dangling or out-of-bounds item references.
template <typename T_BATCH_DATA, typename T_ITEM_JOINED>
struct CanvasBatchStage1Watermark {
	int joined_size = 0;
	int item_refs_size = 0;
	T_ITEM_JOINED saved_curr_joined = {};
	bool has_curr_joined = false;

	_FORCE_INLINE_ void capture(const T_BATCH_DATA &p_bdata, const T_ITEM_JOINED *p_curr_joined) {
		joined_size = p_bdata.items_joined.size();
		item_refs_size = p_bdata.item_refs.size();
		has_curr_joined = (p_curr_joined != nullptr);
		if (has_curr_joined) {
			saved_curr_joined = *p_curr_joined;
		}
	}

	_FORCE_INLINE_ void rollback(T_BATCH_DATA &r_bdata, T_ITEM_JOINED *&r_curr_joined) const {
		r_bdata.items_joined.truncate(joined_size);
		r_bdata.item_refs.truncate(item_refs_size);
		if (has_curr_joined && joined_size > 0) {
			// Restore the previous tail group and rebind the pointer in case
			// an intervening grow() reallocated the backing array.
			r_bdata.items_joined[joined_size - 1] = saved_curr_joined;
			r_curr_joined = &r_bdata.items_joined[joined_size - 1];
		} else {
			r_curr_joined = nullptr;
		}
	}
};

// Stage 2 (Prefill) rollback watermark.
// Snapshots all parallel vertex/attribute arrays, batch descriptors, texture table,
// and FillState cursors before baking a command. If any mid-command allocation fails,
// rolling back keeps every parallel stream strictly synchronized so the caller can
// flush and retry without leaking vertices or desynchronizing FVF translation.
template <typename T_BATCH_DATA, typename T_FILL_STATE, typename T_BATCH>
struct CanvasBatchPrefillWatermark {
	int vertices_size = 0;
	int vertex_colors_size = 0;
	int vertex_modulates_size = 0;
	int vertex_transforms_size = 0;
	int light_angles_size = 0;
	int batches_size = 0;
	int batch_textures_size = 0;

	int total_verts = 0;
	int total_quads = 0;
	int total_color_changes = 0;
	bool use_light_angles = false;

	int curr_batch_index = -1;
	T_BATCH saved_curr_batch = {};
	int batch_tex_id = -1;
	uint32_t sequence_batch_type_flags = 0;
	Vector2 texpixel_size;
	bool contract_uvs = false;

	_FORCE_INLINE_ void capture(const T_BATCH_DATA &p_bdata, const T_FILL_STATE &p_fill_state) {
		vertices_size = p_bdata.vertices.size();
		vertex_colors_size = p_bdata.vertex_colors.size();
		vertex_modulates_size = p_bdata.vertex_modulates.size();
		vertex_transforms_size = p_bdata.vertex_transforms.size();
		light_angles_size = p_bdata.light_angles.size();
		batches_size = p_bdata.batches.size();
		batch_textures_size = p_bdata.batch_textures.size();

		total_verts = p_bdata.total_verts;
		total_quads = p_bdata.total_quads;
		total_color_changes = p_bdata.total_color_changes;
		use_light_angles = p_bdata.use_light_angles;

		batch_tex_id = p_fill_state.batch_tex_id;
		sequence_batch_type_flags = p_fill_state.sequence_batch_type_flags;
		texpixel_size = p_fill_state.texpixel_size;
		contract_uvs = p_fill_state.contract_uvs;

		if (p_fill_state.curr_batch == &p_fill_state.dummy_batch || p_fill_state.curr_batch == nullptr) {
			curr_batch_index = -1;
		} else {
			// Track by index rather than raw pointer so rollback remains valid
			// even if bdata.batches grew and reallocated before failing.
			curr_batch_index = batches_size - 1;
			if (curr_batch_index >= 0) {
				saved_curr_batch = *p_fill_state.curr_batch;
			}
		}
	}

	_FORCE_INLINE_ void rollback(T_BATCH_DATA &r_bdata, T_FILL_STATE &r_fill_state) const {
		r_bdata.vertices.truncate(vertices_size);
		r_bdata.vertex_colors.truncate(vertex_colors_size);
		r_bdata.vertex_modulates.truncate(vertex_modulates_size);
		r_bdata.vertex_transforms.truncate(vertex_transforms_size);
		r_bdata.light_angles.truncate(light_angles_size);
		r_bdata.batches.truncate(batches_size);
		r_bdata.batch_textures.truncate(batch_textures_size);

		r_bdata.total_verts = total_verts;
		r_bdata.total_quads = total_quads;
		r_bdata.total_color_changes = total_color_changes;
		r_bdata.use_light_angles = use_light_angles;

		r_fill_state.batch_tex_id = batch_tex_id;
		r_fill_state.sequence_batch_type_flags = sequence_batch_type_flags;
		r_fill_state.texpixel_size = texpixel_size;
		r_fill_state.contract_uvs = contract_uvs;

		if (curr_batch_index >= 0 && curr_batch_index < r_bdata.batches.size()) {
			r_bdata.batches[curr_batch_index] = saved_curr_batch;
			r_fill_state.curr_batch = &r_bdata.batches[curr_batch_index];
		} else {
			r_fill_state.curr_batch = &r_fill_state.dummy_batch;
		}
	}
};

#endif // RASTERIZER_CANVAS_BATCHER_WATERMARK
