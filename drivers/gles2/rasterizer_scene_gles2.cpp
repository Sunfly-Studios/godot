/**************************************************************************/
/*  rasterizer_scene_gles2.cpp                                            */
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

#include "rasterizer_scene_gles2.h"

#include "drivers/gles2/effects/copy_effects.h"
#include "drivers/gles2/effects/feed_effects.h"
#include "rasterizer_gles2.h"
#include "storage/config.h"
#include "storage/mesh_storage.h"
#include "storage/particles_storage.h"
#include "storage/texture_storage.h"

#include "core/config/project_settings.h"
#include "core/templates/sort_array.h"
#include "servers/camera/camera_feed.h"
#include "servers/camera_server.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"

#ifdef GLES2_ENABLED

RasterizerSceneGLES2 *RasterizerSceneGLES2::singleton = nullptr;

RenderGeometryInstance *RasterizerSceneGLES2::geometry_instance_create(RID p_base) {
	RS::InstanceType type = RSG::utilities->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RS::INSTANCE_GEOMETRY_MASK), nullptr);

	GeometryInstanceGLES2 *ginstance = geometry_instance_alloc.alloc();
	ginstance->data = memnew(GeometryInstanceGLES2::Data);

	ERR_FAIL_NULL_V(ginstance->data, nullptr);

	ginstance->data->base = p_base;
	ginstance->data->base_type = type;
	ginstance->data->dependency_tracker.userdata = ginstance;
	ginstance->data->dependency_tracker.changed_callback = _geometry_instance_dependency_changed;
	ginstance->data->dependency_tracker.deleted_callback = _geometry_instance_dependency_deleted;

	ginstance->_mark_dirty();

	return ginstance;
}

uint32_t RasterizerSceneGLES2::geometry_instance_get_pair_mask() {
	return ((1 << RS::INSTANCE_LIGHT) | (1 << RS::INSTANCE_REFLECTION_PROBE));
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::pair_light_instances(const RID *p_light_instances, uint32_t p_light_instance_count) {

}

void RasterizerSceneGLES2::GeometryInstanceGLES2::pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) {

}

void RasterizerSceneGLES2::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_geometry_instance);
	ERR_FAIL_NULL(ginstance);
	GeometryInstanceSurface *surf = ginstance->surface_caches;
	while (surf) {
		GeometryInstanceSurface *next = surf->next;
		geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	memdelete(ginstance->data);
	geometry_instance_alloc.free(ginstance);
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::_mark_dirty() {

}

void RasterizerSceneGLES2::GeometryInstanceGLES2::set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) {

}

void RasterizerSceneGLES2::GeometryInstanceGLES2::set_lightmap_capture(const Color *p_sh9) {

}

void RasterizerSceneGLES2::_update_dirty_geometry_instances() {

}

void RasterizerSceneGLES2::_geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker) {

}

void RasterizerSceneGLES2::_geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker) {

}

void RasterizerSceneGLES2::_geometry_instance_add_surface_with_material(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh) {

}

void RasterizerSceneGLES2::_geometry_instance_add_surface_with_material_chain(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material_data, RID p_mat_src, RID p_mesh) {

}

void RasterizerSceneGLES2::_geometry_instance_add_surface(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, RID p_material, RID p_mesh) {

}

void RasterizerSceneGLES2::_geometry_instance_update(RenderGeometryInstance *p_geometry_instance) {

}

/* SKY API */

void RasterizerSceneGLES2::_free_sky_data(Sky *p_sky) {

}

RID RasterizerSceneGLES2::sky_allocate() {
    return RID();
}

void RasterizerSceneGLES2::sky_initialize(RID p_rid) {

}

void RasterizerSceneGLES2::sky_set_radiance_size(RID p_sky, int p_radiance_size) {

}

void RasterizerSceneGLES2::sky_set_mode(RID p_sky, RS::SkyMode p_mode) {

}

void RasterizerSceneGLES2::sky_set_material(RID p_sky, RID p_material) {

}

