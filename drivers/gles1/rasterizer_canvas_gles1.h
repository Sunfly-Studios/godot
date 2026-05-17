/**************************************************************************/
/*  rasterizer_canvas_gles1.h                                             */
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

#ifndef RASTERIZER_CANVAS_GLES1_H
#define RASTERIZER_CANVAS_GLES1_H

#ifdef GLES1_ENABLED

#include "rasterizer_scene_gles1.h"
#include "servers/rendering/renderer_canvas_render.h"
#include "servers/rendering/renderer_compositor.h"
#include "storage/material_storage.h"
#include "storage/texture_storage.h"
#include "drivers/rasterizer_common/batch/rasterizer_canvas_batcher_common.h"

#include "drivers/gles1/shaders/canvas.glsl.gen.h"
#include "drivers/gles1/shaders/canvas_occlusion.glsl.gen.h"
#include "polyfill_gles1.h"

class RasterizerCanvasGLES1;

struct BatcherAPIGLES1 {
	using Canvas = RasterizerCanvasGLES1;
	using MaterialData = GLES1::CanvasMaterialData;
	using TextureStorage = GLES1::TextureStorage;
	using Texture = GLES1::Texture;
	using CanvasTexture = GLES1::CanvasTexture;
	using Shader = GLES1::Shader;

	static constexpr bool FORCE_BAKE_MODULATE = true;
};

class RasterizerCanvasGLES1 : public RendererCanvasRender, public RasterizerCanvasBatcherCommon<BatcherAPIGLES1> {
	friend class RasterizerCanvasBatcherCommon<BatcherAPIGLES1>;

	static RasterizerCanvasGLES1 *singleton;

	_FORCE_INLINE_ void _update_transform_2d_to_mat2x4(const Transform2D &p_transform, float *p_mat2x4);
	_FORCE_INLINE_ void _update_transform_2d_to_mat2x3(const Transform2D &p_transform, float *p_mat2x3);

	_FORCE_INLINE_ void _update_transform_2d_to_mat4(const Transform2D &p_transform, float *p_mat4);
	_FORCE_INLINE_ void _update_transform_to_mat4(const Transform3D &p_transform, float *p_mat4);

protected:
	struct Uniforms {
		Transform3D projection_matrix;
		Transform2D modelview_matrix;
		Transform2D extra_matrix;
		Color final_modulate;
		float time;
	};

	struct Data {
		GLuint canvas_quad_vertices;
		GLuint polygon_buffer;
		GLuint polygon_index_buffer;

		uint32_t polygon_buffer_size;
		uint32_t polygon_index_buffer_size;

		GLuint ninepatch_vertices;
		GLuint ninepatch_elements;

		GLuint light_vector_tex;

		RID canvas_shader_default_version;
	} data;

	struct PolyData {
		LocalVector<int> indices;
		LocalVector<Point2> points;
		LocalVector<Color> colors;
		LocalVector<Point2> uvs;
		Vector<int> bones;
		Vector<float> weights;
	};
	uint32_t next_polygon_id = 1;
	HashMap<RendererCanvasRender::PolygonID, PolyData> polygon_cache;
	RasterizerPooledIndirectList<PolyData> _polydata;

	struct Limits {
		float light_multiplier = 0.0f;
	};

	Limits limit_settings;

	/******************/
	/**** LIGHTING ****/
	/******************/

	struct CanvasLight {
		RID texture;
		GLuint directional_tex_id = 0;
		Vector2 last_light_dir = Vector2(0.0f, 0.0f);
		uint8_t directional_pixel[4] = { 127, 127, 127, 255 };
		uint32_t context_generation = 0;

		struct {
			bool enabled = false;
			float z_far;
			Transform2D light_to_world;
			Vector<Vector2> shadow_volumes;
			Vector<Vector2> directional_shadow_volumes;
		} shadow;
	};

	RID_Owner<CanvasLight> canvas_light_owner;

	struct OccluderPolygon {
		RS::CanvasOccluderPolygonCullMode cull_mode = RS::CANVAS_OCCLUDER_POLYGON_CULL_DISABLED;
		int line_point_count = 0;
		Vector<Vector2> lines;
		GLuint vertex_array = 0;
	};

	RID_Owner<OccluderPolygon> occluder_polygon_owner;

	void _update_shadow_atlas();

	struct {
		CanvasOcclusionShaderGLES1 shader;
		RID shader_version;
	} shadow_render;

	struct State {
		Uniforms uniforms;
		bool canvas_texscreen_used;
		CanvasShaderGLES1 *canvas_shader;

