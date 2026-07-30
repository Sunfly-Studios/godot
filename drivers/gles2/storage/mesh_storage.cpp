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

	{
		skeleton_shader.shader.initialize();
		skeleton_shader.shader_version = skeleton_shader.shader.version_create();
	}
}

MeshStorage::~MeshStorage() {
	singleton = nullptr;
	skeleton_shader.shader.version_free(skeleton_shader.shader_version);
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
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGetIntegerv state protections");

	Mesh::Surface *s = memnew(Mesh::Surface);
	ERR_FAIL_NULL(s);
	s->format = p_surface.format;
	s->primitive = p_surface.primitive;

	// Vertex data
	if (p_surface.vertex_data.size()) {
		glGenBuffers(1, &s->vertex_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (vertex buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s->vertex_buffer, p_surface.vertex_data.size(), p_surface.vertex_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW, "Mesh vertex buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (vertex buffer)");
		s->vertex_buffer_size = p_surface.vertex_data.size();
	}

	// Attribute data (colors, UVs)
	if (p_surface.attribute_data.size()) {
		glGenBuffers(1, &s->attribute_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (attribute buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s->attribute_buffer, p_surface.attribute_data.size(), p_surface.attribute_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW, "Mesh attribute buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (attribute buffer)");

		s->attribute_buffer_size = p_surface.attribute_data.size();
	}

	// Index data
	if (p_surface.index_count) {
		glGenBuffers(1, &s->index_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (index buffer)");

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer, p_surface.index_data.size(), p_surface.index_data.ptr(), GL_STATIC_DRAW, "Mesh index buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (index buffer)");

		s->index_count = p_surface.index_count;
		s->index_buffer_size = p_surface.index_data.size();
	}

	// Skin data
	if (p_surface.skin_data.size()) {
		glGenBuffers(1, &s->skin_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, s->skin_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(
			GL_ARRAY_BUFFER,
			s->skin_buffer,
			p_surface.skin_data.size(),
			p_surface.skin_data.ptr(),
			(s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW,
			"Mesh skin buffer"
		);
		s->skin_buffer_size = p_surface.skin_data.size();
	}

	glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_element_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBindBuffer (restore)");

	// Skeleton / transform properties
	s->bone_aabbs = p_surface.bone_aabbs;
	s->mesh_to_skeleton_xform = p_surface.mesh_to_skeleton_xform;
	s->uv_scale = p_surface.uv_scale;

	if (p_surface.format & RS::ARRAY_FORMAT_BONES) {
		mesh->has_bone_weights = true;
	}

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

	// Map skin buffer (bones & weights)
	if (format & RS::ARRAY_FORMAT_BONES) {
		bool use_8 = format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;
		int skin_stride = sizeof(uint16_t) * (use_8 ? 16 : 8);

		v->attribs[RS::ARRAY_CUSTOM0].enabled = true;
		v->attribs[RS::ARRAY_CUSTOM0].size = use_8 ? 8 : 4;
		v->attribs[RS::ARRAY_CUSTOM0].type = GL_UNSIGNED_SHORT;
		v->attribs[RS::ARRAY_CUSTOM0].stride = skin_stride;
		v->attribs[RS::ARRAY_CUSTOM0].offset = 0;
	}

	if (format & RS::ARRAY_FORMAT_WEIGHTS) {
		bool use_8 = format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;
		int skin_stride = sizeof(uint16_t) * (use_8 ? 16 : 8);

		v->attribs[RS::ARRAY_CUSTOM1].enabled = true;
		v->attribs[RS::ARRAY_CUSTOM1].size = use_8 ? 8 : 4;
		v->attribs[RS::ARRAY_CUSTOM1].type = GL_UNSIGNED_SHORT;
		v->attribs[RS::ARRAY_CUSTOM1].stride = skin_stride;
		v->attribs[RS::ARRAY_CUSTOM1].offset = sizeof(uint16_t) * (use_8 ? 8 : 4);
	}

	s->vertex_count = p_surface.vertex_count;
	s->aabb = p_surface.aabb;

	if (mesh->surface_count == 0) {
		mesh->aabb = p_surface.aabb;
	} else {
		mesh->aabb.merge_with(p_surface.aabb);
	}
	mesh->skeleton_aabb_version = 0;

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
			GLES2::Utilities::get_singleton()->buffer_free_data(s->vertex_buffer);
			s->vertex_buffer = 0;
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: buffer_free_data (vertex buffer)");
		}
		if (s->attribute_buffer != 0) {
			GLES2::Utilities::get_singleton()->buffer_free_data(s->attribute_buffer);
			s->attribute_buffer = 0;
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: buffer_free_data (attribute buffer)");
		}
		if (s->skin_buffer != 0) {
			GLES2::Utilities::get_singleton()->buffer_free_data(s->skin_buffer);
			s->skin_buffer = 0;
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: buffer_free_data (skin buffer)");
		}
		if (s->index_buffer != 0) {
			GLES2::Utilities::get_singleton()->buffer_free_data(s->index_buffer);
			s->index_buffer = 0;
			GL_CHECK_ERROR("GLES2::MeshStorage::mesh_clear: buffer_free_data (index buffer)");
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
		GLES2::Utilities::get_singleton()->buffer_free_data(s->vertex_buffer);
	}
	if (s->attribute_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(s->attribute_buffer);
	}
	if (s->index_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(s->index_buffer);
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
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, RS::BLEND_SHAPE_MODE_NORMALIZED);
	return mesh->blend_shape_mode;
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
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		mesh->surfaces[p_surface]->vertex_buffer,
		p_offset,
		data_size,
		r
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_vertex_region: buffer_update_data");
	
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
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		mesh->surfaces[p_surface]->attribute_buffer,
		p_offset,
		data_size,
		r
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_attribute_region: buffer_update_data");

	// Restore state
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
}

void MeshStorage::mesh_surface_update_skin_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);
	ERR_FAIL_COND(p_data.is_empty());

	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > mesh->surfaces[p_surface]->skin_buffer_size);
	const uint8_t *r = p_data.ptr();

	// Protect state
	GLint prev_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->skin_buffer);
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		mesh->surfaces[p_surface]->skin_buffer,
		p_offset,
		data_size,
		r
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_skin_region: buffer_update_data");

	// Restore state
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
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
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, RS::SurfaceData());
	ERR_FAIL_UNSIGNED_INDEX_V((uint32_t)p_surface, mesh->surface_count, RS::SurfaceData());

	Mesh::Surface &s = *mesh->surfaces[p_surface];

	RS::SurfaceData sd;
	sd.format = s.format;
	if (s.vertex_buffer != 0) {
		sd.vertex_data = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.vertex_buffer, s.vertex_buffer_size);

		// When using an uncompressed buffer with normals, but without tangents, we have to trim the padding.
		if (!(s.format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) && (s.format & RS::ARRAY_FORMAT_NORMAL) && !(s.format & RS::ARRAY_FORMAT_TANGENT)) {
			sd.vertex_data.resize(sd.vertex_data.size() - sizeof(uint16_t) * 2);
		}
	}

	if (s.attribute_buffer != 0) {
		sd.attribute_data = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.attribute_buffer, s.attribute_buffer_size);
	}

	if (s.skin_buffer != 0) {
		sd.skin_data = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.skin_buffer, s.skin_buffer_size);
	}

	sd.vertex_count = s.vertex_count;
	sd.index_count = s.index_count;
	sd.primitive = s.primitive;

	if (sd.index_count) {
		sd.index_data = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ELEMENT_ARRAY_BUFFER, s.index_buffer, s.index_buffer_size);
	}

	sd.aabb = s.aabb;
	for (uint32_t i = 0; i < s.lod_count; i++) {
		RS::SurfaceData::LOD lod;
		lod.edge_length = s.lods[i].edge_length;
		lod.index_data = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ELEMENT_ARRAY_BUFFER, s.lods[i].index_buffer, s.lods[i].index_buffer_size);
		sd.lods.push_back(lod);
	}

	sd.bone_aabbs = s.bone_aabbs;
	sd.mesh_to_skeleton_xform = s.mesh_to_skeleton_xform;

	if (mesh->blend_shape_count) {
		sd.blend_shape_data = Vector<uint8_t>();
		for (uint32_t i = 0; i < mesh->blend_shape_count; i++) {
			sd.blend_shape_data.append_array(GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.blend_shapes[i].vertex_buffer, s.vertex_buffer_size));
		}
	}

	sd.uv_scale = s.uv_scale;

	return sd;
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
	if (!mesh) {
		return AABB();
	}

	if (mesh->custom_aabb != AABB()) {
		return mesh->custom_aabb;
	}

	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);

	if (!skeleton || skeleton->size == 0 || mesh->skeleton_aabb_version == skeleton->version) {
		return mesh->aabb;
	}

	// Calculate AABB based on Skeleton

	AABB aabb;

	for (uint32_t i = 0; i < mesh->surface_count; i++) {
		AABB laabb;
		const Mesh::Surface &surface = *mesh->surfaces[i];
		if ((surface.format & RS::ARRAY_FORMAT_BONES) && surface.bone_aabbs.size()) {
			int bs = surface.bone_aabbs.size();
			const AABB *skbones = surface.bone_aabbs.ptr();

			int sbs = skeleton->size;
			ERR_CONTINUE(bs > sbs);
			const float *baseptr = skeleton->data.ptr();

			bool found_bone_aabb = false;

			if (skeleton->use_2d) {
				for (int j = 0; j < bs; j++) {
					if (skbones[j].size == Vector3(-1, -1, -1)) {
						continue; //bone is unused
					}

					const float *dataptr = baseptr + j * 8;

					if (!dataptr) {
						continue;
					}

					Transform3D mtx;

					mtx.basis.rows[0][0] = dataptr[0];
					mtx.basis.rows[0][1] = dataptr[1];
					mtx.origin.x = dataptr[3];

					mtx.basis.rows[1][0] = dataptr[4];
					mtx.basis.rows[1][1] = dataptr[5];
					mtx.origin.y = dataptr[7];

					// Transform bounds to skeleton's space before applying animation data.
					AABB baabb = surface.mesh_to_skeleton_xform.xform(skbones[j]);
					baabb = mtx.xform(baabb);

					if (!found_bone_aabb) {
						laabb = baabb;
						found_bone_aabb = true;
					} else {
						laabb.merge_with(baabb);
					}
				}
			} else {
				for (int j = 0; j < bs; j++) {
					if (skbones[j].size == Vector3(-1, -1, -1)) {
						continue; //bone is unused
					}

					const float *dataptr = baseptr + j * 12;

					if (!dataptr) {
						continue;
					}

					Transform3D mtx;

					mtx.basis.rows[0][0] = dataptr[0];
					mtx.basis.rows[0][1] = dataptr[1];
					mtx.basis.rows[0][2] = dataptr[2];
					mtx.origin.x = dataptr[3];
					mtx.basis.rows[1][0] = dataptr[4];
					mtx.basis.rows[1][1] = dataptr[5];
					mtx.basis.rows[1][2] = dataptr[6];
					mtx.origin.y = dataptr[7];
					mtx.basis.rows[2][0] = dataptr[8];
					mtx.basis.rows[2][1] = dataptr[9];
					mtx.basis.rows[2][2] = dataptr[10];
					mtx.origin.z = dataptr[11];

					// Transform bounds to skeleton's space before applying animation data.
					AABB baabb = surface.mesh_to_skeleton_xform.xform(skbones[j]);
					baabb = mtx.xform(baabb);

					if (!found_bone_aabb) {
						laabb = baabb;
						found_bone_aabb = true;
					} else {
						laabb.merge_with(baabb);
					}
				}
			}

			if (found_bone_aabb) {
				// Transform skeleton bounds back to mesh's space if any animated AABB applied.
				laabb = surface.mesh_to_skeleton_xform.affine_inverse().xform(laabb);
			}

			if (laabb.size == Vector3()) {
				laabb = surface.aabb;
			}
		} else {
			laabb = surface.aabb;
		}

		if (i == 0) {
			aabb = laabb;
		} else {
			aabb.merge_with(laabb);
		}
	}

	mesh->aabb = aabb;
	mesh->skeleton_aabb_version = skeleton->version;
	return aabb;
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
	ERR_FAIL_COND_MSG(p_mesh == p_shadow_mesh, "Cannot set a mesh as its own shadow mesh.");
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);

	Mesh *shadow_mesh = mesh_owner.get_or_null(mesh->shadow_mesh);
	if (shadow_mesh) {
		shadow_mesh->shadow_owners.erase(mesh);
	}
	mesh->shadow_mesh = p_shadow_mesh;

	shadow_mesh = mesh_owner.get_or_null(mesh->shadow_mesh);

	if (shadow_mesh) {
		shadow_mesh->shadow_owners.insert(mesh);
	}

	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

