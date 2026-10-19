/**************************************************************************/
/*  object_soa_pool.h                                                     */
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

#ifndef OBJECT_SOA_POOL_H
#define OBJECT_SOA_POOL_H

#include "core/error/error_macros.h"
#include "core/os/memory.h"
#include "core/templates/generational_handle.h"
#include "core/templates/local_vector.h"
#include "core/typedefs.h"

#include <new>
#include <type_traits>
#include <utility>

// Compile-time type indexing without pulling in <tuple>.
template <size_t Index, typename T, typename... Rest>
struct SOATypeAt {
	using Type = typename SOATypeAt<Index - 1, Rest...>::Type;
};

template <typename T, typename... Rest>
struct SOATypeAt<0, T, Rest...> {
	using Type = T;
};

// Individual contiguous array leaf for a single SoA component.
template <size_t Index, typename T>
struct SOAStreamLeaf {
	LocalVector<T> stream;
};

// Flat multiple-inheritance pack of all component streams.
template <typename Sequence, typename... Components>
struct SOAStreamPack;

template <size_t... Is, typename... Components>
struct SOAStreamPack<IndexSequence<Is...>, Components...> : SOAStreamLeaf<Is, Components>... {};

class ObjectSOAPoolBase {
public:
	static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFU;
	
#if defined(IS_32_BIT)
	static constexpr size_t CACHE_LINE_ALIGNMENT = 32;
#else
	static constexpr size_t CACHE_LINE_ALIGNMENT = 64;
#endif

protected:
	struct SparseSlot {
		uint32_t dense_index;
		uint32_t generation;
		uint32_t next_free;
	};

	SparseSlot *sparse_slots = nullptr;
	uint32_t sparse_capacity = 0;
	uint32_t sparse_count = 0;
	uint32_t free_list_head = INVALID_INDEX;

	LocalVector<GenerationalHandle> dense_to_handle;

	ObjectSOAPoolBase() = default;
	~ObjectSOAPoolBase();

	ObjectSOAPoolBase(const ObjectSOAPoolBase &) = delete;
	ObjectSOAPoolBase &operator=(const ObjectSOAPoolBase &) = delete;

	ObjectSOAPoolBase(ObjectSOAPoolBase &&p_other) noexcept;
	ObjectSOAPoolBase &operator=(ObjectSOAPoolBase &&p_other) noexcept;

	void _grow_sparse_table(uint32_t p_min_capacity);
	GenerationalHandle _allocate_handle(uint32_t p_dense_index);
	bool _free_handle(GenerationalHandle p_handle, uint32_t &r_target_dense, uint32_t &r_last_dense);
	void _reset_handles();

public:
	_FORCE_INLINE_ bool is_valid(GenerationalHandle p_handle) const {
		if (unlikely(!p_handle.is_valid())) {
			return false;
		}
		const uint32_t idx = p_handle.get_index();
		if (unlikely(idx >= sparse_count || sparse_slots == nullptr)) {
			return false;
		}
		const SparseSlot &slot = *std::launder(&sparse_slots[idx]);
		return slot.dense_index < dense_to_handle.size() && slot.generation == p_handle.get_generation();
	}

	_FORCE_INLINE_ uint32_t get_dense_index(GenerationalHandle p_handle) const {
		DEV_ASSERT(is_valid(p_handle));
		return std::launder(&sparse_slots[p_handle.get_index()])->dense_index;
	}

	_FORCE_INLINE_ const LocalVector<GenerationalHandle> &get_handles() const {
		return dense_to_handle;
	}

	_FORCE_INLINE_ uint32_t size() const {
		return dense_to_handle.size();
	}

	_FORCE_INLINE_ bool is_empty() const {
		return dense_to_handle.is_empty();
	}
};

template <typename... Components>
class ObjectSOAPool : public ObjectSOAPoolBase {
	static_assert(sizeof...(Components) > 0, "ObjectSOAPool requires at least one component stream.");
	static_assert(((sizeof(Components) <= 512) && ...), "Component size too large for ObjectSOAPool (> 512 bytes); use PagedAllocator for heavy objects.");
	static_assert(((std::is_move_constructible_v<Components> && std::is_move_assignable_v<Components>) && ...), "All ObjectSOAPool components must support move semantics for swap-and-pop.");
	static_assert(((!std::is_polymorphic_v<Components>) && ...), "Polymorphic types with vtables violate strict DOD layout in ObjectSOAPool.");

public:
	static constexpr size_t COMPONENT_COUNT = sizeof...(Components);

	template <size_t Index>
	using ComponentType = typename SOATypeAt<Index, Components...>::Type;

private:
	using Sequence = BuildIndexSequence<COMPONENT_COUNT>;
	SOAStreamPack<Sequence, Components...> streams;

	// Single-stream helpers
	template <size_t Index>
	_FORCE_INLINE_ void _push_default_single() {
		using T = ComponentType<Index>;
		get_stream<Index>().push_back(T{});
	}

	template <size_t Index, typename Arg>
	_FORCE_INLINE_ void _push_value_single(Arg &&p_arg) {
		get_stream<Index>().push_back(std::forward<Arg>(p_arg));
	}

