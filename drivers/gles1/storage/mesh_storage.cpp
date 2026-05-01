/**************************************************************************/
/*  mesh_storage.cpp                                                      */
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

#ifdef GLES1_ENABLED

#include "mesh_storage.h"
#include "config.h"
#include "texture_storage.h"
#include "utilities.h"

using namespace GLES1;

MeshStorage *MeshStorage::singleton = nullptr;

MeshStorage *MeshStorage::get_singleton() {
	return singleton;
}

MeshStorage::MeshStorage() {
	singleton = this;
}

MeshStorage::~MeshStorage() {
	singleton = nullptr;
}

/* MESH API */

RID MeshStorage::mesh_allocate() {
	return mesh_owner.allocate_rid();
}

void MeshStorage::mesh_initialize(RID p_rid) {
	mesh_owner.initialize_rid(p_rid, Mesh());
}

void MeshStorage::mesh_free(RID p_rid) {
	mesh_clear(p_rid);
	mesh_set_shadow_mesh(p_rid, RID());
	Mesh *mesh = mesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(mesh);

	mesh->dependency.deleted_notify(p_rid);
	if (mesh->instances.size()) {
		ERR_PRINT("deleting mesh with active instances");
	}
	if (mesh->shadow_owners.size()) {
		for (Mesh *E : mesh->shadow_owners) {
			Mesh *shadow_owner = E;
			shadow_owner->shadow_mesh = RID();
			shadow_owner->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
		}
	}
	mesh_owner.free(p_rid);
}

void MeshStorage::mesh_set_blend_shape_count(RID p_mesh, int p_blend_shape_count) {
	ERR_FAIL_COND(p_blend_shape_count < 0);

	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);

	ERR_FAIL_COND(mesh->surface_count > 0); //surfaces already exist
	mesh->blend_shape_count = p_blend_shape_count;
}

bool MeshStorage::mesh_needs_instance(RID p_mesh, bool p_has_skeleton) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, false);

	return mesh->blend_shape_count > 0 || (mesh->has_bone_weights && p_has_skeleton);
}

