/**************************************************************************/
/*  rasterizer_canvas_batcher_common.h                                    */
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

#ifndef RASTERIZER_CANVAS_BATCHER_COMMON_H
#define RASTERIZER_CANVAS_BATCHER_COMMON_H

#include "core/os/os.h"
#include "core/templates/local_vector.h"
#include "batcher_enum.h"
#include "rasterizer_array.h"
#include "rasterizer_asserts.h"
#include "core/config/project_settings.h"
#include "servers/rendering/renderer_compositor.h"

template <typename T_API>
class RasterizerCanvasBatcherCommon {
public:
	// used to determine whether we use hardware transform (none)
	// software transform all verts, or software transform just a translate
	// (no rotate or scale)
	enum TransformMode {
		TM_NONE,
		TM_ALL,
		TM_TRANSLATE,
	};

	// pod versions of vector and color and RID, need to be 32 bit for vertex format
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
		void to(Vector2 &r_o) const {
			r_o.x = x;
			r_o.y = y;
		}
	};

	// Updated from `float`s
	// to `uint8_t`s for performance.
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

	// simplest FVF - local or baked position
	struct BatchVertex {
		// must be 32 bit pod
		BatchVector2 pos;
		BatchVector2 uv;
	};

	// simple FVF but also incorporating baked color
	struct BatchVertexColored {
		BatchVector2 pos;
		BatchVector2 uv;

		// must be 32-bit pod
		BatchColor col;
	};

	// if we are using normal mapping, we need light angles to be sent
	struct BatchVertexLightAngled {
		BatchVector2 pos;
		BatchVector2 uv;
		BatchColor col;

		// must be pod
		float light_angle;
	};

	// CUSTOM SHADER vertex formats. These are larger but will probably
	// be needed with custom shaders in order to have the data accessible in the shader.

	// if we are using COLOR in vertex shader but not position (VERTEX)
	struct BatchVertexModulated {
		BatchVector2 pos;
		BatchVector2 uv;
		BatchColor col;
		BatchColor modulate;
	};

	struct BatchTransform {
		BatchVector2 translate;
		BatchVector2 basis[2];
	};

	// last resort, specially for custom shader, we put everything possible into a huge FVF
	// not very efficient, but better than no batching at all.
	struct BatchVertexLarge {
		BatchVector2 pos;
		BatchVector2 uv;
		BatchColor col;
		BatchColor modulate;

		// must be pod
		BatchTransform transform;
	};

	// Batch should be as small as possible, and ideally nicely aligned (is 32 bytes at the moment)
	struct Batch {
		BatcherEnums::BatchType type; // should be 16 bit
		uint16_t batch_texture_id;

		// also item reference number
		RendererCanvasRender::Item::Command *first_command;

		// in the case of DEFAULT, this is num commands.
		// with rects, is number of command and rects.
		// with lines, is number of lines
		uint32_t num_commands;

		// first vertex of this batch in the vertex lists
		uint32_t first_vert;

		BatchColor color;

		const RendererCanvasRender::Item *item;
	};

	struct BatchTex {
		// TODO: update this. Right now we only handle texture and normal, which should be merged into a single canvas texture
		//			we should also switch to RS::CanvasItemTextureFilter and RS::CanvasItemTextureRepeat

		enum TileMode : uint32_t {
			TILE_OFF,
			TILE_NORMAL,
			TILE_FORCE_REPEAT,
		};
		RID RID_texture;
		RID RID_normal;
		TileMode tile_mode;
		BatchVector2 tex_pixel_size;
		uint32_t flags;
	};

	// items in a list to be sorted prior to joining
	struct BSortItem {
		// have a function to keep as pod, rather than operator
		void assign(const BSortItem &o) {
			item = o.item;
			z_index = o.z_index;
		}
		RendererCanvasRender::Item *item;
		int z_index;
	};

	// batch item may represent 1 or more items
	struct BItemJoined {
		uint32_t first_item_ref;
		uint32_t num_item_refs;

		Rect2 bounding_rect;

		// note the z_index  may only be correct for the first of the joined item references
		// this has implications for light culling with z ranged lights.
		int16_t z_index;

		// these are defined in BatcherEnums::BatchFlags
		uint16_t flags;

		// we are always splitting items with lots of commands,
		// and items with unhandled primitives (default)
		bool use_hardware_transform() const { return num_item_refs == 1; }
	};

	struct BItemRef {
		RendererCanvasRender::Item *item;
		Color final_modulate;
	};

	struct BLightRegion {
		void reset() {
			light_bitfield = 0;
			shadow_bitfield = 0;
			too_many_lights = false;
		}
		uint64_t light_bitfield;
		uint64_t shadow_bitfield;
		bool too_many_lights; // we can only do light region optimization if there are 64 or less lights
	};

	struct BatchData {
		BatchData() {
			reset_flush();
			reset_joined_item();

			gl_vertex_buffer = 0;
			gl_index_buffer = 0;
			max_quads = 0;
			vertex_buffer_size_units = 0;
			vertex_buffer_size_bytes = 0;
			index_buffer_size_units = 0;
			index_buffer_size_bytes = 0;

			use_colored_vertices = false;

			settings_use_batching = false;
			settings_max_join_item_commands = 0;
			settings_colored_vertex_format_threshold = 0.0f;
			settings_batch_buffer_num_verts = 0;
			scissor_threshold_area = 0.0f;
			joined_item_batch_flags = 0;
			diagnose_frame = false;
			next_diagnose_tick = 10000;
			diagnose_frame_number = 9999999999; // some high number
			join_across_z_indices = true;
			settings_item_reordering_lookahead = 0;

			settings_use_batching_original_choice = false;
			settings_flash_batching = false;
			settings_diagnose_frame = false;
			settings_scissor_lights = false;
			settings_scissor_threshold = -1.0f;
			settings_use_single_rect_fallback = false;
			settings_use_software_skinning = true;
			settings_ninepatch_mode = 0; // default
			settings_light_max_join_items = 16;

			settings_uv_contract = false;
			settings_uv_contract_amount = 0.0f;

			buffer_mode_batch_upload_send_null = true;
			buffer_mode_batch_upload_flag_stream = false;

			stats_items_sorted = 0;
			stats_light_items_joined = 0;
		}

		// called for each joined item
		void reset_joined_item() {
			// noop but left in as a stub
		}

		// called after each flush
		void reset_flush() {
			batches.reset();
			batch_textures.reset();

			vertices.reset();
			light_angles.reset();
			vertex_colors.reset();
			vertex_modulates.reset();
			vertex_transforms.reset();

			total_quads = 0;
			total_verts = 0;
			total_color_changes = 0;

			use_light_angles = false;
			use_modulate = false;
			use_large_verts = false;
			fvf = BatcherEnums::FVF_REGULAR;
		}

		uint32_t gl_vertex_buffer;
		uint32_t gl_index_buffer;

		uint32_t max_quads;
		uint32_t vertex_buffer_size_units;
		uint32_t vertex_buffer_size_bytes;
		uint32_t index_buffer_size_units;
		uint32_t index_buffer_size_bytes;

		// small vertex FVF type - pos and UV.
		// This will always be written to initially, but can be translated
		// to larger FVFs if necessary.
		RasterizerArray<BatchVertex> vertices;

		// extra data which can be stored during prefilling, for later translation to larger FVFs
		RasterizerArray<float> light_angles;
		RasterizerArray<BatchColor> vertex_colors; // these aren't usually used, but are for polys
		RasterizerArray<BatchColor> vertex_modulates;
		RasterizerArray<BatchTransform> vertex_transforms;

		// instead of having a different buffer for each vertex FVF type
		// we have a special array big enough for the biggest FVF
		// which can have a changeable unit size, and reuse it.
		RasterizerUnitArray<uint8_t> unit_vertices;

		RasterizerArray<Batch> batches;
		RasterizerArray<Batch> batches_temp; // used for translating to colored vertex batches
		RasterizerArray_non_pod<BatchTex> batch_textures; // the only reason this is non-POD is because of RIDs

		// SHOULD THESE BE IN FILLSTATE?
		// flexible vertex format.
		// all verts have pos and UV.
		// some have color, some light angles etc.
		BatcherEnums::FVF fvf;
		bool use_colored_vertices;
		bool use_light_angles;
		bool use_modulate;
		bool use_large_verts;

		// if the shader is using MODULATE, we prevent baking color so the final_modulate can
		// be read in the shader.
		// if the shader is reading VERTEX, we prevent baking vertex positions with extra matrices etc
		// to prevent the read position being incorrect.
		// These flags are defined in BatcherEnums::BatchFlags
		uint32_t joined_item_batch_flags;

		RasterizerArray<BItemJoined> items_joined;
		RasterizerArray<BItemRef> item_refs;

		// items are sorted prior to joining
		RasterizerArray<BSortItem> sort_items;

		// new for Godot 4 .. the client outputs a linked list so we need to convert this
		// to a linear array
		LocalVector<RendererCanvasRender::Item::Command *> command_shortlist;

		// counts
		int total_quads;
		int total_verts;

		// we keep a record of how many color changes caused new batches
		// if the colors are causing an excessive number of batches, we switch
		// to alternate batching method and add color to the vertex format.
		int total_color_changes;

		// measured in pixels, recalculated each frame
		float scissor_threshold_area;

		// diagnose this frame, every nTh frame when settings_diagnose_frame is on
		bool diagnose_frame;
		String frame_string;
		uint32_t next_diagnose_tick;
		uint64_t diagnose_frame_number;

		// whether to join items across z_indices - this can interfere with z ranged lights,
		// so has to be disabled in some circumstances
		bool join_across_z_indices;

		// global settings
		bool settings_use_batching; // the current use_batching (affected by flash)
		bool settings_use_batching_original_choice; // the choice entered in project settings
		bool settings_flash_batching; // for regression testing, flash between non-batched and batched renderer
		bool settings_diagnose_frame; // print out batches to help optimize / regression test
		int settings_max_join_item_commands;
		float settings_colored_vertex_format_threshold;
		int settings_batch_buffer_num_verts;
		bool settings_scissor_lights;
		float settings_scissor_threshold; // 0.0 to 1.0
		int settings_item_reordering_lookahead;
		bool settings_use_single_rect_fallback;
		bool settings_use_software_skinning;
		int settings_light_max_join_items;
		int settings_ninepatch_mode;

		// buffer orphaning modes
		bool buffer_mode_batch_upload_send_null;
		bool buffer_mode_batch_upload_flag_stream;

		// uv contraction
		bool settings_uv_contract;
		float settings_uv_contract_amount;

		// only done on diagnose frame
		void reset_stats() {
			stats_items_sorted = 0;
			stats_light_items_joined = 0;
		}

		// frame stats (just for monitoring and debugging)
		int stats_items_sorted;
		int stats_light_items_joined;
	} bdata;

	struct FillState {
		void reset_flush() {
			// don't reset members that need to be preserved after flushing
			// half way through a list of commands
			curr_batch = 0;
			batch_tex_id = -1;
			texpixel_size = Vector2(1, 1);
			contract_uvs = false;

			sequence_batch_type_flags = 0;
		}

		void reset_joined_item(bool p_use_hardware_transform) {
			reset_flush();
			use_hardware_transform = p_use_hardware_transform;
			extra_matrix_sent = false;
		}

		// for batching multiple types, we don't allow mixing RECTs / LINEs etc.
		// using flags allows quicker rejection of sequences with different batch types
		uint32_t sequence_batch_type_flags;

		Batch *curr_batch = nullptr;
		Batch dummy_batch;
		int batch_tex_id;
		bool use_hardware_transform;
		bool contract_uvs;
		Vector2 texpixel_size;
		Color final_modulate;
		TransformMode transform_mode;
		TransformMode orig_transform_mode;

		// support for extra matrices
		bool extra_matrix_sent; // whether sent on this item (in which case sofware transform can't be used untl end of item)
		//int transform_extra_command_number_p1; // plus one to allow fast checking against zero
		RendererCanvasRender::Item::Command *transform_extra_command;
		Transform2D transform_combined; // final * extra
	};

	struct RenderItemState {
		RenderItemState() { reset(); }
		void reset() {
			current_clip = nullptr;
			shader_cache = nullptr;
			rebind_shader = true;
			prev_use_skeleton = false;
			prev_distance_field = false;
			last_blend_mode = -1;
			canvas_last_material = RID();
			item_group_z = 0;
			item_group_light = nullptr;
			final_modulate = Color(-1.0, -1.0, -1.0, -1.0);

			joined_item_batch_type_flags_curr = 0;
			joined_item_batch_type_flags_prev = 0;
			joined_item = nullptr;
		}

		RendererCanvasRender::Item *current_clip;
		typename T_API::Shader *shader_cache;
		bool rebind_shader;
		bool prev_use_skeleton;
		bool prev_distance_field;
		int last_blend_mode;
		RID canvas_last_material;
		Color final_modulate;

		BItemJoined *joined_item;
		bool join_batch_break;
		BLightRegion light_region;

		uint32_t joined_item_batch_type_flags_curr;
		uint32_t joined_item_batch_type_flags_prev;

		int item_group_z;
		Color item_group_modulate;
		RendererCanvasRender::Light *item_group_light;
		Transform2D item_group_base_transform;
	} _render_item_state;

	bool use_nvidia_rect_workaround;