		RID shader_version;
		uint64_t specialization;
		CanvasShaderGLES1::ShaderVariant mode_variant = CanvasShaderGLES1::ShaderVariant::MODE_QUAD;

		bool using_ninepatch = false;
		bool using_skeleton = false;

		Transform2D skeleton_transform;
		Transform2D skeleton_transform_inverse;
		Size2i skeleton_texture_size;

		RID current_tex = RID();
		bool current_tex_is_rt = false;
		Size2 texpixel_size = Size2(1.0, 1.0);
		Size2i texture_size = Size2i(1, 1);
		bool normal_used = false;

		Transform3D vp;
		Light *using_light = nullptr;
		bool using_shadow = false;

		RID render_target;
		double time = 0.0;

		Color specular_shininess;

		Light *directional_light_list = nullptr;
		bool using_directional_lights = false;

		bool state_dirty = true;
		bool transparent_render = false;

		RS::CanvasItemTextureFilter default_filter = RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST;
		RS::CanvasItemTextureRepeat default_repeat = RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED;
		RS::CanvasItemTextureFilter current_filter = RS::CANVAS_ITEM_TEXTURE_FILTER_MAX;
		RS::CanvasItemTextureRepeat current_repeat = RS::CANVAS_ITEM_TEXTURE_REPEAT_MAX;
	} state;

	RID default_canvas_texture;
	int maximum_attributes = 0;

	// Internal bindings
	void _set_texture_rect_mode(bool p_texture_rect, bool p_light_angle = false, bool p_modulate = false, bool p_large_vertex = false);
	void _bind_canvas_texture(RID p_texture, RS::CanvasItemTextureFilter p_base_filter, RS::CanvasItemTextureRepeat p_base_repeat);
	void _set_canvas_uniforms();
	void _bind_quad_buffer() const;

	_FORCE_INLINE_ bool _buffer_orphan_and_upload(unsigned int p_buffer_size_bytes, unsigned int p_offset_bytes, unsigned int p_data_size_bytes, const void *p_data, GLenum p_target, GLenum p_usage, bool p_optional_orphan) const;
	void _legacy_draw_polygon(Item::CommandPolygon *p_poly, GLES1::CanvasMaterialData *p_material);
	void _legacy_draw_primitive(Item::CommandPrimitive *p_pr, GLES1::CanvasMaterialData *p_material);
	void _legacy_draw_line(Item::CommandPrimitive *p_pr, GLES1::CanvasMaterialData *p_material);
	void _draw_gui_primitive(int p_points, const Vector2 *p_vertices, const Color *p_colors, const Vector2 *p_uvs, const float *p_light_angles = nullptr);

public:
	static RasterizerCanvasGLES1 *get_singleton();
	RasterizerCanvasGLES1();
	~RasterizerCanvasGLES1();

	virtual void set_time(double p_time);

	void reset_canvas();

	bool is_context_lost() const;
	void force_context_recovery();

	// The Core Rendering Loop
	virtual void canvas_render_items_begin(const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform);
	virtual void canvas_render_items_end();
	void canvas_render_items_internal(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform);
	virtual void canvas_begin(RID p_to_render_target, bool p_to_backbuffer);
	virtual void canvas_end();