void MeshStorage::mesh_add_surface(RID p_mesh, const RS::SurfaceData &p_surface) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);

	Mesh::Surface *s = memnew(Mesh::Surface);
	ERR_FAIL_NULL(s);
	s->format = p_surface.format;
	s->primitive = p_surface.primitive;

	s->vertex_buffer = 0;
	s->attribute_buffer = 0;
	s->index_buffer = 0;
	s->skin_buffer = 0;

	GLint prev_array_buffer = 0;
	GLint prev_element_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_element_buffer);

	// Vertex data
	if (p_surface.vertex_data.size()) {
		glGenBuffers(1, &s->vertex_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (vertex buffer)");

		if (likely(s->vertex_buffer != 0)) {
			glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s->vertex_buffer, p_surface.vertex_data.size(), p_surface.vertex_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW, "Mesh vertex buffer");
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (vertex buffer)");
		} else {
			WARN_PRINT_ONCE("GLES1: Failed to generate vertex buffer. Using client memory fallback.");
			s->vertex_buffer_fallback = p_surface.vertex_data;
		}

		s->vertex_buffer_size = p_surface.vertex_data.size();
	}

	// Attribute data
	if (p_surface.attribute_data.size()) {
		glGenBuffers(1, &s->attribute_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (attributes buffer)");

		if (likely(s->attribute_buffer != 0)) {
			glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s->attribute_buffer, p_surface.attribute_data.size(), p_surface.attribute_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW, "Mesh attribute buffer");
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (attributes buffer)");
		} else {
			WARN_PRINT_ONCE("GLES1: Failed to generate attribute buffer. Using client memory fallback.");
			s->attribute_buffer_fallback = p_surface.attribute_data;
		}

		s->attribute_buffer_size = p_surface.attribute_data.size();
	}

	// Index data
	if (p_surface.index_count) {
		glGenBuffers(1, &s->index_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (indices buffer)");

		if (likely(s->index_buffer != 0)) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer, p_surface.index_data.size(), p_surface.index_data.ptr(), GL_STATIC_DRAW, "Mesh index buffer");
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (indices buffer)");
		} else {
			WARN_PRINT_ONCE("GLES1: Failed to generate index buffer. Using client memory fallback.");
			s->index_buffer_fallback = p_surface.index_data;
		}

		s->index_count = p_surface.index_count;
		s->index_buffer_size = p_surface.index_data.size();
	}

	glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_element_buffer);
	GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glBindBuffer (unbind)");

	// Parse format bitfield into OpenGL attributes
	s->version_count = 1;
	s->versions = (Mesh::Surface::Version *)memalloc(sizeof(Mesh::Surface::Version));
	ERR_FAIL_NULL(s->versions);
	Mesh::Surface::Version *v = &s->versions[0];

	for (int i = 0; i < RS::ARRAY_MAX; i++) {
		v->attribs[i].enabled = false;
		v->attribs[i].size = 0;
		v->attribs[i].stride = 0;
		v->attribs[i].type = 0;
		v->attribs[i].offset = 0;
	}

	uint64_t format = p_surface.format;

	// Map vertex buffer (positions)
	int vertex_stride = p_surface.vertex_count > 0 ? (p_surface.vertex_data.size() / p_surface.vertex_count) : 0;

	if (format & RS::ARRAY_FORMAT_VERTEX) {
		v->attribs[RS::ARRAY_VERTEX].enabled = true;
		v->attribs[RS::ARRAY_VERTEX].size = (format & RS::ARRAY_FLAG_USE_2D_VERTICES) ? 2 : 3;
		v->attribs[RS::ARRAY_VERTEX].type = GL_FLOAT;
		v->attribs[RS::ARRAY_VERTEX].stride = vertex_stride;
		v->attribs[RS::ARRAY_VERTEX].offset = 0;
	}

	// Map attribute buffer (colors & UVs)
	int attr_stride = (p_surface.vertex_count > 0 && p_surface.attribute_data.size() > 0) ? (p_surface.attribute_data.size() / p_surface.vertex_count) : 0;
	int current_attr_offset = 0;

	if (format & RS::ARRAY_FORMAT_COLOR) {
		v->attribs[RS::ARRAY_COLOR].enabled = true;
		v->attribs[RS::ARRAY_COLOR].size = 4;
		v->attribs[RS::ARRAY_COLOR].type = GL_UNSIGNED_BYTE;
		v->attribs[RS::ARRAY_COLOR].stride = attr_stride;
		v->attribs[RS::ARRAY_COLOR].offset = current_attr_offset;
		current_attr_offset += 4;
	}

	if (format & RS::ARRAY_FORMAT_TEX_UV) {
		v->attribs[RS::ARRAY_TEX_UV].enabled = true;
		v->attribs[RS::ARRAY_TEX_UV].size = 2;
		v->attribs[RS::ARRAY_TEX_UV].type = GL_FLOAT;
		v->attribs[RS::ARRAY_TEX_UV].stride = attr_stride;
		v->attribs[RS::ARRAY_TEX_UV].offset = current_attr_offset;
		current_attr_offset += 8;
	}

	s->vertex_count = p_surface.vertex_count;
	s->aabb = p_surface.aabb;

	if (mesh->surface_count == 0) {
		mesh->aabb = p_surface.aabb;
	} else {
		mesh->aabb.merge_with(p_surface.aabb);
	}

	s->material = p_surface.material;

	mesh->surfaces = (Mesh::Surface **)memrealloc(mesh->surfaces, sizeof(Mesh::Surface *) * (mesh->surface_count + 1));
	ERR_FAIL_NULL(mesh->surfaces);

	mesh->surfaces[mesh->surface_count] = s;
	mesh->surface_count++;

	for (MeshInstance *mi : mesh->instances) {
		_mesh_instance_add_surface(mi, mesh, mesh->surface_count - 1);
	}

	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

void MeshStorage::_mesh_surface_clear(Mesh *mesh, int p_surface) {
	Mesh::Surface *s = mesh->surfaces[p_surface];
	ERR_FAIL_NULL(s);

	if (s->vertex_buffer != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(s->vertex_buffer);
		s->vertex_buffer = 0;
	}
	s->vertex_buffer_fallback.clear();

	if (s->attribute_buffer != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(s->attribute_buffer);
		s->attribute_buffer = 0;
	}
	s->attribute_buffer_fallback.clear();

	if (s->index_buffer != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(s->index_buffer);
		s->index_buffer = 0;
	}
	s->index_buffer_fallback.clear();

	if (s->skin_buffer != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(s->skin_buffer);
		s->skin_buffer = 0;
	}
	s->skin_buffer_fallback.clear();

	if (s->versions) {
		memfree(s->versions);
	}

	memdelete(s);
}

