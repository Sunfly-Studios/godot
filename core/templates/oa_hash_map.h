/**************************************************************************/
/*  oa_hash_map.h                                                         */
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

#ifndef OA_HASH_MAP_H
#define OA_HASH_MAP_H

#include "core/math/math_funcs.h"
#include "core/os/memory.h"
#include "core/templates/hashfuncs.h"
#include "core/templates/hashes.h"
#include "core/templates/pair.h"

/**
 * A HashMap implementation that uses open addressing with Robin Hood hashing.
 * Robin Hood hashing swaps out entries that have a smaller probing distance
 * than the to-be-inserted entry, that evens out the average probing distance
 * and enables faster lookups. Backward shift deletion is employed to further
 * improve the performance and to avoid infinite loops in rare cases.
 *
 * The entries are stored inplace, so huge keys or values might fill cache lines
 * a lot faster.
 *
 * Only used keys and values are constructed. For free positions there's space
 * in the arrays for each, but that memory is kept uninitialized.
 *
 * The assignment operator copy the pairs from one map to the other.
 */
template <typename TKey, typename TValue,
		typename Hasher = HashMapHasherDefault,
		typename Comparator = HashMapComparatorDefault<TKey>>
class OAHashMap {
public:
	// Must be a power of two.
	static constexpr uint32_t MIN_CAPACITY = 16;

	// Used by Hashes during lookup.
	_FORCE_INLINE_ bool _compare_function(uint32_t p_pos, const TKey &p_key) const {
		return Comparator::compare(*std::launder(&keys[p_pos]), p_key);
	}

private:
	TValue *values = nullptr;
	TKey *keys = nullptr;
	Hashes hashes;

	// Due to optimization, this is `capacity - 1`. Use + 1 to get normal capacity.
	uint32_t capacity = 0;
	uint32_t num_elements = 0;

	_FORCE_INLINE_ uint32_t _hash(const TKey &p_key) const {
		return Hasher::hash(p_key);
	}

	// 93.75% max load factor; always leaves at least 1 empty slot for capacity >= 15.
	static _FORCE_INLINE_ uint32_t _get_resize_count(uint32_t p_capacity) {
		return p_capacity - (p_capacity >> 4);
	}

	bool _lookup_pos(const TKey &p_key, uint32_t &r_pos) const {
		if (unlikely(hashes.ptr == nullptr || num_elements == 0)) {
			return false;
		}
		return hashes.lookup_pos_with_hash(this, p_key, _hash(p_key), capacity, r_pos);
	}

	void _insert_with_hash(uint32_t p_hash, const TKey &p_key, const TValue &p_value) {
		uint32_t pos = hashes.insert_hash(p_hash, capacity);
		unaligned_construct<TKey>(&keys[pos], p_key);
		unaligned_construct<TValue>(&values[pos], p_value);
		num_elements++;
	}

	void _insert_with_hash_moved(uint32_t p_hash, TKey &&p_key, TValue &&p_value) {
		uint32_t pos = hashes.insert_hash(p_hash, capacity);
		unaligned_construct<TKey>(&keys[pos], std::move(p_key));
		unaligned_construct<TValue>(&values[pos], std::move(p_value));
		num_elements++;
	}

	void _resize_and_rehash(uint32_t p_new_capacity) {
		uint32_t old_real_capacity = (hashes.ptr != nullptr) ? (capacity + 1) : 0;

		uint32_t real_capacity = next_power_of_2(MAX(MIN_CAPACITY, p_new_capacity));
		capacity = real_capacity - 1;

		TKey *old_keys = keys;
		TValue *old_values = values;
		uint8_t *old_hashes_ptr = hashes.ptr;
		hashes.ptr = nullptr;
		hashes.num_tombstones = 0;

		num_elements = 0;
		keys = static_cast<TKey *>(Memory::alloc_aligned_static(sizeof(TKey) * real_capacity, alignof(TKey)));
		values = static_cast<TValue *>(Memory::alloc_aligned_static(sizeof(TValue) * real_capacity, alignof(TValue)));
		hashes.allocate(real_capacity);

		if (old_hashes_ptr == nullptr) {
			return;
		}

		for (uint32_t i = 0; i < old_real_capacity; i++) {
			if (old_hashes_ptr[i] <= Hashes::END_HASH) {
				continue;
			}

			TKey *old_k = std::launder(&old_keys[i]);
			TValue *old_v = std::launder(&old_values[i]);
			_insert_with_hash_moved(_hash(*old_k), std::move(*old_k), std::move(*old_v));

			unaligned_destroy<TKey>(&old_keys[i]);
			unaligned_destroy<TValue>(&old_values[i]);
		}

		Memory::free_aligned_static(old_keys);
		Memory::free_aligned_static(old_values);
		Memory::free_aligned_static(old_hashes_ptr);
	}

public:
	_FORCE_INLINE_ uint32_t get_capacity() const { return hashes.ptr != nullptr ? capacity + 1 : 0; }
	_FORCE_INLINE_ uint32_t get_num_elements() const { return num_elements; }

	bool is_empty() const {
		return num_elements == 0;
	}

	void clear() {
		if (hashes.ptr == nullptr) {
			return;
		}

		uint32_t real_capacity = capacity + 1;
		if constexpr (!(std::is_trivially_destructible_v<TKey> && std::is_trivially_destructible_v<TValue>)) {
			for (uint32_t i = 0; i < real_capacity; i++) {
				if (hashes.is_occupied(i)) {
					unaligned_destroy<TValue>(&values[i]);
					unaligned_destroy<TKey>(&keys[i]);
				}
			}
		}

		hashes.clear(real_capacity);
		num_elements = 0;
	}

