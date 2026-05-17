/* clang-format off */
#[modes]

mode_quad =
mode_texture_rect = #define USE_TEXTURE_RECT

#[specializations]

USE_FORCE_REPEAT = false
USE_ATTRIB_LIGHT_ANGLE = false
USE_ATTRIB_MODULATE = false
USE_ATTRIB_LARGE_VERTEX = false
USE_LIGHTING = true
USE_SHADOWS = false
USE_SKELETON = false
SHADOW_FILTER_NEAREST = false
SHADOW_FILTER_PCF3 = false
SHADOW_FILTER_PCF5 = false
SHADOW_FILTER_PCF7 = false
SHADOW_FILTER_PCF9 = false
SHADOW_FILTER_PCF13 = false
USE_INSTANCING = false
USE_INSTANCE_CUSTOM = false
USE_RGBA_SHADOWS = false

#[vertex]

uniform highp mat4 projection_matrix;
/* clang-format on */

#include "stdlib_inc.glsl"

uniform highp mat4 modelview_matrix;
uniform highp mat4 extra_matrix;

attribute vec2 vertex; // attrib:0

#ifdef USE_ATTRIB_LIGHT_ANGLE
// shared with tangent, not used in canvas shader
attribute float light_angle; // attrib:2
#endif

attribute vec4 color_attrib; // attrib:3
attribute vec2 uv_attrib; // attrib:4

#ifdef USE_ATTRIB_MODULATE
attribute vec4 modulate_attrib; // attrib:5
#endif

#ifdef USE_ATTRIB_LARGE_VERTEX
// shared with skeleton attributes, not used in batched shader
attribute vec2 translate_attrib; // attrib:6
attribute vec4 basis_attrib; // attrib:7
#endif

#ifdef USE_SKELETON
attribute vec4 bone_indices; // attrib:6
attribute vec4 bone_weights; // attrib:7
#endif

#ifdef USE_INSTANCING

attribute vec4 instance_xform0; //attrib:8
attribute vec4 instance_xform1; //attrib:9
attribute vec4 instance_xform2; //attrib:10
attribute vec4 instance_color; //attrib:11

#ifdef USE_INSTANCE_CUSTOM
attribute vec4 instance_custom_data; //attrib:12
#endif

#endif

#ifdef USE_SKELETON
uniform highp mat4 skeleton_transform;
uniform highp mat4 skeleton_transform_inverse;

#ifdef USE_SKELETON_UNIFORM
// Fallback for hardware without Vertex Texture Fetch
uniform highp mat4 bone_transforms[64]; 
#else
uniform highp sampler2D skeleton_texture; // texunit:-3
uniform highp ivec2 skeleton_texture_size;
#endif

#endif

varying highp vec2 uv_interp;
varying mediump vec4 color_interp;

#ifdef USE_ATTRIB_MODULATE
// modulate doesn't need interpolating but we need to send it to the fragment shader
varying mediump vec4 modulate_interp;
#endif

#ifdef MODULATE_USED
uniform mediump vec4 final_modulate;
#endif

uniform highp vec2 color_texture_pixel_size;

#ifdef USE_TEXTURE_RECT

uniform highp vec4 dst_rect;
uniform highp vec4 src_rect;

#endif

uniform highp float time;

#ifdef USE_LIGHTING

// light matrices
uniform highp mat4 light_matrix;
uniform highp mat4 light_matrix_inverse;
uniform highp mat4 light_local_matrix;
uniform highp mat4 shadow_matrix;
uniform highp vec4 light_color;
uniform highp vec4 light_shadow_color;
uniform highp vec2 light_pos;
uniform highp float shadowpixel_size;
uniform highp float shadow_gradient;
uniform highp float light_height;
uniform highp float light_outside_alpha;
uniform highp float shadow_distance_mult;

varying highp vec4 light_uv_interp;
varying highp vec2 transformed_light_uv;
varying highp vec4 local_rot;

varying highp vec2 pos;

const bool at_light_pass = true;
#else
const bool at_light_pass = false;
#endif

/* clang-format off */

#ifdef MATERIAL_UNIFORMS_USED
#MATERIAL_UNIFORMS
#endif

#GLOBALS

/* clang-format on */

vec2 select(vec2 a, vec2 b, bvec2 c) {
	vec2 ret;

	ret.x = c.x ? b.x : a.x;
	ret.y = c.y ? b.y : a.y;

	return ret;
}

