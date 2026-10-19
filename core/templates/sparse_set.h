/**************************************************************************/
/*  sparse_set.h                                                          */
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

#ifndef SPARSE_SET_H
#define SPARSE_SET_H

#include "core/error/error_macros.h"
#include "core/os/memory.h"
#include "core/templates/generational_handle.h"
#include "core/templates/local_vector.h"
#include "core/typedefs.h"

#include <new>
#include <type_traits>
#include <utility>

template <typename T>
class SparseSet {
public:
	static constexpr uint32_t INVALID_DENSE_INDEX = 0xFFFFFFFFU;
	static constexpr size_t SPARSE_PAGE_SHIFT = 10;
	static constexpr size_t SPARSE_PAGE_SIZE = 1ULL << SPARSE_PAGE_SHIFT;
	static constexpr size_t SPARSE_PAGE_MASK = SPARSE_PAGE_SIZE - 1ULL;
	static constexpr size_t ALIGNMENT = alignof(T) > 64 ? alignof(T) : 64;

private:
	struct SparseSlot {
		uint32_t dense_index;
		uint32_t generation;
	};

	SparseSlot **sparse_pages = nullptr;
	uint32_t page_capacity = 0;

	LocalVector<T> dense_array;
	LocalVector<GenerationalHandle> reverse_map;

	_FORCE_INLINE_ void _ensure_sparse_page(uint32_t p_index) {
		const uint32_t page_idx = p_index >> SPARSE_PAGE_SHIFT;
		if (unlikely(page_idx >= page_capacity)) {
			uint32_t new_cap = page_capacity == 0 ? 16U : page_capacity;
			while (new_cap <= page_idx) {
				new_cap <<= 1U;
			}
			const size_t old_bytes = page_capacity * sizeof(SparseSlot *);
			const size_t new_bytes = new_cap * sizeof(SparseSlot *);
			sparse_pages = static_cast<SparseSlot **>(
				Memory::realloc_aligned_static(sparse_pages, new_bytes, old_bytes, alignof(SparseSlot *))
            );
			CRASH_COND_MSG(!sparse_pages, "FATAL: Out of memory allocating SparseSet page directory.");

			for (uint32_t i = page_capacity; i < new_cap; ++i) {
				unaligned_construct<SparseSlot *>(&sparse_pages[i], nullptr);
			}
			page_capacity = new_cap;
		}

		SparseSlot *page = unaligned_read<SparseSlot *>(&sparse_pages[page_idx]);
		if (unlikely(page == nullptr)) {
			const size_t page_bytes = SPARSE_PAGE_SIZE * sizeof(SparseSlot);
			page = static_cast<SparseSlot *>(Memory::alloc_aligned_static(page_bytes, alignof(SparseSlot)));
			CRASH_COND_MSG(!page, "FATAL: Out of memory allocating SparseSet page.");

			for (size_t i = 0; i < SPARSE_PAGE_SIZE; ++i) {
				unaligned_construct<SparseSlot>(&page[i], SparseSlot{ INVALID_DENSE_INDEX, 0U });
			}
			unaligned_write<SparseSlot *>(&sparse_pages[page_idx], page);
		}
	}

	_FORCE_INLINE_ SparseSlot *_get_sparse_slot_mut(uint32_t p_index) {
		const uint32_t page_idx = p_index >> SPARSE_PAGE_SHIFT;
		const uint32_t offset = p_index & SPARSE_PAGE_MASK;
		SparseSlot *page = unaligned_read<SparseSlot *>(&sparse_pages[page_idx]);
		return std::launder(&page[offset]);
	}

	_FORCE_INLINE_ const SparseSlot *_get_sparse_slot(uint32_t p_index) const {
		const uint32_t page_idx = p_index >> SPARSE_PAGE_SHIFT;
		if (unlikely(page_idx >= page_capacity || sparse_pages == nullptr)) {
			return nullptr;
		}
		const SparseSlot *page = unaligned_read<SparseSlot *>(&sparse_pages[page_idx]);
		if (unlikely(page == nullptr)) {
			return nullptr;
		}
		const uint32_t offset = p_index & SPARSE_PAGE_MASK;
		return std::launder(&page[offset]);
	}

	void _free_sparse_pages() {
		if (sparse_pages != nullptr) {
			for (uint32_t i = 0; i < page_capacity; ++i) {
				SparseSlot *page = unaligned_read<SparseSlot *>(&sparse_pages[i]);
				if (page != nullptr) {
					for (size_t j = 0; j < SPARSE_PAGE_SIZE; ++j) {
						unaligned_destroy<SparseSlot>(&page[j]);
					}
					Memory::free_aligned_static(page);
				}
				unaligned_destroy<SparseSlot *>(&sparse_pages[i]);
			}
			Memory::free_aligned_static(sparse_pages);
			sparse_pages = nullptr;
			page_capacity = 0;
		}
	}

public:
	SparseSet() = default;

	~SparseSet() {
		_free_sparse_pages();
	}

	SparseSet(const SparseSet &p_other) {
		*this = p_other;
	}

	SparseSet &operator=(const SparseSet &p_other) {
		if (this == &p_other) {
			return *this;
		}
		clear();
		_free_sparse_pages();

		dense_array = p_other.dense_array;
		reverse_map = p_other.reverse_map;

		for (uint32_t dense_idx = 0; dense_idx < reverse_map.size(); ++dense_idx) {
			const GenerationalHandle h = reverse_map[dense_idx];
			const uint32_t sparse_idx = h.get_index();
			_ensure_sparse_page(sparse_idx);
			SparseSlot *slot = _get_sparse_slot_mut(sparse_idx);
			slot->dense_index = dense_idx;
			slot->generation = h.get_generation();
		}
		return *this;
	}

