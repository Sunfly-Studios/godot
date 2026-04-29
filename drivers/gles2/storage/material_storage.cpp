/**************************************************************************/
/*  material_storage.cpp                                                  */
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

#include "core/config/project_settings.h"

#include "config.h"
#include "material_storage.h"
#include "particles_storage.h"
#include "texture_storage.h"

#include "drivers/gles_common/error_macros.h"
#include "drivers/gles2/rasterizer_canvas_gles2.h"
#include "drivers/gles2/rasterizer_gles2.h"
#include "servers/rendering/storage/variant_converters.h"

using namespace GLES2;

///////////////////////////////////////////////////////////////////////////
// UBI helper functions


_FORCE_INLINE_ static void _fill_std140_ubo_empty(ShaderLanguage::DataType type, int p_array_size, uint8_t *data) {
	if (p_array_size <= 0) {
		p_array_size = 1;
	}

	switch (type) {
		case ShaderLanguage::TYPE_BOOL:
		case ShaderLanguage::TYPE_INT:
		case ShaderLanguage::TYPE_UINT:
		case ShaderLanguage::TYPE_FLOAT: {
			memset(data, 0, 4 * p_array_size);
		} break;
		case ShaderLanguage::TYPE_BVEC2:
		case ShaderLanguage::TYPE_IVEC2:
		case ShaderLanguage::TYPE_UVEC2:
		case ShaderLanguage::TYPE_VEC2: {
			memset(data, 0, 8 * p_array_size);
		} break;
		case ShaderLanguage::TYPE_BVEC3:
		case ShaderLanguage::TYPE_IVEC3:
		case ShaderLanguage::TYPE_UVEC3:
		case ShaderLanguage::TYPE_VEC3: {
			memset(data, 0, 12 * p_array_size);
		} break;
		case ShaderLanguage::TYPE_BVEC4:
		case ShaderLanguage::TYPE_IVEC4:
		case ShaderLanguage::TYPE_UVEC4:
		case ShaderLanguage::TYPE_VEC4: {
			memset(data, 0, 16 * p_array_size);
		} break;
		case ShaderLanguage::TYPE_MAT2: {
			float *m = (float *)data;
			for (int i = 0; i < p_array_size; i++) {
				int ofs = i * 8;
				memset(&m[ofs], 0, 32); // 8 floats per mat2 in std140
				m[ofs + 0] = 1.0f; // col 0, row 0
				m[ofs + 5] = 1.0f; // col 1, row 1
			}
		} break;
		case ShaderLanguage::TYPE_MAT3: {
			float *m = (float *)data;
			for (int i = 0; i < p_array_size; i++) {
				int ofs = i * 12;
				memset(&m[ofs], 0, 48); // 12 floats per mat3 in std140
				m[ofs + 0] = 1.0f; // col 0, row 0
				m[ofs + 5] = 1.0f; // col 1, row 1
				m[ofs + 10] = 1.0f; // col 2, row 2
			}
		} break;
		case ShaderLanguage::TYPE_MAT4: {
			float *m = (float *)data;
			for (int i = 0; i < p_array_size; i++) {
				int ofs = i * 16;
				memset(&m[ofs], 0, 64); // 16 floats per mat4 in std140
				m[ofs + 0] = 1.0f; // col 0, row 0
				m[ofs + 5] = 1.0f; // col 1, row 1
				m[ofs + 10] = 1.0f; // col 2, row 2
				m[ofs + 15] = 1.0f; // col 3, row 3
			}
		} break;

		default: {
		}
	}
}

static void _fill_std140_variant_ubo_value(ShaderLanguage::DataType type, int p_array_size, const Variant &value, uint8_t *data) {
	switch (type) {
		case ShaderLanguage::TYPE_BOOL: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				PackedInt32Array ba = value;
				for (int i = 0; i < ba.size(); i++) {
					ba.set(i, ba[i] ? 1 : 0);
				}
				write_array_std140<int32_t>(ba, gui, p_array_size, 4);
			} else {
				bool v = value;
				gui[0] = v ? 1 : 0;
			}
		} break;
		case ShaderLanguage::TYPE_BVEC2: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				PackedInt32Array ba = convert_array_std140<Vector2i, int32_t>(value);
				for (int i = 0; i < ba.size(); i++) {
					ba.set(i, ba[i] ? 1 : 0);
				}
				write_array_std140<Vector2i>(ba, gui, p_array_size, 4);
			} else {
				uint32_t v = value;
				gui[0] = v & 1 ? 1 : 0;
				gui[1] = v & 2 ? 1 : 0;
			}
		} break;
		case ShaderLanguage::TYPE_BVEC3: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				PackedInt32Array ba = convert_array_std140<Vector3i, int32_t>(value);
				for (int i = 0; i < ba.size(); i++) {
					ba.set(i, ba[i] ? 1 : 0);
				}
				write_array_std140<Vector3i>(ba, gui, p_array_size, 4);
			} else {
				uint32_t v = value;
				gui[0] = (v & 1) ? 1 : 0;
				gui[1] = (v & 2) ? 1 : 0;
				gui[2] = (v & 4) ? 1 : 0;
			}
		} break;
		case ShaderLanguage::TYPE_BVEC4: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				PackedInt32Array ba = convert_array_std140<Vector4i, int32_t>(value);
				for (int i = 0; i < ba.size(); i++) {
					ba.set(i, ba[i] ? 1 : 0);
				}
				write_array_std140<Vector4i>(ba, gui, p_array_size, 4);
			} else {
				uint32_t v = value;
				gui[0] = (v & 1) ? 1 : 0;
				gui[1] = (v & 2) ? 1 : 0;
				gui[2] = (v & 4) ? 1 : 0;
				gui[3] = (v & 8) ? 1 : 0;
			}
		} break;
		case ShaderLanguage::TYPE_INT: {
			int32_t *gui = (int32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = value;
				write_array_std140<int32_t>(iv, gui, p_array_size, 4);
			} else {
				int v = value;
				gui[0] = v;
			}
		} break;
		case ShaderLanguage::TYPE_IVEC2: {
			int32_t *gui = (int32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = convert_array_std140<Vector2i, int32_t>(value);
				write_array_std140<Vector2i>(iv, gui, p_array_size, 4);
			} else {
				Vector2i v = convert_to_vector<Vector2i>(value);
				gui[0] = v.x;
				gui[1] = v.y;
			}
		} break;
		case ShaderLanguage::TYPE_IVEC3: {
			int32_t *gui = (int32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = convert_array_std140<Vector3i, int32_t>(value);
				write_array_std140<Vector3i>(iv, gui, p_array_size, 4);
			} else {
				Vector3i v = convert_to_vector<Vector3i>(value);
				gui[0] = v.x;
				gui[1] = v.y;
				gui[2] = v.z;
			}
		} break;
		case ShaderLanguage::TYPE_IVEC4: {
			int32_t *gui = (int32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = convert_array_std140<Vector4i, int32_t>(value);
				write_array_std140<Vector4i>(iv, gui, p_array_size, 4);
			} else {
				Vector4i v = convert_to_vector<Vector4i>(value);
				gui[0] = v.x;
				gui[1] = v.y;
				gui[2] = v.z;
				gui[3] = v.w;
			}
		} break;
		case ShaderLanguage::TYPE_UINT: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = value;
				write_array_std140<uint32_t>(iv, gui, p_array_size, 4);
			} else {
				int v = value;
				gui[0] = v;
			}
		} break;
		case ShaderLanguage::TYPE_UVEC2: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = convert_array_std140<Vector2i, int32_t>(value);
				write_array_std140<Vector2i>(iv, gui, p_array_size, 4);
			} else {
				Vector2i v = convert_to_vector<Vector2i>(value);
				gui[0] = v.x;
				gui[1] = v.y;
			}
		} break;
		case ShaderLanguage::TYPE_UVEC3: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = convert_array_std140<Vector3i, int32_t>(value);
				write_array_std140<Vector3i>(iv, gui, p_array_size, 4);
			} else {
				Vector3i v = convert_to_vector<Vector3i>(value);
				gui[0] = v.x;
				gui[1] = v.y;
				gui[2] = v.z;
			}
		} break;
		case ShaderLanguage::TYPE_UVEC4: {
			uint32_t *gui = (uint32_t *)data;

			if (p_array_size > 0) {
				const PackedInt32Array &iv = convert_array_std140<Vector4i, int32_t>(value);
				write_array_std140<Vector4i>(iv, gui, p_array_size, 4);
			} else {
				Vector4i v = convert_to_vector<Vector4i>(value);
				gui[0] = v.x;
				gui[1] = v.y;
				gui[2] = v.z;
				gui[3] = v.w;
			}
		} break;
		case ShaderLanguage::TYPE_FLOAT: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = value;
				write_array_std140<float>(a, gui, p_array_size, 4);
			} else {
				float v = value;
				gui[0] = v;
			}
		} break;
		case ShaderLanguage::TYPE_VEC2: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = convert_array_std140<Vector2, float>(value);
				write_array_std140<Vector2>(a, gui, p_array_size, 4);
			} else {
				Vector2 v = convert_to_vector<Vector2>(value);
				gui[0] = v.x;
				gui[1] = v.y;
			}
		} break;
		case ShaderLanguage::TYPE_VEC3: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = convert_array_std140<Vector3, float>(value);
				write_array_std140<Vector3>(a, gui, p_array_size, 4);
			} else {
				Vector3 v = convert_to_vector<Vector3>(value);
				gui[0] = v.x;
				gui[1] = v.y;
				gui[2] = v.z;
			}
		} break;
		case ShaderLanguage::TYPE_VEC4: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = convert_array_std140<Vector4, float>(value);
				write_array_std140<Vector4>(a, gui, p_array_size, 4);
			} else {
				Vector4 v = convert_to_vector<Vector4>(value);
				gui[0] = v.x;
				gui[1] = v.y;
				gui[2] = v.z;
				gui[3] = v.w;
			}
		} break;
		case ShaderLanguage::TYPE_MAT2: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = value;
				int s = a.size();

				for (int i = 0, j = 0; i < p_array_size * 4; i += 4, j += 8) {
					if (i + 3 < s) {
						gui[j] = a[i];
						gui[j + 1] = a[i + 1];

						gui[j + 4] = a[i + 2];
						gui[j + 5] = a[i + 3];
					} else {
						gui[j] = 1;
						gui[j + 1] = 0;

						gui[j + 4] = 0;
						gui[j + 5] = 1;
					}
					gui[j + 2] = 0; // ignored
					gui[j + 3] = 0; // ignored
					gui[j + 6] = 0; // ignored
					gui[j + 7] = 0; // ignored
				}
			} else {
				Transform2D v = value;

				//in std140 members of mat2 are treated as vec4s
				gui[0] = v.columns[0][0];
				gui[1] = v.columns[0][1];
				gui[2] = 0; // ignored
				gui[3] = 0; // ignored

				gui[4] = v.columns[1][0];
				gui[5] = v.columns[1][1];
				gui[6] = 0; // ignored
				gui[7] = 0; // ignored
			}
		} break;
		case ShaderLanguage::TYPE_MAT3: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = convert_array_std140<Basis, float>(value);
				const Basis default_basis;
				const int s = a.size();

				for (int i = 0, j = 0; i < p_array_size * 9; i += 9, j += 12) {
					if (i + 8 < s) {
						gui[j] = a[i];
						gui[j + 1] = a[i + 1];
						gui[j + 2] = a[i + 2];
						gui[j + 3] = 0; // Ignored.

						gui[j + 4] = a[i + 3];
						gui[j + 5] = a[i + 4];
						gui[j + 6] = a[i + 5];
						gui[j + 7] = 0; // Ignored.

						gui[j + 8] = a[i + 6];
						gui[j + 9] = a[i + 7];
						gui[j + 10] = a[i + 8];
						gui[j + 11] = 0; // Ignored.
					} else {
						convert_item_std140(default_basis, gui + j);
					}
				}
			} else {
				convert_item_std140<Basis>(value, gui);
			}
		} break;
		case ShaderLanguage::TYPE_MAT4: {
			float *gui = (float *)data;

			if (p_array_size > 0) {
				const PackedFloat32Array &a = convert_array_std140<Projection, float>(value);
				if (unlikely(a.is_empty())) {
					_fill_std140_ubo_empty(ShaderLanguage::TYPE_MAT4, p_array_size, data);
				} else {
					write_array_std140<Projection>(a, gui, p_array_size, 16);
				}
			} else {
				convert_item_std140<Projection>(value, gui);
			}
		} break;
		default: {
		}
	}
}

