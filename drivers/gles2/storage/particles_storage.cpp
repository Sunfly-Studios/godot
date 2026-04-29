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
#include "drivers/gles2/rasterizer_gles2.h"

using namespace GLES2;

ParticlesStorage *ParticlesStorage::singleton = nullptr;

ParticlesStorage *ParticlesStorage::get_singleton() {
	return singleton;
}

ParticlesStorage::ParticlesStorage() {
	singleton = this;
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	ERR_FAIL_NULL(material_storage);
	{
		String global_defines;
		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
		material_storage->shaders.particles_process_shader.initialize(global_defines, 1);
		particles_shader.default_shader_version = material_storage->shaders.particles_process_shader.version_create();
	}
	{
		// default material and shader for particles shader
		particles_shader.default_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(particles_shader.default_shader);
		material_storage->shader_set_code(particles_shader.default_shader, R"(
// Default particles shader.

shader_type particles;

void process() {
	COLOR = vec4(1.0);
}
)");
		particles_shader.default_material = material_storage->material_allocate();
		material_storage->material_initialize(particles_shader.default_material);
		material_storage->material_set_shader(particles_shader.default_material, particles_shader.default_shader);
	}
	{
		particles_shader.copy_shader.initialize();
		particles_shader.copy_shader_version = particles_shader.copy_shader.version_create();
	}
}

ParticlesStorage::~ParticlesStorage() {
	singleton = nullptr;
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	if (material_storage) {
		material_storage->material_free(particles_shader.default_material);
		material_storage->shader_free(particles_shader.default_shader);

		if (particles_shader.default_shader_version.is_valid()) {
			material_storage->shaders.particles_process_shader.version_free(particles_shader.default_shader_version);
		}
	}

	particles_shader.copy_shader.version_free(particles_shader.copy_shader_version);
}

/* PARTICLES */

RID ParticlesStorage::particles_allocate() {
	return particles_owner.allocate_rid();
}

void ParticlesStorage::particles_initialize(RID p_rid) {
	particles_owner.initialize_rid(p_rid);
}

void ParticlesStorage::particles_free(RID p_rid) {
	Particles *particles = particles_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(particles);

	particles->dependency.deleted_notify(p_rid);
	particles->update_list.remove_from_list();

	_particles_free_data(particles);
	particles_owner.free(p_rid);
}

void ParticlesStorage::particles_set_mode(RID p_particles, RS::ParticlesMode p_mode) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	if (particles->mode == p_mode) {
		return;
	}

	_particles_free_data(particles);
	particles->mode = p_mode;
}

void ParticlesStorage::particles_set_emitting(RID p_particles, bool p_emitting) {
	ERR_FAIL_COND_MSG(GLES2::Config::get_singleton()->disable_particles_workaround, "Due to driver bugs, GPUParticles are not supported on this device. Please use CPUParticles instead.");

	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);

	particles->emitting = p_emitting;
}

bool ParticlesStorage::particles_get_emitting(RID p_particles) {
	if (GLES2::Config::get_singleton()->disable_particles_workaround) {
		return false;
	}

	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, false);

	return particles->emitting;
}

void ParticlesStorage::_particles_free_data(Particles *particles) {
	ERR_FAIL_NULL(particles);

	particles->userdata_count = 0;
	particles->instance_buffer_size_cache = 0;
	particles->instance_buffer_stride_cache = 0;
	particles->num_attrib_arrays_cache = 0;
	particles->process_buffer_stride_cache = 0;

	if (particles->front_process_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(particles->front_process_buffer);
		GLES2::Utilities::get_singleton()->buffer_free_data(particles->front_instance_buffer);
		particles->front_process_buffer = 0;
		particles->front_instance_buffer = 0;

		GLES2::Utilities::get_singleton()->buffer_free_data(particles->back_process_buffer);
		GLES2::Utilities::get_singleton()->buffer_free_data(particles->back_instance_buffer);
		particles->back_process_buffer = 0;
		particles->back_instance_buffer = 0;
	}

	if (particles->sort_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(particles->last_frame_buffer);
		GLES2::Utilities::get_singleton()->buffer_free_data(particles->sort_buffer);
		particles->last_frame_buffer = 0;
		particles->sort_buffer = 0;
		particles->sort_buffer_filled = false;
		particles->last_frame_buffer_filled = false;
	}
}

void ParticlesStorage::particles_set_amount(RID p_particles, int p_amount) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);

	if (particles->amount == p_amount) {
		return;
	}

	_particles_free_data(particles);

	particles->amount = p_amount;
	particles->prev_ticks = 0;
	particles->phase = 0;
	particles->prev_phase = 0;
	particles->clear = true;

	particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_PARTICLES);
}

void ParticlesStorage::particles_set_amount_ratio(RID p_particles, float p_amount_ratio) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->amount_ratio = p_amount_ratio;
}

void ParticlesStorage::particles_set_lifetime(RID p_particles, double p_lifetime) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->lifetime = p_lifetime;
}

