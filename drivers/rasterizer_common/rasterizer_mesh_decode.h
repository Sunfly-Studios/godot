/**************************************************************************/
/*  rasterizer_mesh_decode.h                                              */
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

#ifndef RASTERIZER_MESH_DECODE_H
#define RASTERIZER_MESH_DECODE_H

// Common mesh decoding math that is transferable across
// other driver back-ends

/* 16-BIT MESH FORMAT DECOMPRESSION */

static _FORCE_INLINE_ Vector3 _raster_mesh_decode_octahedral_normal(uint16_t p_x, uint16_t p_y) {
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

static _FORCE_INLINE_ void _raster_mesh_decode_octahedral_tangent(uint16_t p_x, uint16_t p_y, Vector3 &r_tangent, float &r_binormal_sign) {
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

static _FORCE_INLINE_ float _raster_mesh_decode_uv_unorm16(uint16_t p_v, float p_scale) {
	const float u = p_v / 65535.0f;
	// Nonzero scale: centered/scaled encoding.
	// Zero scale: plain UNORM16 in [0,1].
	return (p_scale != 0.0f) ? (u - 0.5f) * p_scale : u;
}

#endif // RASTERIZER_MESH_DECODE_H
