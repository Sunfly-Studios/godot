/**************************************************************************/
/*  particles_storage.cpp                                                 */
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

#include "particles_storage.h"

#include "config.h"
#include "material_storage.h"
#include "mesh_storage.h"
#include "texture_storage.h"
#include "utilities.h"

#include "servers/rendering/rendering_server_globals.h"

using namespace GLES2;

ParticlesStorage *ParticlesStorage::singleton = nullptr;

ParticlesStorage *ParticlesStorage::get_singleton() {
	return singleton;
}

ParticlesStorage::ParticlesStorage() {
	singleton = this;
// 	GLES3::MaterialStorage *material_storage = GLES3::MaterialStorage::get_singleton();

// 	{
// 		String global_defines;
// 		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
// 		material_storage->shaders.particles_process_shader.initialize(global_defines, 1);
// 	}
// 	{
// 		// default material and shader for particles shader
// 		particles_shader.default_shader = material_storage->shader_allocate();
// 		material_storage->shader_initialize(particles_shader.default_shader);
// 		material_storage->shader_set_code(particles_shader.default_shader, R"(
// // Default particles shader.

// shader_type particles;

// void process() {
// 	COLOR = vec4(1.0);
// }
// )");
// 		particles_shader.default_material = material_storage->material_allocate();
// 		material_storage->material_initialize(particles_shader.default_material);
// 		material_storage->material_set_shader(particles_shader.default_material, particles_shader.default_shader);
// 	}
// 	{
// 		particles_shader.copy_shader.initialize();
// 		particles_shader.copy_shader_version = particles_shader.copy_shader.version_create();
// 	}
}

ParticlesStorage::~ParticlesStorage() {
	singleton = nullptr;
	// GLES3::MaterialStorage *material_storage = GLES3::MaterialStorage::get_singleton();

	// material_storage->material_free(particles_shader.default_material);
	// material_storage->shader_free(particles_shader.default_shader);
	// particles_shader.copy_shader.version_free(particles_shader.copy_shader_version);
}

/* PARTICLES */

RID ParticlesStorage::particles_allocate() {
    return RID();
}

void ParticlesStorage::particles_initialize(RID p_rid) {

}

void ParticlesStorage::particles_free(RID p_rid) {

}

void ParticlesStorage::particles_set_mode(RID p_particles, RS::ParticlesMode p_mode) {
    
}

void ParticlesStorage::particles_set_emitting(RID p_particles, bool p_emitting) {

}

bool ParticlesStorage::particles_get_emitting(RID p_particles) {

}

void ParticlesStorage::_particles_free_data(Particles *particles) {

}

void ParticlesStorage::particles_set_amount(RID p_particles, int p_amount) {

}

void ParticlesStorage::particles_set_amount_ratio(RID p_particles, float p_amount_ratio) {

}

void ParticlesStorage::particles_set_lifetime(RID p_particles, double p_lifetime) {

}

void ParticlesStorage::particles_set_one_shot(RID p_particles, bool p_one_shot) {

}

void ParticlesStorage::particles_set_pre_process_time(RID p_particles, double p_time) {

}

void ParticlesStorage::particles_request_process_time(RID p_particles, real_t p_request_process_time) {

}

void ParticlesStorage::particles_set_seed(RID p_particles, uint32_t p_seed) {

}

void ParticlesStorage::particles_set_explosiveness_ratio(RID p_particles, real_t p_ratio) {

}

void ParticlesStorage::particles_set_randomness_ratio(RID p_particles, real_t p_ratio) {

}

void ParticlesStorage::particles_set_custom_aabb(RID p_particles, const AABB &p_aabb) {

}

void ParticlesStorage::particles_set_speed_scale(RID p_particles, double p_scale) {

}

void ParticlesStorage::particles_set_use_local_coordinates(RID p_particles, bool p_enable) {

}

void ParticlesStorage::particles_set_fixed_fps(RID p_particles, int p_fps) {

}

void ParticlesStorage::particles_set_interpolate(RID p_particles, bool p_enable) {

}

void ParticlesStorage::particles_set_fractional_delta(RID p_particles, bool p_enable) {

}

void ParticlesStorage::particles_set_trails(RID p_particles, bool p_enable, double p_length) {
	if (p_enable) {
		WARN_PRINT_ONCE_ED("The Legacy renderer does not support particle trails.");
	}
}

void ParticlesStorage::particles_set_trail_bind_poses(RID p_particles, const Vector<Transform3D> &p_bind_poses) {
	if (p_bind_poses.size() != 0) {
		WARN_PRINT_ONCE_ED("The Legacy renderer does not support particle trails.");
	}
}

void ParticlesStorage::particles_set_collision_base_size(RID p_particles, real_t p_size) {

}

void ParticlesStorage::particles_set_transform_align(RID p_particles, RS::ParticlesTransformAlign p_transform_align) {

}

void ParticlesStorage::particles_set_process_material(RID p_particles, RID p_material) {

}

RID ParticlesStorage::particles_get_process_material(RID p_particles) const {
    return RID();
}

void ParticlesStorage::particles_set_draw_order(RID p_particles, RS::ParticlesDrawOrder p_order) {

}

void ParticlesStorage::particles_set_draw_passes(RID p_particles, int p_passes) {

}

void ParticlesStorage::particles_set_draw_pass_mesh(RID p_particles, int p_pass, RID p_mesh) {

}

void ParticlesStorage::particles_restart(RID p_particles) {

}

void ParticlesStorage::particles_set_subemitter(RID p_particles, RID p_subemitter_particles) {
	if (p_subemitter_particles.is_valid()) {
		WARN_PRINT_ONCE_ED("The Legacy renderer does not support particle sub-emitters.");
	}
}