void ParticlesStorage::particles_set_one_shot(RID p_particles, bool p_one_shot) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->one_shot = p_one_shot;
}

void ParticlesStorage::particles_set_pre_process_time(RID p_particles, double p_time) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->pre_process_time = p_time;
}

void ParticlesStorage::particles_request_process_time(RID p_particles, real_t p_request_process_time) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->request_process_time = p_request_process_time;
}

void ParticlesStorage::particles_set_seed(RID p_particles, uint32_t p_seed) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->random_seed = p_seed;
}

void ParticlesStorage::particles_set_explosiveness_ratio(RID p_particles, real_t p_ratio) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->explosiveness = p_ratio;
}

void ParticlesStorage::particles_set_randomness_ratio(RID p_particles, real_t p_ratio) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->randomness = p_ratio;
}

void ParticlesStorage::particles_set_custom_aabb(RID p_particles, const AABB &p_aabb) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->custom_aabb = p_aabb;
	particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

void ParticlesStorage::particles_set_speed_scale(RID p_particles, double p_scale) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->speed_scale = p_scale;
}

void ParticlesStorage::particles_set_use_local_coordinates(RID p_particles, bool p_enable) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);

	particles->use_local_coords = p_enable;
	particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_PARTICLES);
}

void ParticlesStorage::particles_set_fixed_fps(RID p_particles, int p_fps) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);

	particles->fixed_fps = p_fps;

	_particles_free_data(particles);

	particles->prev_ticks = 0;
	particles->phase = 0;
	particles->prev_phase = 0;
	particles->clear = true;

	particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_PARTICLES);
}

void ParticlesStorage::particles_set_interpolate(RID p_particles, bool p_enable) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->interpolate = p_enable;
}

void ParticlesStorage::particles_set_fractional_delta(RID p_particles, bool p_enable) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->fractional_delta = p_enable;
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
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->collision_base_size = p_size;
}

void ParticlesStorage::particles_set_transform_align(RID p_particles, RS::ParticlesTransformAlign p_transform_align) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->transform_align = p_transform_align;
}

void ParticlesStorage::particles_set_process_material(RID p_particles, RID p_material) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);

	particles->process_material = p_material;
	particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_PARTICLES);
}

RID ParticlesStorage::particles_get_process_material(RID p_particles) const {
	const Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, RID());
	return particles->process_material;
}

void ParticlesStorage::particles_set_draw_order(RID p_particles, RS::ParticlesDrawOrder p_order) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->draw_order = p_order;
}

void ParticlesStorage::particles_set_draw_passes(RID p_particles, int p_passes) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->draw_passes.resize(p_passes);
}

void ParticlesStorage::particles_set_draw_pass_mesh(RID p_particles, int p_pass, RID p_mesh) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	ERR_FAIL_INDEX(p_pass, particles->draw_passes.size());
	particles->draw_passes.write[p_pass] = p_mesh;
	particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_PARTICLES);
}

void ParticlesStorage::particles_restart(RID p_particles) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->restart_request = true;
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
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);

	if (!particles->dirty) {
		particles->dirty = true;
		if (!particles->update_list.in_list()) {
			particle_update_list.add(&particles->update_list);
		}
	}
}

AABB ParticlesStorage::particles_get_current_aabb(RID p_particles) {
	const Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, AABB());

	int total_amount = particles->amount;

	if (total_amount == 0 || !GLES2::Config::get_singleton()->support_mapbuffer) {
		// Cannot read back from GPU in standard GLES2. Use custom AABB.
		return particles->custom_aabb;
	}

	GLuint read_buffer = particles->sort_buffer_filled ? particles->sort_buffer : particles->back_instance_buffer;
	if (read_buffer == 0) {
		return particles->custom_aabb;
	}

	glBindBuffer(GL_ARRAY_BUFFER, read_buffer);
	void *data_ptr = glMapBufferOES(GL_ARRAY_BUFFER, GL_MAP_READ_BIT_OES);
	GL_CHECK_ERROR("ParticlesStorage::particles_get_current_aabb: glMapBufferOES");

	AABB aabb;
	if (data_ptr) {
		Transform3D inv = particles->emission_transform.affine_inverse();
		bool first = true;
		uint32_t particle_data_size = sizeof(ParticleInstanceData3D);

		for (int i = 0; i < total_amount; i++) {
			const ParticleInstanceData3D &particle_data = *(const ParticleInstanceData3D *)((uint8_t *)data_ptr + particle_data_size * i);

			if (particle_data.xform[0] > 0.0) {
				Vector3 pos = Vector3(particle_data.xform[3], particle_data.xform[7], particle_data.xform[11]);
				if (!particles->use_local_coords) {
					pos = inv.xform(pos);
				}
				if (first) {
					aabb.position = pos;
					first = false;
				} else {
					aabb.expand_to(pos);
				}
			}
		}
		glUnmapBufferOES(GL_ARRAY_BUFFER);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	float longest_axis_size = 0;
	for (int i = 0; i < particles->draw_passes.size(); i++) {
		if (particles->draw_passes[i].is_valid()) {
			AABB maabb = MeshStorage::get_singleton()->mesh_get_aabb(particles->draw_passes[i], RID());
			longest_axis_size = MAX(maabb.get_longest_axis_size(), longest_axis_size);
		}
	}

	aabb.grow_by(longest_axis_size);
	return aabb;
}

