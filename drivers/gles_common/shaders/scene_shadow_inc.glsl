#if defined(USE_ADDITIVE_LIGHTING)
#ifdef ADDITIVE_OMNI
uniform highp samplerCube omni_shadow_texture; // texunit:-3
#endif
#ifdef ADDITIVE_SPOT
uniform highp sampler2D spot_shadow_texture; // texunit:-3
uniform highp float positional_shadow_atlas_pixel_size;
#endif
#endif

#if defined(BASE_PASS)
uniform highp sampler2D directional_shadow_atlas; // texunit:-3
uniform mediump float directional_shadow_fade_from;
uniform mediump float directional_shadow_fade_to;
#endif

#if !defined(ADDITIVE_OMNI)
float sample_shadow(highp sampler2D shadow, float shadow_pixel_size, vec4 pos) {
	pos.xyz /= pos.w;
	float depth = pos.z;
	float avg = step(texture2D(shadow, pos.xy).r, depth);
#ifdef SHADOW_MODE_PCF_13
	avg += step(texture2D(shadow, pos.xy + vec2(shadow_pixel_size * 2.0, 0.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(-shadow_pixel_size * 2.0, 0.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(0.0, shadow_pixel_size * 2.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(0.0, -shadow_pixel_size * 2.0)).r, depth);
	if (avg <= 0.000001) {
        return 0.0;
    } else if (avg >= 4.999999) {
        return 1.0;
    }
	avg += step(texture2D(shadow, pos.xy + vec2(shadow_pixel_size, 0.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(-shadow_pixel_size, 0.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(0.0, shadow_pixel_size)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(0.0, -shadow_pixel_size)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(shadow_pixel_size, shadow_pixel_size)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(-shadow_pixel_size, shadow_pixel_size)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(shadow_pixel_size, -shadow_pixel_size)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(-shadow_pixel_size, -shadow_pixel_size)).r, depth);
	return avg * (1.0 / 13.0);
#elif defined(SHADOW_MODE_PCF_5)
	avg += step(texture2D(shadow, pos.xy + vec2(shadow_pixel_size, 0.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(-shadow_pixel_size, 0.0)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(0.0, shadow_pixel_size)).r, depth);
	avg += step(texture2D(shadow, pos.xy + vec2(0.0, -shadow_pixel_size)).r, depth);
	return avg * (1.0 / 5.0);
#else
	return avg;
#endif
}
#else
float sample_shadow(highp sampler2D shadow, float shadow_pixel_size, vec4 pos) {
    return 1.0;
}
#endif