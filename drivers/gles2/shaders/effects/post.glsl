/* clang-format off */
[vertex]

attribute highp vec2 vertex_attrib; //attrib:0
varying highp vec2 uv_interp;

/* clang-format on */

void main() {
    uv_interp = vertex_attrib * 0.5 + 0.5;
    gl_Position = vec4(vertex_attrib, 1.0, 1.0);
}

/* clang-format off */
[fragment]

varying highp vec2 uv_interp;

/* clang-format on */

#define APPLY_TONEMAPPING

#include "../tonemap.glsl"

uniform highp sampler2D source_color; //texunit:0

uniform highp float view;
uniform highp float luminance_multiplier;

#ifdef USE_GLOW
uniform highp sampler2D glow_color; //texunit:1
uniform highp vec2 pixel_size;
uniform highp float glow_intensity;

highp vec4 get_glow_color(highp vec2 uv) {
    highp vec2 half_pixel = pixel_size * 0.5;

    highp vec4 color = texture2D(glow_color, uv + vec2(-half_pixel.x * 2.0, 0.0));
    color += texture2D(glow_color, uv + vec2(-half_pixel.x, half_pixel.y)) * 2.0;
    color += texture2D(glow_color, uv + vec2(0.0, half_pixel.y * 2.0));
    color += texture2D(glow_color, uv + vec2(half_pixel.x, half_pixel.y)) * 2.0;
    color += texture2D(glow_color, uv + vec2(half_pixel.x * 2.0, 0.0));
    color += texture2D(glow_color, uv + vec2(half_pixel.x, -half_pixel.y)) * 2.0;
    color += texture2D(glow_color, uv + vec2(0.0, -half_pixel.y * 2.0));
    color += texture2D(glow_color, uv + vec2(-half_pixel.x, -half_pixel.y)) * 2.0;

    return color / 12.0;
}
#endif

#ifdef USE_COLOR_CORRECTION
#ifdef USE_1D_LUT
uniform highp sampler2D source_color_correction; //texunit:2

highp vec3 apply_color_correction(highp vec3 color) {
    color.r = texture2D(source_color_correction, vec2(color.r, 0.0)).r;
    color.g = texture2D(source_color_correction, vec2(color.g, 0.0)).g;
    color.b = texture2D(source_color_correction, vec2(color.b, 0.0)).b;
    return color;
}
#else
// GLES2 does not support sampler3D. Stubbing as sampler2D to pass compiler.
uniform highp sampler2D source_color_correction; //texunit:2

highp vec3 apply_color_correction(highp vec3 color) {
    return texture2D(source_color_correction, color.xy).rgb;
}
#endif
#endif

// Stubbing SSAO uniforms so the C++ enum generates, avoiding undefined identifier errors.
uniform highp float ssao_intensity;
uniform highp float ssao_radius_frac;
uniform highp vec2 ssao_prn_UV;
uniform highp sampler2D depth_buffer; //texunit:3


void main() {
    highp vec4 color = texture2D(source_color, uv_interp);

#ifdef USE_GLOW
    highp vec4 glow = get_glow_color(uv_interp) * glow_intensity;

    glow.rgb = clamp(glow.rgb, vec3(0.0), vec3(1.0));
    color.rgb = max((color.rgb + glow.rgb) - (color.rgb * glow.rgb), vec3(0.0));
#endif

#ifdef USE_LUMINANCE_MULTIPLIER
    color = color / luminance_multiplier;
#endif

    color.rgb = srgb_to_linear(color.rgb);

    // white is assumed to be defined in tonemap_inc.glsl
    color.rgb = apply_tonemapping(color.rgb, white);
    color.rgb = linear_to_srgb(color.rgb);

#ifdef USE_BCS
    color.rgb = apply_bcs(color.rgb);
#endif

#ifdef USE_COLOR_CORRECTION
    color.rgb = apply_color_correction(color.rgb);
#endif

    gl_FragColor = color;
}