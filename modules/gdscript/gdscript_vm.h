/**************************************************************************/
/*  gdscript_vm.h                                                         */
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

#ifndef GDSCRIPT_VM_H
#define GDSCRIPT_VM_H

#include <atomic> // For atomic operations in load and acquire

#if defined(__GNUC__) || defined(__clang__)

#pragma GCC diagnostic push

#if defined(__has_warning)
	#if __has_warning("-Watomic-alignment")
		#pragma GCC diagnostic ignored "-Watomic-alignment"
	#endif // __has_warning
#endif // defined(__has_warning)

#endif

static inline uint32_t _gdscript_vm_atomic_load_acquire(const int *p_ptr) {
#if defined(__GNUC__) || defined(__clang__)
	return static_cast<uint32_t>(__atomic_load_n(p_ptr, __ATOMIC_ACQUIRE));
#else
	static_assert(sizeof(std::atomic<int>) == sizeof(int) && alignof(std::atomic<int>) <= alignof(int));
	return static_cast<uint32_t>(reinterpret_cast<const std::atomic<int> *>(p_ptr)->load(std::memory_order_acquire));
#endif // __GUNC__ || __clang__
}
	
static inline void _gdscript_vm_atomic_store_release(int *p_ptr, uint32_t p_val) {
#if defined(__GNUC__) || defined(__clang__)
	__atomic_store_n(p_ptr, static_cast<int>(p_val), __ATOMIC_RELEASE);
#else
	static_assert(sizeof(std::atomic<int>) == sizeof(int) && alignof(std::atomic<int>) <= alignof(int));
	reinterpret_cast<std::atomic<int> *>(p_ptr)->store(static_cast<int>(p_val), std::memory_order_release);
#endif // __GUNC__ || __clang__
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif // GDSCRIPT_VM_H
