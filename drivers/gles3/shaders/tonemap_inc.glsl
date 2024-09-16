layout(std140) uniform TonemapData { //ubo:0
	float exposure;
	float white;
	int tonemapper;
	int pad;

	int pad2;
	float brightness;
	float contrast;
	float saturation;
};

uniform sampler3D tony_mc_mapface_lut; //texunit:3

// This expects 0-1 range input.
vec3 linear_to_srgb(vec3 color) {
	//color = clamp(color, vec3(0.0), vec3(1.0));
	//const vec3 a = vec3(0.055f);
	//return mix((vec3(1.0f) + a) * pow(color.rgb, vec3(1.0f / 2.4f)) - a, 12.92f * color.rgb, lessThan(color.rgb, vec3(0.0031308f)));
	// Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	return max(vec3(1.055) * pow(color, vec3(0.416666667)) - vec3(0.055), vec3(0.0));
}

// This expects 0-1 range input, outside that range it behaves poorly.
vec3 srgb_to_linear(vec3 color) {
	// Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

#ifdef APPLY_TONEMAPPING

vec3 tonemap_filmic(vec3 color, float p_white) {
	// exposure bias: input scale (color *= bias, white *= bias) to make the brightness consistent with other tonemappers
	// also useful to scale the input to the range that the tonemapper is designed for (some require very high input values)
	// has no effect on the curve's general shape or visual properties
	const float exposure_bias = 2.0f;
	const float A = 0.22f * exposure_bias * exposure_bias; // bias baked into constants for performance
	const float B = 0.30f * exposure_bias;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.01f;
	const float F = 0.30f;

	vec3 color_tonemapped = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
	float p_white_tonemapped = ((p_white * (A * p_white + C * B) + D * E) / (p_white * (A * p_white + B) + D * F)) - E / F;

	return color_tonemapped / p_white_tonemapped;
}

// Adapted from https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// (MIT License).
vec3 tonemap_aces(vec3 color, float p_white) {
	const float exposure_bias = 1.8f;
	const float A = 0.0245786f;
	const float B = 0.000090537f;
	const float C = 0.983729f;
	const float D = 0.432951f;
	const float E = 0.238081f;

	// Exposure bias baked into transform to save shader instructions. Equivalent to `color *= exposure_bias`
	const mat3 rgb_to_rrt = mat3(
			vec3(0.59719f * exposure_bias, 0.35458f * exposure_bias, 0.04823f * exposure_bias),
			vec3(0.07600f * exposure_bias, 0.90834f * exposure_bias, 0.01566f * exposure_bias),
			vec3(0.02840f * exposure_bias, 0.13383f * exposure_bias, 0.83777f * exposure_bias));

	const mat3 odt_to_rgb = mat3(
			vec3(1.60475f, -0.53108f, -0.07367f),
			vec3(-0.10208f, 1.10813f, -0.00605f),
			vec3(-0.00327f, -0.07276f, 1.07602f));

	color *= rgb_to_rrt;
	vec3 color_tonemapped = (color * (color + A) - B) / (color * (C * color + D) + E);
	color_tonemapped *= odt_to_rgb;

	p_white *= exposure_bias;
	float p_white_tonemapped = (p_white * (p_white + A) - B) / (p_white * (C * p_white + D) + E);

	return color_tonemapped / p_white_tonemapped;
}

// Based on Reinhard's extended formula, see equation 4 in https://doi.org/cjbgrt
vec3 tonemap_reinhard(vec3 color, float p_white) {
	float white_squared = p_white * p_white;
	vec3 white_squared_color = white_squared * color;
	// Equivalent to color * (1 + color / white_squared) / (1 + color)
	return (white_squared_color + color * color) / (white_squared_color + white_squared);
}

// Adapted from https://modelviewer.dev/examples/tone-mapping#commerce
vec3 tonemap_pbr_neutral(vec3 color) {
	const float start_compression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < start_compression) {
		return color;
	}

	float d = 1.0 - start_compression;
	float new_peak = 1.0 - d * d / (peak + d - start_compression);
	color *= new_peak / peak;

	float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
	return mix(color, vec3(1.0, 1.0, 1.0), g);
}

