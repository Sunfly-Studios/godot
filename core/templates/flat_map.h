/**************************************************************************/
/*  flat_map.h                                                            */
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

#ifndef FLAT_MAP_H
#define FLAT_MAP_H

#include "core/error/error_macros.h"
#include "core/os/memory.h"
#include "core/templates/local_vector.h"
#include "core/templates/sort_array.h"
#include "core/typedefs.h"

#include <new>
#include <type_traits>
#include <utility>

template <typename K, typename V, typename Comparator = _DefaultComparator<K>>
class FlatMap {
private:
	LocalVector<K> keys;
	LocalVector<V> values;

	_FORCE_INLINE_ bool _lower_bound(const K &p_key, uint32_t &r_index) const {
		Comparator comp;
		uint32_t low = 0;
		uint32_t high = keys.size();

		while (low < high) {
			const uint32_t mid = low + ((high - low) >> 1U);
			const K &mid_key = *std::launder(&keys[mid]);
			if (comp(mid_key, p_key)) {
				low = mid + 1U;
			} else {
				high = mid;
			}
		}

		r_index = low;
		if (low < keys.size()) {
			const K &found_key = *std::launder(&keys[low]);
			return !comp(p_key, found_key);
		}
		return false;
	}

public:
	struct PairRef {
		const K &key;
		V &value;
	};

	struct ConstPairRef {
		const K &key;
		const V &value;
	};

	FlatMap() = default;
	~FlatMap() = default;

	FlatMap(const FlatMap &p_other) = default;
	FlatMap &operator=(const FlatMap &p_other) = default;

	FlatMap(FlatMap &&p_other) noexcept = default;
	FlatMap &operator=(FlatMap &&p_other) noexcept = default;

	template <typename UK = K, typename UV = V>
	uint32_t insert(UK &&p_key, UV &&p_value) {
		uint32_t idx = 0;
		if (_lower_bound(p_key, idx)) {
			unaligned_write<V>(&values[idx], std::forward<UV>(p_value));
			return idx;
		}

		const uint32_t old_size = keys.size();
		keys.resize(old_size + 1U);
		values.resize(old_size + 1U);

		for (uint32_t i = old_size; i > idx; --i) {
			*std::launder(&keys[i]) = std::move(*std::launder(&keys[i - 1U]));
			*std::launder(&values[i]) = std::move(*std::launder(&values[i - 1U]));
		}

		*std::launder(&keys[idx]) = std::forward<UK>(p_key);
		*std::launder(&values[idx]) = std::forward<UV>(p_value);
		return idx;
	}

	bool erase(const K &p_key) {
		uint32_t idx = 0;
		if (!_lower_bound(p_key, idx)) {
			return false;
		}

		const uint32_t last_idx = keys.size() - 1U;
		for (uint32_t i = idx; i < last_idx; ++i) {
			*std::launder(&keys[i]) = std::move(*std::launder(&keys[i + 1U]));
			*std::launder(&values[i]) = std::move(*std::launder(&values[i + 1U]));
		}

		keys.resize(last_idx);
		values.resize(last_idx);
		return true;
	}

	_FORCE_INLINE_ V *find(const K &p_key) {
		uint32_t idx = 0;
		if (_lower_bound(p_key, idx)) {
			return std::launder(&values[idx]);
		}
		return nullptr;
	}

	_FORCE_INLINE_ const V *find(const K &p_key) const {
		uint32_t idx = 0;
		if (_lower_bound(p_key, idx)) {
			return std::launder(&values[idx]);
		}
		return nullptr;
	}

	_FORCE_INLINE_ bool has(const K &p_key) const {
		uint32_t idx = 0;
		return _lower_bound(p_key, idx);
	}

	_FORCE_INLINE_ V &operator[](const K &p_key) {
		uint32_t idx = 0;
		if (_lower_bound(p_key, idx)) {
			return *std::launder(&values[idx]);
		}
		const uint32_t inserted_idx = insert(p_key, V{});
		return *std::launder(&values[inserted_idx]);
	}

	_FORCE_INLINE_ const LocalVector<K> &get_keys() const {
		return keys;
	}

	_FORCE_INLINE_ LocalVector<V> &get_values() {
		return values;
	}

	_FORCE_INLINE_ const LocalVector<V> &get_values() const {
		return values;
	}

	_FORCE_INLINE_ void reserve(uint32_t p_capacity) {
		keys.reserve(p_capacity);
		values.reserve(p_capacity);
	}

	_FORCE_INLINE_ void clear() {
		keys.clear();
		values.clear();
	}

	_FORCE_INLINE_ uint32_t size() const {
		return keys.size();
	}

	_FORCE_INLINE_ bool is_empty() const {
		return keys.is_empty();
	}
};

#endif // FLAT_MAP_H