void MeshStorage::_mesh_surface_generate_version_for_input_mask(Mesh::Surface::Version &v, Mesh::Surface *s, uint64_t p_input_mask, MeshInstance::Surface *mis) {
	int position_stride = 0;
	int normal_tangent_stride = 0;
	int attributes_stride = 0;
	int skin_stride = 0;

	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		v.attribs[i].enabled = false;
		v.attribs[i].integer = false; // GLES2 does not utilize integer attrib pointers
		if (!(s->format & (1ULL << i))) {
			continue;
		}

		if ((p_input_mask & (1ULL << i))) {
			v.attribs[i].enabled = true;
		}

		switch (i) {
			case RS::ARRAY_VERTEX: {
				v.attribs[i].offset = 0;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].normalized = GL_FALSE;
				if (s->format & RS::ARRAY_FLAG_USE_2D_VERTICES) {
					v.attribs[i].size = 2;
					position_stride = v.attribs[i].size * sizeof(float);
				} else {
					if (!mis && (s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
						v.attribs[i].size = 4;
						position_stride = v.attribs[i].size * sizeof(uint16_t);
						v.attribs[i].type = GL_UNSIGNED_SHORT;
						v.attribs[i].normalized = GL_TRUE;
					} else {
						v.attribs[i].size = 3;
						position_stride = v.attribs[i].size * sizeof(float);
					}
				}
			} break;
			case RS::ARRAY_NORMAL: {
				if (!mis && (s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
					v.attribs[i].size = 2;
					normal_tangent_stride += 2 * v.attribs[i].size;
				} else {
					v.attribs[i].size = 4;
					if (!(s->format & RS::ARRAY_FORMAT_TANGENT)) {
						normal_tangent_stride += (mis ? sizeof(float) : sizeof(uint16_t)) * 2;
					} else {
						normal_tangent_stride += (mis ? sizeof(float) : sizeof(uint16_t)) * 4;
					}
				}

				if (mis) {
					v.attribs[i].offset = position_stride;
					normal_tangent_stride += position_stride;
					position_stride = normal_tangent_stride;
				} else {
					v.attribs[i].offset = position_stride * s->vertex_count;
				}
				v.attribs[i].type = (mis ? GL_FLOAT : GL_UNSIGNED_SHORT);
				v.attribs[i].normalized = GL_TRUE;
			} break;
			case RS::ARRAY_TANGENT: {
				v.attribs[i].enabled = false;
				v.attribs[i].integer = false;
			} break;
			case RS::ARRAY_COLOR: {
				v.attribs[i].offset = attributes_stride;
				v.attribs[i].size = 4;
				v.attribs[i].type = GL_UNSIGNED_BYTE;
				attributes_stride += 4;
				v.attribs[i].normalized = GL_TRUE;
			} break;
			case RS::ARRAY_TEX_UV: {
				v.attribs[i].offset = attributes_stride;
				v.attribs[i].size = 2;
				if (s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
					v.attribs[i].type = GL_UNSIGNED_SHORT;
					attributes_stride += 2 * sizeof(uint16_t);
					v.attribs[i].normalized = GL_TRUE;
				} else {
					v.attribs[i].type = GL_FLOAT;
					attributes_stride += 2 * sizeof(float);
					v.attribs[i].normalized = GL_FALSE;
				}
			} break;
			case RS::ARRAY_TEX_UV2: {
				v.attribs[i].offset = attributes_stride;
				v.attribs[i].size = 2;
				if (s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
					v.attribs[i].type = GL_UNSIGNED_SHORT;
					attributes_stride += 2 * sizeof(uint16_t);
					v.attribs[i].normalized = GL_TRUE;
				} else {
					v.attribs[i].type = GL_FLOAT;
					attributes_stride += 2 * sizeof(float);
					v.attribs[i].normalized = GL_FALSE;
				}
			} break;
			case RS::ARRAY_CUSTOM2:
			case RS::ARRAY_CUSTOM3: {
				v.attribs[i].offset = attributes_stride;
				int idx = i - RS::ARRAY_CUSTOM0;
				uint32_t fmt_shift[RS::ARRAY_CUSTOM_COUNT] = { RS::ARRAY_FORMAT_CUSTOM0_SHIFT, RS::ARRAY_FORMAT_CUSTOM1_SHIFT, RS::ARRAY_FORMAT_CUSTOM2_SHIFT, RS::ARRAY_FORMAT_CUSTOM3_SHIFT };
				uint32_t fmt = (s->format >> fmt_shift[idx]) & RS::ARRAY_FORMAT_CUSTOM_MASK;
				uint32_t fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 4, 8, 4, 8, 12, 16 };
				GLenum gl_type[RS::ARRAY_CUSTOM_MAX] = { GL_UNSIGNED_BYTE, GL_BYTE, GL_HALF_FLOAT_OES, GL_HALF_FLOAT_OES, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT };
				GLboolean norm[RS::ARRAY_CUSTOM_MAX] = { GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };
				v.attribs[i].type = gl_type[fmt];
				attributes_stride += fmtsize[fmt];
				v.attribs[i].size = fmtsize[fmt] / sizeof(float);
				v.attribs[i].normalized = norm[fmt];
			} break;
			case RS::ARRAY_CUSTOM0:
			case RS::ARRAY_BONES: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].size = 4;
				v.attribs[i].type = GL_UNSIGNED_SHORT;
				skin_stride += 4 * sizeof(uint16_t);
				v.attribs[i].normalized = GL_FALSE; // Interpreted directly as float index by our shader
				v.attribs[i].integer = false; // Prevents the system from expecting glVertexAttribIPointer
			} break;
			case RS::ARRAY_CUSTOM1:
			case RS::ARRAY_WEIGHTS: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].size = 4;
				v.attribs[i].type = GL_UNSIGNED_SHORT;
				skin_stride += 4 * sizeof(uint16_t);
				v.attribs[i].normalized = GL_TRUE;
			} break;
		}
	}

	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		if (!v.attribs[i].enabled) {
			continue;
		}
		if (i <= RS::ARRAY_TANGENT) {
			v.attribs[i].stride = (i == RS::ARRAY_VERTEX) ? position_stride : normal_tangent_stride;
		} else if (i >= RS::ARRAY_CUSTOM0 && i <= RS::ARRAY_CUSTOM1) {
			v.attribs[i].stride = skin_stride;
		} else {
			v.attribs[i].stride = attributes_stride;
		}
	}

	v.input_mask = p_input_mask;
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
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	if (mi->skeleton == p_skeleton) {
		return;
	}
	mi->skeleton = p_skeleton;
	mi->skeleton_version = 0;
	mi->dirty = true;
}

