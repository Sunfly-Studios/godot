/* clang-format off */
#[modes]

mode_color = #define BASE_PASS
mode_color_instancing = #define BASE_PASS \n#define USE_INSTANCING
mode_color_matrix_palette = #define BASE_PASS \n#define USE_FAT_VERTEX
mode_additive = #define USE_ADDITIVE_LIGHTING
mode_additive_instancing = #define USE_ADDITIVE_LIGHTING \n#define USE_INSTANCING
mode_depth = #define MODE_RENDER_DEPTH
mode_depth_instancing = #define MODE_RENDER_DEPTH \n#define USE_INSTANCING

#[specializations]

DISABLE_LIGHTMAP = false
DISABLE_LIGHT_DIRECTIONAL = false
DISABLE_LIGHT_OMNI = false
DISABLE_LIGHT_SPOT = false
DISABLE_FOG = false
USE_RADIANCE_MAP = true

#[vertex]

// IWYU: select
#define SELECT_USED
#define SHADER_IS_SRGB true

#include "stdlib_inc.glsl"

#if !defined(MODE_RENDER_DEPTH) || defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED) ||defined(LIGHT_CLEARCOAT_USED)
#ifndef NORMAL_USED
#define NORMAL_USED
#endif
#endif

/*
from RenderingServer:
ARRAY_VERTEX = 0, // RG32F or RGB32F (depending on 2D bit)
ARRAY_NORMAL = 1, // RG16 octahedral compression
ARRAY_TANGENT = 2, // RG16 octahedral compression, sign stored in sign of G
ARRAY_COLOR = 3, // RGBA8
ARRAY_TEX_UV = 4, // RG32F
ARRAY_TEX_UV2 = 5, // RG32F
ARRAY_CUSTOM0 = 6, // Depends on ArrayCustomFormat.
ARRAY_CUSTOM1 = 7,
ARRAY_CUSTOM2 = 8,
ARRAY_CUSTOM3 = 9,
ARRAY_BONES = 10, // RGBA16UI (x2 if 8 weights)
ARRAY_WEIGHTS = 11, // RGBA16UNORM (x2 if 8 weights)
*/

/* INPUT ATTRIBS */

attribute highp vec3 vertex_attrib; // attrib:0
/* clang-format on */

#ifdef NORMAL_USED
attribute vec2 normal_attrib; // attrib:1
#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
attribute vec2 tangent_attrib; // attrib:2
#endif

#if defined(COLOR_USED)
attribute vec4 color_attrib; // attrib:3
#endif

#ifdef UV_USED
attribute vec2 uv_attrib; // attrib:4
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP)
attribute vec2 uv2_attrib; // attrib:5
#endif

#if defined(CUSTOM0_USED)
attribute vec4 custom0_attrib; // attrib:6
#endif

#if defined(CUSTOM1_USED)
attribute vec4 custom1_attrib; // attrib:7
#endif

#if defined(CUSTOM2_USED)
attribute vec4 custom2_attrib; // attrib:8
#endif

#if defined(CUSTOM3_USED)
attribute vec4 custom3_attrib; // attrib:9
#endif

#if defined(BONES_USED)
attribute vec4 bone_attrib; // attrib:10
#endif

#if defined(WEIGHTS_USED)
attribute vec4 weight_attrib; // attrib:11
#endif

vec3 oct_to_vec3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-v.z, 0.0);
	v.xy += t * -sign(v.xy);
	return normalize(v);
}

#if defined(USE_INSTANCING) || defined(USE_FAT_VERTEX)
attribute highp vec4 instance_xform0; // attrib:12
attribute highp vec4 instance_xform1; // attrib:13
attribute highp vec4 instance_xform2; // attrib:14
#endif

#ifdef USE_INSTANCING
attribute highp vec4 instance_color_custom_data; // attrib:15
#endif

uniform vec4 global_shader_uniforms[MAX_GLOBAL_SHADER_UNIFORMS];

uniform highp mat4 projection_matrix;
uniform highp mat4 inv_projection_matrix;
uniform highp mat4 inv_view_matrix;
uniform highp mat4 view_matrix;

// Following GLES3: These are only used for
// billboards to cast correct shadows
uniform highp mat4 main_cam_inv_view_matrix;

uniform vec2 viewport_size;
uniform vec2 screen_pixel_size;

uniform mediump vec4 ambient_light_color_energy;

uniform mediump float ambient_color_sky_mix;
uniform bool material_uv2_mode;
uniform float emissive_exposure_normalization;
uniform bool use_ambient_light;
uniform bool use_ambient_cubemap;
uniform bool use_reflection_cubemap;

uniform float fog_aerial_perspective;
uniform float time;

uniform mat3 radiance_inverse_xform;

uniform int directional_light_count;
uniform float z_far;
uniform float z_near;
uniform float IBL_exposure_normalization;

uniform bool fog_enabled;
uniform float fog_density;
uniform float fog_height;
uniform float fog_height_density;

uniform vec3 fog_light_color;
uniform float fog_sun_scatter;
uniform int camera_visible_layers;

uniform highp mat4 world_transform;

#ifdef USE_LIGHTMAP
uniform highp vec4 lightmap_uv_rect;
#endif

/* Varyings */

varying highp vec3 vertex_interp;
#ifdef NORMAL_USED
varying vec3 normal_interp;
#endif

#if defined(COLOR_USED)
varying vec4 color_interp;
#endif

#if defined(UV_USED)
varying vec2 uv_interp;
#endif

