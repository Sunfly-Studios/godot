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

/* STATIC */

static _FORCE_INLINE_ Vector3 _gles2_decode_octahedral_normal(uint16_t p_x, uint16_t p_y) {
	// Decode 16-bit UNORM components into the [-1.0, 1.0] range
	float x = (p_x / 65535.0f) * 2.0f - 1.0f;
	float y = (p_y / 65535.0f) * 2.0f - 1.0f;
	float z = 1.0f - ABS(x) - ABS(y);

	Vector3 normal(x, y, z);
	float t = CLAMP(-z, 0.0f, 1.0f);
	normal.x += normal.x >= 0.0f ? -t : t;
	normal.y += normal.y >= 0.0f ? -t : t;

	return normal.normalized();
}

static _FORCE_INLINE_ void _gles2_decode_octahedral_tangent(uint16_t p_x, uint16_t p_y, Vector3 &r_tangent, float &r_binormal_sign) {
	// Decode X from standard 16-bit UNORM (65535) into [-1.0, 1.0]
	float x = (p_x / 65535.0f) * 2.0f - 1.0f;
	float y = (p_y / 65535.0f) * 2.0f - 1.0f;

	// Godot 4 encodes binormal sign = -1.0f by
	// mapping oct.y into [0.5, 1.0] (or the 0xFFFF sentinel)
	r_binormal_sign = 1.0f;
	if (x == -1.0f && y == 1.0f) {
		y = -1.0f;
		r_binormal_sign = -1.0f;
	} else if (y >= 0.0f) {
		y = y * 2.0f - 1.0f;
		r_binormal_sign = -1.0f;
	} else {
		y = y * 2.0f + 1.0f;
		r_binormal_sign = 1.0f;
	}

	float z = 1.0f - ABS(x) - ABS(y);

	r_tangent = Vector3(x, y, z);
	float t = CLAMP(-z, 0.0f, 1.0f);
	r_tangent.x += r_tangent.x >= 0.0f ? -t : t;
	r_tangent.y += r_tangent.y >= 0.0f ? -t : t;

	r_tangent.normalize();
}

static _FORCE_INLINE_ float _gles2_decode_uv_unorm16(uint16_t p_v, float p_scale) {
	const float u = p_v / 65535.0f;
	// Nonzero scale: centered/scaled encoding.
	// Zero scale: plain UNORM16 in [0,1].
	return (p_scale != 0.0f) ? (u - 0.5f) * p_scale : u;
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

	Mesh::Surface *s = memnew(Mesh::Surface);
	ERR_FAIL_NULL(s);

	// Layout Setup
	s->uncompressed_stride = 0;
	if (p_surface.format & RS::ARRAY_FORMAT_VERTEX) {
		s->uncompressed_stride += (p_surface.format & RS::ARRAY_FLAG_USE_2D_VERTICES) ? (sizeof(float) * 2) : (sizeof(float) * 3);
	}
	if (p_surface.format & RS::ARRAY_FORMAT_NORMAL) {
		s->uncompressed_stride += (p_surface.format & RS::ARRAY_FLAG_USE_2D_VERTICES) ? (sizeof(float) * 4) : (sizeof(float) * 3);
	}
	if (!(p_surface.format & RS::ARRAY_FLAG_USE_2D_VERTICES) && (p_surface.format & RS::ARRAY_FORMAT_TANGENT)) {
		s->uncompressed_stride += sizeof(float) * 4;
	}

	if (p_surface.vertex_count > 0 && s->uncompressed_stride > 0) {
		s->uncompressed_buffer.resize(p_surface.vertex_count * s->uncompressed_stride);

		// We must be absolutely certain that data the size is equal
		// to our uncompressed size and that the resize matches.
		ERR_FAIL_COND(s->uncompressed_buffer.size() != p_surface.vertex_count * s->uncompressed_stride);
	}

	// Store the data how it came in
	s->original_format = p_surface.format;
	s->original_vertex_data = p_surface.vertex_data;
	s->original_attribute_data = p_surface.attribute_data;
	s->original_blend_shape_data = p_surface.blend_shape_data;

	RS::SurfaceData surface_data = p_surface;
	_decompress_surface_data(surface_data, s);

	s->format = surface_data.format;
	s->primitive = surface_data.primitive;

	// Protect state bindings
	GLint prev_array_buffer = 0;
	GLint prev_element_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGetIntegerv GL_ARRAY_BUFFER_BINDING");
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_element_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGetIntegerv GL_ELEMENT_ARRAY_BUFFER_BINDING");

	// Vertex data
	if (surface_data.vertex_data.size()) {
		glGenBuffers(1, &s->vertex_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (vertex buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBindBuffer (vertex buffer)");
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s->vertex_buffer, surface_data.vertex_data.size(), surface_data.vertex_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW, "Mesh vertex buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (vertex buffer)");
		s->vertex_buffer_size = surface_data.vertex_data.size();
	}

	// Attribute data (colors, UVs)
	if (surface_data.attribute_data.size()) {
		glGenBuffers(1, &s->attribute_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (attribute buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBindBuffer (attribute buffer)");
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s->attribute_buffer, surface_data.attribute_data.size(), surface_data.attribute_data.ptr(), (s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW, "Mesh attribute buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (attribute buffer)");

		s->attribute_buffer_size = surface_data.attribute_data.size();
	}

	// Index data
	if (surface_data.index_count) {
		glGenBuffers(1, &s->index_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (index buffer)");

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBindBuffer (index buffer)");
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer, surface_data.index_data.size(), surface_data.index_data.ptr(), GL_STATIC_DRAW, "Mesh index buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (index buffer)");

		s->index_count = surface_data.index_count;
		s->index_buffer_size = surface_data.index_data.size();
	}

	// Skin data
	if (surface_data.skin_data.size()) {
		glGenBuffers(1, &s->skin_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glGenBuffers (skin buffer)");

		glBindBuffer(GL_ARRAY_BUFFER, s->skin_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: glBindBuffer (skin buffer)");
		GLES2::Utilities::get_singleton()->buffer_allocate_data(
			GL_ARRAY_BUFFER,
			s->skin_buffer,
			surface_data.skin_data.size(),
			surface_data.skin_data.ptr(),
			(s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW,
			"Mesh skin buffer"
		);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: buffer_allocate_data (skin buffer)");
		s->skin_buffer_size = surface_data.skin_data.size();
	}

	glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: restore prev GL_ARRAY_BUFFER");
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_element_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_add_surface: restore prev GL_ELEMENT_ARRAY_BUFFER");

	// Skeleton / transform properties
	s->bone_aabbs = surface_data.bone_aabbs;
	s->mesh_to_skeleton_xform = surface_data.mesh_to_skeleton_xform;
	s->uv_scale = surface_data.uv_scale;

	if (surface_data.format & RS::ARRAY_FORMAT_BONES) {
		mesh->has_bone_weights = true;
	}

	// Parse format bitfield into OpenGL attributes
	const uint64_t base_input_mask = s->format & ((1ULL << RS::ARRAY_INDEX) - 1ULL);
	s->version_count = 1;
	s->versions = (Mesh::Surface::Version *)memalloc(sizeof(Mesh::Surface::Version));
	ERR_FAIL_NULL(s->versions);

	::new (&s->versions[0]) Mesh::Surface::Version();
	_mesh_surface_generate_version_for_input_mask(s->versions[0], s, base_input_mask);
	s->version_index_map.insert(base_input_mask, 0U);

	s->vertex_count = surface_data.vertex_count;
	s->aabb = surface_data.aabb;

	if (mesh->surface_count == 0) {
		mesh->aabb = surface_data.aabb;
	} else {
		mesh->aabb.merge_with(surface_data.aabb);
	}
	mesh->skeleton_aabb_version = 0;

	s->material = surface_data.material;

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
			if (GLES2_CONFIG->support_vao) {
				for (uint32_t j = 0; j < s->version_count; j++) {
					if (s->versions[j].vertex_array != 0) {
						glDeleteVertexArrays(1, &s->versions[j].vertex_array);
					}
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

	s->uncompressed_buffer.clear();
	s->uncompressed_stride = 0;

	// Clean up VAOs
	if (s->versions) {
		if (GLES2_CONFIG->support_vao) {
			for (uint32_t j = 0; j < s->version_count; j++) {
				if (s->versions[j].vertex_array != 0) {
					glDeleteVertexArrays(1, &s->versions[j].vertex_array);
				}
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

	Mesh::Surface *s = mesh->surfaces[p_surface];
	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > s->vertex_buffer_size);
	const uint8_t *r = p_data.ptr();

	if (p_offset + data_size <= (uint64_t)s->uncompressed_buffer.size()) {
		memcpy(s->uncompressed_buffer.ptrw() + p_offset, r, data_size);
	}

	// Protect active
	GLint prev_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_vertex_region: glGetIntegerv");

	glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		s->vertex_buffer,
		p_offset,
		data_size,
		r
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_vertex_region: buffer_update_data");

	// Restore state
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_vertex_region: glBindBuffer restore");
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
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_attribute_region: glGetIntegerv");

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
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_attribute_region: glBindBuffer restore");
}

void MeshStorage::mesh_surface_update_skin_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);
	ERR_FAIL_COND(p_data.is_empty());

	Mesh::Surface *s = mesh->surfaces[p_surface];
	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > s->skin_buffer_size);
	const uint8_t *r = p_data.ptr();

	if (p_offset + data_size <= (uint64_t)s->skin_data_cache.size()) {
		memcpy(s->skin_data_cache.ptrw() + p_offset, r, data_size);
	}

	// Protect state
	GLint prev_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_skin_region: glGetIntegerv");

	glBindBuffer(GL_ARRAY_BUFFER, s->skin_buffer);
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		s->skin_buffer,
		p_offset,
		data_size,
		r
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_skin_region: buffer_update_data");

	// Restore state
	glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_update_skin_region: glBindBuffer restore");
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
	sd.format = s.original_format;
	sd.vertex_data = s.original_vertex_data;
	sd.attribute_data = s.original_attribute_data;

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
	sd.blend_shape_data = s.original_blend_shape_data;
	sd.uv_scale = s.uv_scale;

	return sd;
}

void MeshStorage::mesh_surface_bind_arrays_gles2(void *p_surface, uint64_t p_input_mask) {
	Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
	ERR_FAIL_NULL(s);

	const uint64_t effective_mask = p_input_mask & ((1ULL << RS::ARRAY_INDEX) - 1ULL);

	s->version_lock.lock();

	Mesh::Surface::Version *version = nullptr;

	if (const uint32_t *found_idx = s->version_index_map.find(effective_mask)) {
		version = &s->versions[*found_idx];
	} else {
		// Generate the version if it doesn't exist yet for this input mask
		// or we haven't cached it.
		uint32_t v_idx = s->version_count;
		s->version_count++;
		s->versions = (Mesh::Surface::Version *)memrealloc(s->versions, sizeof(Mesh::Surface::Version) * s->version_count);
		if (unlikely(!s->versions)) {
			s->version_lock.unlock();
			ERR_FAIL_NULL(s->versions);
		}
		::new (&s->versions[v_idx]) Mesh::Surface::Version();
		_mesh_surface_generate_version_for_input_mask(s->versions[v_idx], s, effective_mask);
		s->version_index_map.insert(effective_mask, v_idx);
		version = &s->versions[v_idx];
	}

	s->version_lock.unlock();

	// Vertex data (positions, normals, tangents)
	if (s->vertex_buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_bind_arrays_gles2: glBindBuffer vertex");
	}

#define GL_OFFSET_PTR_VERTEX(offset) ((const void *)(size_t)(offset))
#define GL_OFFSET_PTR_ATTRIB(offset) ((const void *)(size_t)(offset))

	// Vertex
	if (version->attribs[RS::ARRAY_VERTEX].enabled) {
		glEnableVertexAttribArray(RS::ARRAY_VERTEX);
		glVertexAttribPointer(
			RS::ARRAY_VERTEX,
			version->attribs[RS::ARRAY_VERTEX].size,
			version->attribs[RS::ARRAY_VERTEX].type,
			version->attribs[RS::ARRAY_VERTEX].normalized,
			version->attribs[RS::ARRAY_VERTEX].stride,
			GL_OFFSET_PTR_VERTEX(version->attribs[RS::ARRAY_VERTEX].offset)
		);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_VERTEX);
		glVertexAttrib3f(RS::ARRAY_VERTEX, 0.0f, 0.0f, 0.0f);
	}

	// Normal
	if (version->attribs[RS::ARRAY_NORMAL].enabled) {
		glEnableVertexAttribArray(RS::ARRAY_NORMAL);
		glVertexAttribPointer(
			RS::ARRAY_NORMAL,
			version->attribs[RS::ARRAY_NORMAL].size,
			version->attribs[RS::ARRAY_NORMAL].type,
			version->attribs[RS::ARRAY_NORMAL].normalized,
			version->attribs[RS::ARRAY_NORMAL].stride,
			GL_OFFSET_PTR_VERTEX(version->attribs[RS::ARRAY_NORMAL].offset)
		);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_NORMAL);
		glVertexAttrib3f(RS::ARRAY_NORMAL, 0.0f, 1.0f, 0.0f);
	}

	// Tangent
	if (version->attribs[RS::ARRAY_TANGENT].enabled) {
		glEnableVertexAttribArray(RS::ARRAY_TANGENT);
		glVertexAttribPointer(
			RS::ARRAY_TANGENT,
			version->attribs[RS::ARRAY_TANGENT].size,
			version->attribs[RS::ARRAY_TANGENT].type,
			version->attribs[RS::ARRAY_TANGENT].normalized,
			version->attribs[RS::ARRAY_TANGENT].stride,
			GL_OFFSET_PTR_VERTEX(version->attribs[RS::ARRAY_TANGENT].offset)
		);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TANGENT);
		glVertexAttrib4f(RS::ARRAY_TANGENT, 1.0f, 0.0f, 0.0f, 1.0f);
	}

	// --- Attribute buffer (colors, UVs) ---
	if (s->attribute_buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_bind_arrays_gles2: glBindBuffer attribute");
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	// Color
	if (version->attribs[RS::ARRAY_COLOR].enabled) {
		glEnableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttribPointer(
			RS::ARRAY_COLOR,
			version->attribs[RS::ARRAY_COLOR].size,
			version->attribs[RS::ARRAY_COLOR].type,
			version->attribs[RS::ARRAY_COLOR].normalized,
			version->attribs[RS::ARRAY_COLOR].stride,
			GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_COLOR].offset)
		);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_COLOR);
		glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	// tex_uv
	if (version->attribs[RS::ARRAY_TEX_UV].enabled) {
		glEnableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttribPointer(
			RS::ARRAY_TEX_UV,
			version->attribs[RS::ARRAY_TEX_UV].size,
			version->attribs[RS::ARRAY_TEX_UV].type,
			version->attribs[RS::ARRAY_TEX_UV].normalized,
			version->attribs[RS::ARRAY_TEX_UV].stride,
			GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_TEX_UV].offset)
		);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
		glVertexAttrib2f(RS::ARRAY_TEX_UV, 0.0f, 0.0f);
	}

	// tex_uv2
	if (version->attribs[RS::ARRAY_TEX_UV2].enabled) {
		glEnableVertexAttribArray(RS::ARRAY_TEX_UV2);
		glVertexAttribPointer(
			RS::ARRAY_TEX_UV2,
			version->attribs[RS::ARRAY_TEX_UV2].size,
			version->attribs[RS::ARRAY_TEX_UV2].type,
			version->attribs[RS::ARRAY_TEX_UV2].normalized,
			version->attribs[RS::ARRAY_TEX_UV2].stride,
			GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_TEX_UV2].offset)
		);
	} else {
		glDisableVertexAttribArray(RS::ARRAY_TEX_UV2);
		glVertexAttrib2f(RS::ARRAY_TEX_UV2, 0.0f, 0.0f);
	}

	// Custom Attributes
	for (int i = RS::ARRAY_CUSTOM0; i <= RS::ARRAY_CUSTOM3; i++) {
		if (version->attribs[i].enabled) {
			glEnableVertexAttribArray(i);
			glVertexAttribPointer(
				i,
				version->attribs[i].size,
				version->attribs[i].type,
				version->attribs[i].normalized,
				version->attribs[i].stride,
				GL_OFFSET_PTR_ATTRIB(version->attribs[i].offset)
			);
		} else {
			glDisableVertexAttribArray(i);
			glVertexAttrib4f(i, 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	// Skin buffer (Bones & Weights)
	if (s->skin_buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, s->skin_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_bind_arrays_gles2: glBindBuffer skin");

		if (version->attribs[RS::ARRAY_BONES].enabled) {
			glEnableVertexAttribArray(RS::ARRAY_BONES);
			glVertexAttribPointer(
				RS::ARRAY_BONES,
				version->attribs[RS::ARRAY_BONES].size,
				version->attribs[RS::ARRAY_BONES].type,
				version->attribs[RS::ARRAY_BONES].normalized,
				version->attribs[RS::ARRAY_BONES].stride,
				GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_BONES].offset)
			);
		} else {
			glDisableVertexAttribArray(RS::ARRAY_BONES);
		}

		if (version->attribs[RS::ARRAY_WEIGHTS].enabled) {
			glEnableVertexAttribArray(RS::ARRAY_WEIGHTS);
			glVertexAttribPointer(
				RS::ARRAY_WEIGHTS,
				version->attribs[RS::ARRAY_WEIGHTS].size,
				version->attribs[RS::ARRAY_WEIGHTS].type,
				version->attribs[RS::ARRAY_WEIGHTS].normalized,
				version->attribs[RS::ARRAY_WEIGHTS].stride,
				GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_WEIGHTS].offset)
			);
		} else {
			glDisableVertexAttribArray(RS::ARRAY_WEIGHTS);
			glVertexAttrib4f(RS::ARRAY_WEIGHTS, 0.0f, 0.0f, 0.0f, 0.0f);
		}
	} else {
		glDisableVertexAttribArray(RS::ARRAY_BONES);
		glVertexAttrib4f(RS::ARRAY_BONES, 0.0f, 0.0f, 0.0f, 0.0f);
		glDisableVertexAttribArray(RS::ARRAY_WEIGHTS);
		glVertexAttrib4f(RS::ARRAY_WEIGHTS, 0.0f, 0.0f, 0.0f, 0.0f);
	}

