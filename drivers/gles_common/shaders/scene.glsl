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
RENDER_SHADOWS = false
SHADOW_MODE_PCF_5 = false
SHADOW_MODE_PCF_13 = false
LIGHT_USE_PSSM2 = false
LIGHT_USE_PSSM4 = false
LIGHT_USE_PSSM_BLEND = false
BASE_PASS = true
USE_ADDITIVE_LIGHTING = false
ADDITIVE_OMNI = false
ADDITIVE_SPOT = false

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

uniform float shadow_bias;

#if defined(USE_ADDITIVE_LIGHTING) && (defined(ADDITIVE_OMNI) || defined(ADDITIVE_SPOT))
uniform highp mat4 positional_shadow_matrix;
uniform highp vec3 positional_light_position;
uniform highp float positional_shadow_normal_bias;
uniform highp float positional_shadow_atlas_pixel_size;
#endif

#if defined(BASE_PASS)
uniform highp vec3 directional_shadow_direction;
uniform highp float directional_shadow_atlas_pixel_size;
uniform highp vec4 directional_shadow_normal_bias;
uniform highp vec4 directional_shadow_split_offsets;
uniform highp mat4 directional_shadow_matrix1;
uniform highp mat4 directional_shadow_matrix2;
uniform highp mat4 directional_shadow_matrix3;
uniform highp mat4 directional_shadow_matrix4;
#endif

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

#if defined(USE_ADDITIVE_LIGHTING) || defined(BASE_PASS)
varying highp vec4 shadow_coord;
#if defined(LIGHT_USE_PSSM2) || defined(LIGHT_USE_PSSM4)
varying highp vec4 shadow_coord2;
#endif
#ifdef LIGHT_USE_PSSM4
varying highp vec4 shadow_coord3;
varying highp vec4 shadow_coord4;
#endif
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

#if defined(USE_ADDITIVE_LIGHTING) && (defined(ADDITIVE_OMNI) || defined(ADDITIVE_SPOT))
	vec3 light_rel_vec = positional_light_position - vertex_interp;
	float light_length = length(light_rel_vec);
#ifdef NORMAL_USED
	float aNdotL = abs(dot(normalize(normal), normalize(light_rel_vec)));
	vec3 normal_offset = (1.0 - aNdotL) * positional_shadow_normal_bias * light_length * normal;
#else
	vec3 normal_offset = vec3(0.0);
#endif

#ifdef ADDITIVE_SPOT
	shadow_coord = positional_shadow_matrix * vec4(vertex_interp + normal_offset, 1.0);
#endif
#ifdef ADDITIVE_OMNI
	shadow_coord = vec4(vertex_interp + normal_offset, 1.0);
#endif
#endif

#if defined(BASE_PASS)
#ifdef NORMAL_USED
	vec3 base_normal_bias = normalize(normal) * (1.0 - max(0.0, dot(directional_shadow_direction, -normalize(normal))));
	vec3 normal_offset = base_normal_bias * directional_shadow_normal_bias.x;
#else
	vec3 base_normal_bias = vec3(0.0);
	vec3 normal_offset = vec3(0.0);
#endif
	shadow_coord = directional_shadow_matrix1 * vec4(vertex_interp + normal_offset, 1.0);

#if defined(LIGHT_USE_PSSM2) || defined(LIGHT_USE_PSSM4)
	normal_offset = base_normal_bias * directional_shadow_normal_bias.y;
	shadow_coord2 = directional_shadow_matrix2 * vec4(vertex_interp + normal_offset, 1.0);
#endif

#ifdef LIGHT_USE_PSSM4
	normal_offset = base_normal_bias * directional_shadow_normal_bias.z;
	shadow_coord3 = directional_shadow_matrix3 * vec4(vertex_interp + normal_offset, 1.0);
	normal_offset = base_normal_bias * directional_shadow_normal_bias.w;
	shadow_coord4 = directional_shadow_matrix4 * vec4(vertex_interp + normal_offset, 1.0);
#endif
#endif

#if defined(RENDER_SHADOWS)
	float light_length_sq = dot(vertex_interp, vertex_interp);
	vertex_interp += vertex_interp * shadow_bias / light_length_sq;
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

#if defined(USE_ADDITIVE_LIGHTING) || defined(BASE_PASS)
varying highp vec4 shadow_coord;

#if defined(LIGHT_USE_PSSM2) || defined(LIGHT_USE_PSSM4)
varying highp vec4 shadow_coord2;
#endif

#ifdef LIGHT_USE_PSSM4
varying highp vec4 shadow_coord3;
varying highp vec4 shadow_coord4;
#endif
#endif

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

uniform float shadow_bias;