int MeshStorage::mesh_get_blend_shape_count(RID p_mesh) const {
	const Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, -1);
	return mesh->blend_shape_count;
}

void MeshStorage::mesh_set_blend_shape_mode(RID p_mesh, RS::BlendShapeMode p_mode) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_INDEX((int)p_mode, 2);

	mesh->blend_shape_mode = p_mode;
}

RS::BlendShapeMode MeshStorage::mesh_get_blend_shape_mode(RID p_mesh) const {
    return RS::BLEND_SHAPE_MODE_NORMALIZED;
}

void MeshStorage::mesh_surface_update_vertex_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);
	ERR_FAIL_COND(p_data.is_empty());

	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > mesh->surfaces[p_surface]->vertex_buffer_size);
	const uint8_t *r = p_data.ptr();
	ERR_FAIL_NULL(r);

	if (likely(mesh->surfaces[p_surface]->vertex_buffer != 0)) {
		// Capture
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

		glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->vertex_buffer);
		glBufferSubData(GL_ARRAY_BUFFER, p_offset, data_size, r);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_update_vertex_region: glBufferSubData");

		// Restore
		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	} else {
		uint8_t *dst = mesh->surfaces[p_surface]->vertex_buffer_fallback.ptrw();
		ERR_FAIL_NULL(dst);
		memcpy(dst + p_offset, r, data_size);
	}
}

void MeshStorage::mesh_surface_update_attribute_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);
	ERR_FAIL_COND(p_data.is_empty());

	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > mesh->surfaces[p_surface]->attribute_buffer_size);
	const uint8_t *r = p_data.ptr();
	ERR_FAIL_NULL(r);

	if (likely(mesh->surfaces[p_surface]->attribute_buffer != 0)) {
		// Capture
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

		glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->attribute_buffer);
		glBufferSubData(GL_ARRAY_BUFFER, p_offset, data_size, r);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_update_attribute_region: glBufferSubData");

		// Restore
		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	} else {
		uint8_t *dst = mesh->surfaces[p_surface]->attribute_buffer_fallback.ptrw();
		ERR_FAIL_NULL(dst);
		memcpy(dst + p_offset, r, data_size);
	}
}

void MeshStorage::mesh_surface_update_skin_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {

}

void MeshStorage::mesh_surface_set_material(RID p_mesh, int p_surface, RID p_material) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);

	mesh->surfaces[p_surface]->material = p_material;
	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
	mesh->material_cache.clear();
}

RID MeshStorage::mesh_surface_get_material(RID p_mesh, int p_surface) const {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, RID());
	ERR_FAIL_UNSIGNED_INDEX_V((uint32_t)p_surface, mesh->surface_count, RID());

	return mesh->surfaces[p_surface]->material;
}

RS::SurfaceData MeshStorage::mesh_get_surface(RID p_mesh, int p_surface) const {
    return RS::SurfaceData();
}

int MeshStorage::mesh_get_surface_count(RID p_mesh) const {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, 0);
	return mesh->surface_count;
}

void MeshStorage::mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	mesh->custom_aabb = p_aabb;

	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

AABB MeshStorage::mesh_get_custom_aabb(RID p_mesh) const {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, AABB());
	return mesh->custom_aabb;
}

AABB MeshStorage::mesh_get_aabb(RID p_mesh, RID p_skeleton) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, AABB());

	if (mesh->custom_aabb != AABB()) {
		return mesh->custom_aabb;
	}

	// TODO(GLES1): Return the base AABB.
	// Skeleton AABB calculations can be added when moving to 3D.
	return mesh->aabb;
}

void MeshStorage::mesh_set_path(RID p_mesh, const String &p_path) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);

	mesh->path = p_path;
}

String MeshStorage::mesh_get_path(RID p_mesh) const {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, String());

	return mesh->path;
}

void MeshStorage::mesh_set_shadow_mesh(RID p_mesh, RID p_shadow_mesh) {

}

