#[modes]

mode_base_pass =
mode_blend_pass = #define MODE_BLEND_PASS

#[specializations]

MODE_2D = true
USE_BLEND_SHAPES = false
USE_SKELETON = false
USE_NORMAL = false
USE_TANGENT = false
FINAL_PASS = false
USE_EIGHT_WEIGHTS = false

#[vertex]

#include "stdlib_inc.glsl"

#ifdef MODE_2D
#define VFORMAT vec2
#else
#define VFORMAT vec3
#endif

#ifdef FINAL_PASS
#define OFORMAT vec2
#else
#define OFORMAT vec4
#endif

// These come from the source mesh and the output from previous passes.
attribute highp VFORMAT in_vertex; //attrib:0

#ifdef MODE_BLEND_PASS
#ifdef USE_NORMAL
attribute highp vec4 in_normal; //attrib:1
#endif
#ifdef USE_TANGENT
attribute highp vec4 in_tangent; //attrib:2
#endif
#else // MODE_BLEND_PASS
#ifdef USE_NORMAL
attribute highp vec2 in_normal; //attrib:1
#endif
#ifdef USE_TANGENT
attribute highp vec2 in_tangent; //attrib:2
#endif
#endif // MODE_BLEND_PASS

#ifdef USE_SKELETON
#ifdef USE_EIGHT_WEIGHTS
attribute highp vec4 in_bone_attrib; //attrib:10
attribute highp vec4 in_bone_attrib2; //attrib:11
attribute mediump vec4 in_weight_attrib; //attrib:12
attribute mediump vec4 in_weight_attrib2; //attrib:13
#else
attribute highp vec4 in_bone_attrib; //attrib:10
attribute mediump vec4 in_weight_attrib; //attrib:11
#endif

uniform highp sampler2D skeleton_texture; // texunit:0
uniform highp float skeleton_texture_inv_height;
#endif

/* clang-format on */
#ifdef MODE_BLEND_PASS
attribute highp VFORMAT blend_vertex; //attrib:3
#ifdef USE_NORMAL
attribute highp vec2 blend_normal; //attrib:4
#endif
#ifdef USE_TANGENT
attribute highp vec2 blend_tangent; //attrib:5
#endif
#endif // MODE_BLEND_PASS

varying highp VFORMAT out_vertex; //tfb:

#ifdef USE_NORMAL
varying highp OFORMAT out_normal; //tfb:USE_NORMAL
#endif
#ifdef USE_TANGENT
varying highp OFORMAT out_tangent; //tfb:USE_TANGENT
#endif

#ifdef USE_BLEND_SHAPES
uniform highp float blend_weight;
uniform lowp float blend_shape_count;
#endif

#ifdef USE_SKELETON
uniform mediump vec2 skeleton_transform_x;
uniform mediump vec2 skeleton_transform_y;
uniform mediump vec2 skeleton_transform_offset;

uniform mediump vec2 inverse_transform_x;
uniform mediump vec2 inverse_transform_y;
uniform mediump vec2 inverse_transform_offset;
#endif

