/**************************************************************************/
/*  rasterizer_canvas_batcher_common.inc                                  */
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

#include "core/config/project_settings.h"

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_constructor() {
	use_nvidia_rect_workaround = false;
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_initialize() {
	bdata.settings_use_batching = GLOBAL_GET("rendering/2d/batching/use_batching");
	bdata.settings_max_join_item_commands = 16;
	bdata.settings_colored_vertex_format_threshold = 0.5f;
	bdata.settings_batch_buffer_num_verts = 16384;
	bdata.settings_scissor_lights = false;
	bdata.settings_scissor_threshold = -1.0f;
	bdata.settings_item_reordering_lookahead = 4;
	bdata.settings_ninepatch_mode = 0;

	bdata.settings_uv_contract = false;
	bdata.settings_uv_contract_amount = 0.0f;

	const int max_possible_quads = (65536 / 4) - 1;
	const int min_possible_quads = 8;

	int max_quads = bdata.settings_batch_buffer_num_verts / 4;
	max_quads = CLAMP(max_quads, min_possible_quads, max_possible_quads);

	uint32_t sizeof_batch_vert = sizeof(BatchVertexLarge);

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

	bdata.sort_items.create(16384);
	bdata.items_joined.create(16384);
	bdata.item_refs.create(16384);
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_canvas_begin() {
	bdata.frame_string = "";

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
	if (bdata.settings_diagnose_frame) {
		bdata.diagnose_frame = false;

		uint32_t tick = OS::get_singleton()->get_ticks_msec();
		uint64_t frame = Engine::get_singleton()->get_frames_drawn();

		if (tick >= bdata.next_diagnose_tick) {
			bdata.next_diagnose_tick = tick + 10000;

			// the plus one is prevent starting diagnosis half way through frame
			bdata.diagnose_frame_number = frame + 1;
		}

		if (frame == bdata.diagnose_frame_number) {
			bdata.diagnose_frame = true;
			bdata.reset_stats();
		}

		if (bdata.diagnose_frame) {
			bdata.frame_string = "canvas_begin FRAME " + itos(frame) + "\n";
		}
	}
#endif
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_canvas_render_items_begin(const Color &p_modulate, RendererCanvasRender::Light *p_light, const Transform2D &p_base_transform) {
	if (bdata.settings_flash_batching) {
		bdata.settings_use_batching = (Engine::get_singleton()->get_frames_drawn() % 2) == 0;
	}

	// Set up render item state for all Z-indexes
	_render_item_state.reset();
	_render_item_state.item_group_modulate = p_modulate;
	_render_item_state.item_group_light = p_light;
	_render_item_state.item_group_base_transform = p_base_transform;
	_render_item_state.light_region.reset();
	_render_item_state.join_batch_break = false;

	if (!bdata.settings_use_batching) {
		return;
	}

	_calculate_scissor_threshold_area();

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

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_canvas_render_items(RendererCanvasRender::Item *p_item_list, int p_z, const Color &p_modulate, RendererCanvasRender::Light *p_light, const Transform2D &p_base_transform) {
	if (bdata.settings_use_batching) {
		// Stage 1: Record items for sorting and joining. Rendering happens in _end().
		record_items(p_item_list, p_z);
	} else {
		// Legacy immediate render fallback
		get_this()->canvas_render_items_implementation(p_item_list, p_z, p_modulate, p_light, p_base_transform);
	}
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_canvas_end() {
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
	if (bdata.diagnose_frame) {
		bdata.frame_string += "canvas_end\n";
		if (bdata.stats_items_sorted) {
			bdata.frame_string += "\titems reordered: " + itos(bdata.stats_items_sorted) + "\n";
		}
		if (bdata.stats_light_items_joined) {
			bdata.frame_string += "\tlight items joined: " + itos(bdata.stats_light_items_joined) + "\n";
		}

		print_line(bdata.frame_string);
	}
#endif
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::batch_canvas_render_items_end() {
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

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::try_join_item(RendererCanvasRender::Item *p_ci, RenderItemState &r_ris, bool &r_batch_break) {
	bool join = true;
	r_batch_break = false;

	// Copy the final modulate since Godot calculates
	// this upstream
	r_ris.final_modulate = p_ci->final_modulate * r_ris.item_group_modulate;

	// Clip owner changes break batches
	if (p_ci->final_clip_owner != r_ris.current_clip) {
		join = false;
		r_ris.current_clip = p_ci->final_clip_owner;
	}

	// Material changes break batches
	if (p_ci->material != r_ris.canvas_last_material) {
		join = false;
		r_ris.canvas_last_material = p_ci->material;
	}

	if (_detect_item_batch_break(r_ris, p_ci, r_batch_break)) {
		r_batch_break = true;
		join = false;
	}

	return join;
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::record_items(RendererCanvasRender::Item *p_item_list, int p_z) {
	while (p_item_list) {
		// Request a new sort item
		BSortItem *s = bdata.sort_items.request();
		
		// If the array is full, grow it and request again
		if (!s) {
			bdata.sort_items.grow();
			s = bdata.sort_items.request();
			if (unlikely(!s)) {
				ERR_PRINT_ONCE("GLES Batcher: VRAM or Pool Memory exhausted. Some 2D geometry was dropped to prevent a crash.");
				break;
			}
		}

		s->item = p_item_list;
		s->z_index = p_z;

		p_item_list = p_item_list->next;
	}
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::join_sorted_items() {
	sort_items();

	int z = RS::CANVAS_ITEM_Z_MIN;
	_render_item_state.item_group_z = z;

	for (int s = 0; s < bdata.sort_items.size(); s++) {
		const BSortItem &si = bdata.sort_items[s];
		RendererCanvasRender::Item *ci = si.item;
		if (!ci) {
			continue;
		}

		if (si.z_index != z) {
			z = si.z_index;
			_render_item_state.item_group_z = z;

			if (!bdata.join_across_z_indices) {
				_render_item_state.join_batch_break = true;
			}
		}

		bool join = false;

		if (_render_item_state.join_batch_break) {
			join = false;
			try_join_item(ci, _render_item_state, _render_item_state.join_batch_break);
		} else {
			join = try_join_item(ci, _render_item_state, _render_item_state.join_batch_break);
		}

		if (!join || !_render_item_state.joined_item) {
			// Request new joined item (with automatic growth if full)
			BItemJoined *joined = bdata.items_joined.request();
			if (!joined) {
				bdata.items_joined.grow();
				joined = bdata.items_joined.request();
				if (unlikely(!joined)) {
					ERR_PRINT_ONCE("GLES Batcher: VRAM or Pool Memory exhausted. Some 2D geometry was dropped to prevent a crash.");
					continue; 
				}
			}
			_render_item_state.joined_item = joined;

			_render_item_state.joined_item->first_item_ref = bdata.item_refs.size();
			_render_item_state.joined_item->num_item_refs = 1;
			_render_item_state.joined_item->bounding_rect = ci->global_rect_cache;
			_render_item_state.joined_item->z_index = z;
			_render_item_state.joined_item->flags = bdata.joined_item_batch_flags;

			_render_item_state.joined_item_batch_type_flags_prev = _render_item_state.joined_item_batch_type_flags_curr;

			BItemRef *r = bdata.item_refs.request();
			if (!r) {
				bdata.item_refs.grow();
				r = bdata.item_refs.request();
				if (unlikely(!r)) {
					ERR_PRINT_ONCE("GLES Batcher: VRAM or Pool Memory exhausted. Some 2D geometry was dropped to prevent a crash.");
					continue; 
				}
			}
			r->item = ci;
			r->final_modulate = _render_item_state.final_modulate;
		} else {
			// Extend existing joined item
			_render_item_state.joined_item->num_item_refs += 1;
			_render_item_state.joined_item->bounding_rect = _render_item_state.joined_item->bounding_rect.merge(ci->global_rect_cache);

			BItemRef *r = bdata.item_refs.request();
			if (!r) {
				bdata.item_refs.grow();
				r = bdata.item_refs.request();
				if (unlikely(!r)) {
					ERR_PRINT_ONCE("GLES Batcher: VRAM or Pool Memory exhausted. Some 2D geometry was dropped to prevent a crash.");
					continue; 
				}
			}
			r->item = ci;
			r->final_modulate = _render_item_state.final_modulate;
		}
	}
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::sort_items() {
	if (!bdata.settings_item_reordering_lookahead) {
		return;
	}

	for (int s = 0; s < bdata.sort_items.size() - 2; s++) {
		if (sort_items_from(s)) {
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
			bdata.stats_items_sorted++;
#endif
		}
	}
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::_sort_items_match(const BSortItem &p_a, const BSortItem &p_b) const {
	const RendererCanvasRender::Item *a = p_a.item;
	const RendererCanvasRender::Item *b = p_b.item;

	ERR_FAIL_NULL_V(a, false);
	ERR_FAIL_NULL_V(b, false);

	// Fast check for "exactly 1 command" using linked lists
	if (!a->commands || a->commands->next != nullptr) {
		return false;
	}
	if (!b->commands || b->commands->next != nullptr) {
		return false;
	}

	// Ensure we do not swap items across different clipping boundaries
	if (a->final_clip_owner != b->final_clip_owner) {
		return false;
	}

	const RendererCanvasRender::Item::Command *ca = a->commands;
	const RendererCanvasRender::Item::Command *cb = b->commands;
	ERR_FAIL_NULL_V(ca, false);
	ERR_FAIL_NULL_V(cb, false);

	if (ca->type != cb->type) {
		return false;
	}

	// Check if textures match (allowing both RECT and POLY sorting)
	if (ca->type == RendererCanvasRender::Item::Command::TYPE_RECT) {
		const RendererCanvasRender::Item::CommandRect *comm_a = static_cast<const RendererCanvasRender::Item::CommandRect *>(ca);
		const RendererCanvasRender::Item::CommandRect *comm_b = static_cast<const RendererCanvasRender::Item::CommandRect *>(cb);
		ERR_FAIL_NULL_V(comm_a, false);
		ERR_FAIL_NULL_V(comm_b, false);
		if (comm_a->texture != comm_b->texture) {
			return false;
		}

	} else if (ca->type == RendererCanvasRender::Item::Command::TYPE_POLYGON) {
		const RendererCanvasRender::Item::CommandPolygon *comm_a = static_cast<const RendererCanvasRender::Item::CommandPolygon *>(ca);
		const RendererCanvasRender::Item::CommandPolygon *comm_b = static_cast<const RendererCanvasRender::Item::CommandPolygon *>(cb);
		ERR_FAIL_NULL_V(comm_a, false);
		ERR_FAIL_NULL_V(comm_b, false);
		if (comm_a->texture != comm_b->texture) {
			return false;
		}

	} else {
		return false;
	}

	return true;
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::sort_items_from(int p_start) {
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
	ERR_FAIL_COND_V((p_start + 1) >= bdata.sort_items.size(), false);
#endif

	const BSortItem &start = bdata.sort_items[p_start];
	int start_z = start.z_index;

	// Check start is the right type for sorting (exactly 1 command, RECT or POLY)
	if (!start.item->commands || start.item->commands->next != nullptr) {
		return false;
	}
	const RendererCanvasRender::Item::Command *command_start = start.item->commands;
	ERR_FAIL_NULL_V(command_start, false);
	if (command_start->type != RendererCanvasRender::Item::Command::TYPE_RECT && command_start->type != RendererCanvasRender::Item::Command::TYPE_POLYGON) {
		return false;
	}

	BSortItem &second = bdata.sort_items[p_start + 1];
	if (second.z_index != start_z) {
		if (!bdata.join_across_z_indices) {
			return false;
		}
	}

	// if the neighbours are already a good match
	if (_sort_items_match(start, second)) {
		return false;
	}

	// local cached aabb
	Rect2 second_AABB = second.item->global_rect_cache;

	// if the start and 2nd items overlap, can do no more
	if (start.item->global_rect_cache.intersects(second_AABB)) {
		return false;
	}

	// which neighbour to test
	int test_last = 2 + bdata.settings_item_reordering_lookahead;
	for (int test = 2; test < test_last; test++) {
		int test_sort_item_id = p_start + test;

		// if we've got to the end of the list, can't sort any more, give up
		if (test_sort_item_id >= bdata.sort_items.size()) {
			return false;
		}

		BSortItem *test_sort_item = &bdata.sort_items[test_sort_item_id];
		if (!test_sort_item) {
			continue;
		}

		// across z indices
		if (test_sort_item->z_index != start_z) {
			if (!bdata.join_across_z_indices) {
				return false;
			}
		}

		RendererCanvasRender::Item *test_item = test_sort_item->item;
		if (!test_item) {
			continue;
		}

		// if the test item overlaps the second item, we can't swap, AT ALL
		// because swapping an item OVER this one would cause artefacts
		if (second_AABB.intersects(test_item->global_rect_cache)) {
			return false;
		}

		// do they match?
		if (!_sort_items_match(start, *test_sort_item)) {
			continue;
		}

		// we can only swap if there are no AABB overlaps with sandwiched neighbours
		bool ok = true;

		// start from 2, no need to check 1 as the second has already been checked against this item
		// in the intersection test above
		for (int sn = 2; sn < test; sn++) {
			BSortItem *sandwich_neighbour = &bdata.sort_items[p_start + sn];
			if (!sandwich_neighbour) {
				continue;
			}

			// check if the test item hits the sandwich moving backward
			if (test_item->global_rect_cache.intersects(sandwich_neighbour->item->global_rect_cache)) {
				ok = false;
				break;
			}

			// check if the second item hits the sandwich moving forward
			if (second_AABB.intersects(sandwich_neighbour->item->global_rect_cache)) {
				ok = false;
				break;
			}
		}
		if (!ok) {
			continue;
		}

		// it is ok to exchange them!
		BSortItem temp = {};
		temp.assign(second);
		second.assign(*test_sort_item);
		test_sort_item->assign(temp);

		return true;
	} // for test

	return false;
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::_calculate_scissor_threshold_area() {
	if (!bdata.settings_scissor_lights) {
		return;
	}

	// scissor area threshold is 0.0 to 1.0 in the settings for ease of use.
	// we need to translate to an absolute area to determine quickly whether
	// to scissor.
	if (bdata.settings_scissor_threshold < 0.0001f) {
		bdata.scissor_threshold_area = -1.0f; // will always pass
	} else {
		// in pixels
		RID rt = get_this()->state.render_target;
		Size2i size = T_API::TextureStorage::get_singleton()->get_render_target(rt)->size;

		int w = size.width;
		int h = size.height;

		int screen_area = w * h;

		bdata.scissor_threshold_area = bdata.settings_scissor_threshold * screen_area;
	}
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::render_joined_item_commands(const BItemJoined &p_bij, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material, bool p_lit) {
	RendererCanvasRender::Item *item = nullptr;
	RendererCanvasRender::Item *first_item = bdata.item_refs[p_bij.first_item_ref].item;

	// If the CPU is baking the transforms into the vertices, the GPU must be fed an Identity matrix.
	// If it's a hardware transform, the GPU handles it via the first item's matrix.
	if (p_bij.use_hardware_transform()) {
		get_this()->state.uniforms.modelview_matrix = first_item->final_transform;

		if (p_lit && !T_API::FORCE_BAKE_MODULATE) {
			get_this()->state.uniforms.final_modulate = first_item->final_modulate;
		} else {
			get_this()->state.uniforms.final_modulate = bdata.item_refs[p_bij.first_item_ref].final_modulate;
		}
	} else {
		get_this()->state.uniforms.modelview_matrix = Transform2D();
		get_this()->state.uniforms.extra_matrix = Transform2D();
		get_this()->state.uniforms.final_modulate = Color(1, 1, 1, 1);
	}

	// fill_state and bdata have once off setup per joined item, and a smaller reset on flush
	FillState fill_state;
	fill_state.reset_joined_item(p_bij.use_hardware_transform());

	bdata.reset_joined_item();

	// should this joined item be using large FVF?
	if (p_bij.flags & BatcherEnums::USE_MODULATE_FVF) {
		bdata.use_modulate = true;
		bdata.fvf = BatcherEnums::FVF_MODULATED;
	}
	if (p_bij.flags & BatcherEnums::USE_LARGE_FVF) {
		bdata.use_modulate = true;
		bdata.use_large_verts = true;
		bdata.fvf = BatcherEnums::FVF_LARGE;
	}

	// in the special case of custom shaders that read from VERTEX (i.e. vertex position)
	// we want to disable software transform of extra matrix
	if (bdata.joined_item_batch_flags & BatcherEnums::PREVENT_VERTEX_BAKING) {
		fill_state.extra_matrix_sent = true;
	}

	for (unsigned int i = 0; i < p_bij.num_item_refs; i++) {
		const BItemRef &ref = bdata.item_refs[p_bij.first_item_ref + i];
		item = ref.item;

		if (!item) {
			continue;
		}

		if (!p_lit || T_API::FORCE_BAKE_MODULATE) {
			// if not lit we use the calculated final modulate
			fill_state.final_modulate = ref.final_modulate;
		} else {
			// if lit we ignore canvas modulate and just use the item modulate
			fill_state.final_modulate = item->final_modulate;
		}

		// Once off fill state setup, that will be retained over multiple calls to
		// prefill_joined_item()
		fill_state.transform_combined = item->final_transform;

		// decide the initial transform mode, and make a backup
		// in orig_transform_mode in case we need to switch back
		if (!fill_state.use_hardware_transform) {
			fill_state.transform_mode = _find_transform_mode(fill_state.transform_combined);
		} else {
			fill_state.transform_mode = TM_NONE;
		}
		fill_state.orig_transform_mode = fill_state.transform_mode;

		// keep track of when we added an extra matrix
		// so we can defer sending until we see a default command
		fill_state.transform_extra_command = nullptr;

		RendererCanvasRender::Item::Command *current_command = item->commands;
		while (current_command) {
			RendererCanvasRender::Item::Command *prev_command = current_command;
			
			// Track if the buffer was completely empty before attempting to fill
			bool was_empty = (bdata.total_verts == 0);
			bool bFull = get_this()->prefill_joined_item(fill_state, &current_command, item, p_current_clip, r_reclip, p_material);

			if (bFull) {
				flush_render_batches(first_item, p_current_clip, r_reclip, p_material, fill_state.sequence_batch_type_flags);

				bdata.reset_flush();
				fill_state.reset_flush();

				// It only failed if the buffer was already empty
				if (was_empty && current_command == prev_command) {
					get_this()->_legacy_canvas_item_render_commands(item, p_current_clip, r_reclip, p_material);
					
					// Repair the GPU uniform state corrupted by the legacy drawer
					if (!p_bij.use_hardware_transform()) {
						get_this()->state.uniforms.modelview_matrix = Transform2D();
						get_this()->state.uniforms.final_modulate = Color(1, 1, 1, 1);
					}
					fill_state.batch_tex_id = -1;
					break; 
				}
			}
		}
	}

	// flush if any left
	flush_render_batches(first_item, p_current_clip, r_reclip, p_material, fill_state.sequence_batch_type_flags);

	// zero all the batch data ready for a new run
	bdata.reset_flush();
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::flush_render_batches(RendererCanvasRender::Item *p_first_item, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material, uint32_t p_sequence_batch_type_flags) {
	bdata.use_colored_vertices = false;
	BatcherEnums::FVF backup_fvf = bdata.fvf;

	if (p_sequence_batch_type_flags & (BatcherEnums::BTF_LINE | BatcherEnums::BTF_LINE_AA)) {
		// do nothing, use the default regular FVF
		bdata.fvf = BatcherEnums::FVF_REGULAR;
	} else {
		// switch from regular to colored?
		if (bdata.fvf == BatcherEnums::FVF_REGULAR) {
			if (bdata.total_quads && !(bdata.joined_item_batch_flags & BatcherEnums::PREVENT_COLOR_BAKING)) {
				float ratio = (float)(bdata.total_color_changes - 1) / (float)bdata.total_quads;

				if (ratio >= bdata.settings_colored_vertex_format_threshold) {
					bdata.use_colored_vertices = true;
					bdata.fvf = BatcherEnums::FVF_COLOR;
				}
			}

			if (bdata.vertex_colors.size()) {
				bdata.use_colored_vertices = true;
				bdata.fvf = BatcherEnums::FVF_COLOR;
			}

			if (bdata.use_light_angles) {
				bdata.fvf = BatcherEnums::FVF_LIGHT_ANGLE;
			}
		}

		backup_fvf = bdata.fvf;
	}

	// translate if required to larger FVFs
	switch (bdata.fvf) {
		case BatcherEnums::FVF_UNBATCHED:
		case BatcherEnums::FVF_REGULAR:
			break;
		case BatcherEnums::FVF_COLOR: {
			bool contains_non_polygons = (p_sequence_batch_type_flags & ~BatcherEnums::BTF_POLY) != 0;

			if (!bdata.vertex_colors.size() || contains_non_polygons) {
				_translate_batches_to_larger_FVF<BatchVertexColored, false, false, false>(p_sequence_batch_type_flags);
			} else {
				_translate_batches_to_vertex_colored_FVF();
			}
		} break;
		case BatcherEnums::FVF_LIGHT_ANGLE:
			_translate_batches_to_larger_FVF<BatchVertexLightAngled, true, false, false>(p_sequence_batch_type_flags);
			break;
		case BatcherEnums::FVF_MODULATED:
			_translate_batches_to_larger_FVF<BatchVertexModulated, true, true, false>(p_sequence_batch_type_flags);
			break;
		case BatcherEnums::FVF_LARGE:
			_translate_batches_to_larger_FVF<BatchVertexLarge, true, true, true>(p_sequence_batch_type_flags);
			break;
	}

	// Send buffers to opengl
	get_this()->_batch_upload_buffers();

	// Execute the GL calls
	RendererCanvasRender::Item::Command *const *commands = &p_first_item->commands;
	get_this()->render_batches(commands, p_current_clip, r_reclip, p_material);

	// if we overrode the fvf for lines, set it back to the joined item fvf
	bdata.fvf = backup_fvf;
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::prefill_joined_item(FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item *p_item, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material) {
	// Checking the color for not being white makes it noticeably faster for the 90% case where it is white
	bool multiply_final_modulate = false;
	if (!r_fill_state.use_hardware_transform && (r_fill_state.final_modulate != Color(1, 1, 1, 1))) {
		multiply_final_modulate = true;
	}

	// Allocate dummy batch on the stack to prevent dangling zero-length default batches
	if (!r_fill_state.curr_batch) {
		r_fill_state.curr_batch = &r_fill_state.dummy_batch;
		r_fill_state.curr_batch->type = BatcherEnums::BT_DUMMY;
		r_fill_state.curr_batch->num_commands = 0;
	}

	// We use a local pointer to traverse. We only update the caller's pointer
	// when we return, avoiding linked-list corruption.
	RendererCanvasRender::Item::Command *command = *p_command_start;

	while (command) {
		switch (command->type) {
			case RendererCanvasRender::Item::Command::TYPE_TRANSFORM: {
				RendererCanvasRender::Item::CommandTransform *transform = static_cast<RendererCanvasRender::Item::CommandTransform *>(command);
				const Transform2D &extra_matrix = transform->xform;
				if (r_fill_state.extra_matrix_sent) {
					_prefill_default_batch(r_fill_state, command, *p_item);

					r_fill_state.transform_combined = p_item->final_transform * extra_matrix;
				} else {
					if (r_fill_state.use_hardware_transform) {
						r_fill_state.transform_combined = extra_matrix;
					} else {
						r_fill_state.transform_combined = p_item->final_transform * extra_matrix;
					}

					r_fill_state.transform_mode = _find_transform_mode(r_fill_state.transform_combined);
					r_fill_state.transform_extra_command = command;
				}
			} break;

			case RendererCanvasRender::Item::Command::TYPE_RECT: {
				// Ensure VBO alignment for the static index buffer.
				// If a previous polygon broke the 4-vertex alignment, force a flush.
				if (bdata.total_verts % 4 != 0) {
					*p_command_start = command;
					return true; 
				}
				RendererCanvasRender::Item::CommandRect *rect = static_cast<RendererCanvasRender::Item::CommandRect *>(command);
				bool send_light_angles = false;
				if (rect->texture.is_valid()) {
					typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(rect->texture);
					if (ct && ct->normal_map.is_valid()) {
						send_light_angles = true;
					}
				}

				bool buffer_full = false;
				if (send_light_angles) {
					buffer_full = _prefill_rect<true>(rect, r_fill_state, p_command_start, command, p_item, multiply_final_modulate);
				} else {
					buffer_full = _prefill_rect<false>(rect, r_fill_state, p_command_start, command, p_item, multiply_final_modulate);
				}

				if (buffer_full) {
					// VRAM buffer is full.
					// Save our exact spot so the next flush resumes right here.
					*p_command_start = command;
					return true;
				}
			} break;

			case RendererCanvasRender::Item::Command::TYPE_NINEPATCH: {
				// Ensure VBO alignment for the static index buffer.
				if (bdata.total_verts % 4 != 0) {
					*p_command_start = command;
					return true; 
				}
				
				RendererCanvasRender::Item::CommandNinePatch *np = static_cast<RendererCanvasRender::Item::CommandNinePatch *>(command);
				if ((np->axis_x != RS::NINE_PATCH_STRETCH) || (np->axis_y != RS::NINE_PATCH_STRETCH)) {
					// Non-stretch ninepatches are complex, push them to unbatched fallback
					_prefill_default_batch(r_fill_state, command, *p_item);
				} else {
					bool send_light_angles = false;
					if (np->texture.is_valid()) {
						typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(np->texture);
						if (ct && ct->normal_map.is_valid()) {
							send_light_angles = true;
						}
					}

					bool buffer_full = false;
					if (send_light_angles) {
						buffer_full = _prefill_ninepatch<true>(np, r_fill_state, p_command_start, command, p_item, multiply_final_modulate);
					} else {
						buffer_full = _prefill_ninepatch<false>(np, r_fill_state, p_command_start, command, p_item, multiply_final_modulate);
					}

					if (buffer_full) {
						*p_command_start = command;
						return true;
					}
				}
			} break;

			case RendererCanvasRender::Item::Command::TYPE_POLYGON: {
				RendererCanvasRender::Item::CommandPolygon *polygon = static_cast<RendererCanvasRender::Item::CommandPolygon *>(command);
				// The GLES2/GLES1 2D Batcher is heavily optimized for triangles.
				// If the polygon is lines or points, route it to the unbatched fallback.
				if (polygon->primitive != RS::PRIMITIVE_TRIANGLES) {
					_prefill_default_batch(r_fill_state, command, *p_item);
					break;
				}

				bool send_light_angles = false;
				if (polygon->texture.is_valid()) {
					typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(polygon->texture);
					if (ct && ct->normal_map.is_valid()) {
						send_light_angles = true;
					}
				}

				bool buffer_full = false;
				if (send_light_angles) {
					buffer_full = _prefill_polygon<true>(polygon, r_fill_state, p_command_start, command, p_item, multiply_final_modulate);
				} else {
					buffer_full = _prefill_polygon<false>(polygon, r_fill_state, p_command_start, command, p_item, multiply_final_modulate);
				}

				if (buffer_full) {
					// VRAM buffer is full. Resume on next flush.
					*p_command_start = command;
					return true;
				}
			} break;

			default: {
				// Any other command (Lines, Meshes, Multilines) gets routed to the default immediate-mode drawer
				_prefill_default_batch(r_fill_state, command, *p_item);
			} break;
		}

		// Move to the next command
		command = command->next;
	}

	// We successfully baked every single command for this item.
	// Tell the caller there is nothing left.
	*p_command_start = nullptr;
	return false;
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::_legacy_canvas_item_render_commands(RendererCanvasRender::Item* p_item, RendererCanvasRender::Item* p_current_clip, bool& r_reclip, typename T_API::MaterialData* p_material) {
	if (!p_item || !p_item->commands) {
		return;
	}

	// Reset the batch list for this single item
	bdata.batches.reset();

	// Create a single "unbatched" default batch
	Batch *batch = _batch_request_new();
	if (unlikely(!batch)) {
		return;
	}
	batch->type = BatcherEnums::BT_DEFAULT;
	batch->first_command = p_item->commands;
	batch->num_commands = godot4_commands_count(p_item->commands);
	batch->item = p_item;

	get_this()->state.uniforms.modelview_matrix = p_item->final_transform;
	get_this()->state.uniforms.final_modulate = p_item->final_modulate;

	// Send this directly to the OpenGL execution loop
	get_this()->render_batches(nullptr, p_current_clip, r_reclip, p_material);
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::_disallow_item_join_if_batch_types_too_different(RenderItemState &r_ris, uint32_t btf_allowed) {
	r_ris.joined_item_batch_type_flags_curr |= btf_allowed;

	bool disallow = false;

	if (r_ris.joined_item_batch_type_flags_prev & (~btf_allowed)) {
		disallow = true;
	}

	return disallow;
}

template <typename T_API>
template <class BATCH_VERTEX_TYPE, bool INCLUDE_LIGHT_ANGLES, bool INCLUDE_MODULATE, bool INCLUDE_LARGE>
void RasterizerCanvasBatcherCommon<T_API>::_translate_batches_to_larger_FVF(uint32_t p_sequence_batch_type_flags) {
	bool include_poly_color = false;

	if (p_sequence_batch_type_flags & BatcherEnums::BTF_POLY) {
		include_poly_color = true;
	}

	bdata.unit_vertices.prepare(sizeof(BATCH_VERTEX_TYPE));
	bdata.batches_temp.reset();

	Batch *dest_batch = nullptr;

	const BatchColor *source_vertex_colors = bdata.vertex_colors.get_data();
	const float *source_light_angles = bdata.light_angles.get_data();
	const BatchColor *source_vertex_modulates = bdata.vertex_modulates.get_data();
	const BatchTransform *source_vertex_transforms = bdata.vertex_transforms.get_data();

	ERR_FAIL_NULL(source_vertex_colors);
	ERR_FAIL_NULL(source_light_angles);
	ERR_FAIL_NULL(source_vertex_modulates);
	ERR_FAIL_NULL(source_vertex_transforms);

	for (int n = 0; n < bdata.batches.size(); n++) {
		const Batch &source_batch = bdata.batches[n];
		bool source_batch_uses_light_angles = false;
		if (source_batch.type != BatcherEnums::BT_DEFAULT && source_batch.type != BatcherEnums::BT_DUMMY) {
			const BatchTex &btex = bdata.batch_textures[source_batch.batch_texture_id];
			source_batch_uses_light_angles = btex.RID_normal.is_valid();
		}
		bool needs_new_batch = true;

		// Calculate actual vertices dynamically based on batch type
		int num_vertices_in_batch = 0;
		if (source_batch.type == BatcherEnums::BT_RECT) {
			num_vertices_in_batch = source_batch.num_commands * 4;
		} else {
			// For POLY, LINE, etc., num_commands represents the vertex/index count
			num_vertices_in_batch = source_batch.num_commands;
		}

		if (dest_batch) {
			if (dest_batch->type == source_batch.type && source_batch.type == BatcherEnums::BT_RECT) {
				if (dest_batch->batch_texture_id == source_batch.batch_texture_id) {
					dest_batch->num_commands += source_batch.num_commands;
					needs_new_batch = false;

					int first_vert = source_batch.first_vert;
					int end_vert = MIN(first_vert + num_vertices_in_batch, bdata.vertices.size());

					for (int v = first_vert; v < end_vert; v++) {
						const BatchVertex &bv = bdata.vertices[v];
						BATCH_VERTEX_TYPE *cv = (BATCH_VERTEX_TYPE *)bdata.unit_vertices.request();
						if (!cv) {
							ERR_PRINT_ONCE("GLES1: FVF Translation buffer overflowed! Geometry will be missing.");
							break;
						}
						cv->pos = bv.pos;
						cv->uv = bv.uv;
						cv->col = source_batch.color;

						if constexpr (INCLUDE_LIGHT_ANGLES) {
							BatchVertexLightAngled *lv = (BatchVertexLightAngled *)cv;
							ERR_FAIL_NULL(lv);
							lv->light_angle = source_batch_uses_light_angles ? *source_light_angles++ : 0.0f;
						}
						if constexpr (INCLUDE_MODULATE) {
							BatchVertexModulated *mv = (BatchVertexModulated *)cv;
							ERR_FAIL_NULL(mv);
							mv->modulate = *source_vertex_modulates++;
						}
						if constexpr (INCLUDE_LARGE) {
							BatchVertexLarge *lv = (BatchVertexLarge *)cv;
							ERR_FAIL_NULL(lv);
							lv->transform = *source_vertex_transforms++;
						}
					}
				}
			}
		}

		if (needs_new_batch) {
			dest_batch = bdata.batches_temp.request();
			
			if (!dest_batch) {
				ERR_PRINT_ONCE("GLES Batcher: FVF Translation buffer overflowed! Geometry will be missing.");
				break;
			}
			*dest_batch = source_batch;

			if (source_batch.type != BatcherEnums::BT_DEFAULT) {
				int first_vert = source_batch.first_vert;
				int end_vert = MIN(first_vert + num_vertices_in_batch, bdata.vertices.size());

				for (int v = first_vert; v < end_vert; v++) {
					const BatchVertex &bv = bdata.vertices[v];
					BATCH_VERTEX_TYPE *cv = (BATCH_VERTEX_TYPE *)bdata.unit_vertices.request();
					if (!cv) {
						ERR_PRINT_ONCE("GLES1: FVF Translation buffer overflowed! Geometry will be missing.");
						break;
					}
					cv->pos = bv.pos;
					cv->uv = bv.uv;

					if (!include_poly_color || source_batch.type == BatcherEnums::BT_RECT) {
						cv->col = source_batch.color;
					} else {
						cv->col = *source_vertex_colors++;
					}

					if constexpr (INCLUDE_LIGHT_ANGLES) {
						BatchVertexLightAngled *lv = (BatchVertexLightAngled *)cv;
						ERR_FAIL_NULL(lv);
						lv->light_angle = source_batch_uses_light_angles ? *source_light_angles++ : 0.0f;
					}
					if constexpr (INCLUDE_MODULATE) {
						BatchVertexModulated *mv = (BatchVertexModulated *)cv;
						ERR_FAIL_NULL(mv);
						mv->modulate = *source_vertex_modulates++;
					}
					if constexpr (INCLUDE_LARGE) {
						BatchVertexLarge *lv = (BatchVertexLarge *)cv;
						ERR_FAIL_NULL(lv);
						lv->transform = *source_vertex_transforms++;
					}
				}
			}
		}
	}
	bdata.batches.copy_from(bdata.batches_temp);
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::_translate_batches_to_vertex_colored_FVF() {
	bdata.unit_vertices.prepare(sizeof(BatchVertexColored));
	const BatchColor *source_vertex_colors = &bdata.vertex_colors[0];
	int num_verts = bdata.vertices.size();

	for (int n = 0; n < num_verts; n++) {
		const BatchVertex &bv = bdata.vertices[n];
		BatchVertexColored *cv = (BatchVertexColored *)bdata.unit_vertices.request();
		if (!cv) {
			ERR_PRINT_ONCE("GLES1: FVF Translation buffer overflowed!");
			break;
		}
		cv->pos = bv.pos;
		cv->uv = bv.uv;
		cv->col = *source_vertex_colors++;
	}
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::_light_scissor_begin(const Rect2 &p_item_rect, const Transform2D &p_light_xform, const Rect2 &p_light_rect, int p_rt_height) const {
	float area_item = p_item_rect.size.x * p_item_rect.size.y;

	// The area of pixels saved can never be more
	// than the area of the item itself.
	if (area_item < bdata.scissor_threshold_area) {
		return false;
	}

	Rect2 cliprect;
	if (!_light_find_intersection(p_item_rect, p_light_xform, p_light_rect, cliprect)) {
		cliprect = Rect2(0, 0, 0, 0);
	} else {
		// Determine the area (fill rate) that will be saved
		float area_cliprect = cliprect.size.x * cliprect.size.y;
		float area_saved = area_item - area_cliprect;

		if (area_saved < bdata.scissor_threshold_area) {
			return false;
		}
	}

	int y = cliprect.position.y + cliprect.size.y;
	get_this()->gl_enable_scissor(
		cliprect.position.x,
		y,
		cliprect.size.width,
		cliprect.size.height
	);
	return true;
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::_light_find_intersection(const Rect2 &p_item_rect, const Transform2D &p_light_xform, const Rect2 &p_light_rect, Rect2 &r_cliprect) const {
	// Transform the light's rect by its transform to get it into global/screen space
	// We need the AABB of the light in the same space as the item rect

	// Fast path for axis-aligned lights (no rotation)
	if (p_light_xform.columns[0].y == 0.0f && p_light_xform.columns[1].x == 0.0f) {
		Vector2 scale = Vector2(p_light_xform.columns[0].x, p_light_xform.columns[1].y);
		Vector2 pos = p_light_xform.columns[2] + p_light_rect.position * scale;
		Vector2 size = p_light_rect.size * scale;

		Rect2 global_light_rect(pos, size);
		r_cliprect = p_item_rect.intersection(global_light_rect);
	} else {
		// Slower path for rotated lights
		// (calculating the bounding box of the transformed light corners)
		Vector2 points[4] = {
			p_light_xform.xform(p_light_rect.position),
			p_light_xform.xform(Vector2(p_light_rect.position.x + p_light_rect.size.x, p_light_rect.position.y)),
			p_light_xform.xform(Vector2(p_light_rect.position.x, p_light_rect.position.y + p_light_rect.size.y)),
			p_light_xform.xform(Vector2(p_light_rect.position.x + p_light_rect.size.x, p_light_rect.position.y + p_light_rect.size.y)),
		};

		Rect2 global_light_rect(points[0], Vector2());
		for (int i = 1; i < 4; i++) {
			global_light_rect.expand_to(points[i]);
		}

		r_cliprect = p_item_rect.intersection(global_light_rect);
	}

	return r_cliprect.has_area();
}

template <typename T_API>
bool RasterizerCanvasBatcherCommon<T_API>::_detect_item_batch_break(RenderItemState &r_ris, RendererCanvasRender::Item *p_ci, bool &r_batch_break) {
	int command_count = godot4_commands_count(p_ci->commands);

	r_ris.joined_item_batch_type_flags_curr = 0;

	if (command_count > bdata.settings_max_join_item_commands) {
		return true;
	}

	RendererCanvasRender::Item::Command *command = p_ci->commands;

	// Run through the linked list of commands looking for one that could prevent joining
	while (command) {
		switch (command->type) {
			case RendererCanvasRender::Item::Command::TYPE_RECT: {
				if (_disallow_item_join_if_batch_types_too_different(r_ris, BatcherEnums::BTF_LINE | BatcherEnums::BTF_LINE_AA)) {
					return true;
				}
			} break;

			case RendererCanvasRender::Item::Command::TYPE_POLYGON: {
				if (!bdata.settings_use_software_skinning) {
					return true;
				}

				if (_disallow_item_join_if_batch_types_too_different(r_ris, BatcherEnums::BTF_POLY)) {
					return true;
				}
			} break;

			case RendererCanvasRender::Item::Command::TYPE_NINEPATCH: {
				RendererCanvasRender::Item::CommandNinePatch *np = static_cast<RendererCanvasRender::Item::CommandNinePatch *>(command);
				if ((np->axis_x != RS::NINE_PATCH_STRETCH) || (np->axis_y != RS::NINE_PATCH_STRETCH)) {
					return true;
				}
	
				if (_disallow_item_join_if_batch_types_too_different(r_ris, BatcherEnums::BTF_RECT)) {
					return true;
				}
			} break;

			case RendererCanvasRender::Item::Command::TYPE_TRANSFORM: {
				// Transforms are compatible with all batch types.
			} break;

			default: {
				// Any unhandled command type (like meshes) immediately breaks the batch
				return true;
			} break;
		}

		command = command->next;
	}

	if (p_ci->copy_back_buffer) {
		return true;
	}

	return false;
}

template <typename T_API>
template <bool SEND_LIGHT_ANGLES>
bool RasterizerCanvasBatcherCommon<T_API>::_prefill_rect(RendererCanvasRender::Item::CommandRect *rect, FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item::Command *p_command, RendererCanvasRender::Item *p_item, bool multiply_final_modulate) {
	bool change_batch = false;

	if (r_fill_state.curr_batch->type != BatcherEnums::BT_RECT) {
		if (r_fill_state.sequence_batch_type_flags & (~BatcherEnums::BTF_RECT)) {
			*p_command_start = p_command;
			return true;
		}
		r_fill_state.sequence_batch_type_flags |= BatcherEnums::BTF_RECT;
		change_batch = true;

		// Single rect fallback
		if (bdata.settings_use_single_rect_fallback && r_fill_state.use_hardware_transform) {
			bool is_single_rect = false;
			if (p_command->next != nullptr) {
				RendererCanvasRender::Item::Command *command_next = p_command->next;
				ERR_FAIL_NULL_V(command_next, false);

				if ((command_next->type != RendererCanvasRender::Item::Command::TYPE_RECT) && (command_next->type != RendererCanvasRender::Item::Command::TYPE_TRANSFORM)) {
					is_single_rect = true;
				}
			} else {
				is_single_rect = true;
			}
			if (is_single_rect) {
				_prefill_default_batch(r_fill_state, p_command, *p_item);
				return false;
			}
		}
	}

	BatchVertex *bvs = bdata.vertices.request(4);
	if (!bvs) {
		*p_command_start = p_command;
		return true; // Vertex buffer full
	}

	const bool use_large_verts = bdata.use_large_verts;
	const bool use_modulate = bdata.use_modulate;
	Color col = rect->modulate;

	if (T_API::FORCE_BAKE_MODULATE || (!use_large_verts && multiply_final_modulate)) {
		col *= r_fill_state.final_modulate;
	}

	RID normal_rid = RID();
	if constexpr (SEND_LIGHT_ANGLES) {
		bdata.use_light_angles = true;
		if (rect->texture.is_valid()) {
			typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(rect->texture);
			if (ct) {
				normal_rid = ct->normal_map;
			}
		}
	}

	int old_batch_tex_id = r_fill_state.batch_tex_id;
	r_fill_state.batch_tex_id = _batch_find_or_create_tex(rect->texture, normal_rid, rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_TILE, old_batch_tex_id);

	if (old_batch_tex_id != r_fill_state.batch_tex_id) {
		change_batch = true;
	}

	if (!r_fill_state.curr_batch->color.equals(col)) {
		change_batch = true;
		bdata.total_color_changes++;
	}

	if (change_batch) {
		const BatchTex &batchtex = bdata.batch_textures[r_fill_state.batch_tex_id];
		batchtex.tex_pixel_size.to(r_fill_state.texpixel_size);

		if (bdata.settings_uv_contract) {
			r_fill_state.contract_uvs = (batchtex.flags == 0);
		}

		r_fill_state.curr_batch = _batch_request_new(false);
		if (unlikely(!r_fill_state.curr_batch)) {
			*p_command_start = p_command;
			return true; // Trigger fallback
		}
		r_fill_state.curr_batch->type = BatcherEnums::BT_RECT;
		r_fill_state.curr_batch->color.set(col);
		r_fill_state.curr_batch->batch_texture_id = r_fill_state.batch_tex_id;
		r_fill_state.curr_batch->first_command = p_command;
		r_fill_state.curr_batch->num_commands = 1;
		r_fill_state.curr_batch->first_vert = bdata.total_verts;
		r_fill_state.curr_batch->item = p_item;
	} else {
		r_fill_state.curr_batch->num_commands++;
	}

	Vector2 mins = rect->rect.position;
	if (r_fill_state.transform_mode == TM_TRANSLATE && !use_large_verts) {
		_software_transform_vertex(mins, r_fill_state.transform_combined);
	}

	Vector2 maxs = mins + rect->rect.size;

	BatchVertex *bA = &bvs[0];
	ERR_FAIL_NULL_V(bA, false);
	BatchVertex *bB = &bvs[1];
	ERR_FAIL_NULL_V(bB, false);
	BatchVertex *bC = &bvs[2];
	ERR_FAIL_NULL_V(bC, false);
	BatchVertex *bD = &bvs[3];
	ERR_FAIL_NULL_V(bD, false);

	bA->pos.set(mins.x, mins.y);
	bB->pos.set(maxs.x, mins.y);
	bC->pos.set(maxs.x, maxs.y);
	bD->pos.set(mins.x, maxs.y);

	if (rect->rect.size.x < 0) {
		SWAP(bA->pos, bB->pos);
		SWAP(bC->pos, bD->pos);
	}
	if (rect->rect.size.y < 0) {
		SWAP(bA->pos, bD->pos);
		SWAP(bB->pos, bC->pos);
	}

	if (r_fill_state.transform_mode == TM_ALL && !use_large_verts) {
		_software_transform_vertex(bA->pos, r_fill_state.transform_combined);
		_software_transform_vertex(bB->pos, r_fill_state.transform_combined);
		_software_transform_vertex(bC->pos, r_fill_state.transform_combined);
		_software_transform_vertex(bD->pos, r_fill_state.transform_combined);
	}

	Vector2 src_min;
	Vector2 src_max;
	if (rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_REGION) {
		src_min = rect->source.position * r_fill_state.texpixel_size;

		if (rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_TILE) {
			// Bake the tile repetition into the UV bounds.
			src_max = src_min + (rect->rect.size * r_fill_state.texpixel_size);
		} else {
			src_max = (rect->source.position + rect->source.size) * r_fill_state.texpixel_size;
		}

		if (r_fill_state.contract_uvs) {
			const float uv_epsilon = bdata.settings_uv_contract_amount;
			src_min.x += uv_epsilon;
			src_min.y += uv_epsilon;
			src_max.x -= uv_epsilon;
			src_max.y -= uv_epsilon;
		}
	} else if (rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_TILE) {
		src_min = Vector2(0, 0);
		src_max = rect->rect.size * r_fill_state.texpixel_size;
	} else {
		src_min = Vector2(0, 0);
		src_max = Vector2(1, 1);
	}

	Vector2 uvs[4] = {
		src_min,
		Vector2(src_max.x, src_min.y),
		src_max,
		Vector2(src_min.x, src_max.y),
	};

	bool flip_h = false;
	bool flip_v = false;

	if (rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_TRANSPOSE) {
		SWAP(uvs[1], uvs[3]);
	}
	if (rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_FLIP_H) {
		SWAP(uvs[0], uvs[1]);
		SWAP(uvs[2], uvs[3]);
		flip_h = !flip_h;
		flip_v = !flip_v;
	}
	if (rect->flags & RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_FLIP_V) {
		SWAP(uvs[0], uvs[3]);
		SWAP(uvs[1], uvs[2]);
		flip_v = !flip_v;
	}

	bA->uv.set(uvs[0]);
	bB->uv.set(uvs[1]);
	bC->uv.set(uvs[2]);
	bD->uv.set(uvs[3]);

	if (use_modulate) {
		BatchColor *pBC = bdata.vertex_modulates.request(4);
#ifdef DEBUG_ENABLED
		DEV_ASSERT(pBC && "GLES1: Modulate buffer desynchronized from main vertex buffer!");
#else
		if (unlikely(!pBC)) {
			*p_command_start = p_command;
			return true; 
		}
#endif
		pBC[0].set(r_fill_state.final_modulate);
		pBC[1] = pBC[0];
		pBC[2] = pBC[0];
		pBC[3] = pBC[0];
	}

	if (use_large_verts) {
		BatchTransform *pBT = bdata.vertex_transforms.request(4);
#ifdef DEBUG_ENABLED
		DEV_ASSERT(pBT && "GLES1: Modulate buffer desynchronized from main vertex buffer!");
#else
		if (unlikely(!pBT)) {
			*p_command_start = p_command;
			return true; 
		}
#endif
		const Transform2D &tr = r_fill_state.transform_combined;
		pBT[0].translate.set(tr.columns[2].x, tr.columns[2].y);
		pBT[0].basis[0].set(tr.columns[0][0], tr.columns[1][0]);
		pBT[0].basis[1].set(tr.columns[0][1], tr.columns[1][1]);
		pBT[1] = pBT[0];
		pBT[2] = pBT[0];
		pBT[3] = pBT[0];
	}

	if constexpr (SEND_LIGHT_ANGLES) {
		float *angles = bdata.light_angles.request(4);
#ifdef DEBUG_ENABLED
		DEV_ASSERT(angles && "GLES1: Modulate buffer desynchronized from main vertex buffer!");
#else
		if (unlikely(!angles)) {
			*p_command_start = p_command;
			return true; 
		}
#endif
		float angle = 0.0f;
		const float TWO_PI = Math_PI * 2.0f;

		if (r_fill_state.transform_mode != TM_NONE) {
			const Transform2D &tr = r_fill_state.transform_combined;
			Vector2 x_axis(tr.columns[0][0], tr.columns[1][0]);
			Vector2 y_axis(tr.columns[0][1], tr.columns[1][1]);
			if (x_axis.cross(y_axis) < 0.0f) {
				flip_v = !flip_v;
			}
			angle = x_axis.angle();
			if (angle < 0.0f) {
				angle += TWO_PI;
			}
		}

		if (flip_h) {
			angle += Math_PI;
			angle = fmodf(angle, TWO_PI);
		}
		angle += 1.0f;
		if (flip_v) {
			angle *= -1.0f;
		}

		for (int n = 0; n < 4; n++) {
			angles[n] = angle;
		}
	}

	bdata.total_quads++;
	bdata.total_verts += 4;
	return false;
}

template <typename T_API>
template <bool SEND_LIGHT_ANGLES>
bool RasterizerCanvasBatcherCommon<T_API>::_prefill_ninepatch(RendererCanvasRender::Item::CommandNinePatch *p_np, FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item::Command *p_command, RendererCanvasRender::Item *p_item, bool multiply_final_modulate) {
	// Properly unwrap the RID
	RID diffuse_rid = p_np->texture;
	if (diffuse_rid.is_valid()) {
		typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(diffuse_rid);
		if (ct && ct->diffuse.is_valid()) {
			diffuse_rid = ct->diffuse;
		}
	}

	typename T_API::Texture *tex = T_API::TextureStorage::get_singleton()->get_texture(diffuse_rid);

	if (!tex || tex->width == 0 || tex->height == 0) {
		return false;
	}

	if (bdata.vertices.size() + (4 * 9) > bdata.vertices.max_size()) {
		*p_command_start = p_command;
		return true;
	}

	RendererCanvasRender::Item::CommandRect trect;
	trect.type = RendererCanvasRender::Item::Command::TYPE_RECT;
	trect.texture = p_np->texture;
	trect.modulate = p_np->color;
	trect.flags = RendererCanvasRender::CanvasRectFlags::CANVAS_RECT_REGION;

	Rect2 source = p_np->source;
	if (source.size.x == 0 && source.size.y == 0) {
		source.size.x = tex->width;
		source.size.y = tex->height;
	}

	float screen_scale = 1.0f;
	if ((bdata.settings_ninepatch_mode == 1) && !Math::is_zero_approx(source.size.x) && !Math::is_zero_approx(source.size.y)) {
		screen_scale = MIN(p_np->rect.size.x / source.size.x, p_np->rect.size.y / source.size.y);
		screen_scale = MIN(1.0f, screen_scale);
	}

	// Clamp screen margins to prevent the geometry from flipping inside out
	float draw_margin_left = p_np->margin[SIDE_LEFT] * screen_scale;
	float draw_margin_right = p_np->margin[SIDE_RIGHT] * screen_scale;
	float draw_margin_top = p_np->margin[SIDE_TOP] * screen_scale;
	float draw_margin_bottom = p_np->margin[SIDE_BOTTOM] * screen_scale;

	if (draw_margin_left + draw_margin_right > p_np->rect.size.x) {
		float ratio = p_np->rect.size.x / (draw_margin_left + draw_margin_right);
		draw_margin_left *= ratio;
		draw_margin_right *= ratio;
	}

	if (draw_margin_top + draw_margin_bottom > p_np->rect.size.y) {
		float ratio = p_np->rect.size.y / (draw_margin_top + draw_margin_bottom);
		draw_margin_top *= ratio;
		draw_margin_bottom *= ratio;
	}

	Rect2 &rt = trect.rect;
	Rect2 &src = trect.source;

	float tex_margin_left = p_np->margin[SIDE_LEFT];
	float tex_margin_right = p_np->margin[SIDE_RIGHT];
	float tex_margin_top = p_np->margin[SIDE_TOP];
	float tex_margin_bottom = p_np->margin[SIDE_BOTTOM];

	// Tiny epsilon to the inner UV margins
	// to prevent GL_LINEAR NPOT bleed
	constexpr float EPS = 0.01f; 

	float x[4] = {
		(float)p_np->rect.position.x,
		(float)(p_np->rect.position.x + draw_margin_left),
		(float)(p_np->rect.position.x + p_np->rect.size.x - draw_margin_right),
		(float)(p_np->rect.position.x + p_np->rect.size.x)
	};
	float y[4] = {
		(float)p_np->rect.position.y,
		(float)(p_np->rect.position.y + draw_margin_top),
		(float)(p_np->rect.position.y + p_np->rect.size.y - draw_margin_bottom),
		(float)(p_np->rect.position.y + p_np->rect.size.y)
	};
	float u[4] = {
		(float)source.position.x,
		(float)(source.position.x + tex_margin_left) + (tex_margin_left > 0 ? EPS : 0),
		(float)(source.position.x + source.size.x - tex_margin_right) - (tex_margin_right > 0 ? EPS : 0),
		(float)(source.position.x + source.size.x)
	};
	float v[4] = {
		(float)source.position.y,
		(float)(source.position.y + tex_margin_top) + (tex_margin_top > 0 ? EPS : 0),
		(float)(source.position.y + source.size.y - tex_margin_bottom) - (tex_margin_bottom > 0 ? EPS : 0),
		(float)(source.position.y + source.size.y)
	};

	bool single_rect_fallback = bdata.settings_use_single_rect_fallback;
	bdata.settings_use_single_rect_fallback = false;

	for (int line = 0; line < 3; line++) {
		rt.position = Vector2(x[0], y[line]);
		rt.size = Vector2(x[1] - x[0], y[line + 1] - y[line]);
		src.position = Vector2(u[0], v[line]);
		src.size = Vector2(u[1] - u[0], v[line + 1] - v[line]);
		_prefill_rect<SEND_LIGHT_ANGLES>(&trect, r_fill_state, p_command_start, p_command, p_item, multiply_final_modulate);

		if (line != 1 || p_np->draw_center) {
			rt.position.x = x[1];
			rt.size.x = x[2] - x[1];
			src.position.x = u[1];
			src.size.x = u[2] - u[1];
			_prefill_rect<SEND_LIGHT_ANGLES>(&trect, r_fill_state, p_command_start, p_command, p_item, multiply_final_modulate);
		}

		rt.position.x = x[2];
		rt.size.x = x[3] - x[2];
		src.position.x = u[2];
		src.size.x = u[3] - u[2];
		_prefill_rect<SEND_LIGHT_ANGLES>(&trect, r_fill_state, p_command_start, p_command, p_item, multiply_final_modulate);
	}

	bdata.settings_use_single_rect_fallback = single_rect_fallback;
	return false;
}

template <typename T_API>
template <bool SEND_LIGHT_ANGLES>
bool RasterizerCanvasBatcherCommon<T_API>::_prefill_polygon(RendererCanvasRender::Item::CommandPolygon *p_poly, FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item::Command *p_command, RendererCanvasRender::Item *p_item, bool multiply_final_modulate) {
	bool change_batch = false;

	if (r_fill_state.curr_batch->type != BatcherEnums::BT_POLY) {
		if (r_fill_state.sequence_batch_type_flags & (~BatcherEnums::BTF_POLY)) {
			*p_command_start = p_command;
			return true;
		}
		r_fill_state.sequence_batch_type_flags |= BatcherEnums::BTF_POLY;
		change_batch = true;
	}

	const typename T_API::Canvas::PolyData *poly_data = get_this()->polygon_cache.getptr(p_poly->polygon.polygon_id);

	if (unlikely(!poly_data)) {
		return false; // Invalid or freed polygon ID
	}

	int num_inds = poly_data->indices.size();
	int num_points = poly_data->points.size();

	if (num_points == 0) {
		return false; // Nothing to draw
	}

	int num_verts_to_draw = num_inds > 0 ? num_inds : num_points;

	if (num_verts_to_draw > bdata.vertices.max_size()) {
		_prefill_default_batch(r_fill_state, p_command, *p_item);
		return false; 
	}

	BatchVertex *bvs = bdata.vertices.request(num_verts_to_draw);
	if (unlikely(!bvs)) {
		*p_command_start = p_command;
		return true; // Vertex buffer full
	}

	BatchColor *vertex_colors = bdata.vertex_colors.request(num_verts_to_draw);
	if (unlikely(!vertex_colors)) {
		*p_command_start = p_command;
		return true;
	}

	const bool use_large_verts = bdata.use_large_verts;
	const bool use_modulate = bdata.use_modulate;

	BatchColor *vertex_modulates = nullptr;
	if (use_modulate) {
		vertex_modulates = bdata.vertex_modulates.request(num_verts_to_draw);
		if (unlikely(!vertex_modulates)) {
			*p_command_start = p_command;
			return true;
		}
		vertex_modulates[0].set(r_fill_state.final_modulate);
	}

	BatchTransform *pBT = nullptr;
	if (use_large_verts) {
		pBT = bdata.vertex_transforms.request(num_verts_to_draw);
		if (unlikely(!pBT)) {
			*p_command_start = p_command;
			return true;
		}
		const Transform2D &tr = r_fill_state.transform_combined;
		pBT[0].translate.set(tr.columns[2].x, tr.columns[2].y);
		pBT[0].basis[0].set(tr.columns[0][0], tr.columns[1][0]);
		pBT[0].basis[1].set(tr.columns[0][1], tr.columns[1][1]);
	}

	Color modulate = Color(1, 1, 1, 1);
	
	if (T_API::FORCE_BAKE_MODULATE || (!use_large_verts && multiply_final_modulate)) {
		modulate = r_fill_state.final_modulate;
	}

	RID normal_rid = RID();
	if constexpr (SEND_LIGHT_ANGLES) {
		bdata.use_light_angles = true;
		if (p_poly->texture.is_valid()) {
			typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(p_poly->texture);
			if (ct) {
				normal_rid = ct->normal_map;
			}
		}
	}

	int old_batch_tex_id = r_fill_state.batch_tex_id;
	r_fill_state.batch_tex_id = _batch_find_or_create_tex(p_poly->texture, normal_rid, false, old_batch_tex_id);

	if (old_batch_tex_id != r_fill_state.batch_tex_id) {
		change_batch = true;
	}

	if (!r_fill_state.curr_batch->color.equals(modulate)) {
		change_batch = true;
		bdata.total_color_changes++;
	}

	if (change_batch) {
		const BatchTex &batchtex = bdata.batch_textures[r_fill_state.batch_tex_id];
		batchtex.tex_pixel_size.to(r_fill_state.texpixel_size);

		if (bdata.settings_uv_contract) {
			r_fill_state.contract_uvs = (batchtex.flags == 0);
		}

		r_fill_state.curr_batch = _batch_request_new(false);
		if (unlikely(!r_fill_state.curr_batch)) {
			*p_command_start = p_command;
			return true; // Trigger fallback
		}
		r_fill_state.curr_batch->type = BatcherEnums::BT_POLY;
		r_fill_state.curr_batch->color.set(modulate);
		r_fill_state.curr_batch->batch_texture_id = r_fill_state.batch_tex_id;
		r_fill_state.curr_batch->first_command = p_command;
		r_fill_state.curr_batch->num_commands = num_verts_to_draw;
		r_fill_state.curr_batch->first_vert = bdata.total_verts;
		r_fill_state.curr_batch->item = p_item;
	} else {
		r_fill_state.curr_batch->num_commands += num_verts_to_draw;
	}

	BatchColor vcol = {};
	int num_colors_specified = MIN((int)poly_data->colors.size(), num_points);
	BatchColor *precalced_colors = SAFE_ALLOCA_ARRAY(BatchColor, num_points);
	if (unlikely(!precalced_colors)) {
		// Roll back the corrupted command count increment
		if (change_batch) {
			r_fill_state.curr_batch->num_commands = 0;
		} else {
			r_fill_state.curr_batch->num_commands -= num_verts_to_draw;
		}
		
		_prefill_default_batch(r_fill_state, p_command, *p_item);
		return false;
	}

	if (num_colors_specified > 0) {
		vcol.set(poly_data->colors[0] * modulate);
	} else {
		vcol.set(modulate);
	}

	for (int n = 0; n < num_colors_specified; n++) {
		precalced_colors[n].set(poly_data->colors[n] * modulate);
	}

	for (int n = num_colors_specified; n < num_points; n++) {
		precalced_colors[n] = vcol;
	}

	if constexpr (SEND_LIGHT_ANGLES) {
		float *angles = bdata.light_angles.request(num_verts_to_draw);
		if (unlikely(!angles)) {
			*p_command_start = p_command;
			return true;
		}

		float angle = 0.0f;
		constexpr float TWO_PI = Math_PI * 2.0f;

		if (r_fill_state.transform_mode != TM_NONE) {
			const Transform2D &tr = r_fill_state.transform_combined;
			Vector2 x_axis(tr.columns[0][0], tr.columns[1][0]);
			angle = x_axis.angle();
			if (angle < 0.0f) {
				angle += TWO_PI;
			}
		}
		angle += 1.0f;

		for (int n = 0; n < num_verts_to_draw; n++) {
			angles[n] = angle;
		}
	}

	const int *indices_ptr = poly_data->indices.ptr();
	const Vector2 *points_ptr = poly_data->points.ptr();
	const Vector2 *uvs_ptr = poly_data->uvs.size() > 0 ? poly_data->uvs.ptr() : nullptr;

	for (int n = 0; n < num_verts_to_draw; n++) {
		int ind = num_inds > 0 ? indices_ptr[n] : n;

		if (ind < 0 || ind >= num_points) {
			ERR_PRINT("GLES Batcher: Polygon index out of bounds!");
			ind = 0; 
		}

		Vector2 pos = points_ptr[ind];
		if (r_fill_state.transform_mode != TM_NONE) {
			_software_transform_vertex(pos, r_fill_state.transform_combined);
		}
		bvs[n].pos.set(pos.x, pos.y);

		if (uvs_ptr && (uint32_t)ind < poly_data->uvs.size()) {
			bvs[n].uv.set(uvs_ptr[ind].x, uvs_ptr[ind].y);
		} else {
			bvs[n].uv.set(0.0f, 0.0f);
		}

		vertex_colors[n] = precalced_colors[ind];

		if (use_modulate) {
			vertex_modulates[n] = vertex_modulates[0];
		}
		if (use_large_verts) {
			pBT[n] = pBT[0];
		}
	}

	bdata.total_verts += num_verts_to_draw;
	bdata.total_quads += (num_verts_to_draw / 3);

	return false;
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::_software_transform_vertex(BatchVector2 &r_v, const Transform2D &p_tr) const {
	Vector2 vc(r_v.x, r_v.y);
	vc = p_tr.xform(vc);
	r_v.set(vc);
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::_software_transform_vertex(Vector2 &r_v, const Transform2D &p_tr) const {
	r_v = p_tr.xform(r_v);
}

template <typename T_API>
int RasterizerCanvasBatcherCommon<T_API>::_batch_find_or_create_tex(const RID &p_texture, const RID &p_normal, bool p_tile, int p_previous_match) {
	// Fast Path: Assume the last item use this exact texture
	if (p_previous_match >= 0 && p_previous_match < bdata.batch_textures.size()) {
		const BatchTex &batch_texture = bdata.batch_textures[p_previous_match];

		if ((batch_texture.RID_texture == p_texture) && (batch_texture.RID_normal == p_normal)) {
			bool tiles = batch_texture.tile_mode != BatchTex::TILE_OFF;
			if (tiles == p_tile) {
				return p_previous_match;
			}
		}
	}

	// Linear Search: Figure out if we use this texture earlier in the frame
	for (int n = 0; n < bdata.batch_textures.size(); n++) {
		const BatchTex &batch_texture = bdata.batch_textures[n];
		if ((batch_texture.RID_texture == p_texture) && (batch_texture.RID_normal == p_normal)) {
			bool tiles = batch_texture.tile_mode != BatchTex::TILE_OFF;
			if (tiles == p_tile) {
				return n;
			}
		}
	}

	// Not found, create a new BatchTex entry.
	BatchTex new_batch_tex;
	new_batch_tex.RID_texture = p_texture;
	new_batch_tex.RID_normal = p_normal;
	new_batch_tex.flags = 0;

	RID diffuse_rid = p_texture;

	// Unwrap the CanvasTexture.
	if (p_texture.is_valid()) {
		typename T_API::CanvasTexture *ct = T_API::TextureStorage::get_singleton()->get_canvas_texture(p_texture);
		if (ct && ct->diffuse.is_valid()) {
			diffuse_rid = ct->diffuse;
		}
	}

	typename T_API::Texture *texture = T_API::TextureStorage::get_singleton()->get_texture(diffuse_rid);

	if (texture) {
		int w = MAX(1, texture->width);
		int h = MAX(1, texture->height);
		new_batch_tex.tex_pixel_size.set(1.0f / w, 1.0f / h);
	} else {
		new_batch_tex.tex_pixel_size.set(1.0f, 1.0f);
		new_batch_tex.flags = 0;
	}

	// Handle Tiling and Hardware Constraints
	if (p_tile) {
		if (texture) {
			new_batch_tex.tile_mode = BatchTex::TILE_NORMAL;

			// Check if the texture is a Non-Power of Two (NPOT)
			// GLES2/GLES1 struggles with NPOT repeating textures, so we force it in the shader if true.
			bool is_pot = (
				(next_power_of_2(texture->alloc_width) == (unsigned int)texture->alloc_width) &&
				(next_power_of_2(texture->alloc_height) == (unsigned int)texture->alloc_height)
			);

			if (!is_pot) {
				new_batch_tex.tile_mode = BatchTex::TILE_FORCE_REPEAT;
			}
		} else {
			new_batch_tex.tile_mode = BatchTex::TILE_OFF;
		}
	} else {
		new_batch_tex.tile_mode = BatchTex::TILE_OFF;
	}

	bdata.batch_textures.push_back(new_batch_tex);
	return bdata.batch_textures.size() - 1;
}

template <typename T_API>
void RasterizerCanvasBatcherCommon<T_API>::_prefill_default_batch(FillState &r_fill_state, RendererCanvasRender::Item::Command *p_command, const RendererCanvasRender::Item &p_item) {
	if (r_fill_state.curr_batch->type == BatcherEnums::BT_DEFAULT) {
		// We are already in a default batch.
		if (!r_fill_state.transform_extra_command) {
			// Just add this command to the existing default batch count.
			r_fill_state.curr_batch->num_commands++;
		} else {
			// If the first member of the batch is a transform we have to be careful
			if (!r_fill_state.curr_batch->num_commands) {
				r_fill_state.curr_batch->first_command = r_fill_state.transform_extra_command;
			}

			r_fill_state.curr_batch->num_commands += 2;
			r_fill_state.transform_extra_command = nullptr;
			r_fill_state.extra_matrix_sent = true;
			r_fill_state.transform_mode = r_fill_state.orig_transform_mode;
		}
	} else {
		// We were in a RECT/POLY batch, but hit a command we can't accelerate.
		// Close the accelerated batch and start a new DEFAULT batch.

		if (r_fill_state.transform_extra_command) {
			RendererCanvasRender::Item::Command *extra_command = r_fill_state.transform_extra_command;
			r_fill_state.transform_extra_command = nullptr;
			r_fill_state.extra_matrix_sent = true;

			// Send the extra to the GPU in a batch
			r_fill_state.curr_batch = _batch_request_new(false);
			if (unlikely(!r_fill_state.curr_batch)) {
				return;
			}
			r_fill_state.curr_batch->type = BatcherEnums::BT_DEFAULT;
			r_fill_state.curr_batch->first_command = extra_command;
			r_fill_state.curr_batch->num_commands = 1;
			r_fill_state.curr_batch->item = &p_item;

			r_fill_state.transform_mode = r_fill_state.orig_transform_mode;

			if (r_fill_state.orig_transform_mode != TM_NONE) {
				r_fill_state.transform_combined = p_item.final_transform;
			}

			// If the extra command is immediately followed by the current command, group them.
			if ((extra_command->next) == p_command) {
				r_fill_state.curr_batch->num_commands = 2;
				return;
			}
		}

		// Start a fresh default batch
		r_fill_state.curr_batch = _batch_request_new(false);
		if (unlikely(!r_fill_state.curr_batch)) {
			return;
		}
		r_fill_state.curr_batch->type = BatcherEnums::BT_DEFAULT;
		r_fill_state.curr_batch->first_command = p_command;
		r_fill_state.curr_batch->num_commands = 1;
		r_fill_state.curr_batch->item = &p_item;
	}
}
