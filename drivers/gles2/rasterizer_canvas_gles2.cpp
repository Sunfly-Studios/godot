/**************************************************************************/
/*  rasterizer_canvas_gles2.cpp                                           */
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

#include "rasterizer_canvas_gles2.h"

#ifdef GLES2_ENABLED

#include "rasterizer_gles2.h"
#include "rasterizer_scene_gles2.h"

#include "core/config/project_settings.h"
#include "core/math/geometry_2d.h"
#include "core/math/transform_interpolator.h"
#include "servers/rendering/rendering_server_default.h"
#include "storage/config.h"
#include "storage/material_storage.h"
#include "storage/mesh_storage.h"
#include "storage/particles_storage.h"
#include "storage/texture_storage.h"

void RasterizerCanvasGLES2::_update_transform_2d_to_mat4(const Transform2D &p_transform, float *p_mat4) {

}

void RasterizerCanvasGLES2::_update_transform_2d_to_mat2x4(const Transform2D &p_transform, float *p_mat2x4) {

}

void RasterizerCanvasGLES2::_update_transform_2d_to_mat2x3(const Transform2D &p_transform, float *p_mat2x3) {

}

void RasterizerCanvasGLES2::_update_transform_to_mat4(const Transform3D &p_transform, float *p_mat4) {

}

void RasterizerCanvasGLES2::canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_light_list, const Transform2D &p_canvas_transform, RS::CanvasItemTextureFilter p_default_filter, RS::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingMethod::RenderInfo *r_render_info) {

}

void RasterizerCanvasGLES2::_render_items(RID p_to_render_target, int p_item_count, const Transform2D &p_canvas_transform_inverse, Light *p_lights, bool &r_sdf_used, bool p_to_backbuffer, RenderingMethod::RenderInfo *r_render_info, bool p_backbuffer_has_mipmaps) {

}

void RasterizerCanvasGLES2::_record_item_commands(const Item *p_item, RID p_render_target, const Transform2D &p_canvas_transform_inverse, Item *&current_clip, GLES2::CanvasShaderData::BlendMode p_blend_mode, Light *p_lights, uint32_t &r_index, bool &r_batch_broken, bool &r_sdf_used, const Point2 &p_repeat_offset) {

}

_FORCE_INLINE_ static uint32_t _indices_to_primitives(RS::PrimitiveType p_primitive, uint32_t p_indices) {
    return 0;
}

void RasterizerCanvasGLES2::_render_batch(Light *p_lights, uint32_t p_index, RenderingMethod::RenderInfo *r_render_info) {

}

void RasterizerCanvasGLES2::_add_to_batch(uint32_t &r_index, bool &r_batch_broken) {

}

void RasterizerCanvasGLES2::_new_batch(bool &r_batch_broken) {

}

void RasterizerCanvasGLES2::_enable_attributes(uint32_t p_start, bool p_primitive, uint32_t p_rate) {

}

RID RasterizerCanvasGLES2::light_create() {
    return RID();
}

void RasterizerCanvasGLES2::light_set_texture(RID p_rid, RID p_texture) {

}

void RasterizerCanvasGLES2::light_set_use_shadow(RID p_rid, bool p_enable) {

}

void RasterizerCanvasGLES2::light_update_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, const Rect2 &p_light_rect) {

}

void RasterizerCanvasGLES2::light_update_directional_shadow(RID p_rid, int p_shadow_index, const Transform2D &p_light_xform, int p_light_mask, float p_cull_distance, const Rect2 &p_clip_rect, LightOccluderInstance *p_occluders) {

}

void RasterizerCanvasGLES2::_update_shadow_atlas() {

}

void RasterizerCanvasGLES2::render_sdf(RID p_render_target, LightOccluderInstance *p_occluders) {

}

RID RasterizerCanvasGLES2::occluder_polygon_create() {
    return RID();
}

void RasterizerCanvasGLES2::occluder_polygon_set_shape(RID p_occluder, const Vector<Vector2> &p_points, bool p_closed) {

}

void RasterizerCanvasGLES2::occluder_polygon_set_cull_mode(RID p_occluder, RS::CanvasOccluderPolygonCullMode p_mode) {

}

void RasterizerCanvasGLES2::set_shadow_texture_size(int p_size) {

}

bool RasterizerCanvasGLES2::free(RID p_rid) {
	return true;
}

void RasterizerCanvasGLES2::update() {

}

void RasterizerCanvasGLES2::canvas_begin(RID p_to_render_target, bool p_to_backbuffer, bool p_backbuffer_has_mipmaps) {

}

void RasterizerCanvasGLES2::_bind_canvas_texture(RID p_texture, RS::CanvasItemTextureFilter p_base_filter, RS::CanvasItemTextureRepeat p_base_repeat) {

}

void RasterizerCanvasGLES2::_prepare_canvas_texture(RID p_texture, RS::CanvasItemTextureFilter p_base_filter, RS::CanvasItemTextureRepeat p_base_repeat, uint32_t &r_index, Size2 &r_texpixel_size) {

}

void RasterizerCanvasGLES2::reset_canvas() {

}

void RasterizerCanvasGLES2::draw_lens_distortion_rect(const Rect2 &p_rect, float p_k1, float p_k2, const Vector2 &p_eye_center, float p_oversample) {

}

RendererCanvasRender::PolygonID RasterizerCanvasGLES2::request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, const Vector<int> &p_bones, const Vector<float> &p_weights) {
    return 0;
}

void RasterizerCanvasGLES2::free_polygon(PolygonID p_polygon) {

}

// Creates a new uniform buffer and uses it right away
// This expands the instance buffer continually
// In theory allocations can reach as high as number of windows * 3 frames
// because OpenGL can start rendering subsequent frames before finishing the current one
void RasterizerCanvasGLES2::_allocate_instance_data_buffer() {

}

void RasterizerCanvasGLES2::_allocate_instance_buffer() {

}

void RasterizerCanvasGLES2::set_time(double p_time) {
	state.time = p_time;
}

RasterizerCanvasGLES2 *RasterizerCanvasGLES2::singleton = nullptr;

RasterizerCanvasGLES2 *RasterizerCanvasGLES2::get_singleton() {
	return singleton;
}

RasterizerCanvasGLES2::RasterizerCanvasGLES2() {
	singleton = this;
}

RasterizerCanvasGLES2::~RasterizerCanvasGLES2() {
	singleton = nullptr;
}

#endif // GLES3_ENABLED
