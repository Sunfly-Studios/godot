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

#ifdef GLES2_ENABLED

#include "mesh_storage.h"
#include "config.h"
#include "texture_storage.h"
#include "utilities.h"

using namespace GLES2;

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
		for (MeshInstance *mi : mesh->instances) {
			_mesh_instance_clear(mi);
			mi->mesh = nullptr;
		}
		mesh->instances.clear();
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

	// Protect state bindings
	GLint prev_array_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
	GLint prev_element_buffer = 0;
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_element_buffer);

	Mesh::Surface *s = memnew(Mesh::Surface);
	ERR_FAIL_NULL(s);
	s->format = p_surface.format;
	s->primitive = p_surface.primitive;

	// Vertex data
	if (p_surface.vertex_data.size()) {
		glGenBuffers(1, &s->vertex_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (vertex buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, p_surface.vertex_data.size(), p_surface.vertex_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBufferData (vertex buffer)");
		s->vertex_buffer_size = p_surface.vertex_data.size();
	}

	// Attribute data (colors, UVs)
	if (p_surface.attribute_data.size()) {
		glGenBuffers(1, &s->attribute_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (attribute buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
		glBufferData(GL_ARRAY_BUFFER, p_surface.attribute_data.size(), p_surface.attribute_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBufferData (attribute buffer)");

		s->attribute_buffer_size = p_surface.attribute_data.size();
	}

	// Index data
	if (p_surface.index_count) {
		glGenBuffers(1, &s->index_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (index buffer)");

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, p_surface.index_data.size(), p_surface.index_data.ptr(), GL_STATIC_DRAW);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBufferData (index buffer)");

		s->index_count = p_surface.index_count;
		s->index_buffer_size = p_surface.index_data.size();
	}
	glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_element_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBindBuffer (restore)");

	// Parse format bitfield into OpenGL attributes
	s->version_count = 1;
	s->versions = (Mesh::Surface::Version *)memalloc(sizeof(Mesh::Surface::Version));
	ERR_FAIL_NULL(s->versions);
	Mesh::Surface::Version *v = &s->versions[0];

	v->vertex_array = 0;
	v->input_mask = 0;

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

void MeshStorage::mesh_clear(RID p_mesh) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);

	for (MeshInstance *mi : mesh->instances) {
		_mesh_instance_clear(mi);
	}

	for (uint32_t i = 0; i < mesh->surface_count; i++) {
		Mesh::Surface *s = mesh->surfaces[i];
		if (s->vertex_buffer != 0) {
			glDeleteBuffers(1, &s->vertex_buffer);
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: glDeleteBuffers (vertex buffer)");
		}
		if (s->attribute_buffer != 0) {
			glDeleteBuffers(1, &s->attribute_buffer);
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: glDeleteBuffers (attribute buffer)");
		}
		if (s->skin_buffer != 0) {
			glDeleteBuffers(1, &s->skin_buffer);
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: glDeleteBuffers (skin buffer)");
		}
		if (s->index_buffer != 0) {
			glDeleteBuffers(1, &s->index_buffer);
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: glDeleteBuffers (index buffer)");
		}

		if (s->versions) {
			// Delete VAOs
			for (uint32_t j = 0; j < s->version_count; j++) {
				if (s->versions[j].vertex_array != 0) {
					glDeleteVertexArrays(1, &s->versions[j].vertex_array);
				}
			}
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

void MeshStorage::_mesh_surface_clear(Mesh *mesh, int p_surface) {
	Mesh::Surface *s = mesh->surfaces[p_surface];

	if (s->vertex_buffer != 0) {
		glDeleteBuffers(1, &s->vertex_buffer);
	}
	if (s->attribute_buffer != 0) {
		glDeleteBuffers(1, &s->attribute_buffer);
	}
	if (s->index_buffer != 0) {
		glDeleteBuffers(1, &s->index_buffer);
	}

	// Clean up VAOs
	if (s->versions) {
		for (uint32_t j = 0; j < s->version_count; j++) {
			if (s->versions[j].vertex_array != 0) {
				glDeleteVertexArrays(1, &s->versions[j].vertex_array);
			}
		}
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

	// Protect active
	GLint prev_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, p_offset, data_size, r);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_region: glBufferSubData");

	// Restore state
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
}

void MeshStorage::mesh_surface_update_attribute_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);
	ERR_FAIL_COND(p_data.is_empty());

	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > mesh->surfaces[p_surface]->attribute_buffer_size);
	const uint8_t *r = p_data.ptr();

	// Protect active
	GLint prev_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->attribute_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, p_offset, data_size, r);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_attribute_region: glBufferSubData");

	// Restore state
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
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

	// TODO(GLES2): Return the base AABB.
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
		for (uint32_t j = 0; j < mi->surfaces[i].version_count; j++) {
			if (mi->surfaces[i].versions[j].vertex_array != 0) {
				glDeleteVertexArrays(1, &mi->surfaces[i].versions[j].vertex_array);
			}
		}

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
	// TODO(GLES2): For basic 2D, just push the surface.
	// Will expand this when 3D blend shapes or transform feedback.
	mi->surfaces.push_back(s);
	mi->dirty = true;
}

void MeshStorage::_mesh_instance_remove_surface(MeshInstance *mi, int p_surface) {
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mi->surfaces.size());

	// Clean up instance-specific VAOs
	for (uint32_t j = 0; j < mi->surfaces[p_surface].version_count; j++) {
		if (mi->surfaces[p_surface].versions[j].vertex_array != 0) {
			glDeleteVertexArrays(1, &mi->surfaces[p_surface].versions[j].vertex_array);
		}
	}
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
}

void MeshStorage::_multimesh_free(RID p_rid) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(multimesh);

	_interpolation_data.notify_free_multimesh(p_rid);
	_update_dirty_multimeshes();
	multimesh_allocate_data(p_rid, 0, RS::MULTIMESH_TRANSFORM_2D);

	if (multimesh->buffer != 0) {
		glDeleteBuffers(1, &multimesh->buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::_multimesh_free: glDeleteBuffers");
	}

	if (multimesh->data_cache_dirty_regions) {
		memdelete_arr(multimesh->data_cache_dirty_regions);
	}

	multimesh->dependency.deleted_notify(p_rid);
	multimesh_owner.free(p_rid);
}

void MeshStorage::_multimesh_allocate_data(RID p_multimesh, int p_instances, RS::MultimeshTransformFormat p_transform_format, bool p_use_colors, bool p_use_custom_data, bool p_use_indirect) {

}

int MeshStorage::_multimesh_get_instance_count(RID p_multimesh) const {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, 0);
	return multimesh->instances;
}

