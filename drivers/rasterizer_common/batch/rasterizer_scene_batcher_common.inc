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
	bdata.settings_colored_vertex_format_threshold = 0.25f;

	uint32_t threshold_bytes = static_cast<uint32_t>(GLOBAL_GET("rendering/limits/opengl/dynamic_buffer_flush_threshold_kb")) * 1024;

	uint32_t fvf_alignment = sizeof(BatchVertex3DTier4);
	bdata.dynamic_upload_threshold_bytes = threshold_bytes - (threshold_bytes % fvf_alignment);

	bdata.max_batch_draw_call_count = static_cast<uint32_t>(GLOBAL_GET("rendering/limits/opengl/max_batch_draw_call_count"));

	get_this()->_batch_get_hardware_limits(bdata.hardware_limits);

	const uint32_t max_verts = bdata.hardware_limits.max_vertices_per_buffer;
	const bool ok = (
		bdata.unit_vertices.create(max_verts, sizeof(BatchVertex3DTier4)) &&
		bdata.indices.create(bdata.hardware_limits.max_indices_per_buffer) &&
		bdata.batches.create(256) &&
		bdata.items_joined.create(512) &&
		bdata.item_refs.create(2048) &&
		bdata.sort_items.create(2048)
	);

	bdata.is_initialized = ok;
	if (ok) {
		bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier1));
	} else {
		ERR_PRINT_ONCE("GLES Scene Batcher: Failed to allocate staging buffers. Falling back to immediate rendering.");
	}
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
	if (!bdata.is_initialized || !bdata.settings_use_batching || p_count <= 0 || !p_surfaces) {
		bdata.pass_mode = p_pass_mode;
		_render_item_state.reset();
		if (p_count > 0 && p_surfaces) {
			get_this()->scene_render_items_implementation(p_surfaces, p_count, p_camera_transform, p_transparent);
		}
		return;
	}

	// Only replay if the base pass completed cleanly without client-buffer wraparound
	// and the cached batches and item references are populated.
	if (p_replay && bdata.replay_valid && !bdata.client_buffer_wrapped && bdata.batches.size() > 0 && bdata.item_refs.size() > 0) {
		bdata.pass_mode = p_pass_mode;
		_render_item_state.reset();
		for (uint32_t b = 0; b < (uint32_t)bdata.batches.size(); b++) {
			Batch3D &batch = bdata.batches[b];
			if (batch.num_verts == 0) {
				continue;
			}
			if (
				batch.first_item_ref >= (uint32_t)bdata.item_refs.size() ||
				(batch.first_item_ref + batch.num_item_refs) > (uint32_t)bdata.item_refs.size()
			) {
				continue;
			}

			T_SURFACE *first_surface = static_cast<T_SURFACE *>(bdata.item_refs[batch.first_item_ref].surface);
			if (unlikely(!first_surface)) {
				continue;
			}

			typename T_API::MaterialData *material = get_this()->_batch_get_material_data(first_surface);
			RS::PrimitiveType primitive = first_surface->primitive;

			bdata.fvf = batch.fvf;
			_prepare_unit_vertices_for_fvf(batch.fvf);
			_render_item_state.curr_batch = &batch;
			if (unlikely(!get_this()->_batch_render_items(material, primitive, batch, p_transparent))) {
				_demote_item_refs_to_bypass<true, T_SURFACE>(batch.first_item_ref, batch.num_item_refs, p_transparent);
			}
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
	bdata.replay_valid = !bdata.client_buffer_wrapped;
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::record_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	for (int i = 0; i < p_count; i++) {
		T_SURFACE *surface = p_surfaces[i];
		if (unlikely(!surface || !surface->owner || !surface->owner->data)) {
			continue;
		}
		
		// Skip depth-only pre-pass surfaces during the opaque color pass.
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
		const bool is_particles = (surface->owner->data->base_type == RS::INSTANCE_PARTICLES);
		const bool is_multimesh = (surface->owner->data->base_type == RS::INSTANCE_MULTIMESH);
		const bool is_strip = (
			surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP ||
			surface->primitive == RS::PRIMITIVE_LINE_STRIP
		);
		const uint32_t surf_flags = get_this()->_batch_get_surface_flags(surface);
		const uint64_t current_frame = RSG::rasterizer->get_frame_number();

		if (surface->last_transform_frame != current_frame) {
			// Evaluate dynamic motion once per frame so shadow, depth, and color passes agree.
			const uint32_t cur_xform_hash = _hash_transform_3d(surface->owner->transform);
			surface->is_dynamic_frame = (surface->prev_frame_transform_hash != 0 && surface->prev_frame_transform_hash != cur_xform_hash);
			surface->prev_frame_transform_hash = cur_xform_hash;
			surface->last_transform_frame = current_frame;
		}

		const bool is_dynamic = surface->is_dynamic_frame;
		const bool wants_mpi = _should_use_mpi(base_verts, base_indices, instances, is_multimesh, is_strip, is_dynamic, surf_flags);
		const bool can_use_static_lane = (
			bdata.settings_use_single_mesh_fallback &&
			get_this()->_batch_can_use_single_mesh_fallback(surface)
		);
		const bool wants_static_lane = !wants_mpi && _should_use_static_vbo_loop(base_verts, base_indices, instances, is_multimesh, is_strip, surf_flags);

		// Route deformed meshes, particles, extreme AABBs,
		// or unsupported static-lane candidates to RSII bypass.
		if (
			has_deformation ||
			is_particles ||
			has_extreme_bounds ||
			(wants_static_lane && !can_use_static_lane)
		) {
			const float depth = p_transparent ? get_this()->_batch_get_item_depth(surface, p_camera_transform) : 0.0f;
			_bypass_surface(surface, depth, p_transparent);
			continue;
		}

		// This is a small prop. Proceed with tracking.
		BSortItem3D *s = bdata.sort_items.request_with_grow();
		if (unlikely(!s)) {
			ERR_PRINT_ONCE("GLES Scene Batcher: Failed to allocate BSortItem. Demoting remaining surfaces to immediate lane.");
			for (int rem = i; rem < p_count; rem++) {
				T_SURFACE *rem_surface = p_surfaces[rem];
				if (unlikely(!rem_surface || !rem_surface->owner || !rem_surface->owner->data)) {
					continue;
				}
				if (bdata.pass_mode == BatcherEnums::PASS_MODE_COLOR && !(rem_surface->flags & T_SURFACE::FLAG_PASS_OPAQUE)) {
					continue;
				}
				const float rem_depth = p_transparent ? get_this()->_batch_get_item_depth(rem_surface, p_camera_transform) : 0.0f;
				_bypass_surface(rem_surface, rem_depth, p_transparent);
			}
			break;
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
		s->use_static_vbo_loop = wants_static_lane;
		s->use_mpi = wants_mpi;
	}
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::_sort_items_match_3d(const BSortItem3D &p_a, const BSortItem3D &p_b) const {
	if ((p_a.state_hash & MASK_ID) != (p_b.state_hash & MASK_ID)) {
		return false;
	}
	if (p_a.use_static_vbo_loop != p_b.use_static_vbo_loop || p_a.use_mpi != p_b.use_mpi) {
		return false;
	}
	if (p_a.use_static_vbo_loop || p_a.use_mpi) {
		return (
			(p_a.state_hash == p_b.state_hash) &&
			(p_a.mesh_id == p_b.mesh_id) &&
			(p_a.surface_index == p_b.surface_index)
		);
	}
	return true;
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
void RasterizerSceneBatcherCommon<T_API>::join_sorted_items(bool p_transparent) {
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

		const int saved_joined_count = bdata.items_joined.size();
		const int saved_refs_count = bdata.item_refs.size();
		const BItemJoined3D saved_curr_joined_state = curr_joined ? *curr_joined : BItemJoined3D();
		const bool had_curr_joined = (curr_joined != nullptr);
		bool alloc_failed = false;

		uint32_t base_verts = 0;
		uint32_t base_indices = 0;

		// Determine capacity beforehand
		get_this()->_batch_get_instance_geometry_capacity(surface, base_verts, base_indices);

		uint32_t instances = surface->owner->instance_count >= 0 ? surface->owner->instance_count : 1;
		if (instances == 0 || base_verts == 0) {
			continue;
		}

		// Get the surface flags and base modulate
		uint32_t surf_flags = get_this()->_batch_get_surface_flags(surface);
		BatchColor surf_modulate = get_this()->_batch_get_surface_modulate(surface);
		const bool is_multimesh = (surf_flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;
		const bool item_uses_static_loop = bdata.sort_items[i].use_static_vbo_loop;
		const bool item_uses_mpi = bdata.sort_items[i].use_mpi;
		const uint32_t max_safe_verts = MIN((uint32_t)bdata.unit_vertices.max_size(), 65535U);
		const uint32_t max_ibo_indices = (uint32_t)bdata.indices.max_size();
		
		// Detect stitches
		const bool is_strip = (
			surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP ||
			surface->primitive == RS::PRIMITIVE_LINE_STRIP
		);

		// Pre-evaluate owner transform mode and cofactor once per item
		const Transform3D owner_xform = surface->owner->transform;
		float inv_scale = 1.0f;
		const TransformMode3D owner_xform_mode = _find_transform_mode_3d(owner_xform, &inv_scale);
		Basis owner_cofactor;
		if (owner_xform_mode == TM_3D_UNIFORM_SCALE) {
			owner_cofactor = owner_xform.basis * inv_scale;
		} else if (owner_xform_mode == TM_3D_ALL) {
			owner_cofactor = _compute_normal_cofactor(owner_xform.basis);
		}

		uint64_t mm_buffer_version = 0;
		if (is_multimesh && surface->owner->data->base_type == RS::INSTANCE_MULTIMESH) {
			typename T_API::MultiMesh *mm = T_API::MeshStorage::get_singleton()->get_multimesh(surface->owner->data->base);
			if (mm) {
				mm_buffer_version = mm->buffer_version;
			}
		}

		// Calculate max instances per dynamic VBO chunk when expanding MultiMesh into BRI
		uint32_t max_instances_per_chunk = instances;
		if (!item_uses_static_loop && !item_uses_mpi && is_multimesh) {
			const uint32_t max_by_verts = MAX(1U, max_safe_verts / base_verts);
			const uint32_t eff_idx_per_inst = base_indices + (is_strip ? 2U : 0U);
			const uint32_t max_by_inds = eff_idx_per_inst > 0 ? MAX(1U, max_ibo_indices / eff_idx_per_inst) : instances;
			max_instances_per_chunk = MIN(instances, MIN(max_by_verts, max_by_inds));
		}

		uint32_t inst_offset = 0;
		while (inst_offset < instances) {
			const uint32_t chunk_instances = (item_uses_static_loop || item_uses_mpi) ? (instances - inst_offset) : MIN(instances - inst_offset, max_instances_per_chunk);
			const uint32_t internal_stitch_indices = (is_strip && chunk_instances > 1) ? ((chunk_instances - 1) * 2) : 0;
			const uint32_t join_stitch_indices = (curr_joined && is_strip) ? 2 : 0;

			const uint32_t incoming_verts = base_verts * chunk_instances;
			const uint32_t incoming_indices = (base_indices * chunk_instances) + internal_stitch_indices;
			const uint32_t incoming_uniform_vectors = (!is_multimesh) ? (chunk_instances * 3) : 3;

			bool capacity_full = false;
			if (curr_joined && !curr_joined->use_static_vbo_loop && !item_uses_static_loop && !curr_joined->use_mpi && !item_uses_mpi) {
				// A joined group must break if appending the new item would exceed:
				// - The maximum allocated vertex buffer size (capped at 65535 for uint16_t indices).
				// - The maximum allocated index buffer size.
				// - The maximum number of uniforms available on the hardware for matrix palettes.
				if ((curr_joined->total_verts + incoming_verts > max_safe_verts) ||
					(curr_joined->total_indices + incoming_indices + join_stitch_indices > max_ibo_indices) ||
					(_render_item_state.consumed_uniform_vectors + incoming_uniform_vectors > bdata.hardware_limits.max_matrix_palette_vectors)) {
					capacity_full = true;
				}
			}

			bool can_join = false;
			if (curr_joined && !capacity_full && (curr_joined->use_static_vbo_loop == item_uses_static_loop) && (curr_joined->use_mpi == item_uses_mpi)) {
				uint64_t masked_item_hash = bdata.sort_items[i].state_hash & MASK_ID;
				uint64_t masked_curr_hash = curr_joined->state_hash & MASK_ID;
				bool curr_is_multimesh = (curr_joined->flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;

				if (item_uses_static_loop || item_uses_mpi) {
					// SMSG and MPI require identical mesh_id, surface_index, and state_hash.
					// MPI unifies multimeshes and MeshInstance3D runs sharing the same mesh surface.
					const bool mm_match = item_uses_mpi ? true : (is_multimesh == curr_is_multimesh);
					if (
						mm_match &&
						surface->primitive == _render_item_state.current_primitive &&
						bdata.sort_items[i].state_hash == curr_joined->state_hash &&
						bdata.sort_items[i].mesh_id == curr_joined->mesh_id &&
						bdata.sort_items[i].surface_index == curr_joined->surface_index
					) {
						can_join = true;
					}
				} else {
					// Triangle strips with an odd index count flip winding parity on subsequent strips
					// when stitched with 2 degenerate indices, corrupting front/back face culling.
					bool strip_winding_mismatch = (
						surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP &&
						(curr_joined->total_indices & 1) != 0
					);

					// (e.g., MODEL_MATRIX, NODE_POSITION_WORLD, VERTEX manipulation) and MultiMesh
					// chunks (which rely on hardware owner transforms for local-space VBO caching)
					// must not join across distinct items.
					bool prevent_join = (
						is_multimesh ||
						curr_is_multimesh ||
						((surf_flags | curr_joined->flags) &
						 (BatcherEnums::BTF_3D_PREVENT_ITEM_JOINING | BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING)) != 0
					);

					if (
						!strip_winding_mismatch &&
						!prevent_join &&
						surface->primitive == _render_item_state.current_primitive &&
						masked_item_hash == masked_curr_hash
					) {
						can_join = true;
					}
				}
			}

			if (!can_join) {
				// Start a new joined item group
				curr_joined = bdata.items_joined.request_with_grow();
				if (unlikely(!curr_joined)) {
					alloc_failed = true;
					break;
				}
				curr_joined->first_item_ref = bdata.item_refs.size();
				curr_joined->num_item_refs = 0;
				curr_joined->bounding_aabb = bdata.sort_items[i].aabb;
				curr_joined->flags = surf_flags;
				curr_joined->state_hash = bdata.sort_items[i].state_hash;
				curr_joined->mesh_id = bdata.sort_items[i].mesh_id;
				curr_joined->surface_index = bdata.sort_items[i].surface_index;
				curr_joined->use_static_vbo_loop = item_uses_static_loop;
				curr_joined->use_mpi = item_uses_mpi;
				curr_joined->batch_content_hash = (item_uses_static_loop || item_uses_mpi) ? 0 : _fnv1a_64_mix(14695981039346656037ULL, static_cast<uint64_t>(p_pass_mode));
				if (curr_joined->batch_content_hash != 0) {
					curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, curr_joined->state_hash);
				}
				curr_joined->total_verts = 0;
				curr_joined->total_indices = 0;
				curr_joined->total_color_changes = 1;

				_render_item_state.current_state_hash = bdata.sort_items[i].state_hash;
				_render_item_state.current_material_data = get_this()->_batch_get_material_data(surface);
				_render_item_state.current_primitive = surface->primitive;
				_render_item_state.current_mesh_id = bdata.sort_items[i].mesh_id;
				_render_item_state.current_surface_index = bdata.sort_items[i].surface_index;
				_render_item_state.consumed_uniform_vectors = 0;
				_render_item_state.last_modulate = surf_modulate;
			} else {
				// Extend existing joined item group and accumulate required FVF flags
				curr_joined->bounding_aabb.merge_with(bdata.sort_items[i].aabb);
				curr_joined->flags |= surf_flags;
				if (!curr_joined->use_static_vbo_loop) {
					curr_joined->total_indices += join_stitch_indices;
				}
				if (surf_modulate != _render_item_state.last_modulate) {
					curr_joined->total_color_changes++;
					_render_item_state.last_modulate = surf_modulate;
				}
			}

			BItemRef3D *ref = bdata.item_refs.request_with_grow();
			if (unlikely(!ref)) {
				alloc_failed = true;
				break;
			}

			// Pre-evaluate transform mode and cofactor matrix once per reference
			ref->surface = (void *)surface;
			ref->transform = owner_xform;
			ref->transform_mode = owner_xform_mode;
			ref->normal_cofactor = owner_cofactor;
			ref->modulate = surf_modulate;
			ref->base_verts = base_verts;
			ref->base_indices = base_indices;
			ref->instance_offset = inst_offset;
			ref->instance_count = chunk_instances;

			curr_joined->num_item_refs++;

			// Rolling 64-bit content hash for persistent VBO sub-allocation matching across frames
			if (p_pass_mode == BatcherEnums::PASS_MODE_MATERIAL || (is_multimesh && mm_buffer_version == 0)) {
				// Disable cross-frame VBO caching for dynamic MultiMesh/Particles/UV2 passes
				curr_joined->batch_content_hash = 0;
			} else if (curr_joined->batch_content_hash != 0) {
				if (curr_joined->num_item_refs == 2) {
					// Transitioned from single-ref (hardware transform) to multi-ref (CPU baked transform).
					// Retroactively mix the first item's transform hash into the rolling hash.
					const BItemRef3D &first_ref = bdata.item_refs[curr_joined->first_item_ref];
					curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, _hash_transform_3d(first_ref.transform));
				}

				const uint32_t xform_hash = (curr_joined->num_item_refs == 1) ? 0U : _hash_transform_3d(ref->transform);
				const uint32_t mod_packed = (
					static_cast<uint32_t>(surf_modulate.r) |
					(static_cast<uint32_t>(surf_modulate.g) << 8) |
					(static_cast<uint32_t>(surf_modulate.b) << 16) |
					(static_cast<uint32_t>(surf_modulate.a) << 24)
				);
				// For MultiMesh chunks using hardware owner transforms, hash the shared MultiMesh RID.
				const uint64_t instance_identity = is_multimesh
					? surface->owner->data->base.get_id()
					: static_cast<uint64_t>(reinterpret_cast<uintptr_t>(surface));
				curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, instance_identity);
				curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, reinterpret_cast<uintptr_t>(surface->surface));
				curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, (static_cast<uint64_t>(xform_hash) << 32) | mod_packed);
				curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, (static_cast<uint64_t>(base_verts) << 32) | base_indices);
				if (is_multimesh) {
					curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, mm_buffer_version);
					if (surf_flags & BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING) {
						// Under Tier 4 palette instancing, vertex/index data depends on the chunk instances.
						curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, chunk_instances);
					} else {
						curr_joined->batch_content_hash = _fnv1a_64_mix(curr_joined->batch_content_hash, (static_cast<uint64_t>(inst_offset) << 32) | chunk_instances);
					}
				}
				if (unlikely(curr_joined->batch_content_hash == 0)) {
					curr_joined->batch_content_hash = 1ULL;
				}
			}
			curr_joined->total_verts += incoming_verts;
			curr_joined->total_indices += incoming_indices;
			_render_item_state.consumed_uniform_vectors += incoming_uniform_vectors;

			inst_offset += chunk_instances;
		}

		if (unlikely(alloc_failed)) {
			ERR_PRINT_ONCE("GLES Scene Batcher: Stage 1 pool exhausted. Rolling back partial group and demoting remaining items.");
			bdata.items_joined.truncate(saved_joined_count);
			bdata.item_refs.truncate(saved_refs_count);
			if (had_curr_joined && saved_joined_count > 0) {
				bdata.items_joined[saved_joined_count - 1] = saved_curr_joined_state;
			}
			for (uint32_t rem = i; rem < (uint32_t)bdata.sort_items.size(); rem++) {
				T_SURFACE *rem_surface = static_cast<T_SURFACE *>(bdata.sort_items[rem].item);
				if (rem_surface) {
					_bypass_surface(rem_surface, bdata.sort_items[rem].depth, p_transparent);
				}
			}
			break;
		}
	}
}

