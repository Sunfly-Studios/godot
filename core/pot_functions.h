/**************************************************************************/
/*  pot_functions.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef POT_FUNCTIONS_H
#define POT_FUNCTIONS_H

/* Functions to handle powers of 2 and shifting. */

// Returns `true` if a positive integer is a power of 2, `false` otherwise.
template <typename T>
constexpr bool is_power_of_2(const T x) {
	return x && ((x & (x - 1)) == 0);
}

#if defined(_MSC_VER)

// MSVC does not provide CLZ functions, so we define our own here.
static _FORCE_INLINE_ unsigned int __clz_32(unsigned int value) {
	unsigned long num = 0;
	_BitScanReverse(&num, value);
	return 31 - num;
}

static _FORCE_INLINE_ unsigned int __clz_64(unsigned long value) {
	unsigned long num = 0;
	_BitScanReverse64(&num, value);
	return 63 - num;
}

// Next power of 2 (32-bit)
static _FORCE_INLINE_ unsigned int next_power_of_2(unsigned int x) {
	if (x == 0) {
		return 0;
	}
	return 0x80000000 >> __clz_32((x << 1) - 1);
}

// Previous power of 2 (32-bit)
static _FORCE_INLINE_ unsigned int previous_power_of_2(unsigned int x) {
	if (x == 0) {
		return 0;
	}
	return 0x80000000 >> __clz_32(x);
}

// Next power of 2 (64-bit)
static _FORCE_INLINE_ unsigned int next_power_of_2_64(unsigned long x) {
	if (x == 0) {
		return 0;
	}
	return 0x8000000000000000 >> __clz_64((x << 1) - 1);
}

// Previous power of 2 (64-bit)
static _FORCE_INLINE_ unsigned int previous_power_of_2_64(unsigned long x) {
	if (x == 0) {
		return 0;
	}
	return 0x8000000000000000 >> __clz_64(x);
}

// Get a shift value from a power of 2.
static _FORCE_INLINE_ int get_shift_from_power_of_2(unsigned int p_bits) {
	if (p_bits && !(p_bits & (p_bits - 1))) {
		return 31 - __clz_32(p_bits);
	}
	return -1;
}

// Function to find the nearest (bigger) power of 2 to an integer.
static _FORCE_INLINE_ unsigned int nearest_shift(unsigned int p_number) {
	if (p_number == 0) {
		return 0;
	}

	int leading_zeros = __clz_32(p_number);
	return 32 - leading_zeros;
}

#elif defined(__GNUC__) || defined(__clang__)

// Next power of 2 (32-bit)
static _FORCE_INLINE_ unsigned int next_power_of_2(unsigned int x) {
	if (x == 0) {
		return 0;
	}
	return 0x80000000 >> __builtin_clz((x << 1) - 1);
}

// Previous power of 2 (32-bit)
static _FORCE_INLINE_ unsigned int previous_power_of_2(unsigned int x) {
	if (x == 0) {
		return 0;
	}
	return 0x80000000 >> __builtin_clz(x);
}

// Next power of 2 (64-bit)
static _FORCE_INLINE_ unsigned int next_power_of_2_64(unsigned long x) {
	if (x == 0) {
		return 0;
	}
	return 0x8000000000000000 >> __builtin_clzl((x << 1) - 1);
}

// Previous power of 2 (64-bit)
static _FORCE_INLINE_ unsigned int previous_power_of_2_64(unsigned long x) {
	if (x == 0) {
		return 0;
	}
	return 0x8000000000000000 >> __builtin_clzl(x);
}

// Get a shift value from a power of 2.
static _FORCE_INLINE_ int get_shift_from_power_of_2(unsigned int p_bits) {
	if (p_bits && !(p_bits & (p_bits - 1))) {
		return 31 - __builtin_clz(p_bits);
	}
	return -1;
}

// Function to find the nearest (bigger) power of 2 to an integer.
static _FORCE_INLINE_ unsigned int nearest_shift(unsigned int p_number) {
	if (p_number == 0) {
		return 0;
	}

	int leading_zeros = __builtin_clz(p_number);
	return 32 - leading_zeros;
}

#else

// Function to find the next power of 2 to an integer.
static _FORCE_INLINE_ unsigned int next_power_of_2(unsigned int x) {
	if (x == 0) {
		return 0;
	}

	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	return ++x;
}

// Function to find the previous power of 2 to an integer (32-bit).
static _FORCE_INLINE_ unsigned int previous_power_of_2(unsigned int x) {
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return x - (x >> 1);
}

// Function to find the previous power of 2 to an integer (64-bit).
static _FORCE_INLINE_ unsigned int next_power_of_2_64(unsigned long x) {
	if (x == 0) {
		return 0;
	}

	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;

	return ++x;
}

// Get a shift value from a power of 2.
static inline int get_shift_from_power_of_2(unsigned int p_bits) {
	for (unsigned int i = 0; i < 32; i++) {
		if (p_bits == (unsigned int)(1 << i)) {
			return i;
		}
	}

	return -1;
}

// Function to find the nearest (bigger) power of 2 to an integer.
static inline unsigned int nearest_shift(unsigned int p_number) {
	for (int i = 30; i >= 0; i--) {
		if (p_number & (1 << i)) {
			return i + 1;
		}
	}

	return 0;
}

#endif // _MSC_VER || (__GNUC__) || (__clang__)

// Function to find the closest power of 2 to an integer.
static _FORCE_INLINE_ unsigned int closest_power_of_2(unsigned int x) {
	unsigned int nx = next_power_of_2(x);
	unsigned int px = previous_power_of_2(x);
	return (nx - x) > (x - px) ? px : nx;
}

template <typename T>
static constexpr T nearest_power_of_2_templated(T x) {
	--x;

	// The number of operations on x is the base two logarithm
	// of the number of bits in the type. Add three to account
	// for sizeof(T) being in bytes.
	size_t num = get_shift_from_power_of_2(sizeof(T)) + 3;

	// If the compiler is smart, it unrolls this loop.
	// If it's dumb, this is a bit slow.
	for (size_t i = 0; i < num; i++) {
		x |= x >> (1 << i);
	}

	return ++x;
}

#endif // POT_FUNCTIONS_H
