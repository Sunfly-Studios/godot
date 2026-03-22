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

}

Variant ShaderData::get_default_parameter(const StringName &p_parameter) const {
	return Variant();
}

void ShaderData::get_shader_uniform_list(List<PropertyInfo> *p_param_list) const {

}

void ShaderData::get_instance_param_list(List<RendererMaterialStorage::InstanceShaderParam> *p_param_list) const {

}

bool ShaderData::is_parameter_texture(const StringName &p_param) const {
    return false;
}

///////////////////////////////////////////////////////////////////////////
// MaterialData

// Look up table to translate ShaderLanguage::DataType to GL_TEXTURE_*
static const GLenum target_from_type[ShaderLanguage::TYPE_MAX] = {
	GL_TEXTURE_2D, // TYPE_VOID,
	GL_TEXTURE_2D, // TYPE_BOOL,
	GL_TEXTURE_2D, // TYPE_BVEC2,
	GL_TEXTURE_2D, // TYPE_BVEC3,
	GL_TEXTURE_2D, // TYPE_BVEC4,
	GL_TEXTURE_2D, // TYPE_INT,
	GL_TEXTURE_2D, // TYPE_IVEC2,
	GL_TEXTURE_2D, // TYPE_IVEC3,
	GL_TEXTURE_2D, // TYPE_IVEC4,
	GL_TEXTURE_2D, // TYPE_UINT,
	GL_TEXTURE_2D, // TYPE_UVEC2,
	GL_TEXTURE_2D, // TYPE_UVEC3,
	GL_TEXTURE_2D, // TYPE_UVEC4,
	GL_TEXTURE_2D, // TYPE_FLOAT,
	GL_TEXTURE_2D, // TYPE_VEC2,
	GL_TEXTURE_2D, // TYPE_VEC3,
	GL_TEXTURE_2D, // TYPE_VEC4,
	GL_TEXTURE_2D, // TYPE_MAT2,
	GL_TEXTURE_2D, // TYPE_MAT3,
	GL_TEXTURE_2D, // TYPE_MAT4,
	GL_TEXTURE_2D, // TYPE_SAMPLER2D,
	GL_TEXTURE_2D, // TYPE_ISAMPLER2D,
	GL_TEXTURE_2D, // TYPE_USAMPLER2D,
	GL_TEXTURE_2D_ARRAY, // TYPE_SAMPLER2DARRAY,
	GL_TEXTURE_2D_ARRAY, // TYPE_ISAMPLER2DARRAY,
	GL_TEXTURE_2D_ARRAY, // TYPE_USAMPLER2DARRAY,
	GL_TEXTURE_3D, // TYPE_SAMPLER3D,
	GL_TEXTURE_3D, // TYPE_ISAMPLER3D,
	GL_TEXTURE_3D, // TYPE_USAMPLER3D,
	GL_TEXTURE_CUBE_MAP, // TYPE_SAMPLERCUBE,
	GL_TEXTURE_CUBE_MAP, // TYPE_SAMPLERCUBEARRAY,
	_GL_TEXTURE_EXTERNAL_OES, // TYPE_SAMPLEREXT
	GL_TEXTURE_2D, // TYPE_STRUCT
};

static const RS::CanvasItemTextureRepeat repeat_from_uniform[ShaderLanguage::REPEAT_DEFAULT + 1] = {
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, // ShaderLanguage::TextureRepeat::REPEAT_DISABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED, // ShaderLanguage::TextureRepeat::REPEAT_ENABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED, // ShaderLanguage::TextureRepeat::REPEAT_DEFAULT,
};

static const RS::CanvasItemTextureRepeat repeat_from_uniform_canvas[ShaderLanguage::REPEAT_DEFAULT + 1] = {
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, // ShaderLanguage::TextureRepeat::REPEAT_DISABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED, // ShaderLanguage::TextureRepeat::REPEAT_ENABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, // ShaderLanguage::TextureRepeat::REPEAT_DEFAULT,
};

static const RS::CanvasItemTextureFilter filter_from_uniform[ShaderLanguage::FILTER_DEFAULT + 1] = {
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, // ShaderLanguage::TextureFilter::FILTER_NEAREST,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, // ShaderLanguage::TextureFilter::FILTER_LINEAR,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_DEFAULT,
};

static const RS::CanvasItemTextureFilter filter_from_uniform_canvas[ShaderLanguage::FILTER_DEFAULT + 1] = {
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, // ShaderLanguage::TextureFilter::FILTER_NEAREST,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, // ShaderLanguage::TextureFilter::FILTER_LINEAR,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, // ShaderLanguage::TextureFilter::FILTER_DEFAULT,
};

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
				RID gl_texture = texture_storage->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
				if (j < tex_array.size() && tex_array[j].get_type() == Variant::RID) {
					gl_texture = tex_array[j];
				}
				p_textures[k++] = gl_texture;
			}
		} else {
			// Single texture or fallback
			RID gl_texture = texture_storage->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_WHITE);
			if (V && V->value.get_type() == Variant::RID) {
				gl_texture = V->value;
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
}

MaterialStorage::~MaterialStorage() {
	//shaders.copy.version_free(shaders.copy_version);

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
    return RID();
}

