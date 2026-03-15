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

#ifndef RASTERIZER_SCENE_BATCHER_COMMON_H
#define RASTERIZER_SCENE_BATCHER_COMMON_H

#include "core/os/os.h"
#include "core/templates/local_vector.h"
#include "drivers/gles_common/batch/batcher_enum.h"
#include "drivers/gles_common/batch/rasterizer_array.h"
#include "drivers/gles_common/batch/rasterizer_asserts.h"
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
	};

	// A batch represents a single draw call of aggregated geometry
	struct Batch3D {
		BatcherEnums::BatchType type;
		RID material;

		uint32_t first_vert;
		uint32_t num_verts;
		uint32_t num_indices;

		// The base instance, useful if we fallback to hardware transforms
		const RenderGeometryInstance *instance;
	};

	// Sorting.
	// Opaque needs front-to-back or material sort.
	// Transparent needs back-to-front.
	struct BSortItem3D {
		void assign(const BSortItem3D &o) {
			instance = o.instance;
			depth = o.depth;
			material_hash = o.material_hash;
		}
		RenderGeometryInstance *instance;
		float depth;
		uint64_t material_hash;
	};

	struct BatchData3D {
		BatchData3D() {
			reset_flush();
			gl_vertex_buffer = 0;
			gl_index_buffer = 0;
			max_vertices = 0;
			max_indices = 0;
			settings_use_batching = true;
		}

		void reset_flush() {
			batches.reset();
			vertices.reset();
			indices.reset();
			total_verts = 0;
			total_indices = 0;
		}

		uint32_t gl_vertex_buffer;
		uint32_t gl_index_buffer;

		uint32_t max_vertices;
		uint32_t max_indices;

		RasterizerArray<BatchVertex3D> vertices;
		RasterizerArray<uint16_t> indices;
		RasterizerArray<Batch3D> batches;
		RasterizerArray<BSortItem3D> sort_items;

		bool settings_use_batching;

		uint32_t total_verts;
		uint32_t total_indices;
	} bdata;

	struct RenderItemState3D {
		void reset() {
			current_material = RID();
			rebind_shader = true;
			joined_item_batch_type_flags = 0;
		}
		RID current_material;
		bool rebind_shader;
		uint32_t joined_item_batch_type_flags;
	} _render_item_state;

private:
	// CRTP Cast to get the driver-specific Scene
	typename T_API::Scene *get_this() { return static_cast<typename T_API::Scene *>(this); }
	const typename T_API::Scene *get_this() const { return static_cast<const typename T_API::Scene *>(this); }

protected:
	void batch_constructor();
	void batch_initialize();
	void batch_scene_begin();
	void batch_scene_end();

	void batch_scene_render_items(RenderGeometryInstance **p_instances, int p_count, const Transform3D &p_camera_transform, bool p_transparent);

	void record_items(RenderGeometryInstance **p_instances, int p_count, const Transform3D &p_camera_transform);
	bool try_join_item(RenderGeometryInstance *p_instance, RenderItemState3D &r_ris);
	void join_sorted_items();
	void sort_items(bool p_transparent);

	void flush_render_batches(RenderGeometryInstance *p_first_instance, RID p_material);
	bool prefill_joined_item(RenderGeometryInstance *p_instance);

protected:
	void _software_transform_vertex(BatchVector3 &r_v, const Transform3D &p_tr) const;
	void _software_transform_normal(BatchVector3 &r_n, const Transform3D &p_tr) const;

public:
	Batch3D *_batch_request_new(bool p_blank = true) {
		Batch3D *batch = bdata.batches.request();
		if (!batch) {
			bdata.batches.grow();
			batch = bdata.batches.request();
			RAST_DEBUG_ASSERT(batch);
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