#undef GL_OFFSET_PTR_ATTRIB
#undef GL_OFFSET_PTR_VERTEX

	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_bind_arrays_gles2: pointers bound");
}

void MeshStorage::mesh_instance_surface_bind_arrays_gles2(RID p_mesh_instance, uint32_t p_surface_index, uint64_t p_input_mask) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);
	ERR_FAIL_NULL(mi->mesh);
	ERR_FAIL_UNSIGNED_INDEX(p_surface_index, mi->surfaces.size());
	ERR_FAIL_UNSIGNED_INDEX(p_surface_index, mi->mesh->surface_count);

	MeshInstance::Surface &mis = mi->surfaces[p_surface_index];
	Mesh::Surface *s = mi->mesh->surfaces[p_surface_index];

	// If this specific surface has no instance deformation buffer, fallback to base surface VBO
	if (mis.vertex_buffer == 0) {
		mesh_surface_bind_arrays_gles2(s, p_input_mask);
		return;
	}

	GLuint vertex_array_gl = 0;
	Mesh::Surface::Version *version = mesh_instance_surface_get_vertex_arrays_and_format(p_mesh_instance, p_surface_index, p_input_mask, vertex_array_gl);
	ERR_FAIL_NULL(version);

	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		if (!version->attribs[i].enabled) {
			glDisableVertexAttribArray(i);

			// Provide default values even when the attribute
			// is not enabled.
			switch (i) {
				case RS::ARRAY_VERTEX:
					glVertexAttrib3f(RS::ARRAY_VERTEX, 0.0f, 0.0f, 0.0f);
					break;
				case RS::ARRAY_NORMAL:
					glVertexAttrib3f(RS::ARRAY_NORMAL, 0.0f, 1.0f, 0.0f);
					break;
				case RS::ARRAY_TANGENT:
					glVertexAttrib4f(RS::ARRAY_TANGENT, 1.0f, 0.0f, 0.0f, 1.0f);
					break;
				case RS::ARRAY_COLOR:
					glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
					break;
				case RS::ARRAY_TEX_UV:
					glVertexAttrib2f(RS::ARRAY_TEX_UV, 0.0f, 0.0f);
					break;
				case RS::ARRAY_TEX_UV2:
					glVertexAttrib2f(RS::ARRAY_TEX_UV2, 0.0f, 0.0f);
					break;
				case RS::ARRAY_BONES:
					glVertexAttrib4f(RS::ARRAY_BONES, 0.0f, 0.0f, 0.0f, 0.0f);
					break;
				case RS::ARRAY_WEIGHTS:
					glVertexAttrib4f(RS::ARRAY_WEIGHTS, 0.0f, 0.0f, 0.0f, 0.0f);
					break;
				case RS::ARRAY_CUSTOM0:
				case RS::ARRAY_CUSTOM1:
				case RS::ARRAY_CUSTOM2:
				case RS::ARRAY_CUSTOM3:
					glVertexAttrib4f(i, 0.0f, 0.0f, 0.0f, 0.0f);
					break;
			}
			continue;
		}
		if (i <= RS::ARRAY_TANGENT) {
			glBindBuffer(GL_ARRAY_BUFFER, mis.vertex_buffer);
		} else if (i >= RS::ARRAY_BONES && i <= RS::ARRAY_WEIGHTS) {
			glBindBuffer(GL_ARRAY_BUFFER, s->skin_buffer);
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
		}
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(
			i,
			version->attribs[i].size,
			version->attribs[i].type,
			version->attribs[i].normalized,
			version->attribs[i].stride,
			(const void *)(uintptr_t)(version->attribs[i].offset)
		);
	}
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_instance_surface_bind_arrays_gles2: pointers bound");
}