void MeshStorage::mesh_clear(RID p_mesh) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);

	for (MeshInstance *mi : mesh->instances) {
		_mesh_instance_clear(mi);
	}

	for (uint32_t i = 0; i < mesh->surface_count; i++) {
		Mesh::Surface *s = mesh->surfaces[i];

		if (s->vertex_buffer != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(s->vertex_buffer);
			s->vertex_buffer = 0;
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_clear: buffer_free_data (vertex buffer)");
		}
		if (s->attribute_buffer != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(s->attribute_buffer);
			s->attribute_buffer = 0;
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_clear: buffer_free_data (attributes buffer)");
		}
		if (s->skin_buffer != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(s->skin_buffer);
			s->skin_buffer = 0;
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_clear: buffer_free_data (skin buffer)");
		}
		if (s->index_buffer != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(s->index_buffer);
			s->index_buffer = 0;
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_clear: buffer_free_data (indices buffer)");
		}

		if (s->versions) {
			memfree(s->versions);
		}

		memdelete(s);
	}

	if (mesh->surfaces) {
		memfree(mesh->surfaces);
	}

	mesh->surfaces = nullptr;
	mesh->surface_count = 0;
	mesh->material_cache.clear();
	mesh->has_bone_weights = false;
	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

void MeshStorage::_mesh_surface_generate_version_for_input_mask(Mesh::Surface::Version &v, Mesh::Surface *s, uint64_t p_input_mask, MeshInstance::Surface *mis) {

}

void MeshStorage::mesh_surface_remove(RID p_mesh, int p_surface) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);

	_mesh_surface_clear(mesh, p_surface);

	// Shift remaining surfaces down
	for (uint32_t i = p_surface; i < mesh->surface_count - 1; i++) {
		mesh->surfaces[i] = mesh->surfaces[i + 1];
	}

	mesh->surface_count--;
	if (mesh->surface_count == 0) {
		memfree(mesh->surfaces);
		mesh->surfaces = nullptr;
	} else {
		mesh->surfaces = (Mesh::Surface **)memrealloc(mesh->surfaces, sizeof(Mesh::Surface *) * mesh->surface_count);
		ERR_FAIL_NULL(mesh->surfaces);
	}

	// Sync instances to prevent them
	// referencing the removed surface
	for (MeshInstance *mi : mesh->instances) {
		_mesh_instance_remove_surface(mi, p_surface);
	}

	mesh->material_cache.clear();
	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

/* MESH INSTANCE API */

RID MeshStorage::mesh_instance_create(RID p_base) {
	Mesh *mesh = mesh_owner.get_or_null(p_base);
	ERR_FAIL_NULL_V(mesh, RID());

	RID rid = mesh_instance_owner.make_rid();
	MeshInstance *mi = mesh_instance_owner.get_or_null(rid);
	ERR_FAIL_NULL_V(mi, RID());

	mi->mesh = mesh;

	for (uint32_t i = 0; i < mesh->surface_count; i++) {
		_mesh_instance_add_surface(mi, mesh, i);
	}

	mi->I = mesh->instances.push_back(mi);
	mi->dirty = true;

	return rid;
}

void MeshStorage::mesh_instance_free(RID p_rid) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(mi);

	_mesh_instance_clear(mi);
	mi->mesh->instances.erase(mi->I);
	mi->I = nullptr;

	mesh_instance_owner.free(p_rid);
}

void MeshStorage::mesh_instance_set_skeleton(RID p_mesh_instance, RID p_skeleton) {

}

void MeshStorage::mesh_instance_set_blend_shape_weight(RID p_mesh_instance, int p_shape, float p_weight) {

}

void MeshStorage::_mesh_instance_clear(MeshInstance *mi) {
	for (uint32_t i = 0; i < mi->surfaces.size(); i++) {
		if (mi->surfaces[i].versions) {
			memfree(mi->surfaces[i].versions);
			mi->surfaces[i].versions = nullptr;
		}
	}
	mi->surfaces.clear();
	mi->blend_weights.clear();
	mi->skeleton_version = 0;
}

void MeshStorage::_mesh_instance_add_surface(MeshInstance *mi, Mesh *mesh, uint32_t p_surface) {
	MeshInstance::Surface s;
	mi->surfaces.push_back(s);
	mi->dirty = true;
}

void MeshStorage::_mesh_instance_remove_surface(MeshInstance *mi, int p_surface) {
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mi->surfaces.size());

	// Free the host memory for the version trackers.
	if (mi->surfaces[p_surface].versions) {
		memfree(mi->surfaces[p_surface].versions);
	}

	mi->surfaces.remove_at(p_surface);
	mi->dirty = true;
}