	void canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_list, const Transform2D &p_canvas_transform, RS::CanvasItemTextureFilter p_default_filter, RS::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingMethod::RenderInfo *r_render_info = nullptr) override;

	RendererCanvasRender::PolygonID request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs = Vector<Point2>(), const Vector<int> &p_bones = Vector<int>(), const Vector<float> &p_weights = Vector<float>()) override;
	void free_polygon(PolygonID p_polygon) override;

	// Light & Shadow Stubs
	RID light_create() override;
	void light_set_texture(RID p_rid, RID p_texture) override;
	void light_set_use_shadow(RID p_rid, bool p_enable) override;
	void light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) override;
	void light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) override;

	void render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) override;
	RID occluder_polygon_create() override;
	void occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) override;
	void occluder_polygon_set_cull_mode(RID p_occluder, RS::CanvasOccluderPolygonCullMode p_mode) override;
	void set_shadow_texture_size(int p_size) override;

	bool free(RID p_rid) override;
	void update() override;
	virtual void set_debug_redraw(bool p_enabled, double p_time, const Color &p_color) override;
	virtual uint32_t get_pipeline_compilations(RS::PipelineSource p_source) override;

	GLenum get_gl_primitive_type(RS::PrimitiveType primitive) {
		GLenum gl_primitive = GL_TRIANGLES;
		switch (primitive) {
			case RS::PRIMITIVE_POINTS:
				gl_primitive = GL_POINTS;
				break;
			case RS::PRIMITIVE_LINES:
				gl_primitive = GL_LINES;
				break;
			case RS::PRIMITIVE_LINE_STRIP:
				gl_primitive = GL_LINE_STRIP;
				break;
			case RS::PRIMITIVE_TRIANGLES:
				gl_primitive = GL_TRIANGLES;
				break;
			case RS::PRIMITIVE_TRIANGLE_STRIP:
				gl_primitive = GL_TRIANGLE_STRIP;
				break;
			default:
				break;
		}
		return gl_primitive;
	}

	_FORCE_INLINE_ void set_gl_blend_mode(
		GLES1::CanvasShaderData::BlendMode blend_mode,
		bool transparent_rt
	) {
		glEnable(GL_BLEND);

		bool support_subtract = GLES1::Config::get_singleton()->support_blend_subtract;
		bool support_separate = GLES1::Config::get_singleton()->support_blend_func_separate;

		switch (blend_mode) {
			case GLES1::CanvasShaderData::BLEND_MODE_DISABLED: {
				glDisable(GL_BLEND);
			} break;
			case GLES1::CanvasShaderData::BLEND_MODE_MIX: {
				if (support_subtract) {
					glBlendEquationOES(GL_FUNC_ADD_OES);
				}
				if (transparent_rt) {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					}
				} else {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					} else {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					}
				}
			} break;
			case GLES1::CanvasShaderData::BLEND_MODE_ADD: {
				if (support_subtract) {
					glBlendEquationOES(GL_FUNC_ADD_OES);
				}
				if (transparent_rt) {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
					} else {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE);
					}
				} else {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
					} else {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE);
					}
				}
			} break;
			case GLES1::CanvasShaderData::BLEND_MODE_SUB: {
				if (support_subtract) {
					glBlendEquationOES(GL_FUNC_REVERSE_SUBTRACT_OES);
					if (transparent_rt) {
						if (support_separate) {
							glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
						} else {
							glBlendFunc(GL_SRC_ALPHA, GL_ONE);
						}
					} else {
						if (support_separate) {
							glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
						} else {
							glBlendFunc(GL_SRC_ALPHA, GL_ONE);
						}
					}
				} else {
					// Fallback for missing subtract extension:
					// Simulate subtraction by using ZERO and ONE_MINUS_SRC_COLOR
					// It's not a perfect subtract, but it's the standard GLES1.1 workaround.
					glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
				}
			} break;
			case GLES1::CanvasShaderData::BLEND_MODE_MUL: {
				if (support_subtract) {
					glBlendEquationOES(GL_FUNC_ADD_OES);
				}
				if (transparent_rt) {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
					} else {
						glBlendFunc(GL_DST_COLOR, GL_ZERO);
					}
				} else {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
					} else {
						glBlendFunc(GL_DST_COLOR, GL_ZERO);
					}
				}
			} break;
			case GLES1::CanvasShaderData::BLEND_MODE_PMALPHA: {
				if (support_subtract) {
					glBlendEquationOES(GL_FUNC_ADD_OES);
				}
				if (transparent_rt) {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					}
				} else {
					if (support_separate) {
						glBlendFuncSeparateOES(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					} else {
						glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					}
				}
			} break;
			default:
				break;
		}
	}

	_FORCE_INLINE_ bool check_orphan_success(bool success) {
		if (unlikely(!success)) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glDisableClientState(GL_VERTEX_ARRAY);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glDisableClientState(GL_COLOR_ARRAY);
			glDisable(GL_TEXTURE_2D);
			ERR_PRINT("GLES1: Failed to upload buffer orphan. Out of memory. Dropping geometry.");
			return false;
		}
		return true;
	}

private:
	// The Batcher Bridge
	void canvas_render_items_implementation(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform);
	void render_batches(Item::Command *const *p_commands, Item *p_current_clip, bool &r_reclip, GLES1::CanvasMaterialData *p_material);

	void _batch_upload_buffers();
	void _batch_render_generic(const Batch &p_batch, GLES1::CanvasMaterialData *p_material);

	// RasterizerCanvasBatcherGLES1 Template Hooks
	void gl_enable_scissor(int p_x, int p_y, int p_width, int p_height) const;
	void gl_disable_scissor() const;

public:
	void initialize();
};

#endif // GLES1_ENABLED
#endif // RASTERIZER_CANVAS_GLES1_H
