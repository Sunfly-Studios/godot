// This expects 0-1 range input.
vec3 linear_to_srgb(vec3 color) {
	// Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	return max(vec3(1.055) * pow(color, vec3(0.416666667)) - vec3(0.055), vec3(0.0));
}

// This expects 0-1 range input, outside that range it behaves poorly.
vec3 srgb_to_linear(vec3 color) {
	// Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

#ifdef APPLY_TONEMAPPING

// Based on Reinhard's extended formula, see equation 4 in https://doi.org/cjbgrt
vec3 tonemap_reinhard(vec3 color, float p_white) {
	float white_squared = p_white * p_white;
	vec3 white_squared_color = white_squared * color;
	// Equivalent to color * (1 + color / white_squared) / (1 + color)
	return (white_squared_color + color * color) / (white_squared_color + white_squared);
}

vec3 tonemap_filmic(vec3 color, float p_white) {
	// Exposure bias baked into constants
	// A = 0.22 * (2.0 * 2.0), B = 0.30 * 2.0
	// C * B = 0.10 * 0.60 = 0.06, D * E = 0.20 * 0.01 = 0.002
	// D * F = 0.20 * 0.30 = 0.06, E / F = 0.01 / 0.30 = 0.0333333
	const float A = 0.88;
	const float B = 0.60;
	
	vec3 color_tonemapped = ((color * (A * color + 0.06) + 0.002) / (color * (A * color + B) + 0.06)) - 0.0333333;
	float p_white_tonemapped = ((p_white * (A * p_white + 0.06) + 0.002) / (p_white * (A * p_white + B) + 0.06)) - 0.0333333;

	return color_tonemapped / p_white_tonemapped;
}

// Adapted from https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// (MIT License).
vec3 tonemap_aces(vec3 color, float p_white) {
	const float A = 0.0245786;
	const float B = 0.000090537;
	const float C = 0.983729;
	const float D = 0.432951;
	const float E = 0.238081;

	// Exposure bias (1.8) baked into transform
	mat3 rgb_to_rrt = mat3(
		vec3(1.074942, 0.638244, 0.086814),
		vec3(0.1368, 1.635012, 0.028188),
		vec3(0.05112, 0.240894, 1.507986)
	);

	mat3 odt_to_rgb = mat3(
		vec3(1.60475, -0.53108, -0.07367),
		vec3(-0.10208, 1.10813, -0.00605),
		vec3(-0.00327, -0.07276, 1.07602)
	);

	color *= rgb_to_rrt;
	vec3 color_tonemapped = (color * (color + A) - B) / (color * (C * color + D) + E);
	color_tonemapped *= odt_to_rgb;

	p_white *= 1.8;
	float p_white_tonemapped = (p_white * (p_white + A) - B) / (p_white * (C * p_white + D) + E);

	return color_tonemapped / p_white_tonemapped;
}

// Polynomial approximation of EaryChow's AgX sigmoid curve.
// x must be within the range [0.0, 1.0]
vec3 agx_contrast_approx(vec3 x) {
	// Optimized with Horner's method
	return x * (0.021 + x * (4.0111 + x * (-25.682 + x * (70.359 + x * (-74.778 + x * 27.069)))));
}

// This is an approximation and simplification of EaryChow's AgX implementation that is used by Blender.
// Source: https://github.com/EaryChow/AgX_LUT_Gen/blob/main/AgXBasesRGB.py
vec3 tonemap_agx(vec3 color) {
	mat3 srgb_to_rec2020_agx_inset_matrix = mat3(
			0.54490813676363087053, 0.14044005884001287035, 0.088827411851915368603,
			0.37377945959812267119, 0.75410959864013760045, 0.17887712465043811023,
			0.081384976686407536266, 0.10543358536857773485, 0.73224999956948382528);

	mat3 agx_outset_rec2020_to_srgb_matrix = mat3(
			1.9645509602733325934, -0.29932243390911083839, -0.16436833806080403409,
			-0.85585845117807513559, 1.3264510741502356555, -0.23822464068860595117,
			-0.10886710826831608324, -0.027084020983874825605, 1.402665347143271889);

	const float min_ev = -12.4739311883324;
	const float max_ev = 4.02606881166759;

	color = max(color, vec3(2e-10));
	color = srgb_to_rec2020_agx_inset_matrix * color;

	color = clamp(log2(color), min_ev, max_ev);
	color = (color - min_ev) / (max_ev - min_ev);

	color = agx_contrast_approx(color);
	color = pow(color, vec3(2.4));

	color = agx_outset_rec2020_to_srgb_matrix * color;
	return color;
}

float luminance_drago(float L, float b) {
	// b / (log(2.0)/log(10.0)) simplifies to b * 3.32192809489
	float Ld = b * 3.32192809489;
	// log(L+1)/log(2 + 8 * L^(log(b)/log(0.5))) simplifies using log2 and log2(0.5) = -1.0
	Ld *= log2(L + 1.0) / log2(2.0 + 8.0 * pow(L, -log2(b)));
	return Ld;
}

// Based on the paper: "Adaptive Logarithmic Mapping For Displaying High Contrast Scenes"
// https://resources.mpi-inf.mpg.de/tmo/logmap/logmap.pdf
vec3 tonemap_drago(vec3 color, float white) {
	const float BIAS = 0.85;

	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float Ld = luminance_drago(luminance, BIAS);
	color = color * (Ld / max(luminance, 1e-6));
	color *= white;
	return clamp(color, 0.0, 1.0);
}

#define TONEMAPPER_LINEAR 0
#define TONEMAPPER_REINHARD 1
#define TONEMAPPER_FILMIC 2
#define TONEMAPPER_ACES 3
#define TONEMAPPER_AGX 4
#define TONEMAPPER_DRAGO 5

vec3 apply_tonemapping(vec3 color, float p_white, int tonemapper) { // inputs are LINEAR
	// Ensure color values passed to tonemappers are positive.
	// They can be negative in the case of negative lights, which leads to undesired behavior.
	if (tonemapper == TONEMAPPER_LINEAR) {
		return color;
	} else if (tonemapper == TONEMAPPER_REINHARD) {
		return tonemap_reinhard(max(vec3(0.0), color), p_white);
	} else if (tonemapper == TONEMAPPER_FILMIC) {
		return tonemap_filmic(max(vec3(0.0), color), p_white);
	} else if (tonemapper == TONEMAPPER_ACES) {
		return tonemap_aces(max(vec3(0.0), color), p_white);
	} else if (tonemapper == TONEMAPPER_AGX) {
		return tonemap_agx(color);
	} else {
		return tonemap_drago(max(vec3(0.0), color), p_white);
	}
}

#endif // APPLY_TONEMAPPING