void MeshStorage::mesh_instance_check_for_update(RID p_mesh_instance) {

}

void MeshStorage::mesh_instance_set_canvas_item_transform(RID p_mesh_instance, const Transform2D &p_transform) {

}

void MeshStorage::_blend_shape_bind_mesh_instance_buffer(MeshInstance *p_mi, uint32_t p_surface) {

}

void MeshStorage::_compute_skeleton(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface) {

}

void MeshStorage::update_mesh_instances() {

}

/* MULTIMESH API */

RID MeshStorage::_multimesh_allocate() {
	return multimesh_owner.allocate_rid();
}

void MeshStorage::_multimesh_initialize(RID p_rid) {
	multimesh_owner.initialize_rid(p_rid, MultiMesh());
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(multimesh);
	multimesh->buffer = 0;

	// Create a backing GL buffer just in case it's used for 3D later.
	glGenBuffers(1, &multimesh->buffer);
	GL_CHECK_ERROR("GLES1::MeshStorage::_multimesh_initialize: glGenBuffers");

	if (likely(multimesh->buffer != 0)) {
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

		// Pre-register empty size to tracking cache
		glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
		GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, multimesh->buffer, 0, nullptr, GL_DYNAMIC_DRAW, "MultiMesh buffer");

		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	} else {
		WARN_PRINT_ONCE("GLES1: Failed to generate MultiMesh buffer. Using client memory fallback.");
	}
}

void MeshStorage::_multimesh_free(RID p_rid) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(multimesh);

	_interpolation_data.notify_free_multimesh(p_rid);
	_update_dirty_multimeshes();
	multimesh_allocate_data(p_rid, 0, RS::MULTIMESH_TRANSFORM_2D);

	if (multimesh) {
		if (multimesh->buffer != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(multimesh->buffer);
		}
		if (multimesh->data_cache_dirty_regions) {
			memdelete_arr(multimesh->data_cache_dirty_regions);
		}
		multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MULTIMESH);
	}

	multimesh->dependency.deleted_notify(p_rid);
	multimesh_owner.free(p_rid);
}

void MeshStorage::_multimesh_allocate_data(RID p_multimesh, int p_instances, RS::MultimeshTransformFormat p_transform_format, bool p_use_colors, bool p_use_custom_data, bool p_use_indirect) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);

	multimesh->instances = p_instances;
	multimesh->xform_format = p_transform_format;
	multimesh->uses_colors = p_use_colors;
	multimesh->uses_custom_data = p_use_custom_data;

	int xform_size = (p_transform_format == RS::MULTIMESH_TRANSFORM_3D) ? 12 : 8;
	int color_size = p_use_colors ? 4 : 0;
	int custom_data_size = p_use_custom_data ? 4 : 0;

	multimesh->stride_cache = xform_size + color_size + custom_data_size;
	multimesh->color_offset_cache = xform_size;
	multimesh->custom_data_offset_cache = xform_size + color_size;

	int data_size = multimesh->stride_cache * p_instances;
	multimesh->data_cache.resize(data_size);
	multimesh->data_cache.fill(0);

	if (multimesh->data_cache_dirty_regions) {
		memdelete_arr(multimesh->data_cache_dirty_regions);
		multimesh->data_cache_dirty_regions = nullptr;
	}

	multimesh->buffer_set = false;

	// Orphan the VRAM buffer and allocate the correct size
	if (likely(multimesh->buffer != 0)) {
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

		glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
		GLES1::Utilities::get_singleton()->buffer_resize_data(GL_ARRAY_BUFFER, multimesh->buffer, data_size * sizeof(float), nullptr, GL_DYNAMIC_DRAW, "MultiMesh buffer");
		GL_CHECK_ERROR("GLES1::MeshStorage::_multimesh_allocate_data: buffer_resize_data");

		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	}
}

int MeshStorage::_multimesh_get_instance_count(RID p_multimesh) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, 0);
	return multimesh->instances;
}

void MeshStorage::_multimesh_set_mesh(RID p_multimesh, RID p_mesh) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	if (multimesh->mesh == p_mesh || p_mesh.is_null()) {
		return;
	}
	multimesh->mesh = p_mesh;

	if (multimesh->instances == 0) {
		return;
	}

	if (multimesh->data_cache.size()) {
		_multimesh_mark_all_dirty(multimesh, false, true);
	}
	multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

