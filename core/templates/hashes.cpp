/**************************************************************************/
/*  hashes.cpp                                                            */
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

#include "hashes.h"

#include <cstring>

void Hashes::allocate(uint32_t p_real_capacity) {
	reset();
	ptr = reinterpret_cast<uint8_t *>(
		Memory::alloc_aligned_static(
			static_cast<size_t>(p_real_capacity) + HashGroup::GROUP_SIZE,
			alignof(USize)
		)
	);
	memset(ptr, EMPTY_HASH, p_real_capacity);
	memset(ptr + p_real_capacity, END_HASH, HashGroup::GROUP_SIZE);
	num_tombstones = 0;
}

void Hashes::clear(uint32_t p_real_capacity) {
	if (ptr != nullptr) {
		memset(ptr, EMPTY_HASH, p_real_capacity);
	}
	num_tombstones = 0;
}

void Hashes::copy_from(const Hashes &p_other, uint32_t p_real_capacity) {
	reset();
	if (p_other.ptr != nullptr) {
		const size_t total_size = static_cast<size_t>(p_real_capacity) + HashGroup::GROUP_SIZE;
		ptr = reinterpret_cast<uint8_t *>(Memory::alloc_aligned_static(total_size, alignof(USize)));
		memcpy(ptr, p_other.ptr, total_size);
		num_tombstones = p_other.num_tombstones;
	}
}

void Hashes::reset() {
	if (ptr != nullptr) {
		Memory::free_aligned_static(ptr);
		ptr = nullptr;
	}
	num_tombstones = 0;
}

uint32_t Hashes::insert_hash(uint32_t p_hash, uint32_t p_capacity) {
	uint32_t pos = p_hash & p_capacity;
	uint8_t stored_hash = _get_stored_hash(p_hash);

	if (ptr[pos] <= DELETED_HASH) {
		if (ptr[pos] == DELETED_HASH) {
			num_tombstones--;
		}
		ptr[pos] = stored_hash;
		return pos;
	}

	pos = (pos + 1) & p_capacity;

	while (true) {
		HashGroup group(&ptr[pos]);
		USize mask = group.get_compare_mask(EMPTY_HASH) | group.get_compare_mask(DELETED_HASH);
		if (likely(mask)) {
			uint32_t actual_pos = pos + HashGroup::first_match_index(mask);
			if (ptr[actual_pos] == DELETED_HASH) {
				num_tombstones--;
			}
			ptr[actual_pos] = stored_hash;
			return actual_pos;
		}

		pos += HashGroup::GROUP_SIZE;
		if (pos > p_capacity) {
			pos = 0;
		}
	}
}