void main() {
	vec4 color = color_attrib;
	vec2 uv;

#ifdef USE_INSTANCING
	mat4 extra_matrix_instance = extra_matrix * mat4(
		vec4(instance_xform0.x, instance_xform1.x, instance_xform2.x, 0.0),
		vec4(instance_xform0.y, instance_xform1.y, instance_xform2.y, 0.0),
		vec4(instance_xform0.z, instance_xform1.z, instance_xform2.z, 0.0),
		vec4(instance_xform0.w, instance_xform1.w, instance_xform2.w, 1.0)
	);
	color *= instance_color;

#ifdef USE_INSTANCE_CUSTOM
	vec4 instance_custom = instance_custom_data;
#else
	vec4 instance_custom = vec4(0.0);
#endif

#else
	mat4 extra_matrix_instance = extra_matrix;
	vec4 instance_custom = vec4(0.0);
#endif

#ifdef USE_TEXTURE_RECT

	if (dst_rect.z < 0.0) { // Transpose is encoded as negative dst_rect.z
		uv = src_rect.xy + src_rect.zw * vertex.yx;
	} else {
		uv = src_rect.xy + src_rect.zw * vertex;
	}

	vec4 outvec = vec4(0.0, 0.0, 0.0, 1.0);
	
	// We keep outvec strictly positive geometry
	outvec.xy = dst_rect.xy + abs(dst_rect.zw) * vertex;
#else
	vec4 outvec = vec4(vertex.xy, 0.0, 1.0);

	uv = uv_attrib;
#endif

	float point_size = 1.0;

	{
		vec2 src_vtx = outvec.xy;
		// Map Godot 4 built-ins for user shaders
		mat4 model_matrix = extra_matrix_instance;
		mat4 canvas_matrix = modelview_matrix;
		mat4 screen_matrix = projection_matrix;

		/* clang-format off */

#CODE : VERTEX

		/* clang-format on */
	}

	gl_PointSize = point_size;

#ifdef USE_ATTRIB_MODULATE
	// modulate doesn't need interpolating but we need to send it to the fragment shader
	modulate_interp = modulate_attrib;
#endif

#ifdef USE_ATTRIB_LARGE_VERTEX
	// transform is in attributes
	vec2 temp;

	temp = outvec.xy;
	temp.x = (outvec.x * basis_attrib.x) + (outvec.y * basis_attrib.z);
	temp.y = (outvec.x * basis_attrib.y) + (outvec.y * basis_attrib.w);

	temp += translate_attrib;
	outvec.xy = temp;

#else

	// transform is in uniforms
#if !defined(SKIP_TRANSFORM_USED)
	outvec = extra_matrix_instance * outvec;
	outvec = modelview_matrix * outvec;
#endif

#endif // not large integer

	color_interp = color;

#ifdef USE_PIXEL_SNAP
	outvec.xy = floor(outvec + 0.5).xy;
	// precision issue on some hardware creates artifacts within texture
	// offset uv by a small amount to avoid
	uv += 1e-5;
#endif

#ifdef USE_SKELETON
	// look up transform from the "pose texture"
	if (bone_weights != vec4(0.0)) {
		highp mat4 bone_transform = mat4(0.0);
		for (int i = 0; i < 4; i++) {
#ifdef USE_SKELETON_UNIFORM
			// Fast uniform array lookup
			highp mat4 b = bone_transforms[int(bone_indices[i])];
#else
			// Original VTF lookup
			ivec2 tex_ofs = ivec2(int(bone_indices[i]) * 2, 0);
			highp mat4 b = mat4(
					texel2DFetch(skeleton_texture, skeleton_texture_size, tex_ofs + ivec2(0, 0)),
					texel2DFetch(skeleton_texture, skeleton_texture_size, tex_ofs + ivec2(1, 0)),
					vec4(0.0, 0.0, 1.0, 0.0),
					vec4(0.0, 0.0, 0.0, 1.0));
#endif
			bone_transform += b * bone_weights[i];
		}

		mat4 bone_matrix = skeleton_transform * transpose(bone_transform) * skeleton_transform_inverse;
		outvec = bone_matrix * outvec;
	}
#endif

	uv_interp = uv;
	gl_Position = projection_matrix * outvec;

#ifdef USE_LIGHTING

	light_uv_interp.xy = (light_matrix * outvec).xy;
	light_uv_interp.zw = (light_local_matrix * outvec).xy;

	transformed_light_uv = (mat3(light_matrix_inverse) * vec3(light_uv_interp.zw, 0.0)).xy; //for normal mapping

	pos = outvec.xy;

#ifdef USE_ATTRIB_LIGHT_ANGLE
	// we add a fixed offset because we are using the sign later,
	// and don't want floating point error around 0.0
	float la = abs(light_angle) - 1.0;

	// vector light angle
	vec4 vla;
	vla.xy = vec2(cos(la), sin(la));
	vla.zw = vec2(-vla.y, vla.x);

	// vertical flip encoded in the sign
	vla.zw *= sign(light_angle);

	// apply the transform matrix.
	// The rotate will be encoded in the transform matrix for single rects,
	// and just the flips in the light angle.
	// For batching we will encode the rotation and the flips
	// in the light angle, and can use the same shader.
	local_rot.xy = normalize((modelview_matrix * (extra_matrix_instance * vec4(vla.xy, 0.0, 0.0))).xy);
	local_rot.zw = normalize((modelview_matrix * (extra_matrix_instance * vec4(vla.zw, 0.0, 0.0))).xy);
#else
	local_rot.xy = normalize((modelview_matrix * (extra_matrix_instance * vec4(1.0, 0.0, 0.0, 0.0))).xy);
	local_rot.zw = normalize((modelview_matrix * (extra_matrix_instance * vec4(0.0, 1.0, 0.0, 0.0))).xy);
#ifdef USE_TEXTURE_RECT
	local_rot.xy *= sign(src_rect.z);
	local_rot.zw *= sign(src_rect.w);
#endif
#endif // not using light angle

#endif
}

