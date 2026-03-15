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
void RasterizerSceneBatcherCommon<T_API>::batch_constructor() {
	// Constructor logic

	batch_initialize();
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_initialize() {
	bdata.settings_use_batching = GLOBAL_GET("rendering/batching/use_batching");
	bdata.max_vertices = 65536; // Standard 16-bit index limit
	bdata.max_indices = 65536 * 2;

	bdata.vertices.create(bdata.max_vertices);
	bdata.indices.create(bdata.max_indices);
	bdata.batches.create(256);
	bdata.sort_items.create(1024);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_begin() {
	// Called at the start of the 3D pass
	bdata.reset_flush();
	bdata.sort_items.reset();
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_render_items(RenderGeometryInstance **p_instances, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	if (!bdata.settings_use_batching) {
		// Fallback to unbatched rendering
		// get_this()->scene_render_items_implementation(p_instances, p_count, ...);
		return;
	}

	_render_item_state.reset();

	// TODO(GLES1 & GLES2):
	// 1. Calculate depths and state hashes
	record_items(p_instances, p_count, p_camera_transform);

	// 2. Sort depending on blend mode
	sort_items(p_transparent);

	// 3. Bake geometry into the dynamic buffer
	join_sorted_items();

	// 4. Flush any remaining batches
	// flush_render_batches(...);
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::record_items(RenderGeometryInstance **p_instances, int p_count, const Transform3D &p_camera_transform) {
	for (int i = 0; i < p_count; i++) {
		RenderGeometryInstance *instance = p_instances[i];
		BSortItem3D *s = bdata.sort_items.request();
		if (!s) {
			bdata.sort_items.grow();
			s = bdata.sort_items.request();
		}

		s->instance = instance;
		// Calculate linear depth from camera for sorting
		// s->depth = p_camera_transform.affine_inverse().xform(instance->transform.origin).z;
		s->depth = 0.0f; 
		s->material_hash = 0; // Hash based on RID and shader
	}
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::sort_items(bool p_transparent) {
	// TODO (GLES1 & GLES2): Implement custom radix sort on bdata.sort_items
	// If opaque: Sort by material_hash (minimize state changes)
	// If transparent: Sort by depth (back to front)
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::join_sorted_items() {
	// Iterate through bdata.sort_items
	// Extract mesh data via MeshStorage
	// Call prefill_joined_item() to transform vertices and pack them into bdata.vertices
	// When material changes or buffer fills, call flush_render_batches()
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::try_join_item(RenderGeometryInstance *p_instance, RenderItemState3D &r_ris) {
	// Return false if material breaks or buffer is full
	return true;
}

template <typename T_API>
bool RasterizerSceneBatcherCommon<T_API>::prefill_joined_item(RenderGeometryInstance *p_instance) {
	// 1. Get Mesh/Surface from instance
	// 2. Extract vertex data
	// 3. For each vertex: _software_transform_vertex()
	// 4. Push to bdata.vertices and bdata.indices
	return false; // Return true if buffer gets full mid-fill
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::flush_render_batches(RenderGeometryInstance *p_first_instance, RID p_material) {
	// 1. Upload bdata.vertices and bdata.indices to GPU via glBufferSubData
	// 2. Bind material/shader
	// 3. Call get_this()->render_batches(...) to execute draw calls
	bdata.reset_flush();
}

template <typename T_API>
void RasterizerSceneBatcherCommon<T_API>::batch_scene_end() {
	// Cleanup at end of 3D pass
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
	nc = p_tr.basis.xform(nc).normalized();
	r_n.set(nc);
}