	SparseSet(SparseSet &&p_other) noexcept :
			sparse_pages(p_other.sparse_pages),
			page_capacity(p_other.page_capacity),
			dense_array(std::move(p_other.dense_array)),
			reverse_map(std::move(p_other.reverse_map)) {
		p_other.sparse_pages = nullptr;
		p_other.page_capacity = 0;
	}

	SparseSet &operator=(SparseSet &&p_other) noexcept {
		if (this == &p_other) {
			return *this;
		}
		_free_sparse_pages();
		sparse_pages = p_other.sparse_pages;
		page_capacity = p_other.page_capacity;
		dense_array = std::move(p_other.dense_array);
		reverse_map = std::move(p_other.reverse_map);

		p_other.sparse_pages = nullptr;
		p_other.page_capacity = 0;
		return *this;
	}

	_FORCE_INLINE_ bool has(GenerationalHandle p_handle) const {
		if (unlikely(!p_handle.is_valid())) {
			return false;
		}
		const SparseSlot *slot = _get_sparse_slot(p_handle.get_index());
		if (unlikely(slot == nullptr)) {
			return false;
		}
		return slot->dense_index < dense_array.size() && slot->generation == p_handle.get_generation();
	}

	template <typename U = T>
	void insert(GenerationalHandle p_handle, U &&p_value) {
		ERR_FAIL_COND(!p_handle.is_valid());
		const uint32_t sparse_idx = p_handle.get_index();
		const uint32_t gen = p_handle.get_generation();

		_ensure_sparse_page(sparse_idx);
		SparseSlot *slot = _get_sparse_slot_mut(sparse_idx);

		if (slot->dense_index < dense_array.size() && slot->generation == gen) {
			unaligned_write<T>(&dense_array[slot->dense_index], std::forward<U>(p_value));
			return;
		}

		const uint32_t new_dense_idx = dense_array.size();
		dense_array.push_back(std::forward<U>(p_value));
		reverse_map.push_back(p_handle);

		slot->dense_index = new_dense_idx;
		slot->generation = gen;
	}

	bool erase(GenerationalHandle p_handle) {
		if (unlikely(!p_handle.is_valid())) {
			return false;
		}
		const uint32_t sparse_idx = p_handle.get_index();
		const SparseSlot *const_slot = _get_sparse_slot(sparse_idx);
		if (unlikely(const_slot == nullptr)) {
			return false;
		}

		const uint32_t target_dense_idx = const_slot->dense_index;
		const uint32_t current_size = dense_array.size();
		if (unlikely(target_dense_idx >= current_size || const_slot->generation != p_handle.get_generation())) {
			return false;
		}

		const uint32_t last_dense_idx = current_size - 1U;
		if (target_dense_idx != last_dense_idx) {
			const GenerationalHandle moved_handle = reverse_map[last_dense_idx];
			dense_array[target_dense_idx] = std::move(dense_array[last_dense_idx]);
			reverse_map[target_dense_idx] = moved_handle;

			SparseSlot *moved_slot = _get_sparse_slot_mut(moved_handle.get_index());
			moved_slot->dense_index = target_dense_idx;
		}

		dense_array.resize(last_dense_idx);
		reverse_map.resize(last_dense_idx);

		SparseSlot *target_slot = _get_sparse_slot_mut(sparse_idx);
		target_slot->dense_index = INVALID_DENSE_INDEX;
		target_slot->generation = 0U;
		return true;
	}

	_FORCE_INLINE_ T &get(GenerationalHandle p_handle) {
		DEV_ASSERT(has(p_handle));
		const SparseSlot *slot = _get_sparse_slot(p_handle.get_index());
		return *std::launder(&dense_array[slot->dense_index]);
	}

	_FORCE_INLINE_ const T &get(GenerationalHandle p_handle) const {
		DEV_ASSERT(has(p_handle));
		const SparseSlot *slot = _get_sparse_slot(p_handle.get_index());
		return *std::launder(&dense_array[slot->dense_index]);
	}

	_FORCE_INLINE_ T *get_ptr(GenerationalHandle p_handle) {
		if (unlikely(!has(p_handle))) {
			return nullptr;
		}
		const SparseSlot *slot = _get_sparse_slot(p_handle.get_index());
		return std::launder(&dense_array[slot->dense_index]);
	}

	_FORCE_INLINE_ const T *get_ptr(GenerationalHandle p_handle) const {
		if (unlikely(!has(p_handle))) {
			return nullptr;
		}
		const SparseSlot *slot = _get_sparse_slot(p_handle.get_index());
		return std::launder(&dense_array[slot->dense_index]);
	}

	_FORCE_INLINE_ LocalVector<T> &get_dense_array() {
		return dense_array;
	}

	_FORCE_INLINE_ const LocalVector<T> &get_dense_array() const {
		return dense_array;
	}

	_FORCE_INLINE_ const LocalVector<GenerationalHandle> &get_reverse_map() const {
		return reverse_map;
	}

	_FORCE_INLINE_ uint32_t size() const {
		return dense_array.size();
	}

	_FORCE_INLINE_ bool is_empty() const {
		return dense_array.is_empty();
	}

	void reserve(uint32_t p_capacity) {
		dense_array.reserve(p_capacity);
		reverse_map.reserve(p_capacity);
	}

	void clear() {
		for (uint32_t i = 0; i < reverse_map.size(); ++i) {
			const uint32_t sparse_idx = reverse_map[i].get_index();
			SparseSlot *slot = _get_sparse_slot_mut(sparse_idx);
			slot->dense_index = INVALID_DENSE_INDEX;
			slot->generation = 0U;
		}
		dense_array.clear();
		reverse_map.clear();
	}
};

#endif // SPARSE_SET_H