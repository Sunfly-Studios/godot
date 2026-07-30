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

	// Driver-specific hardware capabilities
	get_this()->_batch_get_hardware_limits(bdata.hardware_limits);

	bdata.unit_vertices.create(bdata.hardware_limits.max_vertices_per_buffer, 32); 
	bdata.unit_vertices.prepare(32); // Default to a standard 32-byte FVF
	bdata.indices.create(bdata.hardware_limits.max_indices_per_buffer);
	
	bdata.batches.create(256);
	bdata.sort_items.create(2048);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_begin() {
	bdata.reset_scene();
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_end() {
	bdata.reset_scene();
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_render_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	if (!bdata.settings_use_batching || p_count == 0) {
		get_this()->scene_render_items_implementation(p_surfaces, p_count, p_camera_transform, p_transparent);
		return;
	}

	bdata.reset_scene();
	_render_item_state.reset();

	// Separate batchable small props from unbatchable meshes
	record_items<T_SURFACE>(p_surfaces, p_count, p_camera_transform, p_transparent);

	// Only sort the items that survived the filter
	if (bdata.sort_items.size() > 0) {
		sort_items(p_transparent);
		
		// Pack & Render: Build VBOs and fire draw calls
		join_and_flush_sorted_items<T_SURFACE>(p_transparent);
	}

	// Draw the unbatchable meshes directly, bypassing the batcher overhead
	render_bypassed_items<T_SURFACE>(p_transparent);
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::record_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	for (int i = 0; i < p_count; i++) {
		T_SURFACE *surface = p_surfaces[i];
		
		uint32_t base_verts = 0;
		uint32_t base_indices = 0;
		
		// Quickly look up the base vertex count for the mesh complexity threshold.
		get_this()->_batch_get_instance_geometry_capacity(surface, base_verts, base_indices);

		// Much like the 2D batcher, ours is really optimised for
		// triangles.
		bool can_batch = surface->primitive == RS::PRIMITIVE_TRIANGLES;

		// Do not batch if the base mesh passes the complexity limit
		if (base_verts > bdata.settings_dynamic_vertex_limit || !can_batch) {
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

		s->item = (void*)surface;
		
		// Calculate linear depth
		s->depth = get_this()->_batch_get_item_depth(surface, p_camera_transform);
		
		// Generate a state hash.
		s->state_hash = get_this()->_batch_get_state_hash(surface); 
	}
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::sort_items(bool p_transparent) {
	if (p_transparent) {
		SortArray<BSortItem3D, SortTransparent> sorter;
		sorter.sort(&bdata.sort_items[0], bdata.sort_items.size());
	} else {
		SortArray<BSortItem3D, SortOpaque> sorter;
		sorter.sort(&bdata.sort_items[0], bdata.sort_items.size());
	}
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::join_and_flush_sorted_items(bool p_transparent) {
	for (uint32_t i = 0; i < (uint32_t)bdata.sort_items.size(); i++) {
		T_SURFACE *surface = static_cast<T_SURFACE *>(bdata.sort_items[i].item);

		// Check for state breaks (Material change, Light environment change)
		if (!try_join_item<T_SURFACE>(surface, _render_item_state)) {
			// Flush the pipe before changing state
			flush_render_batches<T_SURFACE>(_render_item_state.current_material_data);
			
			// Update the state tracker
			_render_item_state.current_state_hash = bdata.sort_items[i].state_hash;
			_render_item_state.current_material_data = get_this()->_batch_get_material_data(surface);
		}

		// Attempt to pack the geometry. prefill_joined_item returns TRUE if the buffer is full.
		if (prefill_joined_item<T_SURFACE>(surface)) {
			// Buffer full! Flush it.
			flush_render_batches<T_SURFACE>(_render_item_state.current_material_data);
			
			// Retry packing the instance now that the buffer is empty
			prefill_joined_item<T_SURFACE>(surface);
		}
	}

	// Final flush for remaining batched geometry
	flush_render_batches<T_SURFACE>(_render_item_state.current_material_data);
}

template <typename T_API>
template <class T_SURFACE>
bool RasterizerSceneBatcherCommon<T_API>::try_join_item(T_SURFACE *p_surface, RenderItemState3D &r_ris) {
	uint64_t item_hash = get_this()->_batch_get_state_hash(p_surface);
	bool is_multimesh = (
		p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH
	);
	bool current_is_multimesh = bdata.fvf == BatcherEnums::FVF_INSTANCED;
	
	// Topology switch between instanced and regular
	// always break batches
	if (is_multimesh != current_is_multimesh && bdata.fvf != BatcherEnums::FVF_UNBATCHED) {
		return false;
	}

	return (item_hash == r_ris.current_state_hash);
}

template <typename T_API>
template <class T_SURFACE>
bool RasterizerSceneBatcherCommon<T_API>::prefill_joined_item(T_SURFACE *p_surface) {
	uint32_t base_verts = 0;
	uint32_t base_indices = 0;
	
	get_this()->_batch_get_instance_geometry_capacity(p_surface, base_verts, base_indices);

	if (base_verts == 0) {
		return false;
	}

	bool is_multimesh = p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH;
	uint32_t instances = is_multimesh ? p_surface->owner->instance_count : 1;
	uint32_t incoming_verts = base_verts * instances;
	uint32_t incoming_indices = base_indices * instances;

	if (is_multimesh) {
		if (bdata.instanced_vertices.max_size() == 0) {
			// We allocate on demand for performance and to save
			// memory.
			bdata.instanced_vertices.create(bdata.hardware_limits.max_vertices_per_buffer);
		}
		
		if ((bdata.instanced_vertices.size() + incoming_verts > bdata.hardware_limits.max_vertices_per_buffer) ||
			(bdata.indices.size() + incoming_indices > bdata.hardware_limits.max_indices_per_buffer)) {
			return true; // Buffer is full, signal caller to flush
		}

		bdata.fvf = BatcherEnums::FVF_INSTANCED;
		uint32_t start_vert = bdata.instanced_vertices.size();
		BatchVertex3DInstanced *bvs = bdata.instanced_vertices.request(incoming_verts);
		uint16_t *inds = nullptr;

		if (incoming_indices > 0) {
			inds = bdata.indices.request(incoming_indices);
		}

		if (bvs) {
			get_this()->_batch_fill_multimesh_geometry(p_surface, bvs, inds, start_vert);
		}
	} else {
		if ((bdata.vertices.size() + incoming_verts > bdata.hardware_limits.max_vertices_per_buffer) ||
			(bdata.indices.size() + incoming_indices > bdata.hardware_limits.max_indices_per_buffer)) {
			return true; // Buffer is full, signal caller to flush
		}
		
		bdata.fvf = BatcherEnums::FVF_REGULAR;

		uint32_t start_vert = bdata.vertices.size();
		BatchVertex3D *bvs = bdata.vertices.request(incoming_verts);
		uint16_t *inds = incoming_indices > 0 ? bdata.indices.request(incoming_indices) : nullptr;

		if (bvs) {
			// The driver handles the packing.
			// Multi-threading dispatch (GLES1), or CPU transforming (GLES2).
			get_this()->_batch_fill_instance_geometry(p_surface, bvs, inds, start_vert);
		}
	}

	return false;
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::flush_render_batches(typename T_API::MaterialData *p_material_data) {
	if (bdata.vertices.size() == 0 && bdata.instanced_vertices.size() == 0) {
		return;
	}

	// Upload batched VBOs
	get_this()->_batch_upload_buffers();
	
	// Bind states
	get_this()->_batch_bind_material(p_material_data);
	
	// Execute draw calls
	get_this()->_batch_render_generic();

	bdata.reset_flush();
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
		SortArray<BSortItem3D, SortTransparent> sorter;
		sorter.sort(bdata.bypassed_transparent_items.ptr(), bdata.bypassed_transparent_items.size());

		for (uint32_t i = 0; i < (uint32_t)bdata.bypassed_transparent_items.size(); i++) {
			get_this()->_render_single_item_immediate(static_cast<T_SURFACE*>(bdata.bypassed_transparent_items[i].item));
		}
	}
}