/* clang-format off */
#[fragment]

// texture2DLodEXT and textureCubeLodEXT are fragment shader specific.
// Do not copy these defines in the vertex section.
#ifndef USE_GLES_OVER_GL
#ifdef GL_EXT_shader_texture_lod
#extension GL_EXT_shader_texture_lod : enable
#define texture2DLod(img, coord, lod) texture2DLodEXT(img, coord, lod)
#define textureCubeLod(img, coord, lod) textureCubeLodEXT(img, coord, lod)
#endif
#endif // !USE_GLES_OVER_GL

#ifdef GL_ARB_shader_texture_lod
#extension GL_ARB_shader_texture_lod : enable
#endif

#if !defined(GL_EXT_shader_texture_lod) && !defined(GL_ARB_shader_texture_lod)
#define texture2DLod(img, coord, lod) texture2D(img, coord, lod)
#define textureCubeLod(img, coord, lod) textureCube(img, coord, lod)
#endif

#include "stdlib_inc.glsl"

uniform sampler2D color_texture; // texunit:0
/* clang-format on */
uniform highp vec2 color_texture_pixel_size;

// Re-mapped to avoid collision with light_texture (-6) and shadow_texture (-5)
uniform mediump sampler2D normal_texture; // texunit:-2
uniform mediump sampler2D specular_texture; // texunit:-1

// rgb = specular color, a = shininess
uniform mediump vec4 specular_shininess;

varying highp vec2 uv_interp;
varying mediump vec4 color_interp;

#ifdef USE_ATTRIB_MODULATE
varying mediump vec4 modulate_interp;
#endif

uniform highp float time;

uniform mediump vec4 final_modulate;

#ifdef SCREEN_TEXTURE_USED

uniform sampler2D screen_texture; // texunit:-4

#endif

#ifdef SCREEN_UV_USED

uniform vec2 screen_pixel_size;

#endif

#ifdef USE_LIGHTING

uniform highp mat4 light_matrix;
uniform highp mat4 light_local_matrix;
uniform highp mat4 shadow_matrix;
uniform highp vec4 light_color;
uniform highp vec4 light_shadow_color;
uniform highp vec2 light_pos;
uniform highp float shadowpixel_size;
uniform highp float shadow_gradient;
uniform highp float light_height;
uniform highp float light_outside_alpha;
uniform highp float shadow_distance_mult;
uniform highp float is_directional_light;
uniform highp float shadow_y_ofs;
uniform highp float shadow_zfar_inv;

