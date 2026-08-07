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

	bdata.unit_vertices.create(bdata.hardware_limits.max_vertices_per_buffer, sizeof(BatchVertex3DInstanced)); 
	bdata.unit_vertices.prepare(sizeof(BatchVertex3D)); // Default FVF
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

		if (base_verts == 0) {
			// This happens when primitive topology differs
			// forcing batch break
			continue;
		}

		// Get total verts counts
		uint32_t instances = surface->owner->instance_count > 0 ? surface->owner->instance_count : 1;
		uint32_t total_incoming_verts = base_verts * instances;

		// Do not batch if the base mesh passes the complexity limit
		if (total_incoming_verts > bdata.settings_dynamic_vertex_limit) {
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
	_render_item_state.reset();
	bdata.batches.reset();
	
	// =========================
	// Grouping pass
	// =========================
	for (uint32_t i = 0; i < (uint32_t)bdata.sort_items.size(); i++) {
		T_SURFACE *surface = static_cast<T_SURFACE *>(bdata.sort_items[i].item);

		if (!try_join_item<T_SURFACE>(surface, _render_item_state) || !_render_item_state.curr_batch) {
			_render_item_state.curr_batch = _batch_request_new();
			if (!_render_item_state.curr_batch) {
				break;
			}

			_render_item_state.curr_batch->first_item_index = i;
			_render_item_state.curr_batch->num_items = 0;
			_render_item_state.curr_batch->first_vert = 0;
			_render_item_state.curr_batch->num_verts = 0;
			_render_item_state.curr_batch->num_indices = 0;

			_render_item_state.current_state_hash = bdata.sort_items[i].state_hash;
			_render_item_state.current_material_data = get_this()->_batch_get_material_data(surface);
			_render_item_state.current_primitive = surface->primitive;
		}

		_render_item_state.curr_batch->num_items++;
	}
	
	// =========================
	// Packing & flushing pass
	// =========================
	for (uint32_t b = 0; b < (uint32_t)bdata.batches.size(); b++) {
		Batch3D &batch = bdata.batches[b];
		T_SURFACE *first_surface = static_cast<T_SURFACE *>(bdata.sort_items[batch.first_item_index].item);
		typename T_API::MaterialData *material = get_this()->_batch_get_material_data(first_surface);
		RS::PrimitiveType primitive = first_surface->primitive;

		bool use_hardware_transform = (batch.num_items == 1);

		_render_item_state.curr_batch = &batch;

		for (uint32_t i = 0; i < batch.num_items; i++) {
			T_SURFACE *surface = static_cast<T_SURFACE *>(bdata.sort_items[batch.first_item_index + i].item);

			if (prefill_joined_item<T_SURFACE>(surface, use_hardware_transform, i)) {
				// Buffer full! Flush what we have so far
				flush_render_batches<T_SURFACE>(material, primitive, batch, p_transparent);

				// Reset batch pointers for the remainder
				batch.first_vert = bdata.total_verts;
				batch.num_verts = 0;
				batch.num_indices = 0;

				// Retry current item
				prefill_joined_item<T_SURFACE>(surface, use_hardware_transform, i);
			}
		}

		// Flush the batch
		flush_render_batches<T_SURFACE>(material, primitive, batch, p_transparent);
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

	return (item_hash == r_ris.current_state_hash);
}

template <typename T_API>
template <class T_SURFACE>
bool RasterizerSceneBatcherCommon<T_API>::prefill_joined_item(T_SURFACE *p_surface, bool p_use_hardware_transform, uint32_t p_item_index) {
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

	// Track matrix palette hardware limits
	// A single Transform3D requires 3 vec4 uniforms.
	uint32_t incoming_uniform_vectors = instances * 3;
	if (_render_item_state.consumed_uniform_vectors + incoming_uniform_vectors > bdata.hardware_limits.max_matrix_palette_vectors) {
		return true; // Buffer is full, signal caller to flush
	}

	// Only prepare when this is the first item in the batch.
	// Subsequent items don't need to override.
	if (bdata.total_verts == 0) {
		if (is_multimesh) {
			bdata.fvf = BatcherEnums::FVF_INSTANCED;
			bdata.unit_vertices.prepare(sizeof(BatchVertex3DInstanced));
		} else {
			bdata.fvf = BatcherEnums::FVF_REGULAR;
			bdata.unit_vertices.prepare(sizeof(BatchVertex3D));
		}
	}

	// Validate VBO bounds against the dynamic unit cursor
	if ((bdata.total_verts + incoming_verts > (uint32_t)bdata.unit_vertices.max_size()) ||
		(bdata.total_indices + incoming_indices > (uint32_t)bdata.indices.max_size())) {
		return true; // Buffer is full, signal caller to flush
	}

	uint32_t start_vert = bdata.total_verts;
	uint8_t *bvs = bdata.unit_vertices.request(incoming_verts);
	uint16_t *inds = incoming_indices > 0 ? bdata.indices.request(incoming_indices) : nullptr;

	if (bvs) {
		if (is_multimesh) {
			get_this()->_batch_fill_multimesh_geometry(p_surface, (BatchVertex3DInstanced *)bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		} else {
			// The driver handles the packing.
			// Multi-threading dispatch (GLES1), or CPU transforming (GLES2).
			get_this()->_batch_fill_instance_geometry(p_surface, (BatchVertex3D *)bvs, inds, start_vert, p_use_hardware_transform, p_item_index);
		}
		
		bdata.total_verts += incoming_verts;
		bdata.total_indices += incoming_indices;
		_render_item_state.curr_batch->num_verts += incoming_verts;
		_render_item_state.curr_batch->num_indices += incoming_indices;
		_render_item_state.consumed_uniform_vectors += incoming_uniform_vectors;
	}

	return false;
}

template <typename T_API>
template <class T_SURFACE>
void RasterizerSceneBatcherCommon<T_API>::flush_render_batches(typename T_API::MaterialData *p_material_data, RS::PrimitiveType p_primitive, Batch3D &p_batch, bool p_transparent) {
	if (p_batch.num_verts == 0 || p_primitive == RS::PRIMITIVE_MAX) {
		return;
	}

	// Upload batched VBOs
	get_this()->_batch_upload_buffers();

	// Delegate the API-specific state binding and multi-draw dispatching to the driver
	get_this()->_batch_render_items(p_material_data, p_primitive, p_batch, p_transparent);

	// Reset VBO cursors
	bdata.unit_vertices.prepare(bdata.unit_vertices.get_unit_size_bytes());
	bdata.indices.reset();
	bdata.total_verts = 0;
	bdata.total_indices = 0;

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
		SortArray<BSortItem3D, SortTransparent> sorter;
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
	Vector3 nc(r_n.x, r_n.y, r_n.z);
	// Normals must be transformed by the inverse-transpose of the basis matrix
	// to avoid distortion when the instance has non-uniform scaling.
	Basis cofactor;
	cofactor.set_column(0, p_tr.basis.get_column(1).cross(p_tr.basis.get_column(2)));
	cofactor.set_column(1, p_tr.basis.get_column(2).cross(p_tr.basis.get_column(0)));
	cofactor.set_column(2, p_tr.basis.get_column(0).cross(p_tr.basis.get_column(1)));
	
	nc = cofactor.xform(nc).normalized();
	r_n.set(nc);
}
