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

static _FORCE_INLINE_ Vector3 _gles1_decode_octahedral_normal(uint16_t p_x, uint16_t p_y) {
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

static _FORCE_INLINE_ void _gles1_decode_octahedral_tangent(uint16_t p_x, uint16_t p_y, Vector3 &r_tangent, float &r_binormal_sign) {
	// Godot 4 packs the binormal sign (W) into the least significant bit of the Y component.
	// We use a bitwise & to extract the sign before shifting the Y component to clear that bit.
	r_binormal_sign = (p_y & 1) ? -1.0f : 1.0f;
	uint16_t shifted_y = p_y >> 1;

	// Decode X from standard 16-bit UNORM (65535) and
	// the shifted Y from 15-bit UNORM (32767) into [-1.0, 1.0]
	float x = (p_x / 65535.0f) * 2.0f - 1.0f;
	float y = (shifted_y / 32767.0f) * 2.0f - 1.0f;

	float z = 1.0f - ABS(x) - ABS(y);

	r_tangent = Vector3(x, y, z);
	float t = CLAMP(-z, 0.0f, 1.0f);
	r_tangent.x += r_tangent.x >= 0.0f ? -t : t;
	r_tangent.y += r_tangent.y >= 0.0f ? -t : t;

	r_tangent.normalize();
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

	// Layout Setup
	s->uncompressed_stride = 0;
	if (p_surface.format & RS::ARRAY_FORMAT_VERTEX) {
		s->uncompressed_stride += (p_surface.format & RS::ARRAY_FLAG_USE_2D_VERTICES) ? (sizeof(float) * 2) : (sizeof(float) * 3);
	}
	if (p_surface.format & RS::ARRAY_FORMAT_NORMAL) {
		s->uncompressed_stride += sizeof(float) * 3;
	}
	if (p_surface.format & RS::ARRAY_FORMAT_TANGENT) {
		s->uncompressed_stride += sizeof(float) * 4;
	}

	if (p_surface.vertex_count > 0 && s->uncompressed_stride > 0) {
		s->uncompressed_buffer.resize(p_surface.vertex_count * s->uncompressed_stride);

		// We must be absolutely certain that data the size is equal
		// to our uncompressed size and that the resize matches.
		ERR_FAIL_COND(s->uncompressed_buffer.size() != p_surface.vertex_count * s->uncompressed_stride);
	}

	RS::SurfaceData surface_data = p_surface;

	// Add the compressed flag if its somehow missing
	if (surface_data.vertex_count > 0 && s->uncompressed_stride > 0) {
		if ((uint32_t)surface_data.vertex_data.size() < surface_data.vertex_count * s->uncompressed_stride) {
			surface_data.format |= RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
		}
	}

	_decompress_surface_data(surface_data, s);

	s->format = surface_data.format;
	s->primitive = surface_data.primitive;

	s->vertex_buffer = 0;
	s->attribute_buffer = 0;
	s->index_buffer = 0;
	s->skin_buffer = 0;

	GLint prev_array_buffer = 0;
	GLint prev_element_buffer = 0;

	// Bind our buffers
	if (GLES1::Config::get_singleton()->support_vbo) {
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGetIntegerv GL_ARRAY_BUFFER_BINDING");
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_element_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGetIntegerv GL_ELEMENT_ARRAY_BUFFER_BINDING");
	}

	// Vertex buffer
	if (surface_data.vertex_data.size()) {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &s->vertex_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (vertex buffer)");
		}

		if (s->vertex_buffer != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glBindBuffer (vertex buffer)");
			GLES1::Utilities::get_singleton()->buffer_allocate_data(
				GL_ARRAY_BUFFER,
				s->vertex_buffer,
				surface_data.vertex_data.size(),
				surface_data.vertex_data.ptr(),
				(s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW,
				"Mesh vertex buffer"
			);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (vertex buffer)");
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT_ONCE("GLES1: Failed to generate vertex buffer. Using client memory fallback.");
#endif
			s->vertex_buffer_fallback = surface_data.vertex_data;
		}

		s->vertex_buffer_size = surface_data.vertex_data.size();
	}

	// Attribute buffer
	if (surface_data.attribute_data.size()) {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &s->attribute_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (attributes buffer)");
		}

		if (s->attribute_buffer != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glBindBuffer (attributes buffer)");
			GLES1::Utilities::get_singleton()->buffer_allocate_data(
				GL_ARRAY_BUFFER,
				s->attribute_buffer,
				surface_data.attribute_data.size(),
				surface_data.attribute_data.ptr(),
				(s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW,
				"Mesh attribute buffer"
			);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (attributes buffer)");
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT_ONCE("GLES1: Failed to generate attribute buffer. Using client memory fallback.");
#endif
			s->attribute_buffer_fallback = surface_data.attribute_data;
		}

		s->attribute_buffer_size = surface_data.attribute_data.size();
	}

	// Index buffer
	if (surface_data.index_count) {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &s->index_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (indices buffer)");
		}

		if (s->index_buffer != 0) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glBindBuffer (indices buffer)");
			GLES1::Utilities::get_singleton()->buffer_allocate_data(
				GL_ELEMENT_ARRAY_BUFFER,
				s->index_buffer,
				surface_data.index_data.size(),
				surface_data.index_data.ptr(),
				GL_STATIC_DRAW,
				"Mesh index buffer"
			);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (indices buffer)");
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT_ONCE("GLES1: Failed to generate index buffer. Using client memory fallback.");
#endif
			s->index_buffer_fallback = surface_data.index_data;
		}

		s->index_count = surface_data.index_count;
		s->index_buffer_size = surface_data.index_data.size();
	}

	// Skin buffer
	if (surface_data.skin_data.size()) {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &s->skin_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glGenBuffers (skin buffer)");
		}

		if (s->skin_buffer != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, s->skin_buffer);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: glBindBuffer (skin buffer)");
			GLES1::Utilities::get_singleton()->buffer_allocate_data(
				GL_ARRAY_BUFFER,
				s->skin_buffer,
				surface_data.skin_data.size(),
				surface_data.skin_data.ptr(),
				(s->format & RS::ARRAY_FLAG_USE_DYNAMIC_UPDATE) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW,
				"Mesh skin buffer"
			);
			GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: buffer_allocate_data (skin buffer)");
		} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
			WARN_PRINT_ONCE("GLES1: Failed to generate skin buffer. Using client memory fallback.");