uniform lowp sampler2D light_texture; // texunit:-6
varying highp vec4 light_uv_interp;
varying highp vec2 transformed_light_uv;

varying highp vec4 local_rot;
varying highp vec2 pos;

#ifdef USE_SHADOWS
uniform highp sampler2D shadow_texture; // texunit:-5
#endif

const bool at_light_pass = true;
#else
const bool at_light_pass = false;
#endif

uniform highp float use_default_normal;

/* clang-format off */

#ifdef MATERIAL_UNIFORMS_USED
#MATERIAL_UNIFORMS
#endif

#ifdef SDF_USED
uniform sampler2D sdf_texture; // texunit:-7
uniform highp mat2 screen_to_sdf;
uniform highp vec4 sdf_to_tex;
uniform highp float tex_to_sdf;
uniform highp mat2 sdf_to_screen;

#define SDF_MAX_LENGTH 16384.0

highp float vec4_to_float(highp vec4 p_vec) {
	return dot(p_vec, vec4(1.0 / (255.0 * 255.0 * 255.0), 1.0 / (255.0 * 255.0), 1.0 / 255.0, 1.0)) * 2.0 - 1.0;
}

highp vec2 screen_uv_to_sdf(highp vec2 p_uv) {
	return screen_to_sdf * p_uv;
}

highp float texture_sdf(highp vec2 p_sdf) {
	highp vec2 uv = p_sdf * sdf_to_tex.xy + sdf_to_tex.zw;
	highp float d = vec4_to_float(texture2D(sdf_texture, uv));
	d *= SDF_MAX_LENGTH;
	return d * tex_to_sdf;
}

highp vec2 texture_sdf_normal(highp vec2 p_sdf) {
	highp vec2 uv = p_sdf * sdf_to_tex.xy + sdf_to_tex.zw;

	const highp float EPSILON = 0.001;
	return normalize(vec2(
			vec4_to_float(texture2D(sdf_texture, uv + vec2(EPSILON, 0.0))) - vec4_to_float(texture2D(sdf_texture, uv - vec2(EPSILON, 0.0))),
			vec4_to_float(texture2D(sdf_texture, uv + vec2(0.0, EPSILON))) - vec4_to_float(texture2D(sdf_texture, uv - vec2(0.0, EPSILON)))));
}

highp vec2 sdf_to_screen_uv(highp vec2 p_sdf) {
	return p_sdf * sdf_to_screen;
}
#endif

#GLOBALS

/* clang-format on */

void light_compute(
		inout vec4 light,
		inout vec2 light_vec,
		inout float light_height,
		inout vec4 light_color,
		vec2 light_uv,
		inout vec4 shadow_color,
		inout vec2 shadow_vec,
		vec3 normal,
		vec2 uv,
#if defined(SCREEN_UV_USED)
		vec2 screen_uv,
#endif
		vec4 color) {

#if defined(LIGHT_CODE_USED)

	/* clang-format off */

#CODE : LIGHT

	/* clang-format on */

#endif
}