void MeshStorage::mesh_surface_unbind_arrays_gles2(void *p_surface) {
	if (GLES2_CONFIG->support_vao) {
		glBindVertexArray(0);
	}
	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		glDisableVertexAttribArray(i);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::MeshStorage::mesh_surface_unbind_arrays_gles2: pointers unbound");
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
	if (s->format & RS::ARRAY_FLAG_USE_2D_VERTICES) {
		_mesh_surface_generate_version_for_input_mask_2d(v, s, p_input_mask, mis);
	} else {
		_mesh_surface_generate_version_for_input_mask_3d(v, s, p_input_mask, mis);
	}
}

void MeshStorage::_mesh_surface_generate_version_for_input_mask_2d(Mesh::Surface::Version &v, Mesh::Surface *s, uint64_t p_input_mask, MeshInstance::Surface *mis) {
	int attributes_stride = 0;
	int skin_stride = 0;
	int interleaved_offset = 0;

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
				v.attribs[i].offset = interleaved_offset;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].size = 2;
				v.attribs[i].stride = s->uncompressed_stride;
				interleaved_offset += 2 * sizeof(float);
			} break;
			case RS::ARRAY_NORMAL: {
				v.attribs[i].offset = interleaved_offset;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].size = 4;
				v.attribs[i].stride = s->uncompressed_stride;
				interleaved_offset += 4 * sizeof(float);
			} break;
			case RS::ARRAY_TANGENT: {
				v.attribs[i].enabled = false;
				v.attribs[i].integer = false;
			} break;
			case RS::ARRAY_COLOR: {
				v.attribs[i].offset = attributes_stride;
				v.attribs[i].size = 4;
				v.attribs[i].type = GL_UNSIGNED_BYTE;
				v.attribs[i].normalized = GL_TRUE;
				attributes_stride += 4;
			} break;
			case RS::ARRAY_TEX_UV:
			case RS::ARRAY_TEX_UV2: {
				v.attribs[i].offset = attributes_stride;
				v.attribs[i].size = 2;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].normalized = GL_FALSE;
				attributes_stride += 2 * sizeof(float);
			} break;
			case RS::ARRAY_CUSTOM0:
			case RS::ARRAY_CUSTOM1:
			case RS::ARRAY_CUSTOM2:
			case RS::ARRAY_CUSTOM3: {
				const uint32_t fmt_shift[RS::ARRAY_CUSTOM_COUNT] = { RS::ARRAY_FORMAT_CUSTOM0_SHIFT, RS::ARRAY_FORMAT_CUSTOM1_SHIFT, RS::ARRAY_FORMAT_CUSTOM2_SHIFT, RS::ARRAY_FORMAT_CUSTOM3_SHIFT };
				const uint32_t dst_fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 8, 16, 4, 8, 12, 16 };
				const GLenum gl_type[RS::ARRAY_CUSTOM_MAX] = { GL_UNSIGNED_BYTE, GL_BYTE, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT };
				const GLboolean norm[RS::ARRAY_CUSTOM_MAX] = { GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };
				const GLint comp_count[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 2, 4, 1, 2, 3, 4 };

				v.attribs[i].offset = attributes_stride;
				int idx = i - RS::ARRAY_CUSTOM0;
				uint32_t fmt = (s->format >> fmt_shift[idx]) & RS::ARRAY_FORMAT_CUSTOM_MASK;

				v.attribs[i].type = gl_type[fmt];
				v.attribs[i].size = comp_count[fmt];
				v.attribs[i].normalized = norm[fmt];
				attributes_stride += dst_fmtsize[fmt];
			} break;
			case RS::ARRAY_BONES:
			case RS::ARRAY_WEIGHTS: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].size = (s->format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS) ? 8 : 4;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].integer = false;
				skin_stride += v.attribs[i].size * sizeof(float);
			} break;
		}
	}

	for (int i = RS::ARRAY_COLOR; i < RS::ARRAY_INDEX; i++) {
		if (!(s->format & (1ULL << i))) {
			continue;
		}
		if (i >= RS::ARRAY_BONES && i <= RS::ARRAY_WEIGHTS) {
			v.attribs[i].stride = skin_stride;
		} else {
			v.attribs[i].stride = attributes_stride;
		}
	}

	v.input_mask = p_input_mask;
}

void MeshStorage::_mesh_surface_generate_version_for_input_mask_3d(Mesh::Surface::Version &v, Mesh::Surface *s, uint64_t p_input_mask, MeshInstance::Surface *mis) {
	int attribute_stride = 0;
	int skin_stride = 0;
	int interleaved_offset = 0;

	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		v.attribs[i].enabled = false;
		v.attribs[i].integer = false;

		if (!(s->format & (1ULL << i))) {
			continue;
		}

		if ((p_input_mask & (1ULL << i))) {
			v.attribs[i].enabled = true;
		}

		switch (i) {
			case RS::ARRAY_VERTEX: {
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].size = 3;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].offset = interleaved_offset;
				v.attribs[i].stride = s->uncompressed_stride;
				interleaved_offset += 3 * sizeof(float);
			} break;
			case RS::ARRAY_NORMAL: {
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].size = 3;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].offset = interleaved_offset;
				v.attribs[i].stride = s->uncompressed_stride; // Always Interleaved
				interleaved_offset += 3 * sizeof(float);
			} break;
			case RS::ARRAY_TANGENT: {
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].size = 4;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].offset = interleaved_offset;
				v.attribs[i].stride = s->uncompressed_stride; // Always Interleaved
				interleaved_offset += 4 * sizeof(float);
			} break;
			case RS::ARRAY_COLOR: {
				v.attribs[i].offset = attribute_stride;
				v.attribs[i].type = GL_UNSIGNED_BYTE;
				v.attribs[i].size = 4;
				v.attribs[i].normalized = GL_TRUE;
				attribute_stride += 4;
			} break;
			case RS::ARRAY_TEX_UV:
			case RS::ARRAY_TEX_UV2: {
				v.attribs[i].offset = attribute_stride;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].size = 2;
				v.attribs[i].normalized = GL_FALSE;
				attribute_stride += 2 * sizeof(float);
			} break;
			case RS::ARRAY_CUSTOM0:
			case RS::ARRAY_CUSTOM1:
			case RS::ARRAY_CUSTOM2:
			case RS::ARRAY_CUSTOM3: {
				const uint32_t fmt_shift[RS::ARRAY_CUSTOM_COUNT] = { RS::ARRAY_FORMAT_CUSTOM0_SHIFT, RS::ARRAY_FORMAT_CUSTOM1_SHIFT, RS::ARRAY_FORMAT_CUSTOM2_SHIFT, RS::ARRAY_FORMAT_CUSTOM3_SHIFT };
				const uint32_t dst_fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 8, 16, 4, 8, 12, 16 };
				const GLenum gl_type[RS::ARRAY_CUSTOM_MAX] = { GL_UNSIGNED_BYTE, GL_BYTE, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT };
				const GLboolean norm[RS::ARRAY_CUSTOM_MAX] = { GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };
				const GLint comp_count[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 2, 4, 1, 2, 3, 4 };

				v.attribs[i].offset = attribute_stride;
				int idx = i - RS::ARRAY_CUSTOM0;
				uint32_t fmt = (s->format >> fmt_shift[idx]) & RS::ARRAY_FORMAT_CUSTOM_MASK;

				v.attribs[i].type = gl_type[fmt];
				v.attribs[i].size = comp_count[fmt];
				v.attribs[i].normalized = norm[fmt];
				attribute_stride += dst_fmtsize[fmt];
			} break;
			case RS::ARRAY_BONES:
			case RS::ARRAY_WEIGHTS: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].size = (s->format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS) ? 8 : 4;
				v.attribs[i].normalized = GL_FALSE;
				skin_stride += v.attribs[i].size * sizeof(float);
			} break;
		}
	}

	for (int i = RS::ARRAY_COLOR; i < RS::ARRAY_INDEX; i++) {
		if (!(s->format & (1ULL << i))) {
			continue;
		}

		if (i >= RS::ARRAY_BONES && i <= RS::ARRAY_WEIGHTS) {
			v.attribs[i].stride = skin_stride;
		} else {
			v.attribs[i].stride = attribute_stride;
		}
	}

	v.input_mask = p_input_mask;
}

void MeshStorage::_decompress_surface_data(RS::SurfaceData &r_surface, Mesh::Surface *s) {
	if (r_surface.vertex_count == 0 || r_surface.vertex_data.is_empty()) {
		return;
	}

	// Dispatcher logic
	if (r_surface.format & RS::ARRAY_FLAG_USE_2D_VERTICES) {
		_decompress_surface_data_2d(r_surface, s);
	} else {
		_decompress_surface_data_3d(r_surface, s);
	}
	_decompress_skin_buffer(r_surface, s);

	r_surface.format &= ~RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
}