#if defined(UV2_USED)
varying vec2 uv2_interp;
#else
#ifdef USE_LIGHTMAP
varying vec2 uv2_interp;
#endif
#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
varying vec3 tangent_interp;
varying vec3 binormal_interp;
#endif

#ifdef MATERIAL_UNIFORMS_USED

/* clang-format off */
// flat uniforms:
#MATERIAL_UNIFORMS
/* clang-format on */

#endif

/* clang-format off */

#GLOBALS

/* clang-format on */
invariant gl_Position;

void main() {
	highp vec3 vertex = vertex_attrib;

	highp mat4 model_matrix = world_transform;
#if defined(USE_INSTANCING) || defined(USE_FAT_VERTEX)
	// GLES2 doesn't have transpose(). We manually transpose the incoming row vectors.
	highp mat4 m = mat4(
		vec4(instance_xform0.x, instance_xform1.x, instance_xform2.x, 0.0),
		vec4(instance_xform0.y, instance_xform1.y, instance_xform2.y, 0.0),
		vec4(instance_xform0.z, instance_xform1.z, instance_xform2.z, 0.0),
		vec4(instance_xform0.w, instance_xform1.w, instance_xform2.w, 1.0)
	);
	model_matrix = model_matrix * m;
#endif

#ifdef NORMAL_USED
	vec3 normal = oct_to_vec3(normal_attrib * 2.0 - 1.0);
#endif
	highp mat3 model_normal_matrix = mat3(model_matrix);

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
	vec2 signed_tangent_attrib = tangent_attrib * 2.0 - 1.0;
	vec3 tangent = oct_to_vec3(vec2(signed_tangent_attrib.x, abs(signed_tangent_attrib.y) * 2.0 - 1.0));
	float binormalf = sign(signed_tangent_attrib.y);
	vec3 binormal = normalize(cross(normal, tangent) * binormalf);
#endif

#if defined(COLOR_USED)
	color_interp = color_attrib;
#ifdef USE_INSTANCING
	vec4 instance_color = instance_color_custom_data;
	color_interp *= instance_color;
#endif
#endif

#if defined(UV_USED)
	uv_interp = uv_attrib;
#endif

#ifdef USE_LIGHTMAP
	uv2_interp = lightmap_uv_rect.zw * uv2_attrib + lightmap_uv_rect.xy;
#else
#if defined(UV2_USED)
	uv2_interp = uv2_attrib;
#endif
#endif

#if defined(OVERRIDE_POSITION)
	highp vec4 position;
#endif

	mat4 projection_matrix_local = projection_matrix;
	mat4 inv_projection_matrix_local = inv_projection_matrix;
	vec3 eye_offset = vec3(0.0, 0.0, 0.0);

	vec4 instance_custom = vec4(0.0);

	// Using world coordinates
#if !defined(SKIP_TRANSFORM_USED) && defined(VERTEX_WORLD_COORDS_USED)

	vertex = (model_matrix * vec4(vertex, 1.0)).xyz;

#ifdef NORMAL_USED
	normal = model_normal_matrix * normal;
#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)

	tangent = model_normal_matrix * tangent;
	binormal = model_normal_matrix * binormal;

#endif
#endif

	float roughness = 1.0;

	highp mat4 modelview = view_matrix * model_matrix;
	highp mat3 modelview_normal = mat3(view_matrix) * model_normal_matrix;

	float point_size = 1.0;

	{
#CODE : VERTEX
	}

	gl_PointSize = point_size;

	// Using local coordinates (default)
#if !defined(SKIP_TRANSFORM_USED) && !defined(VERTEX_WORLD_COORDS_USED)

	vertex = (modelview * vec4(vertex, 1.0)).xyz;
#ifdef NORMAL_USED
	normal = modelview_normal * normal;
#endif

#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)

	binormal = modelview_normal * binormal;
	tangent = modelview_normal * tangent;
#endif

	// Using world coordinates
#if !defined(SKIP_TRANSFORM_USED) && defined(VERTEX_WORLD_COORDS_USED)

	vertex = (view_matrix * vec4(vertex, 1.0)).xyz;
#ifdef NORMAL_USED
	normal = (view_matrix * vec4(normal, 0.0)).xyz;
#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
	binormal = (view_matrix * vec4(binormal, 0.0)).xyz;
	tangent = (view_matrix * vec4(tangent, 0.0)).xyz;
#endif
#endif

	vertex_interp = vertex;
#ifdef NORMAL_USED
	normal_interp = normal;
#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
	tangent_interp = tangent;
	binormal_interp = binormal;
#endif

#if defined(OVERRIDE_POSITION)
	gl_Position = position;
#else
	gl_Position = projection_matrix_local * vec4(vertex_interp, 1.0);
#endif
}

/* clang-format off */
#[fragment]

// Default to SPECULAR_SCHLICK_GGX.
#if !defined(SPECULAR_DISABLED) && !defined(SPECULAR_SCHLICK_GGX) && !defined(SPECULAR_TOON)
#define SPECULAR_SCHLICK_GGX
#endif

#if !defined(MODE_RENDER_DEPTH) || defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED) ||defined(LIGHT_CLEARCOAT_USED)
#ifndef NORMAL_USED
#define NORMAL_USED
#endif
#endif

// texture2DLodEXT and textureCubeLodEXT are fragment shader specific.
// Do not copy these defines in the vertex section.
#ifndef USE_GLES_OVER_GL
#ifdef GL_EXT_shader_texture_lod
#extension GL_EXT_shader_texture_lod : enable
#define texture2DLod(img, coord, lod) texture2DLodEXT(img, coord, lod)
#define textureCubeLod(img, coord, lod) textureCubeLodEXT(img, coord, lod)
#endif
#endif // !USE_GLES_OVER_GL