void MeshStorage::mesh_instance_set_blend_shape_weight(RID p_mesh_instance, int p_shape, float p_weight) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);
	ERR_FAIL_INDEX(p_shape, (int)mi->blend_weights.size());
	mi->blend_weights[p_shape] = p_weight;
	mi->dirty = true;
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
	if (mesh->blend_shape_count > 0) {
		mi->blend_weights.resize(mesh->blend_shape_count);
		for (uint32_t i = 0; i < mi->blend_weights.size(); i++) {
			mi->blend_weights[i] = 0.0;
		}
	}

	MeshInstance::Surface s;
	if ((mesh->blend_shape_count > 0 || (mesh->surfaces[p_surface]->format & RS::ARRAY_FORMAT_BONES)) && mesh->surfaces[p_surface]->vertex_buffer_size > 0) {
		// Cache surface properties
		s.format_cache = mesh->surfaces[p_surface]->format;
		if ((s.format_cache & (1ULL << RS::ARRAY_VERTEX))) {
			if (s.format_cache & RS::ARRAY_FLAG_USE_2D_VERTICES) {
				s.vertex_size_cache = 2;
			} else {
				s.vertex_size_cache = 3;
			}
			s.vertex_stride_cache = sizeof(float) * s.vertex_size_cache;
		}
		if ((s.format_cache & (1ULL << RS::ARRAY_NORMAL))) {
			s.vertex_normal_offset_cache = s.vertex_stride_cache;
			s.vertex_stride_cache += sizeof(uint32_t) * 2;
		}
		if ((s.format_cache & (1ULL << RS::ARRAY_TANGENT))) {
			s.vertex_tangent_offset_cache = s.vertex_stride_cache;
			s.vertex_stride_cache += sizeof(uint32_t) * 2;
		}

		int buffer_size = s.vertex_stride_cache * mesh->surfaces[p_surface]->vertex_count;

		// Buffer to be used for rendering. Final output of skeleton and blend shapes.
		glGenBuffers(1, &s.vertex_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, s.vertex_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s.vertex_buffer, buffer_size, nullptr, GL_DYNAMIC_DRAW, "MeshInstance vertex buffer");
		if (mesh->blend_shape_count > 0) {
			// Ping-Pong buffers for processing blendshapes.
			glGenBuffers(2, s.vertex_buffers);
			for (uint32_t i = 0; i < 2; i++) {
				glBindBuffer(GL_ARRAY_BUFFER, s.vertex_buffers[i]);
				GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s.vertex_buffers[i], buffer_size, nullptr, GL_DYNAMIC_DRAW, "MeshInstance process buffer[" + itos(i) + "]");
			}
		}
		glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind
	}

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
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);

	bool needs_update = mi->dirty;

	if (mi->array_update_list.in_list()) {
		return;
	}

	if (!needs_update && mi->skeleton.is_valid()) {
		Skeleton *sk = skeleton_owner.get_or_null(mi->skeleton);
		if (sk && sk->version != mi->skeleton_version) {
			needs_update = true;
		}
	}

	if (needs_update) {
		dirty_mesh_instance_arrays.add(&mi->array_update_list);
	}
}