float RasterizerSceneGLES2::sky_get_baked_exposure(RID p_sky) const {
    return 1.0f;
}

void RasterizerSceneGLES2::_invalidate_sky(Sky *p_sky) {

}

[[maybe_unused]] static GLuint _init_radiance_texture_gles2(int p_size, int p_mipmaps, String p_name) {
    return 0;
}

void RasterizerSceneGLES2::_update_dirty_skys() {

}

void RasterizerSceneGLES2::_setup_sky(const RenderDataGLES2 *p_render_data, const PagedArray<RID> &p_lights, const Projection &p_projection, const Transform3D &p_transform, const Size2i p_screen_size) {

}

void RasterizerSceneGLES2::_draw_sky(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier, float p_luminance_multiplier, bool p_use_multiview, bool p_flip_y, bool p_apply_color_adjustments_in_post) {

}

void RasterizerSceneGLES2::_update_sky_radiance(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier) {

}

Ref<Image> RasterizerSceneGLES2::sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) {
	return Ref<Image>();
}

/* ENVIRONMENT API */

void RasterizerSceneGLES2::environment_glow_set_use_bicubic_upscale(bool p_enable) {
	glow_bicubic_upscale = p_enable;
}

void RasterizerSceneGLES2::environment_set_ssr_roughness_quality(RS::EnvironmentSSRRoughnessQuality p_quality) {
}

void RasterizerSceneGLES2::environment_set_ssao_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ssao_quality = p_quality;
}

void RasterizerSceneGLES2::environment_set_ssil_quality(RS::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_ray_count(RS::EnvironmentSDFGIRayCount p_ray_count) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_frames_to_converge(RS::EnvironmentSDFGIFramesToConverge p_frames) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_frames_to_update_light(RS::EnvironmentSDFGIFramesToUpdateLight p_update) {
}

void RasterizerSceneGLES2::environment_set_volumetric_fog_volume_size(int p_size, int p_depth) {
}

void RasterizerSceneGLES2::environment_set_volumetric_fog_filter_active(bool p_enable) {
}

Ref<Image> RasterizerSceneGLES2::environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) {
	return Ref<Image>();
}

void RasterizerSceneGLES2::positional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.positional_shadow_quality = p_quality;
}

void RasterizerSceneGLES2::directional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.directional_shadow_quality = p_quality;
}

RID RasterizerSceneGLES2::fog_volume_instance_create(RID p_fog_volume) {
	return RID();
}

void RasterizerSceneGLES2::fog_volume_instance_set_transform(RID p_fog_volume_instance, const Transform3D &p_transform) {
}

void RasterizerSceneGLES2::fog_volume_instance_set_active(RID p_fog_volume_instance, bool p_active) {
}

RID RasterizerSceneGLES2::fog_volume_instance_get_volume(RID p_fog_volume_instance) const {
	return RID();
}

Vector3 RasterizerSceneGLES2::fog_volume_instance_get_position(RID p_fog_volume_instance) const {
	return Vector3();
}

RID RasterizerSceneGLES2::voxel_gi_instance_create(RID p_voxel_gi) {
	return RID();
}

void RasterizerSceneGLES2::voxel_gi_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform) {
}

bool RasterizerSceneGLES2::voxel_gi_needs_update(RID p_probe) const {
	return false;
}

void RasterizerSceneGLES2::voxel_gi_update(RID p_probe, bool p_update_light_instances, const Vector<RID> &p_light_instances, const PagedArray<RenderGeometryInstance *> &p_dynamic_objects) {
}

void RasterizerSceneGLES2::voxel_gi_set_quality(RS::VoxelGIQuality) {
}


void RasterizerSceneGLES2::_fill_render_list(RenderListType p_render_list, const RenderDataGLES2 *p_render_data, PassMode p_pass_mode, bool p_append) {

}

// Needs to be called after _setup_lights so that directional_light_count is accurate.
void RasterizerSceneGLES2::_setup_environment(const RenderDataGLES2 *p_render_data, bool p_no_fog, const Size2i &p_screen_size, bool p_flip_y, const Color &p_default_bg_color, bool p_pancake_shadows, float p_shadow_bias) {

}