void MeshStorage::_decompress_surface_data_2d(RS::SurfaceData &r_surface, Mesh::Surface *s) {
	const uint32_t v_count = r_surface.vertex_count;
	const uint64_t format = r_surface.format;

	const bool has_vertex = (format & RS::ARRAY_FORMAT_VERTEX) != 0;
	const bool has_normal = (format & RS::ARRAY_FORMAT_NORMAL) != 0;

	if (!r_surface.vertex_data.is_empty() && s->uncompressed_stride > 0) {
		const uint8_t *src_v = r_surface.vertex_data.ptr();
		uint8_t *dst_v = s->uncompressed_buffer.ptrw();

		const uint32_t src_pos_stride = has_vertex ? 8 : 0;
		const uint32_t src_norm_offset = v_count * src_pos_stride;
		const uint32_t remaining_bytes = (uint32_t)r_surface.vertex_data.size() > src_norm_offset ? ((uint32_t)r_surface.vertex_data.size() - src_norm_offset) : 0;
		const uint32_t src_norm_stride = (has_normal && v_count > 0) ? (remaining_bytes / v_count) : 0;

		for (uint32_t i = 0; i < v_count; i++) {
			uint8_t *dv = dst_v + i * s->uncompressed_stride;
			uint32_t dst_offset = 0;

			// Copy 2D Positions
			if (has_vertex) {
				memcpy(dv + dst_offset, src_v + i * src_pos_stride, 8);
				dst_offset += 8;
			}

			if (has_normal) {
				float *norm_dst = reinterpret_cast<float *>(dv + dst_offset);
				if (src_norm_stride >= 16) {
					memcpy(norm_dst, src_v + src_norm_offset + i * src_norm_stride, 16);
				} else if (src_norm_stride >= 4) {
					const uint16_t *norm_src = reinterpret_cast<const uint16_t *>(src_v + src_norm_offset + i * src_norm_stride);
					Vector3 n = _gles2_decode_octahedral_normal(norm_src[0], norm_src[1]);
					norm_dst[0] = n.x;
					norm_dst[1] = n.y;
					norm_dst[2] = n.z;
					norm_dst[3] = 1.0f;
				} else {
					norm_dst[0] = 0.0f;
					norm_dst[1] = 0.0f;
					norm_dst[2] = 1.0f;
					norm_dst[3] = 1.0f;
				}
				dst_offset += 16;
			}
		}

		r_surface.vertex_data = s->uncompressed_buffer;
	}

	_decompress_attribute_buffer_3d(r_surface, v_count);
}

void MeshStorage::_decompress_surface_data_3d(RS::SurfaceData &r_surface, Mesh::Surface *s) {
	const uint32_t vertex_count = r_surface.vertex_count;
	if (vertex_count == 0) {
		return;
	}

	// Always decompress
	if (!r_surface.vertex_data.is_empty() && s->uncompressed_stride > 0) {
		const uint32_t compressed_stride = r_surface.vertex_data.size() / vertex_count;
		const uint32_t uncompressed_stride = s->uncompressed_stride;

		_decompress_3d_buffer(
			r_surface.vertex_data.ptr(),
			s->uncompressed_buffer.ptrw(),
			vertex_count,
			r_surface.format,
			compressed_stride,
			uncompressed_stride,
			r_surface.aabb
		);
		r_surface.vertex_data = s->uncompressed_buffer;
	}

	// Decompress blend shapes (blend shapes only contain VERTEX, NORMAL, TANGENT)
	if (!r_surface.blend_shape_data.is_empty() && s->uncompressed_stride > 0) {
		const uint64_t bs_format = r_surface.format & (RS::ARRAY_FORMAT_BLEND_SHAPE_MASK | RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES);

		// Calculate single blend shape compressed stride
		const bool has_vertex = (bs_format & RS::ARRAY_FORMAT_VERTEX) != 0;
		const bool has_normal = (bs_format & RS::ARRAY_FORMAT_NORMAL) != 0;
		const bool has_tangent = (bs_format & RS::ARRAY_FORMAT_TANGENT) != 0;
		const bool is_compressed = (r_surface.format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) != 0;

		uint32_t bs_comp_stride = 0;
		if (has_vertex) {
			bs_comp_stride += is_compressed ? 8 : 12;
		}
		if (has_normal && !(is_compressed && !has_tangent)) {
			bs_comp_stride += 4;
		}
		if (has_tangent && !is_compressed) {
			bs_comp_stride += 4;
		}

		if (bs_comp_stride > 0 && (r_surface.blend_shape_data.size() % (vertex_count * bs_comp_stride)) == 0) {
			const uint32_t num_shapes = r_surface.blend_shape_data.size() / (vertex_count * bs_comp_stride);
			Vector<uint8_t> new_bs_data;
			new_bs_data.resize(num_shapes * vertex_count * s->uncompressed_stride);

			const uint8_t *bs_src = r_surface.blend_shape_data.ptr();
			uint8_t *bs_dst = new_bs_data.ptrw();

			for (uint32_t i = 0; i < num_shapes; i++) {
				_decompress_3d_buffer(
					bs_src + i * vertex_count * bs_comp_stride,
					bs_dst + i * vertex_count * s->uncompressed_stride,
					vertex_count,
					bs_format,
					bs_comp_stride,
					s->uncompressed_stride,
					r_surface.aabb
				);
			}
			r_surface.blend_shape_data = new_bs_data;
		}
	}

	// Always run attribute decompression
	// to expand 16-bit UVs and half-float custom attributes to 32-bit float
	_decompress_attribute_buffer_3d(r_surface, vertex_count);
}

void MeshStorage::_decompress_attribute_buffer_3d(RS::SurfaceData &r_surface, uint32_t p_vertex_count) {
	if (r_surface.attribute_data.is_empty() || p_vertex_count == 0) {
		return;
	}

	const uint64_t format = r_surface.format;
	const bool is_2d = (format & RS::ARRAY_FLAG_USE_2D_VERTICES) != 0;
	const bool is_compressed = (format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) != 0;
	const bool has_color = (format & RS::ARRAY_FORMAT_COLOR) != 0;
	const bool has_uv = (format & RS::ARRAY_FORMAT_TEX_UV) != 0;
	const bool has_uv2 = (format & RS::ARRAY_FORMAT_TEX_UV2) != 0;

	const uint32_t fmt_shift[RS::ARRAY_CUSTOM_COUNT] = {
		RS::ARRAY_FORMAT_CUSTOM0_SHIFT,
		RS::ARRAY_FORMAT_CUSTOM1_SHIFT,
		RS::ARRAY_FORMAT_CUSTOM2_SHIFT,
		RS::ARRAY_FORMAT_CUSTOM3_SHIFT
	};
	const uint32_t src_custom_fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 4, 8, 4, 8, 12, 16 };
	// Half-float custom formats (2 and 3) are expanded to 32-bit floats (8 and 16 bytes)
	const uint32_t dst_custom_fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 8, 16, 4, 8, 12, 16 };

	bool has_custom[RS::ARRAY_CUSTOM_COUNT] = { false, false, false, false };
	uint32_t custom_fmt[RS::ARRAY_CUSTOM_COUNT] = { 0, 0, 0, 0 };
	uint32_t src_custom_total = 0;
	uint32_t dst_custom_total = 0;

	for (int i = 0; i < RS::ARRAY_CUSTOM_COUNT; i++) {
		if (!is_2d && (format & (1ULL << (RS::ARRAY_CUSTOM0 + i)))) {
			has_custom[i] = true;
			custom_fmt[i] = (format >> fmt_shift[i]) & RS::ARRAY_FORMAT_CUSTOM_MASK;
			src_custom_total += src_custom_fmtsize[custom_fmt[i]];
			dst_custom_total += dst_custom_fmtsize[custom_fmt[i]];
		}
	}

	const uint32_t actual_src_stride = r_surface.attribute_data.size() / p_vertex_count;
	const uint32_t src_color_bytes = has_color ? 4 : 0;

	// Determine exact byte size of UV1 and UV2 in the source attribute buffer
	const uint32_t src_uv1_bytes = has_uv ? (is_compressed ? 4 : 8) : 0;
	const uint32_t src_uv2_bytes = has_uv2 ? (is_compressed ? 4 : 8) : 0;

	if (actual_src_stride != src_color_bytes + src_uv1_bytes + src_uv2_bytes + src_custom_total) {
		// TODO (GLES2): It should be a dev assert.
		ERR_PRINT_ONCE(vformat("GLES2 attribute stride mismatch: got %d, expected %d (fmt=0x%x)",
			(int)actual_src_stride,
			(int)(src_color_bytes + src_uv1_bytes + src_uv2_bytes + src_custom_total),
			(uint64_t)format)
		);
		return;
	}

	const uint32_t dst_color_bytes = has_color ? 4 : 0;
	const uint32_t dst_uv1_bytes = has_uv ? 8 : 0;
	const uint32_t dst_uv2_bytes = has_uv2 ? 8 : 0;
	const uint32_t dst_stride = dst_color_bytes + dst_uv1_bytes + dst_uv2_bytes + dst_custom_total;

	if (dst_stride == 0) {
		return;
	}

	Vector<uint8_t> new_a_data;
	new_a_data.resize(p_vertex_count * dst_stride);

	const uint8_t *src_base = r_surface.attribute_data.ptr();
	uint8_t *dst_base = new_a_data.ptrw();

	for (uint32_t i = 0; i < p_vertex_count; i++) {
		const uint8_t *sa = src_base + (i * actual_src_stride);
		uint8_t *da = dst_base + (i * dst_stride);

		uint32_t src_offset = 0;
		uint32_t dst_offset = 0;

		// Color (RGBA8 UNORM)
		if (has_color) {
			memcpy(da + dst_offset, sa + src_offset, 4);
			src_offset += 4;
			dst_offset += 4;
		}

		// UV1 -> float32x2
		if (has_uv) {
			float *du = reinterpret_cast<float *>(da + dst_offset);
			if (src_uv1_bytes == 4) {
				const uint16_t *uv_src = reinterpret_cast<const uint16_t *>(sa + src_offset);
				du[0] = _gles2_decode_uv_unorm16(uv_src[0], r_surface.uv_scale.x);
				du[1] = _gles2_decode_uv_unorm16(uv_src[1], r_surface.uv_scale.y);
			} else {
				memcpy(du, sa + src_offset, 8);
			}
			src_offset += src_uv1_bytes;
			dst_offset += 8;
		}

		// UV2 -> float32x2
		if (has_uv2) {
			float *du2 = reinterpret_cast<float *>(da + dst_offset);
			if (src_uv2_bytes == 4) {
				const uint16_t *uv2_src = reinterpret_cast<const uint16_t *>(sa + src_offset);
				du2[0] = _gles2_decode_uv_unorm16(uv2_src[0], r_surface.uv_scale.z);
				du2[1] = _gles2_decode_uv_unorm16(uv2_src[1], r_surface.uv_scale.w);
			} else {
				memcpy(du2, sa + src_offset, 8);
			}
			src_offset += src_uv2_bytes;
			dst_offset += 8;
		}

		// Custom 0..3
		for (int c = 0; c < RS::ARRAY_CUSTOM_COUNT; c++) {
			if (!has_custom[c]) {
				continue;
			}
			uint32_t fmt = custom_fmt[c];
			if (fmt == RS::ARRAY_CUSTOM_RG_HALF) {
				const uint16_t *ch = reinterpret_cast<const uint16_t *>(sa + src_offset);
				float *cf = reinterpret_cast<float *>(da + dst_offset);
				cf[0] = Math::half_to_float(ch[0]);
				cf[1] = Math::half_to_float(ch[1]);
			} else if (fmt == RS::ARRAY_CUSTOM_RGBA_HALF) {
				const uint16_t *ch = reinterpret_cast<const uint16_t *>(sa + src_offset);
				float *cf = reinterpret_cast<float *>(da + dst_offset);
				cf[0] = Math::half_to_float(ch[0]);
				cf[1] = Math::half_to_float(ch[1]);
				cf[2] = Math::half_to_float(ch[2]);
				cf[3] = Math::half_to_float(ch[3]);
			} else {
				memcpy(da + dst_offset, sa + src_offset, src_custom_fmtsize[fmt]);
			}
			src_offset += src_custom_fmtsize[fmt];
			dst_offset += dst_custom_fmtsize[fmt];
		}
	}

	r_surface.attribute_data = new_a_data;
}