void MeshStorage::mesh_instance_set_canvas_item_transform(RID p_mesh_instance, const Transform2D &p_transform) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);

	mi->canvas_item_transform_2d = p_transform;
}

void MeshStorage::_blend_shape_bind_mesh_instance_buffer(MeshInstance *p_mi, uint32_t p_surface) {
	ERR_FAIL_NULL(p_mi);
	glBindBuffer(GL_ARRAY_BUFFER, p_mi->surfaces[p_surface].vertex_buffers[0]);

	if ((p_mi->surfaces[p_surface].format_cache & (1ULL << RS::ARRAY_VERTEX))) {
		glEnableVertexAttribArray(RS::ARRAY_VERTEX);
		glVertexAttribPointer(RS::ARRAY_VERTEX, p_mi->surfaces[p_surface].vertex_size_cache, GL_FLOAT, GL_FALSE, p_mi->surfaces[p_surface].vertex_stride_cache, (const void *)(uintptr_t)(0));
	} else {
		glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	}
	
	if ((p_mi->surfaces[p_surface].format_cache & (1ULL << RS::ARRAY_NORMAL))) {
		glEnableVertexAttribArray(RS::ARRAY_NORMAL);
		// GLES2 doesn't support glVertexAttribIPointer.
		glVertexAttribPointer(RS::ARRAY_NORMAL, 4, GL_FLOAT, GL_FALSE, p_mi->surfaces[p_surface].vertex_stride_cache, (const void *)(uintptr_t)(p_mi->surfaces[p_surface].vertex_normal_offset_cache));
	} else {
		glDisableVertexAttribArray(RS::ARRAY_NORMAL);
	}
	
	if ((p_mi->surfaces[p_surface].format_cache & (1ULL << RS::ARRAY_TANGENT))) {
		glEnableVertexAttribArray(RS::ARRAY_TANGENT);
		glVertexAttribPointer(RS::ARRAY_TANGENT, 4, GL_FLOAT, GL_FALSE, p_mi->surfaces[p_surface].vertex_stride_cache, (const void *)(uintptr_t)(p_mi->surfaces[p_surface].vertex_tangent_offset_cache));
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TANGENT);
	}
	
	GL_CHECK_ERROR("GLES2::MeshStorage::_blend_shape_bind_mesh_instance_buffer");
}

