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

#ifdef MODE_FILTER
uniform highp sampler2D source_color; //texunit:0

uniform highp float view;
uniform highp vec2 pixel_size;
uniform highp float luminance_multiplier;
uniform highp float glow_bloom;
uniform highp float glow_hdr_threshold;
uniform highp float glow_hdr_scale;
uniform highp float glow_luminance_cap;
#endif

#ifdef MODE_DOWNSAMPLE
uniform highp sampler2D source_color; //texunit:0
uniform highp vec2 pixel_size;
#endif

#ifdef MODE_UPSAMPLE
uniform highp sampler2D source_color; //texunit:0
uniform highp vec2 pixel_size;
#endif

void main() {
#ifdef MODE_FILTER
    highp vec2 half_pixel = pixel_size * 0.5;
    highp vec2 uv = uv_interp;
    
    // textureLod is downgraded to texture2D
    highp vec3 color = texture2D(source_color, uv).rgb * 4.0;
    color += texture2D(source_color, uv - half_pixel).rgb;
    color += texture2D(source_color, uv + half_pixel).rgb;
    color += texture2D(source_color, uv - vec2(half_pixel.x, -half_pixel.y)).rgb;
    color += texture2D(source_color, uv + vec2(half_pixel.x, -half_pixel.y)).rgb;

    color /= luminance_multiplier * 8.0;

    highp float feedback_factor = max(color.r, max(color.g, color.b));
    highp float feedback = max(smoothstep(glow_hdr_threshold, glow_hdr_threshold + glow_hdr_scale, feedback_factor), glow_bloom);

    color = min(color * feedback, vec3(glow_luminance_cap));

    gl_FragColor = vec4(luminance_multiplier * color, 1.0);
#endif

#ifdef MODE_DOWNSAMPLE
    highp vec2 half_pixel = pixel_size * 0.5;
    highp vec4 color = texture2D(source_color, uv_interp) * 4.0;
    color += texture2D(source_color, uv_interp - half_pixel);
    color += texture2D(source_color, uv_interp + half_pixel);
    color += texture2D(source_color, uv_interp - vec2(half_pixel.x, -half_pixel.y));
    color += texture2D(source_color, uv_interp + vec2(half_pixel.x, -half_pixel.y));
    gl_FragColor = color / 8.0;
#endif

#ifdef MODE_UPSAMPLE
    highp vec2 half_pixel = pixel_size * 0.5;

    highp vec4 color = texture2D(source_color, uv_interp + vec2(-half_pixel.x * 2.0, 0.0));
    color += texture2D(source_color, uv_interp + vec2(-half_pixel.x, half_pixel.y)) * 2.0;
    color += texture2D(source_color, uv_interp + vec2(0.0, half_pixel.y * 2.0));
    color += texture2D(source_color, uv_interp + vec2(half_pixel.x, half_pixel.y)) * 2.0;
    color += texture2D(source_color, uv_interp + vec2(half_pixel.x * 2.0, 0.0));
    color += texture2D(source_color, uv_interp + vec2(half_pixel.x, -half_pixel.y)) * 2.0;
    color += texture2D(source_color, uv_interp + vec2(0.0, -half_pixel.y * 2.0));
    color += texture2D(source_color, uv_interp + vec2(-half_pixel.x, -half_pixel.y)) * 2.0;

    gl_FragColor = color / 12.0;
#endif
}