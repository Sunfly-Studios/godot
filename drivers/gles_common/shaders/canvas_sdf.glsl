#[modes]
mode_default =
mode_load = #define MODE_LOAD
mode_load_shrink = #define MODE_LOAD_SHRINK
mode_process = #define MODE_PROCESS
mode_store = #define MODE_STORE
mode_store_shrink = #define MODE_STORE_SHRINK

#[specializations]

#[vertex]

attribute vec2 vertex_attrib; // attrib:0
void main() {
    gl_Position = vec4(vertex_attrib, 0.0, 1.0);
}

#[fragment]

#define SDF_MAX_LENGTH 16384.0

#include "stdlib_inc.glsl"

uniform mediump vec2 size;
uniform mediump int stride;
uniform mediump int shift;
uniform mediump vec2 base_size;

#if defined(MODE_LOAD) || defined(MODE_LOAD_SHRINK)
uniform sampler2D src_pixels; // texunit:0
#else
uniform sampler2D src_process; // texunit:0
#endif

void main() {
    mediump vec2 pos = gl_FragCoord.xy;
    mediump float s = pow(2.0, float(shift)); // Replaces: 1 << shift

#if defined(MODE_LOAD)
    mediump vec4 sample = texel2DFetch(src_pixels, pos, base_size);
    mediump bool solid = sample.r > 0.5;
    gl_FragColor = solid ? vec4(vec2(-32767.0), 0.0, 0.0) : vec4(vec2(32767.0), 0.0, 0.0);
#endif

#if defined(MODE_LOAD_SHRINK)
    mediump vec2 base = pos * s; // Replaces: pos << shift
    mediump vec2 center = base + vec2(s * 0.5);

    mediump vec2 rel = vec2(32767.0);
    mediump float d = 1e20;
    int found = 0;
    int solid_found = 0;

    // Constant-bound loop (covers shift up to 4)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (float(i) >= s || float(j) >= s) {
                continue;
            }
            mediump vec2 src_pos = base + vec2(float(i), float(j));
            if (src_pos.x >= base_size.x || src_pos.y >= base_size.y) {
                continue;
            }

            mediump vec4 sample = texel2DFetch(src_pixels, src_pos, base_size);
            mediump bool solid = sample.r > 0.5;
            if (solid) {
                mediump float dist = length(src_pos - center);
                if (dist < d) {
                    d = dist;
                    rel = src_pos;
                }
                solid_found++;
            }
            found++;
        }
    }

    if (solid_found == found && found > 0) {
        rel = vec2(-32767.0);
    }
    gl_FragColor = vec4(rel, 0.0, 0.0);
#endif

#if defined(MODE_PROCESS)
    mediump vec2 base = pos * s;
    mediump vec2 center = base + vec2(s * 0.5);

    mediump vec4 sample = texel2DFetch(src_process, pos, size);
    mediump vec2 rel = sample.xy;
    mediump bool solid = rel.x < 0.0;

    if (solid) {
        rel = -rel - vec2(1.0);
    }

    if (rel != center) {
        mediump float dist = length(rel - center);
        // Unrolled 8-neighbor check
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                if (i == 0 && j == 0) {
                    continue;
                }
                mediump vec2 src_pos = pos + vec2(float(i), float(j)) * float(stride);
                if (src_pos.x < 0.0 || src_pos.y < 0.0 || src_pos.x >= size.x || src_pos.y >= size.y) {
                    continue;
                }

                mediump vec4 src_sample = texel2DFetch(src_process, src_pos, size);
                mediump vec2 src_rel = src_sample.xy;
                mediump bool src_solid = src_rel.x < 0.0;

                if (src_solid) {
                    src_rel = -src_rel - vec2(1.0);
                }

                if (src_solid != solid) {
                    src_rel = src_pos * s; // Replaces: src_pos << shift
                }

                mediump float src_dist = length(src_rel - center);
                if (src_dist < dist) {
                    dist = src_dist;
                    rel = src_rel;
                }
            }
        }
    }

    if (solid) {
        rel = -rel - vec2(1.0);
    }
    gl_FragColor = vec4(rel, 0.0, 0.0);
#endif

#if defined(MODE_STORE)
    mediump vec4 sample = texel2DFetch(src_process, pos, size);
    mediump vec2 rel = sample.xy;
    mediump bool solid = rel.x < 0.0;

    if (solid) {
        rel = -rel - vec2(1.0);
    }

    mediump float d = length(rel - pos);
    if (solid) {
        d = -d;
    }
    d /= SDF_MAX_LENGTH;
    d = clamp(d, -1.0, 1.0);
    gl_FragColor = float_to_vec4(d * 0.5 + 0.5);
#endif

#if defined(MODE_STORE_SHRINK)
    mediump vec2 base = pos * s;
    mediump vec2 center = base + vec2(s * 0.5);

    mediump vec4 sample = texel2DFetch(src_process, pos, size);
    mediump vec2 rel = sample.xy;
    mediump bool solid = rel.x < 0.0;

    if (solid) {
        rel = -rel - vec2(1.0);
    }

    mediump float d = length(rel - center);
    if (solid) {
        d = -d;
    }
    d /= SDF_MAX_LENGTH;
    d = clamp(d, -1.0, 1.0);
    gl_FragColor = float_to_vec4(d * 0.5 + 0.5);
#endif
}