_FORCE_INLINE_ static void _fill_std140_ubo_value(ShaderLanguage::DataType type, const Vector<ShaderLanguage::Scalar> &value, uint8_t *data) {
	switch (type) {
		case ShaderLanguage::TYPE_BOOL: {
			uint32_t *gui = (uint32_t *)data;
			gui[0] = value[0].boolean ? 1 : 0;
		} break;
		case ShaderLanguage::TYPE_BVEC2: {
			uint32_t *gui = (uint32_t *)data;
			gui[0] = value[0].boolean ? 1 : 0;
			gui[1] = value[1].boolean ? 1 : 0;

		} break;
		case ShaderLanguage::TYPE_BVEC3: {
			uint32_t *gui = (uint32_t *)data;
			gui[0] = value[0].boolean ? 1 : 0;
			gui[1] = value[1].boolean ? 1 : 0;
			gui[2] = value[2].boolean ? 1 : 0;

		} break;
		case ShaderLanguage::TYPE_BVEC4: {
			uint32_t *gui = (uint32_t *)data;
			gui[0] = value[0].boolean ? 1 : 0;
			gui[1] = value[1].boolean ? 1 : 0;
			gui[2] = value[2].boolean ? 1 : 0;
			gui[3] = value[3].boolean ? 1 : 0;

		} break;
		case ShaderLanguage::TYPE_INT: {
			int32_t *gui = (int32_t *)data;
			gui[0] = value[0].sint;

		} break;
		case ShaderLanguage::TYPE_IVEC2: {
			int32_t *gui = (int32_t *)data;

			for (int i = 0; i < 2; i++) {
				gui[i] = value[i].sint;
			}

		} break;
		case ShaderLanguage::TYPE_IVEC3: {
			int32_t *gui = (int32_t *)data;

			for (int i = 0; i < 3; i++) {
				gui[i] = value[i].sint;
			}

		} break;
		case ShaderLanguage::TYPE_IVEC4: {
			int32_t *gui = (int32_t *)data;

			for (int i = 0; i < 4; i++) {
				gui[i] = value[i].sint;
			}

		} break;
		case ShaderLanguage::TYPE_UINT: {
			uint32_t *gui = (uint32_t *)data;
			gui[0] = value[0].uint;

		} break;
		case ShaderLanguage::TYPE_UVEC2: {
			int32_t *gui = (int32_t *)data;

			for (int i = 0; i < 2; i++) {
				gui[i] = value[i].uint;
			}
		} break;
		case ShaderLanguage::TYPE_UVEC3: {
			int32_t *gui = (int32_t *)data;

			for (int i = 0; i < 3; i++) {
				gui[i] = value[i].uint;
			}

		} break;
		case ShaderLanguage::TYPE_UVEC4: {
			int32_t *gui = (int32_t *)data;

			for (int i = 0; i < 4; i++) {
				gui[i] = value[i].uint;
			}
		} break;
		case ShaderLanguage::TYPE_FLOAT: {
			float *gui = (float *)data;
			gui[0] = value[0].real;

		} break;
		case ShaderLanguage::TYPE_VEC2: {
			float *gui = (float *)data;

			for (int i = 0; i < 2; i++) {
				gui[i] = value[i].real;
			}

		} break;
		case ShaderLanguage::TYPE_VEC3: {
			float *gui = (float *)data;

			for (int i = 0; i < 3; i++) {
				gui[i] = value[i].real;
			}

		} break;
		case ShaderLanguage::TYPE_VEC4: {
			float *gui = (float *)data;

			for (int i = 0; i < 4; i++) {
				gui[i] = value[i].real;
			}
		} break;
		case ShaderLanguage::TYPE_MAT2: {
			float *gui = (float *)data;

			//in std140 members of mat2 are treated as vec4s
			gui[0] = value[0].real;
			gui[1] = value[1].real;
			gui[2] = 0;
			gui[3] = 0;
			gui[4] = value[2].real;
			gui[5] = value[3].real;
			gui[6] = 0;
			gui[7] = 0;
		} break;
		case ShaderLanguage::TYPE_MAT3: {
			float *gui = (float *)data;

			gui[0] = value[0].real;
			gui[1] = value[1].real;
			gui[2] = value[2].real;
			gui[3] = 0;
			gui[4] = value[3].real;
			gui[5] = value[4].real;
			gui[6] = value[5].real;
			gui[7] = 0;
			gui[8] = value[6].real;
			gui[9] = value[7].real;
			gui[10] = value[8].real;
			gui[11] = 0;
		} break;
		case ShaderLanguage::TYPE_MAT4: {
			float *gui = (float *)data;

			for (int i = 0; i < 16; i++) {
				gui[i] = value[i].real;
			}
		} break;
		default: {
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// ShaderData

void ShaderData::set_path_hint(const String &p_hint) {
	path = p_hint;
}

void ShaderData::set_default_texture_parameter(const StringName &p_name, RID p_texture, int p_index) {
	if (!p_texture.is_valid()) {
		if (default_texture_params.has(p_name) && default_texture_params[p_name].has(p_index)) {
			default_texture_params[p_name].erase(p_index);

			if (default_texture_params[p_name].is_empty()) {
				default_texture_params.erase(p_name);
			}
		}
	} else {
		if (!default_texture_params.has(p_name)) {
			default_texture_params[p_name] = HashMap<int, RID>();
		}
		default_texture_params[p_name][p_index] = p_texture;
	}
}

Variant ShaderData::get_default_parameter(const StringName &p_parameter) const {
	if (uniforms.has(p_parameter)) {
		ShaderLanguage::ShaderNode::Uniform uniform = uniforms[p_parameter];
		Vector<ShaderLanguage::Scalar> default_value = uniform.default_value;
		return ShaderLanguage::constant_value_to_variant(default_value, uniform.type, uniform.array_size, uniform.hint);
	}
	return Variant();
}

void ShaderData::get_shader_uniform_list(List<PropertyInfo> *p_param_list) const {
	SortArray<Pair<StringName, int>, ShaderLanguage::UniformOrderComparator> sorter{};
	LocalVector<Pair<StringName, int>> filtered_uniforms{};

	for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : uniforms) {
		if (E.value.scope != ShaderLanguage::ShaderNode::Uniform::SCOPE_LOCAL) {
			continue;
		}
		if (E.value.texture_order >= 0) {
			filtered_uniforms.push_back(Pair<StringName, int>(E.key, E.value.texture_order + 100000));
		} else {
			filtered_uniforms.push_back(Pair<StringName, int>(E.key, E.value.order));
		}
	}
	int uniform_count = filtered_uniforms.size();
	sorter.sort(filtered_uniforms.ptr(), uniform_count);

	String last_group;
	for (int i = 0; i < uniform_count; i++) {
		const StringName &uniform_name = filtered_uniforms[i].first;
		const ShaderLanguage::ShaderNode::Uniform &uniform = uniforms[uniform_name];

		String group = uniform.group;
		if (!uniform.subgroup.is_empty()) {
			group += "::" + uniform.subgroup;
		}

		if (group != last_group) {
			PropertyInfo pi;
			pi.usage = PROPERTY_USAGE_GROUP;
			pi.name = group;
			p_param_list->push_back(pi);

			last_group = group;
		}

		PropertyInfo pi = ShaderLanguage::uniform_to_property_info(uniform);
		pi.name = uniform_name;
		p_param_list->push_back(pi);
	}
}

void ShaderData::get_instance_param_list(List<RendererMaterialStorage::InstanceShaderParam> *p_param_list) const {
	for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : uniforms) {
		if (E.value.scope != ShaderLanguage::ShaderNode::Uniform::SCOPE_INSTANCE) {
			continue;
		}

		RendererMaterialStorage::InstanceShaderParam p;
		p.info = ShaderLanguage::uniform_to_property_info(E.value);
		p.info.name = E.key; //supply name
		p.index = E.value.instance_index;
		p.default_value = ShaderLanguage::constant_value_to_variant(E.value.default_value, E.value.type, E.value.array_size, E.value.hint);
		p_param_list->push_back(p);
	}
}

bool ShaderData::is_parameter_texture(const StringName &p_param) const {
	if (uniforms.has(p_param)) {
		return uniforms[p_param].texture_order >= 0;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
// MaterialData

void MaterialData::update_uniform_buffer(const HashMap<StringName, ShaderLanguage::ShaderNode::Uniform> &p_uniforms, const uint32_t *p_uniform_offsets, const HashMap<StringName, Variant> &p_parameters, uint8_t *p_buffer, uint32_t p_buffer_size) {
	for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : p_uniforms) {
		if (E.value.order < 0 || E.value.scope == ShaderLanguage::ShaderNode::Uniform::SCOPE_INSTANCE) {
			continue;
		}

		uint32_t offset = p_uniform_offsets[E.value.order];
		uint8_t *data = &p_buffer[offset];
		HashMap<StringName, Variant>::ConstIterator V = p_parameters.find(E.key);

		if (V) {
			_fill_std140_variant_ubo_value(E.value.type, E.value.array_size, V->value, data);
		} else if (!E.value.default_value.is_empty()) {
			_fill_std140_ubo_value(E.value.type, E.value.default_value, data);
		} else {
			_fill_std140_ubo_empty(E.value.type, E.value.array_size, data);
		}
	}
}

MaterialData::~MaterialData() {

}

void MaterialData::update_textures(const HashMap<StringName, Variant> &p_parameters, const HashMap<StringName, HashMap<int, RID>> &p_default_textures, const Vector<ShaderCompiler::GeneratedCode::Texture> &p_texture_uniforms, RID *p_textures, bool p_is_3d_shader_type) {
	TextureStorage *texture_storage = TextureStorage::get_singleton();

	int k = 0;
	for (int i = 0; i < p_texture_uniforms.size(); i++) {
		const StringName &uniform_name = p_texture_uniforms[i].name;
		HashMap<StringName, Variant>::ConstIterator V = p_parameters.find(uniform_name);

		int array_size = p_texture_uniforms[i].array_size > 0 ? p_texture_uniforms[i].array_size : 1;

		// Handle Texture Arrays natively
		if (V && V->value.get_type() == Variant::ARRAY) {
			Array tex_array = V->value;
			for (int j = 0; j < array_size; j++) {
				RID gl_texture;
				if (p_default_textures.has(uniform_name) && p_default_textures[uniform_name].has(j)) {
					gl_texture = p_default_textures[uniform_name][j];
				} else {
					gl_texture = texture_storage->texture_gl_get_default(
						GLES2::DEFAULT_GL_TEXTURE_WHITE
					);
				}

				if (j < tex_array.size() && tex_array[j].get_type() == Variant::RID) {
					RID requested = tex_array[j];
					if (texture_storage->owns_texture(requested)) {
						gl_texture = requested;
					}
				}
				p_textures[k++] = gl_texture;
			}
		} else {
			// Single texture or fallback
			RID gl_texture;
			if (p_default_textures.has(uniform_name) && p_default_textures[uniform_name].has(0)) {
				gl_texture = p_default_textures[uniform_name][0];
			} else {
				gl_texture = texture_storage->texture_gl_get_default(
					GLES2::DEFAULT_GL_TEXTURE_WHITE
				);
			}

			if (V && V->value.get_type() == Variant::RID) {
				RID requested = V->value;
				if (texture_storage->owns_texture(requested)) {
					gl_texture = requested;
				}
			}

			// Fill the entire array span even if the user only provided one texture
			for (int j = 0; j < array_size; j++) {
				p_textures[k++] = gl_texture;
			}
		}
	}
}

void MaterialData::update_parameters_internal(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty, const HashMap<StringName, ShaderLanguage::ShaderNode::Uniform> &p_uniforms, const uint32_t *p_uniform_offsets, const Vector<ShaderCompiler::GeneratedCode::Texture> &p_texture_uniforms, const HashMap<StringName, HashMap<int, RID>> &p_default_texture_params, uint32_t p_ubo_size, bool p_is_3d_shader_type) {
	if ((uint32_t)ubo_data.size() != p_ubo_size) {
		p_uniform_dirty = true;
		ubo_data.resize(p_ubo_size);
		if (ubo_data.size()) {
			memset(ubo_data.ptrw(), 0, ubo_data.size());
		}
	}

	if (p_uniform_dirty && ubo_data.size()) {
		update_uniform_buffer(p_uniforms, p_uniform_offsets, p_parameters, ubo_data.ptrw(), ubo_data.size());
	}

	uint32_t tex_uniform_count = 0U;
	for (int i = 0; i < p_texture_uniforms.size(); i++) {
		tex_uniform_count += uint32_t(p_texture_uniforms[i].array_size > 0 ? p_texture_uniforms[i].array_size : 1);
	}

	if ((uint32_t)texture_cache.size() != tex_uniform_count || p_textures_dirty) {
		texture_cache.resize(tex_uniform_count);
		p_textures_dirty = true;
	}

	if (p_textures_dirty && tex_uniform_count) {
		update_textures(p_parameters, p_default_texture_params, p_texture_uniforms, texture_cache.ptrw(), p_is_3d_shader_type);
	}
}

///////////////////////////////////////////////////////////////////////////
// Material Storage

MaterialStorage *MaterialStorage::singleton = nullptr;

MaterialStorage *MaterialStorage::get_singleton() {
	return singleton;
}

MaterialStorage::MaterialStorage() {
	singleton = this;

	shader_data_request_func[RS::SHADER_SPATIAL] = _create_scene_shader_func;
	shader_data_request_func[RS::SHADER_CANVAS_ITEM] = _create_canvas_shader_func;
	shader_data_request_func[RS::SHADER_PARTICLES] = _create_particles_shader_func;
	shader_data_request_func[RS::SHADER_SKY] = _create_sky_shader_func;
	shader_data_request_func[RS::SHADER_FOG] = nullptr;

	material_data_request_func[RS::SHADER_SPATIAL] = _create_scene_material_func;
	material_data_request_func[RS::SHADER_CANVAS_ITEM] = _create_canvas_material_func;
	material_data_request_func[RS::SHADER_PARTICLES] = _create_particles_material_func;
	material_data_request_func[RS::SHADER_SKY] = _create_sky_material_func;
	material_data_request_func[RS::SHADER_FOG] = nullptr;

	static_assert(sizeof(GlobalShaderUniforms::Value) == 16);

	global_shader_uniforms.buffer_size = MAX(4096, (int)GLOBAL_GET("rendering/limits/global_shader_variables/buffer_size"));
	if (global_shader_uniforms.buffer_size > uint32_t(Config::get_singleton()->max_uniform_buffer_size)) {
		if (Config::get_singleton()->max_uniform_buffer_size > 0) {
			global_shader_uniforms.buffer_size = uint32_t(Config::get_singleton()->max_uniform_buffer_size);
			WARN_PRINT("Project setting \"rendering/limits/global_shader_variables/buffer_size\" exceeds maximum uniform buffer size of: " + itos(Config::get_singleton()->max_uniform_buffer_size));
		} else {
			global_shader_uniforms.buffer_size = 0;
		}
	}

	global_shader_uniforms.buffer_values = memnew_arr(GlobalShaderUniforms::Value, global_shader_uniforms.buffer_size);
	memset(global_shader_uniforms.buffer_values, 0, sizeof(GlobalShaderUniforms::Value) * global_shader_uniforms.buffer_size);
	global_shader_uniforms.buffer_usage = memnew_arr(GlobalShaderUniforms::ValueUsage, global_shader_uniforms.buffer_size);
	global_shader_uniforms.buffer_dirty_regions = memnew_arr(bool, global_shader_uniforms.buffer_size / GlobalShaderUniforms::BUFFER_DIRTY_REGION_SIZE);
	memset(global_shader_uniforms.buffer_dirty_regions, 0, sizeof(bool) * global_shader_uniforms.buffer_size / GlobalShaderUniforms::BUFFER_DIRTY_REGION_SIZE);

	{
		// Setup CanvasItem compiler
		ShaderCompiler::DefaultIdentifierActions actions;

		actions.renames["VERTEX"] = "outvec.xy";
		actions.renames["LIGHT_VERTEX"] = "light_vertex";
		actions.renames["SHADOW_VERTEX"] = "shadow_vertex";
		actions.renames["UV"] = "uv";
		actions.renames["POINT_SIZE"] = "point_size";

		actions.renames["MODEL_MATRIX"] = "model_matrix";
		actions.renames["CANVAS_MATRIX"] = "canvas_transform";
		actions.renames["SCREEN_MATRIX"] = "screen_transform";
		actions.renames["TIME"] = "time";
		actions.renames["PI"] = _MKSTR(Math_PI);
		actions.renames["TAU"] = _MKSTR(Math_TAU);
		actions.renames["E"] = _MKSTR(Math_E);
		actions.renames["AT_LIGHT_PASS"] = "false";
		actions.renames["INSTANCE_CUSTOM"] = "instance_custom";

		actions.renames["COLOR"] = "color";
		actions.renames["NORMAL"] = "normal";
		actions.renames["NORMAL_MAP"] = "normal_map";
		actions.renames["NORMAL_MAP_DEPTH"] = "normal_map_depth";
		actions.renames["TEXTURE"] = "color_texture";
		actions.renames["TEXTURE_PIXEL_SIZE"] = "color_texture_pixel_size";
		actions.renames["NORMAL_TEXTURE"] = "normal_texture";
		actions.renames["SPECULAR_SHININESS_TEXTURE"] = "specular_texture";
		actions.renames["SPECULAR_SHININESS"] = "specular_shininess";
		actions.renames["SCREEN_UV"] = "screen_uv";
		actions.renames["SCREEN_PIXEL_SIZE"] = "screen_pixel_size";
		actions.renames["FRAGCOORD"] = "gl_FragCoord";
		actions.renames["POINT_COORD"] = "gl_PointCoord";

		// Requires hardware extensions
		// (handled in `config.cpp` and `compiler/shader_gles2.cpp`)
		actions.renames["INSTANCE_ID"] = "gl_InstanceID";
		actions.renames["VERTEX_ID"] = "gl_VertexID";

		actions.renames["CUSTOM0"] = "custom0";
		actions.renames["CUSTOM1"] = "custom1";

		actions.renames["LIGHT_POSITION"] = "light_position";
		actions.renames["LIGHT_DIRECTION"] = "light_direction";
		actions.renames["LIGHT_IS_DIRECTIONAL"] = "is_directional";
		actions.renames["LIGHT_COLOR"] = "light_color";
		actions.renames["LIGHT_ENERGY"] = "light_energy";
		actions.renames["LIGHT"] = "light";
		actions.renames["SHADOW_MODULATE"] = "shadow_modulate";

		actions.renames["texture_sdf"] = "texture_sdf";
		actions.renames["texture_sdf_normal"] = "texture_sdf_normal";
		actions.renames["sdf_to_screen_uv"] = "sdf_to_screen_uv";
		actions.renames["screen_uv_to_sdf"] = "screen_uv_to_sdf";

		actions.usage_defines["COLOR"] = "#define COLOR_USED\n";
		actions.usage_defines["SCREEN_UV"] = "#define SCREEN_UV_USED\n";
		actions.usage_defines["SCREEN_PIXEL_SIZE"] = "@SCREEN_UV";
		actions.usage_defines["NORMAL"] = "#define NORMAL_USED\n";
		actions.usage_defines["NORMAL_MAP"] = "#define NORMAL_MAP_USED\n";
		actions.usage_defines["SPECULAR_SHININESS"] = "#define SPECULAR_SHININESS_USED\n";
		actions.usage_defines["CUSTOM0"] = "#define CUSTOM0_USED\n";
		actions.usage_defines["CUSTOM1"] = "#define CUSTOM1_USED\n";

		actions.render_mode_defines["skip_vertex_transform"] = "#define SKIP_TRANSFORM_USED\n";
		actions.render_mode_defines["unshaded"] = "#define MODE_UNSHADED\n";
		actions.render_mode_defines["light_only"] = "#define MODE_LIGHT_ONLY\n";
		actions.render_mode_defines["world_vertex_coords"] = "#define USE_WORLD_VERTEX_COORDS\n";

		actions.global_buffer_array_variable = "global_shader_uniforms";

		shaders.compiler_canvas.initialize(actions);
	}

	{
		// Setup Scene compiler
		ShaderCompiler::DefaultIdentifierActions actions;

		actions.renames["MODEL_MATRIX"] = "model_matrix";
		actions.renames["MODEL_NORMAL_MATRIX"] = "model_normal_matrix";
		actions.renames["VIEW_MATRIX"] = "scene_data.view_matrix";
		actions.renames["INV_VIEW_MATRIX"] = "scene_data.inv_view_matrix";
		actions.renames["PROJECTION_MATRIX"] = "projection_matrix";
		actions.renames["INV_PROJECTION_MATRIX"] = "inv_projection_matrix";
		actions.renames["MODELVIEW_MATRIX"] = "modelview";
		actions.renames["MODELVIEW_NORMAL_MATRIX"] = "modelview_normal";
		actions.renames["MAIN_CAM_INV_VIEW_MATRIX"] = "scene_data.main_cam_inv_view_matrix";

		actions.renames["VERTEX"] = "vertex";
		actions.renames["NORMAL"] = "normal";
		actions.renames["TANGENT"] = "tangent";
		actions.renames["BINORMAL"] = "binormal";
		actions.renames["POSITION"] = "position";
		actions.renames["UV"] = "uv_interp";
		actions.renames["UV2"] = "uv2_interp";
		actions.renames["COLOR"] = "color_interp";
		actions.renames["POINT_SIZE"] = "point_size";
		actions.renames["INSTANCE_ID"] = "gl_InstanceID";
		actions.renames["VERTEX_ID"] = "gl_VertexID";

		actions.renames["ALPHA_SCISSOR_THRESHOLD"] = "alpha_scissor_threshold";
		actions.renames["ALPHA_HASH_SCALE"] = "alpha_hash_scale";
		actions.renames["ALPHA_ANTIALIASING_EDGE"] = "alpha_antialiasing_edge";
		actions.renames["ALPHA_TEXTURE_COORDINATE"] = "alpha_texture_coordinate";

		actions.renames["TIME"] = "scene_data.time";
		actions.renames["EXPOSURE"] = "(1.0 / scene_data.emissive_exposure_normalization)";
		actions.renames["PI"] = _MKSTR(Math_PI);
		actions.renames["TAU"] = _MKSTR(Math_TAU);
		actions.renames["E"] = _MKSTR(Math_E);
		actions.renames["VIEWPORT_SIZE"] = "scene_data.viewport_size";

		actions.renames["FRAGCOORD"] = "gl_FragCoord";
		actions.renames["FRONT_FACING"] = "gl_FrontFacing";
		actions.renames["NORMAL_MAP"] = "normal_map";
		actions.renames["NORMAL_MAP_DEPTH"] = "normal_map_depth";
		actions.renames["ALBEDO"] = "albedo";
		actions.renames["ALPHA"] = "alpha";
		actions.renames["METALLIC"] = "metallic";
		actions.renames["SPECULAR"] = "specular";
		actions.renames["ROUGHNESS"] = "roughness";
		actions.renames["RIM"] = "rim";
		actions.renames["RIM_TINT"] = "rim_tint";
		actions.renames["CLEARCOAT"] = "clearcoat";
		actions.renames["CLEARCOAT_ROUGHNESS"] = "clearcoat_roughness";
		actions.renames["ANISOTROPY"] = "anisotropy";
		actions.renames["ANISOTROPY_FLOW"] = "anisotropy_flow";
		actions.renames["SSS_STRENGTH"] = "sss_strength";
		actions.renames["SSS_TRANSMITTANCE_COLOR"] = "transmittance_color";
		actions.renames["SSS_TRANSMITTANCE_DEPTH"] = "transmittance_depth";
		actions.renames["SSS_TRANSMITTANCE_BOOST"] = "transmittance_boost";
		actions.renames["BACKLIGHT"] = "backlight";
		actions.renames["AO"] = "ao";
		actions.renames["AO_LIGHT_AFFECT"] = "ao_light_affect";
		actions.renames["EMISSION"] = "emission";
		actions.renames["POINT_COORD"] = "gl_PointCoord";
		actions.renames["INSTANCE_CUSTOM"] = "instance_custom";
		actions.renames["SCREEN_UV"] = "screen_uv";

		// Requires hardware extensions
		// (handled in `config.cpp` and `compiler/shader_gles2.cpp`)
		actions.renames["DEPTH"] = "gl_FragDepth";

		actions.renames["FOG"] = "fog";
		actions.renames["RADIANCE"] = "custom_radiance";
		actions.renames["IRRADIANCE"] = "custom_irradiance";
		actions.renames["BONE_INDICES"] = "bone_attrib";
		actions.renames["BONE_WEIGHTS"] = "weight_attrib";
		actions.renames["CUSTOM0"] = "custom0_attrib";
		actions.renames["CUSTOM1"] = "custom1_attrib";
		actions.renames["CUSTOM2"] = "custom2_attrib";
		actions.renames["CUSTOM3"] = "custom3_attrib";
		actions.renames["OUTPUT_IS_SRGB"] = "SHADER_IS_SRGB";

		actions.renames["NODE_POSITION_WORLD"] = "model_matrix[3].xyz";
		actions.renames["CAMERA_POSITION_WORLD"] = "scene_data.inv_view_matrix[3].xyz";
		actions.renames["CAMERA_DIRECTION_WORLD"] = "scene_data.view_matrix[3].xyz";
		actions.renames["CAMERA_VISIBLE_LAYERS"] = "scene_data.camera_visible_layers";
		actions.renames["NODE_POSITION_VIEW"] = "(scene_data.view_matrix * model_matrix)[3].xyz";

		actions.renames["VIEW_INDEX"] = "ViewIndex";
		actions.renames["VIEW_MONO_LEFT"] = "0";
		actions.renames["VIEW_RIGHT"] = "1";
		actions.renames["EYE_OFFSET"] = "eye_offset";

		actions.renames["VIEW"] = "view";
		actions.renames["SPECULAR_AMOUNT"] = "specular_amount";
		actions.renames["LIGHT_COLOR"] = "light_color";
		actions.renames["LIGHT_IS_DIRECTIONAL"] = "is_directional";
		actions.renames["LIGHT"] = "light";
		actions.renames["ATTENUATION"] = "attenuation";

		actions.usage_defines["NORMAL"] = "#define NORMAL_USED\n";
		actions.usage_defines["TANGENT"] = "#define TANGENT_USED\n";
		actions.usage_defines["BINORMAL"] = "@TANGENT";
		actions.usage_defines["RIM"] = "#define LIGHT_RIM_USED\n";
		actions.usage_defines["RIM_TINT"] = "@RIM";
		actions.usage_defines["CLEARCOAT"] = "#define LIGHT_CLEARCOAT_USED\n";
		actions.usage_defines["CLEARCOAT_ROUGHNESS"] = "@CLEARCOAT";
		actions.usage_defines["ANISOTROPY"] = "#define LIGHT_ANISOTROPY_USED\n";
		actions.usage_defines["ANISOTROPY_FLOW"] = "@ANISOTROPY";
		actions.usage_defines["AO"] = "#define AO_USED\n";
		actions.usage_defines["AO_LIGHT_AFFECT"] = "#define AO_USED\n";
		actions.usage_defines["UV"] = "#define UV_USED\n";
		actions.usage_defines["UV2"] = "#define UV2_USED\n";
		actions.usage_defines["BONE_INDICES"] = "#define BONES_USED\n";
		actions.usage_defines["BONE_WEIGHTS"] = "#define WEIGHTS_USED\n";
		actions.usage_defines["CUSTOM0"] = "#define CUSTOM0_USED\n";
		actions.usage_defines["CUSTOM1"] = "#define CUSTOM1_USED\n";
		actions.usage_defines["CUSTOM2"] = "#define CUSTOM2_USED\n";
		actions.usage_defines["CUSTOM3"] = "#define CUSTOM3_USED\n";
		actions.usage_defines["NORMAL_MAP"] = "#define NORMAL_MAP_USED\n";
		actions.usage_defines["NORMAL_MAP_DEPTH"] = "@NORMAL_MAP";
		actions.usage_defines["COLOR"] = "#define COLOR_USED\n";
		actions.usage_defines["INSTANCE_CUSTOM"] = "#define ENABLE_INSTANCE_CUSTOM\n";
		actions.usage_defines["POSITION"] = "#define OVERRIDE_POSITION\n";

		actions.usage_defines["ALPHA_SCISSOR_THRESHOLD"] = "#define ALPHA_SCISSOR_USED\n";
		actions.usage_defines["ALPHA_HASH_SCALE"] = "#define ALPHA_HASH_USED\n";
		actions.usage_defines["ALPHA_ANTIALIASING_EDGE"] = "#define ALPHA_ANTIALIASING_EDGE_USED\n";
		actions.usage_defines["ALPHA_TEXTURE_COORDINATE"] = "@ALPHA_ANTIALIASING_EDGE";

		actions.usage_defines["SSS_STRENGTH"] = "#define ENABLE_SSS\n";
		actions.usage_defines["SSS_TRANSMITTANCE_DEPTH"] = "#define ENABLE_TRANSMITTANCE\n";
		actions.usage_defines["BACKLIGHT"] = "#define LIGHT_BACKLIGHT_USED\n";
		actions.usage_defines["SCREEN_UV"] = "#define SCREEN_UV_USED\n";

		actions.usage_defines["DIFFUSE_LIGHT"] = "#define USE_LIGHT_SHADER_CODE\n";
		actions.usage_defines["SPECULAR_LIGHT"] = "#define USE_LIGHT_SHADER_CODE\n";

		actions.usage_defines["FOG"] = "#define CUSTOM_FOG_USED\n";
		actions.usage_defines["RADIANCE"] = "#define CUSTOM_RADIANCE_USED\n";
		actions.usage_defines["IRRADIANCE"] = "#define CUSTOM_IRRADIANCE_USED\n";

		actions.render_mode_defines["skip_vertex_transform"] = "#define SKIP_TRANSFORM_USED\n";
		actions.render_mode_defines["world_vertex_coords"] = "#define VERTEX_WORLD_COORDS_USED\n";
		actions.render_mode_defines["ensure_correct_normals"] = "#define ENSURE_CORRECT_NORMALS\n";
		actions.render_mode_defines["cull_front"] = "#define DO_SIDE_CHECK\n";
		actions.render_mode_defines["cull_disabled"] = "#define DO_SIDE_CHECK\n";
		actions.render_mode_defines["particle_trails"] = "#define USE_PARTICLE_TRAILS\n";
		actions.render_mode_defines["depth_prepass_alpha"] = "#define USE_OPAQUE_PREPASS\n";

		bool force_lambert = GLOBAL_GET("rendering/shading/overrides/force_lambert_over_burley");

		if (!force_lambert) {
			actions.render_mode_defines["diffuse_burley"] = "#define DIFFUSE_BURLEY\n";
		}

		actions.render_mode_defines["diffuse_lambert_wrap"] = "#define DIFFUSE_LAMBERT_WRAP\n";
		actions.render_mode_defines["diffuse_toon"] = "#define DIFFUSE_TOON\n";

		actions.render_mode_defines["sss_mode_skin"] = "#define SSS_MODE_SKIN\n";

		actions.render_mode_defines["specular_schlick_ggx"] = "#define SPECULAR_SCHLICK_GGX\n";
		actions.render_mode_defines["specular_toon"] = "#define SPECULAR_TOON\n";
		actions.render_mode_defines["specular_disabled"] = "#define SPECULAR_DISABLED\n";
		actions.render_mode_defines["shadows_disabled"] = "#define SHADOWS_DISABLED\n";
		actions.render_mode_defines["ambient_light_disabled"] = "#define AMBIENT_LIGHT_DISABLED\n";
		actions.render_mode_defines["shadow_to_opacity"] = "#define USE_SHADOW_TO_OPACITY\n";
		actions.render_mode_defines["unshaded"] = "#define MODE_UNSHADED\n";
		actions.render_mode_defines["fog_disabled"] = "#define FOG_DISABLED\n";

		actions.default_filter = ShaderLanguage::FILTER_LINEAR_MIPMAP;
		actions.default_repeat = ShaderLanguage::REPEAT_ENABLE;

		actions.check_multiview_samplers = RasterizerGLES2::get_singleton()->is_xr_enabled();
		actions.global_buffer_array_variable = "global_shader_uniforms";

		shaders.compiler_scene.initialize(actions);
	}

	{
		// Setup Particles compiler
		ShaderCompiler::DefaultIdentifierActions actions;

		actions.renames["COLOR"] = "out_color";
		actions.renames["VELOCITY"] = "out_velocity_flags.xyz";
		actions.renames["ACTIVE"] = "particle_active";
		actions.renames["RESTART"] = "restart";
		actions.renames["CUSTOM"] = "out_custom";
		for (int i = 0; i < PARTICLES_MAX_USERDATAS; i++) {
			String udname = "USERDATA" + itos(i + 1);
			actions.renames[udname] = "out_userdata" + itos(i + 1);
			actions.usage_defines[udname] = "#define USERDATA" + itos(i + 1) + "_USED\n";
		}
		actions.renames["TRANSFORM"] = "xform";
		actions.renames["TIME"] = "time";
		actions.renames["PI"] = _MKSTR(Math_PI);
		actions.renames["TAU"] = _MKSTR(Math_TAU);
		actions.renames["E"] = _MKSTR(Math_E);
		actions.renames["LIFETIME"] = "lifetime";
		actions.renames["DELTA"] = "local_delta";
		actions.renames["NUMBER"] = "particle_number";
		actions.renames["INDEX"] = "index";
		actions.renames["AMOUNT_RATIO"] = "amount_ratio";
		actions.renames["EMISSION_TRANSFORM"] = "emission_transform";
		actions.renames["RANDOM_SEED"] = "random_seed";
		actions.renames["RESTART_POSITION"] = "restart_position";
		actions.renames["RESTART_ROT_SCALE"] = "restart_rotation_scale";
		actions.renames["RESTART_VELOCITY"] = "restart_velocity";
		actions.renames["RESTART_COLOR"] = "restart_color";
		actions.renames["RESTART_CUSTOM"] = "restart_custom";
		actions.renames["COLLIDED"] = "collided";
		actions.renames["COLLISION_NORMAL"] = "collision_normal";
		actions.renames["COLLISION_DEPTH"] = "collision_depth";
		actions.renames["ATTRACTOR_FORCE"] = "attractor_force";
		actions.renames["EMITTER_VELOCITY"] = "emitter_velocity";
		actions.renames["INTERPOLATE_TO_END"] = "interp_to_end";

		actions.renames["FLAG_EMIT_POSITION"] = "1";
		actions.renames["FLAG_EMIT_ROT_SCALE"] = "2";
		actions.renames["FLAG_EMIT_VELOCITY"] = "4";
		actions.renames["FLAG_EMIT_COLOR"] = "8";
		actions.renames["FLAG_EMIT_CUSTOM"] = "16";
		actions.renames["emit_subparticle"] = "emit_subparticle";

		actions.usage_defines["emit_subparticle"] = "\nbool emit_subparticle(mat4 p_xform, vec3 p_velocity, vec4 p_color, vec4 p_custom, int p_flags) {\n\treturn false;\n}\n";

		actions.render_mode_defines["disable_force"] = "#define DISABLE_FORCE\n";
		actions.render_mode_defines["disable_velocity"] = "#define DISABLE_VELOCITY\n";
		actions.render_mode_defines["keep_data"] = "#define ENABLE_KEEP_DATA\n";
		actions.render_mode_defines["collision_use_scale"] = "#define USE_COLLISION_SCALE\n";

		actions.default_filter = ShaderLanguage::FILTER_LINEAR_MIPMAP;
		actions.default_repeat = ShaderLanguage::REPEAT_ENABLE;

		actions.global_buffer_array_variable = "global_shader_uniforms";

		shaders.compiler_particles.initialize(actions);
	}

	{
		// Setup Sky compiler
		ShaderCompiler::DefaultIdentifierActions actions;

		actions.renames["COLOR"] = "color";
		actions.renames["ALPHA"] = "alpha";
		actions.renames["EYEDIR"] = "cube_normal";
		actions.renames["POSITION"] = "position";
		actions.renames["SKY_COORDS"] = "panorama_coords";
		actions.renames["SCREEN_UV"] = "uv";
		actions.renames["TIME"] = "time";
		actions.renames["FRAGCOORD"] = "gl_FragCoord";
		actions.renames["PI"] = _MKSTR(Math_PI);
		actions.renames["TAU"] = _MKSTR(Math_TAU);
		actions.renames["E"] = _MKSTR(Math_E);
		actions.renames["HALF_RES_COLOR"] = "half_res_color";
		actions.renames["QUARTER_RES_COLOR"] = "quarter_res_color";
		actions.renames["RADIANCE"] = "radiance";
		actions.renames["FOG"] = "custom_fog";
		actions.renames["LIGHT0_ENABLED"] = "directional_lights.data[0].enabled";
		actions.renames["LIGHT0_DIRECTION"] = "directional_lights.data[0].direction_energy.xyz";
		actions.renames["LIGHT0_ENERGY"] = "directional_lights.data[0].direction_energy.w";
		actions.renames["LIGHT0_COLOR"] = "directional_lights.data[0].color_size.xyz";
		actions.renames["LIGHT0_SIZE"] = "directional_lights.data[0].color_size.w";
		actions.renames["LIGHT1_ENABLED"] = "directional_lights.data[1].enabled";
		actions.renames["LIGHT1_DIRECTION"] = "directional_lights.data[1].direction_energy.xyz";
		actions.renames["LIGHT1_ENERGY"] = "directional_lights.data[1].direction_energy.w";
		actions.renames["LIGHT1_COLOR"] = "directional_lights.data[1].color_size.xyz";
		actions.renames["LIGHT1_SIZE"] = "directional_lights.data[1].color_size.w";
		actions.renames["LIGHT2_ENABLED"] = "directional_lights.data[2].enabled";
		actions.renames["LIGHT2_DIRECTION"] = "directional_lights.data[2].direction_energy.xyz";
		actions.renames["LIGHT2_ENERGY"] = "directional_lights.data[2].direction_energy.w";
		actions.renames["LIGHT2_COLOR"] = "directional_lights.data[2].color_size.xyz";
		actions.renames["LIGHT2_SIZE"] = "directional_lights.data[2].color_size.w";
		actions.renames["LIGHT3_ENABLED"] = "directional_lights.data[3].enabled";
		actions.renames["LIGHT3_DIRECTION"] = "directional_lights.data[3].direction_energy.xyz";
		actions.renames["LIGHT3_ENERGY"] = "directional_lights.data[3].direction_energy.w";
		actions.renames["LIGHT3_COLOR"] = "directional_lights.data[3].color_size.xyz";
		actions.renames["LIGHT3_SIZE"] = "directional_lights.data[3].color_size.w";
		actions.renames["AT_CUBEMAP_PASS"] = "AT_CUBEMAP_PASS";
		actions.renames["AT_HALF_RES_PASS"] = "AT_HALF_RES_PASS";
		actions.renames["AT_QUARTER_RES_PASS"] = "AT_QUARTER_RES_PASS";
		actions.usage_defines["HALF_RES_COLOR"] = "\n#define USES_HALF_RES_COLOR\n";
		actions.usage_defines["QUARTER_RES_COLOR"] = "\n#define USES_QUARTER_RES_COLOR\n";
		actions.render_mode_defines["disable_fog"] = "#define DISABLE_FOG\n";
		actions.render_mode_defines["use_debanding"] = "#define USE_DEBANDING\n";

		actions.default_filter = ShaderLanguage::FILTER_LINEAR_MIPMAP;
		actions.default_repeat = ShaderLanguage::REPEAT_ENABLE;

		actions.global_buffer_array_variable = "global_shader_uniforms";

		shaders.compiler_sky.initialize(actions);
	}

	String global_defines;
	// TODO(GLES2): inject global #defines here

	// Canvas Shader
	shaders.canvas_shader.initialize(global_defines);
	shaders.canvas_shader.default_version = shaders.canvas_shader.version_create();
	shaders.canvas_shader.version_set_code(shaders.canvas_shader.default_version, HashMap<String, String>(), "", "", "", Vector<String>(), LocalVector<ShaderGLES2::TextureUniformData>(), true);

	// Scene Shader
	shaders.scene_shader.initialize(global_defines);
	shaders.scene_shader.default_version = shaders.scene_shader.version_create();
	shaders.scene_shader.version_set_code(shaders.scene_shader.default_version, HashMap<String, String>(), "", "", "", Vector<String>(), LocalVector<ShaderGLES2::TextureUniformData>(), true);

	// Particles Process Shader
	shaders.particles_process_shader.initialize(global_defines);
	shaders.particles_process_shader.default_version = shaders.particles_process_shader.version_create();
	shaders.particles_process_shader.version_set_code(shaders.particles_process_shader.default_version, HashMap<String, String>(), "", "", "", Vector<String>(), LocalVector<ShaderGLES2::TextureUniformData>(), true);

	// Sky Shader
	shaders.sky_shader.initialize(global_defines);
	shaders.sky_shader.default_version = shaders.sky_shader.version_create();
	shaders.sky_shader.version_set_code(shaders.sky_shader.default_version, HashMap<String, String>(), "", "", "", Vector<String>(), LocalVector<ShaderGLES2::TextureUniformData>(), true);
}

MaterialStorage::~MaterialStorage() {
	if (global_shader_uniforms.buffer_values != nullptr) {
		memdelete_arr(global_shader_uniforms.buffer_values);
		global_shader_uniforms.buffer_values = nullptr;
	}
	if (global_shader_uniforms.buffer_usage != nullptr) {
		memdelete_arr(global_shader_uniforms.buffer_usage);
		global_shader_uniforms.buffer_usage = nullptr;
	}
	if (global_shader_uniforms.buffer_dirty_regions != nullptr) {
		memdelete_arr(global_shader_uniforms.buffer_dirty_regions);
		global_shader_uniforms.buffer_dirty_regions = nullptr;
	}

	// Free the un-freed default shaders
	if (shaders.canvas_shader.default_version.is_valid()) {
		shaders.canvas_shader.version_free(shaders.canvas_shader.default_version);
	}
	if (shaders.scene_shader.default_version.is_valid()) {
		shaders.scene_shader.version_free(shaders.scene_shader.default_version);
	}
	if (shaders.particles_process_shader.default_version.is_valid()) {
		shaders.particles_process_shader.version_free(shaders.particles_process_shader.default_version);
	}
	if (shaders.sky_shader.default_version.is_valid()) {
		shaders.sky_shader.version_free(shaders.sky_shader.default_version);
	}

	// Clear the update queue
	while (material_update_list.first()) {
		material_update_list.remove(material_update_list.first());
	}
	singleton = nullptr;
}

/* GLOBAL SHADER UNIFORM API */

int32_t MaterialStorage::_global_shader_uniform_allocate(uint32_t p_elements) {
	return -1;
}

void MaterialStorage::_global_shader_uniform_store_in_buffer(int32_t p_index, RS::GlobalShaderParameterType p_type, const Variant &p_value) {

}

void MaterialStorage::_global_shader_uniform_mark_buffer_dirty(int32_t p_index, int32_t p_elements) {

}

void MaterialStorage::global_shader_parameter_add(const StringName &p_name, RS::GlobalShaderParameterType p_type, const Variant &p_value) {

}

void MaterialStorage::global_shader_parameter_remove(const StringName &p_name) {

}

Vector<StringName> MaterialStorage::global_shader_parameter_get_list() const {
    return Vector<StringName>();
}

void MaterialStorage::global_shader_parameter_set(const StringName &p_name, const Variant &p_value) {

}

void MaterialStorage::global_shader_parameter_set_override(const StringName &p_name, const Variant &p_value) {

}

Variant MaterialStorage::global_shader_parameter_get(const StringName &p_name) const {
    return Variant();
}

RS::GlobalShaderParameterType MaterialStorage::global_shader_parameter_get_type_internal(const StringName &p_name) const {
    return RS::GLOBAL_VAR_TYPE_MAX;
}

RS::GlobalShaderParameterType MaterialStorage::global_shader_parameter_get_type(const StringName &p_name) const {
    return RS::GLOBAL_VAR_TYPE_MAX;
}

void MaterialStorage::global_shader_parameters_load_settings(bool p_load_textures) {

}

void MaterialStorage::global_shader_parameters_clear() {
	global_shader_uniforms.variables.clear();
}

GLuint MaterialStorage::global_shader_parameters_get_uniform_buffer() const {
	return 0;
}

int32_t MaterialStorage::global_shader_parameters_instance_allocate(RID p_instance) {
	return -1;
}

void MaterialStorage::global_shader_parameters_instance_free(RID p_instance) {

}

void MaterialStorage::global_shader_parameters_instance_update(RID p_instance, int p_index, const Variant &p_value, int p_flags_count) {

}

void MaterialStorage::_update_global_shader_uniforms() {

}

/* SHADER API */

RID MaterialStorage::shader_allocate() {
	return shader_owner.allocate_rid();
}

void MaterialStorage::shader_initialize(RID p_rid) {
	Shader shader;
	shader.data = nullptr;
	shader.mode = RS::SHADER_MAX;

	shader_owner.initialize_rid(p_rid, shader);
}

void MaterialStorage::shader_free(RID p_rid) {
	GLES2::Shader *shader = shader_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(shader);

	// make material unreference this safely
	while (shader->owners.size()) {
		Material *mat = *shader->owners.begin();
		shader->owners.erase(mat);
		material_set_shader(mat->self, RID());
	}

	// clear data if exists
	if (shader->data) {
		memdelete(shader->data);
	}
	shader_owner.free(p_rid);
}

void MaterialStorage::shader_set_code(RID p_shader, const String &p_code) {
	GLES2::Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);

	shader->code = p_code;

	String mode_string = ShaderLanguage::get_shader_type(p_code);

	RS::ShaderMode new_mode;
	if (mode_string == "canvas_item") {
		new_mode = RS::SHADER_CANVAS_ITEM;
	} else if (mode_string == "particles") {
		new_mode = RS::SHADER_PARTICLES;
	} else if (mode_string == "spatial") {
		new_mode = RS::SHADER_SPATIAL;
	} else if (mode_string == "sky") {
		new_mode = RS::SHADER_SKY;
	} else {
		new_mode = RS::SHADER_MAX;
		ERR_PRINT("shader type " + mode_string + " not supported in OpenGL renderer");
	}

	if (new_mode != shader->mode) {
		if (shader->data) {
			memdelete(shader->data);
			shader->data = nullptr;
		}

		for (Material *E : shader->owners) {
			Material *material = E;
			material->shader_mode = new_mode;
			if (material->data) {
				memdelete(material->data);
				material->data = nullptr;
			}
		}

		shader->mode = new_mode;

		if (new_mode < RS::SHADER_MAX && shader_data_request_func[new_mode]) {
			shader->data = shader_data_request_func[new_mode]();
		} else {
			shader->mode = RS::SHADER_MAX; //invalid
		}

		for (Material *E : shader->owners) {
			Material *material = E;
			if (shader->data) {
				material->data = material_data_request_func[new_mode](shader->data);
				material->data->self = material->self;
				material->data->set_next_pass(material->next_pass);
				material->data->set_render_priority(material->priority);
			}
			material->shader_mode = new_mode;
		}

		if (shader->data) {
			for (const KeyValue<StringName, HashMap<int, RID>> &E : shader->default_texture_parameter) {
				for (const KeyValue<int, RID> &E2 : E.value) {
					shader->data->set_default_texture_parameter(E.key, E2.value, E2.key);
				}
			}
		}
	}

	if (shader->data) {
		shader->data->set_code(p_code);
	}

	for (Material *E : shader->owners) {
		Material *material = E;
		material->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
		_material_queue_update(material, true, true);
	}
}

void MaterialStorage::shader_set_path_hint(RID p_shader, const String &p_path) {
	GLES2::Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);

	shader->path_hint = p_path;
	if (shader->data) {
		shader->data->set_path_hint(p_path);
	}
}

