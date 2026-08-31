#ifndef DISABLE_LIGHT_DIRECTIONAL

// Directional Light Data
struct DirectionalLightData {
	highp vec4 direction_energy;
	highp vec4 color_size;
	highp vec4 extended_data;
};
struct DirectionalLights { 
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
};
uniform DirectionalLights directional_lights;
#endif

#if !defined(DISABLE_LIGHT_OMNI) || !defined(DISABLE_LIGHT_SPOT)
struct LightData { 
	highp vec4 position_inv_radius;
	highp vec4 direction_size;
	highp vec4 color_attenuation;
	highp vec4 cone_attenuation_angle_specular_shadow;
};
#endif

#ifndef DISABLE_LIGHT_OMNI
struct OmniLights {
    LightData data[MAX_FORWARD_LIGHTS];
};
uniform OmniLights omni_lights;
uniform int omni_light_count;
#endif

#ifndef DISABLE_LIGHT_SPOT
struct SpotLights {
    LightData data[MAX_FORWARD_LIGHTS];
};
uniform SpotLights spot_lights;
uniform int spot_light_count;
#endif