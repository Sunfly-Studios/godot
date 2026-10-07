vec3 F0(float metallic, float specular, vec3 albedo) {
	float dielectric = 0.16 * specular * specular;
	return mix(vec3(dielectric), albedo, vec3(metallic));
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
		float shininess = exp2(15.0 * (1.0 - roughness) + 1.0) * 0.25;
		float blinn = pow(cNdotH, shininess);
		blinn *= (shininess + 8.0) * (1.0 / (8.0 * M_PI));

		float m = 1.0 - cLdotH;
		float m2 = m * m;
		float schlick = m2 * m2 * m;
		vec3 F = f0 + (clamp(50.0 * f0.g, 0.0, 1.0) - f0) * schlick;

		float visibility = blinn / max(4.0 * cNdotV * cNdotL, 0.75);
		specular_light += cNdotL * visibility * F * light_color * attenuation * specular_amount;
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