#define MULTIMESH_DIRTY_REGION_SIZE 512

void MeshStorage::_multimesh_make_local(MultiMesh *multimesh) const {

}

void MeshStorage::_multimesh_mark_dirty(MultiMesh *multimesh, int p_index, bool p_aabb) {
	if (p_aabb) {
		multimesh->aabb_dirty = true;
	}
	if (!multimesh->dirty) {
		multimesh->dirty_list = multimesh_dirty_list;
		multimesh_dirty_list = multimesh;
		multimesh->dirty = true;
	}
}

void MeshStorage::_multimesh_mark_all_dirty(MultiMesh *multimesh, bool p_data, bool p_aabb) {
	if (p_aabb) {
		multimesh->aabb_dirty = true;
	}
	if (!multimesh->dirty) {
		multimesh->dirty_list = multimesh_dirty_list;
		multimesh_dirty_list = multimesh;
		multimesh->dirty = true;
	}
}

void MeshStorage::_multimesh_re_create_aabb(MultiMesh *multimesh, const float *p_data, int p_instances) {
	ERR_FAIL_COND(multimesh->mesh.is_null());
	if (multimesh->custom_aabb != AABB()) {
		return;
	}
	AABB aabb;
	AABB mesh_aabb = mesh_get_aabb(multimesh->mesh);
	for (int i = 0; i < p_instances; i++) {
		const float *data = p_data + multimesh->stride_cache * i;

		if (unlikely(!data)) {
			continue;
		}

		Transform3D t;
		if (multimesh->xform_format == RS::MULTIMESH_TRANSFORM_3D) {
			t.basis.rows[0][0] = data[0];
			t.basis.rows[0][1] = data[1];
			t.basis.rows[0][2] = data[2];
			t.origin.x = data[3];
			t.basis.rows[1][0] = data[4];
			t.basis.rows[1][1] = data[5];
			t.basis.rows[1][2] = data[6];
			t.origin.y = data[7];
			t.basis.rows[2][0] = data[8];
			t.basis.rows[2][1] = data[9];
			t.basis.rows[2][2] = data[10];
			t.origin.z = data[11];
		} else {
			t.basis.rows[0][0] = data[0];
			t.basis.rows[0][1] = data[1];
			t.origin.x = data[3];
			t.basis.rows[1][0] = data[4];
			t.basis.rows[1][1] = data[5];
			t.origin.y = data[7];
		}

		if (i == 0) {
			aabb = t.xform(mesh_aabb);
		} else {
			aabb.merge_with(t.xform(mesh_aabb));
		}
	}
	multimesh->aabb = aabb;
}

void MeshStorage::_multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(multimesh->xform_format != RS::MULTIMESH_TRANSFORM_3D);

	float *data = multimesh->data_cache.ptrw() + (p_index * multimesh->stride_cache);
	data[0] = p_transform.basis[0][0];
	data[1] = p_transform.basis[0][1];
	data[2] = p_transform.basis[0][2];
	data[3] = p_transform.origin[0];
	data[4] = p_transform.basis[1][0];
	data[5] = p_transform.basis[1][1];
	data[6] = p_transform.basis[1][2];
	data[7] = p_transform.origin[1];
	data[8] = p_transform.basis[2][0];
	data[9] = p_transform.basis[2][1];
	data[10] = p_transform.basis[2][2];
	data[11] = p_transform.origin[2];
}

void MeshStorage::_multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(multimesh->xform_format != RS::MULTIMESH_TRANSFORM_2D);

	_multimesh_make_local(multimesh);

	float *w = multimesh->data_cache.ptrw();
	ERR_FAIL_NULL(w);

	float *dataptr = w + p_index * multimesh->stride_cache;
	ERR_FAIL_NULL(dataptr);

	dataptr[0] = p_transform.columns[0][0];
	dataptr[1] = p_transform.columns[0][1];
	dataptr[2] = p_transform.columns[1][0];
	dataptr[3] = p_transform.columns[1][1];
	dataptr[4] = p_transform.columns[2][0];
	dataptr[5] = p_transform.columns[2][1];
	dataptr[6] = 0.0f;
	dataptr[7] = 0.0f;

	_multimesh_mark_dirty(multimesh, p_index, true);
}

