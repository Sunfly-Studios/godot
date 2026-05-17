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

#ifdef GLES1_ENABLED

#include "light_storage.h"
#include "drivers/gles1/rasterizer_gles1.h"
#include "drivers/gles1/rasterizer_scene_gles1.h"
#include "core/config/project_settings.h"
#include "texture_storage.h"

using namespace GLES1;

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
	Light light;
	light.type = p_type;

	light.param[RS::LIGHT_PARAM_ENERGY] = 1.0;
	light.param[RS::LIGHT_PARAM_INDIRECT_ENERGY] = 1.0;
	light.param[RS::LIGHT_PARAM_VOLUMETRIC_FOG_ENERGY] = 1.0;
	light.param[RS::LIGHT_PARAM_SPECULAR] = 0.5;
	light.param[RS::LIGHT_PARAM_RANGE] = 1.0;
	light.param[RS::LIGHT_PARAM_SIZE] = 0.0;
	light.param[RS::LIGHT_PARAM_ATTENUATION] = 1.0;
	light.param[RS::LIGHT_PARAM_SPOT_ANGLE] = 45;
	light.param[RS::LIGHT_PARAM_SPOT_ATTENUATION] = 1.0;
	light.param[RS::LIGHT_PARAM_SHADOW_MAX_DISTANCE] = 0;
	light.param[RS::LIGHT_PARAM_SHADOW_SPLIT_1_OFFSET] = 0.1;
	light.param[RS::LIGHT_PARAM_SHADOW_SPLIT_2_OFFSET] = 0.3;
	light.param[RS::LIGHT_PARAM_SHADOW_SPLIT_3_OFFSET] = 0.6;
	light.param[RS::LIGHT_PARAM_SHADOW_FADE_START] = 0.8;
	light.param[RS::LIGHT_PARAM_SHADOW_NORMAL_BIAS] = 1.0;
	light.param[RS::LIGHT_PARAM_SHADOW_OPACITY] = 1.0;
	light.param[RS::LIGHT_PARAM_SHADOW_BIAS] = 0.02;
	light.param[RS::LIGHT_PARAM_SHADOW_BLUR] = 0;
	light.param[RS::LIGHT_PARAM_SHADOW_PANCAKE_SIZE] = 20.0;
	light.param[RS::LIGHT_PARAM_TRANSMITTANCE_BIAS] = 0.05;
	light.param[RS::LIGHT_PARAM_INTENSITY] = p_type == RS::LIGHT_DIRECTIONAL ? 100000.0 : 1000.0;

	light_owner.initialize_rid(p_light, light);
}

RID LightStorage::directional_light_allocate() {
	return light_owner.allocate_rid();
}

void LightStorage::directional_light_initialize(RID p_rid) {
	_light_initialize(p_rid, RS::LIGHT_DIRECTIONAL);
}

RID LightStorage::omni_light_allocate() {
	return light_owner.allocate_rid();
}

void LightStorage::omni_light_initialize(RID p_rid) {
	_light_initialize(p_rid, RS::LIGHT_OMNI);
}

RID LightStorage::spot_light_allocate() {
	return light_owner.allocate_rid();
}

void LightStorage::spot_light_initialize(RID p_rid) {
	_light_initialize(p_rid, RS::LIGHT_SPOT);
}

void LightStorage::light_free(RID p_rid) {
	light_set_projector(p_rid, RID()); //clear projector

	// delete the texture
	Light *light = light_owner.get_or_null(p_rid);
	light->dependency.deleted_notify(p_rid);
	light_owner.free(p_rid);
}

void LightStorage::light_set_color(RID p_light, const Color &p_color) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->color = p_color;
}