template <typename T_API>
template <typename BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::join_and_flush_sorted_items(bool p_transparent) {
	// Stage 1: Build lightweight BItemJoined3D and BItemRef3D lists
	join_sorted_items<p_pass_mode, T_SURFACE>(p_transparent);

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
		if (unlikely(!first_surface)) {
			continue;
		}
		typename T_API::MaterialData *material = get_this()->_batch_get_material_data(first_surface);
		RS::PrimitiveType primitive = first_surface->primitive;

		Batch3D *batch = _batch_request_new();
		if (unlikely(!batch)) {
			ERR_PRINT_ONCE("GLES Scene Batcher: Batch3D pool exhausted. Demoting remaining joined groups to immediate lane.");
			for (uint32_t rem = j; rem < (uint32_t)bdata.items_joined.size(); rem++) {
				const BItemJoined3D &rem_joined = bdata.items_joined[rem];
				_demote_item_refs_to_bypass<false, T_SURFACE>(rem_joined.first_item_ref, rem_joined.num_item_refs, p_transparent);
			}
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
		batch->mpi_palette_capacity = 0;
		batch->bounding_aabb = joined.bounding_aabb;
		uint32_t effective_flags = joined.flags;
		bdata.total_color_changes = joined.total_color_changes;

		if (
			!(effective_flags & BatcherEnums::BTF_3D_USES_COLORS) &&
			!(effective_flags & BatcherEnums::BTF_3D_PREVENT_COLOR_BAKING) &&
			joined.num_item_refs > 0
		) {
			if (T_API::FORCE_BAKE_MODULATE && joined.total_color_changes > 1) {
				// When FORCE_BAKE_MODULATE merges distinct material albedos into a single batch,
				// promote to Tier 2 unconditionally so per-item colors are preserved in the VBO.
				effective_flags |= BatcherEnums::BTF_3D_USES_COLORS;
				bdata.use_colored_vertices = true;
			} else {
				float ratio = static_cast<float>(joined.total_color_changes > 0 ? (joined.total_color_changes - 1) : 0) / static_cast<float>(joined.num_item_refs);
				if (ratio >= bdata.settings_colored_vertex_format_threshold) {
					effective_flags |= BatcherEnums::BTF_3D_USES_COLORS;
					bdata.use_colored_vertices = true;
				}
			}
		}

		batch->flags = effective_flags;

		_render_item_state.curr_batch = batch;
		_render_item_state.consumed_uniform_vectors = 0;

		const bool use_hardware_transform = joined.use_hardware_transform();

		if (joined.use_mpi) {
			const bool is_strip = (
				primitive == RS::PRIMITIVE_TRIANGLE_STRIP ||
				primitive == RS::PRIMITIVE_LINE_STRIP
			);
			const uint32_t palette_cap = _compute_mpi_palette_capacity(first_ref.base_verts, first_ref.base_indices, is_strip);
			const bool is_isolated_single = (joined.num_item_refs == 1 && first_ref.instance_count == 1 && !(effective_flags & BatcherEnums::BTF_3D_USES_INSTANCING));

			if (is_isolated_single && bdata.settings_use_single_mesh_fallback && get_this()->_batch_can_use_single_mesh_fallback(first_surface)) {
				// Isolated single dynamic mesh avoids VBO replication.
				batch->fvf = BatcherEnums::FVF_UNBATCHED;
				batch->num_verts = first_ref.base_verts;
				batch->num_indices = first_ref.base_indices;
				batch->vbo_index = 0xFFFFFFFF;
				flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
				continue;
			} else if (palette_cap >= 2) {
				// Homogeneous local-space K-instance template VBO
				// with transform-invariant caching.
				BatcherEnums::FVF mpi_fvf = BatcherEnums::FVF_3D_PALETTE_INSTANCED;

				if constexpr (p_pass_mode == BatcherEnums::PASS_MODE_SHADOW || p_pass_mode == BatcherEnums::PASS_MODE_DEPTH) {
					if (effective_flags & BatcherEnums::BTF_3D_USES_ALPHA_CLIP) {
						mpi_fvf = BatcherEnums::FVF_3D_DEPTH_ALPHA_MINIMAL;
					} else {
						mpi_fvf = BatcherEnums::FVF_3D_DEPTH_MINIMAL;
					}
				}

				batch->fvf = mpi_fvf;
				batch->mpi_palette_capacity = palette_cap;
				bdata.fvf = mpi_fvf;
				_prepare_unit_vertices_for_fvf(mpi_fvf);

				// Transform-invariant hash keyed strictly by
				// (pass_mode, mesh surface, surface_index, K, fvf, flags).
				uint64_t mpi_hash = _fnv1a_64_mix(14695981039346656037ULL, 0x4D504935ULL); // "MPI5" domain tag
				mpi_hash = _fnv1a_64_mix(mpi_hash, static_cast<uint64_t>(p_pass_mode));
				mpi_hash = _fnv1a_64_mix(mpi_hash, reinterpret_cast<uintptr_t>(first_surface->surface));
				mpi_hash = _fnv1a_64_mix(mpi_hash, (static_cast<uint64_t>(first_surface->surface_index) << 32) | palette_cap);
				mpi_hash = _fnv1a_64_mix(mpi_hash, (static_cast<uint64_t>(mpi_fvf) << 32) | effective_flags);
				if (unlikely(mpi_hash == 0)) {
					mpi_hash = 1ULL;
				}

				if (get_this()->_batch_try_reuse_cached_vbo_slot_3d(*batch, mpi_hash)) {
					batch->mpi_palette_capacity = palette_cap;
					if (unlikely(!get_this()->_batch_render_items(material, primitive, *batch, p_transparent))) {
						batch->mpi_palette_capacity = 0;
						if (bdata.settings_use_single_mesh_fallback && get_this()->_batch_can_use_single_mesh_fallback(first_surface)) {
							batch->fvf = BatcherEnums::FVF_SAME_MESH_LOOP;
							batch->num_verts = first_ref.base_verts;
							batch->num_indices = first_ref.base_indices;
							batch->vbo_index = 0xFFFFFFFF;
							flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
						} else {
							batch->num_verts = 0;
							batch->num_indices = 0;
							_demote_item_refs_to_bypass<false, T_SURFACE>(joined.first_item_ref, joined.num_item_refs, p_transparent);
						}
					} else {
						_calculate_draw_call_cost_or_flush(*batch, true);
					}
					bdata.reset_flush();
					_render_item_state.consumed_uniform_vectors = 0;
					continue;
				}

				const uint32_t internal_stitches = (is_strip && palette_cap > 1) ? ((palette_cap - 1) * 2) : 0;
				const uint32_t template_verts = first_ref.base_verts * palette_cap;
				const uint32_t template_indices = (first_ref.base_indices * palette_cap) + internal_stitches;

				const bool fits_staging = (
					template_verts <= (uint32_t)bdata.unit_vertices.max_size() &&
					template_indices <= (uint32_t)bdata.indices.max_size()
				);
				uint8_t *dst_bvs = fits_staging ? bdata.unit_vertices.request(template_verts) : nullptr;
				uint16_t *dst_inds = (dst_bvs != nullptr) ? bdata.indices.request(template_indices) : nullptr;
				if (dst_bvs && dst_inds) {
					if constexpr (p_pass_mode == BatcherEnums::PASS_MODE_SHADOW || p_pass_mode == BatcherEnums::PASS_MODE_DEPTH) {
						get_this()->_batch_fill_mpi_depth_template(first_ref, palette_cap, dst_bvs, dst_inds);
					} else {
						get_this()->_batch_fill_mpi_template(first_ref, palette_cap, dst_bvs, dst_inds);
					}
					bdata.total_verts = template_verts;
					bdata.total_indices = template_indices;
					batch->num_verts = template_verts;
					batch->num_indices = template_indices;
					flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent, mpi_hash);
				} else {
					bdata.reset_flush();
					batch->mpi_palette_capacity = 0;
					if (bdata.settings_use_single_mesh_fallback && get_this()->_batch_can_use_single_mesh_fallback(first_surface)) {
						batch->fvf = BatcherEnums::FVF_SAME_MESH_LOOP;
						batch->num_verts = first_ref.base_verts;
						batch->num_indices = first_ref.base_indices;
						batch->vbo_index = 0xFFFFFFFF;
						flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
					} else {
						batch->num_verts = 0;
						batch->num_indices = 0;
						_demote_item_refs_to_bypass<false, T_SURFACE>(joined.first_item_ref, joined.num_item_refs, p_transparent);
					}
				}
				continue;
			} else if (bdata.settings_use_single_mesh_fallback && get_this()->_batch_can_use_single_mesh_fallback(first_surface)) {
				// Fallback when matrix palette hardware is unavailable: demote to SMSG.
				batch->fvf = BatcherEnums::FVF_SAME_MESH_LOOP;
				batch->num_verts = first_ref.base_verts;
				batch->num_indices = first_ref.base_indices;
				batch->vbo_index = 0xFFFFFFFF;
				flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
				continue;
			}
		}

		if (
			joined.use_static_vbo_loop &&
			bdata.settings_use_single_mesh_fallback &&
			get_this()->_batch_can_use_single_mesh_fallback(first_surface)
		) {
			// Single non-instanced static mesh uses FVF_UNBATCHED;
			// multi-item static mesh runs and all MultiMesh items use FVF_SAME_MESH_LOOP.
			if (joined.num_item_refs == 1 && !(effective_flags & BatcherEnums::BTF_3D_USES_INSTANCING)) {
				batch->fvf = BatcherEnums::FVF_UNBATCHED;
				batch->num_verts = joined.total_verts;
				batch->num_indices = joined.total_indices;
			} else {
				batch->fvf = BatcherEnums::FVF_SAME_MESH_LOOP;
				batch->num_verts = first_ref.base_verts;
				batch->num_indices = first_ref.base_indices;
			}
			batch->vbo_index = 0xFFFFFFFF;
			flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
			continue;
		}

		// If a joined group contains only a single non-instanced mesh surface (and the driver
		// does not require CPU packing for UV2/material baking), skip copying vertices into
		// unit_vertices and skip dynamic VBO upload. Tag the batch as FVF_UNBATCHED so both
		// the current flush and any subsequent replay passes bind the mesh's native static VBO.
		if (
			bdata.settings_use_single_mesh_fallback &&
			use_hardware_transform &&
			!(effective_flags & BatcherEnums::BTF_3D_USES_INSTANCING) &&
			get_this()->_batch_can_use_single_mesh_fallback(first_surface)
		) {
			batch->fvf = BatcherEnums::FVF_UNBATCHED;
			batch->num_verts = joined.total_verts;
			batch->num_indices = joined.total_indices;
			batch->vbo_index = 0xFFFFFFFF;
			flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent);
			continue;
		}

		uint64_t final_content_hash = joined.batch_content_hash;
		if (final_content_hash != 0) {
			final_content_hash = _fnv1a_64_mix(
				final_content_hash,
				(static_cast<uint64_t>(effective_flags) << 32) | static_cast<uint64_t>(joined.num_item_refs)
			);
			if (unlikely(final_content_hash == 0)) {
				final_content_hash = 1ULL;
			}
		}

		// Check if the current VBO pool slot already holds identical geometry from frame N-1
		if (
			final_content_hash != 0 &&
			get_this()->_batch_try_reuse_cached_vbo_slot_3d(*batch, final_content_hash)
		) {
			bdata.fvf = batch->fvf;
			_prepare_unit_vertices_for_fvf(batch->fvf);
			if (unlikely(!get_this()->_batch_render_items(material, primitive, *batch, p_transparent))) {
				batch->num_verts = 0;
				batch->num_indices = 0;
				_demote_item_refs_to_bypass<false, T_SURFACE>(joined.first_item_ref, joined.num_item_refs, p_transparent);
			} else {
				_calculate_draw_call_cost_or_flush(*batch, true);
			}
			bdata.reset_flush();
			_render_item_state.consumed_uniform_vectors = 0;
			continue;
		}

		bool prefill_ok = true;
		for (uint32_t i = 0; i < joined.num_item_refs; i++) {
			const BItemRef3D &ref = bdata.item_refs[joined.first_item_ref + i];
			const bool prefill_suceeded = prefill_joined_item<p_pass_mode, T_SURFACE>(ref, use_hardware_transform, i, effective_flags);

			if (unlikely(!prefill_suceeded)) {
				prefill_ok = false;
				break;
			}
		}

		if (unlikely(!prefill_ok)) {
			bdata.reset_flush();
			_render_item_state.consumed_uniform_vectors = 0;
			batch->num_verts = 0;
			batch->num_indices = 0;
			_demote_item_refs_to_bypass<false, T_SURFACE>(joined.first_item_ref, joined.num_item_refs, p_transparent);
			continue;
		}

		// Flush the batch and record its content hash in the VBO slot
		flush_render_batches<T_SURFACE>(material, primitive, *batch, p_transparent, final_content_hash);
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

	// Triangle strips with an odd number of indices flip winding order on the next strip
	// unless an extra index is inserted; break batches if the running strip has odd parity.
	if (
		p_surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP &&
		r_ris.curr_batch &&
		(r_ris.curr_batch->num_indices & 1) != 0
	) {
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

	// Mask out bits 16-31
	uint64_t masked_item_hash = item_hash & MASK_ID;
	uint64_t masked_current_hash = r_ris.current_state_hash & MASK_ID;

	if (masked_item_hash != masked_current_hash) {
		return false;
	}

	// Record if FVF colour changed
	BatchColor surf_modulate = get_this()->_batch_get_surface_modulate(p_surface);
	if (surf_modulate != r_ris.last_modulate) {
		bdata.total_color_changes++;
		r_ris.last_modulate = surf_modulate;
	}

	return true;
}