void MeshStorage::_compute_skeleton(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface) {
	ERR_FAIL_NULL(p_mi);
	ERR_FAIL_NULL(p_sk);

	// CPU-side Software Skinning
	Mesh::Surface *s = p_mi->mesh->surfaces[p_surface];
	Vector<uint8_t> src_vertices = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->vertex_buffer, s->vertex_buffer_size);
	Vector<uint8_t> src_skin = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->skin_buffer, s->skin_buffer_size);

	if (src_vertices.is_empty() || src_skin.is_empty()) {
		return;
	}

	bool use_8_weights = p_mi->surfaces[p_surface].format_cache & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;

	int vertex_count = s->vertex_count;
	int dst_vertex_stride = p_mi->surfaces[p_surface].vertex_stride_cache;
	int src_vertex_stride = s->vertex_buffer_size / vertex_count;
	int skin_stride = s->skin_buffer_size / vertex_count;

	Vector<uint8_t> dst_vertices;
	dst_vertices.resize(vertex_count * dst_vertex_stride);
	
	uint8_t *v_w = dst_vertices.ptrw();
	const uint8_t *v_r = src_vertices.ptr();
	const uint8_t *s_r = src_skin.ptr();
	const float *bone_data = p_sk->data.ptr();

	Transform2D inv_canvas_xform = p_mi->canvas_item_transform_2d.affine_inverse();
	Transform2D base_sk_xform = p_sk->base_transform_2d;
	Transform2D skeleton_matrix = inv_canvas_xform * base_sk_xform;
	Transform2D inverse_matrix = skeleton_matrix.affine_inverse();

	for (int i = 0; i < vertex_count; i++) {
		const uint8_t *p32 = v_r + i * src_vertex_stride;

		float pos_x = unaligned_read<float>(p32 + 0);
		float pos_y = unaligned_read<float>(p32 + 4);

		Vector2 src_pos(pos_x, pos_y);

		const uint8_t *bones_ptr = s_r + i * skin_stride;
		const uint8_t *weights_ptr = s_r + i * skin_stride + (use_8_weights ? 8 : 4) * sizeof(uint16_t);

		Vector2 dst_pos;
		float total_weight = 0.0f;
		int weight_count = use_8_weights ? 8 : 4;

		for (int k = 0; k < weight_count; k++) {
			uint16_t weight_val = unaligned_read<uint16_t>(weights_ptr + k * sizeof(uint16_t));
			float w = weight_val / 65535.0f;
			if (w == 0.0f) {
				continue;
			}
			total_weight += w;

			uint16_t bone_idx = unaligned_read<uint16_t>(bones_ptr + k * sizeof(uint16_t));
			if (bone_idx >= p_sk->size) {
				continue;
			}

			Transform2D b_xform;
			const float *b_ptr = bone_data + bone_idx * (p_sk->use_2d ? 8 : 12);

			if (p_sk->use_2d) {
				b_xform.columns[0][0] = b_ptr[0];
				b_xform.columns[1][0] = b_ptr[1];
				b_xform.columns[2][0] = b_ptr[3];
				b_xform.columns[0][1] = b_ptr[4];
				b_xform.columns[1][1] = b_ptr[5];
				b_xform.columns[2][1] = b_ptr[7];
			} else {
				b_xform.columns[0][0] = b_ptr[0];
				b_xform.columns[1][0] = b_ptr[1];
				b_xform.columns[2][0] = b_ptr[3];
				b_xform.columns[0][1] = b_ptr[4];
				b_xform.columns[1][1] = b_ptr[5];
				b_xform.columns[2][1] = b_ptr[7];
			}

			Transform2D bone_matrix = skeleton_matrix * b_xform * inverse_matrix;
			dst_pos += bone_matrix.xform(src_pos) * w;
		}

		if (total_weight < 0.01f) {
			dst_pos = src_pos;
		} else {
			dst_pos /= total_weight;
		}

		uint8_t *w32 = v_w + i * dst_vertex_stride;

		unaligned_write<float>(w32 + 0, dst_pos.x);
		unaligned_write<float>(w32 + 4, dst_pos.y);

		// Maintain remaining vertex stride block (colors, custom attributes, etc)
		if (src_vertex_stride > 8 && dst_vertex_stride >= src_vertex_stride) {
			memcpy(w32 + 8, p32 + 8, src_vertex_stride - 8);
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, p_mi->surfaces[p_surface].vertex_buffer);
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		p_mi->surfaces[p_surface].vertex_buffer,
		0,
		dst_vertices.size(),
		dst_vertices.ptr()
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::_compute_skeleton: buffer_update_data");
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::MeshStorage::_compute_skeleton: CPU Fallback: glBindBuffer");
	return;
}