private:
	// CRTP Cast to get the driver-specific Canvas
	typename T_API::Canvas *get_this() { return static_cast<typename T_API::Canvas *>(this); }
	const typename T_API::Canvas *get_this() const { return static_cast<const typename T_API::Canvas *>(this); }

protected:
	void batch_constructor();
	void batch_initialize();
	void batch_canvas_begin();
	void batch_canvas_end();
	void batch_canvas_render_items_begin(const Color &p_modulate, RendererCanvasRender::Light *p_light, const Transform2D &p_base_transform);
	void batch_canvas_render_items_end();
	void batch_canvas_render_items(RendererCanvasRender::Item *p_item_list, int p_z, const Color &p_modulate, RendererCanvasRender::Light *p_light, const Transform2D &p_base_transform);

	void record_items(RendererCanvasRender::Item *p_item_list, int p_z);
	bool try_join_item(RendererCanvasRender::Item *p_ci, RenderItemState &r_ris, bool &r_batch_break);
	void join_sorted_items();
	void sort_items();
	bool _sort_items_match(const BSortItem &p_a, const BSortItem &p_b) const;
	bool sort_items_from(int p_start);

	bool _disallow_item_join_if_batch_types_too_different(RenderItemState &r_ris, uint32_t btf_allowed);
	bool _detect_item_batch_break(RenderItemState &r_ris, RendererCanvasRender::Item *p_ci, bool &r_batch_break);

	void render_joined_item_commands(const BItemJoined &p_bij, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material, bool p_lit);