#if !defined(GL_EXT_shader_texture_lod)
#define texture2DLod(img, coord, lod) texture2D(img, coord, lod)
#define textureCubeLod(img, coord, lod) textureCube(img, coord, lod)
#endif

// Replace standard GLES3 texture functions with GLES2 equivalents
#define texture texture2D
#define textureCube textureCube

#ifndef MODE_RENDER_DEPTH
#include "tonemap_inc.glsl"
#endif

// IWYU: select
#define SELECT_USED

#include "stdlib_inc.glsl"

/* texture unit usage, N is max_texture_unity-N

1-color correction // In tonemap_inc.glsl
2-radiance
3-directional_shadow
4-positional_shadow
5-screen
6-depth

*/

/* clang-format on */

#define SHADER_IS_SRGB true

/* Varyings */

#if defined(COLOR_USED)
varying vec4 color_interp;
#endif

#if defined(UV_USED)
varying vec2 uv_interp;
#endif

#if defined(UV2_USED)
varying vec2 uv2_interp;
#else
#ifdef USE_LIGHTMAP
varying vec2 uv2_interp;
#endif
#endif

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
varying vec3 tangent_interp;
varying vec3 binormal_interp;
#endif

#ifdef NORMAL_USED
varying vec3 normal_interp;
#endif

varying highp vec3 vertex_interp;

#ifdef USE_RADIANCE_MAP

#define RADIANCE_MAX_LOD 5.0

uniform samplerCube radiance_map; // texunit:-2

#endif

uniform vec4 global_shader_uniforms[MAX_GLOBAL_SHADER_UNIFORMS];

/* Material Uniforms */

#ifdef MATERIAL_UNIFORMS_USED

/* clang-format off */
// flat uniforms:
#MATERIAL_UNIFORMS

/* clang-format on */

#endif

uniform highp mat4 projection_matrix;
uniform highp mat4 inv_projection_matrix;
uniform highp mat4 inv_view_matrix;
uniform highp mat4 view_matrix;

// Following GLES3: These are only used for
// billboards to cast correct shadows
uniform highp mat4 main_cam_inv_view_matrix;

uniform vec2 viewport_size;
uniform vec2 screen_pixel_size;

uniform mediump vec4 ambient_light_color_energy;

uniform mediump float ambient_color_sky_mix;
uniform bool material_uv2_mode;
uniform float emissive_exposure_normalization;
uniform bool use_ambient_light;
uniform bool use_ambient_cubemap;
uniform bool use_reflection_cubemap;

uniform float fog_aerial_perspective;
uniform float time;

uniform mat3 radiance_inverse_xform;

uniform int directional_light_count;
uniform float z_far;
uniform float z_near;
uniform float IBL_exposure_normalization;

uniform bool fog_enabled;
uniform float fog_density;
uniform float fog_height;
uniform float fog_height_density;

uniform vec3 fog_light_color;
uniform float fog_sun_scatter;
uniform int camera_visible_layers;

uniform float exposure;
uniform float white;

/* clang-format off */

#GLOBALS

/* clang-format on */

// Directional light data.
#ifndef DISABLE_LIGHT_DIRECTIONAL

struct DirectionalLightData {
	highp vec4 direction_energy;
	highp vec4 color_size;
	bool enabled;
};

struct DirectionalLights { 
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
};
uniform DirectionalLights directional_lights;

#endif // !DISABLE_LIGHT_DIRECTIONAL

// Omni and spot light data.
#if !defined(DISABLE_LIGHT_OMNI) || !defined(DISABLE_LIGHT_SPOT)

struct LightData { 
	highp vec4 position_inv_radius;
	highp vec4 direction_size;
	highp vec4 color_attenuation;
	highp vec4 cone_attenuation_angle_specular_shadow;
};

#ifndef DISABLE_LIGHT_OMNI
struct OmniLights {
	LightData data[MAX_FORWARD_LIGHTS];
};
uniform OmniLights omni_lights;
uniform int omni_light_count;
#endif

#ifndef DISABLE_LIGHT_SPOT
struct SpotLights {
	LightData data[MAX_FORWARD_LIGHTS];
};
uniform SpotLights spot_lights;
uniform int spot_light_count;
#endif

#ifdef USE_ADDITIVE_LIGHTING
uniform highp samplerCube positional_shadow; // texunit:-4
#endif

#endif // !defined(DISABLE_LIGHT_OMNI) || !defined(DISABLE_LIGHT_SPOT)

uniform highp sampler2D depth_buffer; // texunit:-6
uniform highp sampler2D color_buffer; // texunit:-5

uniform highp mat4 world_transform;
uniform mediump float opaque_prepass_threshold;

vec4 frag_color; // Maps to gl_FragColor at end of main()

vec3 F0(float metallic, float specular, vec3 albedo) {
	float dielectric = 0.16 * specular * specular;
	return mix(vec3(dielectric), albedo, vec3(metallic));
}

#if !defined(DISABLE_LIGHT_DIRECTIONAL) || !defined(DISABLE_LIGHT_OMNI) || !defined(DISABLE_LIGHT_SPOT)

float D_GGX(float cos_theta_m, float alpha) {
	float a = cos_theta_m * alpha;
	float k = alpha / (1.0 - cos_theta_m * cos_theta_m + a * a);
	return k * k * (1.0 / M_PI);
}

