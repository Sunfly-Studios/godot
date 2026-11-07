/**************************************************************************/
/*  rasterizer_scene_batcher_common.h                                     */
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

/*
 * CORE PIPELINE:
 * [ Incoming Surface ]   -> Exceeds dynamic limits? -> [ Bypass queue (Direct immediate render) ]
 *          | (No)
 *          v
 * [   Hash & Depth   ]   -> Driver calculates a planar depth by projecting the instance's bounds onto the camera's forward vecto.
 *          |                Eliminates issues regarding distortion artifacts.
 *          v
 * [    Sort Queue    ]   -> Opaque items are FTB (Front-to-Back) sorted to maximise early-Z rejection.
 *          |             -> Transparent items are BTF (Back-to-Front) sorted (Painter's Algorithm, following the 2D batcher logic),
 *          |                which is required for alpha blending.
 *          v
 * [    Pack VBOs     ]   -> Transforms instances and packs them into a RasterizerUnitArray.
 *          |                Forces a batch break if it exceeds the device's uniform vector's limit.
 *          v
 * [   Flush to GPU   ]   -> Triggers the draw call. VBOs are retained in a memory pool for pass reuse (such as on additive lights/shadows).
 *                           Bypass queues are intentionally omitted here so multiple flushes don't break, and are only cleared by reset_scene().
 *
 * 64-BIT STATE HASHING:
 * The state_hash is a 64-bit integer which contains the most expensive operations in the higher bits, while the least expensive
 * operations in the lower bits. This is done to exploit standard `<` operator in the opaque sort for speed.
 * Placing the most expensive state changes in the highest bits natively groups identical configurations and prevents state thrashin.
 * 
 *  63          52 51                  32 31                    16 15                 0
 *  +-------------+----------------------+------------------------+-------------------+
 *  |  Shader ID  |      Material ID     | Source mesh surface ID | Surface/Primitive |
 *  +-------------+----------------------+------------------------+-------------------+
 *
 * - Bits 63-52: Shader version ID / state mask (12 bits).
 *   Note: For GLES1, this includes cull modes and depth testing.
 * - Bits 51-32: Material ID (texture bindings, uniform blocks) (20 bits).
 * - Bits 31-16: Source mesh surface ID (groups identical source geometry).
 * - Bits 15-0: Surface index & primitive topology (16 bits).
 *
 * To guarantee optimal vertex fetching speeds, the FVF definitions intentionally pad these byte arrays to 16-byte boundaries.
 *
 * For hardware lacking matrix palettes, we execute CPU-side spatial baking similar to Ogre3D's StaticGeometry.
 * The Transform3D matrix is applied directly to the vertex payload before pushing to the VBO.
 * For accurate lighting without distorting normals under non-uniform scaling, the inverse-transpose
 * of the Basis matrix is precalculated once per instance and passed to the software transform loop.
 */

#ifndef RASTERIZER_SCENE_BATCHER_COMMON_H
#define RASTERIZER_SCENE_BATCHER_COMMON_H

#include "core/os/os.h"
#include "core/object/object_soa_pool.h"
#include "core/templates/flat_map.h"
#include "core/templates/generational_handle.h"
#include "core/templates/local_vector.h"
#include "core/templates/sparse_set.h"
#include "batcher_enum.h"
#include "rasterizer_array.h"
#include "rasterizer_asserts.h"
#include "core/config/project_settings.h"
#include "servers/rendering/renderer_scene_render.h"
#include "core/math/transform_3d.h"

template <typename T_API>
class RasterizerSceneBatcherCommon {
public:
	// 3D and 2D POD Vector
	struct BatchVector3 {
		float x, y, z;
		void set(float xx, float yy, float zz) {
			x = xx;
			y = yy;
			z = zz;
		}
		void set(const Vector3 &p_o) {
			x = p_o.x;
			y = p_o.y;
			z = p_o.z;
		}
		void to(Vector3 &r_o) const {
			r_o.x = x;
			r_o.y = y;
			r_o.z = z;
		}
	};

	struct BatchVector4 {
		float x, y, z, w;
		void set(float xx, float yy, float zz, float ww) {
			x = xx;
			y = yy;
			z = zz;
			w = ww;
		}
	};

	struct BatchVector2 {
		float x, y;
		void set(float xx, float yy) {
			x = xx;
			y = yy;
		}
		void set(const Vector2 &p_o) {
			x = p_o.x;
			y = p_o.y;
		}
	};

	// 32-bit color
	struct BatchColor {
		uint8_t r, g, b, a;