String MaterialStorage::shader_get_code(RID p_shader) const {
	const Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_COND_V(!shader, String());

	return shader->code;
}

void MaterialStorage::get_shader_parameter_list(RID p_shader, List<PropertyInfo> *p_param_list) const {
	GLES2::Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);
	if (shader->data) {
		return shader->data->get_shader_uniform_list(p_param_list);
	}
}

void MaterialStorage::shader_set_default_texture_parameter(RID p_shader, const StringName &p_name, RID p_texture, int p_index) {
	GLES2::Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);

	if (p_texture.is_valid() && TextureStorage::get_singleton()->owns_texture(p_texture)) {
		if (!shader->default_texture_parameter.has(p_name)) {
			shader->default_texture_parameter[p_name] = HashMap<int, RID>();
		}
		shader->default_texture_parameter[p_name][p_index] = p_texture;
	} else {
		if (shader->default_texture_parameter.has(p_name) && shader->default_texture_parameter[p_name].has(p_index)) {
			shader->default_texture_parameter[p_name].erase(p_index);

			if (shader->default_texture_parameter[p_name].is_empty()) {
				shader->default_texture_parameter.erase(p_name);
			}
		}
	}
	if (shader->data) {
		shader->data->set_default_texture_parameter(p_name, p_texture, p_index);
	}
	for (Material *E : shader->owners) {
		Material *material = E;
		_material_queue_update(material, false, true);
	}
}

