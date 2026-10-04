/**************************************************************************/
/*  slab_allocator.h                                                      */
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

#ifndef SLAB_ALLOCATOR_H
#define SLAB_ALLOCATOR_H

#include "core/core_globals.h"
#include "core/os/memory.h"
#include "core/os/spin_lock.h"
#include "core/string/ustring.h"
#include "core/typedefs.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <typeinfo>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// Copied from `cowdata.h`
#if defined(IS_32_BIT)
typedef int32_t Size;
typedef uint32_t USize;
static constexpr USize MAX_INT = INT32_MAX;
static constexpr USize BITMAP_FULL = 0xFFFFFFFFU;
static constexpr size_t OBJECTS_PER_SLAB = 32;
#else
typedef int64_t Size;
typedef uint64_t USize;
static constexpr USize MAX_INT = INT64_MAX;
static constexpr USize BITMAP_FULL = 0xFFFFFFFFFFFFFFFFULL;
static constexpr size_t OBJECTS_PER_SLAB = 64;
#endif

// Branchless De Brujin sequences
#if defined(IS_32_BIT)
_ALWAYS_INLINE_ static uint8_t find_first_trailing_set_bit(USize p_word) {
	static const uint8_t debruijn32[32] = {
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};
	USize isolated_lowest_bit = p_word & (~p_word + 1U);
	return debruijn32[(isolated_lowest_bit * 0x077CB531U) >> 27];
}
#else
_ALWAYS_INLINE_ static uint8_t find_first_trailing_set_bit(USize p_word) {
	static const uint8_t debruijn64[64] = {
		0, 1, 2, 53, 3, 7, 54, 27, 4, 38, 41, 8, 34, 55, 48, 28,
		62, 5, 39, 46, 44, 42, 22, 9, 24, 35, 59, 56, 49, 18, 29, 11,
		63, 52, 6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
		51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12,
	};
	USize isolated_lowest_bit = p_word & (~p_word + 1ULL);
	return debruijn64[(isolated_lowest_bit * 0x022FDD63CC95386DULL) >> 58];
}
#endif

static constexpr size_t _slab_next_power_of_2(size_t v) {
	size_t result = 1;
	while (result < v) {
		result <<= 1;
	}
	return result;
}

template <typename T>
class ThreadSafeSlabAllocator {
	static_assert(sizeof(T) <= 512, "Size of class too big for ThreadSafeSlabAllocator, use PagedAllocator");

	static constexpr uint8_t REUSE_LOW_WATERMARK = sizeof(T) > 128 ? (OBJECTS_PER_SLAB / 2) : (OBJECTS_PER_SLAB / 4);

	struct Slab {
		alignas(T) uint8_t objects[OBJECTS_PER_SLAB * sizeof(T)];

		std::atomic<USize> bitmap{ BITMAP_FULL };
		std::atomic<Slab *> next{ nullptr };
		std::atomic<Slab *> next_available{ nullptr };
		std::atomic<uint8_t> free_count{ OBJECTS_PER_SLAB };
		std::atomic<bool> in_usable_list{ false }; // Prevents duplicate Treiber stack pushes

		template <typename... Args>
		T *allocate(Args &&...p_args) {
			USize current_bitmap = bitmap.load(std::memory_order_acquire);

			// Under lock, nobody else is writing.
			while (current_bitmap != 0) {
				uint8_t index = find_first_trailing_set_bit(current_bitmap);
				USize new_bitmap = current_bitmap & ~(static_cast<USize>(1) << index);

				// CAS loop
				if (likely(bitmap.compare_exchange_weak(current_bitmap, new_bitmap, std::memory_order_acquire, std::memory_order_relaxed))) {
					free_count.fetch_sub(1, std::memory_order_relaxed);

					T *ptr = reinterpret_cast<T *>(&objects[index * sizeof(T)]);
					unaligned_construct<T>(ptr, std::forward<Args>(p_args)...);
					return std::launder(ptr);
				}
			}
			return nullptr;
		}