#if defined(USE_ADDITIVE_LIGHTING) && (defined(ADDITIVE_OMNI) || defined(ADDITIVE_SPOT))
uniform highp mat4 positional_shadow_matrix;
uniform highp vec3 positional_light_position;
uniform highp float positional_shadow_normal_bias;
uniform highp float positional_shadow_atlas_pixel_size;
#endif

#if defined(BASE_PASS)
uniform highp vec3 directional_shadow_direction;
uniform highp float directional_shadow_atlas_pixel_size;
uniform highp vec4 directional_shadow_normal_bias;
uniform highp vec4 directional_shadow_split_offsets;
uniform highp mat4 directional_shadow_matrix1;
uniform highp mat4 directional_shadow_matrix2;
uniform highp mat4 directional_shadow_matrix3;
uniform highp mat4 directional_shadow_matrix4;
#endif

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

#include "scene_uniforms_inc.glsl"
#include "scene_shadow_inc.glsl"

uniform highp sampler2D depth_buffer; // texunit:-6
uniform highp sampler2D color_buffer; // texunit:-5

uniform highp mat4 world_transform;
uniform mediump float opaque_prepass_threshold;

vec4 frag_color; // Maps to gl_FragColor at end of main()

#include "scene_brdf_inc.glsl"

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
#if defined(BASE_PASS)
	if (directional_light_count > 0) {
		float directional_shadow = 1.0;
#if !defined(LIGHT_USE_PSSM2) && !defined(LIGHT_USE_PSSM4)
		directional_shadow = sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord);
#endif
#ifdef LIGHT_USE_PSSM2
		float depth_z = -vertex.z;
		directional_shadow = depth_z < directional_shadow_split_offsets.x ? sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord) : sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord2);
#endif
#ifdef LIGHT_USE_PSSM4
		float depth_z = -vertex.z;
		float shadow1 = sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord);
		float shadow2 = sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord2);
		float shadow3 = sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord3);
		float shadow4 = sample_shadow(directional_shadow_atlas, directional_shadow_atlas_pixel_size, shadow_coord4);
		if (depth_z < directional_shadow_split_offsets.w) {
			if (depth_z < directional_shadow_split_offsets.y) {
				directional_shadow = depth_z < directional_shadow_split_offsets.x ? shadow1 : shadow2;
			} else {
				directional_shadow = depth_z < directional_shadow_split_offsets.z ? shadow3 : shadow4;
			}
		}
#endif
		directional_shadow = mix(directional_shadow, 1.0, 1.0 - smoothstep(directional_shadow_fade_to, directional_shadow_fade_from, vertex.z));
		directional_shadow = mix(1.0, directional_shadow, directional_lights.data[0].extended_data.z);

		light_compute(normal, normalize(directional_lights.data[0].direction_energy.xyz), normalize(view), directional_lights.data[0].color_size.w, directional_lights.data[0].color_size.xyz * directional_lights.data[0].direction_energy.w * directional_shadow, 1.0, f0, roughness, metallic, 1.0, albedo, alpha, diffuse_light, specular_light);
	}
#endif // BASE_PASS
#endif // !DISABLE_LIGHT_DIRECTIONAL

#ifndef DISABLE_LIGHT_OMNI
#if defined(USE_ADDITIVE_LIGHTING) && defined(ADDITIVE_OMNI)
	if (omni_light_count > 0) {
		float omni_shadow = 1.0;
		vec3 light_ray = shadow_coord.xyz - omni_lights.data[0].position_inv_radius.xyz;
		float sm = textureCube(omni_shadow_texture, light_ray).r;
		float depth = (length(light_ray) - 0.01) * omni_lights.data[0].position_inv_radius.w;
		omni_shadow = mix(1.0, step(sm, depth), omni_lights.data[0].cone_attenuation_angle_specular_shadow.w);

		light_process_omni(omni_lights.data[0], vertex, view, normal, f0, roughness, metallic, omni_shadow, albedo, alpha, diffuse_light, specular_light);
	}
#endif
#endif // !DISABLE_LIGHT_OMNI

#ifndef DISABLE_LIGHT_SPOT
#if defined(USE_ADDITIVE_LIGHTING) && defined(ADDITIVE_SPOT)
	if (spot_light_count > 0) {
		float spot_shadow = sample_shadow(spot_shadow_texture, positional_shadow_atlas_pixel_size, shadow_coord);
		spot_shadow = mix(1.0, spot_shadow, spot_lights.data[0].cone_attenuation_angle_specular_shadow.w);

		light_process_spot(spot_lights.data[0], vertex, view, normal, f0, roughness, metallic, spot_shadow, albedo, alpha, diffuse_light, specular_light);
	}
#endif
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
	gl_FragColor = frag_color;
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

	gl_FragColor = frag_color;
#endif //!MODE_RENDER_DEPTH

}