/* clang-format off */
[vertex]

attribute highp vec2 vertex_attrib; //attrib:0
varying highp vec2 uv_interp;

/* clang-format on */

void main() {
#ifdef USE_INVERTED_Y
    uv_interp = vertex_attrib;
#else
    // We're doing clockwise culling so flip the order
    uv_interp = vec2(vertex_attrib.x, vertex_attrib.y * -1.0);
#endif
    gl_Position = vec4(uv_interp, -1.0, 1.0);
}

/* clang-format off */
[fragment]

#define M_PI 3.14159265359

#include "tonemap.glsl"

varying highp vec2 uv_interp;

/* clang-format on */

uniform highp samplerCube radiance; //texunit:0

#ifdef USE_CUBEMAP_PASS
uniform highp samplerCube half_res; //texunit:1
uniform highp samplerCube quarter_res; //texunit:2
#else
uniform highp sampler2D half_res; //texunit:1
uniform highp sampler2D quarter_res; //texunit:2
#endif

uniform highp vec4 global_shader_uniforms_stub;
uniform highp vec4 directional_light_data_stub;

uniform highp mat4 orientation;
uniform highp vec4 projection;
uniform highp vec3 position;
uniform highp float time;
uniform highp float sky_energy_multiplier;
uniform highp float luminance_multiplier;

uniform highp float fog_aerial_perspective;
uniform highp vec4 fog_light_color;
uniform highp float fog_sun_scatter;
uniform bool fog_enabled;
uniform highp float fog_density;
uniform highp float fog_sky_affect;

// GLES2 does not support uint. Changed to int.
uniform int directional_light_count; 

#ifdef USE_DEBANDING
highp vec3 interleaved_gradient_noise(highp vec2 pos) {
    highp vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    highp float res = fract(magic.z * fract(dot(pos, magic.xy))) * 2.0 - 1.0;
    return vec3(res, -res, res) / 255.0;
}
#endif

#if !defined(DISABLE_FOG)
highp vec4 fog_process(highp vec3 view, highp vec3 sky_color) {
    highp vec3 fog_color = mix(fog_light_color.rgb, sky_color, fog_aerial_perspective);
    // Stubbed the complex lighting loop because GLES2 struggles with dynamic array accesses
    // and we just need this to compile for the Project Manager.
    return vec4(fog_color, 1.0);
}
#endif

void main() {
    highp vec3 cube_normal;
    cube_normal.z = -1.0;
    cube_normal.x = (uv_interp.x + projection.x) / projection.y;
    cube_normal.y = (-uv_interp.y - projection.z) / projection.w;

    cube_normal = mat3(orientation) * cube_normal;
    cube_normal = normalize(cube_normal);

    highp vec2 uv = gl_FragCoord.xy; 

    highp vec3 color = vec3(0.0, 0.0, 0.0);
    highp float alpha = 1.0; 
    highp vec4 half_res_color = vec4(1.0);
    highp vec4 quarter_res_color = vec4(1.0);
    highp vec4 custom_fog = vec4(0.0);

#ifdef USE_CUBEMAP_PASS
#ifdef USES_HALF_RES_COLOR
    // GLES2 uses plain textureCube instead of texture() constructors
    half_res_color = textureCube(half_res, cube_normal);
#endif
#ifdef USES_QUARTER_RES_COLOR
    quarter_res_color = textureCube(quarter_res, cube_normal);
#endif
#else
#ifdef USES_HALF_RES_COLOR
    // textureLod is unsupported natively, downgraded to texture2D
    half_res_color = texture2D(half_res, uv);
#endif
#ifdef USES_QUARTER_RES_COLOR
    quarter_res_color = texture2D(quarter_res, uv);
#endif
#endif

    // Godot 4 uses #CODE : SKY, but Godot 3's GLES2 compiler looked for FRAGMENT_SHADER_CODE.
    // We will leave this as a safe comment so it doesn't cause a preprocessor syntax error.
    /* FRAGMENT_SHADER_CODE */

    color *= sky_energy_multiplier;

#if !defined(DISABLE_FOG) && !defined(USE_CUBEMAP_PASS)
    if (fog_enabled) {
        highp vec4 fog = fog_process(cube_normal, color.rgb);
        color.rgb = mix(color.rgb, fog.rgb, fog.a * fog_sky_affect);
    }
    if (custom_fog.a > 0.0) {
        color.rgb = mix(color.rgb, custom_fog.rgb, custom_fog.a);
    }
#endif

    highp vec4 frag_out;
    frag_out.rgb = color * luminance_multiplier;
    frag_out.a = alpha;

#ifdef USE_DEBANDING
    frag_out.rgb += interleaved_gradient_noise(gl_FragCoord.xy) * sky_energy_multiplier * luminance_multiplier;
#endif

    gl_FragColor = frag_out;
}