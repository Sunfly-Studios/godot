/**************************************************************************/
/*  generational_handle.h                                                 */
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

#ifndef GENERATIONAL_HANDLE_H
#define GENERATIONAL_HANDLE_H

#include "core/os/memory.h"
#include "core/templates/hashfuncs.h"
#include "core/typedefs.h"

// 32-bit generational handle packing a 20-bit index and 12-bit generation counter.
// Generation 0 is reserved for the null/invalid state.
class GenerationalHandle {
public:
	static constexpr uint32_t INDEX_BITS = 20;
	static constexpr uint32_t GENERATION_BITS = 12;

	static constexpr uint32_t MAX_INDEX = (1U << INDEX_BITS) - 1U;
	static constexpr uint32_t MAX_GENERATION = (1U << GENERATION_BITS) - 1U;

private:
	static constexpr uint32_t INDEX_MASK = MAX_INDEX;
	static constexpr uint32_t GENERATION_MASK = MAX_GENERATION;
	static constexpr uint32_t GENERATION_SHIFT = INDEX_BITS;

	uint32_t data = 0;

public:
	_FORCE_INLINE_ constexpr GenerationalHandle() = default;

	_FORCE_INLINE_ constexpr GenerationalHandle(uint32_t p_index, uint32_t p_generation) :
			data(((p_generation & GENERATION_MASK) << GENERATION_SHIFT) | (p_index & INDEX_MASK)) {}

	_FORCE_INLINE_ static constexpr GenerationalHandle from_raw(uint32_t p_raw) {
		GenerationalHandle h;
		h.data = p_raw;
		return h;
	}

	_FORCE_INLINE_ constexpr uint32_t get_index() const {
		return data & INDEX_MASK;
	}

	_FORCE_INLINE_ constexpr uint32_t get_generation() const {
		return (data >> GENERATION_SHIFT) & GENERATION_MASK;
	}

	_FORCE_INLINE_ constexpr uint32_t to_raw() const {
		return data;
	}

	_FORCE_INLINE_ constexpr bool is_valid() const {
		return get_generation() != 0;
	}

	_FORCE_INLINE_ constexpr bool is_null() const {
		return get_generation() == 0;
	}

	_FORCE_INLINE_ uint32_t hash() const {
		return hash_fmix32(data);
	}

	_FORCE_INLINE_ constexpr bool operator==(const GenerationalHandle &p_other) const {
		return data == p_other.data;
	}

	_FORCE_INLINE_ constexpr bool operator!=(const GenerationalHandle &p_other) const {
		return data != p_other.data;
	}

	_FORCE_INLINE_ constexpr bool operator<(const GenerationalHandle &p_other) const {
		return data < p_other.data;
	}

	_FORCE_INLINE_ constexpr bool operator<=(const GenerationalHandle &p_other) const {
		return data <= p_other.data;
	}

	_FORCE_INLINE_ constexpr bool operator>(const GenerationalHandle &p_other) const {
		return data > p_other.data;
	}

	_FORCE_INLINE_ constexpr bool operator>=(const GenerationalHandle &p_other) const {
		return data >= p_other.data;
	}
};

#endif // GENERATIONAL_HANDLE_H