float V_GGX(float NdotL, float NdotV, float alpha) {
	return 0.5 / mix(2.0 * NdotL * NdotV, NdotL + NdotV, alpha);
}

float D_GGX_anisotropic(float cos_theta_m, float alpha_x, float alpha_y, float cos_phi, float sin_phi) {
	float alpha2 = alpha_x * alpha_y;
	highp vec3 v = vec3(alpha_y * cos_phi, alpha_x * sin_phi, alpha2 * cos_theta_m);
	highp float v2 = dot(v, v);
	float w2 = alpha2 / v2;
	float D = alpha2 * w2 * w2 * (1.0 / M_PI);
	return D;
}

float V_GGX_anisotropic(float alpha_x, float alpha_y, float TdotV, float TdotL, float BdotV, float BdotL, float NdotV, float NdotL) {
	float Lambda_V = NdotL * length(vec3(alpha_x * TdotV, alpha_y * BdotV, NdotV));
	float Lambda_L = NdotV * length(vec3(alpha_x * TdotL, alpha_y * BdotL, NdotL));
	return 0.5 / (Lambda_V + Lambda_L);
}

float SchlickFresnel(float u) {
	float m = 1.0 - u;
	float m2 = m * m;
	return m2 * m2 * m; 
}

void light_compute(vec3 N, vec3 L, vec3 V, float A, vec3 light_color, float attenuation, vec3 f0, float roughness, float metallic, float specular_amount, vec3 albedo, inout float alpha,
#ifdef LIGHT_BACKLIGHT_USED
		vec3 backlight,
#endif
#ifdef LIGHT_RIM_USED
		float rim, float rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		float clearcoat, float clearcoat_roughness, vec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		vec3 B, vec3 T, float anisotropy,
#endif
		inout vec3 diffuse_light, inout vec3 specular_light) {

#if defined(USE_LIGHT_SHADER_CODE)
	vec3 normal = N;
	vec3 light = L;
	vec3 view = V;

	/* clang-format off */

#CODE : LIGHT

	/* clang-format on */

#else
	float NdotL = min(A + dot(N, L), 1.0);
	float cNdotL = max(NdotL, 0.0); 
	float NdotV = dot(N, V);
	float cNdotV = max(NdotV, 1e-4);

#if defined(DIFFUSE_BURLEY) || defined(SPECULAR_SCHLICK_GGX) || defined(LIGHT_CLEARCOAT_USED)
	vec3 H = normalize(V + L);
#endif

#if defined(SPECULAR_SCHLICK_GGX)
	float cNdotH = clamp(A + dot(N, H), 0.0, 1.0);
#endif

#if defined(DIFFUSE_BURLEY) || defined(SPECULAR_SCHLICK_GGX) || defined(LIGHT_CLEARCOAT_USED)
	float cLdotH = clamp(A + dot(L, H), 0.0, 1.0);
#endif

	if (metallic < 1.0) {
		float diffuse_brdf_NL; 

#if defined(DIFFUSE_LAMBERT_WRAP)
		diffuse_brdf_NL = max(0.0, (NdotL + roughness) / ((1.0 + roughness) * (1.0 + roughness))) * (1.0 / M_PI);
#elif defined(DIFFUSE_TOON)
		diffuse_brdf_NL = smoothstep(-roughness, max(roughness, 0.01), NdotL) * (1.0 / M_PI);
#elif defined(DIFFUSE_BURLEY)
		{
			float FD90_minus_1 = 2.0 * cLdotH * cLdotH * roughness - 0.5;
			float FdV = 1.0 + FD90_minus_1 * SchlickFresnel(cNdotV);
			float FdL = 1.0 + FD90_minus_1 * SchlickFresnel(cNdotL);
			diffuse_brdf_NL = (1.0 / M_PI) * FdV * FdL * cNdotL;
		}
#else
		diffuse_brdf_NL = cNdotL * (1.0 / M_PI);
#endif

		diffuse_light += light_color * diffuse_brdf_NL * attenuation;

#if defined(LIGHT_BACKLIGHT_USED)
		diffuse_light += light_color * (vec3(1.0 / M_PI) - diffuse_brdf_NL) * backlight * attenuation;
#endif

#if defined(LIGHT_RIM_USED)
		float rim_light = pow(max(1e-4, 1.0 - cNdotV), max(0.0, (1.0 - roughness) * 16.0));
		diffuse_light += rim_light * rim * mix(vec3(1.0), albedo, rim_tint) * light_color;
#endif
	}

	if (roughness > 0.0) { 

#if defined(SPECULAR_TOON)

		vec3 R = normalize(-reflect(L, N));
		float RdotV = dot(R, V);
		float mid = 1.0 - roughness;
		mid *= mid;
		float intensity = smoothstep(mid - roughness * 0.5, mid + roughness * 0.5, RdotV) * mid;
		diffuse_light += light_color * intensity * attenuation * specular_amount;

#elif defined(SPECULAR_DISABLED)
		// none..

#elif defined(SPECULAR_SCHLICK_GGX)
		float alpha_ggx = roughness * roughness;
#if defined(LIGHT_ANISOTROPY_USED)
		float aspect = sqrt(1.0 - anisotropy * 0.9);
		float ax = alpha_ggx / aspect;
		float ay = alpha_ggx * aspect;
		float XdotH = dot(T, H);
		float YdotH = dot(B, H);
		float D = D_GGX_anisotropic(cNdotH, ax, ay, XdotH, YdotH);
		float G = V_GGX_anisotropic(ax, ay, dot(T, V), dot(T, L), dot(B, V), dot(B, L), cNdotV, cNdotL);
#else
		float D = D_GGX(cNdotH, alpha_ggx);
		float G = V_GGX(cNdotL, cNdotV, alpha_ggx);
#endif // LIGHT_ANISOTROPY_USED
		float cLdotH5 = SchlickFresnel(cLdotH);
		float f90 = clamp(50.0 * f0.g, 0.0, 1.0);
		vec3 F = f0 + (f90 - f0) * cLdotH5;

		vec3 specular_brdf_NL = cNdotL * D * F * G;

		specular_light += specular_brdf_NL * light_color * attenuation * specular_amount;
#endif

#if defined(LIGHT_CLEARCOAT_USED)
		float ccNdotL = max(min(A + dot(vertex_normal, L), 1.0), 0.0);
		float ccNdotH = clamp(A + dot(vertex_normal, H), 0.0, 1.0);
		float ccNdotV = max(dot(vertex_normal, V), 1e-4);

#if !defined(SPECULAR_SCHLICK_GGX)
		float cLdotH5 = SchlickFresnel(cLdotH);
#endif
		float Dr = D_GGX(ccNdotH, mix(0.001, 0.1, clearcoat_roughness));
		float Gr = 0.25 / (cLdotH * cLdotH);
		float Fr = mix(.04, 1.0, cLdotH5);
		float clearcoat_specular_brdf_NL = clearcoat * Gr * Fr * Dr * cNdotL;

		specular_light += clearcoat_specular_brdf_NL * light_color * attenuation * specular_amount;
#endif // LIGHT_CLEARCOAT_USED
	}

#ifdef USE_SHADOW_TO_OPACITY
	alpha = min(alpha, clamp(1.0 - attenuation, 0.0, 1.0));
#endif

#endif // LIGHT_CODE_USED
}