void LightStorage::light_set_param(RID p_light, RS::LightParam p_param, float p_value) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);
	ERR_FAIL_INDEX(p_param, RS::LIGHT_PARAM_MAX);

	if (light->param[p_param] == p_value) {
		return;
	}

	switch (p_param) {
		case RS::LIGHT_PARAM_RANGE:
		case RS::LIGHT_PARAM_SPOT_ANGLE:
		case RS::LIGHT_PARAM_SHADOW_MAX_DISTANCE:
		case RS::LIGHT_PARAM_SHADOW_SPLIT_1_OFFSET:
		case RS::LIGHT_PARAM_SHADOW_SPLIT_2_OFFSET:
		case RS::LIGHT_PARAM_SHADOW_SPLIT_3_OFFSET:
		case RS::LIGHT_PARAM_SHADOW_NORMAL_BIAS:
		case RS::LIGHT_PARAM_SHADOW_PANCAKE_SIZE:
		case RS::LIGHT_PARAM_SHADOW_BIAS: {
			light->version++;
			light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
		} break;
		case RS::LIGHT_PARAM_SIZE: {
			if ((light->param[p_param] > CMP_EPSILON) != (p_value > CMP_EPSILON)) {
				//changing from no size to size and the opposite
				light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT_SOFT_SHADOW_AND_PROJECTOR);
			}
		} break;
		default: {
		}
	}

	light->param[p_param] = p_value;
}

void LightStorage::light_set_shadow(RID p_light, bool p_enabled) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);
	light->shadow = p_enabled;

	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

void LightStorage::light_set_projector(RID p_light, RID p_texture) {
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	if (light->projector == p_texture) {
		return;
	}

	if (light->type != RS::LIGHT_DIRECTIONAL && light->projector.is_valid()) {
		texture_storage->texture_remove_from_decal_atlas(light->projector, light->type == RS::LIGHT_OMNI);
	}

	light->projector = p_texture;

	if (light->type != RS::LIGHT_DIRECTIONAL) {
		if (light->projector.is_valid()) {
			texture_storage->texture_add_to_decal_atlas(light->projector, light->type == RS::LIGHT_OMNI);
		}
		light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT_SOFT_SHADOW_AND_PROJECTOR);
	}
}

void LightStorage::light_set_negative(RID p_light, bool p_enable) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->negative = p_enable;
}

void LightStorage::light_set_cull_mask(RID p_light, uint32_t p_mask) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->cull_mask = p_mask;

	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

void LightStorage::light_set_shadow_caster_mask(RID p_light, uint32_t p_caster_mask) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->shadow_caster_mask = p_caster_mask;

	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

uint32_t LightStorage::light_get_shadow_caster_mask(RID p_light) const {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, 0);

	return light->shadow_caster_mask;
}

void LightStorage::light_set_distance_fade(RID p_light, bool p_enabled, float p_begin, float p_shadow, float p_length) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->distance_fade = p_enabled;
	light->distance_fade_begin = p_begin;
	light->distance_fade_shadow = p_shadow;
	light->distance_fade_length = p_length;
}

void LightStorage::light_set_reverse_cull_face_mode(RID p_light, bool p_enabled) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->reverse_cull = p_enabled;

	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

void LightStorage::light_set_bake_mode(RID p_light, RS::LightBakeMode p_bake_mode) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->bake_mode = p_bake_mode;

	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

void LightStorage::light_omni_set_shadow_mode(RID p_light, RS::LightOmniShadowMode p_mode) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->omni_shadow_mode = p_mode;

	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

RS::LightOmniShadowMode LightStorage::light_omni_get_shadow_mode(RID p_light) {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, RS::LIGHT_OMNI_SHADOW_CUBE);

	return light->omni_shadow_mode;
}

void LightStorage::light_directional_set_shadow_mode(RID p_light, RS::LightDirectionalShadowMode p_mode) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->directional_shadow_mode = p_mode;
	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

void LightStorage::light_directional_set_blend_splits(RID p_light, bool p_enable) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->directional_blend_splits = p_enable;
	light->version++;
	light->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
}

bool LightStorage::light_directional_get_blend_splits(RID p_light) const {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, false);

	return light->directional_blend_splits;
}

void LightStorage::light_directional_set_sky_mode(RID p_light, RS::LightDirectionalSkyMode p_mode) {
	Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(light);

	light->directional_sky_mode = p_mode;
}

RS::LightDirectionalSkyMode LightStorage::light_directional_get_sky_mode(RID p_light) const {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, RS::LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_AND_SKY);

	return light->directional_sky_mode;
}

RS::LightDirectionalShadowMode LightStorage::light_directional_get_shadow_mode(RID p_light) {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, RS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL);

	return light->directional_shadow_mode;
}

RS::LightBakeMode LightStorage::light_get_bake_mode(RID p_light) {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, RS::LIGHT_BAKE_DISABLED);

	return light->bake_mode;
}

