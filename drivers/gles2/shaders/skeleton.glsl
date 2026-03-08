/* clang-format off */
[vertex]

// Replaced all VFORMAT/OFORMAT and unsigned types (uvec4, uvec2) with standard GLES2 types.
attribute highp vec3 in_vertex; //attrib:0

#ifdef MODE_BLEND_PASS
#ifdef USE_NORMAL
attribute highp vec2 in_normal; //attrib:1
#endif
#ifdef USE_TANGENT
attribute highp vec2 in_tangent; //attrib:2
#endif
#else // MODE_BLEND_PASS
#ifdef USE_NORMAL
attribute highp vec2 in_normal; //attrib:1
#endif
#ifdef USE_TANGENT
attribute highp vec2 in_tangent; //attrib:2
#endif
#endif // MODE_BLEND_PASS

#ifdef USE_SKELETON
#ifdef USE_EIGHT_WEIGHTS
attribute highp vec4 in_bone_attrib; //attrib:10
attribute highp vec4 in_bone_attrib2; //attrib:11
attribute mediump vec4 in_weight_attrib; //attrib:12
attribute mediump vec4 in_weight_attrib2; //attrib:13
#else
attribute highp vec4 in_bone_attrib; //attrib:10
attribute mediump vec4 in_weight_attrib; //attrib:11
#endif

uniform highp sampler2D skeleton_texture; //texunit:0
#endif

/* clang-format on */

#ifdef MODE_BLEND_PASS
attribute highp vec3 blend_vertex; //attrib:3
#ifdef USE_NORMAL
attribute highp vec2 blend_normal; //attrib:4
#endif
#ifdef USE_TANGENT
attribute highp vec2 blend_tangent; //attrib:5
#endif
#endif // MODE_BLEND_PASS

#ifdef USE_BLEND_SHAPES
uniform highp float blend_weight;
uniform lowp float blend_shape_count;
#endif

#ifdef USE_SKELETON
uniform mediump vec2 skeleton_transform_x;
uniform mediump vec2 skeleton_transform_y;
uniform mediump vec2 skeleton_transform_offset;

uniform mediump vec2 inverse_transform_x;
uniform mediump vec2 inverse_transform_y;
uniform mediump vec2 inverse_transform_offset;
#endif

// We need a dummy conditional to ensure FINAL_PASS and MODE_2D are 
// picked up by the Python parser so the C++ enums match perfectly.
#ifdef FINAL_PASS
#endif
#ifdef MODE_2D
#endif

void main() {
    highp vec3 dummy = in_vertex;

#ifdef MODE_BLEND_PASS
    dummy += blend_vertex;
#ifdef USE_NORMAL
    dummy.xy += blend_normal + in_normal;
#endif
#ifdef USE_TANGENT
    dummy.xy += blend_tangent + in_tangent;
#endif
#endif

#ifdef USE_SKELETON
    dummy += in_bone_attrib.xyz + in_weight_attrib.xyz;
#ifdef USE_EIGHT_WEIGHTS
    dummy += in_bone_attrib2.xyz + in_weight_attrib2.xyz;
#endif
    dummy.xy += skeleton_transform_x + skeleton_transform_y + skeleton_transform_offset;
    dummy.xy += inverse_transform_x + inverse_transform_y + inverse_transform_offset;
    // Keep texture sampler alive
    dummy.xy += texture2D(skeleton_texture, vec2(0.0)).xy;
#endif

#ifdef USE_BLEND_SHAPES
    dummy *= blend_weight * blend_shape_count;
#endif

    // Fling the garbage data into the void
    gl_Position = vec4(dummy, 1.0);
}

/* clang-format off */
[fragment]

void main() {
    gl_FragColor = vec4(1.0);
}
/* clang-format on */