private:
	void flush_render_batches(RendererCanvasRender::Item *p_first_item, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material, uint32_t p_sequence_batch_type_flags);
	bool prefill_joined_item(FillState &r_fill_state, RendererCanvasRender::Item::Command **r_first_command, RendererCanvasRender::Item *p_item, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material);
	void _prefill_default_batch(FillState &r_fill_state, RendererCanvasRender::Item::Command *p_command, const RendererCanvasRender::Item &p_item);

	template <bool SEND_LIGHT_ANGLES>
	bool _prefill_rect(RendererCanvasRender::Item::CommandRect *rect, FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item::Command *p_command, RendererCanvasRender::Item *p_item, bool multiply_final_modulate);

	template <bool SEND_LIGHT_ANGLES>
	bool _prefill_ninepatch(RendererCanvasRender::Item::CommandNinePatch *p_np, FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item::Command *p_command, RendererCanvasRender::Item *p_item, bool multiply_final_modulate);

	template <bool SEND_LIGHT_ANGLES>
	bool _prefill_polygon(RendererCanvasRender::Item::CommandPolygon *p_poly, FillState &r_fill_state, RendererCanvasRender::Item::Command **p_command_start, RendererCanvasRender::Item::Command *p_command, RendererCanvasRender::Item *p_item, bool multiply_final_modulate);
	
	int _batch_find_or_create_tex(const RID &p_texture, const RID &p_normal, bool p_tile, int p_previous_match);