uint64_t LightStorage::light_get_version(RID p_light) const {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, 0);

	return light->version;
}

uint32_t LightStorage::light_get_cull_mask(RID p_light) const {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, 0);

	return light->cull_mask;
}

AABB LightStorage::light_get_aabb(RID p_light) const {
	const Light *light = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(light, AABB());

	switch (light->type) {
		case RS::LIGHT_SPOT: {
			float len = light->param[RS::LIGHT_PARAM_RANGE];
			float size = Math::tan(Math::deg_to_rad(light->param[RS::LIGHT_PARAM_SPOT_ANGLE])) * len;
			return AABB(Vector3(-size, -size, -len), Vector3(size * 2, size * 2, len));
		};
		case RS::LIGHT_OMNI: {
			float r = light->param[RS::LIGHT_PARAM_RANGE];
			return AABB(-Vector3(r, r, r), Vector3(r, r, r) * 2);
		};
		case RS::LIGHT_DIRECTIONAL: {
			return AABB();
		};
	}

	ERR_FAIL_V(AABB());
}

/* LIGHT INSTANCE API */

RID LightStorage::light_instance_create(RID p_light) {
	RID li = light_instance_owner.make_rid(LightInstance());

	LightInstance *light_instance = light_instance_owner.get_or_null(li);

	light_instance->self = li;
	light_instance->light = p_light;
	light_instance->light_type = light_get_type(p_light);

	return li;
}

void LightStorage::light_instance_free(RID p_light_instance) {
	LightInstance *light_instance = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(light_instance);

	// Remove from shadow atlases.
	for (const RID &E : light_instance->shadow_atlases) {
		ShadowAtlas *shadow_atlas = shadow_atlas_owner.get_or_null(E);
		ERR_CONTINUE(!shadow_atlas->shadow_owners.has(p_light_instance));
		uint32_t key = shadow_atlas->shadow_owners[p_light_instance];
		uint32_t q = (key >> QUADRANT_SHIFT) & 0x3;
		uint32_t s = key & SHADOW_INDEX_MASK;

		shadow_atlas->quadrants[q].shadows.write[s].owner = RID();

		shadow_atlas->shadow_owners.erase(p_light_instance);
	}

	light_instance_owner.free(p_light_instance);
}

void LightStorage::light_instance_set_transform(RID p_light_instance, const Transform3D &p_transform) {
	LightInstance *light_instance = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(light_instance);

	light_instance->transform = p_transform;
}

void LightStorage::light_instance_set_aabb(RID p_light_instance, const AABB &p_aabb) {
	LightInstance *light_instance = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(light_instance);

	light_instance->aabb = p_aabb;
}

void LightStorage::light_instance_set_shadow_transform(RID p_light_instance, const Projection &p_projection, const Transform3D &p_transform, float p_far, float p_split, int p_pass, float p_shadow_texel_size, float p_bias_scale, float p_range_begin, const Vector2 &p_uv_scale) {
	LightInstance *light_instance = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(light_instance);

	ERR_FAIL_INDEX(p_pass, 6);

	light_instance->shadow_transform[p_pass].camera = p_projection;
	light_instance->shadow_transform[p_pass].transform = p_transform;
	light_instance->shadow_transform[p_pass].farplane = p_far;
	light_instance->shadow_transform[p_pass].split = p_split;
	light_instance->shadow_transform[p_pass].bias_scale = p_bias_scale;
	light_instance->shadow_transform[p_pass].range_begin = p_range_begin;
	light_instance->shadow_transform[p_pass].shadow_texel_size = p_shadow_texel_size;
	light_instance->shadow_transform[p_pass].uv_scale = p_uv_scale;
}

void LightStorage::light_instance_mark_visible(RID p_light_instance) {
	LightInstance *light_instance = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(light_instance);

	light_instance->last_scene_pass = RasterizerSceneGLES1::get_singleton()->get_scene_pass();
}

/* PROBE API */

RID LightStorage::reflection_probe_allocate() {
	return reflection_probe_owner.allocate_rid();
}

void LightStorage::reflection_probe_initialize(RID p_rid) {
	ReflectionProbe probe;

	reflection_probe_owner.initialize_rid(p_rid, probe);
}

