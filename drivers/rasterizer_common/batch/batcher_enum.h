/**************************************************************************/
/*  batcher_enum.h                                                        */
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

#ifndef BATCHER_ENUMS_H
#define BATCHER_ENUMS_H

class BatcherEnums {
public:
	enum FVF {
		FVF_UNBATCHED,
		FVF_REGULAR,
		FVF_COLOR,
		FVF_LIGHT_ANGLE,
		FVF_MODULATED,
		FVF_LARGE,
		FVF_INSTANCED,
		FVF_DEPTH_ONLY,
		FVF_DEPTH_ALPHA,

		// 3D Tiered Payloads
		FVF_3D_TIER1_BASE,
		FVF_3D_TIER2_COLOR,
		FVF_3D_TIER3_TANGENT,
		FVF_3D_TIER4_PALETTE,
		FVF_3D_DEPTH_MINIMAL,
		FVF_3D_DEPTH_ALPHA_MINIMAL,
		FVF_STATIC_MESH_LOOP,
	};

	enum PassMode {
		PASS_MODE_COLOR,
		PASS_MODE_COLOR_TRANSPARENT,
		PASS_MODE_SHADOW,
		PASS_MODE_DEPTH,
		PASS_MODE_MATERIAL,

		// Specifically for FFP multi-pass shadows architecture
		PASS_MODE_SHADOW_PROJECTION,
	};

	// these flags are specifically for batching
	// some of the logic is thus in rasterizer_storage.cpp
	// we could alternatively set bitflags for each 'uses' and test on the fly
	enum BatchFlags {
		PREVENT_COLOR_BAKING = 1 << 0,
		PREVENT_VERTEX_BAKING = 1 << 1,

		// custom vertex shaders using BUILTINS that vary per item
		PREVENT_ITEM_JOINING = 1 << 2,

		USE_MODULATE_FVF = 1 << 3,
		USE_LARGE_FVF = 1 << 4,
	};

	// these flags are specifically for 3D batching
	enum BatchFlags3D : uint32_t {
		BTF_3D_NONE = 0,
		BTF_3D_USES_COLORS = 1 << 0,
		BTF_3D_USES_TANGENTS = 1 << 1,
		BTF_3D_USES_UV2 = 1 << 2,
		BTF_3D_USES_SKINNING = 1 << 3,
		BTF_3D_USES_INSTANCING = 1 << 4,
		BTF_3D_USES_ALPHA_CLIP = 1 << 5,
		BTF_3D_PREVENT_COLOR_BAKING = 1 << 6,
		BTF_3D_PREVENT_VERTEX_BAKING = 1 << 7,
		BTF_3D_PREVENT_ITEM_JOINING = 1 << 8,
	};

	enum BatchType : uint16_t {
		BT_DEFAULT = 0,
		BT_RECT = 1,
		BT_LINE = 2,
		BT_LINE_AA = 3,
		BT_POLY = 4,
		BT_DUMMY = 5, // dummy batch is just used to keep the batch creation loop simple
	};

	enum BatchTypeFlags {
		BTF_DEFAULT = 1 << BT_DEFAULT,
		BTF_RECT = 1 << BT_RECT,
		BTF_LINE = 1 << BT_LINE,
		BTF_LINE_AA = 1 << BT_LINE_AA,
		BTF_POLY = 1 << BT_POLY,
	};
};

#endif // BATCHER_ENUMS_H
