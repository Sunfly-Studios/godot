// Operands are wrapped into an unsigned bit range defined by this macro,
// and results are returned in that range.
// - 8 bits  ~= 65 instructions per AND
// - 16 bits ~= 130 instructions per AND (default)
// - 24 bits is the practical maximum (float mantissa)
#ifndef GLES2_BIT_WIDTH
#define GLES2_BIT_WIDTH 16
#define GLES2_BIT_RANGE 65536.0
#endif

#ifndef M_PI
#define M_PI 3.14159265359
#endif 

// Common
vec4 _gles2_wrap(vec4 x) { return mod(x, GLES2_BIT_RANGE); }
vec4 _gles2_and4(vec4 a, vec4 b) {
	vec4 res = vec4(0.0);
	float w = 4.0;
	for (int i = 0; i < GLES2_BIT_WIDTH; i++) {
		a *= 0.5;
		b *= 0.5;
		vec4 fa = fract(a);
		vec4 fb = fract(b);
		res += w * (fa * fb);
		a -= fa;
		b -= fb;
		w *= 2.0;
	}
	return res;
}
vec2 _gles2_and2(vec4 v) {
	vec2 res = vec2(0.0);
	float w = 4.0;
	for (int i = 0; i < GLES2_BIT_WIDTH; i++) {
		v *= 0.5;
		vec4 f = fract(v);
		res += w * (f.xy * f.zw);
		v -= f;
		w *= 2.0;
	}
	return res;
}

#if defined(LSHIFT_USED)
// Shift left

vec4 _gles2_shl4(vec4 a, vec4 b) {
	vec4 p = floor(exp2(clamp(b, 0.0, 31.0)) + 0.5);
	return a * p * step(b, vec4(30.5));
}

int   _gles2_shl(int a, int b)     { return int(_gles2_shl4(vec4(float(a)), vec4(float(b))).x); }
ivec2 _gles2_shl(ivec2 a, ivec2 b) { return ivec2(_gles2_shl4(vec4(vec2(a), 0.0, 0.0), vec4(vec2(b), 0.0, 0.0)).xy); }
ivec2 _gles2_shl(ivec2 a, int b)   { return ivec2(_gles2_shl4(vec4(vec2(a), 0.0, 0.0), vec4(float(b))).xy); }
ivec3 _gles2_shl(ivec3 a, ivec3 b) { return ivec3(_gles2_shl4(vec4(vec3(a), 0.0), vec4(vec3(b), 0.0)).xyz); }
ivec3 _gles2_shl(ivec3 a, int b)   { return ivec3(_gles2_shl4(vec4(vec3(a), 0.0), vec4(float(b))).xyz); }
ivec4 _gles2_shl(ivec4 a, ivec4 b) { return ivec4(_gles2_shl4(vec4(a), vec4(b))); }
ivec4 _gles2_shl(ivec4 a, int b)   { return ivec4(_gles2_shl4(vec4(a), vec4(float(b)))); }

#endif // LSHIFT_USED

#if defined(RSHIFT_USED)
// Shift right

vec4 _gles2_shr4(vec4 a, vec4 b) {
	return floor((a + 0.5) * exp2(-clamp(b, 0.0, 31.0)));
}

int   _gles2_shr(int a, int b)     { return int(_gles2_shr4(vec4(float(a)), vec4(float(b))).x); }
ivec2 _gles2_shr(ivec2 a, ivec2 b) { return ivec2(_gles2_shr4(vec4(vec2(a), 0.0, 0.0), vec4(vec2(b), 0.0, 0.0)).xy); }
ivec2 _gles2_shr(ivec2 a, int b)   { return ivec2(_gles2_shr4(vec4(vec2(a), 0.0, 0.0), vec4(float(b))).xy); }
ivec3 _gles2_shr(ivec3 a, ivec3 b) { return ivec3(_gles2_shr4(vec4(vec3(a), 0.0), vec4(vec3(b), 0.0)).xyz); }
ivec3 _gles2_shr(ivec3 a, int b)   { return ivec3(_gles2_shr4(vec4(vec3(a), 0.0), vec4(float(b))).xyz); }
ivec4 _gles2_shr(ivec4 a, ivec4 b) { return ivec4(_gles2_shr4(vec4(a), vec4(b))); }
ivec4 _gles2_shr(ivec4 a, int b)   { return ivec4(_gles2_shr4(vec4(a), vec4(float(b)))); }

#endif // RSHIFT_USED

#if defined(BAND_USED)
// Bitwise AND