void LightStorage::reflection_probe_free(RID p_rid) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_rid);
	reflection_probe->dependency.deleted_notify(p_rid);

	reflection_probe_owner.free(p_rid);
}

void LightStorage::reflection_probe_set_update_mode(RID p_probe, RS::ReflectionProbeUpdateMode p_mode) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->update_mode = p_mode;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_intensity(RID p_probe, float p_intensity) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->intensity = p_intensity;
}

void LightStorage::reflection_probe_set_blend_distance(RID p_probe, float p_blend_distance) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->blend_distance = p_blend_distance;
}

void LightStorage::reflection_probe_set_ambient_mode(RID p_probe, RS::ReflectionProbeAmbientMode p_mode) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->ambient_mode = p_mode;
}

void LightStorage::reflection_probe_set_ambient_color(RID p_probe, const Color &p_color) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->ambient_color = p_color;
}

void LightStorage::reflection_probe_set_ambient_energy(RID p_probe, float p_energy) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->ambient_color_energy = p_energy;
}

void LightStorage::reflection_probe_set_max_distance(RID p_probe, float p_distance) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->max_distance = p_distance;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_size(RID p_probe, const Vector3 &p_size) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->size = p_size;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_origin_offset(RID p_probe, const Vector3 &p_offset) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->origin_offset = p_offset;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_as_interior(RID p_probe, bool p_enable) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->interior = p_enable;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_enable_box_projection(RID p_probe, bool p_enable) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->box_projection = p_enable;
}

void LightStorage::reflection_probe_set_enable_shadows(RID p_probe, bool p_enable) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->enable_shadows = p_enable;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_cull_mask(RID p_probe, uint32_t p_layers) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->cull_mask = p_layers;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_reflection_mask(RID p_probe, uint32_t p_layers) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->reflection_mask = p_layers;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

void LightStorage::reflection_probe_set_resolution(RID p_probe, int p_resolution) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->resolution = p_resolution;
}

AABB LightStorage::reflection_probe_get_aabb(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, AABB());

	AABB aabb;
	aabb.position = -reflection_probe->size / 2;
	aabb.size = reflection_probe->size;

	return aabb;
}

RS::ReflectionProbeUpdateMode LightStorage::reflection_probe_get_update_mode(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, RenderingServer::REFLECTION_PROBE_UPDATE_ONCE);

	return reflection_probe->update_mode;
}

uint32_t LightStorage::reflection_probe_get_cull_mask(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, 0);

	return reflection_probe->cull_mask;
}

uint32_t LightStorage::reflection_probe_get_reflection_mask(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, 0);

	return reflection_probe->reflection_mask;
}

Vector3 LightStorage::reflection_probe_get_size(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, Vector3());

	return reflection_probe->size;
}

Vector3 LightStorage::reflection_probe_get_origin_offset(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, Vector3());

	return reflection_probe->origin_offset;
}

float LightStorage::reflection_probe_get_origin_max_distance(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, 0.0);

	return reflection_probe->max_distance;
}

bool LightStorage::reflection_probe_renders_shadows(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, false);

	return reflection_probe->enable_shadows;
}

void LightStorage::reflection_probe_set_mesh_lod_threshold(RID p_probe, float p_ratio) {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL(reflection_probe);

	reflection_probe->mesh_lod_threshold = p_ratio;
	reflection_probe->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_REFLECTION_PROBE);
}

float LightStorage::reflection_probe_get_mesh_lod_threshold(RID p_probe) const {
	const ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, 0.0);

	return reflection_probe->mesh_lod_threshold;
}

Dependency *LightStorage::reflection_probe_get_dependency(RID p_probe) const {
	ReflectionProbe *reflection_probe = reflection_probe_owner.get_or_null(p_probe);
	ERR_FAIL_NULL_V(reflection_probe, nullptr);

	return &reflection_probe->dependency;
}

/* REFLECTION ATLAS */

RID LightStorage::reflection_atlas_create() {
	ReflectionAtlas ra = {};
	ra.count = GLOBAL_GET("rendering/reflections/reflection_atlas/reflection_count");
	ra.size = GLOBAL_GET("rendering/reflections/reflection_atlas/reflection_size");

	return reflection_atlas_owner.make_rid(ra);
}