		void set_white() {
			r = 255;
			g = 255;
			b = 255;
			a = 255;
		}
		void set(const Color &p_c) {
			r = (uint8_t)CLAMP(p_c.r * 255.0f, 0.0f, 255.0f);
			g = (uint8_t)CLAMP(p_c.g * 255.0f, 0.0f, 255.0f);
			b = (uint8_t)CLAMP(p_c.b * 255.0f, 0.0f, 255.0f);
			a = (uint8_t)CLAMP(p_c.a * 255.0f, 0.0f, 255.0f);
		}
		void set(float rr, float gg, float bb, float aa) {
			r = (uint8_t)CLAMP(rr * 255.0f, 0.0f, 255.0f);
			g = (uint8_t)CLAMP(gg * 255.0f, 0.0f, 255.0f);
			b = (uint8_t)CLAMP(bb * 255.0f, 0.0f, 255.0f);
			a = (uint8_t)CLAMP(aa * 255.0f, 0.0f, 255.0f);
		}
		bool operator==(const BatchColor &p_c) const {
			return (r == p_c.r) && (g == p_c.g) && (b == p_c.b) && (a == p_c.a);
		}
		bool operator!=(const BatchColor &p_c) const { return !(*this == p_c); }
		bool equals(const Color &p_c) const {
			return (r == (uint8_t)CLAMP(p_c.r * 255.0f, 0.0f, 255.0f)) &&
					(g == (uint8_t)CLAMP(p_c.g * 255.0f, 0.0f, 255.0f)) &&
					(b == (uint8_t)CLAMP(p_c.b * 255.0f, 0.0f, 255.0f)) &&
					(a == (uint8_t)CLAMP(p_c.a * 255.0f, 0.0f, 255.0f));
		}
		const uint8_t *get_data() const { return &r; }

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
		String to_string() const {
			return "{" + itos(r) + " " + itos(g) + " " + itos(b) + " " + itos(a) + "}";
		}
#endif
	};

	// Used to determine whether we use hardware transform (none),
	// software transform just a translate (no rotate or scale),
	// or software transform all verts (full 3x3 basis + origin).
	enum TransformMode3D : uint8_t {
		TM_3D_NONE = 0,
		TM_3D_TRANSLATE = 1,
		TM_3D_ALL = 2,
	};

	// Simplest 3D FVF (Tier 1 - 32 bytes):
	// local or baked position, normal, and primary UV.
	// This is always written to initially during prefill, and can be promoted
	// to larger tiered FVFs during flush if secondary attributes are required.
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DTier1 {
		BatchVector3 pos;
		BatchVector3 normal;
		BatchVector2 uv;
	};
	static_assert(sizeof(BatchVertex3DTier1) == 32, "BatchVertex3DTier1 must be 32 bytes.");
	static_assert(sizeof(BatchVertex3DTier1) % GODOT_MIN_STACK_ALIGN == 0, "BatchVertex3DTier1 alignment mismatch.");

	// Tier 2 (Colored - 48 bytes):
	// incorporates baked vertex/albedo color and secondary UV2.
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DTier2 {
		BatchVector3 pos;
		BatchColor color;
		BatchVector3 normal;
		uint32_t pad0;
		BatchVector2 uv;
		BatchVector2 uv2;
	};
	static_assert(sizeof(BatchVertex3DTier2) == 48, "BatchVertex3DTier2 must be 48 bytes.");
	static_assert(sizeof(BatchVertex3DTier2) % GODOT_MIN_STACK_ALIGN == 0, "BatchVertex3DTier2 alignment mismatch.");

	// Tier 3 (Tangent - 64 bytes):
	// adds tangent vectors for normal-mapped surfaces.
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DTier3 {
		BatchVector3 pos;
		BatchColor color;
		BatchVector3 normal;
		uint32_t pad0;
		BatchVector2 uv;
		BatchVector2 uv2;
		BatchVector4 tangent;
	};
	static_assert(sizeof(BatchVertex3DTier3) == 64, "BatchVertex3DTier3 must be 64 bytes.");
	static_assert(sizeof(BatchVertex3DTier3) % GODOT_MIN_STACK_ALIGN == 0, "BatchVertex3DTier3 alignment mismatch.");

	// Tier 4 (Skinned/Palette/Instanced - 80 bytes)
	// Incorporates blend weights and matrix palette indices for hardware palette skinning or instancing.
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DTier4 {
		BatchVector3 pos;
		BatchColor color;
		BatchVector3 normal;
		uint8_t matrix_index[4];
		BatchVector2 uv;
		BatchVector2 uv2;
		BatchVector4 tangent;
		BatchVector4 weights;
	};
	static_assert(sizeof(BatchVertex3DTier4) == 80, "BatchVertex3DTier4 must be 80 bytes.");
	static_assert(sizeof(BatchVertex3DTier4) % GODOT_MIN_STACK_ALIGN == 0, "BatchVertex3DTier4 alignment mismatch.");