void MeshStorage::update_mesh_instances() {
	if (dirty_mesh_instance_arrays.first() == nullptr) {
		return;
	}

	while (dirty_mesh_instance_arrays.first()) {
		MeshInstance *mi = dirty_mesh_instance_arrays.first()->self();
		Skeleton *sk = skeleton_owner.get_or_null(mi->skeleton);

		float base_weight = 1.0;
		if (mi->surfaces.size() && mi->mesh->blend_shape_count && mi->mesh->blend_shape_mode == RS::BLEND_SHAPE_MODE_NORMALIZED) {
			for (uint32_t i = 0; i < mi->mesh->blend_shape_count; i++) {
				base_weight -= mi->blend_weights[i];
			}
		}

		for (uint32_t i = 0; i < mi->surfaces.size(); i++) {
			if (mi->surfaces[i].vertex_buffer == 0) {
				continue;
			}

			bool array_is_2d = mi->surfaces[i].format_cache & RS::ARRAY_FLAG_USE_2D_VERTICES;
			bool can_use_skeleton = sk != nullptr && sk->use_2d == array_is_2d && (mi->surfaces[i].format_cache & RS::ARRAY_FORMAT_BONES);
			bool use_8_weights = mi->surfaces[i].format_cache & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;

			if (mi->mesh->blend_shape_count) {
				SkeletonShaderGLES2::ShaderVariant variant = SkeletonShaderGLES2::MODE_BASE_PASS;
				uint64_t specialization = 0;
				specialization |= array_is_2d ? SkeletonShaderGLES2::MODE_2D : 0;
				specialization |= SkeletonShaderGLES2::USE_BLEND_SHAPES;

				if (!array_is_2d) {
					if ((mi->surfaces[i].format_cache & (1ULL << RS::ARRAY_NORMAL))) {
						specialization |= SkeletonShaderGLES2::USE_NORMAL;
					}
					if ((mi->surfaces[i].format_cache & (1ULL << RS::ARRAY_TANGENT))) {
						specialization |= SkeletonShaderGLES2::USE_TANGENT;
					}
				}

				bool success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
				if (!success) {
					continue;
				}

				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::BLEND_WEIGHT, base_weight, skeleton_shader.shader_version, variant, specialization);
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::BLEND_SHAPE_COUNT, float(mi->mesh->blend_shape_count), skeleton_shader.shader_version, variant, specialization);

				glBindBuffer(GL_ARRAY_BUFFER, 0);
				GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindBuffer(GL_ARRAY_BUFFER, 0) [Base Pass]");

				GLuint vertex_array_gl = 0;
				uint64_t mask = RS::ARRAY_FORMAT_VERTEX | RS::ARRAY_FORMAT_NORMAL | RS::ARRAY_FORMAT_VERTEX;
				uint64_t format = mi->mesh->surfaces[i]->format & mask;
				mesh_surface_get_vertex_arrays_and_format(mi->mesh->surfaces[i], format, vertex_array_gl);

				glBindVertexArray(vertex_array_gl);
				GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindVertexArray [Base Pass]");

				variant = SkeletonShaderGLES2::MODE_BLEND_PASS;
				success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
				if (!success) {
					continue;
				}

				for (uint32_t bs = 0; bs < mi->mesh->blend_shape_count - 1; bs++) {
					float weight = mi->blend_weights[bs];

					if (Math::is_zero_approx(weight)) {
						continue;
					}

					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::BLEND_WEIGHT, weight, skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::BLEND_SHAPE_COUNT, float(mi->mesh->blend_shape_count), skeleton_shader.shader_version, variant, specialization);

					glBindVertexArray(mi->mesh->surfaces[i]->blend_shapes[bs].vertex_array);
					GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindVertexArray [Mid Blend Pass]");

					_blend_shape_bind_mesh_instance_buffer(mi, i);

					SWAP(mi->surfaces[i].vertex_buffers[0], mi->surfaces[i].vertex_buffers[1]);
				}

				uint32_t bs = mi->mesh->blend_shape_count - 1;
				float weight = mi->blend_weights[bs];

				glBindVertexArray(mi->mesh->surfaces[i]->blend_shapes[bs].vertex_array);
				GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindVertexArray [Final Blend Pass]");

				_blend_shape_bind_mesh_instance_buffer(mi, i);

				specialization |= can_use_skeleton ? SkeletonShaderGLES2::USE_SKELETON : 0;
				specialization |= (can_use_skeleton && use_8_weights) ? SkeletonShaderGLES2::USE_EIGHT_WEIGHTS : 0;
				specialization |= SkeletonShaderGLES2::FINAL_PASS;

				success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
				if (!success) {
					continue;
				}

				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::BLEND_WEIGHT, weight, skeleton_shader.shader_version, variant, specialization);
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::BLEND_SHAPE_COUNT, float(mi->mesh->blend_shape_count), skeleton_shader.shader_version, variant, specialization);

				if (can_use_skeleton) {
					Transform2D transform = mi->canvas_item_transform_2d.affine_inverse() * sk->base_transform_2d;
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::SKELETON_TRANSFORM_X, transform[0], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::SKELETON_TRANSFORM_Y, transform[1], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::SKELETON_TRANSFORM_OFFSET, transform[2], skeleton_shader.shader_version, variant, specialization);

					Transform2D inverse_transform = transform.affine_inverse();
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::INVERSE_TRANSFORM_X, inverse_transform[0], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::INVERSE_TRANSFORM_Y, inverse_transform[1], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::INVERSE_TRANSFORM_OFFSET, inverse_transform[2], skeleton_shader.shader_version, variant, specialization);

					// Will seamlessly process CPU skinning if TF is unsupported
					_compute_skeleton(mi, sk, i);
					can_use_skeleton = false;
				}

				glBindVertexArray(RS::ARRAY_VERTEX);
				GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindVertexArray(RS::ARRAY_VERTEX) [Post Blend]");
			}

			if (can_use_skeleton) {
				SkeletonShaderGLES2::ShaderVariant variant = SkeletonShaderGLES2::MODE_BASE_PASS;
				uint64_t specialization = 0;
				specialization |= array_is_2d ? SkeletonShaderGLES2::MODE_2D : 0;
				specialization |= SkeletonShaderGLES2::USE_SKELETON;
				specialization |= SkeletonShaderGLES2::FINAL_PASS;
				specialization |= use_8_weights ? SkeletonShaderGLES2::USE_EIGHT_WEIGHTS : 0;

				if (!array_is_2d) {
					if ((mi->surfaces[i].format_cache & (1ULL << RS::ARRAY_NORMAL))) {
						specialization |= SkeletonShaderGLES2::USE_NORMAL;
					}
					if ((mi->surfaces[i].format_cache & (1ULL << RS::ARRAY_TANGENT))) {
						specialization |= SkeletonShaderGLES2::USE_TANGENT;
					}
				}

				bool success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
				if (!success) {
					continue;
				}

				Transform2D transform = mi->canvas_item_transform_2d.affine_inverse() * sk->base_transform_2d;
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::SKELETON_TRANSFORM_X, transform[0], skeleton_shader.shader_version, variant, specialization);
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::SKELETON_TRANSFORM_Y, transform[1], skeleton_shader.shader_version, variant, specialization);
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::SKELETON_TRANSFORM_OFFSET, transform[2], skeleton_shader.shader_version, variant, specialization);

				Transform2D inverse_transform = transform.affine_inverse();
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::INVERSE_TRANSFORM_X, inverse_transform[0], skeleton_shader.shader_version, variant, specialization);
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::INVERSE_TRANSFORM_Y, inverse_transform[1], skeleton_shader.shader_version, variant, specialization);
				skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES2::INVERSE_TRANSFORM_OFFSET, inverse_transform[2], skeleton_shader.shader_version, variant, specialization);

				GLuint vertex_array_gl = 0;
				uint64_t mask = RS::ARRAY_FORMAT_VERTEX | RS::ARRAY_FORMAT_NORMAL | RS::ARRAY_FORMAT_VERTEX;
				uint64_t format = mi->mesh->surfaces[i]->format & mask;
				mesh_surface_get_vertex_arrays_and_format(mi->mesh->surfaces[i], format, vertex_array_gl);

				glBindVertexArray(vertex_array_gl);
				GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindVertexArray [Skeleton Pass]");

				_compute_skeleton(mi, sk, i);
			}
		}

		mi->dirty = false;
		if (sk) {
			mi->skeleton_version = sk->version;
		}
		dirty_mesh_instance_arrays.remove(&mi->array_update_list);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glBindBuffer final");
}