RID MaterialStorage::shader_get_default_texture_parameter(RID p_shader, const StringName &p_name, int p_index) const {
	const GLES2::Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, RID());
	if (shader->default_texture_parameter.has(p_name) && shader->default_texture_parameter[p_name].has(p_index)) {
		return shader->default_texture_parameter[p_name][p_index];
	}

	return RID();
}

Variant MaterialStorage::shader_get_parameter_default(RID p_shader, const StringName &p_param) const {
	Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, Variant());
	if (shader->data) {
		return shader->data->get_default_parameter(p_param);
	}
	return Variant();
}

RS::ShaderNativeSourceCode MaterialStorage::shader_get_native_source_code(RID p_shader) const {
	Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, RS::ShaderNativeSourceCode());
	if (shader->data) {
		return shader->data->get_native_source_code();
	}
	return RS::ShaderNativeSourceCode();
}

/* MATERIAL API */

void MaterialStorage::_material_queue_update(Material *material, bool p_uniform, bool p_texture) {
	ERR_FAIL_NULL(material);

	if (p_uniform) {
		material->uniform_dirty = true;
	}
	if (p_texture) {
		material->texture_dirty = true;
	}

	if (!material->update_element.in_list()) {
		material_update_list.add(&material->update_element);
	}
}

void MaterialStorage::_update_queued_materials() {
	while (material_update_list.first()) {
		Material *mat = material_update_list.first()->self();

		// Push the cached params into the GLES2 Uniform Buffers
		if (mat->data) {
			mat->data->update_parameters(mat->params, mat->uniform_dirty, mat->texture_dirty);
		}

		mat->texture_dirty = false;
		mat->uniform_dirty = false;

		material_update_list.remove(material_update_list.first());
	}
}