AABB ParticlesStorage::particles_get_aabb(RID p_particles) const {
	const Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, AABB());
	return particles->custom_aabb;
}

void ParticlesStorage::particles_set_emission_transform(RID p_particles, const Transform3D &p_transform) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->emission_transform = p_transform;
}

void ParticlesStorage::particles_set_emitter_velocity(RID p_particles, const Vector3 &p_velocity) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->emitter_velocity = p_velocity;
}

void ParticlesStorage::particles_set_interp_to_end(RID p_particles, float p_interp) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->interp_to_end = p_interp;
}

int ParticlesStorage::particles_get_draw_passes(RID p_particles) const {
	const Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, 0);
	return particles->draw_passes.size();
}

RID ParticlesStorage::particles_get_draw_pass_mesh(RID p_particles, int p_pass) const {
	const Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, RID());
	ERR_FAIL_INDEX_V(p_pass, particles->draw_passes.size(), RID());
	return particles->draw_passes[p_pass];
}

void ParticlesStorage::particles_add_collision(RID p_particles, RID p_particles_collision_instance) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->collisions.insert(p_particles_collision_instance);
}

void ParticlesStorage::particles_remove_collision(RID p_particles, RID p_particles_collision_instance) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->collisions.erase(p_particles_collision_instance);
}

void ParticlesStorage::particles_set_canvas_sdf_collision(RID p_particles, bool p_enable, const Transform2D &p_xform, const Rect2 &p_to_screen, GLuint p_texture) {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL(particles);
	particles->has_sdf_collision = p_enable;
	particles->sdf_collision_transform = p_xform;
	particles->sdf_collision_to_screen = p_to_screen;
	particles->sdf_collision_texture = p_texture;
}

// Does one step of processing particles by reading from back_process_buffer and writing to front_process_buffer.
void ParticlesStorage::_particles_process(Particles *p_particles, double p_delta) {
	ERR_FAIL_NULL(p_particles);
	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	ERR_FAIL_NULL(texture_storage);
	ERR_FAIL_NULL(material_storage);

	double new_phase = Math::fmod(p_particles->phase + (p_delta / p_particles->lifetime), 1.0);

	if (p_particles->clear) {
		p_particles->cycle_number = 0;
	} else if (new_phase < p_particles->phase) {
		if (p_particles->one_shot) {
			p_particles->emitting = false;
		}
		p_particles->cycle_number++;
	}

	p_particles->phase = new_phase;

	ParticleProcessMaterialData *m = static_cast<ParticleProcessMaterialData *>(material_storage->material_get_data(p_particles->process_material, RS::SHADER_PARTICLES));
	if (!m) {
		m = static_cast<ParticleProcessMaterialData *>(material_storage->material_get_data(particles_shader.default_material, RS::SHADER_PARTICLES));
	}
	ERR_FAIL_NULL(m);

	ParticlesShaderGLES2::ShaderVariant variant = ParticlesShaderGLES2::MODE_DEFAULT;
	uint32_t specialization = 0;

	if (p_particles->mode == RS::ParticlesMode::PARTICLES_MODE_3D) {
		specialization |= ParticlesShaderGLES2::MODE_3D;
	}

	RID version = particles_shader.default_shader_version;
	if (m->shader_data->version.is_valid() && m->shader_data->valid) {
		m->bind_uniforms();
		version = m->shader_data->version;
	}

	bool success = material_storage->shaders.particles_process_shader.version_bind_shader(version, variant, specialization);
	if (!success) {
		return;
	}

	// Upload standard uniforms
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::EMITTING, p_particles->emitting, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::CYCLE, (int)p_particles->cycle_number, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::SYSTEM_PHASE, (float)new_phase, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::PREV_SYSTEM_PHASE, (float)p_particles->phase, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::EXPLOSIVENESS, (float)p_particles->explosiveness, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::RANDOMNESS, (float)p_particles->randomness, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::TIME, (float)RSG::rasterizer->get_total_time(), version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::DELTA, (float)p_delta, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::PARTICLE_SIZE, (float)p_particles->collision_base_size, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::RANDOM_SEED, (int)p_particles->random_seed, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::FRAME, (int)(p_particles->frame_counter++), version, variant, specialization);

	Transform3D emission_xform;
	if (!p_particles->use_local_coords) {
		emission_xform = p_particles->emission_transform;
	}
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::EMISSION_TRANSFORM, emission_xform, version, variant, specialization);

	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::LIFETIME, (float)p_particles->lifetime, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::CLEAR, p_particles->clear, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::TOTAL_PARTICLES, (int)p_particles->amount, version, variant, specialization);
	material_storage->shaders.particles_process_shader.version_set_uniform(ParticlesShaderGLES2::USE_FRACTIONAL_DELTA, p_particles->fractional_delta, version, variant, specialization);
	GL_CHECK_ERROR("ParticlesStorage::_particles_process: Set uniforms");

	p_particles->clear = false;
	p_particles->has_collision_cache = m->shader_data->uses_collision;

	// Transform feedback binding
	glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, p_particles->front_process_buffer);
	GL_CHECK_ERROR("ParticlesStorage::_particles_process: glBindBufferBaseEXT");

	glBindBuffer(GL_ARRAY_BUFFER, p_particles->back_process_buffer);

	// Vertex attribute binding
	uint32_t stride = p_particles->process_buffer_stride_cache;
	for (uint32_t j = 0; j < p_particles->num_attrib_arrays_cache; j++) {
		glEnableVertexAttribArray(j);
		glVertexAttribPointer(j, 4, GL_FLOAT, GL_FALSE, stride, CAST_INT_TO_UCHAR_PTR(sizeof(float) * 4 * j));
	}
	GL_CHECK_ERROR("ParticlesStorage::_particles_process: glVertexAttribPointers");

	glBeginTransformFeedbackEXT(GL_POINTS);
	glDrawArrays(GL_POINTS, 0, p_particles->amount);
	glEndTransformFeedbackEXT();
	GL_CHECK_ERROR("ParticlesStorage::_particles_process: glDrawArrays TFB");

	// Cleanup state
	for (uint32_t j = 0; j < p_particles->num_attrib_arrays_cache; j++) {
		glDisableVertexAttribArray(j);
	}

	glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	SWAP(p_particles->front_process_buffer, p_particles->back_process_buffer);
}