void LightStorage::reflection_atlas_free(RID p_ref_atlas) {
	reflection_atlas_set_size(p_ref_atlas, 0, 0);

	reflection_atlas_owner.free(p_ref_atlas);
}

int LightStorage::reflection_atlas_get_size(RID p_ref_atlas) const {
	ReflectionAtlas *ra = reflection_atlas_owner.get_or_null(p_ref_atlas);
	ERR_FAIL_NULL_V(ra, 0);

	return ra->size;
}

void LightStorage::reflection_atlas_set_size(RID p_ref_atlas, int p_reflection_size, int p_reflection_count) {
	ReflectionAtlas *ra = reflection_atlas_owner.get_or_null(p_ref_atlas);
	ERR_FAIL_NULL(ra);

	if (ra->size == p_reflection_size && ra->count == p_reflection_count) {
		return; //no changes
	}

	ra->size = p_reflection_size;
	ra->count = p_reflection_count;

	if (ra->depth != 0) {
		//clear and invalidate everything
		for (int i = 0; i < ra->reflections.size(); i++) {
			for (int j = 0; j < 7; j++) {
				if (ra->reflections[i].fbos[j] != 0) {
					glDeleteFramebuffersOES(1, &ra->reflections[i].fbos[j]);
					ra->reflections.write[i].fbos[j] = 0;
				}
			}

			GLES1::Utilities::get_singleton()->texture_free_data(ra->reflections[i].color);
			ra->reflections.write[i].color = 0;

			GLES1::Utilities::get_singleton()->texture_free_data(ra->reflections[i].radiance);
			ra->reflections.write[i].radiance = 0;

			if (ra->reflections[i].owner.is_null()) {
				continue;
			}
			reflection_probe_release_atlas_index(ra->reflections[i].owner);
			//rp->atlasindex clear
		}

		ra->reflections.clear();

		GLES1::Utilities::get_singleton()->texture_free_data(ra->depth);
		ra->depth = 0;
	}

	if (ra->render_buffers.is_valid()) {
		ra->render_buffers->free_render_buffer_data();
	}
}

/* REFLECTION PROBE INSTANCE */

RID LightStorage::reflection_probe_instance_create(RID p_probe) {
	ReflectionProbeInstance rpi;
	rpi.probe = p_probe;

	return reflection_probe_instance_owner.make_rid(rpi);
}

void LightStorage::reflection_probe_instance_free(RID p_instance) {
	reflection_probe_release_atlas_index(p_instance);
	reflection_probe_instance_owner.free(p_instance);
}

void LightStorage::reflection_probe_instance_set_transform(RID p_instance, const Transform3D &p_transform) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL(rpi);

	rpi->transform = p_transform;
	rpi->dirty = true;
}

bool LightStorage::reflection_probe_has_atlas_index(RID p_instance) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL_V(rpi, false);

	if (rpi->atlas.is_null()) {
		return false;
	}

	return rpi->atlas_index >= 0;
}

void LightStorage::reflection_probe_release_atlas_index(RID p_instance) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL(rpi);

	if (rpi->atlas.is_null()) {
		return; //nothing to release
	}
	ReflectionAtlas *atlas = reflection_atlas_owner.get_or_null(rpi->atlas);
	ERR_FAIL_NULL(atlas);

	ERR_FAIL_INDEX(rpi->atlas_index, atlas->reflections.size());
	atlas->reflections.write[rpi->atlas_index].owner = RID();

	if (rpi->rendering) {
		// We were cancelled mid rendering, trigger refresh.
		rpi->rendering = false;
		rpi->dirty = true;
		rpi->processing_layer = 0;
	}

	rpi->atlas_index = -1;
	rpi->atlas = RID();
}

bool LightStorage::reflection_probe_instance_needs_redraw(RID p_instance) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL_V(rpi, false);

	if (rpi->rendering) {
		return false;
	}

	if (rpi->dirty) {
		return true;
	}

	if (reflection_probe_get_update_mode(rpi->probe) == RS::REFLECTION_PROBE_UPDATE_ALWAYS) {
		return true;
	}

	return rpi->atlas_index == -1;
}

bool LightStorage::reflection_probe_instance_has_reflection(RID p_instance) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL_V(rpi, false);

	return rpi->atlas.is_valid();
}