void MaterialStorage::shader_initialize(RID p_rid) {

}

void MaterialStorage::shader_free(RID p_rid) {

}

void MaterialStorage::shader_set_code(RID p_shader, const String &p_code) {

}

void MaterialStorage::shader_set_path_hint(RID p_shader, const String &p_path) {

}

String MaterialStorage::shader_get_code(RID p_shader) const {
	return String();
}

void MaterialStorage::get_shader_parameter_list(RID p_shader, List<PropertyInfo> *p_param_list) const {

}

void MaterialStorage::shader_set_default_texture_parameter(RID p_shader, const StringName &p_name, RID p_texture, int p_index) {

}

RID MaterialStorage::shader_get_default_texture_parameter(RID p_shader, const StringName &p_name, int p_index) const {
	return RID();
}

Variant MaterialStorage::shader_get_parameter_default(RID p_shader, const StringName &p_param) const {
	return Variant();
}

RS::ShaderNativeSourceCode MaterialStorage::shader_get_native_source_code(RID p_shader) const {
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
}

void MaterialStorage::material_free(RID p_rid) {
	Material *material = material_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(material);

	// Destroy the CanvasMaterialData and unbinds the shader
	material_set_shader(p_rid, RID());

	// Ensure we don't leave dangling pointers in our update queue
	if (material->update_element.in_list()) {
		material_update_list.remove(&material->update_element);
	}

	material_owner.free(p_rid);
}

void MaterialStorage::material_set_shader(RID p_material, RID p_shader) {
	Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);

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
    return Variant();
}

void MaterialStorage::material_set_next_pass(RID p_material, RID p_next_material) {

}

void MaterialStorage::material_set_render_priority(RID p_material, int priority) {

}

bool MaterialStorage::material_is_animated(RID p_material) {
	return false; //by default nothing is animated
}

bool MaterialStorage::material_casts_shadows(RID p_material) {
	return true; //by default everything casts shadows
}

RS::CullMode MaterialStorage::material_get_cull_mode(RID p_material) const {
	return RS::CULL_MODE_DISABLED;
}

void MaterialStorage::material_get_instance_shader_parameters(RID p_material, List<InstanceShaderParam> *r_parameters) {

}

void MaterialStorage::material_update_dependency(RID p_material, DependencyTracker *p_instance) {

}

LocalVector<ShaderGLES2::TextureUniformData> get_texture_uniform_data(const Vector<ShaderCompiler::GeneratedCode::Texture> &texture_uniforms) {
	return LocalVector<ShaderGLES2::TextureUniformData>();
}

/* Canvas Shader Data */

void CanvasShaderData::set_code(const String &p_code) {

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
}

CanvasShaderData::~CanvasShaderData() {
	if (version.is_valid()) {
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
	ERR_FAIL_COND(prog == 0);

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
				continue; // Uniform was optimized out by GLSL
			}

			uint32_t offset = shader_data->ubo_offsets[E.value.order];
			const uint8_t *val_ptr = &data[offset];

			// Translate the std140 bytes into raw GLES2 bindings
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

	// Bind Textures to their active GL_TEXTURE units
	const RID *textures = texture_cache.ptr();
	const ShaderCompiler::GeneratedCode::Texture *texture_uniforms = shader_data->texture_uniforms.ptr();
	int texture_uniform_index = 0;
	int texture_uniform_count = 0;

	int max_texture_units = GLES2::Config::get_singleton()->max_texture_image_units;
	int max_custom_textures = MAX(1, max_texture_units - 8); // Reserve top 8 for engine

	for (int ti = 0; ti < texture_cache.size(); ti++) {
		if (unlikely(ti >= max_custom_textures)) {
			ERR_PRINT_ONCE(vformat("GLES2: Custom shader uses too many textures! Hardware limit is %d. Skipping remainder.", max_custom_textures));
			break;
		}

		const ShaderCompiler::GeneratedCode::Texture &texture_uniform = texture_uniforms[texture_uniform_index];

		// Tell the custom shader which texture unit to read from
		String tex_name = "m_" + String(texture_uniform.name);
		GLint tex_loc = glGetUniformLocation(prog, tex_name.utf8().get_data());
		if (tex_loc >= 0) {
			glUniform1i(tex_loc, ti);
		}

		GLES2::Texture *texture = GLES2::TextureStorage::get_singleton()->get_texture(textures[ti]);
		if (texture) {
			glActiveTexture(GL_TEXTURE0 + ti);
			glBindTexture(GL_TEXTURE_2D, texture->tex_id);
		}

		texture_uniform_count++;
		if (texture_uniform_count >= texture_uniform.array_size) {
			texture_uniform_index++;
			texture_uniform_count = 0;
		}
	}
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
	if (version.is_valid()) {
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
	if (version.is_valid()) {
		MaterialStorage::get_singleton()->shaders.scene_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_scene_shader_func() {
	SceneShaderData *shader_data = memnew(SceneShaderData);
	return shader_data;
}

void SceneMaterialData::set_render_priority(int p_priority) {
//	priority = p_priority - RS::MATERIAL_RENDER_PRIORITY_MIN; //8 bits
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
	if (version.is_valid()) {
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