void ParticlesStorage::particles_set_view_axis(RID p_particles, const Vector3 &p_axis, const Vector3 &p_up_axis) {

}

void ParticlesStorage::_particles_update_buffers(Particles *particles) {
	ERR_FAIL_NULL(particles);
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	uint32_t userdata_count = 0;

	if (particles->process_material.is_valid()) {
		GLES2::ParticleProcessMaterialData *material_data = static_cast<GLES2::ParticleProcessMaterialData *>(
			material_storage->material_get_data(
				particles->process_material,
				RS::SHADER_PARTICLES
			)
		);

		if (material_data) {
			if (material_data->shader_data->version.is_valid() && material_data->shader_data->valid) {
				userdata_count = material_data->shader_data->userdata_count;
			}
		}
	}

	if (userdata_count != particles->userdata_count) {
		_particles_free_data(particles);
	}

	if (particles->amount > 0 && particles->front_process_buffer == 0) {
		int total_amount = particles->amount;

		particles->userdata_count = userdata_count;

		uint32_t xform_size = particles->mode == RS::PARTICLES_MODE_2D ? 2 : 3;
		particles->instance_buffer_stride_cache = sizeof(float) * 4 * (xform_size + 1);
		particles->instance_buffer_size_cache = particles->instance_buffer_stride_cache * total_amount;
		particles->num_attrib_arrays_cache = 5 + userdata_count + (xform_size - 2);
		particles->process_buffer_stride_cache = sizeof(float) * 4 * particles->num_attrib_arrays_cache;

		PackedByteArray data;
		data.resize_zeroed(particles->process_buffer_stride_cache * total_amount);

		PackedByteArray instance_data;
		instance_data.resize_zeroed(particles->instance_buffer_size_cache);

		// Generate buffers
		glGenBuffers(1, &particles->front_process_buffer);
		GL_CHECK_ERROR("ParticlesStorage::_particles_update_buffers: glGenBuffers front_process");
		glGenBuffers(1, &particles->front_instance_buffer);
		GL_CHECK_ERROR("ParticlesStorage::_particles_update_buffers: glGenBuffers front_instance");

		glBindBuffer(GL_ARRAY_BUFFER, particles->front_process_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, particles->front_process_buffer, particles->process_buffer_stride_cache * total_amount, data.ptr(), GL_DYNAMIC_DRAW, "Particles front process buffer");

		glBindBuffer(GL_ARRAY_BUFFER, particles->front_instance_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, particles->front_instance_buffer, particles->instance_buffer_size_cache, instance_data.ptr(), GL_DYNAMIC_DRAW, "Particles front instance buffer");

		glGenBuffers(1, &particles->back_process_buffer);
		GL_CHECK_ERROR("ParticlesStorage::_particles_update_buffers: glGenBuffers back_process");
		glGenBuffers(1, &particles->back_instance_buffer);
		GL_CHECK_ERROR("ParticlesStorage::_particles_update_buffers: glGenBuffers back_instance");

		glBindBuffer(GL_ARRAY_BUFFER, particles->back_process_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, particles->back_process_buffer, particles->process_buffer_stride_cache * total_amount, data.ptr(), GL_DYNAMIC_DRAW, "Particles back process buffer");

		glBindBuffer(GL_ARRAY_BUFFER, particles->back_instance_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, particles->back_instance_buffer, particles->instance_buffer_size_cache, instance_data.ptr(), GL_DYNAMIC_DRAW, "Particles back instance buffer");

		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

void ParticlesStorage::_particles_allocate_history_buffers(Particles *particles) {
	ERR_FAIL_NULL(particles);
	if (particles->sort_buffer == 0) {
		glGenBuffers(1, &particles->last_frame_buffer);
		GL_CHECK_ERROR("ParticlesStorage::_particles_allocate_history_buffers: glGenBuffers last_frame");
		glBindBuffer(GL_ARRAY_BUFFER, particles->last_frame_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, particles->last_frame_buffer, particles->instance_buffer_size_cache, nullptr, GL_DYNAMIC_DRAW, "Particles last frame buffer");

		glGenBuffers(1, &particles->sort_buffer);
		GL_CHECK_ERROR("ParticlesStorage::_particles_allocate_history_buffers: glGenBuffers sort_buffer");
		glBindBuffer(GL_ARRAY_BUFFER, particles->sort_buffer);
		GLES2::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, particles->sort_buffer, particles->instance_buffer_size_cache, nullptr, GL_DYNAMIC_DRAW, "Particles sort buffer");

		particles->sort_buffer_filled = false;
		particles->last_frame_buffer_filled = false;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

void ParticlesStorage::_particles_update_instance_buffer(Particles *particles, const Vector3 &p_axis, const Vector3 &p_up_axis) {
	ERR_FAIL_NULL(particles);

	ParticlesCopyShaderGLES2::ShaderVariant variant = ParticlesCopyShaderGLES2::MODE_DEFAULT;
	uint64_t specialization = 0;
	if (particles->mode == RS::ParticlesMode::PARTICLES_MODE_3D) {
		specialization |= ParticlesCopyShaderGLES2::MODE_3D;
	}

	bool success = particles_shader.copy_shader.version_bind_shader(particles_shader.copy_shader_version, variant, specialization);
	if (!success) {
		return;
	}

	if (particles->use_local_coords) {
		particles_shader.copy_shader.version_set_uniform(ParticlesCopyShaderGLES2::INV_EMISSION_TRANSFORM, Transform3D(), particles_shader.copy_shader_version, variant, specialization);
	} else {
		Transform3D inv = particles->emission_transform.affine_inverse();
		particles_shader.copy_shader.version_set_uniform(ParticlesCopyShaderGLES2::INV_EMISSION_TRANSFORM, inv, particles_shader.copy_shader_version, variant, specialization);
	}

	particles_shader.copy_shader.version_set_uniform(ParticlesCopyShaderGLES2::FRAME_REMAINDER, particles->interpolate ? particles->frame_remainder : 0.0, particles_shader.copy_shader_version, variant, specialization);
	particles_shader.copy_shader.version_set_uniform(ParticlesCopyShaderGLES2::ALIGN_MODE, (int)particles->transform_align, particles_shader.copy_shader_version, variant, specialization);
	particles_shader.copy_shader.version_set_uniform(ParticlesCopyShaderGLES2::ALIGN_UP, p_up_axis, particles_shader.copy_shader_version, variant, specialization);
	particles_shader.copy_shader.version_set_uniform(ParticlesCopyShaderGLES2::SORT_DIRECTION, p_axis, particles_shader.copy_shader_version, variant, specialization);

	glBindBuffer(GL_ARRAY_BUFFER, particles->back_process_buffer);

	if (RasterizerGLES2::is_gles_over_gl()) {
		glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, particles->front_instance_buffer);
	} else {
		glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, particles->front_instance_buffer);
	}
	GL_CHECK_ERROR("ParticlesStorage::_particles_update_instance_buffer: glBindBufferBase");

	if (particles->draw_order == RS::PARTICLES_DRAW_ORDER_LIFETIME) {
		WARN_PRINT_ONCE("PARTICLES_DRAW_ORDER_LIFETIME is not fully supported in GLES2 due to missing glBindBufferRange. Falling back to default sorting order.");
	}

	uint32_t stride = particles->process_buffer_stride_cache;
	for (uint32_t j = 0; j < particles->num_attrib_arrays_cache; j++) {
		glEnableVertexAttribArray(j);
		glVertexAttribPointer(j, 4, GL_FLOAT, GL_FALSE, stride, CAST_INT_TO_UCHAR_PTR(sizeof(float) * 4 * j));
	}
	GL_CHECK_ERROR("ParticlesStorage::_particles_update_instance_buffer: glVertexAttribPointer");

	if (RasterizerGLES2::is_gles_over_gl()) {
		glBeginTransformFeedback(GL_POINTS);
		glDrawArrays(GL_POINTS, 0, particles->amount);
		glEndTransformFeedback();
		glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
	} else {
		glBeginTransformFeedbackEXT(GL_POINTS);
		glDrawArrays(GL_POINTS, 0, particles->amount);
		glEndTransformFeedbackEXT();
		glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, 0);
	}
	GL_CHECK_ERROR("ParticlesStorage::_particles_update_instance_buffer: glDrawArrays");

	for (uint32_t j = 0; j < particles->num_attrib_arrays_cache; j++) {
		glDisableVertexAttribArray(j);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ParticlesStorage::update_particles() {
	if (!particle_update_list.first()) {
		return;
	}

	RENDER_TIMESTAMP("Update GPUParticles GLES2");
	glEnable(GL_RASTERIZER_DISCARD);
	glBindFramebuffer(GL_FRAMEBUFFER, GLES2::TextureStorage::system_fbo);

	while (particle_update_list.first()) {
		Particles *particles = particle_update_list.first()->self();

		if (unlikely(!particles)) {
			continue;
		}

		particles->update_list.remove_from_list();
		particles->dirty = false;

		_particles_update_buffers(particles);

		if (particles->restart_request) {
			particles->prev_ticks = 0;
			particles->phase = 0;
			particles->prev_phase = 0;
			particles->clear = true;
			particles->restart_request = false;
		}

		if (particles->inactive && !particles->emitting) {
			continue;
		}

		if (particles->emitting) {
			if (particles->inactive) {
				particles->prev_ticks = 0;
				particles->phase = 0;
				particles->prev_phase = 0;
				particles->clear = true;
			}
			particles->inactive = false;
			particles->inactive_time = 0;
		} else {
			particles->inactive_time += particles->speed_scale * RSG::rasterizer->get_frame_delta_time();
			if (particles->inactive_time > particles->lifetime * 1.2) {
				particles->inactive = true;
				continue;
			}
		}

		// Buffer copying
		if (particles->draw_order == RS::PARTICLES_DRAW_ORDER_VIEW_DEPTH || particles->draw_order == RS::PARTICLES_DRAW_ORDER_REVERSE_LIFETIME) {
			_particles_allocate_history_buffers(particles);
			SWAP(particles->last_frame_buffer, particles->sort_buffer);

			if (GLES2::Config::get_singleton()->support_mapbuffer) {
				glBindBuffer(GL_ARRAY_BUFFER, particles->back_instance_buffer);
				// Query standard mapbuffer.
				void *read_ptr = glMapBufferOES(GL_ARRAY_BUFFER, GL_MAP_READ_BIT_OES);
				GL_CHECK_ERROR("ParticlesStorage::update_particles: glMapBufferOES read");

				glBindBuffer(GL_ARRAY_BUFFER, particles->last_frame_buffer);
				void *write_ptr = glMapBufferOES(GL_ARRAY_BUFFER, GL_MAP_WRITE_BIT_OES);
				GL_CHECK_ERROR("ParticlesStorage::update_particles: glMapBufferOES write");

				if (read_ptr && write_ptr) {
					memcpy(write_ptr, read_ptr, particles->instance_buffer_size_cache);
				} else {
					ERR_PRINT("Failed to map buffers for particle history copy via GL_OES_mapbuffer.");
				}

				glUnmapBufferOES(GL_ARRAY_BUFFER);
				glBindBuffer(GL_ARRAY_BUFFER, particles->back_instance_buffer);
				glUnmapBufferOES(GL_ARRAY_BUFFER);
				glBindBuffer(GL_ARRAY_BUFFER, 0);

				particles->sort_buffer_filled = particles->last_frame_buffer_filled;
				particles->sort_buffer_phase = particles->last_frame_phase;
				particles->last_frame_buffer_filled = true;
				particles->last_frame_phase = particles->phase;
			} else {
				WARN_PRINT_ONCE("Particle history and depth sorting require GL_OES_mapbuffer extension. Your driver does not support this. Visual artifacts may occur.");
				particles->sort_buffer_filled = false;
				particles->last_frame_buffer_filled = false;
			}
		}

		int fixed_fps = 0;
		if (particles->fixed_fps > 0) {
			fixed_fps = particles->fixed_fps;
		}

		if (particles->clear && particles->pre_process_time > 0.0) {
			double frame_time = (fixed_fps > 0) ? (1.0 / fixed_fps) : (1.0 / 30.0);
			double todo = particles->pre_process_time;
			while (todo >= 0) {
				_particles_process(particles, frame_time);
				todo -= frame_time;
			}
		}

		double time_scale = MAX(particles->speed_scale, 0.0);

		if (fixed_fps > 0) {
			double frame_time = 1.0 / fixed_fps;
			double delta = RSG::rasterizer->get_frame_delta_time();
			if (delta > 0.1) {
				delta = 0.1;
			} else if (delta < 0.0) {
				delta = 0.0;
			}

			double todo = particles->frame_remainder + delta * time_scale;
			while (todo >= frame_time) {
				_particles_process(particles, frame_time);
				todo -= frame_time;
			}
			particles->frame_remainder = todo;

		} else {
			_particles_process(particles, RSG::rasterizer->get_frame_delta_time() * time_scale);
		}

		// Update instance buffer
		if (particles->draw_order != RS::PARTICLES_DRAW_ORDER_VIEW_DEPTH && particles->transform_align != RS::PARTICLES_TRANSFORM_ALIGN_Z_BILLBOARD && particles->transform_align != RS::PARTICLES_TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY) {
			_particles_update_instance_buffer(particles, Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0));

			if (particles->draw_order == RS::PARTICLES_DRAW_ORDER_REVERSE_LIFETIME && particles->sort_buffer_filled && GLES2::Config::get_singleton()->support_mapbuffer) {
				if (particles->mode == RS::ParticlesMode::PARTICLES_MODE_2D) {
					_particles_reverse_lifetime_sort<ParticleInstanceData2D>(particles);
				} else {
					_particles_reverse_lifetime_sort<ParticleInstanceData3D>(particles);
				}
			}
		}

		SWAP(particles->front_instance_buffer, particles->back_instance_buffer);
		particles->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
	}

	glDisable(GL_RASTERIZER_DISCARD);
}

template <typename ParticleInstanceData>
void ParticlesStorage::_particles_reverse_lifetime_sort(Particles *particles) {

}

Dependency *ParticlesStorage::particles_get_dependency(RID p_particles) const {
	Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, nullptr);

	return &particles->dependency;
}

