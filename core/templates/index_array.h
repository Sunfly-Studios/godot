/**************************************************************************/
/*  index_array.h                                                         */
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

#ifndef INDEX_ARRAY_H
#define INDEX_ARRAY_H

#include "core/os/memory.h"
#include "core/typedefs.h"

#include <cstdint>

/**
 * Stores an array of element indices using the smallest unsigned integer width
 * (1, 2, or 4 bytes) required to represent `p_max_index`.
 */
class IndexArray {
public:
	enum State : uint8_t {
		Small = 1,
		Medium = 2,
		Large = 4,
	};

private:
	union {
		uint8_t *small = nullptr;
		uint16_t *medium;
		uint32_t *large;
	};

	State state = Small;
	uint32_t size = 0;

	void _allocate(uint32_t p_size);

public:
	_FORCE_INLINE_ State get_size_state() const {
		return state;
	}

	_FORCE_INLINE_ void set_index(uint32_t p_pos, uint32_t p_index) {
		switch (state) {
			case Small:
				small[p_pos] = static_cast<uint8_t>(p_index);
				break;
			case Medium:
				medium[p_pos] = static_cast<uint16_t>(p_index);
				break;
			default:
				large[p_pos] = p_index;
				break;
		}
	}

	_FORCE_INLINE_ uint32_t get_index(uint32_t p_pos) const {
		switch (state) {
			case Small:
				return static_cast<uint32_t>(small[p_pos]);
			case Medium:
				return static_cast<uint32_t>(medium[p_pos]);
			default:
				return large[p_pos];
		}
	}

	size_t get_allocated_size() const;
	size_t get_alignment() const;

	// p_size: number of slots in the index array.
	// p_max_index: maximum index value that will be stored (e.g. max_elements - 1).
	void initialize(uint32_t p_size, uint32_t p_max_index);

	void operator=(const IndexArray &p_other);

	IndexArray(const IndexArray &p_other);

	IndexArray() = default;

	void reset();

	~IndexArray() {
		reset();
	}
};

#endif // INDEX_ARRAY_H