#endif
			s->skin_buffer_fallback = surface_data.skin_data;
		}
		s->skin_buffer_size = surface_data.skin_data.size();
	}

	// Restore bindings
	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: restore prev GL_ARRAY_BUFFER");
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prev_element_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_add_surface: restore prev GL_ELEMENT_ARRAY_BUFFER");
	}

	// Skeleton / transform properties
	s->bone_aabbs = surface_data.bone_aabbs;
	s->mesh_to_skeleton_xform = surface_data.mesh_to_skeleton_xform;
	s->uv_scale = surface_data.uv_scale;

	if (surface_data.format & RS::ARRAY_FORMAT_BONES) {
		mesh->has_bone_weights = true;
	}

	// Setup version attrib mapping
	s->version_count = 1;
	s->versions = (Mesh::Surface::Version *)memalloc(sizeof(Mesh::Surface::Version));
	ERR_FAIL_NULL(s->versions);

	_mesh_surface_generate_version_for_input_mask(s->versions[0], s, s->format);

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

	s->uncompressed_buffer.clear();
	s->uncompressed_stride = 0;

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
	ERR_FAIL_NULL(r);

	if (likely(mesh->surfaces[p_surface]->vertex_buffer != 0)) {
		// Capture
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->vertex_buffer);
		GLES1::Utilities::get_singleton()->buffer_update_data(
			GL_ARRAY_BUFFER,
			mesh->surfaces[p_surface]->vertex_buffer,
			p_offset,
			data_size,
			r
		);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_update_vertex_region: buffer_update_data");

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
		GLES1::Utilities::get_singleton()->buffer_update_data(
			GL_ARRAY_BUFFER,
			mesh->surfaces[p_surface]->attribute_buffer,
			p_offset,
			data_size,
			r
		);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_update_attribute_region: buffer_update_data");

		// Restore
		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	} else {
		uint8_t *dst = mesh->surfaces[p_surface]->attribute_buffer_fallback.ptrw();
		ERR_FAIL_NULL(dst);
		memcpy(dst + p_offset, r, data_size);
	}
}

void MeshStorage::mesh_surface_update_skin_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mesh->surface_count);
	ERR_FAIL_COND(p_data.is_empty());

	uint64_t data_size = p_data.size();
	ERR_FAIL_COND(p_offset + data_size > mesh->surfaces[p_surface]->skin_buffer_size);
	const uint8_t *r = p_data.ptr();

	if (likely(mesh->surfaces[p_surface]->skin_buffer != 0)) {
		// Capture
		GLint prev_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buffer);

		glBindBuffer(GL_ARRAY_BUFFER, mesh->surfaces[p_surface]->skin_buffer);
		GLES1::Utilities::get_singleton()->buffer_update_data(
			GL_ARRAY_BUFFER,
			mesh->surfaces[p_surface]->skin_buffer,
			p_offset,
			data_size,
			r
		);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_update_skin_region: buffer_update_data");

		// Restore state
		glBindBuffer(GL_ARRAY_BUFFER, prev_buffer);
	} else {
		uint8_t *dst = mesh->surfaces[p_surface]->skin_buffer_fallback.ptrw();
		ERR_FAIL_NULL(dst);
		memcpy(dst + p_offset, r, data_size);
	}
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
		sd.vertex_data = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.vertex_buffer, s.vertex_buffer_size);
	} else {
		sd.vertex_data = s.vertex_buffer_fallback;
	}

	if (s.attribute_buffer != 0) {
		sd.attribute_data = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.attribute_buffer, s.attribute_buffer_size);
	} else {
		sd.attribute_data = s.attribute_buffer_fallback;
	}

	if (s.skin_buffer != 0) {
		sd.skin_data = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.skin_buffer, s.skin_buffer_size);
	} else {
		sd.skin_data = s.skin_buffer_fallback;
	}

	sd.vertex_count = s.vertex_count;
	sd.index_count = s.index_count;
	sd.primitive = s.primitive;

	if (sd.index_count) {
		sd.index_data = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ELEMENT_ARRAY_BUFFER, s.index_buffer, s.index_buffer_size);
	}

	sd.aabb = s.aabb;
	for (uint32_t i = 0; i < s.lod_count; i++) {
		RS::SurfaceData::LOD lod;
		lod.edge_length = s.lods[i].edge_length;
		lod.index_data = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ELEMENT_ARRAY_BUFFER, s.lods[i].index_buffer, s.lods[i].index_buffer_size);
		sd.lods.push_back(lod);
	}

	sd.bone_aabbs = s.bone_aabbs;
	sd.mesh_to_skeleton_xform = s.mesh_to_skeleton_xform;

	if (mesh->blend_shape_count) {
		sd.blend_shape_data = Vector<uint8_t>();
		for (uint32_t i = 0; i < mesh->blend_shape_count; i++) {
			sd.blend_shape_data.append_array(GLES1::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s.blend_shapes[i].vertex_buffer, s.vertex_buffer_size));
		}
	}

	sd.uv_scale = s.uv_scale;

	return sd;
}