bool ParticlesStorage::particles_is_inactive(RID p_particles) const {
	const Particles *particles = particles_owner.get_or_null(p_particles);
	ERR_FAIL_NULL_V(particles, false);
	return !particles->emitting && particles->inactive;
}

/* PARTICLES COLLISION API */

RID ParticlesStorage::particles_collision_allocate() {
	return particles_collision_owner.allocate_rid();
}

void ParticlesStorage::particles_collision_initialize(RID p_rid) {
	particles_collision_owner.initialize_rid(p_rid, ParticlesCollision());
}

void ParticlesStorage::particles_collision_free(RID p_rid) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(particles_collision);

	if (particles_collision->heightfield_texture != 0) {
		GLES2::Utilities::get_singleton()->texture_free_data(particles_collision->heightfield_texture);
		particles_collision->heightfield_texture = 0;
		glDeleteFramebuffers(1, &particles_collision->heightfield_fb);
		GL_CHECK_ERROR("ParticlesStorage::particles_collision_free: glDeleteFramebuffers");
		particles_collision->heightfield_fb = 0;
	}
	particles_collision->dependency.deleted_notify(p_rid);
	particles_collision_owner.free(p_rid);
}

GLuint ParticlesStorage::particles_collision_get_heightfield_framebuffer(RID p_particles_collision) const {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL_V(particles_collision, 0);
	ERR_FAIL_COND_V(particles_collision->type != RS::PARTICLES_COLLISION_TYPE_HEIGHTFIELD_COLLIDE, 0);

	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();
	ERR_FAIL_NULL_V(texture_storage, 0);

	if (particles_collision->heightfield_texture == 0) {
		const int resolutions[RS::PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_MAX] = { 256, 512, 1024, 2048, 4096, 8192 };
		Size2i size;
		if (particles_collision->extents.x > particles_collision->extents.z) {
			size.x = resolutions[particles_collision->heightfield_resolution];
			size.y = int32_t(particles_collision->extents.z / particles_collision->extents.x * size.x);
		} else {
			size.y = resolutions[particles_collision->heightfield_resolution];
			size.x = int32_t(particles_collision->extents.x / particles_collision->extents.z * size.y);
		}

		glGenTextures(1, &particles_collision->heightfield_texture);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, particles_collision->heightfield_texture);
		GL_CHECK_ERROR("ParticlesStorage::particles_collision_get_heightfield_framebuffer: glBindTexture");
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, size.x, size.y, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		GL_CHECK_ERROR("ParticlesStorage::particles_collision_get_heightfield_framebuffer: glTexImage2D");

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		GL_CHECK_ERROR("ParticlesStorage::particles_collision_get_heightfield_framebuffer: glTexParameteri");

		glGenFramebuffers(1, &particles_collision->heightfield_fb);
		texture_storage->bind_framebuffer(particles_collision->heightfield_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, particles_collision->heightfield_texture, 0);
		GL_CHECK_ERROR("ParticlesStorage::particles_collision_get_heightfield_framebuffer: glFramebufferTexture2D");

#ifdef DEBUG_ENABLED
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			WARN_PRINT("Could create heightmap texture status: " + texture_storage->get_framebuffer_error(status));
		}