void MeshStorage::_decompress_3d_buffer(const uint8_t *p_src, uint8_t *p_dst, uint32_t p_vertex_count, uint64_t p_format, uint32_t p_compressed_stride, uint32_t p_uncompressed_stride, const AABB &p_aabb) {
	const bool has_vertex = (p_format & RS::ARRAY_FORMAT_VERTEX) != 0;
	const bool has_normal = (p_format & RS::ARRAY_FORMAT_NORMAL) != 0;
	const bool has_tangent = (p_format & RS::ARRAY_FORMAT_TANGENT) != 0;

	// Already decompressed interleaved float32 buffer
	if (p_compressed_stride == p_uncompressed_stride && !(p_format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
		// Verify if it isn't the edge case where compressed
		// size equals uncompressed size (not possible for 3D since pos is 12, norm is 4 vs 12)
		if (unlikely(!has_normal && !has_tangent)) {
			memcpy(p_dst, p_src, p_vertex_count * p_uncompressed_stride);
			return;
		}
	}

	// Determine Godot 4's exact planar block layout:
	// Block 0: Position stream (p_vertex_count * pos_stride)
	//   - Uncompressed: 12 bytes (float32 x 3)
	//   - Compressed:   8 bytes  (uint16 x 4). Word 3 holds either:
	//       * Tangent (octahedral uint8 x 2) if has_tangent is true
	//       * Normal  (octahedral uint8 x 2) if has_normal && !has_tangent
	//       * 0xFFFF  if neither is present
	// Block 1: Normal / Tangent stream (p_vertex_count * norm_tang_stride)
	//   - When positions are compressed (8-byte):
	//       * If has_normal && has_tangent: 4 bytes (octahedral normal uint16 x 2)
	//       * Otherwise: 0 bytes (normal was packed into position Word 3)
	//   - When positions are uncompressed (12-byte):
	//       * has_normal  ? 4 bytes (octahedral normal uint16 x 2) : 0
	//       * has_tangent ? 4 bytes (octahedral tangent uint16 x 2) : 0
	uint32_t pos_stride = 0;
	uint32_t norm_tang_stride = 0;
	bool pos_is_compressed = false;
	bool normal_in_pos_w = false;
	bool tangent_in_pos_w = false;

	const uint32_t uncomp_pos_norm_tang_bytes = (
		(has_vertex ? 12 : 0) + (has_normal ? 4 : 0) + (has_tangent ? 4 : 0)
	);

	if (has_vertex) {
		if (
			(p_format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) &&
			p_compressed_stride < uncomp_pos_norm_tang_bytes
		) {
			pos_stride = 8;
			pos_is_compressed = true;
			if (has_normal && has_tangent) {
				norm_tang_stride = 4; // Normal is in separate 4-byte stream, Tangent is in pos[3]
				tangent_in_pos_w = true;
			} else if (has_normal && !has_tangent) {
				norm_tang_stride = 0; // Normal is in pos[3]
				normal_in_pos_w = true;
			} else if (!has_normal && has_tangent) {
				norm_tang_stride = 0; // Tangent is in pos[3]
				tangent_in_pos_w = true;
			}
		} else {
			pos_stride = 12;
			pos_is_compressed = false;
			norm_tang_stride = (has_normal ? 4 : 0) + (has_tangent ? 4 : 0);
		}
	} else {
		norm_tang_stride = (has_normal ? 4 : 0) + (has_tangent ? 4 : 0);
	}

	const uint8_t *src_pos_base = p_src;
	const uint8_t *src_norm_tang_base = p_src + (p_vertex_count * pos_stride);

	const uint32_t dst_pos_offset = 0;
	const uint32_t dst_norm_offset = has_vertex ? 12 : 0;
	const uint32_t dst_tang_offset = dst_norm_offset + (has_normal ? 12 : 0);

	for (uint32_t i = 0; i < p_vertex_count; i++) {
		const uint8_t *src_pos_ptr = src_pos_base + (i * pos_stride);
		const uint8_t *src_nt_ptr = src_norm_tang_base + (i * norm_tang_stride);
		uint8_t *dst_v = p_dst + (i * p_uncompressed_stride);

		// Position -> float32 x 3
		if (has_vertex) {
			float *pos_dst = reinterpret_cast<float *>(dst_v + dst_pos_offset);
			if (pos_is_compressed) {
				const uint16_t *pos_u16 = reinterpret_cast<const uint16_t *>(src_pos_ptr);
				pos_dst[0] = (pos_u16[0] / 65535.0f) * p_aabb.size.x + p_aabb.position.x;
				pos_dst[1] = (pos_u16[1] / 65535.0f) * p_aabb.size.y + p_aabb.position.y;
				pos_dst[2] = (pos_u16[2] / 65535.0f) * p_aabb.size.z + p_aabb.position.z;
			} else {
				memcpy(pos_dst, src_pos_ptr, 12);
			}
		}

		// Normal -> float32 x 3
		if (has_normal) {
			float *norm_dst = reinterpret_cast<float *>(dst_v + dst_norm_offset);
			Vector3 normal;
			if (normal_in_pos_w) {
				// 2x uint8 UNORM packed into bytes 6 and 7 of the 8-byte position
				const uint8_t *norm_u8 = src_pos_ptr + 6;
				uint16_t nx = static_cast<uint16_t>(norm_u8[0]) * 257;
				uint16_t ny = static_cast<uint16_t>(norm_u8[1]) * 257;
				normal = _gles2_decode_octahedral_normal(nx, ny);
			} else {
				const uint16_t *norm_u16 = reinterpret_cast<const uint16_t *>(src_nt_ptr);
				normal = _gles2_decode_octahedral_normal(norm_u16[0], norm_u16[1]);
			}
			norm_dst[0] = normal.x;
			norm_dst[1] = normal.y;
			norm_dst[2] = normal.z;
		}

		// Tangent -> float32 x 4
		if (has_tangent) {
			float *tang_dst = reinterpret_cast<float *>(dst_v + dst_tang_offset);
			Vector3 tangent;
			float binormal_sign = 1.0f;

			if (tangent_in_pos_w) {
				// 2x uint8 UNORM packed into bytes 6 and 7 of the 8-byte position
				const uint8_t *tang_u8 = src_pos_ptr + 6;
				uint16_t tx = static_cast<uint16_t>(tang_u8[0]) * 257;
				uint16_t ty = static_cast<uint16_t>(tang_u8[1]) * 257;
				_gles2_decode_octahedral_tangent(tx, ty, tangent, binormal_sign);
			} else {
				const uint32_t tang_src_offset = has_normal ? 4 : 0;
				const uint16_t *tang_u16 = reinterpret_cast<const uint16_t *>(src_nt_ptr + tang_src_offset);
				_gles2_decode_octahedral_tangent(tang_u16[0], tang_u16[1], tangent, binormal_sign);
			}

			tang_dst[0] = tangent.x;
			tang_dst[1] = tangent.y;
			tang_dst[2] = tangent.z;
			tang_dst[3] = binormal_sign;
		}
	}
}

void MeshStorage::_decompress_skin_buffer(RS::SurfaceData &r_surface, Mesh::Surface *s) {
	if (r_surface.skin_data.is_empty() || r_surface.vertex_count == 0) {
		return;
	}

	// Preserve original uint16 skin data
	s->skin_data_cache = r_surface.skin_data;

	const uint32_t v_count = r_surface.vertex_count;
	const bool use_8_weights = (r_surface.format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS) != 0;
	const uint32_t weight_count = use_8_weights ? 8 : 4;

	// Incoming Godot 4 skin_data is interleaved per vertex:
	// [bones: weight_count * uint16_t] [weights: weight_count * uint16_t]
	const uint32_t expected_src_stride = weight_count * 2 * sizeof(uint16_t);
	const uint32_t actual_src_stride = r_surface.skin_data.size() / v_count;
	if (actual_src_stride != expected_src_stride) {
		return;
	}

	// Decompress to 32-bit floats for GL_FLOAT vertex attributes
	const uint32_t dst_stride = weight_count * 2 * sizeof(float);
	Vector<uint8_t> new_skin_data;
	new_skin_data.resize(v_count * dst_stride);

	const uint8_t *src_ptr = r_surface.skin_data.ptr();
	uint8_t *dst_ptr = new_skin_data.ptrw();

	for (uint32_t i = 0; i < v_count; i++) {
		const uint16_t *src_u16 = reinterpret_cast<const uint16_t *>(src_ptr + i * actual_src_stride);
		float *dst_f32 = reinterpret_cast<float *>(dst_ptr + i * dst_stride);

		// Bones: uint16 index -> float32
		for (uint32_t k = 0; k < weight_count; k++) {
			dst_f32[k] = static_cast<float>(src_u16[k]);
		}
		// Weights: uint16 UNORM -> float32 [0.0, 1.0]
		for (uint32_t k = 0; k < weight_count; k++) {
			dst_f32[weight_count + k] = static_cast<float>(src_u16[weight_count + k]) / 65535.0f;
		}
	}

	r_surface.skin_data = new_skin_data;
}

Array MeshStorage::mesh_surface_get_arrays(RID p_mesh, int p_surface) const {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, Array());
	ERR_FAIL_UNSIGNED_INDEX_V((uint32_t)p_surface, mesh->surface_count, Array());

	Mesh::Surface *s = mesh->surfaces[p_surface];

	Array arrays;
	arrays.resize(RS::ARRAY_MAX);

	if (s->vertex_count == 0) {
		return arrays;
	}

	if (!s->vertex_cache.is_empty()) {
		arrays[RS::ARRAY_VERTEX] = s->vertex_cache;
		if (!s->normal_cache.is_empty()) {
			arrays[RS::ARRAY_NORMAL] = s->normal_cache;
		}
		if (!s->tangent_cache.is_empty()) {
			arrays[RS::ARRAY_TANGENT] = s->tangent_cache;
		}
		if (!s->color_cache.is_empty()) {
			arrays[RS::ARRAY_COLOR] = s->color_cache;
		}
		if (!s->uv_cache.is_empty()) {
			arrays[RS::ARRAY_TEX_UV] = s->uv_cache;
		}
		if (!s->uv2_cache.is_empty()) {
			arrays[RS::ARRAY_TEX_UV2] = s->uv2_cache;
		}
		if (!s->index_cache.is_empty()) {
			arrays[RS::ARRAY_INDEX] = s->index_cache;
		}
		return arrays;
	}

	Mesh::Surface::Version v;
	const_cast<MeshStorage *>(this)->_mesh_surface_generate_version_for_input_mask(v, s, 0xFFFFFFFF);

	Vector<uint8_t> v_buf;
	if (s->vertex_buffer != 0) {
		v_buf = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->vertex_buffer, s->vertex_buffer_size);
	}
	Vector<uint8_t> a_buf;
	if (s->attribute_buffer != 0) {
		a_buf = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->attribute_buffer, s->attribute_buffer_size);
	}
	Vector<uint8_t> i_buf;
	if (s->index_count > 0 && s->index_buffer != 0) {
		i_buf = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer, s->index_buffer_size);
	}

	const uint8_t *v_data = v_buf.is_empty() ? nullptr : v_buf.ptr();
	const uint8_t *a_data = a_buf.is_empty() ? nullptr : a_buf.ptr();

	// Vertex
	if (v.attribs[RS::ARRAY_VERTEX].enabled && v_data) {
		PackedVector3Array vertices;
		vertices.resize(s->vertex_count);
		Vector3 *dst = vertices.ptrw();
		int stride = v.attribs[RS::ARRAY_VERTEX].stride;
		int offset = v.attribs[RS::ARRAY_VERTEX].offset;

		for (uint32_t i = 0; i < s->vertex_count; i++) {
			const uint8_t *src = v_data + i * stride + offset;
			const float *f = (const float *)src;
			if (s->format & RS::ARRAY_FLAG_USE_2D_VERTICES) {
				dst[i] = Vector3(f[0], f[1], 0.0f);
			} else {
				dst[i] = Vector3(f[0], f[1], f[2]);
			}
		}
		s->vertex_cache = vertices;
		arrays[RS::ARRAY_VERTEX] = vertices;
	}

	// Normal
	if (v.attribs[RS::ARRAY_NORMAL].enabled && v_data) {
		PackedVector3Array normals;
		normals.resize(s->vertex_count);
		Vector3 *dst = normals.ptrw();
		int stride = v.attribs[RS::ARRAY_NORMAL].stride;
		int offset = v.attribs[RS::ARRAY_NORMAL].offset;

		for (uint32_t i = 0; i < s->vertex_count; i++) {
			uint32_t read_offset = i * stride + offset;

			if ((read_offset + sizeof(float) * 3) > (uint32_t)(v_buf.size())) {
				dst[i] = Vector3(0.0f, 1.0f, 0.0f);
				continue;
			}

			const float *f = (const float *)(v_data + read_offset);
			dst[i] = Vector3(f[0], f[1], f[2]);
		}
		s->normal_cache = normals;
		arrays[RS::ARRAY_NORMAL] = normals;
	}

	// Tangent
	if (v.attribs[RS::ARRAY_TANGENT].enabled && v_data) {
		PackedFloat32Array tangents;
		tangents.resize(s->vertex_count * 4);
		float *dst = tangents.ptrw();
		int stride = v.attribs[RS::ARRAY_TANGENT].stride;
		int offset = v.attribs[RS::ARRAY_TANGENT].offset;

		for (uint32_t i = 0; i < s->vertex_count; i++) {
			uint32_t read_offset = i * stride + offset;

			if ((read_offset + sizeof(float) * 4) > (uint32_t)(v_buf.size())) {
				dst[i * 4 + 0] = 1.0f;
				dst[i * 4 + 1] = 0.0f;
				dst[i * 4 + 2] = 0.0f;
				dst[i * 4 + 3] = 1.0f;
				continue;
			}

			const float *f = (const float *)(v_data + read_offset);
			dst[i * 4 + 0] = f[0];
			dst[i * 4 + 1] = f[1];
			dst[i * 4 + 2] = f[2];
			dst[i * 4 + 3] = f[3];
		}
		s->tangent_cache = tangents;
		arrays[RS::ARRAY_TANGENT] = tangents;
	}

	// Color
	if (v.attribs[RS::ARRAY_COLOR].enabled && a_data) {
		PackedColorArray colors;
		colors.resize(s->vertex_count);
		Color *dst = colors.ptrw();
		int stride = v.attribs[RS::ARRAY_COLOR].stride;
		int offset = v.attribs[RS::ARRAY_COLOR].offset;

		for (uint32_t i = 0; i < s->vertex_count; i++) {
			const uint8_t *src = a_data + i * stride + offset;
			if (v.attribs[RS::ARRAY_COLOR].type == GL_FLOAT) {
				const float *f = (const float *)src;
				dst[i] = Color(f[0], f[1], f[2], f[3]);
			} else {
				dst[i] = Color(src[0] / 255.0f, src[1] / 255.0f, src[2] / 255.0f, src[3] / 255.0f);
			}
		}
		s->color_cache = colors;
		arrays[RS::ARRAY_COLOR] = colors;
	}

	// tex_uv
	if (v.attribs[RS::ARRAY_TEX_UV].enabled && a_data) {
		PackedVector2Array uvs;
		uvs.resize(s->vertex_count);
		Vector2 *dst = uvs.ptrw();
		int stride = v.attribs[RS::ARRAY_TEX_UV].stride;
		int offset = v.attribs[RS::ARRAY_TEX_UV].offset;

		for (uint32_t i = 0; i < s->vertex_count; i++) {
			const float *f = (const float *)(a_data + i * stride + offset);
			dst[i] = Vector2(f[0], f[1]);
		}
		s->uv_cache = uvs;
		arrays[RS::ARRAY_TEX_UV] = uvs;
	}

	// tex_uv2
	if (v.attribs[RS::ARRAY_TEX_UV2].enabled && a_data) {
		PackedVector2Array uvs2;
		uvs2.resize(s->vertex_count);
		Vector2 *dst = uvs2.ptrw();
		int stride = v.attribs[RS::ARRAY_TEX_UV2].stride;
		int offset = v.attribs[RS::ARRAY_TEX_UV2].offset;

		for (uint32_t i = 0; i < s->vertex_count; i++) {
			const float *f = (const float *)(a_data + i * stride + offset);
			dst[i] = Vector2(f[0], f[1]);
		}
		s->uv2_cache = uvs2;
		arrays[RS::ARRAY_TEX_UV2] = uvs2;
	}

	// Index
	if (s->index_count > 0 && !i_buf.is_empty()) {
		PackedInt32Array indices;
		indices.resize(s->index_count);
		int32_t *dst = indices.ptrw();
		const uint8_t *i_data = i_buf.ptr();

		const bool use_16 = (s->vertex_count <= 65536);
		for (uint32_t i = 0; i < s->index_count; i++) {
			if (use_16) {
				dst[i] = ((const uint16_t *)i_data)[i];
			} else {
				dst[i] = ((const uint32_t *)i_data)[i];
			}
		}
		s->index_cache = indices;
		arrays[RS::ARRAY_INDEX] = indices;
	}

	return arrays;
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
	if (!mi->array_update_list.in_list()) {
		dirty_mesh_instance_arrays.add(&mi->array_update_list);
	}

	return rid;
}

