vec3 F0(float metallic, float specular, vec3 albedo) {
	float dielectric = 0.16 * specular * specular;
	return mix(vec3(dielectric), albedo, vec3(metallic));
}

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
	float w2 = alpha2 / dot(v, v);
	return alpha2 * w2 * w2 * (1.0 / M_PI);
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

float get_omni_spot_attenuation(float distance, float inv_range, float decay) {
	float nd = max(1.0 - pow(distance * inv_range, 4.0), 0.0);
	return (nd * nd) * pow(max(distance, 0.0001), -decay);
}

void light_compute(vec3 N, vec3 L, vec3 V, float A, vec3 light_color, float attenuation, vec3 f0, float roughness, float metallic, float specular_amount, vec3 albedo, inout float alpha, inout vec3 diffuse_light, inout vec3 specular_light) {
#if defined(USE_LIGHT_SHADER_CODE)
	vec3 normal = N;
	vec3 light = L;
	vec3 view = V;

/* clang-format off */

#CODE : LIGHT

/* clang-format on */

#else
#if defined(USE_ADDITIVE_LIGHTING)
	// Fast-path Lambertian
	float NdotL = max(A + dot(N, L), 0.0);
	diffuse_light += light_color * NdotL * attenuation * (1.0 / M_PI);
#else
	float NdotL = min(A + dot(N, L), 1.0);
	float cNdotL = max(NdotL, 0.0); 
	float NdotV = dot(N, V);
	float cNdotV = max(NdotV, 1e-4);
	vec3 H = normalize(V + L);
	float cNdotH = clamp(A + dot(N, H), 0.0, 1.0);
	float cLdotH = clamp(A + dot(L, H), 0.0, 1.0);

	if (metallic < 1.0) {
		diffuse_light += light_color * cNdotL * (1.0 / M_PI) * attenuation;
	}

	if (roughness > 0.0) { 
		float alpha_ggx = roughness * roughness;
		float D = D_GGX(cNdotH, alpha_ggx);
		float G = V_GGX(cNdotL, cNdotV, alpha_ggx);
		vec3 F = f0 + (clamp(50.0 * f0.g, 0.0, 1.0) - f0) * SchlickFresnel(cLdotH);
		specular_light += cNdotL * D * F * G * light_color * attenuation * specular_amount;
	}
#endif // USE_ADDITIVE_LIGHTING

#ifdef USE_SHADOW_TO_OPACITY
	alpha = min(alpha, clamp(1.0 - attenuation, 0.0, 1.0));
#endif

#endif // USE_LIGHT_SHADER_CODE
}

#ifndef DISABLE_LIGHT_OMNI
void light_process_omni(LightData light, vec3 vertex, vec3 eye_vec, vec3 normal, vec3 f0, float roughness, float metallic, float shadow, vec3 albedo, inout float alpha, inout vec3 diffuse_light, inout vec3 specular_light) {
	vec3 light_rel_vec = light.position_inv_radius.xyz - vertex;
	float light_length = length(light_rel_vec);
	float omni_attenuation = get_omni_spot_attenuation(light_length, light.position_inv_radius.w, light.color_attenuation.w) * shadow;
	float size_A = light.direction_size.w > 0.0 ? max(0.0, 1.0 - 1.0 / sqrt(1.0 + pow(light.direction_size.w / max(0.001, light_length), 2.0))) : 0.0;

	light_compute(normal, normalize(light_rel_vec), eye_vec, size_A, light.color_attenuation.xyz, omni_attenuation, f0, roughness, metallic, light.cone_attenuation_angle_specular_shadow.z, albedo, alpha, diffuse_light, specular_light);
}
#endif

#ifndef DISABLE_LIGHT_SPOT
void light_process_spot(LightData light, vec3 vertex, vec3 eye_vec, vec3 normal, vec3 f0, float roughness, float metallic, float shadow, vec3 albedo, inout float alpha, inout vec3 diffuse_light, inout vec3 specular_light) {
	vec3 light_rel_vec = light.position_inv_radius.xyz - vertex;
	float light_length = length(light_rel_vec);
	float spot_attenuation = get_omni_spot_attenuation(light_length, light.position_inv_radius.w, light.color_attenuation.w);
	vec3 spot_dir = light.direction_size.xyz;
	float scos = max(dot(-normalize(light_rel_vec), spot_dir), light.cone_attenuation_angle_specular_shadow.y);
	float spot_rim = max(0.0001, (1.0 - scos) / (1.0 - light.cone_attenuation_angle_specular_shadow.y));
	spot_attenuation *= (1.0 - pow(spot_rim, light.cone_attenuation_angle_specular_shadow.x)) * shadow;
	
	float size_A = light.direction_size.w > 0.0 ? max(0.0, 1.0 - 1.0 / sqrt(1.0 + pow(light.direction_size.w / max(0.001, light_length), 2.0))) : 0.0;
	light_compute(normal, normalize(light_rel_vec), eye_vec, size_A, light.color_attenuation.xyz, spot_attenuation, f0, roughness, metallic, light.cone_attenuation_angle_specular_shadow.z, albedo, alpha, diffuse_light, specular_light);
}
#endif