/* MULTIMESH API */

RID MeshStorage::_multimesh_allocate() {
	return multimesh_owner.allocate_rid();
}

void MeshStorage::_multimesh_initialize(RID p_rid) {
	multimesh_owner.initialize_rid(p_rid, MultiMesh());
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(multimesh);

	// Create a backing GL buffer just in case it's used for 3D later.
	glGenBuffers(1, &multimesh->buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::_multimesh_initialize: glGenBuffers");

	GLint prev_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::_multimesh_initialize: glGetIntegerv GL_ARRAY_BUFFER_BINDING");

	// Pre-register empty size to tracking cache to satisfy strict un-allocations
	glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
	GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, multimesh->buffer, 0, nullptr, GL_DYNAMIC_DRAW, "MultiMesh buffer");
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::_multimesh_initialize: buffer_allocate_data and state restore");
}

void MeshStorage::_multimesh_free(RID p_rid) {
	MultiMesh *multimesh = multimesh_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(multimesh);

	_interpolation_data.notify_free_multimesh(p_rid);
	_update_dirty_multimeshes();
	multimesh_allocate_data(p_rid, 0, RS::MULTIMESH_TRANSFORM_2D);

	if (multimesh) {
		if (multimesh->buffer != 0) {
			GLES2::Utilities::get_singleton()->buffer_free_data(multimesh->buffer);
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
	glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
	GLES2::Utilities::get_singleton()->buffer_resize_data(GL_ARRAY_BUFFER, multimesh->buffer, data_size * sizeof(float), nullptr, GL_DYNAMIC_DRAW, "MultiMesh buffer");
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::MeshStorage::_multimesh_allocate_data: buffer_resize_data");
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

	_multimesh_mark_dirty(multimesh, p_index, true);
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

	_multimesh_mark_dirty(multimesh, p_index, true);
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

	_multimesh_mark_dirty(multimesh, p_index, true);
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
	if (multimesh->buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
		GLES2::Utilities::get_singleton()->buffer_update_data(
			GL_ARRAY_BUFFER,
			multimesh->buffer,
			0,
			p_buffer.size() * sizeof(float),
			p_buffer.ptr()
		);
		GL_CHECK_ERROR("GLES2::MeshStorage::_multimesh_set_buffer: buffer_update_data");
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	multimesh->buffer_set = true;

	// Generate the AABB.
	if (multimesh->mesh.is_valid() && multimesh->custom_aabb == AABB()) {
		const float *data = p_buffer.ptr();
		_multimesh_re_create_aabb(multimesh, data, multimesh->instances);
		multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
	}
}

RID MeshStorage::_multimesh_get_command_buffer_rd_rid(RID p_multimesh) const {
	ERR_FAIL_V_MSG(RID(), "GLES2 does not implement indirect multimeshes.");
}

RID MeshStorage::_multimesh_get_buffer_rd_rid(RID p_multimesh) const {
	ERR_FAIL_V_MSG(RID(), "GLES2 does not contain a Rid for the multimesh buffer.");
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
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(skeleton);
	ERR_FAIL_COND(p_bones < 0);

	if (skeleton->size == p_bones && skeleton->use_2d == p_2d_skeleton) {
		return;
	}

	skeleton->size = p_bones;
	skeleton->use_2d = p_2d_skeleton;
	skeleton->height = (p_bones * (p_2d_skeleton ? 2 : 3)) / 256;
	if ((p_bones * (p_2d_skeleton ? 2 : 3)) % 256) {
		skeleton->height++;
	}

	if (skeleton->transforms_texture != 0) {
		GLES2::Utilities::get_singleton()->texture_free_data(skeleton->transforms_texture);
		skeleton->transforms_texture = 0;
		skeleton->data.clear();
	}

	if (skeleton->size) {
		skeleton->data.resize(256 * skeleton->height * 4);

		GLint current_texture = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

		glGenTextures(1, &skeleton->transforms_texture);
		glBindTexture(GL_TEXTURE_2D, skeleton->transforms_texture);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, skeleton->height, 0, GL_RGBA, GL_FLOAT, nullptr);
		GL_CHECK_ERROR("GLES2::MeshStorage::skeleton_allocate_data: glTexImage2D");

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GL_CHECK_ERROR("GLES2::MeshStorage::skeleton_allocate_data: glTexParameteri");

		// Restore state
		glBindTexture(GL_TEXTURE_2D, current_texture);

		GLES2::Utilities::get_singleton()->texture_allocated_data(skeleton->transforms_texture, skeleton->data.size() * sizeof(float), "Skeleton transforms texture");

		memset(skeleton->data.ptr(), 0, skeleton->data.size() * sizeof(float));

		_skeleton_make_dirty(skeleton);
	}

	skeleton->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_SKELETON_DATA);
}

void MeshStorage::skeleton_set_base_transform_2d(RID p_skeleton, const Transform2D &p_base_transform) {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);

	ERR_FAIL_NULL(skeleton);
	ERR_FAIL_COND(!skeleton->use_2d);

	skeleton->base_transform_2d = p_base_transform;
}