void MeshStorage::_multimesh_set_mesh(RID p_multimesh, RID p_mesh) {

}

#define MULTIMESH_DIRTY_REGION_SIZE 512

void MeshStorage::_multimesh_make_local(MultiMesh *multimesh) const {

}

void MeshStorage::_multimesh_mark_dirty(MultiMesh *multimesh, int p_index, bool p_aabb) {

}

void MeshStorage::_multimesh_mark_all_dirty(MultiMesh *multimesh, bool p_data, bool p_aabb) {

}

void MeshStorage::_multimesh_re_create_aabb(MultiMesh *multimesh, const float *p_data, int p_instances) {

}

void MeshStorage::_multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) {

}

void MeshStorage::_multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) {

}

void MeshStorage::_multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) {

}

void MeshStorage::_multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) {

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
    return Transform3D();
}

Transform2D MeshStorage::_multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const {
    return Transform2D();
}

Color MeshStorage::_multimesh_instance_get_color(RID p_multimesh, int p_index) const {
	return Color();
}

Color MeshStorage::_multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const {
	return Color();
}

void MeshStorage::_multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) {

}

RID MeshStorage::_multimesh_get_command_buffer_rd_rid(RID p_multimesh) const {
	ERR_FAIL_V_MSG(RID(), "GLES2 does not implement indirect multimeshes.");
}

RID MeshStorage::_multimesh_get_buffer_rd_rid(RID p_multimesh) const {
	ERR_FAIL_V_MSG(RID(), "GLES2 does not contain a Rid for the multimesh buffer.");
}

Vector<float> MeshStorage::_multimesh_get_buffer(RID p_multimesh) const {
    return Vector<float>();
}

void MeshStorage::_multimesh_set_visible_instances(RID p_multimesh, int p_visible) {

}

int MeshStorage::_multimesh_get_visible_instances(RID p_multimesh) const {
    return 0;
}

MeshStorage::MultiMeshInterpolator *MeshStorage::_multimesh_get_interpolator(RID p_multimesh) const {
    return nullptr;
}

void MeshStorage::_update_dirty_multimeshes() {

}

/* SKELETON API */

RID MeshStorage::skeleton_allocate() {
	return skeleton_owner.allocate_rid();
}

void MeshStorage::skeleton_initialize(RID p_rid) {
	skeleton_owner.initialize_rid(p_rid, Skeleton());
}

void MeshStorage::skeleton_free(RID p_rid) {

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

#endif // GLES2_ENABLED