bool LightStorage::reflection_probe_instance_begin_render(RID p_instance, RID p_reflection_atlas) {
	// TODO(GLES1): Port the GLES2 code.
    return false;
}

Ref<RenderSceneBuffers> LightStorage::reflection_probe_atlas_get_render_buffers(RID p_reflection_atlas) {
	ReflectionAtlas *atlas = reflection_atlas_owner.get_or_null(p_reflection_atlas);
	ERR_FAIL_NULL_V(atlas, Ref<RenderSceneBuffersGLES1>());

	return atlas->render_buffers;
}

bool LightStorage::reflection_probe_instance_postprocess_step(RID p_instance) {
	// TODO(GLES1): Port the GLES2 code.
    return false;
}

GLuint LightStorage::reflection_probe_instance_get_texture(RID p_instance) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL_V(rpi, 0);

	ReflectionAtlas *atlas = reflection_atlas_owner.get_or_null(rpi->atlas);
	ERR_FAIL_NULL_V(atlas, 0);

	return atlas->reflections[rpi->atlas_index].radiance;
}

GLuint LightStorage::reflection_probe_instance_get_framebuffer(RID p_instance, int p_index) {
	ReflectionProbeInstance *rpi = reflection_probe_instance_owner.get_or_null(p_instance);
	ERR_FAIL_NULL_V(rpi, 0);
	ERR_FAIL_INDEX_V(p_index, 6, 0);

	ReflectionAtlas *atlas = reflection_atlas_owner.get_or_null(rpi->atlas);
	ERR_FAIL_NULL_V(atlas, 0);
	return atlas->reflections[rpi->atlas_index].fbos[p_index];
}

/* LIGHTMAP CAPTURE */

RID LightStorage::lightmap_allocate() {
	return lightmap_owner.allocate_rid();
}

void LightStorage::lightmap_initialize(RID p_rid) {
	lightmap_owner.initialize_rid(p_rid, Lightmap());
}

void LightStorage::lightmap_free(RID p_rid) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(lightmap);
	lightmap->dependency.deleted_notify(p_rid);
	lightmap_owner.free(p_rid);
}

void LightStorage::lightmap_set_textures(RID p_lightmap, RID p_light, bool p_uses_spherical_haromics) {
	// TODO(GLES1): Port the GLES2 code.
}

void LightStorage::lightmap_set_probe_bounds(RID p_lightmap, const AABB &p_bounds) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lightmap);
	lightmap->bounds = p_bounds;
}

void LightStorage::lightmap_set_probe_interior(RID p_lightmap, bool p_interior) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lightmap);
	lightmap->interior = p_interior;
}

void LightStorage::lightmap_set_probe_capture_data(RID p_lightmap, const PackedVector3Array &p_points, const PackedColorArray &p_point_sh, const PackedInt32Array &p_tetrahedra, const PackedInt32Array &p_bsp_tree) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lightmap);

	if (p_points.size()) {
		ERR_FAIL_COND(p_points.size() * 9 != p_point_sh.size());
		ERR_FAIL_COND((p_tetrahedra.size() % 4) != 0);
		ERR_FAIL_COND((p_bsp_tree.size() % 6) != 0);
	}

	lightmap->points = p_points;
	lightmap->point_sh = p_point_sh;
	lightmap->tetrahedra = p_tetrahedra;
	lightmap->bsp_tree = p_bsp_tree;
}

void LightStorage::lightmap_set_baked_exposure_normalization(RID p_lightmap, float p_exposure) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lightmap);

	lightmap->baked_exposure = p_exposure;
}

PackedVector3Array LightStorage::lightmap_get_probe_capture_points(RID p_lightmap) const {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, PackedVector3Array());
	return lightmap->points;
}

PackedColorArray LightStorage::lightmap_get_probe_capture_sh(RID p_lightmap) const {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, PackedColorArray());
	return lightmap->point_sh;
}

PackedInt32Array LightStorage::lightmap_get_probe_capture_tetrahedra(RID p_lightmap) const {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, PackedInt32Array());
	return lightmap->tetrahedra;
}

PackedInt32Array LightStorage::lightmap_get_probe_capture_bsp_tree(RID p_lightmap) const {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, PackedInt32Array());
	return lightmap->bsp_tree;
}

AABB LightStorage::lightmap_get_aabb(RID p_lightmap) const {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, AABB());
	return lightmap->bounds;
}