float get_omni_spot_attenuation(float distance, float inv_range, float decay) {
	float nd = distance * inv_range;
	nd *= nd;
	nd *= nd; // nd^4
	nd = max(1.0 - nd, 0.0);
	nd *= nd; // nd^2
	return nd * pow(max(distance, 0.0001), -decay);
}

#ifndef DISABLE_LIGHT_OMNI
void light_process_omni(LightData light, vec3 vertex, vec3 eye_vec, vec3 normal, vec3 f0, float roughness, float metallic, float shadow, vec3 albedo, inout float alpha,
#ifdef LIGHT_BACKLIGHT_USED
		vec3 backlight,
#endif
#ifdef LIGHT_RIM_USED
		float rim, float rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		float clearcoat, float clearcoat_roughness, vec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		vec3 binormal, vec3 tangent, float anisotropy,
#endif
		inout vec3 diffuse_light, inout vec3 specular_light) {
	vec3 light_rel_vec = light.position_inv_radius.xyz - vertex;
	float light_length = length(light_rel_vec);
	float omni_attenuation = get_omni_spot_attenuation(light_length, light.position_inv_radius.w, light.color_attenuation.w);
	vec3 color = light.color_attenuation.xyz;
	float size_A = 0.0;

	if (light.direction_size.w > 0.0) {
		float t = light.direction_size.w / max(0.001, light_length);
		size_A = max(0.0, 1.0 - 1.0 / sqrt(1.0 + t * t));
	}

	light_compute(normal, normalize(light_rel_vec), eye_vec, size_A, color, omni_attenuation, f0, roughness, metallic, light.cone_attenuation_angle_specular_shadow.z, albedo, alpha,
#ifdef LIGHT_BACKLIGHT_USED
			backlight,
#endif
#ifdef LIGHT_RIM_USED
			rim * omni_attenuation, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
			clearcoat, clearcoat_roughness, vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
			binormal, tangent, anisotropy,
#endif
			diffuse_light,
			specular_light);
}
#endif // !DISABLE_LIGHT_OMNI

#ifndef DISABLE_LIGHT_SPOT
void light_process_spot(LightData light, vec3 vertex, vec3 eye_vec, vec3 normal, vec3 f0, float roughness, float metallic, float shadow, vec3 albedo, inout float alpha,
#ifdef LIGHT_BACKLIGHT_USED
		vec3 backlight,
#endif
#ifdef LIGHT_RIM_USED
		float rim, float rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
		float clearcoat, float clearcoat_roughness, vec3 vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
		vec3 binormal, vec3 tangent, float anisotropy,
#endif
		inout vec3 diffuse_light,
		inout vec3 specular_light) {

	vec3 light_rel_vec = light.position_inv_radius.xyz - vertex;
	float light_length = length(light_rel_vec);
	float spot_attenuation = get_omni_spot_attenuation(light_length, light.position_inv_radius.w, light.color_attenuation.w);
	vec3 spot_dir = light.direction_size.xyz;
	float scos = max(dot(-normalize(light_rel_vec), spot_dir), light.cone_attenuation_angle_specular_shadow.y);
	float spot_rim = max(0.0001, (1.0 - scos) / (1.0 - light.cone_attenuation_angle_specular_shadow.y));
	spot_attenuation *= 1.0 - pow(spot_rim, light.cone_attenuation_angle_specular_shadow.x);
	vec3 color = light.color_attenuation.xyz;

	float size_A = 0.0;

	if (light.direction_size.w > 0.0) {
		float t = light.direction_size.w / max(0.001, light_length);
		size_A = max(0.0, 1.0 - 1.0 / sqrt(1.0 + t * t));
	}

	light_compute(normal, normalize(light_rel_vec), eye_vec, size_A, color, spot_attenuation, f0, roughness, metallic, light.cone_attenuation_angle_specular_shadow.z, albedo, alpha,
#ifdef LIGHT_BACKLIGHT_USED
			backlight,
#endif
#ifdef LIGHT_RIM_USED
			rim * spot_attenuation, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
			clearcoat, clearcoat_roughness, vertex_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
			binormal, tangent, anisotropy,
#endif
			diffuse_light, specular_light);
}
#endif // !DISABLE_LIGHT_SPOT