Array MeshStorage::mesh_surface_get_arrays(RID p_mesh, int p_surface) const {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL_V(mesh, Array());
	ERR_FAIL_UNSIGNED_INDEX_V((uint32_t)p_surface, mesh->surface_count, Array());

	Mesh::Surface *s = mesh->surfaces[p_surface];
	RS::SurfaceData sd = mesh_get_surface(p_mesh, p_surface);

	Array arrays;
	arrays.resize(RS::ARRAY_MAX);

	if (sd.vertex_count == 0) {
		return arrays;
	}

	Mesh::Surface::Version v;
	const_cast<MeshStorage *>(this)->_mesh_surface_generate_version_for_input_mask(v, s, 0xFFFFFFFF);

	const uint8_t *v_data = sd.vertex_data.is_empty() ? nullptr : sd.vertex_data.ptr();
	const uint8_t *a_data = sd.attribute_data.is_empty() ? nullptr : sd.attribute_data.ptr();

	// Vertex
	if (v.attribs[RS::ARRAY_VERTEX].enabled && v_data) {
		PackedVector3Array vertices;
		vertices.resize(sd.vertex_count);
		Vector3 *dst = vertices.ptrw();
		int stride = v.attribs[RS::ARRAY_VERTEX].stride;
		int offset = v.attribs[RS::ARRAY_VERTEX].offset;

		for (uint32_t i = 0; i < sd.vertex_count; i++) {
			const uint8_t *src = v_data + i * stride + offset;
			if (s->format & RS::ARRAY_FLAG_USE_2D_VERTICES) {
				const float *f = (const float *)src;
				dst[i] = Vector3(f[0], f[1], 0);
			} else if (s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
				dst[i] = Vector3(0, 0, 0);
			} else {
				const float *f = (const float *)src;
				dst[i] = Vector3(f[0], f[1], f[2]);
			}
		}
		arrays[RS::ARRAY_VERTEX] = vertices;
	}

	// Normal
	if (v.attribs[RS::ARRAY_NORMAL].enabled && v_data) {
		PackedVector3Array normals;
		normals.resize(sd.vertex_count);
		Vector3 *dst = normals.ptrw();
		int stride = v.attribs[RS::ARRAY_NORMAL].stride;
		int offset = v.attribs[RS::ARRAY_NORMAL].offset;

		for (uint32_t i = 0; i < sd.vertex_count; i++) {
			uint32_t read_offset = i * stride + offset;

			// Don't overflow past an offset.
			if ((read_offset + sizeof(float) * 3) > (uint32_t)(sd.vertex_data.size())) {
				dst[i] = Vector3(0, 1, 0);
				continue;
			}

			const uint8_t *src = v_data + read_offset;
			if (!(s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
				const float *f = (const float *)src;
				dst[i] = Vector3(f[0], f[1], f[2]);
			} else {
				dst[i] = Vector3(0, 1, 0);
			}
		}
		arrays[RS::ARRAY_NORMAL] = normals;
	}

	// Color
	if (v.attribs[RS::ARRAY_COLOR].enabled && a_data) {
		PackedColorArray colors;
		colors.resize(sd.vertex_count);
		Color *dst = colors.ptrw();
		int stride = v.attribs[RS::ARRAY_COLOR].stride;
		int offset = v.attribs[RS::ARRAY_COLOR].offset;

		for (uint32_t i = 0; i < sd.vertex_count; i++) {
			const uint8_t *src = a_data + i * stride + offset;
			dst[i] = Color(src[0] / 255.0f, src[1] / 255.0f, src[2] / 255.0f, src[3] / 255.0f);
		}
		arrays[RS::ARRAY_COLOR] = colors;
	}

	// tex_uv
	if (v.attribs[RS::ARRAY_TEX_UV].enabled && a_data) {
		PackedVector2Array uvs;
		uvs.resize(sd.vertex_count);
		Vector2 *dst = uvs.ptrw();
		int stride = v.attribs[RS::ARRAY_TEX_UV].stride;
		int offset = v.attribs[RS::ARRAY_TEX_UV].offset;

		for (uint32_t i = 0; i < sd.vertex_count; i++) {
			const uint8_t *src = a_data + i * stride + offset;
			if (!(s->format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
				const float *f = (const float *)src;
				dst[i] = Vector2(f[0], f[1]);
			} else {
				dst[i] = Vector2(0, 0);
			}
		}
		arrays[RS::ARRAY_TEX_UV] = uvs;
	}

	// Index
	if (sd.index_count > 0 && !sd.index_data.is_empty()) {
		PackedInt32Array indices;
		indices.resize(sd.index_count);
		int32_t *dst = indices.ptrw();
		const uint8_t *i_data = sd.index_data.ptr();

		bool use_16 = (sd.vertex_count <= 65536);
		for (uint32_t i = 0; i < sd.index_count; i++) {
			if (use_16) {
				dst[i] = ((const uint16_t *)i_data)[i];
			} else {
				dst[i] = ((const uint32_t *)i_data)[i];
			}
		}
		arrays[RS::ARRAY_INDEX] = indices;
	}

	return arrays;
}

void MeshStorage::mesh_surface_bind_arrays_gles1(void *p_surface, uint64_t p_input_mask) {
	Mesh::Surface *s = reinterpret_cast<Mesh::Surface *>(p_surface);
	ERR_FAIL_NULL(s);

	s->version_lock.lock();

	Mesh::Surface::Version *version = nullptr;
	for (uint32_t i = 0; i < s->version_count; i++) {
		if (s->versions[i].input_mask == p_input_mask) {
			version = &s->versions[i];
			break;
		}
	}

	if (!version) {
		// Generate the version if it doesn't exist yet for this input mask
		uint32_t v_idx = s->version_count;
		s->version_count++;
		s->versions = (Mesh::Surface::Version *)memrealloc(s->versions, sizeof(Mesh::Surface::Version) * s->version_count);
		ERR_FAIL_NULL(s->versions);
		_mesh_surface_generate_version_for_input_mask(s->versions[v_idx], s, p_input_mask);
		version = &s->versions[v_idx];
	}

	s->version_lock.unlock();

	bool support_vbo = GLES1::Config::get_singleton()->support_vbo;

	// Vertex data (positions, normals)
	bool use_vbo_vertex = support_vbo && s->vertex_buffer != 0;
	if (use_vbo_vertex) {
		glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_bind_arrays_gles1: glBindBuffer vertex");
	} else if (support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	// If VBOs are not supported or the buffer failed to generate, we fall back to client memory
	const uint8_t *base_ptr_vertex = use_vbo_vertex ? nullptr : s->vertex_buffer_fallback.ptr();

	// Macro helper to make things more readable
#define GL_OFFSET_PTR_VERTEX(offset) (use_vbo_vertex ? (const void *)(size_t)(offset) : (const void *)(base_ptr_vertex + (offset)))
#define GL_OFFSET_PTR_ATTRIB(offset) (use_vbo_attrib ? (const void *)(size_t)(offset) : (const void *)(base_ptr_attrib + (offset)))

	// Vertex
	if (version->attribs[RS::ARRAY_VERTEX].enabled && (use_vbo_vertex || base_ptr_vertex)) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(
			version->attribs[RS::ARRAY_VERTEX].size,
			version->attribs[RS::ARRAY_VERTEX].type,
			version->attribs[RS::ARRAY_VERTEX].stride,
			GL_OFFSET_PTR_VERTEX(version->attribs[RS::ARRAY_VERTEX].offset)
		);
	} else {
		glDisableClientState(GL_VERTEX_ARRAY);
	}

	// Normal
	if (version->attribs[RS::ARRAY_NORMAL].enabled && (use_vbo_vertex || base_ptr_vertex)) {
		glEnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(
			version->attribs[RS::ARRAY_NORMAL].type,
			version->attribs[RS::ARRAY_NORMAL].stride,
			GL_OFFSET_PTR_VERTEX(version->attribs[RS::ARRAY_NORMAL].offset)
		);
	} else {
		glDisableClientState(GL_NORMAL_ARRAY);
	}

	// --- Attribute buffer (colors, UVs) ---
	bool use_vbo_attrib = support_vbo && s->attribute_buffer != 0;
	if (use_vbo_attrib) {
		glBindBuffer(GL_ARRAY_BUFFER, s->attribute_buffer);
		GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_bind_arrays_gles1: glBindBuffer attribute");
	} else if (support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	const uint8_t *base_ptr_attrib = use_vbo_attrib ? nullptr : s->attribute_buffer_fallback.ptr();

	// Color
	if (version->attribs[RS::ARRAY_COLOR].enabled && (use_vbo_attrib || base_ptr_attrib)) {
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(
			4,
			GL_UNSIGNED_BYTE,
			version->attribs[RS::ARRAY_COLOR].stride,
			GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_COLOR].offset)
		);
	} else {
		glDisableClientState(GL_COLOR_ARRAY);
	}
	glClientActiveTexture(GL_TEXTURE0);

	// tex_uv
	if (version->attribs[RS::ARRAY_TEX_UV].enabled && (use_vbo_attrib || base_ptr_attrib)) {
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(
			version->attribs[RS::ARRAY_TEX_UV].size,
			version->attribs[RS::ARRAY_TEX_UV].type,
			version->attribs[RS::ARRAY_TEX_UV].stride,
			GL_OFFSET_PTR_ATTRIB(version->attribs[RS::ARRAY_TEX_UV].offset)
		);
	} else {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	glClientActiveTexture(GL_TEXTURE1);

	// Hijack GL_TEXTURE1 for tangents
	if (version->attribs[RS::ARRAY_TANGENT].enabled && (use_vbo_vertex || base_ptr_vertex)) {
		// Restore the vertex buffer binding before evaluating the tangent pointer
		if (use_vbo_vertex) {
			glBindBuffer(GL_ARRAY_BUFFER, s->vertex_buffer);
		} else if (support_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(
			version->attribs[RS::ARRAY_TANGENT].size,
			version->attribs[RS::ARRAY_TANGENT].type,
			version->attribs[RS::ARRAY_TANGENT].stride,
			GL_OFFSET_PTR_VERTEX(version->attribs[RS::ARRAY_TANGENT].offset)
		);
	} else {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

#undef GL_OFFSET_PTR_ATTRIB
#undef GL_OFFSET_PTR_VERTEX

	// Reset active client texture state for subsequent operations
	glClientActiveTexture(GL_TEXTURE0);

	GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_bind_arrays_gles1: pointers bound");
}

void MeshStorage::mesh_surface_unbind_arrays_gles1(void *p_surface) {
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glClientActiveTexture(GL_TEXTURE0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	GL_CHECK_ERROR("GLES1::MeshStorage::mesh_surface_unbind_arrays_gles1: pointers unbound");
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
	if (s->format & RS::ARRAY_FLAG_USE_2D_VERTICES) {
		_mesh_surface_generate_version_for_input_mask_2d(v, s, p_input_mask, mis);
	} else {
		_mesh_surface_generate_version_for_input_mask_3d(v, s, p_input_mask, mis);
	}
}

void MeshStorage::_mesh_surface_generate_version_for_input_mask_2d(Mesh::Surface::Version &v, Mesh::Surface *s, uint64_t p_input_mask, MeshInstance::Surface *mis) {
	int position_stride = 0;
	int normal_tangent_stride = 0;
	int attributes_stride = 0;
	int skin_stride = 0;

	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		v.attribs[i].enabled = false;
		v.attribs[i].integer = false; // GLES1 does not utilize integer attrib pointers
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
					position_stride = 2 * sizeof(float);
				} else {
					v.attribs[i].size = 3;
					position_stride = 3 * sizeof(float);
				}
			} break;
			case RS::ARRAY_NORMAL: {
				v.attribs[i].size = 3;
				v.attribs[i].type = GL_FLOAT;
				v.attribs[i].normalized = GL_FALSE;

				if (mis) {
					// Skeleton animation output is interleaved
					v.attribs[i].offset = position_stride;
					normal_tangent_stride = position_stride + (3 * sizeof(float));
					position_stride = normal_tangent_stride;
				} else {
					// Standard static mesh output is planar
					v.attribs[i].offset = position_stride * s->vertex_count;
					normal_tangent_stride = 3 * sizeof(float);
				}
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
			case RS::ARRAY_CUSTOM2:
			case RS::ARRAY_CUSTOM3: {
				v.attribs[i].offset = attributes_stride;
				int idx = i - RS::ARRAY_CUSTOM0;
				uint32_t fmt_shift[RS::ARRAY_CUSTOM_COUNT] = { RS::ARRAY_FORMAT_CUSTOM0_SHIFT, RS::ARRAY_FORMAT_CUSTOM1_SHIFT, RS::ARRAY_FORMAT_CUSTOM2_SHIFT, RS::ARRAY_FORMAT_CUSTOM3_SHIFT };
				uint32_t fmt = (s->format >> fmt_shift[idx]) & RS::ARRAY_FORMAT_CUSTOM_MASK;
				uint32_t fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 4, 8, 4, 8, 12, 16 };
				GLenum gl_type[RS::ARRAY_CUSTOM_MAX] = { GL_UNSIGNED_BYTE, GL_BYTE, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT };
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
				v.attribs[i].normalized = GL_FALSE; // Interpreted directly as float index by our shader
				v.attribs[i].integer = false; // Prevents the system from expecting glVertexAttribIPointer
				skin_stride += 4 * sizeof(uint16_t);
			} break;
			case RS::ARRAY_CUSTOM1:
			case RS::ARRAY_WEIGHTS: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].size = 4;
				v.attribs[i].type = GL_UNSIGNED_SHORT;
				v.attribs[i].normalized = GL_FALSE;
				v.attribs[i].integer = false;
				skin_stride += 4 * sizeof(uint16_t);
			} break;
		}
	}

	for (int i = 0; i < RS::ARRAY_INDEX; i++) {
		if (!v.attribs[i].enabled) {
			continue;
		}
		if (i <= RS::ARRAY_TANGENT) {
			if (mis) {
				v.attribs[i].stride = position_stride; // Interleaved for skeletal
			} else {
				v.attribs[i].stride = (i == RS::ARRAY_VERTEX) ? position_stride : normal_tangent_stride;
			}
		} else if (i >= RS::ARRAY_CUSTOM0 && i <= RS::ARRAY_CUSTOM1) {
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
				v.attribs[i].offset = attribute_stride;
				int idx = i - RS::ARRAY_CUSTOM0;
				uint32_t fmt_shift[RS::ARRAY_CUSTOM_COUNT] = { RS::ARRAY_FORMAT_CUSTOM0_SHIFT, RS::ARRAY_FORMAT_CUSTOM1_SHIFT, RS::ARRAY_FORMAT_CUSTOM2_SHIFT, RS::ARRAY_FORMAT_CUSTOM3_SHIFT };
				uint32_t fmt = (s->format >> fmt_shift[idx]) & RS::ARRAY_FORMAT_CUSTOM_MASK;
				uint32_t fmtsize[RS::ARRAY_CUSTOM_MAX] = { 4, 4, 4, 8, 4, 8, 12, 16 };
				GLenum gl_type[RS::ARRAY_CUSTOM_MAX] = { GL_UNSIGNED_BYTE, GL_BYTE, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT };
				GLboolean norm[RS::ARRAY_CUSTOM_MAX] = { GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };
				v.attribs[i].type = gl_type[fmt];
				v.attribs[i].size = fmtsize[fmt] / (gl_type[fmt] == GL_UNSIGNED_BYTE || gl_type[fmt] == GL_BYTE ? 1 : 4);
				v.attribs[i].normalized = norm[fmt];
				attribute_stride += fmtsize[fmt];
			} break;
			case RS::ARRAY_BONES: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].type = GL_UNSIGNED_SHORT;
				v.attribs[i].size = (s->format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS) ? 8 : 4;
				v.attribs[i].normalized = GL_FALSE;
				skin_stride += v.attribs[i].size * sizeof(uint16_t);
			} break;
			case RS::ARRAY_WEIGHTS: {
				v.attribs[i].offset = skin_stride;
				v.attribs[i].type = GL_UNSIGNED_SHORT;
				v.attribs[i].size = (s->format & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS) ? 8 : 4;
				v.attribs[i].normalized = GL_TRUE;
				skin_stride += v.attribs[i].size * sizeof(uint16_t);
			} break;
		}
	}

	for (int i = RS::ARRAY_COLOR; i < RS::ARRAY_INDEX; i++) {
		if (!v.attribs[i].enabled) {
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
}

void MeshStorage::_decompress_surface_data_2d(RS::SurfaceData &r_surface, Mesh::Surface *s) {
	uint32_t v_count = r_surface.vertex_count;
	uint64_t format = r_surface.format;

	bool is_compressed = format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
	bool has_color = format & RS::ARRAY_FORMAT_COLOR;
	bool has_uv = format & RS::ARRAY_FORMAT_TEX_UV;
	bool has_uv2 = format & RS::ARRAY_FORMAT_TEX_UV2;

	uint32_t src_pos_offset = 0;

	uint32_t src_a_stride = 0;
	uint32_t src_col_offset = 0;
	if (has_color) {
		src_col_offset = src_a_stride;
		src_a_stride += 4;
	}

	uint32_t src_uv_offset = 0;
	if (has_uv) {
		src_uv_offset = src_a_stride;
		src_a_stride += is_compressed ? 4 : 8;
	}

	uint32_t src_uv2_offset = 0;
	if (has_uv2) {
		src_uv2_offset = src_a_stride;
		src_a_stride += is_compressed ? 4 : 8;
	}

	uint32_t actual_v_stride = r_surface.vertex_data.size() / v_count;
	uint32_t actual_a_stride = r_surface.attribute_data.size() > 0 ? (r_surface.attribute_data.size() / v_count) : 0;

	uint32_t dst_pos_bytes = 8;
	uint32_t dst_v_stride = dst_pos_bytes;

	uint32_t dst_col_bytes = has_color ? 4 : 0;
	uint32_t dst_uv_bytes = has_uv ? 8 : 0;
	uint32_t dst_uv2_bytes = has_uv2 ? 8 : 0;
	uint32_t dst_a_stride = dst_col_bytes + dst_uv_bytes + dst_uv2_bytes;

	Vector<uint8_t> new_v_data;
	new_v_data.resize(v_count * dst_v_stride);

	Vector<uint8_t> new_a_data;
	if (dst_a_stride > 0) {
		new_a_data.resize(v_count * dst_a_stride);
	}

	const uint8_t *src_v = r_surface.vertex_data.ptr();
	const uint8_t *src_a = r_surface.attribute_data.ptr();

	uint8_t *dst_v = new_v_data.ptrw();
	uint8_t *dst_a = new_a_data.ptrw();

	for (uint32_t i = 0; i < v_count; i++) {
		const uint8_t *sv = src_v + (i * actual_v_stride);
		uint8_t *dv = dst_v + (i * dst_v_stride);

		// Copy 2D Positions
		memcpy(dv, sv + src_pos_offset, dst_pos_bytes);

		if (src_a && dst_a_stride > 0) {
			const uint8_t *sa = src_a + (i * actual_a_stride);
			uint8_t *da = dst_a + (i * dst_a_stride);
			uint32_t dst_a_offset = 0;

			if (has_color) {
				da[dst_a_offset + 0] = sa[src_col_offset + 0];
				da[dst_a_offset + 1] = sa[src_col_offset + 1];
				da[dst_a_offset + 2] = sa[src_col_offset + 2];
				da[dst_a_offset + 3] = sa[src_col_offset + 3];
				dst_a_offset += 4;
			}

			if (has_uv) {
				float *du = (float *)(da + dst_a_offset);
				if (is_compressed) {
					const uint16_t *uv_ptr = (const uint16_t *)(sa + src_uv_offset);
					du[0] = Math::half_to_float(uv_ptr[0]);
					du[1] = Math::half_to_float(uv_ptr[1]);
				} else {
					memcpy(du, sa + src_uv_offset, 8);
				}
				dst_a_offset += 8;
			}

			if (has_uv2) {
				float *du = (float *)(da + dst_a_offset);
				if (is_compressed) {
					const uint16_t *uv2_ptr = (const uint16_t *)(sa + src_uv2_offset);
					du[0] = Math::half_to_float(uv2_ptr[0]);
					du[1] = Math::half_to_float(uv2_ptr[1]);
				} else {
					memcpy(du, sa + src_uv2_offset, 8);
				}
			}
		}
	}

	r_surface.vertex_data = new_v_data;

	if (new_a_data.size() > 0) {
		r_surface.attribute_data = new_a_data;
	} else {
		r_surface.attribute_data.clear();
	}

	r_surface.format &= ~RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
	r_surface.format &= ~RS::ARRAY_FORMAT_CUSTOM0;
	r_surface.format &= ~RS::ARRAY_FORMAT_CUSTOM1;
	r_surface.format &= ~RS::ARRAY_FORMAT_CUSTOM2;
	r_surface.format &= ~RS::ARRAY_FORMAT_CUSTOM3;
}

void MeshStorage::_decompress_3d_buffer(const uint8_t *p_src, uint8_t *p_dst, uint32_t p_vertex_count, uint64_t p_format, uint32_t p_compressed_stride, uint32_t p_uncompressed_stride, const AABB &p_aabb) {
	bool has_vertex = p_format & RS::ARRAY_FORMAT_VERTEX;
	bool has_normal = p_format & RS::ARRAY_FORMAT_NORMAL;
	bool has_tangent = p_format & RS::ARRAY_FORMAT_TANGENT;

	// Source offsets (Planar for position, Interleaved for normals/tangents)
	uint32_t src_pos_base = 0;

	// The attribute block starts after the planar position block
	const uint8_t *src_attr_base = p_src + (has_vertex ? (p_vertex_count * 12) : 0);
	uint32_t src_attr_stride = (has_normal ? 4 : 0) + (has_tangent ? 4 : 0);

	uint32_t src_norm_offset = 0;
	uint32_t src_tang_offset = has_normal ? 4 : 0;

	// Destination offsets (uncompressed, fully interleaved layout)
	uint32_t dst_pos_offset = 0;
	uint32_t dst_norm_offset = has_vertex ? 12 : 0;
	uint32_t dst_tang_offset = dst_norm_offset + (has_normal ? 12 : 0);

	for (uint32_t i = 0; i < p_vertex_count; i++) {
		const uint8_t *src_a = src_attr_base + (i * src_attr_stride);
		uint8_t *dst_v = p_dst + (i * p_uncompressed_stride);

		if (has_vertex) {
			// Read from the planar position block
			const float *pos_src = (const float *)(p_src + src_pos_base + (i * 12));
			float *pos_dst = (float *)(dst_v + dst_pos_offset);

			pos_dst[0] = pos_src[0];
			pos_dst[1] = pos_src[1];
			pos_dst[2] = pos_src[2];
		}

		if (has_normal) {
			// Read from the compressed interleaved attribute layout
			const uint16_t *norm_src = (const uint16_t *)(src_a + src_norm_offset);
			float *norm_dst = (float *)(dst_v + dst_norm_offset);

			Vector3 normal = _gles1_decode_octahedral_normal(norm_src[0], norm_src[1]);
			norm_dst[0] = normal.x;
			norm_dst[1] = normal.y;
			norm_dst[2] = normal.z;
		}

		if (has_tangent) {
			// Read from the compressed interleaved attribute layout
			const uint16_t *tang_src = (const uint16_t *)(src_a + src_tang_offset);
			float *tang_dst = (float *)(dst_v + dst_tang_offset);

			Vector3 tangent;
			float binormal_sign;
			_gles1_decode_octahedral_tangent(tang_src[0], tang_src[1], tangent, binormal_sign);

			tang_dst[0] = tangent.x;
			tang_dst[1] = tangent.y;
			tang_dst[2] = tangent.z;
			tang_dst[3] = binormal_sign;
		}
	}
}

void MeshStorage::_decompress_surface_data_3d(RS::SurfaceData &r_surface, Mesh::Surface *s) {
	if (!(r_surface.format & RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES)) {
		return;
	}

	uint32_t vertex_count = r_surface.vertex_count;
	if (vertex_count == 0 || r_surface.vertex_data.is_empty()) {
		return;
	}

	uint32_t compressed_stride = r_surface.vertex_data.size() / vertex_count;
	uint32_t uncompressed_stride = s->uncompressed_stride;

	_decompress_3d_buffer(r_surface.vertex_data.ptr(), s->uncompressed_buffer.ptrw(), vertex_count, r_surface.format, compressed_stride, uncompressed_stride, r_surface.aabb);
	r_surface.vertex_data = s->uncompressed_buffer;

	if (!r_surface.blend_shape_data.is_empty()) {
		uint32_t bs_count = r_surface.blend_shape_data.size() / (vertex_count * compressed_stride);
		Vector<uint8_t> new_bs_data;
		new_bs_data.resize(bs_count * vertex_count * uncompressed_stride);

		const uint8_t *bs_src = r_surface.blend_shape_data.ptr();
		uint8_t *bs_dst = new_bs_data.ptrw();

		for (uint32_t i = 0; i < bs_count; i++) {
			_decompress_3d_buffer(bs_src + i * vertex_count * compressed_stride, bs_dst + i * vertex_count * uncompressed_stride, vertex_count, r_surface.format, compressed_stride, uncompressed_stride, r_surface.aabb);
		}
		r_surface.blend_shape_data = new_bs_data;
	}

	r_surface.format &= ~RS::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
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
		if (GLES1::Config::get_singleton()->support_vbo) {
			if (mi->surfaces[i].vertex_buffer != 0) {
				GLES1::Utilities::get_singleton()->buffer_free_data(mi->surfaces[i].vertex_buffer);
			}
			if (mi->surfaces[i].vertex_buffers[0] != 0) {
				GLES1::Utilities::get_singleton()->buffer_free_data(mi->surfaces[i].vertex_buffers[0]);
			}
			if (mi->surfaces[i].vertex_buffers[1] != 0) {
				GLES1::Utilities::get_singleton()->buffer_free_data(mi->surfaces[i].vertex_buffers[1]);
			}
		}

		mi->surfaces[i].vertex_buffer_fallback.clear();
		mi->surfaces[i].vertex_buffers_fallback[0].clear();
		mi->surfaces[i].vertex_buffers_fallback[1].clear();

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
		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &s.vertex_buffer);
			glBindBuffer(GL_ARRAY_BUFFER, s.vertex_buffer);
			GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s.vertex_buffer, buffer_size, nullptr, GL_DYNAMIC_DRAW, "MeshInstance vertex buffer");
			if (mesh->blend_shape_count > 0) {
				glGenBuffers(2, s.vertex_buffers);
				for (uint32_t i = 0; i < 2; i++) {
					glBindBuffer(GL_ARRAY_BUFFER, s.vertex_buffers[i]);
					GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, s.vertex_buffers[i], buffer_size, nullptr, GL_DYNAMIC_DRAW, "MeshInstance process buffer");
				}
			}
			glBindBuffer(GL_ARRAY_BUFFER, 0); //unbind
		} else {
			s.vertex_buffer_fallback.resize(buffer_size);
			if (mesh->blend_shape_count > 0) {
				s.vertex_buffers_fallback[0].resize(buffer_size);
				s.vertex_buffers_fallback[1].resize(buffer_size);
			}
		}
	}

	mi->surfaces.push_back(s);
	mi->dirty = true;
}