	template <size_t Index>
	_FORCE_INLINE_ void _swap_and_pop_single(uint32_t p_target_dense, uint32_t p_last_dense) {
		LocalVector<ComponentType<Index>> &vec = get_stream<Index>();
		if (p_target_dense != p_last_dense) {
			ComponentType<Index> *dst = std::launder(&vec[p_target_dense]);
			ComponentType<Index> *src = std::launder(&vec[p_last_dense]);
			*dst = std::move(*src);
		}
		vec.resize(p_last_dense);
	}

	// Pack expansion dispatchers
	template <size_t... Is>
	_FORCE_INLINE_ void _push_defaults_all(IndexSequence<Is...>) {
		(_push_default_single<Is>(), ...);
	}

	template <size_t... Is, typename... Args>
	_FORCE_INLINE_ void _push_values_all(IndexSequence<Is...>, Args &&...p_args) {
		(_push_value_single<Is>(std::forward<Args>(p_args)), ...);
	}

	template <size_t... Is>
	_FORCE_INLINE_ void _swap_and_pop_all(uint32_t p_target_dense, uint32_t p_last_dense, IndexSequence<Is...>) {
		(_swap_and_pop_single<Is>(p_target_dense, p_last_dense), ...);
	}

	template <size_t... Is>
	_FORCE_INLINE_ void _reserve_all(uint32_t p_capacity, IndexSequence<Is...>) {
		(get_stream<Is>().reserve(p_capacity), ...);
	}

	template <size_t... Is>
	_FORCE_INLINE_ void _tight_all(IndexSequence<Is...>) {
		(get_stream<Is>().tight(), ...);
	}

	template <size_t... Is>
	_FORCE_INLINE_ void _clear_all(IndexSequence<Is...>) {
		(get_stream<Is>().clear(), ...);
	}

public:
	ObjectSOAPool() = default;
	~ObjectSOAPool() = default;

	ObjectSOAPool(const ObjectSOAPool &) = delete;
	ObjectSOAPool &operator=(const ObjectSOAPool &) = delete;

	ObjectSOAPool(ObjectSOAPool &&p_other) noexcept = default;
	ObjectSOAPool &operator=(ObjectSOAPool &&p_other) noexcept = default;

	GenerationalHandle allocate() {
		static_assert((std::is_default_constructible_v<Components> && ...), "allocate() without arguments requires all components to be default-constructible.");
		const uint32_t dense_idx = dense_to_handle.size();
		const GenerationalHandle handle = _allocate_handle(dense_idx);
		if (unlikely(handle.is_null())) {
			return GenerationalHandle();
		}
		_push_defaults_all(Sequence{});
		return handle;
	}

	template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) == COMPONENT_COUNT) && (std::is_constructible_v<Components, Args &&> && ...)>>
	GenerationalHandle allocate(Args &&...p_args) {
		const uint32_t dense_idx = dense_to_handle.size();
		const GenerationalHandle handle = _allocate_handle(dense_idx);
		if (unlikely(handle.is_null())) {
			return GenerationalHandle();
		}
		_push_values_all(Sequence{}, std::forward<Args>(p_args)...);
		return handle;
	}

	bool free(GenerationalHandle p_handle) {
		uint32_t target_dense = INVALID_INDEX;
		uint32_t last_dense = INVALID_INDEX;
		if (unlikely(!_free_handle(p_handle, target_dense, last_dense))) {
			return false;
		}
		_swap_and_pop_all(target_dense, last_dense, Sequence{});
		return true;
	}

	// Casts the multi-inherited pack to the exact leaf for O(1) compile-time stream lookup.
	template <size_t Index>
	_FORCE_INLINE_ LocalVector<ComponentType<Index>> &get_stream() {
		static_assert(Index < COMPONENT_COUNT, "Component stream index out of bounds.");
		return static_cast<SOAStreamLeaf<Index, ComponentType<Index>> &>(streams).stream;
	}

	template <size_t Index>
	_FORCE_INLINE_ const LocalVector<ComponentType<Index>> &get_stream() const {
		static_assert(Index < COMPONENT_COUNT, "Component stream index out of bounds.");
		return static_cast<const SOAStreamLeaf<Index, ComponentType<Index>> &>(streams).stream;
	}

	template <size_t Index>
	_FORCE_INLINE_ ComponentType<Index> &get_component(GenerationalHandle p_handle) {
		static_assert(Index < COMPONENT_COUNT, "Component stream index out of bounds.");
		const uint32_t dense_idx = get_dense_index(p_handle);
		return *std::launder(&get_stream<Index>()[dense_idx]);
	}

	template <size_t Index>
	_FORCE_INLINE_ const ComponentType<Index> &get_component(GenerationalHandle p_handle) const {
		static_assert(Index < COMPONENT_COUNT, "Component stream index out of bounds.");
		const uint32_t dense_idx = get_dense_index(p_handle);
		return *std::launder(&get_stream<Index>()[dense_idx]);
	}

	void reserve(uint32_t p_capacity) {
		_grow_sparse_table(p_capacity);
		dense_to_handle.reserve(p_capacity);
		_reserve_all(p_capacity, Sequence{});
	}

	void shrink_to_fit() {
		dense_to_handle.tight();
		_tight_all(Sequence{});
	}

	void clear() {
		_clear_all(Sequence{});
		_reset_handles();
	}
};

#endif // OBJECT_SOA_POOL_H