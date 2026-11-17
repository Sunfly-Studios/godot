/**************************************************************************/
/*  rasterizer_scene_batcher_helpers_gles2.h                              */
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

#ifndef RASTERIZER_SCENE_BATCHER_HELPERS_GLES2_H
#define RASTERIZER_SCENE_BATCHER_HELPERS_GLES2_H

/* BATCH API HELPERS */

// NOTE: This is always included _after_ the scene raster header
// so that we can use some of the raster's functions here
// without having to include it.

_FORCE_INLINE_ void _batch_transform_pos(RasterizerSceneGLES2::BatchType::BatchVector3 &r_pos, const Vector3 &p_src_pos, RasterizerSceneGLES2::BatchType::TransformMode3D p_mode, const Transform3D &p_xform) {
	if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_NONE) {
		r_pos.set(p_src_pos);
	} else if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_TRANSLATE) {
		r_pos.set(
			p_src_pos.x + p_xform.origin.x,
			p_src_pos.y + p_xform.origin.y,
			p_src_pos.z + p_xform.origin.z
		);
	} else {
		r_pos.set(p_xform.xform(p_src_pos));
	}
}

_FORCE_INLINE_ void _batch_transform_pos_norm(RasterizerSceneGLES2::BatchType::BatchVector3 &r_pos, RasterizerSceneGLES2::BatchType::BatchNormal &r_norm, const Vector3 &p_src_pos, const Vector3 *p_src_norm, uint32_t p_idx, RasterizerSceneGLES2::BatchType::TransformMode3D p_mode, const Transform3D &p_xform, const Basis &p_cofactor) {
	_batch_transform_pos(r_pos, p_src_pos, p_mode, p_xform);
	if (!p_src_norm) {
		r_norm.set(0.0f, 1.0f, 0.0f);
		return;
	}

	if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_NONE || p_mode == RasterizerSceneGLES2::BatchType::TM_3D_TRANSLATE) {
		r_norm.set(p_src_norm[p_idx]);
	} else if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_ORTHOGONAL) {
		r_norm.set(p_xform.basis.xform(p_src_norm[p_idx]));
	} else if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_UNIFORM_SCALE) {
		r_norm.set(p_cofactor.xform(p_src_norm[p_idx]));
	} else {
		Vector3 n = p_cofactor.xform(p_src_norm[p_idx]);
		const float len_sq = n.length_squared();
		if (len_sq > 0.0f) {
			r_norm.set(n * (1.0f / Math::sqrt(len_sq)));
		} else {
			r_norm.set(0.0f, 1.0f, 0.0f);
		}
	}
}

_FORCE_INLINE_ void _batch_transform_tangent(RasterizerSceneGLES2::BatchType::BatchTangent &r_tan, const float *p_src_tan, uint32_t p_idx, RasterizerSceneGLES2::BatchType::TransformMode3D p_mode, const Transform3D &p_xform, const Basis &p_cofactor) {
	if (!p_src_tan) {
		r_tan.set(1.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	Vector3 t(p_src_tan[p_idx * 4 + 0], p_src_tan[p_idx * 4 + 1], p_src_tan[p_idx * 4 + 2]);
	if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_ORTHOGONAL) {
		t = p_xform.basis.xform(t);
	} else if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_UNIFORM_SCALE) {
		t = p_cofactor.xform(t);
	} else if (p_mode == RasterizerSceneGLES2::BatchType::TM_3D_ALL) {
		t = p_cofactor.xform(t);
		const float len_sq = t.length_squared();
		t = len_sq > 0.0f ? t * (1.0f / Math::sqrt(len_sq)) : Vector3(1.0f, 0.0f, 0.0f);
	}
	r_tan.set(t.x, t.y, t.z, p_src_tan[p_idx * 4 + 3]);
}

