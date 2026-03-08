/* clang-format off */
[fragment]

varying highp vec2 uv_interp;

/* clang-format on */

uniform highp float dummy_force_enum; 

#ifdef USE_EXTERNAL_SAMPLER
#extension GL_OES_EGL_image_external : require
uniform highp samplerExternalOES sourceFeed; //texunit:0
#else
uniform highp sampler2D sourceFeed; //texunit:0
#endif

void main() {
    highp vec4 color = texture2D(sourceFeed, uv_interp);
    // You don't even have to use the dummy uniform, but if the GLSL compiler 
    // optimizes it out and the C++ side complains, just multiply it by 0:
    // gl_FragColor = color + (dummy_force_enum * 0.0);
    gl_FragColor = color + (dummy_force_enum * 0.0);
}