void MeshStorage::_multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(!multimesh->uses_colors);

	float *data = multimesh->data_cache.ptrw() + (p_index * multimesh->stride_cache) + multimesh->color_offset_cache;
	ERR_FAIL_NULL(data);

	data[0] = p_color.r;
	data[1] = p_color.g;
	data[2] = p_color.b;
	data[3] = p_color.a;
}

void MeshStorage::_multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(!multimesh->uses_custom_data);

	float *data = multimesh->data_cache.ptrw() + (p_index * multimesh->stride_cache) + multimesh->custom_data_offset_cache;
	ERR_FAIL_NULL(data);

	data[0] = p_color.r;
	data[1] = p_color.g;
	data[2] = p_color.b;
	data[3] = p_color.a;
}

RID MeshStorage::_multimesh_get_mesh(RID p_multimesh) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, RID());
	return multimesh->mesh;
}

void MeshStorage::_multimesh_set_custom_aabb(RID p_multimesh, const AABB &p_aabb) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	multimesh->custom_aabb = p_aabb;
	multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

AABB MeshStorage::_multimesh_get_custom_aabb(RID p_multimesh) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, AABB());
	return multimesh->custom_aabb;
}

AABB MeshStorage::_multimesh_get_aabb(RID p_multimesh) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, AABB());

	if (multimesh->custom_aabb != AABB()) {
		return multimesh->custom_aabb;
	}

	if (multimesh->aabb_dirty) {
		_update_dirty_multimeshes();
	}

	return multimesh->aabb;
}

Transform3D MeshStorage::_multimesh_instance_get_transform(RID p_multimesh, int p_index) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Transform3D());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Transform3D());
	ERR_FAIL_COND_V(multimesh->xform_format != RS::MULTIMESH_TRANSFORM_3D, Transform3D());

	const float *data = multimesh->data_cache.ptr() + (p_index * multimesh->stride_cache);
	ERR_FAIL_NULL_V(data, Transform3D());

	Transform3D t;
	t.basis[0][0] = data[0];
	t.basis[0][1] = data[1];
	t.basis[0][2] = data[2];
	t.origin[0] = data[3];
	t.basis[1][0] = data[4];
	t.basis[1][1] = data[5];
	t.basis[1][2] = data[6];
	t.origin[1] = data[7];
	t.basis[2][0] = data[8];
	t.basis[2][1] = data[9];
	t.basis[2][2] = data[10];
	t.origin[2] = data[11];
	return t;
}

Transform2D MeshStorage::_multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Transform2D());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Transform2D());
	ERR_FAIL_COND_V(multimesh->xform_format != RS::MULTIMESH_TRANSFORM_2D, Transform2D());

	_multimesh_make_local(multimesh);

	const float *r = multimesh->data_cache.ptr();
	ERR_FAIL_NULL_V(r, Transform2D());

	const float *dataptr = r + p_index * multimesh->stride_cache;
	ERR_FAIL_NULL_V(dataptr, Transform2D());

	Transform2D t;
	t.columns[0][0] = dataptr[0];
	t.columns[0][1] = dataptr[1];
	t.columns[1][0] = dataptr[2];
	t.columns[1][1] = dataptr[3];
	t.columns[2][0] = dataptr[4];
	t.columns[2][1] = dataptr[5];

	return t;
}

Color MeshStorage::_multimesh_instance_get_color(RID p_multimesh, int p_index) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Color());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Color());
	ERR_FAIL_COND_V(!multimesh->uses_colors, Color());

	const float *data = multimesh->data_cache.ptr() + (p_index * multimesh->stride_cache) + multimesh->color_offset_cache;
	ERR_FAIL_NULL_V(data, Color());

	return Color(data[0], data[1], data[2], data[3]);
}

Color MeshStorage::_multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Color());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Color());
	ERR_FAIL_COND_V(!multimesh->uses_custom_data, Color());

	const float *data = multimesh->data_cache.ptr() + (p_index * multimesh->stride_cache) + multimesh->custom_data_offset_cache;
	ERR_FAIL_NULL_V(data, Color());

	return Color(data[0], data[1], data[2], data[3]);
}