void MeshStorage::mesh_instance_free(RID p_rid) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(mi);

	_mesh_instance_clear(mi);
	if (mi->array_update_list.in_list()) {
		dirty_mesh_instance_arrays.remove(&mi->array_update_list);
	}
	if (mi->weight_update_list.in_list()) {
		dirty_mesh_instance_weights.remove(&mi->weight_update_list);
	}
	mi->mesh->instances.erase(mi->I);
	mi->I = nullptr;

	mesh_instance_owner.free(p_rid);
}

void MeshStorage::mesh_instance_set_skeleton(RID p_mesh_instance, RID p_skeleton) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	if (!mi || mi->skeleton == p_skeleton) {
		return;
	}
	mi->skeleton = p_skeleton;
	mi->skeleton_version = 0;
	mi->dirty = true;
	if (!mi->array_update_list.in_list()) {
		dirty_mesh_instance_arrays.add(&mi->array_update_list);
	}
}

void MeshStorage::mesh_instance_set_blend_shape_weight(RID p_mesh_instance, int p_shape, float p_weight) {
	MeshInstance *mi = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(mi);
	ERR_FAIL_INDEX(p_shape, (int)mi->blend_weights.size());
	mi->blend_weights[p_shape] = p_weight;
	mi->dirty = true;
}