template <typename T_VERTEX>
_FORCE_INLINE_ void _batch_write_tier_secondary_attribs(
	T_VERTEX &r_dst,
	uint32_t p_idx,
	const Vector2 *p_uv_ptr,
	const Vector2 *p_uv2_ptr,
	const Color *p_col_ptr,
	const Color &p_inst_color_mul,
	bool p_mul_color,
	const RasterizerSceneGLES2::BatchType::BatchColor &p_fallback_color,
	const float *p_tan_ptr,
	const RasterizerSceneGLES2::BatchType::BatchTangent *p_cached_tan,
	RasterizerSceneGLES2::BatchType::TransformMode3D p_mode,
	const Transform3D &p_xform,
	const Basis &p_cofactor,
	uint8_t p_matrix_idx
) {
	if (p_uv_ptr) {
		r_dst.uv.set(p_uv_ptr[p_idx]);
	} else {
		r_dst.uv.set(0.0f, 0.0f);
	}

	if constexpr (
		std::is_same_v<T_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DTier2> ||
		std::is_same_v<T_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DTier3> ||
		std::is_same_v<T_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DTier4>
	) {
		if (p_col_ptr) {
			r_dst.color.set(p_mul_color ? (p_col_ptr[p_idx] * p_inst_color_mul) : p_col_ptr[p_idx]);
		} else {
			r_dst.color = p_fallback_color;
		}
		if (p_uv2_ptr) {
			r_dst.uv2.set(p_uv2_ptr[p_idx]);
		} else {
			r_dst.uv2.set(0.0f, 0.0f);
		}
	}

	if constexpr (
		std::is_same_v<T_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DTier3> ||
		std::is_same_v<T_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DTier4>
	) {
		if (p_cached_tan) {
			r_dst.tangent = p_cached_tan[p_idx];
		} else {
			_batch_transform_tangent(r_dst.tangent, p_tan_ptr, p_idx, p_mode, p_xform, p_cofactor);
		}
	}

	if constexpr (std::is_same_v<T_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DTier4>) {
		r_dst.matrix_index[0] = p_matrix_idx;
		r_dst.matrix_index[1] = 0;
		r_dst.matrix_index[2] = 0;
		r_dst.matrix_index[3] = 0;
		r_dst.pad0 = 0;
		r_dst.weights.set(1.0f, 0.0f, 0.0f, 0.0f);
	}
}

template <typename T_DEPTH_VERTEX>
_FORCE_INLINE_ void _batch_write_depth_vertex(
	T_DEPTH_VERTEX &r_dst,
	uint32_t p_idx,
	const Vector3 *p_pos_ptr,
	const Vector2 *p_uv_ptr,
	const Color *p_col_ptr,
	const RasterizerSceneGLES2::BatchType::BatchColor &p_fallback_color,
	const RasterizerSceneGLES2::BatchType::BatchVertex3DTier1 *p_world_cache,
	RasterizerSceneGLES2::BatchType::TransformMode3D p_mode,
	const Transform3D &p_xform,
	uint8_t p_matrix_idx
) {
	if (p_world_cache) {
		r_dst.pos = p_world_cache[p_idx].pos;
	} else {
		_batch_transform_pos(r_dst.pos, p_pos_ptr[p_idx], p_mode, p_xform);
	}
	r_dst.matrix_index[0] = p_matrix_idx;
	r_dst.matrix_index[1] = 0;
	r_dst.matrix_index[2] = 0;
	r_dst.matrix_index[3] = 0;

	if constexpr (std::is_same_v<T_DEPTH_VERTEX, RasterizerSceneGLES2::BatchType::BatchVertex3DDepthAlphaMinimal>) {
		if (p_world_cache) {
			r_dst.uv = p_world_cache[p_idx].uv;
		} else if (p_uv_ptr) {
			r_dst.uv.set(p_uv_ptr[p_idx]);
		} else {
			r_dst.uv.set(0.0f, 0.0f);
		}
		r_dst.weight = 1.0f;
		if (p_col_ptr) {
			r_dst.color.set(p_col_ptr[p_idx]);
		} else {
			r_dst.color = p_fallback_color;
		}
	}
}

_FORCE_INLINE_ void _batch_fill_indices_single(uint16_t *r_inds, const PackedInt32Array &p_indices, uint32_t p_v_count, uint32_t p_start_vert) {
	if (!r_inds) {
		return;
	}
	if (p_indices.size() > 0) {
		const uint32_t i_count = p_indices.size();
		const int32_t *idx_ptr = p_indices.ptr();
		for (uint32_t i = 0; i < i_count; i++) {
			r_inds[i] = static_cast<uint16_t>(idx_ptr[i] + p_start_vert);
		}
	} else {
		// Auto-generate sequential indices for unindexed meshes.
		for (uint32_t i = 0; i < p_v_count; i++) {
			r_inds[i] = static_cast<uint16_t>(i + p_start_vert);
		}
	}
}

