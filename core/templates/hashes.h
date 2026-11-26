/**************************************************************************/
/*  hashes.h                                                              */
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

#ifndef HASHES_H
#define HASHES_H

#include "core/os/memory.h"
#include "core/typedefs.h"

#include <cstdint>

struct HashGroup {
	static constexpr uint32_t GROUP_SIZE = sizeof(USize);

private:
#if defined(IS_32_BIT)
	static constexpr USize ONES = 0x01010101U;
	static constexpr USize LOWS = 0x7F7F7F7FU;
	static constexpr USize HIGH_BIT = 0x80000000U;
#else
	static constexpr USize ONES = 0x0101010101010101ULL;
	static constexpr USize LOWS = 0x7F7F7F7F7F7F7F7FULL;
	static constexpr USize HIGH_BIT = 0x8000000000000000ULL;
#endif

	USize word;

public:
	_FORCE_INLINE_ explicit HashGroup(const uint8_t *p_begin) :
			word(unaligned_read<USize>(p_begin)) {}

	_FORCE_INLINE_ USize get_compare_mask(uint8_t p_value) const {
		// Non-propagating SWAR zero-byte test: (x & LOWS) + LOWS is <= 0xFE per lane.
		USize x = word ^ (ONES * static_cast<USize>(p_value));
		return ~(((x & LOWS) + LOWS) | x | LOWS);
	}

	// Precondition: p_mask != 0. Returns byte offset [0, GROUP_SIZE - 1] of the first match.
	static _FORCE_INLINE_ uint32_t first_match_index(USize p_mask) {
#if defined(BIG_ENDIAN_ENABLED)
        // Use clz
		return static_cast<uint32_t>(find_first_leading_set_bit(p_mask) >> 3);
#else
        // Use ctz
		return static_cast<uint32_t>(find_first_trailing_set_bit(p_mask) >> 3);
#endif
	}

	// Precondition: p_mask != 0. Clears the first matching byte's bit from p_mask.
	static _FORCE_INLINE_ USize clear_first_match(USize p_mask) {
#if defined(BIG_ENDIAN_ENABLED)
		uint8_t bit = find_first_leading_set_bit(p_mask);
        
        // Mask high bit position to byte lane offset
		return p_mask ^ (HIGH_BIT >> bit);
#else
		return p_mask & (p_mask - 1);
#endif
	}
};

struct Hashes {
private:
	static _FORCE_INLINE_ uint8_t _get_stored_hash(uint32_t p_hash) {
		uint8_t stored_hash = static_cast<uint8_t>(p_hash >> 24);
		if (unlikely(stored_hash <= END_HASH)) {
			stored_hash += END_HASH + 1;
		}
		return stored_hash;
	}

public:
	static constexpr uint8_t EMPTY_HASH = 0;
	static constexpr uint8_t DELETED_HASH = 1;
	static constexpr uint8_t END_HASH = 2;

	static_assert(EMPTY_HASH == 0 && DELETED_HASH == 1 && END_HASH == 2);

	uint8_t *ptr = nullptr;
	uint32_t num_tombstones = 0;

	Hashes() = default;
	Hashes(const Hashes &) = delete;
	Hashes &operator=(const Hashes &) = delete;

	~Hashes() {
		reset();
	}

	void allocate(uint32_t p_real_capacity);
	void clear(uint32_t p_real_capacity);
	void copy_from(const Hashes &p_other, uint32_t p_real_capacity);
	void reset();

	template <typename Container, typename TKey>
	inline bool lookup_pos_with_hash(const Container *p_container, TKey &&p_key, uint32_t p_hash, uint32_t p_capacity, uint32_t &r_hash_pos) const {
		uint32_t pos = p_hash & p_capacity;

		if (ptr[pos] == EMPTY_HASH) {
			return false;
		}

		uint8_t stored_hash = _get_stored_hash(p_hash);

		if (ptr[pos] == stored_hash && likely(p_container->_compare_function(pos, p_key))) {
			r_hash_pos = pos;
			return true;
		}

		pos = (pos + 1) & p_capacity;
		uint32_t distance = 1;

		while (true) {
			HashGroup group(&ptr[pos]);
			USize mask = group.get_compare_mask(stored_hash);
			while (mask) {
				uint32_t actual_pos = pos + HashGroup::first_match_index(mask);
				if (likely(p_container->_compare_function(actual_pos, p_key))) {
					r_hash_pos = actual_pos;
					return true;
				}
				mask = HashGroup::clear_first_match(mask);
			}

			if (likely(group.get_compare_mask(EMPTY_HASH))) {
				return false;
			}

			pos += HashGroup::GROUP_SIZE;
			if (pos > p_capacity) {
				distance += p_capacity + HashGroup::GROUP_SIZE + 1 - pos;
				if (unlikely(distance > p_capacity)) {
					return false;
				}
				pos = 0;
			} else {
				distance += HashGroup::GROUP_SIZE;
			}
		}
	}

	uint32_t insert_hash(uint32_t p_hash, uint32_t p_capacity);

	_FORCE_INLINE_ bool is_occupied(uint32_t p_index) const {
		return ptr[p_index] > END_HASH;
	}

	_FORCE_INLINE_ const uint8_t &operator[](uint32_t p_index) const {
		return ptr[p_index];
	}

	_FORCE_INLINE_ void delete_hash(uint32_t p_index) {
		ptr[p_index] = DELETED_HASH;
		num_tombstones++;
	}
};

#endif // HASHES_H