RID MaterialStorage::material_allocate() {
	return material_owner.allocate_rid();
}

void MaterialStorage::material_initialize(RID p_rid) {
	material_owner.initialize_rid(p_rid);
	
	Material *material = material_owner.get_or_null(p_rid);
	material->self = p_rid;
}

void MaterialStorage::material_free(RID p_rid) {
	Material *material = material_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(material);

	// Destroy the CanvasMaterialData and unbinds the shader
	material_set_shader(p_rid, RID());

	// Ensure we don't leave dangling pointers in our update queue
	for (KeyValue<StringName, Variant> &E : material->params) {
		if (E.value.get_type() == Variant::ARRAY) {
			Array(E.value).clear();
		}
	}

	if (material->update_element.in_list()) {
		material_update_list.remove(&material->update_element);
	}

	material->dependency.deleted_notify(p_rid);
	material_owner.free(p_rid);
}

void MaterialStorage::material_set_shader(RID p_material, RID p_shader) {
	Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);

	if (material->shader) {
		material->shader->owners.erase(material);
	}

	if (material->data) {
		memdelete(material->data);
		material->data = nullptr;
	}

	if (p_shader.is_null()) {
		material->shader = nullptr;
		material->shader_id = 0;
		material->shader_mode = RS::SHADER_MAX;
		return;
	}

	Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);

	material->shader = shader;
	material->shader_id = p_shader.get_id();
	material->shader_mode = shader->mode;

	if (shader->data) {
		if (shader->mode == RS::SHADER_CANVAS_ITEM) {
			material->data = GLES2::_create_canvas_material_func(shader->data);
		} else if (shader->mode == RS::SHADER_PARTICLES) {
			material->data = GLES2::_create_particles_material_func(shader->data);
		}
		// TODO(GLES2): Add Spatial material allocation when 3D is implemented
	}

	shader->owners.insert(material);

	_material_queue_update(material, true, true);
}

