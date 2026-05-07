/* clang-format off */
#[modes]

mode_sdf =
mode_shadow = #define MODE_SHADOW
mode_shadow_RGBA = #define MODE_SHADOW \n#define USE_RGBA_SHADOWS

#[specializations]

#[vertex]

attribute highp vec3 vertex_attrib; // attrib:0

uniform highp mat4 projection;
uniform highp vec4 modelview1;
uniform highp vec4 modelview2;
uniform highp vec2 direction;
uniform highp float z_far;

#ifdef MODE_SHADOW
varying highp float depth;
#endif

void main() {
	highp vec4 vtx = vec4(vertex_attrib, 1.0) * mat4(modelview1, modelview2, vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));

#ifdef MODE_SHADOW
	depth = dot(direction, vtx.xy);
#endif
	gl_Position = projection * vtx;
}

#[fragment]

uniform highp mat4 projection;
uniform highp vec4 modelview1;
uniform highp vec4 modelview2;
uniform highp vec2 direction;
uniform highp float z_far;

#ifdef MODE_SHADOW
varying highp float depth;
#endif

void main() {
	highp float out_depth = 1.0;

#ifdef MODE_SHADOW
	out_depth = depth / z_far;
#endif

#ifdef USE_RGBA_SHADOWS
	out_depth = clamp(out_depth, -1.0, 1.0);
	out_depth = out_depth * 0.5 + 0.5;
	highp vec4 comp = fract(out_depth * vec4(255.0 * 255.0 * 255.0, 255.0 * 255.0, 255.0, 1.0));
	comp -= comp.xxyz * vec4(0.0, 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0);
	gl_FragColor = comp;
#else
	gl_FragColor = vec4(out_depth, 0.0, 0.0, 1.0);
#endif
}