		void deallocate(T *p_ptr, uint8_t index) {
			unaligned_destroy<T>(p_ptr);
			bitmap.fetch_or(static_cast<USize>(1) << index, std::memory_order_release);

			uint8_t prev_count = free_count.fetch_add(1, std::memory_order_relaxed);

			// Re-evaluating usable pool condition against watermark
			if (unlikely(prev_count + 1 <= REUSE_LOW_WATERMARK)) {
				bool expected = false;
				if (in_usable_list.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
					// We won't be reconsidered for adding to the usable pool.
					// And we can't yet be taken out of the free pool because the usable pool's head has.
					// not yet been changed.
					Slab *usable_head = global_usable_slabs.load(std::memory_order_relaxed);
					do {
						next_available.store(usable_head, std::memory_order_relaxed);
					} while (!global_usable_slabs.compare_exchange_weak(usable_head, this, std::memory_order_release, std::memory_order_relaxed));
				}
			}
		}
	};

	static constexpr size_t SLAB_ALIGNMENT = _slab_next_power_of_2(sizeof(Slab));
	static constexpr uintptr_t SLAB_MASK = ~(SLAB_ALIGNMENT - 1);

	inline static thread_local Slab *local_slab = nullptr;
	inline static std::atomic<Slab *> global_slabs{ nullptr };
	inline static std::atomic<Slab *> global_usable_slabs{ nullptr };

	Slab *allocate_slab() {
		// Not under lock, guessing.
		Slab *usable = global_usable_slabs.load(std::memory_order_acquire);
		while (usable) {
			Slab *next_usable = usable->next_available.load(std::memory_order_relaxed);
			if (global_usable_slabs.compare_exchange_weak(usable, next_usable, std::memory_order_acquire, std::memory_order_relaxed)) {
				usable->in_usable_list.store(false, std::memory_order_release);
				return usable;
			}
		}

		void *raw = Memory::alloc_aligned_static(sizeof(Slab), SLAB_ALIGNMENT);
		Slab *slab = ::new (raw) Slab();

		Slab *global_head = global_slabs.load(std::memory_order_relaxed);
		do {
			slab->next.store(global_head, std::memory_order_relaxed);
		} while (!global_slabs.compare_exchange_weak(global_head, slab, std::memory_order_release, std::memory_order_relaxed));

		return slab;
	}

	static bool _check_used() {
		Slab *current = global_slabs.load(std::memory_order_acquire);
		while (current) {
			Slab *next = current->next.load(std::memory_order_relaxed);
			if (current->bitmap.load(std::memory_order_acquire) != BITMAP_FULL) {
				return true;
			}
			current = next;
		}
		return false;
	}

public:
	template <typename... Args>
	T *alloc(Args &&...p_args) {
		if (unlikely(!local_slab)) {
			local_slab = allocate_slab();
		}

		while (true) {
			T *result = local_slab->allocate(std::forward<Args>(p_args)...);
			if (likely(result)) {
				return result;
			}

			local_slab = allocate_slab();
		}
	}

	template <typename... Args>
	T *new_allocation(Args &&...p_args) { return alloc(std::forward<Args>(p_args)...); }
	void delete_allocation(T *p_mem) { free(p_mem); }

	void free(T *p_mem) {
		uintptr_t mem_addr = reinterpret_cast<uintptr_t>(p_mem);
		Slab *slab = reinterpret_cast<Slab *>(mem_addr & SLAB_MASK);
		uint8_t index = (mem_addr - reinterpret_cast<uintptr_t>(slab->objects)) / sizeof(T);
		
		slab->deallocate(p_mem, index);
	}

	static void cleanup() {
		bool leaked = _check_used();
		if (leaked) {
			if (CoreGlobals::leak_reporting_enabled) {
				ERR_PRINT(String("Slabs in use at exit in ThreadSafeSlabAllocator: ") + String(typeid(T).name()));
			}
		} else {
			Slab *current = global_slabs.load(std::memory_order_acquire);
			while (current) {
				Slab *next = current->next.load(std::memory_order_relaxed);
				current->~Slab();
				Memory::free_aligned_static(current);
				current = next;
			}
			global_slabs.store(nullptr, std::memory_order_release);
			global_usable_slabs.store(nullptr, std::memory_order_release);
		}
		local_slab = nullptr;
	}
};