void MaterialStorage::material_set_param(RID p_material, const StringName &p_param, const Variant &p_value) {
	Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);

	if (p_value.get_type() == Variant::NIL) {
		material->params.erase(p_param);
	} else {
		material->params[p_param] = p_value;
	}

	// Tell the queue what kind of update this is
	if (p_value.get_type() == Variant::OBJECT || p_value.get_type() == Variant::RID) {
		_material_queue_update(material, false, true); // Texture dirty
	} else {
		_material_queue_update(material, true, false); // Uniform dirty
	}
}

Variant MaterialStorage::material_get_param(RID p_material, const StringName &p_param) const {
	const GLES2::Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL_V(material, Variant());
	if (material->params.has(p_param)) {
		return material->params[p_param];
	} else {
		return Variant();
	}
}

void MaterialStorage::material_set_next_pass(RID p_material, RID p_next_material) {
	GLES2::Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);

	if (material->next_pass == p_next_material) {
		return;
	}

	material->next_pass = p_next_material;
	if (material->data) {
		material->data->set_next_pass(p_next_material);
	}

	material->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
}

void MaterialStorage::material_set_render_priority(RID p_material, int priority) {
	ERR_FAIL_COND(priority < RS::MATERIAL_RENDER_PRIORITY_MIN);
	ERR_FAIL_COND(priority > RS::MATERIAL_RENDER_PRIORITY_MAX);

	GLES2::Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);
	material->priority = priority;
	if (material->data) {
		material->data->set_render_priority(priority);
	}
	material->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
}

bool MaterialStorage::material_is_animated(RID p_material) {
	GLES2::Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL_V(material, false);
	if (material->shader && material->shader->data) {
		if (material->shader->data->is_animated()) {
			return true;
		} else if (material->next_pass.is_valid()) {
			return material_is_animated(material->next_pass);
		}
	}
	return false; //by default nothing is animated
}

bool MaterialStorage::material_casts_shadows(RID p_material) {
	return true; //by default everything casts shadows
}

RS::CullMode MaterialStorage::material_get_cull_mode(RID p_material) const {
	return RS::CULL_MODE_DISABLED;
}

void MaterialStorage::material_get_instance_shader_parameters(RID p_material, List<InstanceShaderParam> *r_parameters) {
	GLES2::Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);
	if (material->shader && material->shader->data) {
		material->shader->data->get_instance_param_list(r_parameters);

		if (material->next_pass.is_valid()) {
			material_get_instance_shader_parameters(material->next_pass, r_parameters);
		}
	}
}

