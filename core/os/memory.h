/**************************************************************************/
/*  memory.h                                                              */
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

#ifndef MEMORY_H
#define MEMORY_H

#include "core/error/error_macros.h"
#include "core/templates/safe_refcount.h"

#include <stddef.h>
#include <new>
#include <type_traits>
#include <cstdint> // for uintptr_t

#ifdef _WIN32
    #include <malloc.h>
#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
    #include <stdlib.h>
#else
    #include <alloca.h>
#endif

// Define a safe minimum alignment.
// "16" is the goldilocks for SSE/NEON/128-bit SIMD,
// as well as having a mathematical guarantee
// to be divisble by 8 (or 4, or 2).
#define GODOT_MIN_STACK_ALIGN 16

// Calculate the required alignment:
// The larger of the type's requirement or GODOT_MIN_STACK_ALIGN.
#define SAFE_ALIGN_SIZE(m_type) \
    ((alignof(m_type) > GODOT_MIN_STACK_ALIGN) ? alignof(m_type) : GODOT_MIN_STACK_ALIGN)

// Unify all safe memory allocation macros
// here for convenience.

#ifdef _WIN32
// Windows wants `_alloca`
#define SAFE_ALLOCA(m_size, m_align) \
	((void *)((((uintptr_t)_alloca((m_size) + (m_align))) + ((m_align) - 1)) & ~((uintptr_t)((m_align) - 1))))
#else
#define SAFE_ALLOCA(m_size, m_align) \
	((void *)((((uintptr_t)alloca((m_size) + (m_align))) + ((m_align) - 1)) & ~((uintptr_t)((m_align) - 1))))
#endif

// Safe Stack Allocation Macro. This macro:
// - Allocates requested size + alignment padding.
// - Shifts the pointer to match the type's alignment requirement (alignof).
// - Always guarantees GODOT_MIN_STACK_ALIGN.
// - Safely handles zero-count allocations.
// 
// Should futher prevent crashes on strict RISC architectures
// and improve SIMD safety on x86.
#define SAFE_ALLOCA_ARRAY(m_type, m_count) \
	((m_count) > 0) ? (m_type *)SAFE_ALLOCA(sizeof(m_type) * (m_count), SAFE_ALIGN_SIZE(m_type)) : (m_type *)nullptr

// Single-element version.
#define SAFE_ALLOCA_SINGLE(m_type) SAFE_ALLOCA_ARRAY(m_type, 1)

// Helper defined outside the class to ensure it is visible for constexpr usage
// inside the class static member initialization.
static inline constexpr size_t _memory_get_aligned_address(size_t p_address, size_t p_alignment) {
	const size_t n_bytes_unaligned = p_address % p_alignment;
	return (n_bytes_unaligned == 0) ? p_address : (p_address + p_alignment - n_bytes_unaligned);
}

class Memory {
public:
	// Forwarder to the helper for external usage (e.g. cowdata.h)
	static constexpr size_t get_aligned_address(size_t p_address, size_t p_alignment) {
		return _memory_get_aligned_address(p_address, p_alignment);
	}

private:
#ifdef DEBUG_ENABLED
	static SafeNumeric<uint64_t> mem_usage;
	static SafeNumeric<uint64_t> max_usage;
#endif
	static SafeNumeric<uint64_t> alloc_count;

public:
#if defined(__MINGW32__) && !defined(__MINGW64__)
	// Note: Using hardcoded value, since the value can end up different in different compile units on 32-bit windows
	// due to a compiler bug (see GH-113145)
	static constexpr size_t MAX_ALIGN = 16;
	static_assert(MAX_ALIGN % alignof(max_align_t) == 0);
#elif defined(__sparc__) || defined(__sparc64__) || defined(__ppc__) || defined(__powerpc__) || defined(__hppa__)
	// These architectures require strict alignment.
	// Force 16-byte alignment to ensure CowData headers (uint64_t size)
	// are strictly aligned to 8-byte boundaries and prevent SIGBUS.
	static constexpr size_t MAX_ALIGN = 16;
	static_assert(MAX_ALIGN % alignof(max_align_t) == 0);
#else
	static constexpr size_t MAX_ALIGN = alignof(max_align_t);
#endif