#endif
		GLES2::Utilities::get_singleton()->texture_allocated_data(particles_collision->heightfield_texture, size.x * size.y * 4, "Particles collision heightfield texture");

		particles_collision->heightfield_fb_size = size;

		glBindTexture(GL_TEXTURE_2D, 0);
		texture_storage->bind_framebuffer_system();
	}

	return particles_collision->heightfield_fb;
}

void ParticlesStorage::particles_collision_set_collision_type(RID p_particles_collision, RS::ParticlesCollisionType p_type) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);

	if (p_type == particles_collision->type) {
		return;
	}

	if (particles_collision->heightfield_texture != 0) {
		GLES2::Utilities::get_singleton()->texture_free_data(particles_collision->heightfield_texture);
		particles_collision->heightfield_texture = 0;
		glDeleteFramebuffers(1, &particles_collision->heightfield_fb);
		particles_collision->heightfield_fb = 0;
	}

	particles_collision->type = p_type;
	particles_collision->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

void ParticlesStorage::particles_collision_set_cull_mask(RID p_particles_collision, uint32_t p_cull_mask) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->cull_mask = p_cull_mask;
}

void ParticlesStorage::particles_collision_set_sphere_radius(RID p_particles_collision, real_t p_radius) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->radius = p_radius;
	particles_collision->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