	void insert(const TKey &p_key, const TValue &p_value) {
		if (unlikely(hashes.ptr == nullptr)) {
			_resize_and_rehash(MIN_CAPACITY);
		}

		if (unlikely(num_elements + hashes.num_tombstones >= _get_resize_count(capacity))) {
			if (num_elements * 2 >= _get_resize_count(capacity)) {
				_resize_and_rehash((capacity + 1) * 4);
			} else {
				// Mostly tombstones: rehash in-place without growing.
				_resize_and_rehash(capacity + 1);
			}
		}

		_insert_with_hash(_hash(p_key), p_key, p_value);
	}

	void set(const TKey &p_key, const TValue &p_data) {
		uint32_t pos = 0;
		bool exists = _lookup_pos(p_key, pos);

		if (exists) {
			values[pos] = p_data;
		} else {
			insert(p_key, p_data);
		}
	}

	/**
	 * returns true if the value was found, false otherwise.
	 *
	 * if r_data is not nullptr then the value will be written to the object
	 * it points to.
	 */
	bool lookup(const TKey &p_key, TValue &r_data) const {
		uint32_t pos = 0;
		bool exists = _lookup_pos(p_key, pos);

		if (exists) {
			r_data = *std::launder(&values[pos]);
			return true;
		}

		return false;
	}

	const TValue *lookup_ptr(const TKey &p_key) const {
		uint32_t pos = 0;
		if (_lookup_pos(p_key, pos)) {
			return std::launder(&values[pos]);
		}
		return nullptr;
	}

	TValue *lookup_ptr(const TKey &p_key) {
		uint32_t pos = 0;
		if (_lookup_pos(p_key, pos)) {
			return std::launder(&values[pos]);
		}
		return nullptr;
	}

	_FORCE_INLINE_ bool has(const TKey &p_key) const {
		uint32_t _pos = 0;
		return _lookup_pos(p_key, _pos);
	}

	void remove(const TKey &p_key) {
		uint32_t pos = 0;
		bool exists = _lookup_pos(p_key, pos);

		if (!exists) {
			return;
		}

		hashes.delete_hash(pos);
		unaligned_destroy<TValue>(&values[pos]);
		unaligned_destroy<TKey>(&keys[pos]);

		num_elements--;
	}

	/**
	 * reserves space for a number of elements, useful to avoid many resizes and rehashes
	 *  if adding a known (possibly large) number of elements at once, must be larger than old
	 *  capacity.
	 **/
	void reserve(uint32_t p_new_capacity) {
		ERR_FAIL_COND(p_new_capacity < get_capacity());
		_resize_and_rehash(p_new_capacity);
	}

	struct Iterator {
		bool valid;

		const TKey *key = nullptr;
		TValue *value = nullptr;

	private:
		uint32_t pos = 0;
		friend class OAHashMap;
	};

	Iterator iter() const {
		Iterator it = {};

		it.valid = true;
		it.pos = 0;

		return next_iter(it);
	}

	Iterator next_iter(const Iterator &p_iter) const {
		if (!p_iter.valid || hashes.ptr == nullptr) {
			return p_iter;
		}

		Iterator it = {};
		it.valid = false;
		it.pos = p_iter.pos;
		it.key = nullptr;
		it.value = nullptr;

		uint32_t real_capacity = capacity + 1;
		for (uint32_t i = it.pos; i < real_capacity; i++) {
			it.pos = i + 1;

			if (!hashes.is_occupied(i)) {
				continue;
			}

			it.valid = true;
			it.key = std::launder(&keys[i]);
			it.value = std::launder(&values[i]);
			return it;
		}

		return it;
	}

	OAHashMap(std::initializer_list<KeyValue<TKey, TValue>> p_init) {
		reserve(p_init.size());
		for (const KeyValue<TKey, TValue> &E : p_init) {
			set(E.key, E.value);
		}
	}

	OAHashMap(const OAHashMap &p_other) {
		(*this) = p_other;
	}

	void operator=(const OAHashMap &p_other) {
		if (this == &p_other) {
			return;
		}

		if (hashes.ptr != nullptr) {
			clear();
		}

		uint32_t other_cap = p_other.get_capacity();
		if (other_cap == 0) {
			return;
		}

		_resize_and_rehash(other_cap);

		if constexpr (std::is_trivially_copyable_v<TKey> && std::is_trivially_copyable_v<TValue>) {
			uint32_t real_capacity = capacity + 1;
			memcpy(keys, p_other.keys, sizeof(TKey) * real_capacity);
			memcpy(values, p_other.values, sizeof(TValue) * real_capacity);
			hashes.copy_from(p_other.hashes, real_capacity);
			num_elements = p_other.num_elements;
		} else {
			for (Iterator it = p_other.iter(); it.valid; it = p_other.next_iter(it)) {
				set(*it.key, *it.value);
			}
		}
	}

	OAHashMap(uint32_t p_initial_capacity = 64) {
		_resize_and_rehash(p_initial_capacity);
	}

	~OAHashMap() {
		if (hashes.ptr != nullptr) {
			uint32_t real_capacity = capacity + 1;
			if constexpr (!(std::is_trivially_destructible_v<TKey> && std::is_trivially_destructible_v<TValue>)) {
				for (uint32_t i = 0; i < real_capacity; i++) {
					if (hashes.is_occupied(i)) {
						unaligned_destroy<TValue>(&values[i]);
						unaligned_destroy<TKey>(&keys[i]);
					}
				}
			}

			Memory::free_aligned_static(keys);
			Memory::free_aligned_static(values);
		}
	}
};

#endif // OA_HASH_MAP_H
