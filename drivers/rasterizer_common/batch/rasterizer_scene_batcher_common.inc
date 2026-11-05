/**************************************************************************/
/*  rasterizer_scene_batcher_common.inc                                   */
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

template <typename T_API>
typename T_API::Scene *RasterizerSceneBatcherCommon<T_API>::get_this() { 
	return static_cast<typename T_API::Scene *>(this); 
}

template <typename T_API>
const typename T_API::Scene *RasterizerSceneBatcherCommon<T_API>::get_this() const { 
	return static_cast<const typename T_API::Scene *>(this); 
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_constructor() {
	batch_initialize();
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_initialize() {
	bdata.settings_use_batching = GLOBAL_GET("rendering/batching/use_batching");
	bdata.settings_dynamic_vertex_limit = GLOBAL_GET("rendering/limits/opengl/max_dynamic_vertex_limit");
	bdata.settings_item_reordering_lookahead = 4;
	bdata.settings_use_single_mesh_fallback = true;

	uint32_t threshold_bytes = static_cast<uint32_t>(GLOBAL_GET("rendering/limits/opengl/dynamic_buffer_flush_threshold_kb")) * 1024;

	uint32_t fvf_alignment = sizeof(BatchVertex3DTier4);
	bdata.dynamic_upload_threshold_bytes = threshold_bytes - (threshold_bytes % fvf_alignment);

	bdata.max_batch_draw_call_count = static_cast<uint32_t>(GLOBAL_GET("rendering/limits/opengl/max_batch_draw_call_count"));

	get_this()->_batch_get_hardware_limits(bdata.hardware_limits);

	const uint32_t max_verts = bdata.hardware_limits.max_vertices_per_buffer;
	bdata.base_vertices.create(max_verts);
	bdata.vertex_colors.create(max_verts);
	bdata.vertex_uv2s.create(max_verts);
	bdata.vertex_tangents.create(max_verts);
	bdata.vertex_weights.create(max_verts);
	bdata.vertex_matrix_indices.create(max_verts);

	bdata.unit_vertices.create(max_verts, sizeof(BatchVertex3DTier4));
	bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier1));
	bdata.indices.create(bdata.hardware_limits.max_indices_per_buffer);

	bdata.batches.create(256);
	bdata.items_joined.create(512);
	bdata.item_refs.create(2048);
	bdata.sort_items.create(2048);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_begin() {
	bdata.reset_scene();
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_end() {
	bdata.reset_scene();
	bdata.dynamic_upload_accumulator = 0;
	bdata.draw_call_accumulator = 0;
}

template <typename T_API>
template <typename BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_render_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent, bool p_replay) {
	if (!bdata.settings_use_batching || p_count == 0) {
		bdata.pass_mode = p_pass_mode;
		_render_item_state.reset();
		if (p_count > 0) {
			get_this()->scene_render_items_implementation(p_surfaces, p_count, p_camera_transform, p_transparent);
		}
		return;
	}

	// Only replay if the cached batches and item references are populated.
	// Prevents replaying stale opaque batches when a subsequent pass has 0 items.
	if (p_replay && bdata.batches.size() > 0 && bdata.item_refs.size() > 0) {
		// During additive light or shadow passes, we can bypass the CPU overhead of 
		// sorting and packing geometry. The VBOs were already populated during the 
		// base opaque pass, so we just iterate over the established batch list and 
		// re-issue the draw calls with the new pass state.
		bdata.pass_mode = p_pass_mode;
		_render_item_state.reset();
		for (uint32_t b = 0; b < (uint32_t)bdata.batches.size(); b++) {
			Batch3D &batch = bdata.batches[b];
			if (batch.num_verts == 0) {
				continue;
			}
			T_SURFACE *first_surface = static_cast<T_SURFACE *>(bdata.item_refs[batch.first_item_ref].surface);
			typename T_API::MaterialData *material = get_this()->_batch_get_material_data(first_surface);
			RS::PrimitiveType primitive = first_surface->primitive;
			
			// Restore the FVF state for this specific batch so the driver knows 
			// how to correctly interpret the VBO layout and apply transforms.
			bdata.fvf = batch.fvf;
			switch (batch.fvf) {
				case BatcherEnums::FVF_UNBATCHED:
					break;
				case BatcherEnums::FVF_3D_DEPTH_MINIMAL:
				case BatcherEnums::FVF_DEPTH_ONLY:
					bdata.unit_vertices.prepare(sizeof(BatchVertex3DDepthMinimal));
					break;
				case BatcherEnums::FVF_3D_DEPTH_ALPHA_MINIMAL:
				case BatcherEnums::FVF_DEPTH_ALPHA:
					bdata.unit_vertices.prepare(sizeof(BatchVertex3DDepthAlphaMinimal));
					break;
				case BatcherEnums::FVF_3D_TIER1_BASE:
					bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier1));
					break;
				case BatcherEnums::FVF_3D_TIER2_COLOR:
					bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier2));
					break;
				case BatcherEnums::FVF_3D_TIER3_TANGENT:
					bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier3));
					break;
				case BatcherEnums::FVF_3D_TIER4_PALETTE:
				case BatcherEnums::FVF_INSTANCED:
				case BatcherEnums::FVF_REGULAR:
				default:
					bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier4));
					break;
			}
			_render_item_state.curr_batch = &batch;
			get_this()->_batch_render_items(material, primitive, batch, p_transparent);
		}
		render_bypassed_items<T_SURFACE>(p_transparent);
		return;
	}

	bdata.reset_scene();
	_render_item_state.reset();
	bdata.pass_mode = p_pass_mode;

	// Separate batchable small props from unbatchable meshes
	record_items<T_SURFACE>(p_surfaces, p_count, p_camera_transform, p_transparent);

	// Only sort the items that survived the filter
	if (bdata.sort_items.size() > 0) {
		sort_items(p_transparent);
		
		// Pack & Render: Build VBOs and fire draw calls
		join_and_flush_sorted_items<p_pass_mode, T_SURFACE>(p_transparent);
	}

	// Draw the unbatchable meshes directly, bypassing the batcher overhead
	render_bypassed_items<T_SURFACE>(p_transparent);
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::record_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	for (int i = 0; i < p_count; i++) {
		T_SURFACE *surface = p_surfaces[i];
		if (
			bdata.pass_mode == BatcherEnums::PASS_MODE_COLOR &&
			!(surface->flags & T_SURFACE::FLAG_PASS_OPAQUE)
		) {
			continue;
		}
		
		uint32_t base_verts = 0;
		uint32_t base_indices = 0;
		
		// Quickly look up the base vertex count for the mesh complexity threshold.
		get_this()->_batch_get_instance_geometry_capacity(surface, base_verts, base_indices);

		if (base_verts == 0) {
			// This happens when primitive topology differs
			// forcing batch break
			continue;
		}

		// Get total verts counts
		uint32_t instances = surface->owner->instance_count >= 0 ? surface->owner->instance_count : 1;
		if (instances == 0) {
			continue;
		}
		uint32_t total_incoming_verts = base_verts * instances;

		// Skeletons cannot be batched. If we find any in the mesh, don't even try.
		bool has_deformation = false;
		if (surface->owner->data->base_type == RS::INSTANCE_MESH) {
			bool has_skel = false;
			if (surface->owner->mesh_instance.is_valid()) {
				typename T_API::MeshInstance *mi = T_API::MeshStorage::get_singleton()->get_mesh_instance(surface->owner->mesh_instance);
				if (mi && mi->skeleton.is_valid()) {
					has_skel = true;
				}
			}
			
			if (has_skel || T_API::MeshStorage::get_singleton()->mesh_get_blend_shape_count(surface->owner->data->base) > 0) {
				has_deformation = true;
			}
		}

		// Some models may have very long AABBs
		// whose length_squared() overflows 32-bit float to INF/NaN and corrupts planar depth
		// or vertex coordinates during software transformation.
		const AABB &aabb = surface->owner->transformed_aabb;
		const bool has_extreme_bounds = (
			!Math::is_finite(aabb.position.x) || !Math::is_finite(aabb.position.y) || !Math::is_finite(aabb.position.z) ||
			!Math::is_finite(aabb.size.x) || !Math::is_finite(aabb.size.y) || !Math::is_finite(aabb.size.z) ||
			Math::abs(aabb.size.x) > 1e6f || Math::abs(aabb.size.y) > 1e6f || Math::abs(aabb.size.z) > 1e6f
		);

		// Do not batch if:
		// 1. The base mesh passes the complexity limit
		// 2. Some kind of skeleton mesh has been detected
		// 3. Overflowing bounds
		if (total_incoming_verts > bdata.settings_dynamic_vertex_limit || has_deformation || has_extreme_bounds) {
			if (!p_transparent) {
				// Opaque massive models don't need strict sorting. Just dump them in the bypass list.
				bdata.bypassed_opaque_items.push_back((void*)surface);
			} else {
				// Transparent massive models still need
				// depth calculation for BTF rendering
				BSortItem3D bypassed_trans;
				bypassed_trans.item = (void*)surface;
				bypassed_trans.depth = get_this()->_batch_get_item_depth(surface, p_camera_transform);
				bypassed_trans.state_hash = 0; // Don't care for bypass transparent
				bdata.bypassed_transparent_items.push_back(bypassed_trans);
			}
			continue;
		}

		// This is a small prop. Proceed with tracking.
		BSortItem3D *s = bdata.sort_items.request();
		if (!s) {
			bdata.sort_items.grow();
			s = bdata.sort_items.request();
			if (unlikely(!s)) {
				ERR_PRINT_ONCE("GLES Scene Batcher: Failed to allocate BSortItem even after growing array.");
				break;
			}
		}

		s->item = (void *)surface;
		s->aabb = surface->owner->transformed_aabb;
		
		// Calculate linear depth
		s->depth = get_this()->_batch_get_item_depth(surface, p_camera_transform);

		// Generate a state hash.
		s->state_hash = get_this()->_batch_get_state_hash(surface);

		// Get mesh ID
		s->mesh_id = surface->surface ? static_cast<uint32_t>((uintptr_t)surface->surface >> 4) : 0;
		s->surface_index = surface->surface_index;
	}
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::_sort_items_match_3d(const BSortItem3D &p_a, const BSortItem3D &p_b) const {
	// Mask out bits 16-31 (source mesh/surface ID) so distinct meshes with identical materials can join.
	const uint64_t mask = 0xFFFFFFFF0000FFFFULL;
	return (p_a.state_hash & mask) == (p_b.state_hash & mask);
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::sort_items_from_3d(int p_start) {
	// Heuristic look-ahead window for back-to-front sorted 3D items.
	// Scans ahead up to settings_item_reordering_lookahead items when a state break occurs,
	// and swaps a matching future item into the neighbor slot if its world-space AABB
	// does not intersect the second item or any sandwiched items in between.
	if ((p_start + 1) >= bdata.sort_items.size()) {
		return false;
	}

	const BSortItem3D &start = bdata.sort_items[p_start];
	BSortItem3D &second = bdata.sort_items[p_start + 1];

	// If the immediate neighbors already match state, no look-ahead is needed.
	if (_sort_items_match_3d(start, second)) {
		return false;
	}

	// Local cached AABB of the second item.
	const AABB &second_aabb = second.aabb;

	// If the start and 2nd items overlap spatially, we cannot reorder across them.
	if (start.aabb.intersects(second_aabb)) {
		return false;
	}

	// Scan ahead within the look-ahead window for a compatible candidate.
	const int test_last = MIN(
		bdata.sort_items.size(),
		p_start + 2 + bdata.settings_item_reordering_lookahead
	);
	for (int test = p_start + 2; test < test_last; test++) {
		BSortItem3D &test_sort_item = bdata.sort_items[test];

		// If the candidate overlaps the second item, we cannot swap over it at all
		// without risking depth-sorting/alpha-blending artifacts.
		if (second_aabb.intersects(test_sort_item.aabb)) {
			return false;
		}

		// Do their state hashes match?
		if (!_sort_items_match_3d(start, test_sort_item)) {
			continue;
		}

		// We can only swap if there are no AABB overlaps with sandwiched neighbors.
		// Start from p_start + 2 as p_start + 1 (second) was already tested above.
		bool ok = true;
		for (int sn = p_start + 2; sn < test; sn++) {
			const AABB &sandwich_aabb = bdata.sort_items[sn].aabb;

			// Check if the candidate hits the sandwich moving backward,
			// or if the second item hits the sandwich moving forward.
			if (test_sort_item.aabb.intersects(sandwich_aabb) || second_aabb.intersects(sandwich_aabb)) {
				ok = false;
				break;
			}
		}

		if (!ok) {
			continue;
		}

		// Safe to exchange them to merge batches.
		BSortItem3D temp;
		temp.assign(second);
		second.assign(test_sort_item);
		test_sort_item.assign(temp);
		return true;
	}

	return false;
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::sort_items(bool p_transparent) {
	if (p_transparent) {
		SortArray<BSortItem3D, SortTransparent> sorter{};
		sorter.sort(&bdata.sort_items[0], bdata.sort_items.size());

		// Run heuristic look-ahead pass on transparent items to recover batches
		// broken by strict back-to-front depth ordering when AABBs are disjoint.
		if (bdata.settings_item_reordering_lookahead > 0 && bdata.sort_items.size() > 2) {
			for (int s = 0; s < bdata.sort_items.size() - 2; s++) {
				sort_items_from_3d(s);
			}
		}
	} else {
		SortArray<BSortItem3D, SortOpaque> sorter{};
		sorter.sort(&bdata.sort_items[0], bdata.sort_items.size());
	}
}

template <typename T_API>
template <typename BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::join_sorted_items() {
	// Stage 1 of delayed packing:
	// Iterates over the sorted items and builds lightweight BItemJoined3D groups and BItemRef3D references.
	// Pre-evaluates TransformMode3D, normal cofactor matrices, and required FVF feature flags
	// without touching VBO staging memory.

	// Reset states first.
	_render_item_state.reset();
	bdata.items_joined.reset();
	bdata.item_refs.reset();

	BItemJoined3D *curr_joined = nullptr;
	
	// =========================
	// Grouping pass
	// =========================
	for (uint32_t i = 0; i < (uint32_t)bdata.sort_items.size(); i++) {
		T_SURFACE *surface = static_cast<T_SURFACE *>(bdata.sort_items[i].item);
		if (!surface || !surface->owner) {
			continue;
		}

		uint32_t base_verts = 0;
		uint32_t base_indices = 0;

		// Determine capacity beforehand
		get_this()->_batch_get_instance_geometry_capacity(surface, base_verts, base_indices);

		uint32_t instances = surface->owner->instance_count >= 0 ? surface->owner->instance_count : 1;
		if (instances == 0 || base_verts == 0) {
			continue;
		}

		uint32_t incoming_verts = base_verts * instances;
		uint32_t incoming_indices = base_indices * instances;
		uint32_t incoming_uniform_vectors = instances * 3;

		// Get the surface flags
		uint32_t surf_flags = get_this()->_batch_get_surface_flags(surface);
		
		bool capacity_full = false;
		if (curr_joined) {
			// A joined group must break if appending the new item would exceed:
			// - The maximum allocated vertex buffer size.
			// - The maximum allocated index buffer size.
			// - The maximum number of uniforms available on the hardware for matrix palettes.
			if ((curr_joined->total_verts + incoming_verts > (uint32_t)bdata.unit_vertices.max_size()) ||
				(curr_joined->total_indices + incoming_indices > (uint32_t)bdata.indices.max_size()) ||
				(_render_item_state.consumed_uniform_vectors + incoming_uniform_vectors > bdata.hardware_limits.max_matrix_palette_vectors)) {
				capacity_full = true;
			}
		}

		bool can_join = false;
		if (curr_joined && !capacity_full) {
			uint64_t masked_item_hash = bdata.sort_items[i].state_hash & 0xFFFFFFFF0000FFFFULL;
			uint64_t masked_curr_hash = curr_joined->state_hash & 0xFFFFFFFF0000FFFFULL;
			bool is_multimesh = (surf_flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;
			bool curr_is_multimesh = (curr_joined->flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;

			// Triangle strips and line strips cannot be concatenated without degenerate stitching,
			// so they always force a batch break.
			bool strip_primitive = (
				surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP ||
				surface->primitive == RS::PRIMITIVE_LINE_STRIP
			);

			// Custom vertex shaders that rely on per-node model matrices or local coordinates
			// (e.g., MODEL_MATRIX, NODE_POSITION_WORLD, VERTEX manipulation)
			// must not join across items.
			bool prevent_join = (
				(surf_flags | curr_joined->flags) &
				(BatcherEnums::BTF_3D_PREVENT_ITEM_JOINING | BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING)
			) != 0;

			if (
				!strip_primitive &&
				!prevent_join &&
				is_multimesh == curr_is_multimesh &&
				surface->primitive == _render_item_state.current_primitive &&
				masked_item_hash == masked_curr_hash
			) {
				can_join = true;
			}
		}

		if (!can_join) {
			// Start a new joined item group
			curr_joined = bdata.items_joined.request_with_grow();
			if (unlikely(!curr_joined)) {
				break;
			}
			curr_joined->first_item_ref = bdata.item_refs.size();
			curr_joined->num_item_refs = 0;
			curr_joined->bounding_aabb = bdata.sort_items[i].aabb;
			curr_joined->flags = surf_flags;
			curr_joined->state_hash = bdata.sort_items[i].state_hash;
			curr_joined->total_verts = 0;
			curr_joined->total_indices = 0;

			_render_item_state.current_state_hash = bdata.sort_items[i].state_hash;
			_render_item_state.current_material_data = get_this()->_batch_get_material_data(surface);
			_render_item_state.current_primitive = surface->primitive;
			_render_item_state.consumed_uniform_vectors = 0;
		} else {
			// Extend existing joined item group and accumulate required FVF flags
			curr_joined->bounding_aabb.merge_with(bdata.sort_items[i].aabb);
			curr_joined->flags |= surf_flags;
		}

		BItemRef3D *ref = bdata.item_refs.request_with_grow();
		if (unlikely(!ref)) {
			break;
		}

		// Pre-evaluate transform mode and cofactor matrix once per reference
		ref->surface = (void *)surface;
		ref->transform = surface->owner->transform;
		ref->transform_mode = _find_transform_mode_3d(ref->transform);
		if (ref->transform_mode == TM_3D_ALL) {
			ref->normal_cofactor = _compute_normal_cofactor(ref->transform.basis);
		} else {
			ref->normal_cofactor = Basis();
		}
		ref->modulate = get_this()->_batch_get_surface_modulate(surface);
		ref->base_verts = base_verts;
		ref->base_indices = base_indices;
		ref->instance_count = instances;

		curr_joined->num_item_refs++;
		curr_joined->total_verts += incoming_verts;
		curr_joined->total_indices += incoming_indices;
		_render_item_state.consumed_uniform_vectors += incoming_uniform_vectors;
	}
}

template <typename T_API>
template <typename BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::join_and_flush_sorted_items(bool p_transparent) {
	// Stage 1: Build lightweight BItemJoined3D and BItemRef3D lists
	join_sorted_items<p_pass_mode, T_SURFACE>();

	// Reset the state tracker because join_sorted_items mutated it for CPU grouping.
	_render_item_state.reset();

	// Also resets the batches
	bdata.batches.reset();

	// Stage 2: Delayed VRAM packing and Dynamic FVF Promotion per joined group
	for (uint32_t j = 0; j < (uint32_t)bdata.items_joined.size(); j++) {
		const BItemJoined3D &joined = bdata.items_joined[j];
		if (joined.num_item_refs == 0 || joined.total_verts == 0) {
			continue;
		}

		const BItemRef3D &first_ref = bdata.item_refs[joined.first_item_ref];
		T_SURFACE *first_surface = static_cast<T_SURFACE *>(first_ref.surface);
		typename T_API::MaterialData *material = get_this()->_batch_get_material_data(first_surface);
		RS::PrimitiveType primitive = first_surface->primitive;

		Batch3D *batch = _batch_request_new();
		if (unlikely(!batch)) {
			break;
		}

		batch->first_joined_index = j;
		batch->first_item_ref = joined.first_item_ref;
		batch->num_item_refs = joined.num_item_refs;
		batch->first_vert = 0;
		batch->num_verts = 0;
		batch->first_index = 0;
		batch->num_indices = 0;
		batch->vbo_byte_offset = 0;
		batch->ibo_byte_offset = 0;
		batch->flags = joined.flags;

		_render_item_state.curr_batch = batch;
		_render_item_state.consumed_uniform_vectors = 0;

		const bool use_hardware_transform = joined.use_hardware_transform();

		// If a joined group contains only a single non-instanced mesh surface (and the driver
		// does not require CPU packing for UV2/material baking), skip copying vertices into
		// unit_vertices and skip dynamic VBO upload. Tag the batch as FVF_UNBATCHED so both
		// the current flush and any subsequent replay passes bind the mesh's native static VBO.
		if (
			bdata.settings_use_single_mesh_fallback &&
			use_hardware_transform &&
			get_this()->_batch_can_use_single_mesh_fallback(first_surface)
		) {
			batch->fvf = BatcherEnums::FVF_UNBATCHED;
			batch->num_verts = joined.total_verts;
			batch->num_indices = joined.total_indices;
			batch->vbo_index = 0xFFFFFFFF;
			flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
			continue;
		}

		for (uint32_t i = 0; i < joined.num_item_refs; i++) {
			const BItemRef3D &ref = bdata.item_refs[joined.first_item_ref + i];

			// Retry current item
			prefill_joined_item<p_pass_mode, T_SURFACE>(ref, use_hardware_transform, i, joined.flags);
		}
		
		// Flush the batch
		flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
	}
}

template <typename T_API>
template <class T_SURFACE>
bool RasterizerSceneBatcherCommon<T_API>::try_join_item(T_SURFACE *p_surface, RenderItemState3D &r_ris) {
	uint64_t item_hash = get_this()->_batch_get_state_hash(p_surface);
	bool is_multimesh = (
		p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH
	);
	
	bool current_is_multimesh = false;
	if (r_ris.curr_batch && r_ris.curr_batch->num_items > 0) {
		T_SURFACE *first_surf = static_cast<T_SURFACE *>(bdata.sort_items[r_ris.curr_batch->first_item_index].item);
		current_is_multimesh = (first_surf->owner->data->base_type == RS::INSTANCE_MULTIMESH);
	}
	
	// Topology switch between instanced and regular
	// always break batches
	if (r_ris.curr_batch && r_ris.curr_batch->num_items > 0 && is_multimesh != current_is_multimesh) {
		return false;
	}

	// Primitives that are either triangle strips OR
	// line strips always break batches
	if (p_surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP || p_surface->primitive == RS::PRIMITIVE_LINE_STRIP) {
		return false;
	}

	// 2 or more items that differ in primitives
	// always breaks batches
	if (r_ris.curr_batch && r_ris.curr_batch->num_items > 0 && p_surface->primitive != r_ris.current_primitive) {
		return false;
	}

	// A mesh that has any form of skeletons always breaks batches.
	if (p_surface->owner->data->base_type == RS::INSTANCE_MESH) {
		if (p_surface->owner->skeleton.is_valid() || T_API::MeshStorage::get_singleton()->mesh_get_blend_shape_count(p_surface->owner->data->base) > 0) {
			return false;
		}
	}

	// Mask out bits 16-31 (surface id / mesh id)
	uint64_t masked_item_hash = item_hash & 0xFFFFFFFF0000FFFFULL;
	uint64_t masked_current_hash = r_ris.current_state_hash & 0xFFFFFFFF0000FFFFULL;

	return (masked_item_hash == masked_current_hash);
}

template <typename T_API>
template <class T_BATCH_VERT, bool INCLUDE_COLOR_UV2, bool INCLUDE_TANGENT, bool INCLUDE_PALETTE>
void RasterizerSceneBatcherCommon<T_API>::_translate_batches_to_larger_FVF_3d(uint32_t p_num_verts) {
	// Interleaves the base Tier 1 vertex stream (pos, normal, uv) with any active
	// auxiliary attribute streams (color, uv2, tangent, palette indices/weights) into
	// the promoted target FVF struct inside unit_vertices prior to VBO upload.
	bdata.unit_vertices.prepare(sizeof(T_BATCH_VERT));
	T_BATCH_VERT *dst = reinterpret_cast<T_BATCH_VERT *>(bdata.unit_vertices.request(p_num_verts));
	ERR_FAIL_NULL(dst);

	const BatchVertex3DTier1 *src_base = bdata.base_vertices.get_data();
	const BatchColor *src_col = bdata.vertex_colors.get_data();
	const BatchVector2 *src_uv2 = bdata.vertex_uv2s.get_data();
	const BatchVector4 *src_tan = bdata.vertex_tangents.get_data();
	const BatchVector4 *src_weights = bdata.vertex_weights.get_data();
	const uint32_t *src_m_idx = bdata.vertex_matrix_indices.get_data();

	const bool has_col = bdata.vertex_colors.size() >= (int)p_num_verts;
	const bool has_uv2 = bdata.vertex_uv2s.size() >= (int)p_num_verts;
	const bool has_tan = bdata.vertex_tangents.size() >= (int)p_num_verts;
	const bool has_weights = bdata.vertex_weights.size() >= (int)p_num_verts;
	const bool has_m_idx = bdata.vertex_matrix_indices.size() >= (int)p_num_verts;

	for (uint32_t v = 0; v < p_num_verts; v++) {
		dst[v].pos = src_base[v].pos;
		dst[v].normal = src_base[v].normal;
		dst[v].uv = src_base[v].uv;

		if constexpr (INCLUDE_COLOR_UV2) {
			if (has_col) {
				dst[v].color = src_col[v];
			} else {
				dst[v].color.set_white();
			}
			if (has_uv2) {
				dst[v].uv2 = src_uv2[v];
			} else {
				dst[v].uv2.set(0.0f, 0.0f);
			}
		}

		if constexpr (INCLUDE_TANGENT) {
			if (has_tan) {
				dst[v].tangent = src_tan[v];
			} else {
				dst[v].tangent.set(1.0f, 0.0f, 0.0f, 1.0f);
			}
		}

		if constexpr (INCLUDE_PALETTE) {
			if (has_m_idx) {
				memcpy(dst[v].matrix_index, &src_m_idx[v], sizeof(uint32_t));
			} else {
				memset(dst[v].matrix_index, 0, sizeof(uint32_t));
			}
			if (has_weights) {
				dst[v].weights = src_weights[v];
			} else {
				dst[v].weights.set(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}
	}
}

template <typename T_API>
template <typename BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::prefill_joined_item(const BItemRef3D &p_ref, bool p_use_hardware_transform, uint32_t p_item_index, uint32_t p_joined_flags) {
	T_SURFACE *surface = static_cast<T_SURFACE *>(p_ref.surface);
	if (!surface || p_ref.base_verts == 0) {
		return;
	}

	const bool is_multimesh = (p_joined_flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;
	const uint32_t incoming_verts = p_ref.base_verts * p_ref.instance_count;
	const uint32_t incoming_indices = p_ref.base_indices * p_ref.instance_count;
	const uint32_t incoming_uniform_vectors = p_ref.instance_count * 3;

	// Evaluate the combined flags of the joined group on the first item
	// and commit to the lowest possible FVF tier that satisfies all members.
	if (bdata.total_verts == 0) {
		// Explicitly call the templated parameter for performance
		// and better DCE. We guarantee that `bdata.pass_mode` is in
		// sync as well.
		if constexpr (p_pass_mode == BatcherEnums::PASS_MODE_SHADOW || p_pass_mode == BatcherEnums::PASS_MODE_DEPTH) {
			if (p_joined_flags & BatcherEnums::BTF_3D_USES_ALPHA_CLIP) {
				bdata.fvf = BatcherEnums::FVF_3D_DEPTH_ALPHA_MINIMAL;
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DDepthAlphaMinimal));
			} else {
				bdata.fvf = BatcherEnums::FVF_3D_DEPTH_MINIMAL;
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DDepthMinimal));
			}
		} else {
			if (
				(p_joined_flags & BatcherEnums::BTF_3D_USES_SKINNING) ||
				get_this()->_batch_requires_palette_fvf()
			) {
				bdata.fvf = BatcherEnums::FVF_3D_TIER4_PALETTE;
			} else if (p_joined_flags & BatcherEnums::BTF_3D_USES_TANGENTS) {
				bdata.fvf = BatcherEnums::FVF_3D_TIER3_TANGENT;
			} else if (p_joined_flags & (BatcherEnums::BTF_3D_USES_COLORS | BatcherEnums::BTF_3D_USES_UV2)) {
				bdata.fvf = BatcherEnums::FVF_3D_TIER2_COLOR;
			} else {
				bdata.fvf = BatcherEnums::FVF_3D_TIER1_BASE;
			}
		}
		_render_item_state.curr_batch->fvf = bdata.fvf;
	}

	const uint32_t start_vert = bdata.total_verts;
	uint16_t *inds = incoming_indices > 0 ? bdata.indices.request(incoming_indices) : nullptr;

	if constexpr (p_pass_mode == BatcherEnums::PASS_MODE_SHADOW || p_pass_mode == BatcherEnums::PASS_MODE_DEPTH) {
		// Only perform the batch fill for shadow or
		// depth pass modes
		uint8_t *depth_bvs = bdata.unit_vertices.request(incoming_verts);
		if (!depth_bvs) {
			return;
		}
		if (is_multimesh) {
			get_this()->_batch_fill_multimesh_depth(p_ref, depth_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		} else {
			get_this()->_batch_fill_instance_depth(p_ref, depth_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		}
	} else {
		// Regular batch fill
		BatchVertex3DTier1 *base_bvs = bdata.base_vertices.request(incoming_verts);
		if (!base_bvs) {
			return;
		}

		BatchColor *col_bvs = (bdata.fvf >= BatcherEnums::FVF_3D_TIER2_COLOR) ? bdata.vertex_colors.request(incoming_verts) : nullptr;
		BatchVector2 *uv2_bvs = (bdata.fvf >= BatcherEnums::FVF_3D_TIER2_COLOR) ? bdata.vertex_uv2s.request(incoming_verts) : nullptr;
		BatchVector4 *tan_bvs = (bdata.fvf >= BatcherEnums::FVF_3D_TIER3_TANGENT) ? bdata.vertex_tangents.request(incoming_verts) : nullptr;
		BatchVector4 *w_bvs = (bdata.fvf >= BatcherEnums::FVF_3D_TIER4_PALETTE) ? bdata.vertex_weights.request(incoming_verts) : nullptr;
		uint32_t *m_bvs = (bdata.fvf >= BatcherEnums::FVF_3D_TIER4_PALETTE) ? bdata.vertex_matrix_indices.request(incoming_verts) : nullptr;

		if (is_multimesh) {
			get_this()->_batch_fill_multimesh_tiered(p_ref, base_bvs, col_bvs, uv2_bvs, tan_bvs, w_bvs, m_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		} else {
			get_this()->_batch_fill_instance_tiered(p_ref, base_bvs, col_bvs, uv2_bvs, tan_bvs, w_bvs, m_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		}
	}

	bdata.total_verts += incoming_verts;
	bdata.total_indices += incoming_indices;
	_render_item_state.curr_batch->num_verts += incoming_verts;
	_render_item_state.curr_batch->num_indices += incoming_indices;
	_render_item_state.consumed_uniform_vectors += incoming_uniform_vectors;
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::flush_render_batches(typename T_API::MaterialData *p_material_data, RS::PrimitiveType p_primitive, Batch3D &p_batch, bool p_transparent) {
	if (p_batch.num_verts == 0 || p_primitive == RS::PRIMITIVE_MAX) {
		bdata.reset_flush();
		return;
	}

	// Intercept single-item batches flagged for the single-mesh direct draw fallback.
	if (p_batch.fvf == BatcherEnums::FVF_UNBATCHED) {
		bdata.fvf = BatcherEnums::FVF_UNBATCHED;
		get_this()->_batch_render_items(p_material_data, p_primitive, p_batch, p_transparent);

		_calculate_draw_call_cost_or_flush(p_batch);

		bdata.reset_flush();
		_render_item_state.consumed_uniform_vectors = 0;
		return;
	}

	// Prepare to upload the specific FVFs
	switch (bdata.fvf) {
		case BatcherEnums::FVF_3D_DEPTH_MINIMAL:
		case BatcherEnums::FVF_3D_DEPTH_ALPHA_MINIMAL:
		case BatcherEnums::FVF_DEPTH_ONLY:
		case BatcherEnums::FVF_DEPTH_ALPHA:
			break;
		case BatcherEnums::FVF_3D_TIER1_BASE: {
			bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier1));
			uint8_t *dst = bdata.unit_vertices.request(p_batch.num_verts);
			if (dst) {
				memcpy(dst, bdata.base_vertices.get_data(), p_batch.num_verts * sizeof(BatchVertex3DTier1));
			}
		} break;
		case BatcherEnums::FVF_3D_TIER2_COLOR:
			_translate_batches_to_larger_FVF_3d<BatchVertex3DTier2, true, false, false>(p_batch.num_verts);
			break;
		case BatcherEnums::FVF_3D_TIER3_TANGENT:
			_translate_batches_to_larger_FVF_3d<BatchVertex3DTier3, true, true, false>(p_batch.num_verts);
			break;
		case BatcherEnums::FVF_3D_TIER4_PALETTE:
		case BatcherEnums::FVF_INSTANCED:
		case BatcherEnums::FVF_REGULAR:
		default:
			_translate_batches_to_larger_FVF_3d<BatchVertex3DTier4, true, true, true>(p_batch.num_verts);
			break;
	}

	// Upload batched VBOs
	get_this()->_batch_upload_buffers(p_batch);

	// Delegate the API-specific state binding and multi-draw dispatching to the driver
	get_this()->_batch_render_items(p_material_data, p_primitive, p_batch, p_transparent);

	_calculate_draw_call_cost_or_flush(p_batch);
	
	// Reset VBO cursors
	bdata.reset_flush();
	_render_item_state.consumed_uniform_vectors = 0; // Release uniform locks for the next batch
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::render_bypassed_items(bool p_transparent) {
	// Reset the tracker before bypassing
	_render_item_state.reset();

	if (!p_transparent) {
		for (uint32_t i = 0; i < (uint32_t)bdata.bypassed_opaque_items.size(); i++) {
			get_this()->_render_single_item_immediate(static_cast<T_SURFACE*>(bdata.bypassed_opaque_items[i]));
		}
	} else {
		SortArray<BSortItem3D, SortTransparent> sorter{};
		sorter.sort(bdata.bypassed_transparent_items.ptr(), bdata.bypassed_transparent_items.size());

		for (uint32_t i = 0; i < (uint32_t)bdata.bypassed_transparent_items.size(); i++) {
			get_this()->_render_single_item_immediate(static_cast<T_SURFACE*>(bdata.bypassed_transparent_items[i].item));
		}
	}
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::_software_transform_vertex(BatchVector3 &r_v, const Transform3D &p_tr) const {
	Vector3 vc(r_v.x, r_v.y, r_v.z);
	vc = p_tr.xform(vc);
	r_v.set(vc);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::_software_transform_normal(BatchVector3 &r_n, const Transform3D &p_tr) const {
	const Basis cofactor = _compute_normal_cofactor(p_tr.basis);
	_software_transform_normal_precomputed(r_n, cofactor);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::_software_transform_normal_precomputed(BatchVector3 &r_n, const Basis &p_cofactor) const {
	Vector3 nc(r_n.x, r_n.y, r_n.z);
	nc = p_cofactor.xform(nc).normalized();
	r_n.set(nc);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::_calculate_draw_call_cost_or_flush(Batch3D &p_batch) const {
	RasterizerSceneBatcherCommon<T_API> *self = const_cast<RasterizerSceneBatcherCommon<T_API> *>(this);

	if (p_batch.fvf != BatcherEnums::FVF_UNBATCHED) {
		// Calculate the cost of this call
		// and verify if we can keep it in the GPU "inventory."
		// Don't count unbatched since they bypass the batcher
		// anyway.
		self->bdata.dynamic_upload_accumulator += (
			p_batch.num_verts * self->bdata.unit_vertices.get_unit_size_bytes()
		) + (p_batch.num_indices * sizeof(uint16_t));
	}

	// Always increment,
	self->bdata.draw_call_accumulator++;

	if (
		(self->bdata.dynamic_upload_threshold_bytes > 0 && self->bdata.dynamic_upload_accumulator >= self->bdata.dynamic_upload_threshold_bytes) ||
		(self->bdata.max_batch_draw_call_count > 0 && self->bdata.draw_call_accumulator >= self->bdata.max_batch_draw_call_count)
	) {
		// It is safer to manually flush and drop
		// the inventory from the GPU than to keep it.
		self->get_this()->_batch_threshold_flush();
		self->bdata.dynamic_upload_accumulator = 0;
		self->bdata.draw_call_accumulator = 0;
	}
}