	// Minimal depth-only FVF (16 bytes)
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DDepthMinimal {
		BatchVector3 pos;
		uint8_t matrix_index[4];
	};
	static_assert(sizeof(BatchVertex3DDepthMinimal) == 16, "BatchVertex3DDepthMinimal must be 16 bytes.");

	// Minimal alpha-tested depth FVF (32 bytes)
	// adds primary UV and weight for alpha scissor passes.
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DDepthAlphaMinimal {
		BatchVector3 pos;
		uint8_t matrix_index[4];
		BatchVector2 uv;
		float weight;
		float pad0;
	};
	static_assert(sizeof(BatchVertex3DDepthAlphaMinimal) == 32, "BatchVertex3DDepthAlphaMinimal must be 32 bytes.");

	// Legacy aliases for driver compatibility
	using BatchVertex3D = BatchVertex3DTier4;
	using BatchVertex3DInstanced = BatchVertex3DTier4;
	using BatchVertex3DDepth = BatchVertex3DDepthMinimal;
	using BatchVertex3DDepthAlpha = BatchVertex3DDepthAlphaMinimal;

	// Represents a single draw call of aggregated geometry after two-stage packing.
	struct Batch3D {
		uint32_t first_joined_index;
		uint32_t first_item_ref;
		uint32_t num_item_refs;
		uint32_t first_vert;
		uint32_t num_verts;
		uint32_t first_index;
		uint32_t num_indices;
		uint32_t vbo_index;
		uint32_t vbo_byte_offset;
		uint32_t ibo_byte_offset;
		BatcherEnums::FVF fvf;
		uint32_t flags;
	};

	// Items in a list to be sorted prior to joining.
	// Caches world-space AABB for 3D heuristic look-ahead reordering.
	struct BSortItem3D {
		// Have a function to keep as POD, rather than operator
		void assign(const BSortItem3D &o) {
			item = o.item;
			aabb = o.aabb;
			depth = o.depth;
			state_hash = o.state_hash;
			mesh_id = o.mesh_id;
			surface_index = o.surface_index;
		}
		void *item;
		AABB aabb;
		float depth;
		uint64_t state_hash;
		uint32_t mesh_id;
		uint32_t surface_index;
	};

	// Batch joined item may represent 1 or more surfaces sharing compatible render state.
	// Separates logical grouping from physical VBO packing until the final flush.
	struct BItemJoined3D {
		uint32_t first_item_ref;
		uint32_t num_item_refs;
		AABB bounding_aabb;

		// Bitmask defined in BatcherEnums::BatchFlags3D
		uint32_t flags;
		uint64_t state_hash;
		uint32_t total_verts;
		uint32_t total_indices;
		uint32_t total_color_changes;

		// Single non-instanced items use the GPU modelview matrix directly
		// rather than paying CPU software-transform overhead.
		_FORCE_INLINE_ bool use_hardware_transform() const {
			return num_item_refs == 1 && !(flags & BatcherEnums::BTF_3D_USES_INSTANCING);
		}
	};

	// Lightweight middleman holding pre-evaluated transform state and surface capacity
	// so the downstream software transform knows exactly how much math to skip.
	struct BItemRef3D {
		void *surface;
		Transform3D transform;
		Basis normal_cofactor;
		BatchColor modulate;
		TransformMode3D transform_mode;
		uint32_t base_verts;
		uint32_t base_indices;
		uint32_t instance_count;
	};

	// Limits for the 3D batcher
	struct BatchLimits {
		uint32_t max_matrix_palette_vectors;
		uint32_t max_vertices_per_buffer;
		uint32_t max_indices_per_buffer;
	};

	// Tracker for VBOs
	struct VBOPool {
		uint32_t gl_vertex_buffer = 0;
		uint32_t gl_index_buffer = 0;
	};

	struct BatchData3D {
		BatchData3D() {
			reset_scene();
			settings_use_batching = true;
			settings_use_single_mesh_fallback = true;
			settings_dynamic_vertex_limit = 1024;
			settings_item_reordering_lookahead = 4;
			settings_colored_vertex_format_threshold = 0.25f;
			dynamic_upload_accumulator = 0;
			dynamic_upload_threshold_bytes = 0;
			draw_call_accumulator = 0;
			max_batch_draw_call_count = 0;
			current_vbo_byte_offset = 0;
			current_ibo_byte_offset = 0;
			use_colored_vertices = false;
		}

