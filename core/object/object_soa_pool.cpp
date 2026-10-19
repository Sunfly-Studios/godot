/**************************************************************************/
/*  object_soa_pool.cpp                                                   */
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

#include "core/object/object_soa_pool.h"

void ObjectSOAPoolBase::_grow_sparse_table(uint32_t p_min_capacity) {
	if (p_min_capacity <= sparse_capacity) {
		return;
	}

	ERR_FAIL_COND_MSG(p_min_capacity > GenerationalHandle::MAX_INDEX + 1U, "ObjectSOAPool exceeded maximum generational handle index capacity.");

	uint32_t new_capacity = sparse_capacity == 0 ? 64U : sparse_capacity;
	while (new_capacity < p_min_capacity) {
		new_capacity <<= 1U;
	}
	if (new_capacity > GenerationalHandle::MAX_INDEX + 1U) {
		new_capacity = GenerationalHandle::MAX_INDEX + 1U;
	}

	const size_t old_bytes = sparse_capacity * sizeof(SparseSlot);
	const size_t new_bytes = new_capacity * sizeof(SparseSlot);

	// Align sparse table to byte cache lines t
	// prevent split-line loads during handle lookups.
	sparse_slots = static_cast<SparseSlot *>(
		Memory::realloc_aligned_static(sparse_slots, new_bytes, old_bytes, CACHE_LINE_ALIGNMENT)
	);
	CRASH_COND_MSG(!sparse_slots, "FATAL: Out of memory reallocating ObjectSOAPool sparse table.");

	// Begin object lifetimes for newly allocated slots.
	for (uint32_t i = sparse_capacity; i < new_capacity; ++i) {
		unaligned_construct<SparseSlot>(&sparse_slots[i], SparseSlot{ INVALID_INDEX, 1U, INVALID_INDEX });
	}

	sparse_capacity = new_capacity;
}

GenerationalHandle ObjectSOAPoolBase::_allocate_handle(uint32_t p_dense_index) {
	uint32_t sparse_idx = INVALID_INDEX;
	uint32_t generation = 1U;

	// Pop a recycled slot from the intrusive free-list if available.
	if (free_list_head != INVALID_INDEX) {
		sparse_idx = free_list_head;
		SparseSlot *slot = std::launder(&sparse_slots[sparse_idx]);
		free_list_head = slot->next_free;
		slot->next_free = INVALID_INDEX;
		generation = slot->generation;
	} else {
		ERR_FAIL_COND_V_MSG(sparse_count > GenerationalHandle::MAX_INDEX, GenerationalHandle(), "ObjectSOAPool exhausted all sparse indices.");
		if (unlikely(sparse_count >= sparse_capacity)) {
			_grow_sparse_table(sparse_count + 1U);
		}
		sparse_idx = sparse_count++;
		SparseSlot *slot = std::launder(&sparse_slots[sparse_idx]);
		generation = slot->generation;
	}

	const GenerationalHandle handle(sparse_idx, generation);
	SparseSlot *slot = std::launder(&sparse_slots[sparse_idx]);
	slot->dense_index = p_dense_index;

	dense_to_handle.push_back(handle);
	return handle;
}

bool ObjectSOAPoolBase::_free_handle(GenerationalHandle p_handle, uint32_t &r_target_dense, uint32_t &r_last_dense) {
	if (unlikely(!is_valid(p_handle))) {
		return false;
	}

	const uint32_t sparse_idx = p_handle.get_index();
	SparseSlot *target_slot = std::launder(&sparse_slots[sparse_idx]);
	r_target_dense = target_slot->dense_index;
	r_last_dense = dense_to_handle.size() - 1U;

	// Swap-and-pop the reverse lookup table and redirect
	// the moved element's sparse slot.
	if (r_target_dense != r_last_dense) {
		const GenerationalHandle moved_handle = *std::launder(&dense_to_handle[r_last_dense]);
		*std::launder(&dense_to_handle[r_target_dense]) = moved_handle;

		SparseSlot *moved_slot = std::launder(&sparse_slots[moved_handle.get_index()]);
		moved_slot->dense_index = r_target_dense;
	}

	dense_to_handle.resize(r_last_dense);

	// Invalidate the freed slot, increment generation (skipping 0),
	// and push to the free-list.
	target_slot->dense_index = INVALID_INDEX;
	uint32_t next_gen = (target_slot->generation + 1U) & GenerationalHandle::MAX_GENERATION;
	if (unlikely(next_gen == 0U)) {
		next_gen = 1U;
	}
	target_slot->generation = next_gen;
	target_slot->next_free = free_list_head;
	free_list_head = sparse_idx;

	return true;
}

void ObjectSOAPoolBase::_reset_handles() {
	dense_to_handle.clear();
	free_list_head = INVALID_INDEX;

	// Bump generations on all previously used slots
	// so stale handles cannot alias new allocations.
	for (uint32_t i = 0; i < sparse_count; ++i) {
		SparseSlot *slot = std::launder(&sparse_slots[i]);
		slot->dense_index = INVALID_INDEX;
		uint32_t next_gen = (slot->generation + 1U) & GenerationalHandle::MAX_GENERATION;
		if (unlikely(next_gen == 0U)) {
			next_gen = 1U;
		}
		slot->generation = next_gen;
		slot->next_free = INVALID_INDEX;
	}
	sparse_count = 0;
}

ObjectSOAPoolBase::~ObjectSOAPoolBase() {
	if (sparse_slots != nullptr) {
		for (uint32_t i = 0; i < sparse_capacity; ++i) {
			unaligned_destroy<SparseSlot>(&sparse_slots[i]);
		}
		Memory::free_aligned_static(sparse_slots);
		sparse_slots = nullptr;
	}
}

ObjectSOAPoolBase::ObjectSOAPoolBase(ObjectSOAPoolBase &&p_other) noexcept :
		sparse_slots(p_other.sparse_slots),
		sparse_capacity(p_other.sparse_capacity),
		sparse_count(p_other.sparse_count),
		free_list_head(p_other.free_list_head),
		dense_to_handle(std::move(p_other.dense_to_handle)) {
	p_other.sparse_slots = nullptr;
	p_other.sparse_capacity = 0;
	p_other.sparse_count = 0;
	p_other.free_list_head = INVALID_INDEX;
}

ObjectSOAPoolBase &ObjectSOAPoolBase::operator=(ObjectSOAPoolBase &&p_other) noexcept {
	if (this == &p_other) {
		return *this;
	}

	if (sparse_slots != nullptr) {
		for (uint32_t i = 0; i < sparse_capacity; ++i) {
			unaligned_destroy<SparseSlot>(&sparse_slots[i]);
		}
		Memory::free_aligned_static(sparse_slots);
	}

	sparse_slots = p_other.sparse_slots;
	sparse_capacity = p_other.sparse_capacity;
	sparse_count = p_other.sparse_count;
	free_list_head = p_other.free_list_head;
	dense_to_handle = std::move(p_other.dense_to_handle);

	p_other.sparse_slots = nullptr;
	p_other.sparse_capacity = 0;
	p_other.sparse_count = 0;
	p_other.free_list_head = INVALID_INDEX;
	return *this;
}