template <typename T_API>
template <typename BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
bool RasterizerSceneBatcherCommon<T_API>::prefill_joined_item(const BItemRef3D &p_ref, bool p_use_hardware_transform, uint32_t p_item_index, uint32_t p_joined_flags) {
	T_SURFACE *surface = static_cast<T_SURFACE *>(p_ref.surface);
	if (!surface || p_ref.base_verts == 0) {
		return true;
	}

	const bool is_multimesh = (p_joined_flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;
	const bool is_strip = (
		surface->primitive == RS::PRIMITIVE_TRIANGLE_STRIP ||
		surface->primitive == RS::PRIMITIVE_LINE_STRIP
	);
	const uint32_t stitch_count = (is_strip && bdata.total_indices > 0) ? 2 : 0;
	const uint32_t internal_stitch_count = (is_strip && p_ref.instance_count > 1) ? ((p_ref.instance_count - 1) * 2) : 0;

	const uint32_t incoming_verts = p_ref.base_verts * p_ref.instance_count;
	const uint32_t incoming_indices = (p_ref.base_indices * p_ref.instance_count) + internal_stitch_count;
	const uint32_t total_requested_indices = incoming_indices + stitch_count;
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
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier4));
			} else if (p_joined_flags & BatcherEnums::BTF_3D_USES_TANGENTS) {
				bdata.fvf = BatcherEnums::FVF_3D_TIER3_TANGENT;
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier3));
			} else if (p_joined_flags & (BatcherEnums::BTF_3D_USES_COLORS | BatcherEnums::BTF_3D_USES_UV2)) {
				bdata.fvf = BatcherEnums::FVF_3D_TIER2_COLOR;
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier2));
			} else {
				bdata.fvf = BatcherEnums::FVF_3D_TIER1_BASE;
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier1));
			}
		}
		_render_item_state.curr_batch->fvf = bdata.fvf;
	}

	const uint32_t start_vert = bdata.total_verts;
	if (
		(start_vert + incoming_verts) > 65536U ||
		(bdata.unit_vertices.size() + static_cast<int>(incoming_verts)) > bdata.unit_vertices.max_size() ||
		(bdata.indices.size() + static_cast<int>(total_requested_indices)) > bdata.indices.max_size()
	) {
		return false;
	}

	uint16_t prev_last_index = (stitch_count > 0) ? bdata.indices[bdata.total_indices - 1] : 0;
	uint8_t *dst_bvs = bdata.unit_vertices.request(incoming_verts);
	if (unlikely(!dst_bvs)) {
		return false;
	}

	uint16_t *raw_inds = total_requested_indices > 0 ? bdata.indices.request(total_requested_indices) : nullptr;
	if (unlikely(total_requested_indices > 0 && !raw_inds)) {
		return false;
	}
	uint16_t *inds = raw_inds ? (raw_inds + stitch_count) : nullptr;

	if constexpr (p_pass_mode == BatcherEnums::PASS_MODE_SHADOW || p_pass_mode == BatcherEnums::PASS_MODE_DEPTH) {
		// Only perform the batch fill for shadow or
		// depth pass modes
		if (is_multimesh) {
			get_this()->_batch_fill_multimesh_depth(p_ref, dst_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		} else {
			get_this()->_batch_fill_instance_depth(p_ref, dst_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		}
	} else {
		// Regular batch fill
		// Write directly into unit_vertices at the target FVF stride
		if (is_multimesh) {
			get_this()->_batch_fill_multimesh_tiered(p_ref, dst_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		} else {
			get_this()->_batch_fill_instance_tiered(p_ref, dst_bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		}
	}

	// Sync stitches counts
	if (stitch_count > 0 && raw_inds && inds) {
		raw_inds[0] = prev_last_index;
		raw_inds[1] = inds[0];
	}

	bdata.total_verts += incoming_verts;
	bdata.total_indices += total_requested_indices;
	_render_item_state.curr_batch->num_verts += incoming_verts;
	_render_item_state.curr_batch->num_indices += total_requested_indices;
	_render_item_state.consumed_uniform_vectors += incoming_uniform_vectors;
	return true;
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::flush_render_batches(typename T_API::MaterialData *p_material_data, RS::PrimitiveType p_primitive, Batch3D &p_batch, bool p_transparent, uint64_t p_content_hash) {
	if (p_batch.num_verts == 0 || p_primitive == RS::PRIMITIVE_MAX) {
		bdata.reset_flush();
		return;
	}

	// Intercept batches flagged for the single-mesh direct draw fallback or static mesh loop lane.
	if (p_batch.fvf == BatcherEnums::FVF_UNBATCHED || p_batch.fvf == BatcherEnums::FVF_SAME_MESH_LOOP) {
		bdata.fvf = p_batch.fvf;
		if (unlikely(!get_this()->_batch_render_items(p_material_data, p_primitive, p_batch, p_transparent))) {
			p_batch.num_verts = 0;
			p_batch.num_indices = 0;
			_demote_item_refs_to_bypass<false, T_SURFACE>(p_batch.first_item_ref, p_batch.num_item_refs, p_transparent);
		} else {
			_calculate_draw_call_cost_or_flush(p_batch);
		}

		bdata.reset_flush();
		_render_item_state.consumed_uniform_vectors = 0;
		return;
	}

	// Upload batched VBOs
	if (unlikely(!get_this()->_batch_upload_buffers_3d(p_batch, p_content_hash))) {
		bdata.reset_flush();
		_render_item_state.consumed_uniform_vectors = 0;

		if (p_batch.mpi_palette_capacity >= 2 && p_batch.first_item_ref < (uint32_t)bdata.item_refs.size()) {
			const BItemRef3D &first_ref = bdata.item_refs[p_batch.first_item_ref];
			T_SURFACE *first_surface = static_cast<T_SURFACE *>(first_ref.surface);
			p_batch.mpi_palette_capacity = 0;
			if (first_surface && bdata.settings_use_single_mesh_fallback && get_this()->_batch_can_use_single_mesh_fallback(first_surface)) {
				p_batch.fvf = BatcherEnums::FVF_SAME_MESH_LOOP;
				p_batch.num_verts = first_ref.base_verts;
				p_batch.num_indices = first_ref.base_indices;
				p_batch.vbo_index = 0xFFFFFFFF;
				bdata.fvf = p_batch.fvf;
				if (likely(get_this()->_batch_render_items(p_material_data, p_primitive, p_batch, p_transparent))) {
					return;
				}
			}
		}

		p_batch.num_verts = 0;
		p_batch.num_indices = 0;
		_demote_item_refs_to_bypass<false, T_SURFACE>(p_batch.first_item_ref, p_batch.num_item_refs, p_transparent);
		return;
	}

	// Delegate the API-specific state binding and multi-draw dispatching to the driver
	if (unlikely(!get_this()->_batch_render_items(p_material_data, p_primitive, p_batch, p_transparent))) {
		p_batch.num_verts = 0;
		p_batch.num_indices = 0;
		p_batch.mpi_palette_capacity = 0;
		_demote_item_refs_to_bypass<false, T_SURFACE>(p_batch.first_item_ref, p_batch.num_item_refs, p_transparent);
	} else {
		_calculate_draw_call_cost_or_flush(p_batch);
	}

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
			get_this()->_render_single_item_immediate(static_cast<T_SURFACE *>(bdata.bypassed_opaque_items[i]));
		}
	} else {
		SortArray<BSortItem3D, SortTransparent> sorter{};
		sorter.sort(bdata.bypassed_transparent_items.ptr(), bdata.bypassed_transparent_items.size());

		for (uint32_t i = 0; i < (uint32_t)bdata.bypassed_transparent_items.size(); i++) {
			get_this()->_render_single_item_immediate(static_cast<T_SURFACE *>(bdata.bypassed_transparent_items[i].item));
		}
	}
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::_bypass_surface(T_SURFACE *p_surface, float p_depth, bool p_transparent) {
	if (!p_surface) {
		return;
	}

	if (!p_transparent) {
		bdata.bypassed_opaque_items.push_back((void *)p_surface);
	} else {
		BSortItem3D bypassed_trans{};
		bypassed_trans.item = (void *)p_surface;
		bypassed_trans.depth = p_depth;
		bdata.bypassed_transparent_items.push_back(bypassed_trans);
	}
}

template <typename T_API>
template <bool p_render_now, class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::_demote_item_refs_to_bypass(uint32_t p_first_ref, uint32_t p_num_refs, bool p_transparent) {
	if (p_first_ref >= (uint32_t)bdata.item_refs.size()) {
		return;
	}
	const uint32_t end_ref = MIN(p_first_ref + p_num_refs, (uint32_t)bdata.item_refs.size());
	T_SURFACE *last_surface = nullptr;

	for (uint32_t r = p_first_ref; r < end_ref; r++) {
		T_SURFACE *surf = static_cast<T_SURFACE *>(bdata.item_refs[r].surface);
		// MultiMesh chunks share the same surface pointer across contiguous item_refs;
		// RSII draws all instances of the surface, so deduplicate per surface.
		if (!surf || surf == last_surface) {
			continue;
		}
		last_surface = surf;

		if constexpr (p_render_now) {
			get_this()->_render_single_item_immediate(surf);
		} else {
			const float depth = (p_transparent && surf->owner) ? surf->owner->depth : 0.0f;
			_bypass_surface(surf, depth, p_transparent);
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
void RasterizerSceneBatcherCommon<T_API>::_software_transform_normal(BatchNormal &r_n, const Transform3D &p_tr) const {
	const Basis cofactor = _compute_normal_cofactor(p_tr.basis);
	_software_transform_normal_precomputed(r_n, cofactor);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::_software_transform_normal_precomputed(BatchNormal &r_n, const Basis &p_cofactor) const {
	Vector3 nc;
	r_n.to(nc);
	nc = p_cofactor.xform(nc).normalized();
	r_n.set(nc);
}

template <typename T_API>
uint32_t RasterizerSceneBatcherCommon<T_API>::_get_effective_dynamic_vertex_limit() const {
	const bool is_depth_pass = (
		bdata.pass_mode == BatcherEnums::PASS_MODE_SHADOW ||
		bdata.pass_mode == BatcherEnums::PASS_MODE_DEPTH
	);
	const uint32_t limit = is_depth_pass
		? (bdata.settings_dynamic_vertex_limit * 4U)
		: bdata.settings_dynamic_vertex_limit;
	const uint32_t max_safe_verts = MIN((uint32_t)bdata.unit_vertices.max_size(), 65535U);
	return MIN(limit, max_safe_verts);
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::_should_use_static_vbo_loop(uint32_t p_base_verts, uint32_t p_base_indices, uint32_t p_instances, bool p_is_multimesh, bool p_is_strip, uint32_t p_surf_flags) const {
	const uint32_t effective_limit = _get_effective_dynamic_vertex_limit();
	if (p_base_verts > effective_limit) {
		return true;
	}
	if (!p_is_multimesh) {
		return false;
	}

	if ((p_surf_flags & BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING) != 0) {
		return true;
	}

	const uint64_t max_safe_verts = MIN(static_cast<uint64_t>(bdata.unit_vertices.max_size()), 65535ULL);
	if (
		static_cast<uint64_t>(p_base_verts) > max_safe_verts ||
		static_cast<uint64_t>(p_base_indices) > static_cast<uint64_t>(bdata.indices.max_size())
	) {
		return true;
	}

	// Triangle strips with odd per-instance index counts flip
	// winding parity across stitched instances.
	if (p_is_strip && p_instances > 1 && (p_base_indices & 1U) != 0U) {
		return true;
	}

	return false;
}

template <typename T_API>
uint32_t RasterizerSceneBatcherCommon<T_API>::_compute_mpi_palette_capacity(uint32_t p_base_verts, uint32_t p_base_indices, bool p_is_strip) const {
	const uint32_t hw_cap = get_this()->_batch_get_mpi_palette_capacity();
	if (hw_cap < 2 || p_base_verts == 0) {
		return 0;
	}

	const uint32_t max_safe_verts = MIN(static_cast<uint32_t>(bdata.unit_vertices.max_size()), 65535U);
	const uint32_t max_ibo_indices = static_cast<uint32_t>(bdata.indices.max_size());
	const uint32_t max_by_verts = max_safe_verts / p_base_verts;
	const uint32_t eff_idx_per_inst = p_base_indices + (p_is_strip ? 2U : 0U);
	const uint32_t max_by_inds = eff_idx_per_inst > 0 ? (max_ibo_indices / eff_idx_per_inst) : hw_cap;

	return MIN(hw_cap, MIN(max_by_verts, max_by_inds));
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::_should_use_mpi(uint32_t p_base_verts, uint32_t p_base_indices, uint32_t p_instances, bool p_is_multimesh, bool p_is_strip, bool p_is_dynamic, uint32_t p_surf_flags) const {
	// UV2 lightmap baking requires CPU UV2->position remapping and cannot use this.
	if (bdata.pass_mode == BatcherEnums::PASS_MODE_MATERIAL) {
		return false;
	}
	if (p_base_verts > _get_effective_dynamic_vertex_limit()) {
		return false;
	}
	// Triangle strips with odd per-instance index counts
	// flip winding parity across stitched instances.
	if (p_is_strip && (p_base_indices & 1U) != 0U) {
		return false;
	}
	// MultiMeshes with per-instance colors require per-instance
	// color variation not carried in the matrix palette.
	if (p_is_multimesh && (p_surf_flags & BatcherEnums::BTF_3D_USES_COLORS) != 0) {
		return false;
	}
	if (_compute_mpi_palette_capacity(p_base_verts, p_base_indices, p_is_strip) < 2) {
		return false;
	}

	// Route MultiMesh, PREVENT_VERTEX_BAKING, and dynamic small/medium meshes.
	return (
		p_is_multimesh ||
		p_is_dynamic ||
		((p_surf_flags & BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING) != 0)
	);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::_calculate_draw_call_cost_or_flush(Batch3D &p_batch, bool p_was_cached_vbo) const {
	if (
		p_was_cached_vbo ||
		p_batch.fvf == BatcherEnums::FVF_UNBATCHED ||
		p_batch.fvf == BatcherEnums::FVF_SAME_MESH_LOOP
	) {
		// Cached VBO slots and static mesh draws do not upload dynamic staging data.
		return;
	}

	RasterizerSceneBatcherCommon<T_API> *self = const_cast<RasterizerSceneBatcherCommon<T_API> *>(this);

	// Calculate the cost of this call
	// and verify if we can keep it in the GPU "inventory."
	self->bdata.dynamic_upload_accumulator += (
		p_batch.num_verts * self->bdata.unit_vertices.get_unit_size_bytes()
	) + (p_batch.num_indices * sizeof(uint16_t));
	
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
