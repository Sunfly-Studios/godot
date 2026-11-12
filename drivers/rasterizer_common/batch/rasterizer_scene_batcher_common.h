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
 * 3D SCENE BATCHER EXECUTION ARCHITECTURE:
 * The batcher is divided in 4 paths or "lanes" that will be executed
 * at any given point in the pipeline:
 *
 * [ Incoming Surface ]
 *          |
 *          +--> Deformed (Skeleton / BlendShapes), Particles, Extreme AABB, or unsupported static fallback?
 *          |             |
 *          |             +--> (Yes) --> [ LANE 3: RSII (Render Single Item Immediate) ]
 *          |                            Bypass queue. Rendered after batched bypasses via `_render_single_item_immediate`.
 *          |
 *          v (No)
 * [ Record & Evaluate ]  --> Computes planar depth along camera forward vector, 64-bit `state_hash`,
 *          |                 `mesh_id`, `surface_index`, and world-space AABB into `BSortItem3D`.
 *          v
 * [ Sort & Reorder    ]  --> Opaque:      Sorted by `state_hash -> mesh_id -> surface_index -> depth (FTB)`.
 *          |                 Transparent: Sorted BTF by depth, followed by a bounded AABB intersection
 *          |                              look-ahead window (`sort_items_from_3d`) to reunite matching states.
 *          v
 * [ Stage 1: Joining  ]  --> Groups compatible adjacent items into `BItemJoined3D` and `BItemRef3D`:
 *          |                 - Pre-evaluates `TransformMode3D` (None, Translate, Orthogonal, Uniform Scale, All)
 *          |                   and precomputes inverse-transpose normal cofactor matrices.
 *          |                 - Accumulates `BatchFlags3D`, color change ratios, and a rolling 64-bit FNV-1a
 *          |                   hash for cross-frame VBO slot reuse.
 *          |                 - Enforces strict `(state_hash, mesh_id, surface_index)` matching for static-loop items
 *          |                   and `MASK_ID` state matching for dynamic-merge items.
 *          v
 * [ Stage 2: Dispatch ]  --> Routes each `BItemJoined3D` into one of the 3 batcher lanes:
 *          |
 *          +--> [ LANE 1: BRI (Batch Render Items - Multi-Item Dynamic VBO) ]
 *          |    Target: Multiple static meshes with that are within the default limit in `settings_dynamic_vertex_limit`
 *          |            sharing material/shader state (`num_item_refs > 1`).
 *          |    Action: 1. Checks `VBOPool::cached_slots` via `batch_content_hash` for zero-upload frame-to-frame reuse.
 *          |            2. Selects minimal FVF tier (`TIER1_BASE` [24B], `TIER2_COLOR` [36B], `TIER3_TANGENT` [40B],
 *          |               `TIER4_PALETTE` [64B], or `DEPTH_MINIMAL` [16B] / `DEPTH_ALPHA_MINIMAL` [32B]).
 *          |            3. Software-transforms vertices into `unit_vertices` (caching world-transformed vertices in
 *          |               `GeometryInstanceSurface::baked_world_cache` and `baked_tangent_cache`), uploads to
 *          |               `VBOPool`, and issues a single merged `glDrawElements`.
 *          |
 *          +--> [ LANE 2: SMDD (Single Mesh Direct Draw - `FVF_UNBATCHED`) ]
 *          |    Target: Isolated single-instance static meshes (`num_item_refs == 1`, non-MultiMesh) that cannot
 *          |            be joined with neighbours. Acts as the shock absorber for Lane 1.
 *          |    Action: Skips CPU vertex transformation and dynamic VBO staging. Binds the mesh's native static
 *          |            VBO/IBO directly via `_batch_render_single_mesh_fallback` and issues 1 hardware-transformed draw.
 *          |
 *          +--> [ LANE 4: SMSG (Static Mesh State Grouping - `FVF_STATIC_MESH_LOOP`) ]
 *               Target: Repeated heavy static meshes (`base_verts > settings_dynamic_vertex_limit`, `num_item_refs > 1`)
 *                       and all eligible `MultiMesh` instances regardless of vertex count. Acts as the shock absorber
 *                       for Lane 3 (`RSII`) and eliminates CPU vertex expansion for `MultiMesh`.
 *               Action: Binds the material, shader, and the mesh's native static VBO/ABO/IBO once for the entire
 *                       contiguous run in `_batch_render_static_mesh_group`, loops over all joined `BItemRef3D`
 *                       and `MultiMesh` instances updating only the hardware transform and instance color, and
 *                       unbinds once at the end of the run.
 *
 * MULTI-PASS REPLAY (`p_replay == true`):
 * During multi-pass rendering (GLES1 additive light chunks/shadow projections, or `_render_uv2` lightmap baking),
 * `batch_scene_render_items` bypasses recording, sorting, and VBO packing, re-dispatching the existing `bdata.batches`
 * list (Lanes 1, 2, and 4) and bypassed render itmes (Lane 3) directly against the new pass state.
 *
 * 64-BIT STATE HASHING:
 * `state_hash` orders pipeline states from most expensive (MSB) to least expensive (LSB) so standard `<` comparisons
 * in `SortOpaque` naturally minimise driver state changes:
 *
 *  63          52 51                  32 31          31 30                  16 15                 0
 *  +-------------+----------------------+--------------+----------------------+-------------------+
 *  |  Shader ID  |      Material ID     |  Has Texture |   Instance Base Type | Primitive Topology|
 *  +-------------+----------------------+--------------+----------------------+-------------------+
 *
 * - Bits 63-52: Shader version ID (and cull/depth state mask in GLES1) (12 bits).
 * - Bits 51-32: Material pointer ID (textures, uniform state) (20 bits).
 * - Bit  31   : Texture presence flag (1 bit).
 * - Bits 30-16: `RS::InstanceType` base type (masked out by `MASK_ID` during Lane 1 joining) (15 bits).
 * - Bits 15-0 : `RS::PrimitiveType` topology (16 bits).
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

	// Quantised 32-bit signed normalized normal (GL_BYTE)
	struct BatchNormal {
		int8_t x, y, z, pad;

		_FORCE_INLINE_ static int8_t _quantize_snorm8(float p_v) {
			float c = CLAMP(p_v, -1.0f, 1.0f);
			return static_cast<int8_t>(c >= 0.0f ? (c * 127.0f + 0.5f) : (c * 127.0f - 0.5f));
		}

		void set(float xx, float yy, float zz) {
			x = _quantize_snorm8(xx);
			y = _quantize_snorm8(yy);
			z = _quantize_snorm8(zz);
			pad = 0;
		}
		void set(const Vector3 &p_o) {
			set(p_o.x, p_o.y, p_o.z);
		}
		void to(Vector3 &r_o) const {
			r_o.x = static_cast<float>(x) * (1.0f / 127.0f);
			r_o.y = static_cast<float>(y) * (1.0f / 127.0f);
			r_o.z = static_cast<float>(z) * (1.0f / 127.0f);
		}
	};
	static_assert(sizeof(BatchNormal) == 4, "BatchNormal must be 4 bytes.");

	// Quantised 32-bit signed normalized tangent (GL_BYTE)
	struct BatchTangent {
		int8_t x, y, z, w;

		void set(float xx, float yy, float zz, float ww) {
			x = BatchNormal::_quantize_snorm8(xx);
			y = BatchNormal::_quantize_snorm8(yy);
			z = BatchNormal::_quantize_snorm8(zz);
			w = BatchNormal::_quantize_snorm8(ww);
		}
	};
	static_assert(sizeof(BatchTangent) == 4, "BatchTangent must be 4 bytes.");

	// ID magic number that masks out the bits 16-31 (source mesh/surface ID)
	// so distinct meshes with identical materials can join.
	static constexpr uint64_t MASK_ID = 0xFFFFFFFF0000FFFFULL;

	// Used to determine whether we use hardware transform (none),
	// software transform just a translate (no rotate or scale),
	// or software transform all verts (full 3x3 basis + origin).
	enum TransformMode3D : uint8_t {
		TM_3D_NONE = 0,
		TM_3D_TRANSLATE = 1,
		TM_3D_ORTHOGONAL = 2,
		TM_3D_UNIFORM_SCALE = 3,
		TM_3D_ALL = 4,
	};

	// Simplest 3D FVF (Tier 1 - 24 bytes):
	// local or baked position, quantized normal, and primary UV.
	// This is always written to initially during prefill, and can be promoted
	// to larger tiered FVFs during flush if secondary attributes are required.
	struct alignas(4) BatchVertex3DTier1 {
		BatchVector3 pos;
		BatchNormal normal;
		BatchVector2 uv;
	};
	static_assert(sizeof(BatchVertex3DTier1) == 24, "BatchVertex3DTier1 must be 24 bytes.");

	// Tier 2 (Colored - 36 bytes):
	// incorporates baked vertex/albedo color and secondary UV2.
	struct alignas(4) BatchVertex3DTier2 {
		BatchVector3 pos;
		BatchColor color;
		BatchNormal normal;
		BatchVector2 uv;
		BatchVector2 uv2;
	};
	static_assert(sizeof(BatchVertex3DTier2) == 36, "BatchVertex3DTier2 must be 36 bytes.");

	// Tier 3 (Tangent - 40 bytes):
	// adds quantized tangent vectors for normal-mapped surfaces.
	struct alignas(4) BatchVertex3DTier3 {
		BatchVector3 pos;
		BatchColor color;
		BatchNormal normal;
		BatchTangent tangent;
		BatchVector2 uv;
		BatchVector2 uv2;
	};
	static_assert(sizeof(BatchVertex3DTier3) == 40, "BatchVertex3DTier3 must be 40 bytes.");

	// Tier 4 (Skinned/Palette/Instanced - 64 bytes)
	// Incorporates blend weights and matrix palette indices for hardware palette skinning or instancing.
	struct alignas(GODOT_MIN_STACK_ALIGN) BatchVertex3DTier4 {
		BatchVector3 pos;
		BatchColor color;
		BatchNormal normal;
		BatchTangent tangent;
		uint8_t matrix_index[4];
		uint32_t pad0;
		BatchVector2 uv;
		BatchVector2 uv2;
		BatchVector4 weights;
	};
	static_assert(sizeof(BatchVertex3DTier4) == 64, "BatchVertex3DTier4 must be 64 bytes.");
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
		uint64_t batch_content_hash;
		uint32_t mesh_id;
		uint32_t surface_index;
		uint32_t total_verts;
		uint32_t total_indices;
		uint32_t total_color_changes;
		bool use_static_vbo_loop;

		// Single non-instanced items (or items that explicitly forbid CPU vertex baking)
		// use the GPU modelview matrix directly rather than paying CPU software-transform overhead.
		_FORCE_INLINE_ bool use_hardware_transform() const {
			return (
				use_static_vbo_loop ||
				(num_item_refs == 1 && !(flags & BatcherEnums::BTF_3D_USES_INSTANCING)) ||
				((flags & BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING) != 0 && !(flags & BatcherEnums::BTF_3D_USES_INSTANCING))
			);
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

	// Struct for static cache batching
	struct CachedBatchSlot3D {
		uint64_t content_hash = 0;
		uint32_t vbo_byte_offset = 0;
		uint32_t ibo_byte_offset = 0;
		uint32_t aligned_vbo_bytes = 0;
		uint32_t aligned_ibo_bytes = 0;
		uint32_t num_verts = 0;
		uint32_t num_indices = 0;
		BatcherEnums::FVF fvf = BatcherEnums::FVF_3D_TIER1_BASE;
		uint32_t flags = 0;
	};

	// Tracker for VBOs
	struct VBOPool {
		uint32_t gl_vertex_buffer = 0;
		uint32_t gl_index_buffer = 0;
		LocalVector<CachedBatchSlot3D> cached_slots;
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
		uint32_t current_vbo_slot_index = 0;

		void invalidate_vbo_cache() {
			for (uint32_t i = 0; i < vbo_pool.size(); i++) {
				vbo_pool[i].cached_slots.clear();
			}
			current_vbo_index = 0;
			current_vbo_byte_offset = 0;
			current_ibo_byte_offset = 0;
			current_vbo_slot_index = 0;
		}

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
			current_mesh_id = 0;
			current_surface_index = 0;
		}
		uint64_t current_state_hash;
		typename T_API::MaterialData *current_material_data;
		uint32_t consumed_uniform_vectors;
		uint32_t matrix_palette_count;
		RS::PrimitiveType current_primitive;
		Batch3D *curr_batch;
		BatchColor last_modulate;
		uint32_t current_mesh_id;
		uint32_t current_surface_index;
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

	template <class T_SURFACE>
	void flush_render_batches(typename T_API::MaterialData *p_material_data, RS::PrimitiveType p_primitive, Batch3D &p_batch, bool p_transparent, uint64_t p_content_hash = 0);

	template <class T_SURFACE>
	void render_bypassed_items(bool p_transparent);

	// Evaluates the 3D transform once per instance to determine whether software vertex
	// expansion can skip the 3x3 basis multiplication and normal cofactor generation.
	_FORCE_INLINE_ static TransformMode3D _find_transform_mode_3d(const Transform3D &p_tr, float *r_inv_scale = nullptr) {
		const Basis &b = p_tr.basis;
		const bool is_identity_basis = (
			b.rows[0][0] == 1.0f && b.rows[0][1] == 0.0f && b.rows[0][2] == 0.0f &&
			b.rows[1][0] == 0.0f && b.rows[1][1] == 1.0f && b.rows[1][2] == 0.0f &&
			b.rows[2][0] == 0.0f && b.rows[2][1] == 0.0f && b.rows[2][2] == 1.0f
		);
		if (is_identity_basis) {
			if (r_inv_scale) {
				*r_inv_scale = 1.0f;
			}
			if (p_tr.origin.x == 0.0f && p_tr.origin.y == 0.0f && p_tr.origin.z == 0.0f) {
				return TM_3D_NONE;
			}
			return TM_3D_TRANSLATE;
		}

		const Vector3 c0 = b.get_column(0);
		const Vector3 c1 = b.get_column(1);
		const Vector3 c2 = b.get_column(2);

		const float l0_sq = c0.length_squared();
		const float l1_sq = c1.length_squared();
		const float l2_sq = c2.length_squared();

		if (
			l0_sq > 1e-8f &&
			Math::is_equal_approx(l0_sq, l1_sq) &&
			Math::is_equal_approx(l0_sq, l2_sq)
		) {
			const float tol = l0_sq * 1e-4f;
			if (
				Math::abs(c0.dot(c1)) <= tol &&
				Math::abs(c0.dot(c2)) <= tol &&
				Math::abs(c1.dot(c2)) <= tol
			) {
				if (Math::is_equal_approx(l0_sq, 1.0f)) {
					if (r_inv_scale) {
						*r_inv_scale = 1.0f;
					}
					return TM_3D_ORTHOGONAL;
				}
				if (r_inv_scale) {
					*r_inv_scale = 1.0f / Math::sqrt(l0_sq);
				}
				return TM_3D_UNIFORM_SCALE;
			}
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
	_FORCE_INLINE_ static uint32_t _hash_transform_3d(const Transform3D &p_tr) {
		const uint32_t *words = reinterpret_cast<const uint32_t *>(&p_tr);
		uint32_t h = 2166136261U;
		for (uint32_t i = 0; i < (sizeof(Transform3D) / sizeof(uint32_t)); i++) {
			uint32_t w = words[i];
			if (w == 0x80000000U) {
				w = 0U; // Normalise -0.0f to +0.0f
			}
			h = (h ^ w) * 16777619U;
		}
		return h == 0U ? 1U : h;
	}
	_FORCE_INLINE_ static uint64_t _fnv1a_64_mix(uint64_t p_hash, uint64_t p_val) {
		p_hash ^= p_val;
		p_hash *= 1099511628211ULL;
		return p_hash;
	}
	_FORCE_INLINE_ void _prepare_unit_vertices_for_fvf(BatcherEnums::FVF p_fvf) {
		switch (p_fvf) {
			case BatcherEnums::FVF_UNBATCHED:
			case BatcherEnums::FVF_STATIC_MESH_LOOP:
				break;
			case BatcherEnums::FVF_3D_DEPTH_MINIMAL:
			case BatcherEnums::FVF_DEPTH_ONLY:
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DDepthMinimal));
				break;
			case BatcherEnums::FVF_3D_DEPTH_ALPHA_MINIMAL:
			case BatcherEnums::FVF_DEPTH_ALPHA:
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DDepthAlphaMinimal));
				break;
			case BatcherEnums::FVF_3D_TIER1_BASE:
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier1));
				break;
			case BatcherEnums::FVF_3D_TIER2_COLOR:
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier2));
				break;
			case BatcherEnums::FVF_3D_TIER3_TANGENT:
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier3));
				break;
			case BatcherEnums::FVF_3D_TIER4_PALETTE:
			case BatcherEnums::FVF_INSTANCED:
			case BatcherEnums::FVF_REGULAR:
			default:
				bdata.unit_vertices.prepare(sizeof(BatchVertex3DTier4));
				break;
		}
	}
	void _software_transform_vertex(BatchVector3 &r_v, const Transform3D &p_tr) const;
	void _software_transform_normal(BatchNormal &r_n, const Transform3D &p_tr) const;
	void _software_transform_normal_precomputed(BatchNormal &r_n, const Basis &p_cofactor) const;
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