void MeshStorage::_multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	multimesh->data_cache = p_buffer;

	// Pipe it to VRAM in case a 3D extension
	// requests hardware instancing
	if (likely(multimesh->buffer != 0)) {
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

		glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
		glBufferSubData(GL_ARRAY_BUFFER, 0, p_buffer.size() * sizeof(float), p_buffer.ptr());
		GL_CHECK_ERROR("GLES1::MeshStorage::_multimesh_set_buffer: glBufferSubData");

		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	}

	multimesh->buffer_set = true;

	// Generate the AABB.
	if (multimesh->mesh.is_valid() && multimesh->custom_aabb == AABB()) {
		const float *data = p_buffer.ptr();
		ERR_FAIL_NULL(data);

		_multimesh_re_create_aabb(multimesh, data, multimesh->instances);
		multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
	}
}

RID MeshStorage::_multimesh_get_command_buffer_rd_rid(RID p_multimesh) const {
	ERR_FAIL_V_MSG(RID(), "GLES1 does not implement indirect multimeshes.");
}

RID MeshStorage::_multimesh_get_buffer_rd_rid(RID p_multimesh) const {
	ERR_FAIL_V_MSG(RID(), "GLES1 does not contain a Rid for the multimesh buffer.");
}

Vector<float> MeshStorage::_multimesh_get_buffer(RID p_multimesh) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Vector<float>());
	return multimesh->data_cache;
}

void MeshStorage::_multimesh_set_visible_instances(RID p_multimesh, int p_visible) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	multimesh->visible_instances = p_visible;
}

int MeshStorage::_multimesh_get_visible_instances(RID p_multimesh) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, 0);
	return multimesh->visible_instances;
}

MeshStorage::MultiMeshInterpolator *MeshStorage::_multimesh_get_interpolator(RID p_multimesh) const {
    return nullptr;
}

void MeshStorage::_update_dirty_multimeshes() {
	while (multimesh_dirty_list) {
		MultiMesh *multimesh = multimesh_dirty_list;

		if (likely(multimesh)) {
			if (multimesh->data_cache.size()) {
				const float *data = multimesh->data_cache.ptr();

				uint32_t visible_instances = multimesh->visible_instances >= 0 ? multimesh->visible_instances : multimesh->instances;

				if (multimesh->aabb_dirty && multimesh->mesh.is_valid()) {
					multimesh->aabb_dirty = false;
					if (multimesh->custom_aabb == AABB()) {
						_multimesh_re_create_aabb(multimesh, data, visible_instances);
						multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
					}
				}
			}

			multimesh_dirty_list = multimesh->dirty_list;
			multimesh->dirty_list = nullptr;
			multimesh->dirty = false;
		}
	}
	multimesh_dirty_list = nullptr;
}

/* SKELETON API */

RID MeshStorage::skeleton_allocate() {
	return skeleton_owner.allocate_rid();
}

void MeshStorage::skeleton_initialize(RID p_rid) {
	skeleton_owner.initialize_rid(p_rid, Skeleton());
}

void MeshStorage::skeleton_free(RID p_rid) {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(skeleton);

	skeleton->dependency.deleted_notify(p_rid);
	skeleton_owner.free(p_rid);
}

void MeshStorage::_skeleton_make_dirty(Skeleton *skeleton) {
	if (!skeleton->dirty) {
		skeleton->dirty = true;
		skeleton->dirty_list = skeleton_dirty_list;
		skeleton_dirty_list = skeleton;
	}
}

void MeshStorage::skeleton_allocate_data(RID p_skeleton, int p_bones, bool p_2d_skeleton) {

}

void MeshStorage::skeleton_set_base_transform_2d(RID p_skeleton, const Transform2D &p_base_transform) {

}

int MeshStorage::skeleton_get_bone_count(RID p_skeleton) const {
    return 0;
}

void MeshStorage::skeleton_bone_set_transform(RID p_skeleton, int p_bone, const Transform3D &p_transform) {

}

Transform3D MeshStorage::skeleton_bone_get_transform(RID p_skeleton, int p_bone) const {
    return Transform3D();
}

void MeshStorage::skeleton_bone_set_transform_2d(RID p_skeleton, int p_bone, const Transform2D &p_transform) {

}

Transform2D MeshStorage::skeleton_bone_get_transform_2d(RID p_skeleton, int p_bone) const {
    return Transform2D();
}

void MeshStorage::_update_dirty_skeletons() {

}

void MeshStorage::skeleton_update_dependency(RID p_skeleton, DependencyTracker *p_instance) {

}

#endif // GLES1_ENABLED