void MeshStorage::_mesh_instance_remove_surface(MeshInstance *mi, int p_surface) {
	ERR_FAIL_UNSIGNED_INDEX((uint32_t)p_surface, mi->surfaces.size());

	if (GLES1::Config::get_singleton()->support_vbo) {
		if (mi->surfaces[p_surface].vertex_buffer != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(mi->surfaces[p_surface].vertex_buffer);
		}
		if (mi->surfaces[p_surface].vertex_buffers[0] != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(mi->surfaces[p_surface].vertex_buffers[0]);
		}
		if (mi->surfaces[p_surface].vertex_buffers[1] != 0) {
			GLES1::Utilities::get_singleton()->buffer_free_data(mi->surfaces[p_surface].vertex_buffers[1]);
		}
	}

	// Free the host memory for the version trackers.
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

void MeshStorage::_compute_skeleton(MeshInstance *p_mi, Skeleton *p_sk, uint32_t p_surface) {
	ERR_FAIL_NULL(p_mi);
	ERR_FAIL_NULL(p_sk);

	// CPU-side Software Skinning
	Mesh::Surface *s = p_mi->mesh->surfaces[p_surface];
	Vector<uint8_t> src_vertices;
	Vector<uint8_t> src_skin;

	if (GLES1::Config::get_singleton()->support_vbo) {
		if (s->vertex_buffer != 0) {
			src_vertices = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->vertex_buffer, s->vertex_buffer_size);
		}
		if (s->skin_buffer != 0) {
			src_skin = GLES1::Utilities::get_singleton()->buffer_get_data(GL_ARRAY_BUFFER, s->skin_buffer, s->skin_buffer_size);
		}
	} else {
		src_vertices = s->vertex_buffer_fallback;
		src_skin = s->skin_buffer_fallback;
	}

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

	if (GLES1::Config::get_singleton()->support_vbo && p_mi->surfaces[p_surface].vertex_buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, p_mi->surfaces[p_surface].vertex_buffer);
		GLES1::Utilities::get_singleton()->buffer_update_data(
			GL_ARRAY_BUFFER,
			p_mi->surfaces[p_surface].vertex_buffer,
			0,
			dst_vertices.size(),
			dst_vertices.ptr()
		);
		GL_CHECK_ERROR("GLES1::MeshStorage::_compute_skeleton: buffer_update_data");
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES1::MeshStorage::_compute_skeleton: CPU Fallback: glBindBuffer");
	} else {
		p_mi->surfaces[p_surface].vertex_buffer_fallback = dst_vertices;
	}
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
			if (mi->surfaces[i].vertex_buffer == 0 && mi->surfaces[i].vertex_buffer_fallback.is_empty()) {
				continue;
			}

			bool array_is_2d = mi->surfaces[i].format_cache & RS::ARRAY_FLAG_USE_2D_VERTICES;
			bool can_use_skeleton = sk != nullptr && sk->use_2d == array_is_2d && (mi->surfaces[i].format_cache & RS::ARRAY_FORMAT_BONES);
			bool use_8_weights = mi->surfaces[i].format_cache & RS::ARRAY_FLAG_USE_8_BONE_WEIGHTS;

			if (mi->mesh->blend_shape_count) {
				SkeletonShaderGLES1::ShaderVariant variant = SkeletonShaderGLES1::MODE_BASE_PASS;
				uint64_t specialization = 0;
				specialization |= array_is_2d ? SkeletonShaderGLES1::MODE_2D : 0;
				specialization |= SkeletonShaderGLES1::USE_BLEND_SHAPES;

				bool success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
				if (success) {
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::BLEND_WEIGHT, base_weight, skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::BLEND_SHAPE_COUNT, float(mi->mesh->blend_shape_count), skeleton_shader.shader_version, variant, specialization);

#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
					WARN_PRINT_ONCE("GLES1: Blend shapes are not supported natively on this backend.");
#endif

					variant = SkeletonShaderGLES1::MODE_BLEND_PASS;
					success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
					if (success) {
						for (uint32_t bs = 0; bs < mi->mesh->blend_shape_count - 1; bs++) {
							float weight = mi->blend_weights[bs];
							if (Math::is_zero_approx(weight)) {
								continue;
							}
							skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::BLEND_WEIGHT, weight, skeleton_shader.shader_version, variant, specialization);
						}

						uint32_t bs = mi->mesh->blend_shape_count - 1;
						float weight = mi->blend_weights[bs];

						specialization |= can_use_skeleton ? SkeletonShaderGLES1::USE_SKELETON : 0;
						specialization |= (can_use_skeleton && use_8_weights) ? SkeletonShaderGLES1::USE_EIGHT_WEIGHTS : 0;
						specialization |= SkeletonShaderGLES1::FINAL_PASS;

						success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
						if (success) {
							skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::BLEND_WEIGHT, weight, skeleton_shader.shader_version, variant, specialization);

							if (can_use_skeleton) {
								Transform2D transform = mi->canvas_item_transform_2d.affine_inverse() * sk->base_transform_2d;
								skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::SKELETON_TRANSFORM_X, transform[0], skeleton_shader.shader_version, variant, specialization);
								skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::SKELETON_TRANSFORM_Y, transform[1], skeleton_shader.shader_version, variant, specialization);
								skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::SKELETON_TRANSFORM_OFFSET, transform[2], skeleton_shader.shader_version, variant, specialization);

								Transform2D inverse_transform = transform.affine_inverse();
								skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::INVERSE_TRANSFORM_X, inverse_transform[0], skeleton_shader.shader_version, variant, specialization);
								skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::INVERSE_TRANSFORM_Y, inverse_transform[1], skeleton_shader.shader_version, variant, specialization);
								skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::INVERSE_TRANSFORM_OFFSET, inverse_transform[2], skeleton_shader.shader_version, variant, specialization);

								bool hw_skinning = GLES1::Config::get_singleton()->support_matrix_palette && !use_8_weights;
								if (!hw_skinning) {
									_compute_skeleton(mi, sk, i);
								}
								can_use_skeleton = false;
							}
						}
					}
				}
			}

			if (can_use_skeleton) {
				SkeletonShaderGLES1::ShaderVariant variant = SkeletonShaderGLES1::MODE_BASE_PASS;
				uint64_t specialization = 0;
				specialization |= array_is_2d ? SkeletonShaderGLES1::MODE_2D : 0;
				specialization |= SkeletonShaderGLES1::USE_SKELETON;
				specialization |= SkeletonShaderGLES1::FINAL_PASS;
				specialization |= use_8_weights ? SkeletonShaderGLES1::USE_EIGHT_WEIGHTS : 0;

				bool success = skeleton_shader.shader.version_bind_shader(skeleton_shader.shader_version, variant, specialization);
				if (success) {
					Transform2D transform = mi->canvas_item_transform_2d.affine_inverse() * sk->base_transform_2d;
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::SKELETON_TRANSFORM_X, transform[0], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::SKELETON_TRANSFORM_Y, transform[1], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::SKELETON_TRANSFORM_OFFSET, transform[2], skeleton_shader.shader_version, variant, specialization);

					Transform2D inverse_transform = transform.affine_inverse();
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::INVERSE_TRANSFORM_X, inverse_transform[0], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::INVERSE_TRANSFORM_Y, inverse_transform[1], skeleton_shader.shader_version, variant, specialization);
					skeleton_shader.shader.version_set_uniform(SkeletonShaderGLES1::INVERSE_TRANSFORM_OFFSET, inverse_transform[2], skeleton_shader.shader_version, variant, specialization);

					bool hw_skinning = GLES1::Config::get_singleton()->support_matrix_palette && !use_8_weights;
					if (!hw_skinning) {
						_compute_skeleton(mi, sk, i);
					}
				}
			}
		}

		mi->dirty = false;
		if (sk) {
			mi->skeleton_version = sk->version;
		}
		dirty_mesh_instance_arrays.remove(&mi->array_update_list);
	}
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

	if (GLES1::Config::get_singleton()->support_vbo) {
		// Create a backing GL buffer just in case it's used for 3D later.
		glGenBuffers(1, &multimesh->buffer);
	}

	if (multimesh->buffer != 0) {
		GLint prev_array_buffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

		// Pre-register empty size to tracking cache
		glBindBuffer(GL_ARRAY_BUFFER, multimesh->buffer);
		GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, multimesh->buffer, 0, nullptr, GL_DYNAMIC_DRAW, "MultiMesh buffer");

		glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
	} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
		WARN_PRINT_ONCE("GLES1: Failed to generate MultiMesh buffer. Using client memory fallback.");