void LightStorage::lightmap_tap_sh_light(RID p_lightmap, const Vector3 &p_point, Color *r_sh) {
	Lightmap *lm = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lm);

	for (int i = 0; i < 9; i++) {
		r_sh[i] = Color(0, 0, 0, 0);
	}

	if (!lm->points.size() || !lm->bsp_tree.size() || !lm->tetrahedra.size()) {
		return;
	}

	static_assert(sizeof(Lightmap::BSP) == 24);

	// TODO(GLES2): Update to C++17 static/const cast
	const Lightmap::BSP *bsp = (const Lightmap::BSP *)lm->bsp_tree.ptr();
	int32_t node = 0;
	while (node >= 0) {
		if (Plane(bsp[node].plane[0], bsp[node].plane[1], bsp[node].plane[2], bsp[node].plane[3]).is_point_over(p_point)) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_COND(bsp[node].over >= 0 && bsp[node].over < node);
#endif

			node = bsp[node].over;
		} else {
#ifdef DEBUG_ENABLED
			ERR_FAIL_COND(bsp[node].under >= 0 && bsp[node].under < node);
#endif
			node = bsp[node].under;
		}
	}

	if (node == Lightmap::BSP::EMPTY_LEAF) {
		return; // Nothing could be done.
	}

	node = ABS(node) - 1;

	uint32_t *tetrahedron = unaligned_read<uint32_t*>(&lm->tetrahedra[node * 4]);
	Vector3 points[4] = { lm->points[tetrahedron[0]], lm->points[tetrahedron[1]], lm->points[tetrahedron[2]], lm->points[tetrahedron[3]] };
	const Color *sh_colors[4]{ &lm->point_sh[tetrahedron[0] * 9], &lm->point_sh[tetrahedron[1] * 9], &lm->point_sh[tetrahedron[2] * 9], &lm->point_sh[tetrahedron[3] * 9] };
	Color barycentric = Geometry3D::tetrahedron_get_barycentric_coords(points[0], points[1], points[2], points[3], p_point);

	for (int i = 0; i < 4; i++) {
		float c = CLAMP(barycentric[i], 0.0, 1.0);
		for (int j = 0; j < 9; j++) {
			r_sh[j] += sh_colors[i][j] * c;
		}
	}
}

bool LightStorage::lightmap_is_interior(RID p_lightmap) const {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, false);
	return lightmap->interior;
}

void LightStorage::lightmap_set_probe_capture_update_speed(float p_speed) {
	// lightmap_probe_capture_update_speed = p_speed;
}

float LightStorage::lightmap_get_probe_capture_update_speed() const {
	//return lightmap_probe_capture_update_speed;
    return 0.0f;
}

void LightStorage::lightmap_set_shadowmask_textures(RID p_lightmap, RID p_shadow) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lightmap);
	lightmap->shadow_texture = p_shadow;

	// TODO(GLES1): Port the GLES2 code.
}

RS::ShadowmaskMode LightStorage::lightmap_get_shadowmask_mode(RID p_lightmap) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL_V(lightmap, RS::SHADOWMASK_MODE_NONE);

	return lightmap->shadowmask_mode;
}

void LightStorage::lightmap_set_shadowmask_mode(RID p_lightmap, RS::ShadowmaskMode p_mode) {
	Lightmap *lightmap = lightmap_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(lightmap);
	lightmap->shadowmask_mode = p_mode;
}

/* LIGHTMAP INSTANCE */

RID LightStorage::lightmap_instance_create(RID p_lightmap) {
	LightmapInstance li;
	li.lightmap = p_lightmap;
	return lightmap_instance_owner.make_rid(li);
}

void LightStorage::lightmap_instance_free(RID p_lightmap) {
	lightmap_instance_owner.free(p_lightmap);
}

void LightStorage::lightmap_instance_set_transform(RID p_lightmap, const Transform3D &p_transform) {
	LightmapInstance *li = lightmap_instance_owner.get_or_null(p_lightmap);
	ERR_FAIL_NULL(li);
	li->transform = p_transform;
}

/* SHADOW ATLAS API */

RID LightStorage::shadow_atlas_create() {
	return shadow_atlas_owner.make_rid(ShadowAtlas());
}