// "Hable Tone Mapping" a.k.a Uncharted 2 tonemapping.
// source: https://64.github.io/tonemapping/#uncharted-2
vec3 hable_tonemap_partial(vec3 x) {
	const float A = 0.15f;
	const float B = 0.50f;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.02f;
	const float F = 0.30f;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 tonemap_hable(vec3 color, float white) {
	const float EXPOSURE_BIAS = 2.0f;
	vec3 curr = hable_tonemap_partial(color * EXPOSURE_BIAS);

	vec3 W = vec3(11.2f);
	float white_padding = white;
	if (white >= 1.0) {
		white_padding = white + 1.0;
	}
	vec3 white_scale = vec3(white_padding) / hable_tonemap_partial(W);
	return curr * white_scale;
}

// Optimised cineon tonemapping.
// Adapted from Three.js (MIT)
vec3 tonemap_cineon(vec3 color, float white) {
	color *= white;
	color = max(vec3(0.0), color - 0.004);
	return pow(
		(color * (6.2 * color + 0.5)) /
		(color * (6.2 * color + 1.7) + 0.06),
		vec3(2.2)
	);
}

float luminance_drago(float L, float b) {
	const float LMax = 1.0;
	float Ld = b / (log(LMax + 1.0) / log(10.0));
	Ld *= log(L + 1.0) / log(2.0 + 8.0 * pow((L / LMax), log(b) / log(0.5)));
	return Ld;
}

// Based on the paper: "Adaptive Logarithmic Mapping For Displaying High Contrast Scenes"
// https://resources.mpi-inf.mpg.de/tmo/logmap/logmap.pdf
vec3 tonemap_drago(vec3 color, float white) {
	const float BIAS = 0.85;

	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float Ld = luminance_drago(luminance, BIAS);
	color = color * (Ld / luminance);
	color *= white;
	return clamp(color, 0.0, 1.0);
}

// Mean error^2: 3.6705141e-06
vec3 agx_default_contrast_approx(vec3 x) {
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;

	return +15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 val, float white) {
	const mat3 agx_mat = mat3(
			0.842479062253094, 0.0423282422610123, 0.0423756549057051,
			0.0784335999999992, 0.878468636469772, 0.0784336,
			0.0792237451477643, 0.0791661274605434, 0.879142973793104);

	const float min_ev = -12.47393f;
	float max_ev = log2(white);

	// Input transform (inset).
	val = agx_mat * val;

	// Log2 space encoding.
	val = clamp(log2(val), min_ev, max_ev);
	val = (val - min_ev) / (max_ev - min_ev);

	// Apply sigmoid function approximation.
	val = agx_default_contrast_approx(val);

	return val;
}

vec3 agx_eotf(vec3 val) {
	const mat3 agx_mat_inv = mat3(
			1.19687900512017, -0.0528968517574562, -0.0529716355144438,
			-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
			-0.0990297440797205, -0.0989611768448433, 1.15107367264116);

	// Inverse input transform (outset).
	val = agx_mat_inv * val;

	// sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
	// NOTE: We're linearizing the output here. Comment/adjust when
	// *not* using a sRGB render target.
	val = pow(val, vec3(2.2));

	return val;
}

vec3 agx_look_punchy(vec3 val) {
	const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
	float luma = dot(val, lw);

	vec3 offset = vec3(0.0);
	vec3 slope = vec3(1.0);
	vec3 power = vec3(1.35, 1.35, 1.35);
	float sat = 1.4;

	// ASC CDL.
	val = pow(val * slope + offset, power);
	return luma + sat * (val - luma);
}

// Adapted from https://iolite-engine.com/blog_posts/minimal_agx_implementation
vec3 tonemap_agx(vec3 color, float white, bool punchy) {
	color = agx(color, white);
	if (punchy) {
		color = agx_look_punchy(color);
	}
	color = agx_eotf(color);
	return color;
}

// https://github.com/h3r2tic/tony-mc-mapface/blob/main/shader/tony_mc_mapface.hlsl
vec3 tonemap_tony_mc_mapface(vec3 stimulus) {
	vec3 encoded = stimulus / (stimulus + 1.0f);

	const float LUT_DIMS = 48.0f;
	vec3 uv = encoded * ((LUT_DIMS - 1.0f) / LUT_DIMS) + 0.5f / LUT_DIMS;

	return texture(tony_mc_mapface_lut, uv).rgb;
}

#define TONEMAPPER_LINEAR 0
#define TONEMAPPER_REINHARD 1
#define TONEMAPPER_FILMIC 2
#define TONEMAPPER_ACES 3
#define TONEMAPPER_AGX 4
#define TONEMAPPER_AGX_PUNCHY 5
#define TONEMAPPER_PBR_NEUTRAL 6
#define TONEMAPPER_HABLE 7
#define TONEMAPPER_CINEON 8
#define TONEMAPPER_DRAGO 9
#define TONEMAPPER_TONY_MC_MAPFACE 10

vec3 apply_tonemapping(vec3 color, float p_white) { // inputs are LINEAR
	// Ensure color values passed to tonemappers are positive.
	// They can be negative in the case of negative lights, which leads to undesired behavior.
	if (tonemapper == TONEMAPPER_LINEAR) {
		return color;
	} else if (tonemapper == TONEMAPPER_REINHARD) {
		return tonemap_reinhard(max(vec3(0.0f), color), p_white);
	} else if (tonemapper == TONEMAPPER_FILMIC) {
		return tonemap_filmic(max(vec3(0.0f), color), p_white);
	} else if (tonemapper == TONEMAPPER_ACES) {
		return tonemap_aces(max(vec3(0.0f), color), p_white);
	} else if (tonemapper == TONEMAPPER_AGX) {
		return tonemap_agx(max(vec3(0.0f), color), p_white, false);
	} else if (tonemapper == TONEMAPPER_AGX_PUNCHY) {
		return tonemap_agx(max(vec3(0.0f), color), p_white, true);
	} else if (tonemapper == TONEMAPPER_PBR_NEUTRAL) {
		return tonemap_pbr_neutral(max(vec3(0.0f), color));
	} else if (tonemapper == TONEMAPPER_HABLE) {
		return tonemap_hable(max(vec3(0.0f), color), p_white);
	} else if (tonemapper == TONEMAPPER_CINEON) {
		return tonemap_cineon(max(vec3(0.0f), color), p_white);
	} else if (tonemapper == TONEMAPPER_DRAGO) {
		return tonemap_drago(max(vec3(0.0f), color), p_white);
	} else { // TONEMAPPER_TONY_MC_MAPFACE
#ifdef SKY_SHADER
		// Sampling the Tony McMapface LUT in the sky shader leads to pitch black shadows if the "Sky" background
		// mode is enabled for the environment. Avoid this by returning the color as is.
		return color;
#else
		return tonemap_tony_mc_mapface(max(vec3(0.0f), color));
#endif
	}
}

#endif // APPLY_TONEMAPPING