int   _gles2_bit_and(int a, int b)     { return int(_gles2_and2(_gles2_wrap(vec4(float(a), 0.0, float(b), 0.0))).x); }
ivec2 _gles2_bit_and(ivec2 a, ivec2 b) { return ivec2(_gles2_and2(_gles2_wrap(vec4(vec2(a), vec2(b))))); }
ivec2 _gles2_bit_and(ivec2 a, int b)   { return ivec2(_gles2_and2(_gles2_wrap(vec4(vec2(a), float(b), float(b))))); }
ivec3 _gles2_bit_and(ivec3 a, ivec3 b) { return ivec3(_gles2_and4(_gles2_wrap(vec4(vec3(a), 0.0)), _gles2_wrap(vec4(vec3(b), 0.0))).xyz); }
ivec3 _gles2_bit_and(ivec3 a, int b)   { return ivec3(_gles2_and4(_gles2_wrap(vec4(vec3(a), 0.0)), _gles2_wrap(vec4(float(b)))).xyz); }
ivec4 _gles2_bit_and(ivec4 a, ivec4 b) { return ivec4(_gles2_and4(_gles2_wrap(vec4(a)), _gles2_wrap(vec4(b)))); }
ivec4 _gles2_bit_and(ivec4 a, int b)   { return ivec4(_gles2_and4(_gles2_wrap(vec4(a)), _gles2_wrap(vec4(float(b))))); }

#endif // BAND_USED

#if defined(BOR_USED)
// Bitwise OR

vec2 _gles2_or2(vec4 v) { return v.xy + v.zw - _gles2_and2(v); }
vec4 _gles2_or4(vec4 a, vec4 b) { return a + b - _gles2_and4(a, b); }

int   _gles2_bit_or(int a, int b)     { return int(_gles2_or2(_gles2_wrap(vec4(float(a), 0.0, float(b), 0.0))).x); }
ivec2 _gles2_bit_or(ivec2 a, ivec2 b) { return ivec2(_gles2_or2(_gles2_wrap(vec4(vec2(a), vec2(b))))); }
ivec2 _gles2_bit_or(ivec2 a, int b)   { return ivec2(_gles2_or2(_gles2_wrap(vec4(vec2(a), float(b), float(b))))); }
ivec3 _gles2_bit_or(ivec3 a, ivec3 b) { return ivec3(_gles2_or4(_gles2_wrap(vec4(vec3(a), 0.0)), _gles2_wrap(vec4(vec3(b), 0.0))).xyz); }
ivec3 _gles2_bit_or(ivec3 a, int b)   { return ivec3(_gles2_or4(_gles2_wrap(vec4(vec3(a), 0.0)), _gles2_wrap(vec4(float(b)))).xyz); }
ivec4 _gles2_bit_or(ivec4 a, ivec4 b) { return ivec4(_gles2_or4(_gles2_wrap(vec4(a)), _gles2_wrap(vec4(b)))); }
ivec4 _gles2_bit_or(ivec4 a, int b)   { return ivec4(_gles2_or4(_gles2_wrap(vec4(a)), _gles2_wrap(vec4(float(b))))); }

#endif // BOR_USED

#if defined(BXOR_USED)
// Bitwise XOR

vec2 _gles2_xor2(vec4 v) { return v.xy + v.zw - 2.0 * _gles2_and2(v); }
vec4 _gles2_xor4(vec4 a, vec4 b) { return a + b - 2.0 * _gles2_and4(a, b); }

int   _gles2_bit_xor(int a, int b)     { return int(_gles2_xor2(_gles2_wrap(vec4(float(a), 0.0, float(b), 0.0))).x); }
ivec2 _gles2_bit_xor(ivec2 a, ivec2 b) { return ivec2(_gles2_xor2(_gles2_wrap(vec4(vec2(a), vec2(b))))); }
ivec2 _gles2_bit_xor(ivec2 a, int b)   { return ivec2(_gles2_xor2(_gles2_wrap(vec4(vec2(a), float(b), float(b))))); }
ivec3 _gles2_bit_xor(ivec3 a, ivec3 b) { return ivec3(_gles2_xor4(_gles2_wrap(vec4(vec3(a), 0.0)), _gles2_wrap(vec4(vec3(b), 0.0))).xyz); }
ivec3 _gles2_bit_xor(ivec3 a, int b)   { return ivec3(_gles2_xor4(_gles2_wrap(vec4(vec3(a), 0.0)), _gles2_wrap(vec4(float(b)))).xyz); }
ivec4 _gles2_bit_xor(ivec4 a, ivec4 b) { return ivec4(_gles2_xor4(_gles2_wrap(vec4(a)), _gles2_wrap(vec4(b)))); }
ivec4 _gles2_bit_xor(ivec4 a, int b)   { return ivec4(_gles2_xor4(_gles2_wrap(vec4(a)), _gles2_wrap(vec4(float(b))))); }

#endif // BXOR_USED

#if defined(BNOT_USED)
// Bitwise NOT (two's complement: ~a == -a - 1)

int   _gles2_bit_not(int a)   { return -a - 1; }
ivec2 _gles2_bit_not(ivec2 a) { return -a - ivec2(1); }
ivec3 _gles2_bit_not(ivec3 a) { return -a - ivec3(1); }
ivec4 _gles2_bit_not(ivec4 a) { return -a - ivec4(1); }

#endif // BNOT_USED