		BatchLimits hardware_limits;

		void reset_scene() {
			reset_flush();
			batches.reset();
			items_joined.reset();
			item_refs.reset();
			bypassed_opaque_items.clear();
			bypassed_transparent_items.clear();
			sort_items.reset();
			fvf = BatcherEnums::FVF_3D_TIER1_BASE;
		}

		void reset_flush() {
			base_vertices.reset();
			vertex_colors.reset();
			vertex_uv2s.reset();
			vertex_tangents.reset();
			vertex_weights.reset();
			vertex_matrix_indices.reset();

			unit_vertices.prepare(unit_vertices.get_unit_size_bytes());
			indices.reset();
			total_verts = 0;
			total_indices = 0;
			total_color_changes = 0;
			use_colored_vertices = false;
		}

		LocalVector<VBOPool> vbo_pool;
		uint32_t current_vbo_index = 0;
		uint32_t current_vbo_byte_offset = 0;
		uint32_t current_ibo_byte_offset = 0;

		uint32_t max_vertices;
		uint32_t max_indices;

		// Batching settings

		// How many TOTAL vertices does a model have that can be
		// safely batched?
		// Default: 1024, maximum: 2048.
		uint32_t settings_dynamic_vertex_limit;

		// Controls the heuristic to evaluate neighbours, which
		// then its used to swap them and prevent batch breaks.
		// Default: 4
		int settings_item_reordering_lookahead;

		// Self-explanatory
		// Default: true
		bool settings_use_batching;

		// Controls whether single-item 3D batches bypass dynamic VBO staging
		// and draw directly from the mesh's static buffers inside the batcher.
		// Default: true
		bool settings_use_single_mesh_fallback;

		// Ratio of colour state changes to joined items above which Tier 1 batches
		// dynamically promote to tier 2 colour or FVF_3D_TIER2_COLOR.
		float settings_colored_vertex_format_threshold;
		bool use_colored_vertices;

		// Accumulators
		uint32_t dynamic_upload_accumulator;
		uint32_t dynamic_upload_threshold_bytes;
		uint32_t draw_call_accumulator;
		uint32_t max_batch_draw_call_count;
		uint32_t total_color_changes;

		BatcherEnums::FVF fvf;

		// Tier 1 minimal staging stream and auxiliary promotion streams
		RasterizerArray<BatchVertex3DTier1> base_vertices;
		RasterizerArray<BatchColor> vertex_colors;
		RasterizerArray<BatchVector2> vertex_uv2s;
		RasterizerArray<BatchVector4> vertex_tangents;
		RasterizerArray<BatchVector4> vertex_weights;
		RasterizerArray<uint32_t> vertex_matrix_indices;

		RasterizerUnitArray<uint8_t> unit_vertices;
		RasterizerArray<uint16_t> indices;

		RasterizerArray<Batch3D> batches;
		RasterizerArray<BItemJoined3D> items_joined;
		RasterizerArray<BItemRef3D> item_refs;
		RasterizerArray<BSortItem3D> sort_items;

		LocalVector<void *> bypassed_opaque_items;
		LocalVector<BSortItem3D> bypassed_transparent_items;

		// SoA pre-computed workspace for MultiMesh / software transform passes:
		// Stream 0: World/Instance Transform3D
		// Stream 1: Cofactor (inverse-transpose) Basis for normal/tangent transformation
		// Stream 2: Pre-quantized BatchColor
		// Stream 3: TransformMode3D fast-path tag
		ObjectSOAPool<Transform3D, Basis, BatchColor, TransformMode3D> multimesh_xform_soa;

		uint32_t total_verts;
		uint32_t total_indices;

		BatcherEnums::PassMode pass_mode;
	} bdata;

	struct RenderItemState3D {
		void reset() {
			current_state_hash = 0;
			current_material_data = nullptr;
			consumed_uniform_vectors = 0;
			matrix_palette_count = 0;
			current_primitive = RS::PRIMITIVE_MAX;
			curr_batch = nullptr;
			last_modulate.set_white();
		}
		uint64_t current_state_hash;
		typename T_API::MaterialData *current_material_data;
		uint32_t consumed_uniform_vectors;
		uint32_t matrix_palette_count;
		RS::PrimitiveType current_primitive;
		Batch3D *curr_batch;
		BatchColor last_modulate;
	} _render_item_state;

private:
	// CRTP Cast to get the driver-specific Scene
	typename T_API::Scene *get_this();
	const typename T_API::Scene *get_this() const;

protected:
	void batch_constructor();
	void batch_initialize();
	void batch_scene_begin();
	void batch_scene_end();