int MeshStorage::skeleton_get_bone_count(RID p_skeleton) const {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL_V(skeleton, 0);

	return skeleton->size;
}

void MeshStorage::skeleton_bone_set_transform(RID p_skeleton, int p_bone, const Transform3D &p_transform) {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);

	ERR_FAIL_NULL(skeleton);
	ERR_FAIL_INDEX(p_bone, skeleton->size);
	ERR_FAIL_COND(skeleton->use_2d);

	float *dataptr = skeleton->data.ptr() + p_bone * 12;
	ERR_FAIL_NULL(dataptr);

	dataptr[0] = p_transform.basis.rows[0][0];
	dataptr[1] = p_transform.basis.rows[0][1];
	dataptr[2] = p_transform.basis.rows[0][2];
	dataptr[3] = p_transform.origin.x;
	dataptr[4] = p_transform.basis.rows[1][0];
	dataptr[5] = p_transform.basis.rows[1][1];
	dataptr[6] = p_transform.basis.rows[1][2];
	dataptr[7] = p_transform.origin.y;
	dataptr[8] = p_transform.basis.rows[2][0];
	dataptr[9] = p_transform.basis.rows[2][1];
	dataptr[10] = p_transform.basis.rows[2][2];
	dataptr[11] = p_transform.origin.z;

	_skeleton_make_dirty(skeleton);
}

Transform3D MeshStorage::skeleton_bone_get_transform(RID p_skeleton, int p_bone) const {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);

	ERR_FAIL_NULL_V(skeleton, Transform3D());
	ERR_FAIL_INDEX_V(p_bone, skeleton->size, Transform3D());
	ERR_FAIL_COND_V(skeleton->use_2d, Transform3D());

	const float *dataptr = skeleton->data.ptr() + p_bone * 12;
	ERR_FAIL_NULL_V(dataptr, Transform3D());

	Transform3D t;

	t.basis.rows[0][0] = dataptr[0];
	t.basis.rows[0][1] = dataptr[1];
	t.basis.rows[0][2] = dataptr[2];
	t.origin.x = dataptr[3];
	t.basis.rows[1][0] = dataptr[4];
	t.basis.rows[1][1] = dataptr[5];
	t.basis.rows[1][2] = dataptr[6];
	t.origin.y = dataptr[7];
	t.basis.rows[2][0] = dataptr[8];
	t.basis.rows[2][1] = dataptr[9];
	t.basis.rows[2][2] = dataptr[10];
	t.origin.z = dataptr[11];

	return t;
}

void MeshStorage::skeleton_bone_set_transform_2d(RID p_skeleton, int p_bone, const Transform2D &p_transform) {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);

	ERR_FAIL_NULL(skeleton);
	ERR_FAIL_INDEX(p_bone, skeleton->size);
	ERR_FAIL_COND(!skeleton->use_2d);

	float *dataptr = skeleton->data.ptr() + p_bone * 8;
	ERR_FAIL_NULL(dataptr);

	dataptr[0] = p_transform.columns[0][0];
	dataptr[1] = p_transform.columns[1][0];
	dataptr[2] = 0;
	dataptr[3] = p_transform.columns[2][0];
	dataptr[4] = p_transform.columns[0][1];
	dataptr[5] = p_transform.columns[1][1];
	dataptr[6] = 0;
	dataptr[7] = p_transform.columns[2][1];

	_skeleton_make_dirty(skeleton);
}

Transform2D MeshStorage::skeleton_bone_get_transform_2d(RID p_skeleton, int p_bone) const {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);

	ERR_FAIL_NULL_V(skeleton, Transform2D());
	ERR_FAIL_INDEX_V(p_bone, skeleton->size, Transform2D());
	ERR_FAIL_COND_V(!skeleton->use_2d, Transform2D());

	const float *dataptr = skeleton->data.ptr() + p_bone * 8;
	ERR_FAIL_NULL_V(dataptr, Transform2D());

	Transform2D t;
	t.columns[0][0] = dataptr[0];
	t.columns[1][0] = dataptr[1];
	t.columns[2][0] = dataptr[3];
	t.columns[0][1] = dataptr[4];
	t.columns[1][1] = dataptr[5];
	t.columns[2][1] = dataptr[7];

	return t;
}

void MeshStorage::_update_dirty_skeletons() {
	if (!skeleton_dirty_list) {
		return;
	}

	// Capture
	GLint current_texture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

	while (skeleton_dirty_list) {
		Skeleton *skeleton = skeleton_dirty_list;

		if (skeleton->size) {
			glBindTexture(GL_TEXTURE_2D, skeleton->transforms_texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, skeleton->height, 0, GL_RGBA, GL_FLOAT, skeleton->data.ptr());
			GL_CHECK_ERROR("GLES2::MeshStorage::_update_dirty_skeletons: glTexImage2D");
		}

		skeleton_dirty_list = skeleton->dirty_list;
		skeleton->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_SKELETON_BONES);
		skeleton->version++;
		skeleton->dirty = false;
		skeleton->dirty_list = nullptr;
	}

	// Restore
	glBindTexture(GL_TEXTURE_2D, current_texture);

	skeleton_dirty_list = nullptr;
}

void MeshStorage::skeleton_update_dependency(RID p_skeleton, DependencyTracker *p_instance) {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(skeleton);

	p_instance->update_dependency(&skeleton->dependency);
}

#endif // GLES2_ENABLED