void ParticlesStorage::particles_collision_set_box_extents(RID p_particles_collision, const Vector3 &p_extents) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->extents = p_extents;
	particles_collision->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

void ParticlesStorage::particles_collision_set_attractor_strength(RID p_particles_collision, real_t p_strength) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->attractor_strength = p_strength;
}

void ParticlesStorage::particles_collision_set_attractor_directionality(RID p_particles_collision, real_t p_directionality) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->attractor_directionality = p_directionality;
}

void ParticlesStorage::particles_collision_set_attractor_attenuation(RID p_particles_collision, real_t p_curve) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->attractor_attenuation = p_curve;
}

void ParticlesStorage::particles_collision_set_field_texture(RID p_particles_collision, RID p_texture) {
	WARN_PRINT_ONCE_ED("The Legacy renderer does not support SDF collisions in 3D particle shaders");
}

void ParticlesStorage::particles_collision_height_field_update(RID p_particles_collision) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_AABB);
}

void ParticlesStorage::particles_collision_set_height_field_resolution(RID p_particles_collision, RS::ParticlesCollisionHeightfieldResolution p_resolution) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	ERR_FAIL_INDEX(p_resolution, RS::PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_MAX);

	if (particles_collision->heightfield_resolution == p_resolution) {
		return;
	}

	particles_collision->heightfield_resolution = p_resolution;

	if (particles_collision->heightfield_texture != 0) {
		GLES2::Utilities::get_singleton()->texture_free_data(particles_collision->heightfield_texture);
		particles_collision->heightfield_texture = 0;
		glDeleteFramebuffers(1, &particles_collision->heightfield_fb);
		particles_collision->heightfield_fb = 0;
	}
}