	template <BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
	void batch_scene_render_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent, bool p_replay = false);

	template <class T_SURFACE>
	void record_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent);

	struct SortOpaque {
		_FORCE_INLINE_ bool operator()(const BSortItem3D &A, const BSortItem3D &B) const {
			if (A.state_hash == B.state_hash) {
				if (A.mesh_id == B.mesh_id) {
					if (A.surface_index == B.surface_index) {
						return A.depth < B.depth;
					}
					return A.surface_index < B.surface_index;
				}
				return A.mesh_id < B.mesh_id;
			}
			return A.state_hash < B.state_hash;
		}
	};

	struct SortTransparent {
		_FORCE_INLINE_ bool operator()(const BSortItem3D &A, const BSortItem3D &B) const {
			return A.depth > B.depth;
		}
	};

	void sort_items(bool p_transparent);
	bool _sort_items_match_3d(const BSortItem3D &p_a, const BSortItem3D &p_b) const;
	bool sort_items_from_3d(int p_start);

	template <BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
	void join_sorted_items();

	template <BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
	void join_and_flush_sorted_items(bool p_transparent);

	template <class T_SURFACE>
	bool try_join_item(T_SURFACE *p_surface, RenderItemState3D &r_ris);

	template <BatcherEnums::PassMode p_pass_mode, class T_SURFACE>
	void prefill_joined_item(const BItemRef3D &p_ref, bool p_use_hardware_transform, uint32_t p_item_index, uint32_t p_joined_flags);

	void _translate_batches_to_vertex_colored_FVF_3d(const Batch3D &p_batch);

	template <class T_BATCH_VERT, bool INCLUDE_COLOR_UV2, bool INCLUDE_TANGENT, bool INCLUDE_PALETTE>
	void _translate_batches_to_larger_FVF_3d(uint32_t p_num_verts);

	template <class T_SURFACE>
	void flush_render_batches(typename T_API::MaterialData *p_material_data, RS::PrimitiveType p_primitive, Batch3D &p_batch, bool p_transparent);

	template <class T_SURFACE>
	void render_bypassed_items(bool p_transparent);

	// Evaluates the 3D transform once per instance to determine whether software vertex
	// expansion can skip the 3x3 basis multiplication and normal cofactor generation.
	_FORCE_INLINE_ static TransformMode3D _find_transform_mode_3d(const Transform3D &p_tr) {
		const Basis &b = p_tr.basis;
		const bool is_identity_basis = (
			b.rows[0][0] == 1.0f && b.rows[0][1] == 0.0f && b.rows[0][2] == 0.0f &&
			b.rows[1][0] == 0.0f && b.rows[1][1] == 1.0f && b.rows[1][2] == 0.0f &&
			b.rows[2][0] == 0.0f && b.rows[2][1] == 0.0f && b.rows[2][2] == 1.0f
		);
		if (is_identity_basis) {
			if (p_tr.origin.x == 0.0f && p_tr.origin.y == 0.0f && p_tr.origin.z == 0.0f) {
				return TM_3D_NONE;
			}
			return TM_3D_TRANSLATE;
		}
		return TM_3D_ALL;
	}

	_FORCE_INLINE_ static Basis _compute_normal_cofactor(const Basis &p_basis) {
		Basis cofactor;
		// Normals must be transformed by the inverse-transpose of the basis matrix
		// to avoid distortion when the instance has non-uniform scaling.
		cofactor.set_column(0, p_basis.get_column(1).cross(p_basis.get_column(2)));
		cofactor.set_column(1, p_basis.get_column(2).cross(p_basis.get_column(0)));
		cofactor.set_column(2, p_basis.get_column(0).cross(p_basis.get_column(1)));
		return cofactor;
	}
	void _software_transform_vertex(BatchVector3 &r_v, const Transform3D &p_tr) const;
	void _software_transform_normal(BatchVector3 &r_n, const Transform3D &p_tr) const;
	void _software_transform_normal_precomputed(BatchVector3 &r_n, const Basis &p_cofactor) const;
	void _calculate_draw_call_cost_or_flush(Batch3D &p_batch) const;

public:
	Batch3D *_batch_request_new(bool p_blank = true) {
		Batch3D *batch = bdata.batches.request();
		if (!batch) {
			bdata.batches.grow();
			batch = bdata.batches.request();
			if (unlikely(!batch)) {
				return nullptr;
			}
		}
		if (p_blank) {
			*batch = Batch3D();
		}
		return batch;
	}
};

// Include the template implementation inline
#include "rasterizer_scene_batcher_common.inc"

#endif // RASTERIZER_SCENE_BATCHER_COMMON_H