protected:
	void _legacy_canvas_item_render_commands(RendererCanvasRender::Item *p_item, RendererCanvasRender::Item *p_current_clip, bool &r_reclip, typename T_API::MaterialData *p_material);
	bool _light_scissor_begin(const Rect2 &p_item_rect, const Transform2D &p_light_xform, const Rect2 &p_light_rect, int p_rt_height) const;
	bool _light_find_intersection(const Rect2 &p_item_rect, const Transform2D &p_light_xform, const Rect2 &p_light_rect, Rect2 &r_cliprect) const;
	void _calculate_scissor_threshold_area();

private:
	void _translate_batches_to_vertex_colored_FVF();

	template <class BATCH_VERTEX_TYPE, bool INCLUDE_LIGHT_ANGLES, bool INCLUDE_MODULATE, bool INCLUDE_LARGE>
	void _translate_batches_to_larger_FVF(uint32_t p_sequence_batch_type_flags);

protected:
	void _software_transform_vertex(BatchVector2 &r_v, const Transform2D &p_tr) const;
	void _software_transform_vertex(Vector2 &r_v, const Transform2D &p_tr) const;
	TransformMode _find_transform_mode(const Transform2D &p_tr) const {
		// decided whether to do translate only for software transform
		if ((p_tr.columns[0].x == 1.0f) &&
				(p_tr.columns[0].y == 0.0f) &&
				(p_tr.columns[1].x == 0.0f) &&
				(p_tr.columns[1].y == 1.0f)) {
			return TM_TRANSLATE;
		}

		return TM_ALL;
	}

	typename T_API::Texture *_get_canvas_texture(const RID &p_texture) const {
		if (p_texture.is_valid()) {
			typename T_API::Texture *texture = T_API::TextureStorage::get_singleton()->get_texture(p_texture);
			if (texture) {
				return texture;
			}
		}
		return nullptr;
	}

