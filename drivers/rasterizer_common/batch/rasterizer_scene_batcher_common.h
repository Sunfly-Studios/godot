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
 * [ Incoming Surface ] -> Exceeds dynamic limits? -> [ Bypass queue (Direct immediate render) ]
 *          | (No)
 *          v
 * [   Hash & Depth   ] -> Driver calculates a planar depth by projecting the instance's bounds onto the camera's forward vecto.
 *          |                Eliminates issues regarding distortion artifacts.
 *          v
 * [    Sort Queue    ]   -> Opaque items are FTB (Front-to-Back) sorted to maximise early-Z rejection.
 *          |             -> Transparent items are BTF (Back-to-Front) sorted (Painter's Algorithm, following the 2D batcher logic),
 *          |                which is required for alpha blending.
 *          v
 * [    Pack VBOs     ]   -> Transforms instances and packs them into a RasterizerUnitArray.
 *          |                Forces a batch break if it exceeds the device's uniform vector's limit.
 *          v
 * [   Flush to GPU   ]   -> Triggers the draw call. VBOs and index buffers are reset via reset_flush().
 *                           Bypass queues are intentionally omitted here so multiple flushes don't break, and are only cleared by reset_scene().
 *
 * 64-BIT STATE HASHING:
 * The state_hash is a 64-bit integer which contains the most expensive operations in the higher bits, while the least expensive
 * operations in the lower bits. This is done to exploit standard `<` operator in the opaque sort for speed.
 * Placing the most expensive state changes in the highest bits natively groups identical configurations and prevents state thrashin.
 * 
 *  63                    48 47                    16 15                 0
 *  +----------------------+----------------------+----------------------+
 *  |  Shader / Prog ID    |  Material / Tex ID   |  FVF / Mesh ID       |
 *  +----------------------+----------------------+----------------------+
 *
 * - Bits 63-48: Shader/Program ID.
 *   Note: For GLES1, this maps to its state mask (texture environment modes, hardware lighting state).
 * - Bits 47-16: Material/Texture ID
 * - Bits 15-0: Flexible Vertex Format (FVF) type or Mesh ID.
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
#include "core/templates/local_vector.h"
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

	// Standard 3D vertex FVF
	struct BatchVertex3D {
		BatchVector3 pos;
		BatchVector3 normal;
		BatchVector2 uv;
		BatchColor color;
		float instance_index; // Used by Matrix Palette Uniforms in GLES2

		float pad[2];
	};

	// A struct used to simulate instaced rendering for
	// MultiMesh
	struct BatchVertex3DInstanced {
		BatchVector3 pos;
		BatchVector3 normal;
		BatchVector2 uv;
		BatchColor color;
		float instance_index;
		BatchVector4 instance_xform0;
		BatchVector4 instance_xform1;
		BatchVector4 instance_xform2;
		BatchColor instance_color_custom_data;
		float pad;
	};

	// A batch represents a single draw call of aggregated geometry
	struct Batch3D {
		BatcherEnums::BatchType type;
		uint32_t first_vert;
		uint32_t num_verts;
		uint32_t num_indices;
	};

	struct BSortItem3D {
		void assign(const BSortItem3D &o) {
			item = o.item;
			depth = o.depth;
			state_hash = o.state_hash;
		}
		void *item;
		float depth;
		uint64_t state_hash;
	};

	struct BatchLimits {
		uint32_t max_matrix_palette_vectors;
		uint32_t max_vertices_per_buffer;
		uint32_t max_indices_per_buffer;
	};

	struct BatchData3D {
		BatchData3D() {
			reset_scene();
			gl_vertex_buffer = 0;
			gl_instanced_vertex_buffer = 0;
			gl_index_buffer = 0;
			settings_use_batching = true;
			settings_dynamic_vertex_limit = 1024;
		}

		BatchLimits hardware_limits;

		void reset_scene() {
			reset_flush();
			bypassed_opaque_items.clear();
			bypassed_transparent_items.clear();
			sort_items.reset();
			fvf = BatcherEnums::FVF_REGULAR;
		}

		void reset_flush() {
			batches.reset();

			// Keep active FVF byte size, reset cursor
			unit_vertices.prepare(unit_vertices.get_unit_size_bytes());
			vertices.reset();
			instanced_vertices.reset();
			indices.reset();
			total_verts = 0;
			total_indices = 0;
		}

		uint32_t gl_vertex_buffer;
		uint32_t gl_instanced_vertex_buffer;
		uint32_t gl_index_buffer;

		uint32_t max_vertices;
		uint32_t max_indices;

		uint32_t settings_dynamic_vertex_limit;
		bool settings_use_batching;

		BatcherEnums::FVF fvf;
		RasterizerUnitArray<uint8_t> unit_vertices;
		RasterizerArray<uint16_t> indices;

		RasterizerArray<BatchVertex3D> vertices;
		RasterizerArray<BatchVertex3DInstanced> instanced_vertices;
		RasterizerArray<Batch3D> batches;
		RasterizerArray<BSortItem3D> sort_items;

		LocalVector<void *> bypassed_opaque_items;
		LocalVector<BSortItem3D> bypassed_transparent_items;

		uint32_t total_verts;
		uint32_t total_indices;
	} bdata;

	struct RenderItemState3D {
		void reset() {
			current_state_hash = 0;
			current_material_data = nullptr;
			consumed_uniform_vectors = 0;
		}
		uint64_t current_state_hash;
		typename T_API::MaterialData *current_material_data;
		uint32_t consumed_uniform_vectors;
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

	template <class T_SURFACE>
	void batch_scene_render_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent);

	template <class T_SURFACE>
	void record_items(T_SURFACE **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent);

	struct SortOpaque {
		_FORCE_INLINE_ bool operator()(const BSortItem3D &A, const BSortItem3D &B) const {
			if (A.state_hash == B.state_hash) {
				return A.depth < B.depth;
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

	template <class T_SURFACE>
	void join_and_flush_sorted_items(bool p_transparent);

	template <class T_SURFACE>
	bool try_join_item(T_SURFACE *p_surface, RenderItemState3D &r_ris);

	template <class T_SURFACE>
	bool prefill_joined_item(T_SURFACE *p_surface);

	template <class T_SURFACE>
	void flush_render_batches(typename T_API::MaterialData *p_material_data);

	template <class T_SURFACE>
	void render_bypassed_items(bool p_transparent);

	void _software_transform_vertex(BatchVector3 &r_v, const Transform3D &p_tr) const;
	void _software_transform_normal(BatchVector3 &r_n, const Transform3D &p_tr) const;

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

	BatchVertex3D *_batch_vertex_request_new() {
		return bdata.vertices.request();
	}
};

// Include the template implementation inline
#include "rasterizer_scene_batcher_common.inc"

#endif // RASTERIZER_SCENE_BATCHER_COMMON_H