void MaterialStorage::material_update_dependency(RID p_material, DependencyTracker *p_instance) {
	Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);
	p_instance->update_dependency(&material->dependency);
	if (material->next_pass.is_valid()) {
		material_update_dependency(material->next_pass, p_instance);
	}
}

_FORCE_INLINE_ LocalVector<ShaderGLES2::TextureUniformData> get_texture_uniform_data(const Vector<ShaderCompiler::GeneratedCode::Texture> &texture_uniforms) {
	LocalVector<ShaderGLES2::TextureUniformData> texture_uniform_data;
	for (int i = 0; i < texture_uniforms.size(); i++) {
		int num_textures = texture_uniforms[i].array_size;
		if (num_textures == 0) {
			num_textures = 1;
		}
		texture_uniform_data.push_back({ texture_uniforms[i].name, num_textures });
	}
	return texture_uniform_data;
}

/* Canvas Shader Data */

void CanvasShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader

	code = p_code;
	valid = false;
	ubo_size = 0;
	uniforms.clear();

	uses_screen_texture = false;
	uses_screen_texture_mipmaps = false;
	uses_sdf = false;
	uses_time = false;
	uses_custom0 = false;
	uses_custom1 = false;

	if (code.is_empty()) {
		return; // Just invalid, but no error
	}

	ShaderCompiler::GeneratedCode gen_code;

	// Actual enum set further down after compilation
	int blend_modei = BLEND_MODE_MIX;

	ShaderCompiler::IdentifierActions actions;
	actions.entry_point_stages["vertex"] = ShaderCompiler::STAGE_VERTEX;
	actions.entry_point_stages["fragment"] = ShaderCompiler::STAGE_FRAGMENT;
	actions.entry_point_stages["light"] = ShaderCompiler::STAGE_FRAGMENT;

	actions.render_mode_values["blend_add"] = Pair<int *, int>(&blend_modei, BLEND_MODE_ADD);
	actions.render_mode_values["blend_mix"] = Pair<int *, int>(&blend_modei, BLEND_MODE_MIX);
	actions.render_mode_values["blend_sub"] = Pair<int *, int>(&blend_modei, BLEND_MODE_SUB);
	actions.render_mode_values["blend_mul"] = Pair<int *, int>(&blend_modei, BLEND_MODE_MUL);
	actions.render_mode_values["blend_premul_alpha"] = Pair<int *, int>(&blend_modei, BLEND_MODE_PMALPHA);
	actions.render_mode_values["blend_disabled"] = Pair<int *, int>(&blend_modei, BLEND_MODE_DISABLED);

	actions.usage_flag_pointers["texture_sdf"] = &uses_sdf;
	actions.usage_flag_pointers["TIME"] = &uses_time;
	actions.usage_flag_pointers["CUSTOM0"] = &uses_custom0;
	actions.usage_flag_pointers["CUSTOM1"] = &uses_custom1;

	actions.uniforms = &uniforms;
	Error err = MaterialStorage::get_singleton()->shaders.compiler_canvas.compile(RS::SHADER_CANVAS_ITEM, code, &actions, path, gen_code);
	ERR_FAIL_COND_MSG(err != OK, "Shader compilation failed.");

	if (version.is_null()) {
		version = MaterialStorage::get_singleton()->shaders.canvas_shader.version_create();
	}

	blend_mode = BlendMode(blend_modei);
	uses_screen_texture = gen_code.uses_screen_texture;
	uses_screen_texture_mipmaps = gen_code.uses_screen_texture_mipmaps;

#if 0
	print_line("**compiling shader:");
	print_line("**defines:\n");
	for (int i = 0; i < gen_code.defines.size(); i++) {
		print_line(gen_code.defines[i]);
	}

	HashMap<String, String>::Iterator el = gen_code.code.begin();
	while (el) {
		print_line("\n**code " + el->key + ":\n" + el->value);
		++el;
	}

	print_line("\n**uniforms:\n" + gen_code.uniforms);
	print_line("\n**vertex_globals:\n" + gen_code.stage_globals[ShaderCompiler::STAGE_VERTEX]);
	print_line("\n**fragment_globals:\n" + gen_code.stage_globals[ShaderCompiler::STAGE_FRAGMENT]);
#endif

	LocalVector<ShaderGLES2::TextureUniformData> texture_uniform_data = get_texture_uniform_data(gen_code.texture_uniforms);

	MaterialStorage::get_singleton()->shaders.canvas_shader.version_set_code(version, gen_code.code, gen_code.uniforms, gen_code.stage_globals[ShaderCompiler::STAGE_VERTEX], gen_code.stage_globals[ShaderCompiler::STAGE_FRAGMENT], gen_code.defines, texture_uniform_data, true);
	ERR_FAIL_COND(!MaterialStorage::get_singleton()->shaders.canvas_shader.version_is_valid(version));

	vertex_input_mask = RS::ARRAY_FORMAT_VERTEX | RS::ARRAY_FORMAT_COLOR | RS::ARRAY_FORMAT_TEX_UV;
	vertex_input_mask |= uses_custom0 << RS::ARRAY_CUSTOM0;
	vertex_input_mask |= uses_custom1 << RS::ARRAY_CUSTOM1;

	ubo_size = gen_code.uniform_total_size;
	ubo_offsets = gen_code.uniform_offsets;
	texture_uniforms = gen_code.texture_uniforms;
	valid = true;
}

bool CanvasShaderData::is_animated() const {
	return false;
}

bool CanvasShaderData::casts_shadows() const {
	return false;
}

RS::ShaderNativeSourceCode CanvasShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.canvas_shader.version_get_native_source_code(version);
}

CanvasShaderData::CanvasShaderData() {
	valid = false;
	uses_screen_texture = false;
	uses_screen_texture_mipmaps = false;
	uses_sdf = false;
	uses_time = false;
	uses_custom0 = false;
	uses_custom1 = false;
	vertex_input_mask = 0;
	blend_mode = BLEND_MODE_MIX;
	ubo_size = 0;
}

CanvasShaderData::~CanvasShaderData() {
	if (version.is_valid() && MaterialStorage::get_singleton()) {
		MaterialStorage::get_singleton()->shaders.canvas_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_canvas_shader_func() {
	CanvasShaderData *shader_data = memnew(CanvasShaderData);
	return shader_data;
}

void CanvasMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, false);
}