	// Alignment:  ↓ max_align_t        ↓ uint64_t          ↓ MAX_ALIGN
	//             ┌─────────────────┬──┬────────────────┬──┬───────────...
	//             │ uint64_t        │░░│ uint64_t       │░░│ T[]
	//             │ alloc size      │░░│ element count  │░░│ data
	//             └─────────────────┴──┴────────────────┴──┴───────────...
	// Offset:     ↑ SIZE_OFFSET        ↑ ELEMENT_OFFSET    ↑ DATA_OFFSET

	static inline constexpr size_t SIZE_OFFSET = 0;

	// Use the private function defined outside instead of the internal
	// class one.
	static inline constexpr size_t ELEMENT_OFFSET = _memory_get_aligned_address(SIZE_OFFSET + sizeof(uint64_t), alignof(uint64_t));
	static inline constexpr size_t DATA_OFFSET = _memory_get_aligned_address(ELEMENT_OFFSET + sizeof(uint64_t), MAX_ALIGN);

	static void *alloc_static(size_t p_bytes, bool p_pad_align = false);
	static void *realloc_static(void *p_memory, size_t p_bytes, bool p_pad_align = false);
	static void free_static(void *p_ptr, bool p_pad_align = false);

	//	                            ↓ return value of alloc_aligned_static
	//	┌─────────────────┬─────────┬─────────┬──────────────────┐
	//	│ padding (up to  │ uint32_t│ void*   │ padding (up to   │
	//	│ p_alignment - 1)│ offset  │ p_bytes │ p_alignment - 1) │
	//	└─────────────────┴─────────┴─────────┴──────────────────┘
	//
	// alloc_aligned_static will allocate p_bytes + p_alignment - 1 + sizeof(uint32_t) and
	// then offset the pointer until alignment is satisfied.
	//
	// This offset is stored before the start of the returned ptr so we can retrieve the original/real
	// start of the ptr in order to free it.
	//
	// The rest is wasted as padding in the beginning and end of the ptr. The sum of padding at
	// both start and end of the block must add exactly to p_alignment - 1.
	//
	// p_alignment MUST be a power of 2.
	static void *alloc_aligned_static(size_t p_bytes, size_t p_alignment);
	static void *realloc_aligned_static(void *p_memory, size_t p_bytes, size_t p_prev_bytes, size_t p_alignment);
	// Pass the ptr returned by alloc_aligned_static to free it.
	// e.g.
	//	void *data = realloc_aligned_static( bytes, 16 );
	//  free_aligned_static( data );
	static void free_aligned_static(void *p_memory);

	static uint64_t get_mem_available();
	static uint64_t get_mem_usage();
	static uint64_t get_mem_max_usage();
};

class DefaultAllocator {
public:
	_FORCE_INLINE_ static void *alloc(size_t p_memory) { return Memory::alloc_static(p_memory, false); }
	_FORCE_INLINE_ static void free(void *p_ptr) { Memory::free_static(p_ptr, false); }
};

void *operator new(size_t p_size, const char *p_description); ///< operator new that takes a description and uses MemoryStaticPool
void *operator new(size_t p_size, void *(*p_allocfunc)(size_t p_size)); ///< operator new that takes a description and uses MemoryStaticPool

void *operator new(size_t p_size, void *p_pointer, size_t check, const char *p_description); ///< operator new that takes a description and uses a pointer to the preallocated memory

#ifdef _MSC_VER
// When compiling with VC++ 2017, the above declarations of placement new generate many irrelevant warnings (C4291).
// The purpose of the following definitions is to muffle these warnings, not to provide a usable implementation of placement delete.
void operator delete(void *p_mem, const char *p_description);
void operator delete(void *p_mem, void *(*p_allocfunc)(size_t p_size));
void operator delete(void *p_mem, void *p_pointer, size_t check, const char *p_description);
#endif

#define memalloc(m_size) Memory::alloc_static(m_size)
#define memrealloc(m_mem, m_size) Memory::realloc_static(m_mem, m_size)
#define memfree(m_mem) Memory::free_static(m_mem)