void LightStorage::shadow_atlas_free(RID p_atlas) {
	shadow_atlas_set_size(p_atlas, 0);
	shadow_atlas_owner.free(p_atlas);
}

void LightStorage::shadow_atlas_set_size(RID p_atlas, int p_size, bool p_16_bits) {
	// TODO(GLES1): Port the GLES2 code.
}

void LightStorage::shadow_atlas_set_quadrant_subdivision(RID p_atlas, int p_quadrant, int p_subdivision) {
	// TODO(GLES1): Port the GLES2 code.
}

bool LightStorage::shadow_atlas_update_light(RID p_atlas, RID p_light_instance, float p_coverage, uint64_t p_light_version) {
	// TODO(GLES1): Port the GLES2 code.
	return false;
}

bool LightStorage::_shadow_atlas_find_shadow(ShadowAtlas *shadow_atlas, int *p_in_quadrants, int p_quadrant_count, int p_current_subdiv, uint64_t p_tick, bool is_omni, int &r_quadrant, int &r_shadow) {
	// TODO(GLES1): Port the GLES2 code.
	return false;
}

void LightStorage::_shadow_atlas_invalidate_shadow(ShadowAtlas::Quadrant::Shadow *p_shadow, RID p_atlas, ShadowAtlas *p_shadow_atlas, uint32_t p_quadrant, uint32_t p_shadow_idx) {
	if (p_shadow->owner.is_valid()) {
		LightInstance *sli = light_instance_owner.get_or_null(p_shadow->owner);

		p_shadow_atlas->shadow_owners.erase(p_shadow->owner);
		p_shadow->version = 0;
		p_shadow->owner = RID();
		sli->shadow_atlases.erase(p_atlas);
	}
}

void LightStorage::shadow_atlas_update(RID p_atlas) {
	// Do nothing as there is no shadow atlas texture.
}

/* DIRECTIONAL SHADOW */

// Create if necessary and clear.
void LightStorage::update_directional_shadow_atlas() {
	// TODO(GLES1): Port the GLES2 code.
}

void LightStorage::directional_shadow_atlas_set_size(int p_size, bool p_16_bits) {
	p_size = nearest_power_of_2_templated(p_size);

	if (directional_shadow.size == p_size && directional_shadow.use_16_bits == p_16_bits) {
		return;
	}

	directional_shadow.size = p_size;
	directional_shadow.use_16_bits = p_16_bits;

	if (directional_shadow.depth != 0) {
		glDeleteTextures(1, &directional_shadow.depth);
		directional_shadow.depth = 0;
		glDeleteFramebuffersOES(1, &directional_shadow.fbo);
		directional_shadow.fbo = 0;
	}
}

void LightStorage::set_directional_shadow_count(int p_count) {
	directional_shadow.light_count = p_count;
	directional_shadow.current_light = 0;
}

static Rect2i _get_directional_shadow_rect(int p_size, int p_shadow_count, int p_shadow_index) {
	int split_h = 1;
	int split_v = 1;

	while (split_h * split_v < p_shadow_count) {
		if (split_h == split_v) {
			split_h <<= 1;
		} else {
			split_v <<= 1;
		}
	}

	Rect2i rect(0, 0, p_size, p_size);
	rect.size.width /= split_h;
	rect.size.height /= split_v;

	rect.position.x = rect.size.width * (p_shadow_index % split_h);
	rect.position.y = rect.size.height * (p_shadow_index / split_h);

	return rect;
}

Rect2i LightStorage::get_directional_shadow_rect() {
	return _get_directional_shadow_rect(directional_shadow.size, directional_shadow.light_count, directional_shadow.current_light);
}

int LightStorage::get_directional_light_shadow_size(RID p_light_instance) {
	ERR_FAIL_COND_V(directional_shadow.light_count == 0, 0);

	Rect2i r = _get_directional_shadow_rect(directional_shadow.size, directional_shadow.light_count, 0);

	LightInstance *light_instance = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL_V(light_instance, 0);

	switch (light_directional_get_shadow_mode(light_instance->light)) {
		case RS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL:
			break; //none
		case RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS:
			r.size.height /= 2;
			break;
		case RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_4_SPLITS:
			r.size /= 2;
			break;
	}

	return MAX(r.size.width, r.size.height);
}

#endif // GLES1_ENABLED