#endif // !defined(DISABLE_LIGHT_DIRECTIONAL) || !defined(DISABLE_LIGHT_OMNI) || !defined(DISABLE_LIGHT_SPOT)

#ifndef MODE_RENDER_DEPTH
vec4 fog_process(vec3 vertex) {
	vec3 fog_color = fog_light_color;

#ifdef USE_RADIANCE_MAP
/*
		if (fog_aerial_perspective > 0.0) {
		vec3 sky_fog_color = vec3(0.0);
		vec3 cube_view = radiance_inverse_xform * vertex;
		// mip_level always reads from the second mipmap and higher so the fog is always slightly blurred
		float mip_level = mix(1.0 / MAX_ROUGHNESS_LOD, 1.0, 1.0 - (abs(vertex.z) - z_near) / (z_far - z_near));

		sky_fog_color = textureLod(radiance_map, cube_view, mip_level * RADIANCE_MAX_LOD).rgb;

		fog_color = mix(fog_color, sky_fog_color, fog_aerial_perspective);
	}
	*/
#endif

#ifndef DISABLE_LIGHT_DIRECTIONAL
	if (fog_sun_scatter > 0.001) {
		vec4 sun_scatter = vec4(0.0);
		float sun_total = 0.0;
		vec3 view = normalize(vertex);
		for (int i = 0; i < MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS; i++) {
			if (i >= directional_light_count) {
				break;
			}
			vec3 light_color = directional_lights.data[i].color_size.xyz * directional_lights.data[i].direction_energy.w;
			float light_amount = pow(max(dot(view, directional_lights.data[i].direction_energy.xyz), 0.0), 8.0);
			fog_color += light_color * light_amount * fog_sun_scatter;
		}
	}
#endif // !DISABLE_LIGHT_DIRECTIONAL

	float fog_amount = 1.0 - exp(min(0.0, -length(vertex) * fog_density));

	if (abs(fog_height_density) >= 0.0001) {
		float y = (inv_view_matrix * vec4(vertex, 1.0)).y;

		float y_dist = y - fog_height;

		float vfog_amount = 1.0 - exp(min(0.0, y_dist * fog_height_density));

		fog_amount = max(vfog_amount, fog_amount);
	}

	return vec4(fog_color, fog_amount);
}

#endif // !MODE_RENDER_DEPTH

void main() {
	vec3 vertex = vertex_interp;

	vec3 eye_offset = vec3(0.0, 0.0, 0.0);
	vec3 view = -normalize(vertex_interp);
	mat4 projection_matrix_local = projection_matrix;
	mat4 inv_projection_matrix_local = inv_projection_matrix;

	highp mat4 model_matrix = world_transform;
	vec3 albedo = vec3(1.0);
	vec3 backlight = vec3(0.0);
	vec4 transmittance_color = vec4(0.0, 0.0, 0.0, 1.0);
	float transmittance_depth = 0.0;
	float transmittance_boost = 0.0;
	float metallic = 0.0;
	float specular = 0.5;
	vec3 emission = vec3(0.0);
	float roughness = 1.0;
	float rim = 0.0;
	float rim_tint = 0.0;
	float clearcoat = 0.0;
	float clearcoat_roughness = 0.0;
	float anisotropy = 0.0;
	vec2 anisotropy_flow = vec2(1.0, 0.0);
	vec4 fog = vec4(0.0);
#if defined(CUSTOM_RADIANCE_USED)
	vec4 custom_radiance = vec4(0.0);
#endif
#if defined(CUSTOM_IRRADIANCE_USED)
	vec4 custom_irradiance = vec4(0.0);
#endif

	float ao = 1.0;
	float ao_light_affect = 0.0;

	float alpha = 1.0;

#if defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
	vec3 binormal = normalize(binormal_interp);
	vec3 tangent = normalize(tangent_interp);
#else
	vec3 binormal = vec3(0.0);
	vec3 tangent = vec3(0.0);
#endif

#ifdef NORMAL_USED
	vec3 normal = normalize(normal_interp);

#if defined(DO_SIDE_CHECK)
	if (!gl_FrontFacing) {
		normal = -normal;
	}
#endif

#endif //NORMAL_USED

#ifdef UV_USED
	vec2 uv = uv_interp;
#endif

#if defined(UV2_USED) || defined(USE_LIGHTMAP)
	vec2 uv2 = uv2_interp;
#endif

#if defined(COLOR_USED)
	vec4 color = color_interp;
#endif

#if defined(NORMAL_MAP_USED)

	vec3 normal_map = vec3(0.5);
#endif

	float normal_map_depth = 1.0;

	vec2 screen_uv = gl_FragCoord.xy * screen_pixel_size;

	float sss_strength = 0.0;

#ifdef ALPHA_SCISSOR_USED
	float alpha_scissor_threshold = 1.0;
#endif // ALPHA_SCISSOR_USED

#ifdef ALPHA_HASH_USED
	float alpha_hash_scale = 1.0;
#endif // ALPHA_HASH_USED

#ifdef ALPHA_ANTIALIASING_EDGE_USED
	float alpha_antialiasing_edge = 0.0;
	vec2 alpha_texture_coordinate = vec2(0.0, 0.0);
#endif // ALPHA_ANTIALIASING_EDGE_USED
	{
#CODE : FRAGMENT
	}

#ifndef USE_SHADOW_TO_OPACITY

#if defined(ALPHA_SCISSOR_USED)
	if (alpha < alpha_scissor_threshold) {
		discard;
	}
#endif // ALPHA_SCISSOR_USED

#ifdef USE_OPAQUE_PREPASS
#if !defined(ALPHA_SCISSOR_USED)

	if (alpha < opaque_prepass_threshold) {
		discard;
	}

#endif // not ALPHA_SCISSOR_USED
#endif // USE_OPAQUE_PREPASS

#endif // !USE_SHADOW_TO_OPACITY

#ifdef NORMAL_MAP_USED

	normal_map.xy = normal_map.xy * 2.0 - 1.0;
	normal_map.z = sqrt(max(0.0, 1.0 - dot(normal_map.xy, normal_map.xy))); //always ignore Z, as it can be RG packed, Z may be pos/neg, etc.

	normal = normalize(mix(normal, tangent * normal_map.x + binormal * normal_map.y + normal * normal_map.z, normal_map_depth));

#endif

#ifdef LIGHT_ANISOTROPY_USED

	if (anisotropy > 0.01) {
		//rotation matrix
		mat3 rot = mat3(tangent, binormal, normal);
		//make local to space
		tangent = normalize(rot * vec3(anisotropy_flow.x, anisotropy_flow.y, 0.0));
		binormal = normalize(rot * vec3(-anisotropy_flow.y, anisotropy_flow.x, 0.0));
	}

#endif

#ifndef MODE_RENDER_DEPTH

#ifndef CUSTOM_FOG_USED
#ifndef DISABLE_FOG
	if (fog_enabled) {
		fog = fog_process(vertex);
	}
#endif // !DISABLE_FOG
#endif // !CUSTOM_FOG_USED

	// Convert colors to linear
	albedo = srgb_to_linear(albedo);
	emission = srgb_to_linear(emission);
	// TODO Backlight and transmittance when used
#ifndef MODE_UNSHADED
	vec3 f0 = F0(metallic, specular, albedo);
	vec3 specular_light = vec3(0.0, 0.0, 0.0);
	vec3 diffuse_light = vec3(0.0, 0.0, 0.0);
	vec3 ambient_light = vec3(0.0, 0.0, 0.0);

#ifdef BASE_PASS
	/////////////////////// LIGHTING //////////////////////////////

	// IBL precalculations
	float ndotv = clamp(dot(normal, view), 0.0, 1.0);
	vec3 F = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - ndotv, 5.0);

