/**************************************************************************/
/*  light_storage.cpp                                                     */
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

#ifdef GLES2_ENABLED

#include "light_storage.h"
#include "../rasterizer_gles2.h"
#include "../rasterizer_scene_gles2.h"
#include "core/config/project_settings.h"
#include "texture_storage.h"

using namespace GLES2;

LightStorage *LightStorage::singleton = nullptr;

LightStorage *LightStorage::get_singleton() {
	return singleton;
}

LightStorage::LightStorage() {
	singleton = this;

	directional_shadow.size = GLOBAL_GET("rendering/lights_and_shadows/directional_shadow/size");
	directional_shadow.use_16_bits = GLOBAL_GET("rendering/lights_and_shadows/directional_shadow/16_bits");

	// lightmap_probe_capture_update_speed = GLOBAL_GET("rendering/lightmapping/probe_capture/update_speed");
}

LightStorage::~LightStorage() {
	singleton = nullptr;
}

/* Light API */

void LightStorage::_light_initialize(RID p_light, RS::LightType p_type) {
    
}

RID LightStorage::directional_light_allocate() {
    return RID();
}

void LightStorage::directional_light_initialize(RID p_rid) {
}

RID LightStorage::omni_light_allocate() {
    return RID();
}

void LightStorage::omni_light_initialize(RID p_rid) {

}

RID LightStorage::spot_light_allocate() {
    return RID();
}

void LightStorage::spot_light_initialize(RID p_rid) {

}

void LightStorage::light_free(RID p_rid) {

}

void LightStorage::light_set_color(RID p_light, const Color &p_color) {

}

void LightStorage::light_set_param(RID p_light, RS::LightParam p_param, float p_value) {

}

void LightStorage::light_set_shadow(RID p_light, bool p_enabled) {

}

void LightStorage::light_set_projector(RID p_light, RID p_texture) {

}

void LightStorage::light_set_negative(RID p_light, bool p_enable) {

}

void LightStorage::light_set_cull_mask(RID p_light, uint32_t p_mask) {

}

void LightStorage::light_set_shadow_caster_mask(RID p_light, uint32_t p_caster_mask) {

}

uint32_t LightStorage::light_get_shadow_caster_mask(RID p_light) const {
    return 0;
}

void LightStorage::light_set_distance_fade(RID p_light, bool p_enabled, float p_begin, float p_shadow, float p_length) {

}

void LightStorage::light_set_reverse_cull_face_mode(RID p_light, bool p_enabled) {

}

void LightStorage::light_set_bake_mode(RID p_light, RS::LightBakeMode p_bake_mode) {

}

void LightStorage::light_omni_set_shadow_mode(RID p_light, RS::LightOmniShadowMode p_mode) {

}

RS::LightOmniShadowMode LightStorage::light_omni_get_shadow_mode(RID p_light) {
    return RS::LIGHT_OMNI_SHADOW_DUAL_PARABOLOID;
}

void LightStorage::light_directional_set_shadow_mode(RID p_light, RS::LightDirectionalShadowMode p_mode) {

}

void LightStorage::light_directional_set_blend_splits(RID p_light, bool p_enable) {

}

bool LightStorage::light_directional_get_blend_splits(RID p_light) const {
    return false;
}

void LightStorage::light_directional_set_sky_mode(RID p_light, RS::LightDirectionalSkyMode p_mode) {
}

RS::LightDirectionalSkyMode LightStorage::light_directional_get_sky_mode(RID p_light) const {
    return RS::LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_AND_SKY;
}

RS::LightDirectionalShadowMode LightStorage::light_directional_get_shadow_mode(RID p_light) {
    return RS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL;
}

RS::LightBakeMode LightStorage::light_get_bake_mode(RID p_light) {
    return RS::LIGHT_BAKE_DYNAMIC;
}

uint64_t LightStorage::light_get_version(RID p_light) const {
    return 0;
}

uint32_t LightStorage::light_get_cull_mask(RID p_light) const {
    return 0;
}

AABB LightStorage::light_get_aabb(RID p_light) const {
    return AABB();
}

/* LIGHT INSTANCE API */

RID LightStorage::light_instance_create(RID p_light) {
    return RID();
}

void LightStorage::light_instance_free(RID p_light_instance) {
}

void LightStorage::light_instance_set_transform(RID p_light_instance, const Transform3D &p_transform) {
}

void LightStorage::light_instance_set_aabb(RID p_light_instance, const AABB &p_aabb) {
}

void LightStorage::light_instance_set_shadow_transform(RID p_light_instance, const Projection &p_projection, const Transform3D &p_transform, float p_far, float p_split, int p_pass, float p_shadow_texel_size, float p_bias_scale, float p_range_begin, const Vector2 &p_uv_scale) {
}

void LightStorage::light_instance_mark_visible(RID p_light_instance) {
}

/* PROBE API */