void MeshStorage::_mesh_instance_clear(MeshInstance *mi) {
	if (mi->scratch_handle.is_valid()) {
		active_skinning_instances.erase(mi->scratch_handle);
		mi->scratch_handle = GenerationalHandle();
	}
	for (uint32_t i = 0; i < mi->surfaces.size(); i++) {
		if (mi->surfaces[i].vertex_buffer != 0) {
			GLES2::Utilities::get_singleton()->buffer_free_data(mi->surfaces[i].vertex_buffer);
			mi->surfaces[i].vertex_buffer = 0;
		}
		for (int j = 0; j < 2; j++) {
			if (mi->surfaces[i].vertex_buffers[j] != 0) {
				GLES2::Utilities::get_singleton()->buffer_free_data(mi->surfaces[i].vertex_buffers[j]);
				mi->surfaces[i].vertex_buffers[j] = 0;
			}
		}

		if (mi->surfaces[i].versions) {
			memfree(mi->surfaces[i].versions);
			mi->surfaces[i].versions = nullptr;
		}
		mi->surfaces[i].version_index_map.clear();
		mi->surfaces[i].skinning_scratch_buffer.clear();
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
			s.vertex_stride_cache += sizeof(float) * 3;
		}
		if ((s.format_cache & (1ULL << RS::ARRAY_TANGENT))) {
			s.vertex_tangent_offset_cache = s.vertex_stride_cache;
			s.vertex_stride_cache += sizeof(float) * 4;
		}

		uint32_t buffer_size = s.vertex_stride_cache * mesh->surfaces[p_surface]->vertex_count;
		s.skinning_scratch_buffer.resize(buffer_size);

		const void *initial_data = nullptr;
		if ((uint32_t)mesh->surfaces[p_surface]->uncompressed_buffer.size() == buffer_size) {
			initial_data = mesh->surfaces[p_surface]->uncompressed_buffer.ptr();
		}

		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::_mesh_instance_add_surface: glGetIntegerv GL_ARRAY_BUFFER_BINDING");

		// Buffer to be used for rendering. Final output of skeleton and blend shapes.
		glGenBuffers(1, &s.vertex_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, s.vertex_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s.vertex_buffer, buffer_size, initial_data, GL_DYNAMIC_DRAW, "MeshInstance vertex buffer");
		GL_CHECK_ERROR("GLES2::MeshStorage::_mesh_instance_add_surface: buffer_allocate_data vertex_buffer");

		if (mesh->blend_shape_count > 0) {
			// Ping-Pong buffers for processing blendshapes.
			glGenBuffers(2, s.vertex_buffers);
			for (uint32_t i = 0; i < 2; i++) {
				glBindBuffer(GL_ARRAY_BUFFER, s.vertex_buffers[i]);
				GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s.vertex_buffers[i], buffer_size, initial_data, GL_DYNAMIC_DRAW, "MeshInstance process buffer[" + itos(i) + "]");
				GL_CHECK_ERROR("GLES2::MeshStorage::_mesh_instance_add_surface: buffer_allocate_data process buffer");
			}
		}
		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
		GL_CHECK_ERROR("GLES2::MeshStorage::_mesh_instance_add_surface: restore GL_ARRAY_BUFFER");
	}

	mi->surfaces.push_back(std::move(s));
	mi->dirty = true;
}

void MeshStorage::_mesh_instance_remove_surface(MeshInstance *mi, int p_surface) {
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mi->surfaces.size());

	if (mi->surfaces[p_surface].vertex_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(mi->surfaces[p_surface].vertex_buffer);
		mi->surfaces[p_surface].vertex_buffer = 0;
	}
	for (int j = 0; j < 2; j++) {
		if (mi->surfaces[p_surface].vertex_buffers[j] != 0) {
			GLES2::Utilities::get_singleton()->buffer_free_data(mi->surfaces[p_surface].vertex_buffers[j]);
			mi->surfaces[p_surface].vertex_buffers[j] = 0;
		}
	}
	if (mi->surfaces[p_surface].versions) {
		memfree(mi->surfaces[p_surface].versions);
	}
	mi->surfaces[p_surface].version_index_map.clear();
	mi->surfaces[p_surface].skinning_scratch_buffer.clear();

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

void MeshStorage::_compute_skeleton_2d(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface) {
	ERR_FAIL_NULL(p_mi);
	ERR_FAIL_NULL(p_sk);

	Mesh::Surface *s = p_mi->mesh->surfaces[p_surface];
	MeshInstance::Surface &mis = p_mi->surfaces[p_surface];

	if (s->uncompressed_buffer.is_empty() && s->vertex_buffer != 0) {
		s->uncompressed_buffer = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->vertex_buffer, s->vertex_buffer_size);
	}
	if (s->skin_data_cache.is_empty() && s->skin_buffer != 0) {
		s->skin_data_cache = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->skin_buffer, s->skin_buffer_size);
	}

	if (s->uncompressed_buffer.is_empty() || s->skin_data_cache.is_empty() || s->vertex_count == 0) {
		return;
	}

	const bool use_8_weights = mis.format_cache & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;
	const uint32_t vertex_count = s->vertex_count;
	const uint32_t dst_vertex_stride = mis.vertex_stride_cache;
	const uint32_t src_vertex_stride = s->uncompressed_buffer.size() / vertex_count;
	const uint32_t skin_stride = s->skin_data_cache.size() / vertex_count;
	const uint32_t required_bytes = vertex_count * dst_vertex_stride;

	if (unlikely(mis.skinning_scratch_buffer.size() != required_bytes)) {
		mis.skinning_scratch_buffer.resize(required_bytes);
	}

	uint8_t *RESTRICT v_w = mis.skinning_scratch_buffer.ptr();
	const uint8_t *RESTRICT v_r = s->uncompressed_buffer.ptr();
	const uint8_t *RESTRICT s_r = s->skin_data_cache.ptr();
	const float *RESTRICT bone_data = p_sk->data.ptr();

	const Transform2D inv_canvas_xform = p_mi->canvas_item_transform_2d.affine_inverse();
	const Transform2D skeleton_matrix = inv_canvas_xform * p_sk->base_transform_2d;
	const Transform2D inverse_matrix = skeleton_matrix.affine_inverse();

	// Pre-compute final 2D bone matrices once per surface
	const int bone_count = p_sk->size;
	const int bone_floats = p_sk->use_2d ? 8 : 12;
	if (skeleton_bone_2d_scratch.size() < (uint32_t)bone_count) {
		skeleton_bone_2d_scratch.resize(bone_count);
	}
	Transform2D *RESTRICT precomputed_bones = skeleton_bone_2d_scratch.ptr();

	for (int b = 0; b < bone_count; b++) {
		const float *b_ptr = bone_data + b * bone_floats;
		Transform2D b_xform;
		b_xform.columns[0][0] = b_ptr[0];
		b_xform.columns[1][0] = b_ptr[1];
		b_xform.columns[2][0] = b_ptr[3];
		b_xform.columns[0][1] = b_ptr[4];
		b_xform.columns[1][1] = b_ptr[5];
		b_xform.columns[2][1] = b_ptr[7];
		precomputed_bones[b] = skeleton_matrix * b_xform * inverse_matrix;
	}

	const int weight_count = use_8_weights ? 8 : 4;
	const uint32_t weights_byte_offset = weight_count * sizeof(uint16_t);

	for (uint32_t i = 0; i < vertex_count; i++) {
		const uint8_t *p32 = v_r + i * src_vertex_stride;
		const Vector2 src_pos(
			unaligned_read<float>(p32 + 0),
			unaligned_read<float>(p32 + 4)
		);

		const uint8_t *bones_ptr = s_r + i * skin_stride;
		const uint8_t *weights_ptr = bones_ptr + weights_byte_offset;

		Vector2 dst_pos;
		float total_weight = 0.0f;

		for (int k = 0; k < weight_count; k++) {
			const uint16_t weight_val = unaligned_read<uint16_t>(weights_ptr + k * sizeof(uint16_t));
			if (weight_val == 0) {
				continue;
			}
			const uint16_t bone_idx = unaligned_read<uint16_t>(bones_ptr + k * sizeof(uint16_t));
			if (unlikely(bone_idx >= (uint16_t)bone_count)) {
				continue;
			}

			const float w = weight_val * (1.0f / 65535.0f);
			total_weight += w;
			dst_pos += precomputed_bones[bone_idx].xform(src_pos) * w;
		}

		if (total_weight < 0.01f) {
			dst_pos = src_pos;
		} else {
			dst_pos /= total_weight;
		}

		uint8_t *w32 = v_w + i * dst_vertex_stride;
		unaligned_write<float>(w32 + 0, dst_pos.x);
		unaligned_write<float>(w32 + 4, dst_pos.y);

		if (src_vertex_stride > 8 && dst_vertex_stride >= src_vertex_stride) {
			memcpy(w32 + 8, p32 + 8, src_vertex_stride - 8);
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, mis.vertex_buffer);
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		mis.vertex_buffer,
		0,
		required_bytes,
		v_w
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::_compute_skeleton_2d: buffer_update_data");
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::MeshStorage::_compute_skeleton_2d: glBindBuffer");
}