#ifdef USE_RADIANCE_MAP
	if (use_reflection_cubemap) {
#ifdef LIGHT_ANISOTROPY_USED
		// https://google.github.io/filament/Filament.html#lighting/imagebasedlights/anisotropy
		vec3 anisotropic_direction = anisotropy >= 0.0 ? binormal : tangent;
		vec3 anisotropic_tangent = cross(anisotropic_direction, view);
		vec3 anisotropic_normal = cross(anisotropic_tangent, anisotropic_direction);
		vec3 bent_normal = normalize(mix(normal, anisotropic_normal, abs(anisotropy) * clamp(5.0 * roughness, 0.0, 1.0)));
		vec3 ref_vec = reflect(-view, bent_normal);
#else
		vec3 ref_vec = reflect(-view, normal);
#endif
		ref_vec = mix(ref_vec, normal, roughness * roughness);
		float horizon = min(1.0 + dot(ref_vec, normal), 1.0);
		ref_vec = radiance_inverse_xform * ref_vec;
		specular_light = textureCubeLod(radiance_map, ref_vec, sqrt(roughness) * RADIANCE_MAX_LOD).rgb;
		specular_light = srgb_to_linear(specular_light);
		specular_light *= horizon * horizon;
		specular_light *= ambient_light_color_energy.a;
	}
#endif

	// Calculate Reflection probes
	// Calculate Lightmaps

#if defined(CUSTOM_RADIANCE_USED)
	specular_light = mix(specular_light, custom_radiance.rgb, custom_radiance.a);
#endif // CUSTOM_RADIANCE_USED

#ifndef USE_LIGHTMAP
	//lightmap overrides everything
	if (use_ambient_light) {
		ambient_light = ambient_light_color_energy.rgb;
#ifdef USE_RADIANCE_MAP
		if (use_ambient_cubemap) {
			vec3 ambient_dir = radiance_inverse_xform * normal;
			vec3 cubemap_ambient = textureCubeLod(radiance_map, ambient_dir, RADIANCE_MAX_LOD).rgb;
			cubemap_ambient = srgb_to_linear(cubemap_ambient);
			ambient_light = mix(ambient_light, cubemap_ambient * ambient_light_color_energy.a, ambient_color_sky_mix);
		}
#endif
	}
#endif // USE_LIGHTMAP

#if defined(CUSTOM_IRRADIANCE_USED)
	ambient_light = mix(ambient_light, custom_irradiance.rgb, custom_irradiance.a);