RID LightStorage::reflection_probe_allocate() {
    return RID();
}

void LightStorage::reflection_probe_initialize(RID p_rid) {
}

void LightStorage::reflection_probe_free(RID p_rid) {
}

void LightStorage::reflection_probe_set_update_mode(RID p_probe, RS::ReflectionProbeUpdateMode p_mode) {
}

void LightStorage::reflection_probe_set_intensity(RID p_probe, float p_intensity) {
}

void LightStorage::reflection_probe_set_blend_distance(RID p_probe, float p_blend_distance) {
}

void LightStorage::reflection_probe_set_ambient_mode(RID p_probe, RS::ReflectionProbeAmbientMode p_mode) {
}

void LightStorage::reflection_probe_set_ambient_color(RID p_probe, const Color &p_color) {
}

void LightStorage::reflection_probe_set_ambient_energy(RID p_probe, float p_energy) {
}

void LightStorage::reflection_probe_set_max_distance(RID p_probe, float p_distance) {
}

void LightStorage::reflection_probe_set_size(RID p_probe, const Vector3 &p_size) {
}

void LightStorage::reflection_probe_set_origin_offset(RID p_probe, const Vector3 &p_offset) {
}

void LightStorage::reflection_probe_set_as_interior(RID p_probe, bool p_enable) {
}

void LightStorage::reflection_probe_set_enable_box_projection(RID p_probe, bool p_enable) {
}

void LightStorage::reflection_probe_set_enable_shadows(RID p_probe, bool p_enable) {
}

void LightStorage::reflection_probe_set_cull_mask(RID p_probe, uint32_t p_layers) {
}

void LightStorage::reflection_probe_set_reflection_mask(RID p_probe, uint32_t p_layers) {
}

void LightStorage::reflection_probe_set_resolution(RID p_probe, int p_resolution) {
}

AABB LightStorage::reflection_probe_get_aabb(RID p_probe) const {
    return AABB();
}

RS::ReflectionProbeUpdateMode LightStorage::reflection_probe_get_update_mode(RID p_probe) const {
    return RS::REFLECTION_PROBE_UPDATE_ONCE;
}

uint32_t LightStorage::reflection_probe_get_cull_mask(RID p_probe) const {
    return 0;
}

uint32_t LightStorage::reflection_probe_get_reflection_mask(RID p_probe) const {
    return 0;
}

Vector3 LightStorage::reflection_probe_get_size(RID p_probe) const {
    return Vector3();
}

Vector3 LightStorage::reflection_probe_get_origin_offset(RID p_probe) const {
    return Vector3();
}

float LightStorage::reflection_probe_get_origin_max_distance(RID p_probe) const {
    return 0.0;
}

bool LightStorage::reflection_probe_renders_shadows(RID p_probe) const {
    return false;
}

void LightStorage::reflection_probe_set_mesh_lod_threshold(RID p_probe, float p_ratio) {

}

float LightStorage::reflection_probe_get_mesh_lod_threshold(RID p_probe) const {
    return 0.0;
}

Dependency *LightStorage::reflection_probe_get_dependency(RID p_probe) const {
    return nullptr;
}

/* REFLECTION ATLAS */

RID LightStorage::reflection_atlas_create() {
    return RID();
}

void LightStorage::reflection_atlas_free(RID p_ref_atlas) {

}

int LightStorage::reflection_atlas_get_size(RID p_ref_atlas) const {
    return 0;
}

void LightStorage::reflection_atlas_set_size(RID p_ref_atlas, int p_reflection_size, int p_reflection_count) {
    
}

/* REFLECTION PROBE INSTANCE */

RID LightStorage::reflection_probe_instance_create(RID p_probe) {
    return RID();
}

void LightStorage::reflection_probe_instance_free(RID p_instance) {
    
}

void LightStorage::reflection_probe_instance_set_transform(RID p_instance, const Transform3D &p_transform) {

}

bool LightStorage::reflection_probe_has_atlas_index(RID p_instance) {
    return 0;
}

void LightStorage::reflection_probe_release_atlas_index(RID p_instance) {

}

bool LightStorage::reflection_probe_instance_needs_redraw(RID p_instance) {
    return false;
}

bool LightStorage::reflection_probe_instance_has_reflection(RID p_instance) {
    return false;
}

bool LightStorage::reflection_probe_instance_begin_render(RID p_instance, RID p_reflection_atlas) {
    return false;
}

Ref<RenderSceneBuffers> LightStorage::reflection_probe_atlas_get_render_buffers(RID p_reflection_atlas) {
    return Ref<RenderSceneBuffers>();
}

bool LightStorage::reflection_probe_instance_postprocess_step(RID p_instance) {
    return false;
}

GLuint LightStorage::reflection_probe_instance_get_texture(RID p_instance) {
    return static_cast<GLuint>(0);
}