_FORCE_INLINE_ void _batch_fill_indices_multimesh(uint16_t *r_inds, const PackedInt32Array &p_indices, uint32_t p_v_count, int p_instances, uint32_t p_start_vert, RS::PrimitiveType p_primitive) {
	if (!r_inds) {
		return;
	}
	const bool is_strip = (
		p_primitive == RS::PRIMITIVE_TRIANGLE_STRIP ||
		p_primitive == RS::PRIMITIVE_LINE_STRIP
	);
	uint32_t inds_idx = 0;
	if (p_indices.size() > 0) {
		const uint32_t i_count = p_indices.size();
		const int32_t *idx_ptr = p_indices.ptr();
		for (int inst = 0; inst < p_instances; inst++) {
			const uint32_t base_v = p_start_vert + (inst * p_v_count);
			if (is_strip && inst > 0) {
				r_inds[inds_idx] = r_inds[inds_idx - 1];
				r_inds[inds_idx + 1] = static_cast<uint16_t>(idx_ptr[0] + base_v);
				inds_idx += 2;
			}
			for (uint32_t i = 0; i < i_count; i++) {
				r_inds[inds_idx++] = static_cast<uint16_t>(idx_ptr[i] + base_v);
			}
		}
	} else {
		for (int inst = 0; inst < p_instances; inst++) {
			const uint32_t base_v = p_start_vert + (inst * p_v_count);
			if (is_strip && inst > 0) {
				r_inds[inds_idx] = r_inds[inds_idx - 1];
				r_inds[inds_idx + 1] = static_cast<uint16_t>(base_v);
				inds_idx += 2;
			}
			for (uint32_t i = 0; i < p_v_count; i++) {
				r_inds[inds_idx++] = static_cast<uint16_t>(i + base_v);
			}
		}
	}
}

template <typename T_BATCH_TYPE>
_FORCE_INLINE_ void _batch_fill_tier(
	T_BATCH_TYPE *RESTRICT p_dst,
	uint32_t p_v_count,
	const Vector3 *p_pos_ptr,
	const Vector3 *p_norm_ptr,
	const float *p_tan_ptr,
	const Vector2 *p_uv_ptr,
	const Vector2 *p_uv2_ptr,
	const Color *p_col_ptr,
	const RasterizerSceneGLES2::BatchType::BatchVertex3DTier1 *RESTRICT p_cache_base,
	const RasterizerSceneGLES2::BatchType::BatchTangent *RESTRICT p_cache_tan,
	const RasterizerSceneGLES2::BatchType::BItemRef3D &p_ref,
	uint8_t p_m_idx
) {
	const Color dummy_color(1.0f, 1.0f, 1.0f, 1.0f);
	const Basis identity_basis;
	for (uint32_t i = 0; i < p_v_count; i++) {
		if (p_cache_base) {
			p_dst[i].pos = p_cache_base[i].pos;
			p_dst[i].normal = p_cache_base[i].normal;
		} else {
			_batch_transform_pos_norm(p_dst[i].pos, p_dst[i].normal, p_pos_ptr[i], p_norm_ptr, i, RasterizerSceneGLES2::BatchType::TM_3D_NONE, p_ref.transform, identity_basis);
		}
		_batch_write_tier_secondary_attribs(
			p_dst[i],
			i,
			p_uv_ptr,
			p_uv2_ptr,
			p_col_ptr,
			dummy_color,
			false,
			p_ref.modulate,
			p_tan_ptr,
			p_cache_tan,
			RasterizerSceneGLES2::BatchType::TM_3D_NONE,
			p_ref.transform,
			identity_basis,
			p_m_idx
		);
	}
}

template <typename T_BATCH_TYPE>
_FORCE_INLINE_ void _batch_fill_multimesh_tier(
	T_BATCH_TYPE *RESTRICT p_dst,
	int p_instances,
	uint32_t p_v_count,
	bool p_is_multimesh,
	bool p_mm_uses_colors,
	const float *p_mm_data,
	uint32_t p_mm_stride,
	uint32_t p_mm_color_offset,
	const Vector3 *p_pos_ptr,
	const Vector3 *p_norm_ptr,
	const float *p_tan_ptr,
	const Vector2 *p_uv_ptr,
	const Vector2 *p_uv2_ptr,
	const Color *p_col_ptr,
	const Transform3D *RESTRICT p_xform_dst,
	const RasterizerSceneGLES2::BatchType::TransformMode3D *RESTRICT p_mode_dst,
	const Basis *RESTRICT p_cofactor_dst,
	const RasterizerSceneGLES2::BatchType::BatchColor *RESTRICT p_color_dst,
	uint8_t p_m_idx
) {
	const Basis identity_basis;
	uint32_t bvs_idx = 0;
	for (int inst = 0; inst < p_instances; inst++) {
		const Transform3D &world_xform = p_xform_dst[inst];
		const RasterizerSceneGLES2::BatchType::TransformMode3D mode = p_mode_dst[inst];
		const Basis &cofactor = p_cofactor_dst ? p_cofactor_dst[inst] : identity_basis;
		const RasterizerSceneGLES2::BatchType::BatchColor &quant_col = p_color_dst[inst];

		Color raw_col(1.0f, 1.0f, 1.0f, 1.0f);
		const bool mul_color = (p_is_multimesh && p_mm_uses_colors && p_col_ptr != nullptr);
		if (mul_color) {
			const float *cdata = p_mm_data + (inst * p_mm_stride) + p_mm_color_offset;
			raw_col = Color(cdata[0], cdata[1], cdata[2], cdata[3]);
		}

		const uint8_t inst_m_idx = static_cast<uint8_t>(inst & 0xFF);
		for (uint32_t i = 0; i < p_v_count; i++) {
			_batch_transform_pos_norm(p_dst[bvs_idx].pos, p_dst[bvs_idx].normal, p_pos_ptr[i], p_norm_ptr, i, mode, world_xform, cofactor);
			_batch_write_tier_secondary_attribs(
				p_dst[bvs_idx],
				i,
				p_uv_ptr,
				p_uv2_ptr,
				p_col_ptr,
				raw_col,
				mul_color,
				quant_col,
				p_tan_ptr,
				nullptr,
				mode,
				world_xform,
				cofactor,
				inst_m_idx
			);
			bvs_idx++;
		}
	}
}