public:
	// Requests a new batch object (optionally blank)
	// with automatic growth.
	// 
	// !!! IMPORTANT !!!
	// When running on a device low on memory,
	// this function will return `nullptr` if it fails.
	// Make sure to check if it actually
	// succeeds OR fails to not trigger
	// a hard-crash to the engine later down the line.
	Batch *_batch_request_new(bool p_blank = true) {
		Batch *batch = bdata.batches.request();
		if (!batch) {
			// grow the batches
			bdata.batches.grow();

			// and the temporary batches (used for color verts)
			bdata.batches_temp.reset();
			bdata.batches_temp.grow();

			// this should always succeed after growing
			// keyword _should_.
			batch = bdata.batches.request();
			if (unlikely(!batch)) {
				return nullptr;
			}
		}

		if (p_blank) {
			*batch = Batch();
		}

		return batch;
	}

	BatchVertex *_batch_vertex_request_new() {
		return bdata.vertices.request();
	}

protected:
	int godot4_commands_count(RendererCanvasRender::Item::Command *p_comm) const {
		int count = 0;
		int circuit_breaker = 65536;
		while (p_comm && circuit_breaker-- > 0) {
			count++;
			p_comm = p_comm->next;
		}
		ERR_FAIL_COND_V_MSG(circuit_breaker <= 0, 0, "GLES Batcher: Cyclic linked list detected in CanvasItem commands. Infinite loop prevented.");
		return count;
	}

	unsigned int godot4_commands_to_vector(RendererCanvasRender::Item::Command *p_comm, LocalVector<RendererCanvasRender::Item::Command *> &p_list) {
		int circuit_breaker = 65536;
		p_list.clear();
		while (p_comm && circuit_breaker-- > 0) {
			p_list.push_back(p_comm);
			p_comm = p_comm->next;
		}
		ERR_FAIL_COND_V_MSG(circuit_breaker <= 0, 0, "GLES Batcher: Cyclic linked list detected in CanvasItem commands. Infinite loop prevented.");
		return p_list.size();
	}

	// no need to compile these in in release, they are unneeded outside the editor and only add to executable size
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
//#include "batch_diagnose.inc"
#endif
};

// Include the template implementation inline
#include "rasterizer_canvas_batcher_common.inc"

#endif // RASTERIZER_CANVAS_BATCHER_COMMON_H