void main() {
	vec4 color = color_interp;
	vec2 uv = uv_interp;
#ifdef USE_FORCE_REPEAT
	//needs to use this to workaround GLES2/WebGL1 forcing tiling that textures that don't support it
	uv = mod(uv, vec2(1.0, 1.0));
#endif

#if !defined(COLOR_USED)
	//default behavior, texture by color
	color *= texture2D(color_texture, uv);
#endif

#ifdef SCREEN_UV_USED
	vec2 screen_uv = gl_FragCoord.xy * screen_pixel_size;
#endif

	vec3 normal;

#if defined(NORMAL_USED)

	bool normal_used = true;
#else
	bool normal_used = false;
#endif

	if (use_default_normal > 0.5) {
		normal.xy = texture2D(normal_texture, uv).xy * 2.0 - 1.0;
		normal.z = sqrt(max(0.0, 1.0 - dot(normal.xy, normal.xy)));
		normal_used = true;
	} else {
		normal = vec3(0.0, 0.0, 1.0);
	}

	{
		float normal_depth = 1.0;

#if defined(NORMALMAP_USED)
		vec3 normal_map = vec3(0.0, 0.0, 1.0);
		normal_used = true;
#endif

		/* clang-format off */

#CODE : FRAGMENT

		/* clang-format on */

#if defined(NORMALMAP_USED)
		normal = mix(vec3(0.0, 0.0, 1.0), normal_map * vec3(2.0, -2.0, 1.0) - vec3(1.0, -1.0, 0.0), normal_depth);
#endif
	}

#ifdef USE_ATTRIB_MODULATE
	color *= modulate_interp;
#else
#if !defined(MODULATE_USED)
	color *= final_modulate;
#endif
#endif

#ifdef USE_LIGHTING

	vec2 light_vec = transformed_light_uv;
	vec2 shadow_vec = transformed_light_uv;

	if (normal_used) {
		normal.xy = mat2(local_rot.xy, local_rot.zw) * normal.xy;
	}

	float att = 1.0;

	vec2 light_uv = light_uv_interp.xy;
	vec4 light = texture2D(light_texture, light_uv);

	if (is_directional_light < 0.5 && (any(lessThan(light_uv_interp.xy, vec2(0.0, 0.0))) || any(greaterThanEqual(light_uv_interp.xy, vec2(1.0, 1.0))))) {
		color.a *= light_outside_alpha; //invisible

	} else {
		float real_light_height = light_height;
		vec4 real_light_color = light_color;
		vec4 real_light_shadow_color = light_shadow_color;

#if defined(USE_LIGHT_SHADER_CODE)
		//light is written by the light shader
		light_compute(
				light,
				light_vec,
				real_light_height,
				real_light_color,
				light_uv,
				real_light_shadow_color,
				shadow_vec,
				normal,
				uv,
#if defined(SCREEN_UV_USED)
				screen_uv,
#endif
				color);
#endif

		light *= real_light_color;

		if (normal_used) {
			vec3 light_pos_3d = vec3(light_pos, real_light_height);
			vec3 p = vec3(pos, 0.0);
			vec3 light_dir;
			if (is_directional_light > 0.5) {
				light_dir = normalize(mix(vec3(light_pos_3d.xy, 0.0), vec3(0.0, 0.0, 1.0), real_light_height));
			} else {
				light_dir = normalize(light_pos_3d - p);
			}
			
			float cNdotL = max(dot(normal, light_dir), 0.0);
			light *= cNdotL;
			
			if (specular_shininess.a > 0.0) {
				vec3 view = vec3(0.0, 0.0, 1.0);
				vec3 half_vec = normalize(view + light_dir);
				float cNdotV = max(dot(normal, view), 0.0);
				float cNdotH = max(dot(normal, half_vec), 0.0);
				float shininess = exp2(15.0 * specular_shininess.a + 1.0) * 0.25;
				float blinn = pow(cNdotH, shininess);
				blinn *= (shininess + 8.0) * (1.0 / (8.0 * 3.141592653589793));
				float s = blinn / max(4.0 * cNdotV * cNdotL, 0.75);
				light.rgb += specular_shininess.rgb * light.rgb * s;
			}
		}

		color *= light;

#ifdef USE_SHADOWS

		vec2 shadow_pos = (shadow_matrix * vec4(pos, 0.0, 1.0)).xy;
		float su, sz;
		float sh = shadow_y_ofs;

		if (is_directional_light > 0.5) {
			su = shadow_pos.x;
			sz = shadow_pos.y * shadow_zfar_inv;
		} else {
			vec2 pos_norm = normalize(shadow_pos);
			vec2 pos_abs = abs(pos_norm);
			vec2 pos_box = pos_norm / max(pos_abs.x, pos_abs.y);
			vec2 pos_rot = pos_norm * mat2(vec2(0.7071067811865476, -0.7071067811865476), vec2(0.7071067811865476, 0.7071067811865476));
			float tex_ofs;
			float dist;
			if (pos_rot.y > 0.0) {
				if (pos_rot.x > 0.0) {
					tex_ofs = pos_box.y * 0.125 + 0.125;
					dist = shadow_pos.x;
				} else {
					tex_ofs = pos_box.x * -0.125 + (0.25 + 0.125);
					dist = shadow_pos.y;
				}
			} else {
				if (pos_rot.x < 0.0) {
					tex_ofs = pos_box.y * -0.125 + (0.5 + 0.125);
					dist = -shadow_pos.x;
				} else {
					tex_ofs = pos_box.x * 0.125 + (0.75 + 0.125);
					dist = -shadow_pos.y;
				}
			}
			su = tex_ofs;
			sz = dist * shadow_zfar_inv;
		}

		highp float shadow_attenuation = 0.0;

#ifdef USE_RGBA_SHADOWS
#define SHADOW_DEPTH(m_tex, m_uv) (dot(texture2D((m_tex), (m_uv)), vec4(1.0 / (255.0 * 255.0 * 255.0), 1.0 / (255.0 * 255.0), 1.0 / 255.0, 1.0)) * 2.0 - 1.0)
#else
#define SHADOW_DEPTH(m_tex, m_uv) (texture2D((m_tex), (m_uv)).r)
#endif

#ifdef SHADOW_USE_GRADIENT

		/* clang-format off */
		/* GLSL es 100 doesn't support line continuation characters(backslashes) */
#define SHADOW_TEST(m_ofs) { highp float sd = SHADOW_DEPTH(shadow_texture, vec2(m_ofs, sh)); shadow_attenuation += 1.0 - smoothstep(sd, sd + shadow_gradient, sz); }

#else

#define SHADOW_TEST(m_ofs) { highp float sd = SHADOW_DEPTH(shadow_texture, vec2(m_ofs, sh)); shadow_attenuation += step(sz, sd); }
		/* clang-format on */

#endif

#ifdef SHADOW_FILTER_NEAREST

		SHADOW_TEST(su);

#endif

#ifdef SHADOW_FILTER_PCF3

		SHADOW_TEST(su + shadowpixel_size);
		SHADOW_TEST(su);
		SHADOW_TEST(su - shadowpixel_size);
		shadow_attenuation /= 3.0;

#endif

#ifdef SHADOW_FILTER_PCF5

		SHADOW_TEST(su + shadowpixel_size * 2.0);
		SHADOW_TEST(su + shadowpixel_size);
		SHADOW_TEST(su);
		SHADOW_TEST(su - shadowpixel_size);
		SHADOW_TEST(su - shadowpixel_size * 2.0);
		shadow_attenuation /= 5.0;

#endif

#ifdef SHADOW_FILTER_PCF7

		SHADOW_TEST(su + shadowpixel_size * 3.0);
		SHADOW_TEST(su + shadowpixel_size * 2.0);
		SHADOW_TEST(su + shadowpixel_size);
		SHADOW_TEST(su);
		SHADOW_TEST(su - shadowpixel_size);
		SHADOW_TEST(su - shadowpixel_size * 2.0);
		SHADOW_TEST(su - shadowpixel_size * 3.0);
		shadow_attenuation /= 7.0;

#endif

#ifdef SHADOW_FILTER_PCF9

		SHADOW_TEST(su + shadowpixel_size * 4.0);
		SHADOW_TEST(su + shadowpixel_size * 3.0);
		SHADOW_TEST(su + shadowpixel_size * 2.0);
		SHADOW_TEST(su + shadowpixel_size);
		SHADOW_TEST(su);
		SHADOW_TEST(su - shadowpixel_size);
		SHADOW_TEST(su - shadowpixel_size * 2.0);
		SHADOW_TEST(su - shadowpixel_size * 3.0);
		SHADOW_TEST(su - shadowpixel_size * 4.0);
		shadow_attenuation /= 9.0;

#endif

#ifdef SHADOW_FILTER_PCF13

		SHADOW_TEST(su + shadowpixel_size * 6.0);
		SHADOW_TEST(su + shadowpixel_size * 5.0);
		SHADOW_TEST(su + shadowpixel_size * 4.0);
		SHADOW_TEST(su + shadowpixel_size * 3.0);
		SHADOW_TEST(su + shadowpixel_size * 2.0);
		SHADOW_TEST(su + shadowpixel_size);
		SHADOW_TEST(su);
		SHADOW_TEST(su - shadowpixel_size);
		SHADOW_TEST(su - shadowpixel_size * 2.0);
		SHADOW_TEST(su - shadowpixel_size * 3.0);
		SHADOW_TEST(su - shadowpixel_size * 4.0);
		SHADOW_TEST(su - shadowpixel_size * 5.0);
		SHADOW_TEST(su - shadowpixel_size * 6.0);
		shadow_attenuation /= 13.0;

#endif

		//color *= shadow_attenuation;
		color = mix(real_light_shadow_color, color, shadow_attenuation);
//use shadows
#endif
	}

//use lighting
#endif

	gl_FragColor = color;
}