GLuint LightStorage::reflection_probe_instance_get_framebuffer(RID p_instance, int p_index) {
    return static_cast<GLuint>(0);
}

/* LIGHTMAP CAPTURE */

RID LightStorage::lightmap_allocate() {
    return RID();
}

void LightStorage::lightmap_initialize(RID p_rid) {
}

void LightStorage::lightmap_free(RID p_rid) {

}

void LightStorage::lightmap_set_textures(RID p_lightmap, RID p_light, bool p_uses_spherical_haromics) {

}

void LightStorage::lightmap_set_probe_bounds(RID p_lightmap, const AABB &p_bounds) {

}

void LightStorage::lightmap_set_probe_interior(RID p_lightmap, bool p_interior) {

}

void LightStorage::lightmap_set_probe_capture_data(RID p_lightmap, const PackedVector3Array &p_points, const PackedColorArray &p_point_sh, const PackedInt32Array &p_tetrahedra, const PackedInt32Array &p_bsp_tree) {

}

void LightStorage::lightmap_set_baked_exposure_normalization(RID p_lightmap, float p_exposure) {

}

PackedVector3Array LightStorage::lightmap_get_probe_capture_points(RID p_lightmap) const {
	return PackedVector3Array();
}

PackedColorArray LightStorage::lightmap_get_probe_capture_sh(RID p_lightmap) const {
    return PackedColorArray();
}

PackedInt32Array LightStorage::lightmap_get_probe_capture_tetrahedra(RID p_lightmap) const {
    return PackedInt32Array();
}

PackedInt32Array LightStorage::lightmap_get_probe_capture_bsp_tree(RID p_lightmap) const {
    return PackedInt32Array();
}

AABB LightStorage::lightmap_get_aabb(RID p_lightmap) const {
    return AABB();
}

void LightStorage::lightmap_tap_sh_light(RID p_lightmap, const Vector3 &p_point, Color *r_sh) {

}

bool LightStorage::lightmap_is_interior(RID p_lightmap) const {

}

void LightStorage::lightmap_set_probe_capture_update_speed(float p_speed) {

}

float LightStorage::lightmap_get_probe_capture_update_speed() const {

}

void LightStorage::lightmap_set_shadowmask_textures(RID p_lightmap, RID p_shadow) {

}

RS::ShadowmaskMode LightStorage::lightmap_get_shadowmask_mode(RID p_lightmap) {

}

void LightStorage::lightmap_set_shadowmask_mode(RID p_lightmap, RS::ShadowmaskMode p_mode) {

}

/* LIGHTMAP INSTANCE */

RID LightStorage::lightmap_instance_create(RID p_lightmap) {

}

void LightStorage::lightmap_instance_free(RID p_lightmap) {

}

void LightStorage::lightmap_instance_set_transform(RID p_lightmap, const Transform3D &p_transform) {

}

/* SHADOW ATLAS API */

RID LightStorage::shadow_atlas_create() {
	return RID();
}

void LightStorage::shadow_atlas_free(RID p_atlas) {

}

void LightStorage::shadow_atlas_set_size(RID p_atlas, int p_size, bool p_16_bits) {

}

void LightStorage::shadow_atlas_set_quadrant_subdivision(RID p_atlas, int p_quadrant, int p_subdivision) {

}

bool LightStorage::shadow_atlas_update_light(RID p_atlas, RID p_light_instance, float p_coverage, uint64_t p_light_version) {
	return false;
}

bool LightStorage::_shadow_atlas_find_shadow(ShadowAtlas *shadow_atlas, int *p_in_quadrants, int p_quadrant_count, int p_current_subdiv, uint64_t p_tick, bool is_omni, int &r_quadrant, int &r_shadow) {
    return false;
}

void LightStorage::_shadow_atlas_invalidate_shadow(ShadowAtlas::Quadrant::Shadow *p_shadow, RID p_atlas, ShadowAtlas *p_shadow_atlas, uint32_t p_quadrant, uint32_t p_shadow_idx) {

}

void LightStorage::shadow_atlas_update(RID p_atlas) {
	// Do nothing as there is no shadow atlas texture.
}

/* DIRECTIONAL SHADOW */

// Create if necessary and clear.
void LightStorage::update_directional_shadow_atlas() {

}

void LightStorage::directional_shadow_atlas_set_size(int p_size, bool p_16_bits) {

}

void LightStorage::set_directional_shadow_count(int p_count) {

}

static Rect2i _get_directional_shadow_rect(int p_size, int p_shadow_count, int p_shadow_index) {
    return Rect2i();
}

Rect2i LightStorage::get_directional_shadow_rect() {
	return _get_directional_shadow_rect(directional_shadow.size, directional_shadow.light_count, directional_shadow.current_light);
}

int LightStorage::get_directional_light_shadow_size(RID p_light_instance) {
	return 0;
}

#endif // !GLES2_ENABLED