template <typename T_BATCH_TYPE>
_FORCE_INLINE_ void _batch_fill_depth(
	T_BATCH_TYPE *RESTRICT p_dst,
	uint32_t p_v_count,
	const Vector3 *p_pos_ptr,
	const Vector2 *p_uv_ptr,
	const Color *p_col_ptr,
	const RasterizerSceneGLES2::BatchType::BatchColor &p_fallback_color,
	const RasterizerSceneGLES2::BatchType::BatchVertex3DTier1 *RESTRICT p_world_cache,
	RasterizerSceneGLES2::BatchType::TransformMode3D p_effective_mode,
	const Transform3D &p_xform,
	uint8_t p_m_idx
) {
	for (uint32_t i = 0; i < p_v_count; i++) {
		_batch_write_depth_vertex(p_dst[i], i, p_pos_ptr, p_uv_ptr, p_col_ptr, p_fallback_color, p_world_cache, p_effective_mode, p_xform, p_m_idx);
	}
}

template <typename T_BATCH_TYPE>
_FORCE_INLINE_ void _batch_fill_multimesh_depth_vertices(
	T_BATCH_TYPE *RESTRICT p_dst,
	int p_instances,
	uint32_t p_v_count,
	bool p_is_multimesh,
	const float *p_mm_data,
	uint32_t p_mm_stride,
	RS::MultimeshTransformFormat p_mm_format,
	bool p_use_local_xform,
	const Transform3D &p_ref_xform,
	const Vector3 *p_pos_ptr,
	const Vector2 *p_uv_ptr,
	const Color *p_col_ptr,
	const RasterizerSceneGLES2::BatchType::BatchColor &p_fallback_color,
	uint8_t p_m_idx
) {
	uint32_t bvs_idx = 0;
	for (int inst = 0; inst < p_instances; inst++) {
		Transform3D xform;
		if (p_is_multimesh) {
			const float *d = p_mm_data + (inst * p_mm_stride);
			if (p_mm_format == RS::MULTIMESH_TRANSFORM_3D) {
				xform.basis.rows[0] = Vector3(d[0], d[1], d[2]);
				xform.origin.x = d[3];
				xform.basis.rows[1] = Vector3(d[4], d[5], d[6]);
				xform.origin.y = d[7];
				xform.basis.rows[2] = Vector3(d[8], d[9], d[10]);
				xform.origin.z = d[11];
			} else {
				xform.basis.rows[0] = Vector3(d[0], d[1], 0.0f);
				xform.origin.x = d[3];
				xform.basis.rows[1] = Vector3(d[4], d[5], 0.0f);
				xform.origin.y = d[7];
			}
		}
		const Transform3D world_xform = p_use_local_xform ? xform : (p_ref_xform * xform);
		const RasterizerSceneGLES2::BatchType::TransformMode3D mode = RasterizerSceneGLES2::_batch_find_transform_mode_3d(world_xform);

		const uint8_t inst_m_idx = static_cast<uint8_t>(inst & 0xFF);
		for (uint32_t i = 0; i < p_v_count; i++) {
			_batch_write_depth_vertex(p_dst[bvs_idx], i, p_pos_ptr, p_uv_ptr, p_col_ptr, p_fallback_color, nullptr, mode, world_xform, inst_m_idx);
			bvs_idx++;
		}
	}
}

#endif // RASTERIZER_SCENE_BATCHER_HELPERS_GLES2_H