#endif
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
		GLES1::Utilities::get_singleton()->buffer_update_data(
			GL_ARRAY_BUFFER,
			multimesh->buffer,
			0,
			p_buffer.size() * sizeof(float),
			p_buffer.ptr()
		);
		GL_CHECK_ERROR("GLES1::MeshStorage::_multimesh_set_buffer: buffer_update_data");

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

		if (multimesh) {
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

	skeleton->data.clear();

	if (skeleton->size) {
		// Just allocate local memory block.
		// Matrix Palettes or CPU skinner will read directly from it.
		skeleton->data.resize(p_bones * (p_2d_skeleton ? 8 : 12));
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

	while (skeleton_dirty_list) {
		Skeleton *skeleton = skeleton_dirty_list;

		skeleton_dirty_list = skeleton->dirty_list;
		skeleton->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_SKELETON_BONES);
		skeleton->version++;
		skeleton->dirty = false;
		skeleton->dirty_list = nullptr;
	}

	skeleton_dirty_list = nullptr;
}

void MeshStorage::skeleton_update_dependency(RID p_skeleton, DependencyTracker *p_instance) {
	Skeleton *skeleton = skeleton_owner.get_or_null(p_skeleton);
	ERR_FAIL_NULL(skeleton);

	p_instance->update_dependency(&skeleton->dependency);
}

#endif // GLES1_ENABLED