void CanvasMaterialData::bind_uniforms() {
	GLint prog = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	if (unlikely(prog == 0)) {
		// Guarantee baseline texture unit before error exit
		glActiveTexture(GL_TEXTURE0);
		ERR_FAIL_MSG("GLES2: CanvasMaterialData failed to bind uniforms because GL_CURRENT_PROGRAM is 0.");
	}

	// Unpack the custom UBO data and push via glUniform
	if (!ubo_data.is_empty()) {
		const uint8_t *data = ubo_data.ptr();

		for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : shader_data->uniforms) {
			if (E.value.order < 0) {
				continue; // Skip Textures
			}
			if (E.value.scope == ShaderLanguage::ShaderNode::Uniform::SCOPE_INSTANCE) {
				continue;
			}

			// Godot 4 shader compiler always prefixes custom uniforms with "m_"
			String uniform_name = "m_" + String(E.key);
			GLint loc = glGetUniformLocation(prog, uniform_name.utf8().get_data());

			if (loc < 0) {
				// In case it is not prefixed with `m_`
				uniform_name = String(E.key);
				loc = glGetUniformLocation(prog, uniform_name.utf8().get_data());
			}

			if (loc < 0) {
				continue; // Uniform was optimized out by GLSL
			}

			uint32_t offset = shader_data->ubo_offsets[E.value.order];
			const uint8_t *val_ptr = &data[offset];
			int count = E.value.array_size > 0 ? E.value.array_size : 1;

			if (count > 1) {
				// Uniform Arrays require unpacking from std140 before submitting to GLES2
				switch (E.value.type) {
					case ShaderLanguage::TYPE_BOOL:
					case ShaderLanguage::TYPE_INT:
					case ShaderLanguage::TYPE_UINT: {
						LocalVector<int32_t> tmp;
						tmp.resize(count);
						for (int i = 0; i < count; i++) {
							tmp[i] = *(const int32_t *)&val_ptr[i * 16]; // std140 arrays align to 16 bytes
						}
						glUniform1iv(loc, count, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_FLOAT: {
						LocalVector<float> tmp;
						tmp.resize(count);
						for (int i = 0; i < count; i++) {
							tmp[i] = *(const float *)&val_ptr[i * 16];
						}
						glUniform1fv(loc, count, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_VEC2: {
						LocalVector<float> tmp;
						tmp.resize(count * 2);
						for (int i = 0; i < count; i++) {
							tmp[i * 2 + 0] = ((const float *)&val_ptr[i * 16])[0];
							tmp[i * 2 + 1] = ((const float *)&val_ptr[i * 16])[1];
						}
						glUniform2fv(loc, count, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_VEC3: {
						LocalVector<float> tmp;
						tmp.resize(count * 3);
						for (int i = 0; i < count; i++) {
							tmp[i * 3 + 0] = ((const float *)&val_ptr[i * 16])[0];
							tmp[i * 3 + 1] = ((const float *)&val_ptr[i * 16])[1];
							tmp[i * 3 + 2] = ((const float *)&val_ptr[i * 16])[2];
						}
						glUniform3fv(loc, count, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_VEC4: {
						LocalVector<float> tmp;
						tmp.resize(count * 4);
						for (int i = 0; i < count; i++) {
							tmp[i * 4 + 0] = ((const float *)&val_ptr[i * 16])[0];
							tmp[i * 4 + 1] = ((const float *)&val_ptr[i * 16])[1];
							tmp[i * 4 + 2] = ((const float *)&val_ptr[i * 16])[2];
							tmp[i * 4 + 3] = ((const float *)&val_ptr[i * 16])[3];
						}
						glUniform4fv(loc, count, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_MAT2: {
						LocalVector<float> tmp;
						tmp.resize(count * 4);
						for (int i = 0; i < count; i++) {
							tmp[i * 4 + 0] = ((const float *)&val_ptr[i * 32])[0];
							tmp[i * 4 + 1] = ((const float *)&val_ptr[i * 32])[1];
							tmp[i * 4 + 2] = ((const float *)&val_ptr[i * 32])[4];
							tmp[i * 4 + 3] = ((const float *)&val_ptr[i * 32])[5];
						}
						glUniformMatrix2fv(loc, count, GL_FALSE, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_MAT3: {
						LocalVector<float> tmp;
						tmp.resize(count * 9);
						for (int i = 0; i < count; i++) {
							tmp[i * 9 + 0] = ((const float *)&val_ptr[i * 48])[0];
							tmp[i * 9 + 1] = ((const float *)&val_ptr[i * 48])[1];
							tmp[i * 9 + 2] = ((const float *)&val_ptr[i * 48])[2];
							tmp[i * 9 + 3] = ((const float *)&val_ptr[i * 48])[4];
							tmp[i * 9 + 4] = ((const float *)&val_ptr[i * 48])[5];
							tmp[i * 9 + 5] = ((const float *)&val_ptr[i * 48])[6];
							tmp[i * 9 + 6] = ((const float *)&val_ptr[i * 48])[8];
							tmp[i * 9 + 7] = ((const float *)&val_ptr[i * 48])[9];
							tmp[i * 9 + 8] = ((const float *)&val_ptr[i * 48])[10];
						}
						glUniformMatrix3fv(loc, count, GL_FALSE, tmp.ptr());
					} break;
					case ShaderLanguage::TYPE_MAT4: {
						LocalVector<float> tmp;
						tmp.resize(count * 16);
						for (int i = 0; i < count; i++) {
							for (int j = 0; j < 16; j++) {
								tmp[i * 16 + j] = ((const float *)&val_ptr[i * 64])[j];
							}
						}
						glUniformMatrix4fv(loc, count, GL_FALSE, tmp.ptr());
					} break;
					default:
						break;
				}
			} else {
				// Translate the std140 bytes into raw GLES2 bindings (Single Element)
				switch (E.value.type) {
					case ShaderLanguage::TYPE_BOOL:
					case ShaderLanguage::TYPE_INT:
					case ShaderLanguage::TYPE_UINT:
						glUniform1i(loc, *(const int32_t *)val_ptr);
						break;
					case ShaderLanguage::TYPE_FLOAT:
						glUniform1f(loc, *(const float *)val_ptr);
						break;
					case ShaderLanguage::TYPE_VEC2:
						glUniform2f(loc, ((const float *)val_ptr)[0], ((const float *)val_ptr)[1]);
						break;
					case ShaderLanguage::TYPE_VEC3:
						glUniform3f(loc, ((const float *)val_ptr)[0], ((const float *)val_ptr)[1], ((const float *)val_ptr)[2]);
						break;
					case ShaderLanguage::TYPE_VEC4:
						glUniform4f(loc, ((const float *)val_ptr)[0], ((const float *)val_ptr)[1], ((const float *)val_ptr)[2], ((const float *)val_ptr)[3]);
						break;
					case ShaderLanguage::TYPE_MAT4:
						glUniformMatrix4fv(loc, 1, GL_FALSE, (const GLfloat *)val_ptr);
						break;
					case ShaderLanguage::TYPE_MAT2: {
						float m[4] = {};
						m[0] = ((const float *)val_ptr)[0];
						m[1] = ((const float *)val_ptr)[1];
						m[2] = ((const float *)val_ptr)[4];
						m[3] = ((const float *)val_ptr)[5];
						glUniformMatrix2fv(loc, 1, GL_FALSE, m);
					} break;
					case ShaderLanguage::TYPE_MAT3: {
						float m[9] = {};
						m[0] = ((const float *)val_ptr)[0];
						m[1] = ((const float *)val_ptr)[1];
						m[2] = ((const float *)val_ptr)[2];
						m[3] = ((const float *)val_ptr)[4];
						m[4] = ((const float *)val_ptr)[5];
						m[5] = ((const float *)val_ptr)[6];
						m[6] = ((const float *)val_ptr)[8];
						m[7] = ((const float *)val_ptr)[9];
						m[8] = ((const float *)val_ptr)[10];
						glUniformMatrix3fv(loc, 1, GL_FALSE, m);
					} break;
					default:
						break;
				}
			}
		}
	}

	// Bind Textures to their active GL_TEXTURE units
	const RID *textures = texture_cache.ptr();
	const ShaderCompiler::GeneratedCode::Texture *texture_uniforms = shader_data->texture_uniforms.ptr();
	int texture_uniform_index = 0;
	int texture_uniform_count = 0;

	int max_texture_units = GLES2::Config::get_singleton()->max_texture_image_units;
	int max_custom_textures = MAX(1, max_texture_units - 8); // Reserve top 8 for engine

	// Start at 1 to preserve GL_TEXTURE0,
	// which is reserved
	int current_tex_unit = 1;

	for (int ti = 0; ti < texture_cache.size(); ti++) {
		const ShaderCompiler::GeneratedCode::Texture &texture_uniform = texture_uniforms[texture_uniform_index];

		// Tell the custom shader which texture unit to read from
		String tex_name = "m_" + String(texture_uniform.name);
		GLint tex_loc = glGetUniformLocation(prog, tex_name.utf8().get_data());

		if (tex_loc < 0) {
			// In case it is not prefixed with `m_`
			tex_name = String(texture_uniform.name);
			tex_loc = glGetUniformLocation(prog, tex_name.utf8().get_data());
		}

		if (texture_uniform.hint == ShaderLanguage::ShaderNode::Uniform::HINT_SCREEN_TEXTURE) {
			// Map directly to the engine's backbuffer texture unit.
			if (tex_loc >= 0) {
				glUniform1i(tex_loc + texture_uniform_count, max_texture_units - 4);
			}
		} else if (texture_uniform.hint == ShaderLanguage::ShaderNode::Uniform::HINT_NORMAL_ROUGHNESS_TEXTURE) {
			// TODO(GLES2): Map screen normal hints to the dedicated normal
			// unit after implementing screen normals later
			if (tex_loc >= 0) {
				glUniform1i(tex_loc + texture_uniform_count, max_texture_units - 6);
			}
		} else {
			// Standard custom texture mapping
			if (unlikely(current_tex_unit >= max_custom_textures)) {
				ERR_PRINT_ONCE(vformat("GLES2: Custom shader uses too many textures! Hardware limit is %d. Skipping remainder.", max_custom_textures));
				break;
			}

			if (tex_loc >= 0) {
				glUniform1i(tex_loc + texture_uniform_count, current_tex_unit);
			}

			glActiveTexture(GL_TEXTURE0 + current_tex_unit);

			GLES2::Texture *texture = GLES2::TextureStorage::get_singleton()->get_texture(textures[ti]);
			if (texture && texture->tex_id != 0) {
				glBindTexture(texture->target, texture->tex_id);
			} else {
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			current_tex_unit++;
		}

		texture_uniform_count++;
		if (texture_uniform_count >= texture_uniform.array_size) {
			texture_uniform_index++;
			texture_uniform_count = 0;
		}
	}

	// Reset active texture to 0 so we don't accidentally break the geometry drawing
	// pass that happens immediately after this function returns.
	glActiveTexture(GL_TEXTURE0);
}

CanvasMaterialData::~CanvasMaterialData() {
}

GLES2::MaterialData *GLES2::_create_canvas_material_func(ShaderData *p_shader) {
	CanvasMaterialData *material_data = memnew(CanvasMaterialData);
	material_data->shader_data = static_cast<CanvasShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

////////////////////////////////////////////////////////////////////////////////
// SKY SHADER

void SkyShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader.
	valid = true;
}

bool SkyShaderData::is_animated() const {
	return false;
}

bool SkyShaderData::casts_shadows() const {
	return false;
}

RS::ShaderNativeSourceCode SkyShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.sky_shader.version_get_native_source_code(version);
}

SkyShaderData::SkyShaderData() {
	valid = false;
}

SkyShaderData::~SkyShaderData() {
	if (version.is_valid() && MaterialStorage::get_singleton()) {
		MaterialStorage::get_singleton()->shaders.sky_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_sky_shader_func() {
	SkyShaderData *shader_data = memnew(SkyShaderData);
	return shader_data;
}

////////////////////////////////////////////////////////////////////////////////
// Sky material

void SkyMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	uniform_set_updated = true;
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, true);
}

SkyMaterialData::~SkyMaterialData() {
}

GLES2::MaterialData *GLES2::_create_sky_material_func(ShaderData *p_shader) {
	SkyMaterialData *material_data = memnew(SkyMaterialData);
	material_data->shader_data = static_cast<SkyShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

void SkyMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

////////////////////////////////////////////////////////////////////////////////
// Scene SHADER

void SceneShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader.
}

bool SceneShaderData::is_animated() const {
	return (uses_fragment_time && uses_discard) || (uses_vertex_time && uses_vertex);
}

bool SceneShaderData::casts_shadows() const {
	bool has_read_screen_alpha = uses_screen_texture || uses_depth_texture || uses_normal_texture;
	bool has_base_alpha = (uses_alpha && !uses_alpha_clip) || has_read_screen_alpha;
	bool has_alpha = has_base_alpha || uses_blend_alpha;

	return !has_alpha || (uses_depth_prepass_alpha && !(depth_draw == DEPTH_DRAW_DISABLED || depth_test == DEPTH_TEST_DISABLED));
}

RS::ShaderNativeSourceCode SceneShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.scene_shader.version_get_native_source_code(version);
}

SceneShaderData::SceneShaderData() {
	valid = false;
	uses_screen_texture = false;
	uses_screen_texture_mipmaps = false;
	uses_depth_texture = false;
	uses_normal_texture = false;
	uses_time = false;
	uses_vertex_time = false;
	uses_fragment_time = false;
	uses_discard = false;
	uses_roughness = false;
	uses_normal = false;
	uses_particle_trails = false;
	wireframe = false;
	unshaded = false;
	uses_vertex = false;
	uses_position = false;
	uses_sss = false;
	uses_transmittance = false;
	writes_modelview_or_projection = false;
	uses_world_coordinates = false;
	uses_tangent = false;
	uses_color = false;
	uses_uv = false;
	uses_uv2 = false;
	uses_custom0 = false;
	uses_custom1 = false;
	uses_custom2 = false;
	uses_custom3 = false;
	uses_bones = false;
	uses_weights = false;
	uses_alpha = false;
	uses_alpha_clip = false;
	uses_blend_alpha = false;
	uses_depth_prepass_alpha = false;
	uses_point_size = false;

	ubo_size = 0;

	blend_mode = BLEND_MODE_MIX;
	alpha_antialiasing_mode = ALPHA_ANTIALIASING_OFF;
	depth_draw = DEPTH_DRAW_OPAQUE;
	depth_test = DEPTH_TEST_ENABLED;
	cull_mode = RS::CULL_MODE_BACK;
	vertex_input_mask = 0;
}

SceneShaderData::~SceneShaderData() {
	if (version.is_valid() && MaterialStorage::get_singleton()) {
		MaterialStorage::get_singleton()->shaders.scene_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_scene_shader_func() {
	SceneShaderData *shader_data = memnew(SceneShaderData);
	return shader_data;
}

void SceneMaterialData::set_render_priority(int p_priority) {
	priority = p_priority - RS::MATERIAL_RENDER_PRIORITY_MIN; //8 bits
}

void SceneMaterialData::set_next_pass(RID p_pass) {
	next_pass = p_pass;
}

void SceneMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, true);
}

SceneMaterialData::~SceneMaterialData() {
}

GLES2::MaterialData *GLES2::_create_scene_material_func(ShaderData *p_shader) {
	SceneMaterialData *material_data = memnew(SceneMaterialData);
	material_data->shader_data = static_cast<SceneShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

void SceneMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

/* Particles SHADER */

void ParticlesShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader.
}

bool ParticlesShaderData::is_animated() const {
	return false;
}

bool ParticlesShaderData::casts_shadows() const {
	return false;
}

RS::ShaderNativeSourceCode ParticlesShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.particles_process_shader.version_get_native_source_code(version);
}

ParticlesShaderData::~ParticlesShaderData() {
	if (version.is_valid() && MaterialStorage::get_singleton()) {
		MaterialStorage::get_singleton()->shaders.particles_process_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_particles_shader_func() {
	ParticlesShaderData *shader_data = memnew(ParticlesShaderData);
	return shader_data;
}

void ParticleProcessMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, true);
}

ParticleProcessMaterialData::~ParticleProcessMaterialData() {
}

GLES2::MaterialData *GLES2::_create_particles_material_func(ShaderData *p_shader) {
	ParticleProcessMaterialData *material_data = memnew(ParticleProcessMaterialData);
	material_data->shader_data = static_cast<ParticlesShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

void ParticleProcessMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

#endif // !GLES2_ENABLED