_ALWAYS_INLINE_ void postinitialize_handler(void *) {}

template <typename T>
_ALWAYS_INLINE_ T *_post_initialize(T *p_obj) {
	postinitialize_handler(p_obj);
	return p_obj;
}

#define memnew(m_class) _post_initialize(::new ("") m_class)

#define memnew_allocator(m_class, m_allocator) _post_initialize(::new (m_allocator::alloc) m_class)
#define memnew_placement(m_placement, m_class) _post_initialize(::new (m_placement) m_class)

_ALWAYS_INLINE_ bool predelete_handler(void *) {
	return true;
}

template <typename T>
void memdelete(T *p_class) {
	if (!predelete_handler(p_class)) {
		return; // doesn't want to be deleted
	}
	if constexpr (!std::is_trivially_destructible_v<T>) {
		p_class->~T();
	}

	Memory::free_static(p_class, false);
}

template <typename T, typename A>
void memdelete_allocator(T *p_class) {
	if (!predelete_handler(p_class)) {
		return; // doesn't want to be deleted
	}
	if constexpr (!std::is_trivially_destructible_v<T>) {
		p_class->~T();
	}

	A::free(p_class);
}

#define memdelete_notnull(m_v) \
	{                          \
		if (m_v) {             \
			memdelete(m_v);    \
		}                      \
	}

#define memnew_arr(m_class, m_count) memnew_arr_template<m_class>(m_count)

_FORCE_INLINE_ uint64_t *_get_element_count_ptr(uint8_t *p_ptr) {
	return (uint64_t *)(p_ptr - Memory::DATA_OFFSET + Memory::ELEMENT_OFFSET);
}

template <typename T>
T *memnew_arr_template(size_t p_elements) {
	if (p_elements == 0) {
		return nullptr;
	}
	/** overloading operator new[] cannot be done , because it may not return the real allocated address (it may pad the 'element count' before the actual array). Because of that, it must be done by hand. This is the
	same strategy used by std::vector, and the Vector class, so it should be safe.*/

	size_t len = sizeof(T) * p_elements;
	uint8_t *mem = (uint8_t *)Memory::alloc_static(len, true);
	T *failptr = nullptr; //get rid of a warning
	ERR_FAIL_NULL_V(mem, failptr);

	uint64_t *_elem_count_ptr = _get_element_count_ptr(mem);
	*(_elem_count_ptr) = p_elements;

	if constexpr (!std::is_trivially_constructible_v<T>) {
		T *elems = (T *)mem;

		/* call operator new */
		for (size_t i = 0; i < p_elements; i++) {
			::new (&elems[i]) T;
		}
	}

	return (T *)mem;
}

/**
 * Wonders of having own array functions, you can actually check the length of
 * an allocated-with memnew_arr() array
 */

template <typename T>
size_t memarr_len(const T *p_class) {
	uint8_t *ptr = (uint8_t *)p_class;
	uint64_t *_elem_count_ptr = _get_element_count_ptr(ptr);
	return *(_elem_count_ptr);
}

template <typename T>
void memdelete_arr(T *p_class) {
	uint8_t *ptr = (uint8_t *)p_class;

	if constexpr (!std::is_trivially_destructible_v<T>) {
		uint64_t *_elem_count_ptr = _get_element_count_ptr(ptr);
		uint64_t elem_count = *(_elem_count_ptr);

		for (uint64_t i = 0; i < elem_count; i++) {
			p_class[i].~T();
		}
	}

	Memory::free_static(ptr, true);
}

struct _GlobalNil {
	int color = 1;
	_GlobalNil *right = nullptr;
	_GlobalNil *left = nullptr;
	_GlobalNil *parent = nullptr;

	_GlobalNil();
};

struct _GlobalNilClass {
	static _GlobalNil _nil;
};

template <typename T>
class DefaultTypedAllocator {
public:
	template <typename... Args>
	_FORCE_INLINE_ T *new_allocation(const Args &&...p_args) { return memnew(T(p_args...)); }
	_FORCE_INLINE_ void delete_allocation(T *p_allocation) { memdelete(p_allocation); }
};

#endif // MEMORY_H