AABB ParticlesStorage::particles_collision_get_aabb(RID p_particles_collision) const {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL_V(particles_collision, AABB());

	switch (particles_collision->type) {
		case RS::PARTICLES_COLLISION_TYPE_SPHERE_ATTRACT:
		case RS::PARTICLES_COLLISION_TYPE_SPHERE_COLLIDE: {
			AABB aabb;
			aabb.position = -Vector3(1, 1, 1) * particles_collision->radius;
			aabb.size = Vector3(2, 2, 2) * particles_collision->radius;
			return aabb;
		}
		default: {
			AABB aabb;
			aabb.position = -particles_collision->extents;
			aabb.size = particles_collision->extents * 2;
			return aabb;
		}
	}
}

Vector3 ParticlesStorage::particles_collision_get_extents(RID p_particles_collision) const {
	const ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL_V(particles_collision, Vector3());
	return particles_collision->extents;
}

bool ParticlesStorage::particles_collision_is_heightfield(RID p_particles_collision) const {
	const ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL_V(particles_collision, false);
	return particles_collision->type == RS::PARTICLES_COLLISION_TYPE_HEIGHTFIELD_COLLIDE;
}

uint32_t ParticlesStorage::particles_collision_get_height_field_mask(RID p_particles_collision) const {
	const ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL_V(particles_collision, false);
	return particles_collision->heightfield_mask;
}

void ParticlesStorage::particles_collision_set_height_field_mask(RID p_particles_collision, uint32_t p_heightfield_mask) {
	ParticlesCollision *particles_collision = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL(particles_collision);
	particles_collision->heightfield_mask = p_heightfield_mask;
}

Dependency *ParticlesStorage::particles_collision_get_dependency(RID p_particles_collision) const {
	ParticlesCollision *pc = particles_collision_owner.get_or_null(p_particles_collision);
	ERR_FAIL_NULL_V(pc, nullptr);

	return &pc->dependency;
}

/* Particles collision instance */

RID ParticlesStorage::particles_collision_instance_create(RID p_collision) {
	ParticlesCollisionInstance pci;
	pci.collision = p_collision;
	return particles_collision_instance_owner.make_rid(pci);
}

void ParticlesStorage::particles_collision_instance_free(RID p_rid) {
	particles_collision_instance_owner.free(p_rid);
}

void ParticlesStorage::particles_collision_instance_set_transform(RID p_collision_instance, const Transform3D &p_transform) {
	ParticlesCollisionInstance *pci = particles_collision_instance_owner.get_or_null(p_collision_instance);
	ERR_FAIL_NULL(pci);
	pci->transform = p_transform;
}

void ParticlesStorage::particles_collision_instance_set_active(RID p_collision_instance, bool p_active) {
	ParticlesCollisionInstance *pci = particles_collision_instance_owner.get_or_null(p_collision_instance);
	ERR_FAIL_NULL(pci);
	pci->active = p_active;
}

#endif // GLES2_ENABLED
