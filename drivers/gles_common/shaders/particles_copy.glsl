/* clang-format off */
#[modes]

mode_default =

#[specializations]

MODE_3D = false
TRANSFORM_ALIGN_Z_BILLBOARD = false
TRANSFORM_ALIGN_Y_TO_VELOCITY = false
TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY = false

#[vertex]

#include "stdlib_inc.glsl"

attribute highp vec4 color; //attrib:0
attribute highp vec4 velocity_active; //attrib:1
attribute highp vec4 custom; //attrib:2
attribute highp vec4 xform_1; //attrib:3
attribute highp vec4 xform_2; //attrib:4
#ifdef MODE_3D
attribute highp vec4 xform_3; //attrib:5
#endif

/* clang-format on */

varying highp vec4 out_xform_1; //tfb:
varying highp vec4 out_xform_2; //tfb:
#ifdef MODE_3D
varying highp vec4 out_xform_3; //tfb:MODE_3D
#endif
varying highp vec4 instance_color_custom_data_1; //tfb:
varying highp vec4 instance_color_custom_data_2; //tfb:

uniform lowp vec3 sort_direction;
uniform highp float frame_remainder;
uniform highp vec3 align_up;
uniform highp int align_mode;

uniform highp mat4 inv_emission_transform;

#define TRANSFORM_ALIGN_DISABLED 0
#define TRANSFORM_ALIGN_Z_BILLBOARD 1
#define TRANSFORM_ALIGN_Y_TO_VELOCITY 2
#define TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY 3

#define FLT_MAX 3.402823466e+38

void main() {
	mat4 txform = mat4(vec4(0.0), vec4(0.0), vec4(0.0), vec4(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0));

	if (velocity_active.w > 0.5) { 
#ifdef MODE_3D
		txform = transpose(mat4(xform_1, xform_2, xform_3, vec4(0.0, 0.0, 0.0, 1.0)));
#else
		txform = transpose(mat4(xform_1, xform_2, vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0)));
#endif

#if defined(TRANSFORM_ALIGN_Z_BILLBOARD)
		mat3 local = mat3(normalize(cross(align_up, sort_direction)), align_up, sort_direction);
		local = local * mat3(txform);
		txform[0].xyz = local[0];
		txform[1].xyz = local[1];
		txform[2].xyz = local[2];
#elif defined(TRANSFORM_ALIGN_Y_TO_VELOCITY)
		vec3 v = velocity_active.xyz;
		float s = (length(txform[0]) + length(txform[1]) + length(txform[2])) / 3.0;
		if (length(v) > 0.0) {
			txform[1].xyz = normalize(v);
		} else {
			txform[1].xyz = normalize(txform[1].xyz);
		}
		txform[0].xyz = normalize(cross(txform[1].xyz, txform[2].xyz));
		txform[2].xyz = vec3(0.0, 0.0, 1.0) * s;
		txform[0].xyz *= s;
		txform[1].xyz *= s;
#elif defined(TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY)
		vec3 sv = velocity_active.xyz - sort_direction * dot(sort_direction, velocity_active.xyz);
		float s = (length(txform[0]) + length(txform[1]) + length(txform[2])) / 3.0;
		if (length(sv) == 0.0) {
			sv = align_up;
		}
		sv = normalize(sv);
		txform[0].xyz = normalize(cross(sv, sort_direction)) * s;
		txform[1].xyz = sv * s;
		txform[2].xyz = sort_direction * s;
#endif

		txform[3].xyz += velocity_active.xyz * frame_remainder;

#ifndef MODE_3D
		txform = inv_emission_transform * txform;
#endif
	}
	txform = transpose(txform);

	instance_color_custom_data_1 = color;
	instance_color_custom_data_2 = custom;
	
	out_xform_1 = txform[0];
	out_xform_2 = txform[1];
#ifdef MODE_3D
	out_xform_3 = txform[2];
#endif
	gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}

/* clang-format off */
#[fragment]

void main() {
#ifndef USE_TRANSFORM_FEEDBACK
	// Ensure fragment shader isn't optimized out
	gl_FragColor = vec4(0.0);
#endif
}
/* clang-format on */
