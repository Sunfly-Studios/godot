/* clang-format off */
#[modes]

mode_default =

#[specializations]

MODE_3D = false

#[vertex]

#include "stdlib_inc.glsl"

#define SDF_MAX_LENGTH 16384.0

#ifdef GLOBAL_SHADER_UNIFORMS_USED
uniform highp vec4 global_shader_uniforms[256];
#endif

#define MAX_ATTRACTORS 8

struct Attractor {
	mat4 transform;
	vec4 extents;
	int type;
	float strength;
	float attenuation;
	float directionality;
};

#define MAX_COLLIDERS 8

struct Collider {
	mat4 transform;
	vec4 extents;
	int type;
	float scale;
	float pad0;
	float pad1;
};

uniform bool emitting;
uniform int cycle;
uniform float system_phase;
uniform float prev_system_phase;
uniform float explosiveness;
uniform float randomness;
uniform float time;
uniform float delta;
uniform float particle_size;
uniform int random_seed;
uniform int attractor_count;
uniform int collider_count;
uniform int frame;
uniform mat4 emission_transform;

uniform Attractor attractors[MAX_ATTRACTORS];
uniform Collider colliders[MAX_COLLIDERS];

attribute highp vec4 color; //attrib:0
attribute highp vec4 velocity_active; //attrib:1
attribute highp vec4 custom; //attrib:2
attribute highp vec4 xform_1; //attrib:3
attribute highp vec4 xform_2; //attrib:4
#ifdef MODE_3D
attribute highp vec4 xform_3; //attrib:5
#endif

varying highp vec4 out_color; //tfb:
varying highp vec4 out_velocity_active; //tfb:
varying highp vec4 out_custom; //tfb:
varying highp vec4 out_xform_1; //tfb:
varying highp vec4 out_xform_2; //tfb:
#ifdef MODE_3D
varying highp vec4 out_xform_3; //tfb:MODE_3D
#endif

uniform sampler2D height_field_texture; //texunit:0

uniform float lifetime;
uniform bool clear;
uniform int total_particles;
uniform bool use_fractional_delta;

float rand_from_seed(float seed) {
	return fract(sin(dot(vec2(seed, seed * 1.341), vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 safe_normalize(vec3 direction) {
	const float EPSILON = 0.001;
	if (length(direction) < EPSILON) {
		return vec3(0.0);
	}
	return normalize(direction);
}

#GLOBALS

void main() {
	bool apply_forces = true;
	bool apply_velocity = true;
	float local_delta = delta;

	float mass = 1.0;
	bool restart = false;
	bool restart_position = false;
	bool restart_rotation_scale = false;
	bool restart_velocity = false;
	bool restart_color = false;
	bool restart_custom = false;

	mat4 xform = mat4(1.0);
	float active_flag = 0.0;

	if (clear) {
		out_color = vec4(1.0);
		out_custom = vec4(0.0);
		out_velocity_active = vec4(0.0);
	} else {
		out_color = color;
		out_velocity_active = velocity_active;
		out_custom = custom;
		xform[0] = xform_1;
		xform[1] = xform_2;
#ifdef MODE_3D
		xform[2] = xform_3;
#endif
		xform = transpose(xform);
		active_flag = velocity_active.w;
	}

	bool collided = false;
	vec3 collision_normal = vec3(0.0);
	float collision_depth = 0.0;
	vec3 attractor_force = vec3(0.0);

#if !defined(DISABLE_VELOCITY)
	if (active_flag > 0.5) {
		xform[3].xyz += out_velocity_active.xyz * local_delta;
	}
#endif

	float index_normalized = custom.w; 

	if (emitting) {
		float restart_phase = index_normalized;

		if (randomness > 0.0) {
			float seed = float(cycle);
			if (restart_phase >= system_phase) {
				seed -= 1.0;
			}
			seed *= float(total_particles);
			seed += index_normalized * float(total_particles);
			float random = rand_from_seed(seed);
			restart_phase += randomness * random * 1.0 / float(total_particles);
		}

		restart_phase *= (1.0 - explosiveness);

		if (system_phase > prev_system_phase) {
			if (restart_phase >= prev_system_phase && restart_phase < system_phase) {
				restart = true;
				if (use_fractional_delta) {
					local_delta = (system_phase - restart_phase) * lifetime;
				}
			}
		} else if (delta > 0.0) {
			if (restart_phase >= prev_system_phase) {
				restart = true;
				if (use_fractional_delta) {
					local_delta = (1.0 - restart_phase + system_phase) * lifetime;
				}
			} else if (restart_phase < system_phase) {
				restart = true;
				if (use_fractional_delta) {
					local_delta = (system_phase - restart_phase) * lifetime;
				}
			}
		}

		if (restart) {
			active_flag = emitting ? 1.0 : 0.0;
			restart_position = true;
			restart_rotation_scale = true;
			restart_velocity = true;
			restart_color = true;
			restart_custom = true;
		}
	}

	bool particle_active = active_flag > 0.5;

	if (restart && particle_active) {
#CODE : START
	}

	if (particle_active) {
#CODE : PROCESS
	}

	if (particle_active) {
		active_flag = 1.0;
	} else {
		active_flag = 0.0;
	}

	xform = transpose(xform);
	out_xform_1 = xform[0];
	out_xform_2 = xform[1];
#ifdef MODE_3D
	out_xform_3 = xform[2];
#endif
	out_velocity_active.w = active_flag;
	gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}

/* clang-format off */
#[fragment]

void main() {
#ifndef USE_TRANSFORM_FEEDBACK
	gl_FragColor = vec4(0.0);
#endif
}
/* clang-format on */