void MeshStorage::_compute_skeleton_3d(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface) {
	ERR_FAIL_NULL(p_mi);
	ERR_FAIL_NULL(p_sk);

	Mesh::Surface *s = p_mi->mesh->surfaces[p_surface];
	MeshInstance::Surface &mis = p_mi->surfaces[p_surface];

	if (s->uncompressed_buffer.is_empty() && s->vertex_buffer != 0) {
		s->uncompressed_buffer = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->vertex_buffer, s->vertex_buffer_size);
	}
	if (s->skin_data_cache.is_empty() && s->skin_buffer != 0) {
		s->skin_data_cache = GLES2::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->skin_buffer, s->skin_buffer_size);
	}

	if (s->uncompressed_buffer.is_empty() || s->skin_data_cache.is_empty() || s->vertex_count == 0) {
		return;
	}

	const bool use_8_weights = mis.format_cache & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;
	const bool has_normal = mis.format_cache & RS::ARRAY_FORMAT_NORMAL;
	const bool has_tangent = mis.format_cache & RS::ARRAY_FORMAT_TANGENT;

	const uint32_t vertex_count = s->vertex_count;
	const uint32_t dst_vertex_stride = mis.vertex_stride_cache;
	const uint32_t src_vertex_stride = s->uncompressed_buffer.size() / vertex_count;
	const uint32_t skin_stride = s->skin_data_cache.size() / vertex_count;
	const uint32_t required_bytes = vertex_count * dst_vertex_stride;

	if (unlikely(mis.skinning_scratch_buffer.size() != required_bytes)) {
		mis.skinning_scratch_buffer.resize(required_bytes);
	}

	uint8_t *RESTRICT v_w = mis.skinning_scratch_buffer.ptr();
	const uint8_t *RESTRICT v_r = s->uncompressed_buffer.ptr();
	const uint8_t *RESTRICT s_r = s->skin_data_cache.ptr();
	const float *RESTRICT bone_data = p_sk->data.ptr();

	// Pre-unpack 3D bone transforms and normal cofactor bases into SoA streams
	const int bone_count = p_sk->size;
	LocalVector<Transform3D> &bone_xform_stream = skeleton_bone_3d_soa.get_stream<0>();
	LocalVector<Basis> &bone_cofactor_stream = skeleton_bone_3d_soa.get_stream<1>();

	if (bone_xform_stream.size() < (uint32_t)bone_count) {
		bone_xform_stream.resize(bone_count);
		bone_cofactor_stream.resize(bone_count);
	}

	Transform3D *RESTRICT bone_xforms = bone_xform_stream.ptr();
	Basis *RESTRICT bone_cofactors = bone_cofactor_stream.ptr();

	for (int b = 0; b < bone_count; b++) {
		const float *b_ptr = bone_data + b * 12;
		Transform3D b_xform;
		b_xform.basis.rows[0][0] = b_ptr[0];
		b_xform.basis.rows[0][1] = b_ptr[1];
		b_xform.basis.rows[0][2] = b_ptr[2];
		b_xform.origin.x = b_ptr[3];
		b_xform.basis.rows[1][0] = b_ptr[4];
		b_xform.basis.rows[1][1] = b_ptr[5];
		b_xform.basis.rows[1][2] = b_ptr[6];
		b_xform.origin.y = b_ptr[7];
		b_xform.basis.rows[2][0] = b_ptr[8];
		b_xform.basis.rows[2][1] = b_ptr[9];
		b_xform.basis.rows[2][2] = b_ptr[10];
		b_xform.origin.z = b_ptr[11];

		bone_xforms[b] = b_xform;
		if (has_normal || has_tangent) {
			Basis cofactor;
			cofactor.set_column(0, b_xform.basis.get_column(1).cross(b_xform.basis.get_column(2)));
			cofactor.set_column(1, b_xform.basis.get_column(2).cross(b_xform.basis.get_column(0)));
			cofactor.set_column(2, b_xform.basis.get_column(0).cross(b_xform.basis.get_column(1)));
			bone_cofactors[b] = cofactor;
		}
	}

	const int weight_count = use_8_weights ? 8 : 4;
	const uint32_t weights_byte_offset = weight_count * sizeof(uint16_t);

	for (uint32_t i = 0; i < vertex_count; i++) {
		const uint8_t *p32 = v_r + i * src_vertex_stride;

		const Vector3 src_pos(
			unaligned_read<float>(p32 + 0),
			unaligned_read<float>(p32 + 4),
			unaligned_read<float>(p32 + 8)
		);

		Vector3 src_norm;
		if (has_normal) {
			src_norm = Vector3(
				unaligned_read<float>(p32 + 12),
				unaligned_read<float>(p32 + 16),
				unaligned_read<float>(p32 + 20)
			);
		}

		Vector3 src_tang;
		float src_binorm_sign = 1.0f;
		if (has_tangent) {
			const uint32_t tang_offset = has_normal ? 24 : 12;
			src_tang = Vector3(
				unaligned_read<float>(p32 + tang_offset + 0),
				unaligned_read<float>(p32 + tang_offset + 4),
				unaligned_read<float>(p32 + tang_offset + 8)
			);
			src_binorm_sign = unaligned_read<float>(p32 + tang_offset + 12);
		}

		const uint8_t *bones_ptr = s_r + i * skin_stride;
		const uint8_t *weights_ptr = bones_ptr + weights_byte_offset;

		Vector3 dst_pos;
		Vector3 dst_norm;
		Vector3 dst_tang;
		float total_weight = 0.0f;

		for (int k = 0; k < weight_count; k++) {
			const uint16_t weight_val = unaligned_read<uint16_t>(weights_ptr + k * sizeof(uint16_t));
			if (weight_val == 0) {
				continue;
			}

			const uint16_t bone_idx = unaligned_read<uint16_t>(bones_ptr + k * sizeof(uint16_t));
			if (unlikely(bone_idx >= (uint16_t)bone_count)) {
				continue;
			}

			const float w = weight_val * (1.0f / 65535.0f);
			total_weight += w;

			const Transform3D &b_xform = bone_xforms[bone_idx];
			dst_pos += b_xform.xform(src_pos) * w;

			if (has_normal || has_tangent) {
				const Basis &b_cofactor = bone_cofactors[bone_idx];
				if (has_normal) {
					dst_norm += b_cofactor.xform(src_norm) * w;
				}
				if (has_tangent) {
					dst_tang += b_cofactor.xform(src_tang) * w;
				}
			}
		}

		if (total_weight < 0.01f) {
			dst_pos = src_pos;
			dst_norm = src_norm;
			dst_tang = src_tang;
		} else {
			dst_pos /= total_weight;
			if (has_normal) {
				dst_norm = (dst_norm / total_weight).normalized();
			}
			if (has_tangent) {
				dst_tang = (dst_tang / total_weight).normalized();
			}
		}

		uint8_t *w32 = v_w + i * dst_vertex_stride;

		unaligned_write<float>(w32 + 0, dst_pos.x);
		unaligned_write<float>(w32 + 4, dst_pos.y);
		unaligned_write<float>(w32 + 8, dst_pos.z);

		uint32_t current_offset = 12;
		if (has_normal) {
			unaligned_write<float>(w32 + current_offset + 0, dst_norm.x);
			unaligned_write<float>(w32 + current_offset + 4, dst_norm.y);
			unaligned_write<float>(w32 + current_offset + 8, dst_norm.z);
			current_offset += 12;
		}
		if (has_tangent) {
			unaligned_write<float>(w32 + current_offset + 0, dst_tang.x);
			unaligned_write<float>(w32 + current_offset + 4, dst_tang.y);
			unaligned_write<float>(w32 + current_offset + 8, dst_tang.z);
			unaligned_write<float>(w32 + current_offset + 12, src_binorm_sign);
			current_offset += 16;
		}

		if (src_vertex_stride > current_offset && dst_vertex_stride >= src_vertex_stride) {
			memcpy(w32 + current_offset, p32 + current_offset, src_vertex_stride - current_offset);
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, mis.vertex_buffer);
	GLES2::Utilities::get_singleton()->buffer_update_data(
		GL_ARRAY_BUFFER,
		mis.vertex_buffer,
		0,
		required_bytes,
		v_w
	);
	GL_CHECK_ERROR("GLES2::MeshStorage::_compute_skeleton_3d: buffer_update_data");
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_CHECK_ERROR("GLES2::MeshStorage::_compute_skeleton_3d: glBindBuffer");
}

void MeshStorage::_update_mesh_instance_surface_2d(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface, float p_base_weight) {
	bool can_use_skeleton = (
		p_sk != nullptr &&
		p_sk->use_2d &&
		(p_mi->surfaces[p_surface].format_cache & RS::ARRAY_FORMAT_BONES)
	);

	if (can_use_skeleton) {
		_compute_skeleton_2d(p_mi, p_sk, p_surface);
	}
}

void MeshStorage::_update_mesh_instance_surface_3d(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface, float p_base_weight) {
	bool can_use_skeleton = (
		p_sk != nullptr &&
		!p_sk->use_2d &&
		(p_mi->surfaces[p_surface].format_cache & RS::ARRAY_FORMAT_BONES)
	);

	if (can_use_skeleton) {
		_compute_skeleton_3d(p_mi, p_sk, p_surface);
	}
}

void MeshStorage::update_mesh_instances() {
	if (dirty_mesh_instance_arrays.first() == nullptr) {
		return;
	}

	GLint prev_array_buffer = 0;
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: glGetIntegerv GL_ARRAY_BUFFER_BINDING");

	while (dirty_mesh_instance_arrays.first()) {
		MeshInstance *mi = dirty_mesh_instance_arrays.first()->self();
		Skeleton *sk = skeleton_owner.get_or_null(mi->skeleton);
		float base_weight = 1.0f;
		if (mi->surfaces.size() && mi->mesh->blend_shape_count && mi->mesh->blend_shape_mode == RS::BLEND_SHAPE_MODE_NORMALIZED) {
			for (uint32_t i = 0; i < mi->mesh->blend_shape_count; i++) {
				base_weight -= mi->blend_weights[i];
			}
		}

		for (uint32_t i = 0; i < mi->surfaces.size(); i++) {
			if (mi->surfaces[i].vertex_buffer == 0) {
				continue;
			}

			if (mi->surfaces[i].format_cache & RS::ARRAY_FLAG_USE_2D_VERTICES) {
				_update_mesh_instance_surface_2d(mi, sk, i, base_weight);
			} else {
				_update_mesh_instance_surface_3d(mi, sk, i, base_weight);
			}
		}

		mi->dirty = false;
		if (sk) {
			mi->skeleton_version = sk->version;
		}
		dirty_mesh_instance_arrays.remove(&mi->array_update_list);
	}

	glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	GL_CHECK_ERROR("GLES2::MeshStorage::update_mesh_instances: restore GL_ARRAY_BUFFER");
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
	multimesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES);
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

		if (
			GLES2_CONFIG->max_vertex_texture_image_units > 0 &&
			GLES2_CONFIG->float_texture_supported
		) {
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
		} else {
			skeleton->transforms_texture = 0;
		}

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

		if (skeleton->size && skeleton->transforms_texture != 0) {
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
