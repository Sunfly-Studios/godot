/* clang-format off */
#[modes]

mode_background =
mode_half_res = #define USE_HALF_RES_PASS
mode_quarter_res = #define USE_QUARTER_RES_PASS
mode_cubemap = #define USE_CUBEMAP_PASS
mode_cubemap_half_res = #define USE_CUBEMAP_PASS \n#define USE_HALF_RES_PASS
mode_cubemap_quarter_res = #define USE_CUBEMAP_PASS \n#define USE_QUARTER_RES_PASS

#[specializations]

USE_INVERTED_Y = true
APPLY_TONEMAPPING = true

#[vertex]

attribute highp vec2 vertex_attrib; // attrib:0
varying highp vec2 uv_interp;

/* clang-format on */

void main() {
#ifdef USE_INVERTED_Y
	uv_interp = vertex_attrib;
#else
	// We're doing clockwise culling so flip the order
	uv_interp = vec2(vertex_attrib.x, vertex_attrib.y * -1.0);
#endif
	// In Reverse-Z mapped to OpenGL's [-1, 1] clip space, the far plane sits at Z = -1.0.
	gl_Position = vec4(uv_interp, -1.0, 1.0);
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
#include "tonemap_inc.glsl"

varying highp vec2 uv_interp;

/* clang-format on */

uniform samplerCube radiance; //texunit:-1
#ifdef USE_CUBEMAP_PASS
uniform samplerCube half_res; //texunit:-2
uniform samplerCube quarter_res; //texunit:-3
#else
uniform sampler2D half_res; //texunit:-2
uniform sampler2D quarter_res; //texunit:-3
#endif

uniform highp vec4 global_shader_uniforms[MAX_GLOBAL_SHADER_UNIFORMS];

struct DirectionalLightData {
	highp vec4 direction_energy;
	highp vec4 color_size;
	bool enabled;
};

// Declaring nested uniform structs enables users doing directional_lights.data[i] natively.
struct DirectionalLights { 
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
};
uniform DirectionalLights directional_lights;

/* clang-format off */

#ifdef MATERIAL_UNIFORMS_USED
// flat uniforms:
#MATERIAL_UNIFORMS
#endif

/* clang-format on */
#GLOBALS

#ifdef USE_CUBEMAP_PASS
#define AT_CUBEMAP_PASS true
#else
#define AT_CUBEMAP_PASS false
#endif

#ifdef USE_HALF_RES_PASS
#define AT_HALF_RES_PASS true
#else
#define AT_HALF_RES_PASS false
#endif

#ifdef USE_QUARTER_RES_PASS
#define AT_QUARTER_RES_PASS true
#else
#define AT_QUARTER_RES_PASS false
#endif

// TODO(MBCX): mat4 is a waste of space, but we don't have an easy way to set a mat3 uniform for now
uniform highp mat4 orientation;
uniform highp vec4 projection;
uniform highp vec3 position;
uniform highp float time;
uniform highp float luminance_multiplier;

uniform highp float fog_aerial_perspective;
uniform highp vec3 fog_light_color;
uniform highp float fog_sun_scatter;
uniform bool fog_enabled;
uniform highp float fog_density;
uniform highp float z_far;
uniform int directional_light_count;

uniform highp float exposure;
uniform highp float white;

#ifdef USE_DEBANDING
// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
highp vec3 interleaved_gradient_noise(highp vec2 pos) {
	const highp vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	highp float res = fract(magic.z * fract(dot(pos, magic.xy))) * 2.0 - 1.0;
	return vec3(res, -res, res) / 255.0;
}
#endif

void main() {
	highp vec3 cube_normal;
	cube_normal.z = -1.0;
	cube_normal.x = (uv_interp.x - projection.x) / projection.y;
	cube_normal.y = (uv_interp.y - projection.z) / projection.w;
	cube_normal = mat3(orientation) * cube_normal;
	cube_normal = normalize(cube_normal);

	highp vec2 uv = gl_FragCoord.xy; // uv_interp * 0.5 + 0.5;

	highp vec2 panorama_coords = vec2(atan(cube_normal.x, -cube_normal.z), acos(cube_normal.y));

	if (panorama_coords.x < 0.0) {
		panorama_coords.x += M_PI * 2.0;
	}

	panorama_coords /= vec2(M_PI * 2.0, M_PI);

	highp vec3 color = vec3(0.0, 0.0, 0.0);
	highp float alpha = 1.0;
	
	// Only available to subpasses
	highp vec4 half_res_color = vec4(1.0);
	highp vec4 quarter_res_color = vec4(1.0);
	highp vec4 custom_fog = vec4(0.0);

#ifdef USE_CUBEMAP_PASS
	#ifdef USES_HALF_RES_COLOR
		half_res_color = textureCube(half_res, cube_normal);
	#endif
	#ifdef USES_QUARTER_RES_COLOR
		quarter_res_color = textureCube(quarter_res, cube_normal);
	#endif
#else
	#ifdef USES_HALF_RES_COLOR
		half_res_color = texture2DLod(half_res, uv, 0.0);
	#endif
	#ifdef USES_QUARTER_RES_COLOR
		quarter_res_color = texture2DLod(quarter_res, uv, 0.0);
	#endif
#endif

	{

#CODE : SKY

	}

	color *= luminance_multiplier;
	
	// Convert to Linear for tonemapping so color matches scene shader better
	color = srgb_to_linear(color);
	color *= exposure;
#ifdef APPLY_TONEMAPPING
	color = apply_tonemapping(color, white);
#endif
	color = linear_to_srgb(color);

#ifdef USE_BCS
	color = apply_bcs(color, bcs);
#endif

#ifdef USE_COLOR_CORRECTION
	color = apply_color_correction(color, color_correction);
#endif

	highp vec4 frag_color = vec4(color, alpha);

#ifdef USE_DEBANDING
	frag_color.rgb += interleaved_gradient_noise(gl_FragCoord.xy);
#endif

	gl_FragColor = frag_color;
}