// Puts lights into Uniform Buffers. Needs to be called before _fill_list as this caches the index of each light in the Uniform Buffer
void RasterizerSceneGLES2::_setup_lights(const RenderDataGLES2 *p_render_data, bool p_using_shadows, uint32_t &r_directional_light_count, uint32_t &r_omni_light_count, uint32_t &r_spot_light_count, uint32_t &r_directional_shadow_count) {

}

// Render shadows
void RasterizerSceneGLES2::_render_shadows(const RenderDataGLES2 *p_render_data, const Size2i &p_viewport_size) {

}

void RasterizerSceneGLES2::_render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, RenderingMethod::RenderInfo *p_render_info, const Size2i &p_viewport_size, const Transform3D &p_main_cam_transform) {

}

void RasterizerSceneGLES2::render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, const RenderSDFGIUpdateData *p_sdfgi_update_data, RenderingMethod::RenderInfo *r_render_info) {

}

void RasterizerSceneGLES2::_render_post_processing(const RenderDataGLES2 *p_render_data) {

}

template <RasterizerSceneGLES2::PassMode p_pass_mode>
void RasterizerSceneGLES2::_render_list_template(RenderListParameters *p_params, const RenderDataGLES2 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass) {

}

void RasterizerSceneGLES2::render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) {
}

void RasterizerSceneGLES2::render_particle_collider_heightfield(RID p_collider, const Transform3D &p_transform, const PagedArray<RenderGeometryInstance *> &p_instances) {

}

void RasterizerSceneGLES2::_render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, GLuint p_framebuffer, const Rect2i &p_region) {

}

void RasterizerSceneGLES2::set_time(double p_time, double p_step) {
	time = p_time;
	time_step = p_step;
}

void RasterizerSceneGLES2::set_debug_draw_mode(RS::ViewportDebugDraw p_debug_draw) {
	debug_draw = p_debug_draw;
}

Ref<RenderSceneBuffers> RasterizerSceneGLES2::render_buffers_create() {
	Ref<RenderSceneBuffersGLES2> rb;
	rb.instantiate();
	ERR_FAIL_COND_V(rb.is_null(), Ref<RenderSceneBuffers>());
	return rb;
}

void RasterizerSceneGLES2::_render_buffers_debug_draw(Ref<RenderSceneBuffersGLES2> p_render_buffers, RID p_shadow_atlas, GLuint p_fbo) {
}

void RasterizerSceneGLES2::gi_set_use_half_resolution(bool p_enable) {
}

void RasterizerSceneGLES2::screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_curve) {
}

bool RasterizerSceneGLES2::screen_space_roughness_limiter_is_active() const {
	return false;
}

void RasterizerSceneGLES2::sub_surface_scattering_set_quality(RS::SubSurfaceScatteringQuality p_quality) {
}

void RasterizerSceneGLES2::sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) {
}

TypedArray<Image> RasterizerSceneGLES2::bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) {
    return TypedArray<Image>();
}

bool RasterizerSceneGLES2::free(RID p_rid) {
	return true;
}

void RasterizerSceneGLES2::update() {
	_update_dirty_skys();
}

void RasterizerSceneGLES2::sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) {
}

void RasterizerSceneGLES2::decals_set_filter(RS::DecalFilter p_filter) {
}

void RasterizerSceneGLES2::light_projectors_set_filter(RS::LightProjectorFilter p_filter) {
}

void RasterizerSceneGLES2::lightmaps_set_bicubic_filter(bool p_enable) {
	lightmap_bicubic_upscale = p_enable;
}

RasterizerSceneGLES2::RasterizerSceneGLES2() {
	singleton = this;

	batch_constructor();
	batch_initialize();
}

RasterizerSceneGLES2::~RasterizerSceneGLES2() {
	singleton = nullptr;
}

#endif // GLES2_ENABLED