void ParticlesStorage::particles_emit(RID p_particles, const Transform3D &p_transform, const Vector3 &p_velocity, const Color &p_color, const Color &p_custom, uint32_t p_emit_flags) {
	WARN_PRINT_ONCE_ED("The Legacy renderer does not support manually emitting particles.");
}

void ParticlesStorage::particles_request_process(RID p_particles) {

}

AABB ParticlesStorage::particles_get_current_aabb(RID p_particles) {
    return AABB();
}

AABB ParticlesStorage::particles_get_aabb(RID p_particles) const {
    return AABB();
}

void ParticlesStorage::particles_set_emission_transform(RID p_particles, const Transform3D &p_transform) {

}

void ParticlesStorage::particles_set_emitter_velocity(RID p_particles, const Vector3 &p_velocity) {

}

void ParticlesStorage::particles_set_interp_to_end(RID p_particles, float p_interp) {

}

int ParticlesStorage::particles_get_draw_passes(RID p_particles) const {
    return 0;
}

RID ParticlesStorage::particles_get_draw_pass_mesh(RID p_particles, int p_pass) const {
    return RID();
}

void ParticlesStorage::particles_add_collision(RID p_particles, RID p_particles_collision_instance) {

}

void ParticlesStorage::particles_remove_collision(RID p_particles, RID p_particles_collision_instance) {

}

void ParticlesStorage::particles_set_canvas_sdf_collision(RID p_particles, bool p_enable, const Transform2D &p_xform, const Rect2 &p_to_screen, GLuint p_texture) {

}

// Does one step of processing particles by reading from back_process_buffer and writing to front_process_buffer.
void ParticlesStorage::_particles_process(Particles *p_particles, double p_delta) {
	
}

void ParticlesStorage::particles_set_view_axis(RID p_particles, const Vector3 &p_axis, const Vector3 &p_up_axis) {
	
}

void ParticlesStorage::_particles_update_buffers(Particles *particles) {
	
}

void ParticlesStorage::_particles_allocate_history_buffers(Particles *particles) {

}

void ParticlesStorage::_particles_update_instance_buffer(Particles *particles, const Vector3 &p_axis, const Vector3 &p_up_axis) {

}

void ParticlesStorage::update_particles() {

}

template <typename ParticleInstanceData>
void ParticlesStorage::_particles_reverse_lifetime_sort(Particles *particles) {

}

Dependency *ParticlesStorage::particles_get_dependency(RID p_particles) const {
    return nullptr;
}

bool ParticlesStorage::particles_is_inactive(RID p_particles) const {
    return false;
}

/* PARTICLES COLLISION API */

RID ParticlesStorage::particles_collision_allocate() {
	return RID();
}

void ParticlesStorage::particles_collision_initialize(RID p_rid) {

}

void ParticlesStorage::particles_collision_free(RID p_rid) {

}

GLuint ParticlesStorage::particles_collision_get_heightfield_framebuffer(RID p_particles_collision) const {
    return 0;
}

void ParticlesStorage::particles_collision_set_collision_type(RID p_particles_collision, RS::ParticlesCollisionType p_type) {

}

void ParticlesStorage::particles_collision_set_cull_mask(RID p_particles_collision, uint32_t p_cull_mask) {

}

void ParticlesStorage::particles_collision_set_sphere_radius(RID p_particles_collision, real_t p_radius) {

}

void ParticlesStorage::particles_collision_set_box_extents(RID p_particles_collision, const Vector3 &p_extents) {

}

void ParticlesStorage::particles_collision_set_attractor_strength(RID p_particles_collision, real_t p_strength) {

}

void ParticlesStorage::particles_collision_set_attractor_directionality(RID p_particles_collision, real_t p_directionality) {

}

void ParticlesStorage::particles_collision_set_attractor_attenuation(RID p_particles_collision, real_t p_curve) {

}

void ParticlesStorage::particles_collision_set_field_texture(RID p_particles_collision, RID p_texture) {
	WARN_PRINT_ONCE_ED("The Legacy renderer does not support SDF collisions in 3D particle shaders");
}

void ParticlesStorage::particles_collision_height_field_update(RID p_particles_collision) {

}

void ParticlesStorage::particles_collision_set_height_field_resolution(RID p_particles_collision, RS::ParticlesCollisionHeightfieldResolution p_resolution) {

}

AABB ParticlesStorage::particles_collision_get_aabb(RID p_particles_collision) const {
    return AABB();
}

Vector3 ParticlesStorage::particles_collision_get_extents(RID p_particles_collision) const {
    return Vector3();
}

bool ParticlesStorage::particles_collision_is_heightfield(RID p_particles_collision) const {
    return false;
}

uint32_t ParticlesStorage::particles_collision_get_height_field_mask(RID p_particles_collision) const {
    return 0;
}

void ParticlesStorage::particles_collision_set_height_field_mask(RID p_particles_collision, uint32_t p_heightfield_mask) {

}

Dependency *ParticlesStorage::particles_collision_get_dependency(RID p_particles_collision) const {
    return nullptr;
}

/* Particles collision instance */

RID ParticlesStorage::particles_collision_instance_create(RID p_collision) {
    return RID();
}

void ParticlesStorage::particles_collision_instance_free(RID p_rid) {

}

void ParticlesStorage::particles_collision_instance_set_transform(RID p_collision_instance, const Transform3D &p_transform) {

}

void ParticlesStorage::particles_collision_instance_set_active(RID p_collision_instance, bool p_active) {
    
}

#endif // GLES2_ENABLED