#endif // CUSTOM_IRRADIANCE_USED

	{
#if defined(AMBIENT_LIGHT_DISABLED)
		ambient_light = vec3(0.0, 0.0, 0.0);
#else
		ambient_light *= albedo.rgb;
		ambient_light *= ao;
#endif // AMBIENT_LIGHT_DISABLED
	}

	// convert ao to direct light ao
	ao = mix(1.0, ao, ao_light_affect);

	{
#if defined(DIFFUSE_TOON)
		//simplify for toon, as
		specular_light *= specular * metallic * albedo * 2.0;
#else

		// scales the specular reflections, needs to be be computed before lighting happens,
		// but after environment, GI, and reflection probes are added
		// Environment brdf approximation (Lazarov 2013)
		// see https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
		const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
		const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
		vec4 r = roughness * c0 + c1;
		float ndotv = clamp(dot(normal, view), 0.0, 1.0);

		float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
		vec2 env = vec2(-1.04, 1.04) * a004 + r.zw;
		specular_light *= env.x * f0 + env.y * clamp(50.0 * f0.g, metallic, 1.0);
#endif
	}

#endif // BASE_PASS

#ifndef DISABLE_LIGHT_DIRECTIONAL
	for (int i = 0; i < MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS; i++) {
		if (i < directional_light_count) {
			light_compute(normal, normalize(directional_lights.data[i].direction_energy.xyz), normalize(view), directional_lights.data[i].color_size.w, directional_lights.data[i].color_size.xyz * directional_lights.data[i].direction_energy.w, 1.0, f0, roughness, metallic, 1.0, albedo, alpha,
#ifdef LIGHT_BACKLIGHT_USED
				backlight,
#endif
#ifdef LIGHT_RIM_USED
				rim, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
				clearcoat, clearcoat_roughness, normalize(normal_interp),
#endif
#ifdef LIGHT_ANISOTROPY_USED
				binormal,
				tangent, anisotropy,
#endif
				diffuse_light,
				specular_light);
		}
	}
#endif // !DISABLE_LIGHT_DIRECTIONAL

#ifndef DISABLE_LIGHT_OMNI
	for (int i = 0; i < MAX_FORWARD_LIGHTS; i++) {
		if (i < omni_light_count) {
			LightData light = omni_lights.data[i];
			light_process_omni(light, vertex, view, normal, f0, roughness, metallic, 0.0, albedo, alpha,
#ifdef LIGHT_BACKLIGHT_USED
				backlight,
#endif
#ifdef LIGHT_RIM_USED
				rim,
				rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
				clearcoat, clearcoat_roughness, normalize(normal_interp),
#endif
#ifdef LIGHT_ANISOTROPY_USED
				binormal, tangent, anisotropy,
#endif
				diffuse_light, specular_light);
		}
	}
#endif // !DISABLE_LIGHT_OMNI

#ifndef DISABLE_LIGHT_SPOT
	for (int i = 0; i < MAX_FORWARD_LIGHTS; i++) {
		if (i < spot_light_count) {
			LightData light = spot_lights.data[i];
			light_process_spot(light, vertex, view, normal, f0, roughness, metallic, 0.0, albedo, alpha,
#ifdef LIGHT_BACKLIGHT_USED
				backlight,
#endif
#ifdef LIGHT_RIM_USED
				rim,
				rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
				clearcoat, clearcoat_roughness, normalize(normal_interp),
#endif
#ifdef LIGHT_ANISOTROPY_USED
				tangent,
				binormal, anisotropy,
#endif
				diffuse_light, specular_light);
		}
	}
#endif // !DISABLE_LIGHT_SPOT

#endif // !MODE_UNSHADED

#endif // !MODE_RENDER_DEPTH

#if defined(USE_SHADOW_TO_OPACITY)
	alpha = min(alpha, clamp(length(ambient_light), 0.0, 1.0));

#if defined(ALPHA_SCISSOR_USED)
	if (alpha < alpha_scissor_threshold) {
		discard;
	}
#endif // ALPHA_SCISSOR_USED

#ifdef USE_OPAQUE_PREPASS
#if !defined(ALPHA_SCISSOR_USED)

	if (alpha < opaque_prepass_threshold) {
		discard;
	}

#endif // not ALPHA_SCISSOR_USED
#endif // USE_OPAQUE_PREPASS

#endif // USE_SHADOW_TO_OPACITY

#ifdef MODE_RENDER_DEPTH
//nothing happens, so a tree-ssa optimizer will result in no fragment shader :)
#else // !MODE_RENDER_DEPTH

#ifdef MODE_UNSHADED
	frag_color = vec4(albedo, alpha);
#else

	diffuse_light *= albedo;

	diffuse_light *= 1.0 - metallic;
	ambient_light *= 1.0 - metallic;

	frag_color = vec4(diffuse_light + specular_light, alpha);
#ifdef BASE_PASS
	frag_color.rgb += emission + ambient_light;
#endif
#endif //MODE_UNSHADED

#ifndef DISABLE_FOG
	if (fog_enabled) {
#ifdef BASE_PASS
		frag_color.rgb = mix(frag_color.rgb, fog.rgb, fog.a);
#else
		frag_color.rgb *= (1.0 - fog.a);
#endif // BASE_PASS
	}
#endif

	// Tonemap before writing as we are writing to an sRGB framebuffer
	frag_color.rgb *= exposure;
	frag_color.rgb = apply_tonemapping(frag_color.rgb, white);
	frag_color.rgb = linear_to_srgb(frag_color.rgb);

#ifdef USE_BCS
	frag_color.rgb = apply_bcs(frag_color.rgb, bcs);
#endif

#ifdef USE_COLOR_CORRECTION
	frag_color.rgb = apply_color_correction(frag_color.rgb, color_correction);
#endif

#endif //!MODE_RENDER_DEPTH

	gl_FragColor = frag_color;
}