/**************************************************************************/
/*  common_math.h                                                         */
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

#ifndef COMMON_MATH_GLES_COMMON_H
#define COMMON_MATH_GLES_COMMON_H

#include "core/typedefs.h"
#include "platform_gl.h"

_FORCE_INLINE_ uint32_t _gl_indices_to_primitives_2d(GLenum p_primitive, uint32_t p_indices) {
	switch (p_primitive) {
		case GL_POINTS:
			return p_indices;
		case GL_LINES:
			return p_indices / 2;
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			return p_indices > 1 ? p_indices - 1 : 0;
		case GL_TRIANGLES:
			return p_indices / 3;
		case GL_TRIANGLE_STRIP:
		case GL_TRIANGLE_FAN:
			return p_indices > 2 ? p_indices - 2 : 0;
		default:
			return 0;
	}
}

_FORCE_INLINE_ uint32_t _gl_indices_to_primitives_3d(RS::PrimitiveType p_primitive, uint32_t p_indices) {
    constexpr uint32_t DIVISOR[RS::PRIMITIVE_MAX] = { 1, 2, 1, 3, 1 };
    constexpr uint32_t SUBSTRACTOR[RS::PRIMITIVE_MAX] = { 0, 0, 1, 0, 1 };

    if (
        p_primitive <= 0 &&
        unlikely(p_primitive >= RS::PRIMITIVE_MAX || p_indices < SUBSTRACTOR[p_primitive])
    ) {
        return 0;
    }
    return (p_indices - SUBSTRACTOR[p_primitive]) / DIVISOR[p_primitive];
}

_FORCE_INLINE_ const float *_gl_load_transform(const Transform3D &p_transform) {
	static float m[16] = {};

	// Column 0
	m[0] = static_cast<float>(p_transform.basis.rows[0][0]);
	m[1] = static_cast<float>(p_transform.basis.rows[1][0]);
	m[2] = static_cast<float>(p_transform.basis.rows[2][0]);
	m[3] = 0.0f;

	// Column 1
	m[4] = static_cast<float>(p_transform.basis.rows[0][1]);
	m[5] = static_cast<float>(p_transform.basis.rows[1][1]);
	m[6] = static_cast<float>(p_transform.basis.rows[2][1]);
	m[7] = 0.0f;

	// Column 2
	m[8] = static_cast<float>(p_transform.basis.rows[0][2]);
	m[9] = static_cast<float>(p_transform.basis.rows[1][2]);
	m[10] = static_cast<float>(p_transform.basis.rows[2][2]);
	m[11] = 0.0f;

	// Column 3
	m[12] = static_cast<float>(p_transform.origin.x);
	m[13] = static_cast<float>(p_transform.origin.y);
	m[14] = static_cast<float>(p_transform.origin.z);
	m[15] = 1.0f;

	return m;
}

_FORCE_INLINE_ const float *_gl_mult_transform(const Transform3D &p_transform) {
	return _gl_load_transform(p_transform);
}

_FORCE_INLINE_ const float *_gl_load_projection(const Projection &p_proj) {
	static float m[16] = {};

	// Column 0
	m[0] = static_cast<float>(p_proj.columns[0][0]);
	m[1] = static_cast<float>(p_proj.columns[0][1]);
	m[2] = static_cast<float>(p_proj.columns[0][2]);
	m[3] = static_cast<float>(p_proj.columns[0][3]);

	// Column 1
	m[4] = static_cast<float>(p_proj.columns[1][0]);
	m[5] = static_cast<float>(p_proj.columns[1][1]);
	m[6] = static_cast<float>(p_proj.columns[1][2]);
	m[7] = static_cast<float>(p_proj.columns[1][3]);

	// Column 2
	m[8] = static_cast<float>(p_proj.columns[2][0]);
	m[9] = static_cast<float>(p_proj.columns[2][1]);
	m[10] = static_cast<float>(p_proj.columns[2][2]);
	m[11] = static_cast<float>(p_proj.columns[2][3]);

	// Column 3
	m[12] = static_cast<float>(p_proj.columns[3][0]);
	m[13] = static_cast<float>(p_proj.columns[3][1]);
	m[14] = static_cast<float>(p_proj.columns[3][2]);
	m[15] = static_cast<float>(p_proj.columns[3][3]);

	return m;
}

_FORCE_INLINE_ const float *_gl_mult_projection(const Projection &p_proj) {
	return _gl_load_projection(p_proj);
}

_FORCE_INLINE_ Basis _gl_array_to_basis(const float *p_array) {
	Basis basis;
	basis.set_column(0, Vector3(p_array[0], p_array[1], p_array[2]));
	basis.set_column(1, Vector3(p_array[4], p_array[5], p_array[6]));
	basis.set_column(2, Vector3(p_array[8], p_array[9], p_array[10]));
	return basis;
}

_FORCE_INLINE_ void _gl_reconstruct_view_matrix(Transform3D &view_matrix, const float *m) {
    view_matrix.basis.set_column(0, Vector3(m[0], m[1], m[2]));
    view_matrix.basis.set_column(1, Vector3(m[4], m[5], m[6]));
    view_matrix.basis.set_column(2, Vector3(m[8], m[9], m[10]));
    view_matrix.origin = Vector3(m[12], m[13], m[14]);
}

_FORCE_INLINE_ Projection _gl_array_to_projection(const float *p_array) {
    Projection proj;
    proj.columns[0] = Vector4(p_array[0], p_array[1], p_array[2], p_array[3]);
    proj.columns[1] = Vector4(p_array[4], p_array[5], p_array[6], p_array[7]);
    proj.columns[2] = Vector4(p_array[8], p_array[9], p_array[10], p_array[11]);
    proj.columns[3] = Vector4(p_array[12], p_array[13], p_array[14], p_array[15]);
    return proj;
}

_FORCE_INLINE_ void _gl_inverse_transpose_normal(Basis &normal_basis, Transform3D &world_xform) {
    normal_basis = world_xform.basis.inverse();
    normal_basis.transpose();
}

_FORCE_INLINE_ void _gl_basis_to_array(const Basis &p_basis, float *r_array) {
    Vector3 col0 = p_basis.get_column(0);
    Vector3 col1 = p_basis.get_column(1);
    Vector3 col2 = p_basis.get_column(2);
    r_array[0] = col0.x;
    r_array[1] = col0.y;
    r_array[2] = col0.z;
    r_array[3] = 0.0f;
    r_array[4] = col1.x;
    r_array[5] = col1.y;
    r_array[6] = col1.z;
    r_array[7] = 0.0f;
    r_array[8] = col2.x;
    r_array[9] = col2.y;
    r_array[10] = col2.z;
    r_array[11] = 0.0f;
}

#endif // COMMON_MATH_GLES_COMMON_H