template <typename T>
class SlabAllocator {
	static_assert(sizeof(T) <= 512, "Size of class too big for SlabAllocator, use PagedAllocator");

	static constexpr uint8_t REUSE_LOW_WATERMARK = sizeof(T) > 128 ? (OBJECTS_PER_SLAB / 2) : (OBJECTS_PER_SLAB / 4);

	struct Slab {
		enum slabstate {
			IN_USE,
			UNUSED,
			IN_USABLE,
		};

		alignas(T) uint8_t objects[OBJECTS_PER_SLAB * sizeof(T)];
		uint64_t bitmap = BITMAP_FULL;
		Slab *next = nullptr;
		Slab *next_available = nullptr;
		slabstate state = IN_USE;
		uint8_t free_count = OBJECTS_PER_SLAB;

		template <typename... Args>
		T *allocate(Args &&...p_args) {
			// Bitmap already checked in SlabAllocator::alloc().
			uint8_t index = find_first_trailing_set_bit(bitmap);
			bitmap &= ~(static_cast<USize>(1) << index);
			free_count--;

			T *ptr = reinterpret_cast<T *>(&objects[index * sizeof(T)]);
			unaligned_construct<T>(ptr, std::forward<Args>(p_args)...);
			return std::launder(ptr);
		}

		void deallocate(T *p_ptr, uint8_t index) {
			unaligned_destroy<T>(p_ptr);
			bitmap |= (static_cast<USize>(1) << index);
			free_count++;
		}
	};

	static constexpr size_t SLAB_ALIGNMENT = _slab_next_power_of_2(sizeof(Slab));
	static constexpr uintptr_t SLAB_MASK = ~(SLAB_ALIGNMENT - 1);

	Slab *current_slab = nullptr;
	Slab *slabs = nullptr;
	Slab *usable_slabs = nullptr;

	Slab *allocate_slab() {
		if (usable_slabs) {
			Slab *new_slab = usable_slabs;
			Slab *next_global_usable = usable_slabs->next_available;
			usable_slabs = next_global_usable;

			new_slab->state = Slab::IN_USE;
			return new_slab;
		}

		void *raw = Memory::alloc_aligned_static(sizeof(Slab), SLAB_ALIGNMENT);
		Slab *slab = ::new (raw) Slab();

		slab->next = slabs;
		slabs = slab;

		return slab;
	}

	bool _check_used() {
		Slab *current = slabs;
		while (current) {
			Slab *next = current->next;
			if (current->bitmap != UINT64_MAX) {
				return true;
			}
			current = next;
		}
		return false;
	}

public:
	template <typename... Args>
	T *alloc(Args &&...p_args) {
		if (!current_slab->bitmap) {
			current_slab->state = Slab::UNUSED;
			current_slab = allocate_slab();
		}
		return current_slab->allocate(std::forward<Args>(p_args)...);
	}

	template <typename... Args>
	T *new_allocation(Args &&...p_args) { return alloc(std::forward<Args>(p_args)...); }
	void delete_allocation(T *p_mem) { free(p_mem); }

	void free(T *p_mem) {
		uintptr_t mem_addr = reinterpret_cast<uintptr_t>(p_mem);
		Slab *slab = reinterpret_cast<Slab *>(mem_addr & SLAB_MASK);
		uint8_t index = (mem_addr - reinterpret_cast<uintptr_t>(slab->objects)) / sizeof(T);

		slab->deallocate(p_mem, index);

		if (slab->state == Slab::UNUSED && slab->free_count <= REUSE_LOW_WATERMARK) {
			slab->state = Slab::IN_USABLE;
			Slab *usable = usable_slabs;
			slab->next_available = usable;
			usable_slabs = slab;
		}
	}

	SlabAllocator() {
		current_slab = allocate_slab();
	}

	~SlabAllocator() {
		if (_check_used()) {
			if (CoreGlobals::leak_reporting_enabled) {
				ERR_PRINT(String("Slabs in use at exit in SlabAllocator: ") + String(typeid(T).name()));
			}
		}

		Slab *current = slabs;
		while (current) {
			Slab *next = current->next;
			current->~Slab();
			Memory::free_aligned_static(current);
			current = next;
		}
	}
};

#endif // SLAB_ALLOCATOR_H