vec2 signNotZero(vec2 v) {
	return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 oct_to_vec3(vec2 oct) {
	oct = oct * 2.0 - 1.0;
	vec3 v = vec3(oct.xy, 1.0 - abs(oct.x) - abs(oct.y));
	if (v.z < 0.0) {
		v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
	}
	return normalize(v);
}

vec2 vec3_to_oct(vec3 e) {
	e /= abs(e.x) + abs(e.y) + abs(e.z);
	vec2 oct = e.z >= 0.0 ? e.xy : (vec2(1.0) - abs(e.yx)) * signNotZero(e.xy);
	return oct * 0.5 + 0.5;
}

vec4 oct_to_tang(vec2 oct_sign_encoded) {
	vec2 oct = vec2(oct_sign_encoded.x, abs(oct_sign_encoded.y) * 2.0 - 1.0);
	return vec4(oct_to_vec3(oct), sign(oct_sign_encoded.y));
}

vec2 tang_to_oct(vec4 base) {
	vec2 oct = vec3_to_oct(base.xyz);
	oct.y = oct.y * 0.5 + 0.5;
	oct.y = base.w >= 0.0 ? oct.y : 1.0 - oct.y;
	return oct;
}

vec4 vec4_to_vec2(vec4 p_vec) {
	return p_vec;
}

vec4 vec2_to_vec4(vec4 p_vec) {
	return p_vec;
}

void main() {
#ifdef MODE_2D
	out_vertex = in_vertex;
#ifdef USE_BLEND_SHAPES
#ifdef MODE_BLEND_PASS
	out_vertex = in_vertex + blend_vertex * blend_weight;
#else
	out_vertex = in_vertex * blend_weight;
#endif
#ifdef FINAL_PASS
	out_vertex = normalize(out_vertex);
#endif
#endif // USE_BLEND_SHAPES

#ifdef USE_SKELETON

	// 0.00390625 = 1.0 / 256.0. Used to calculate U coordinates.
#define TEX(m) texture2D(skeleton_texture, vec2((mod((m), 256.0) + 0.5) * 0.00390625, (floor((m) * 0.00390625) + 0.5) * skeleton_texture_inv_height))

	vec4 bones = in_bone_attrib * 2.0;
	vec4 bones_a = bones + 1.0;

	vec4 m0 = TEX(bones.x) * in_weight_attrib.x;
	vec4 m1 = TEX(bones_a.x) * in_weight_attrib.x;

	m0 += TEX(bones.y) * in_weight_attrib.y;
	m1 += TEX(bones_a.y) * in_weight_attrib.y;
	
	m0 += TEX(bones.z) * in_weight_attrib.z;
	m1 += TEX(bones_a.z) * in_weight_attrib.z;
	
	m0 += TEX(bones.w) * in_weight_attrib.w;
	m1 += TEX(bones_a.w) * in_weight_attrib.w;

	mat4 skeleton_matrix = mat4(vec4(skeleton_transform_x, 0.0, 0.0), vec4(skeleton_transform_y, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(skeleton_transform_offset, 0.0, 1.0));
	mat4 inverse_matrix = mat4(vec4(inverse_transform_x, 0.0, 0.0), vec4(inverse_transform_y, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(inverse_transform_offset, 0.0, 1.0));
	mat4 trans_bone_matrix = mat4(
		vec4(m0.x, m1.x, 0.0, 0.0),
		vec4(m0.y, m1.y, 0.0, 0.0),
		vec4(m0.z, m1.z, 1.0, 0.0),
		vec4(m0.w, m1.w, 0.0, 1.0)
	);

	mat4 bone_matrix = skeleton_matrix * trans_bone_matrix * inverse_matrix;
	out_vertex = (bone_matrix * vec4(out_vertex, 0.0, 1.0)).xy;
#endif // USE_SKELETON

#else // MODE_2D

#ifdef USE_BLEND_SHAPES
#ifdef MODE_BLEND_PASS
	out_vertex = in_vertex + blend_vertex * blend_weight;
#ifdef USE_NORMAL
	vec3 normal = vec2_to_vec4(in_normal).xyz * blend_shape_count;
	vec3 normal_blend = oct_to_vec3(blend_normal) * blend_weight;
#ifdef FINAL_PASS
	out_normal = vec3_to_oct(normalize(normal + normal_blend));
#else
	out_normal = vec4_to_vec2(vec4(normal + normal_blend, 0.0) / blend_shape_count);
#endif
#endif // USE_NORMAL

#ifdef USE_TANGENT
	vec4 tangent = vec2_to_vec4(in_tangent) * blend_shape_count;
	vec4 tangent_blend = oct_to_tang(blend_tangent) * blend_weight;
#ifdef FINAL_PASS
	out_tangent = tang_to_oct(vec4(normalize(tangent.xyz + tangent_blend.xyz), tangent.w));
#else
	out_tangent = vec4_to_vec2(vec4((tangent.xyz + tangent_blend.xyz) / blend_shape_count, tangent.w));
#endif
#endif // USE_TANGENT

#else // MODE_BLEND_PASS
	out_vertex = in_vertex * blend_weight;
#ifdef USE_NORMAL
	vec3 normal = oct_to_vec3(in_normal);
	out_normal = vec4_to_vec2(vec4(normal * blend_weight / blend_shape_count, 0.0));
#endif
#ifdef USE_TANGENT
	vec4 tangent = oct_to_tang(in_tangent);
	out_tangent = vec4_to_vec2(vec4(tangent.rgb * blend_weight / blend_shape_count, tangent.w));
#endif
#endif // MODE_BLEND_PASS
#else // USE_BLEND_SHAPES

	// Make attributes available to the skeleton shader if not written by blend shapes.
	out_vertex = in_vertex;
#ifdef USE_NORMAL
	out_normal = in_normal;
#endif
#ifdef USE_TANGENT
	out_tangent = in_tangent;
#endif
#endif // USE_BLEND_SHAPES

#ifdef USE_SKELETON

#define TEX_3D(m) texture2D(skeleton_texture, vec2((mod((m), 256.0) + 0.5) * 0.00390625, (floor((m) * 0.00390625) + 0.5) * skeleton_texture_inv_height))
#define GET_BONE_MATRIX_3D(a, b, c, w) mat4(TEX_3D(a), TEX_3D(b), TEX_3D(c), vec4(0.0, 0.0, 0.0, 1.0)) * w

	vec4 bones = in_bone_attrib * 3.0;
	vec4 bones_a = bones + 1.0;
	vec4 bones_b = bones + 2.0;

	highp mat4 m;
	m = GET_BONE_MATRIX_3D(bones.x, bones_a.x, bones_b.x, in_weight_attrib.x);
	m += GET_BONE_MATRIX_3D(bones.y, bones_a.y, bones_b.y, in_weight_attrib.y);
	m += GET_BONE_MATRIX_3D(bones.z, bones_a.z, bones_b.z, in_weight_attrib.z);
	m += GET_BONE_MATRIX_3D(bones.w, bones_a.w, bones_b.w, in_weight_attrib.w);

#ifdef USE_EIGHT_WEIGHTS
	bones = in_bone_attrib2 * 3.0;
	bones_a = bones + 1.0;
	bones_b = bones + 2.0;

	m += GET_BONE_MATRIX_3D(bones.x, bones_a.x, bones_b.x, in_weight_attrib2.x);
	m += GET_BONE_MATRIX_3D(bones.y, bones_a.y, bones_b.y, in_weight_attrib2.y);
	m += GET_BONE_MATRIX_3D(bones.z, bones_a.z, bones_b.z, in_weight_attrib2.z);
	m += GET_BONE_MATRIX_3D(bones.w, bones_a.w, bones_b.w, in_weight_attrib2.w);
#endif

	// Reverse order because its transposed.
	out_vertex = (vec4(out_vertex, 1.0) * m).xyz;
#ifdef USE_NORMAL
	vec3 vertex_normal = oct_to_vec3(out_normal);
	out_normal = vec3_to_oct(normalize((vec4(vertex_normal, 0.0) * m).xyz));
#endif // USE_NORMAL
#ifdef USE_TANGENT
	vec4 vertex_tangent = oct_to_tang(out_tangent);
	out_tangent = tang_to_oct(vec4(normalize((vec4(vertex_tangent.xyz, 0.0) * m).xyz), vertex_tangent.w));
#endif // USE_TANGENT
#endif // USE_SKELETON
#endif // MODE_2D

    // Satisfies the compiler's requirement for vertex geometry
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}

/* clang-format off */
#[fragment]

void main() {
#ifndef USE_TRANSFORM_FEEDBACK
    // Satisfies the compiler's requirement for a fragment output
	gl_FragColor = vec4(0.0);
#endif
}
/* clang-format on */