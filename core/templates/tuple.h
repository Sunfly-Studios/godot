/**************************************************************************/
/*  tuple.h                                                               */
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

#ifndef TUPLE_H
#define TUPLE_H

// Simple multiple inheritance Tuple type that has no runtime overhead.
//
// The compile-time deduction works as follows:
// Assume the following: Tuple<int, float> my_tuple(42, 3.14f);
// This expands to a class hierarchy that creates a distinct TupleLeaf<I, T> for each value.
// So in this case this leads to:
//  - struct TupleLeaf<0, int>        <--- This contains the int value.
//  - struct TupleLeaf<1, float>      <--- This contains the float value.
// where each of the classes has a single field of the type. So: float value;  int value; etc.
//
// This works by using fold expressions to inherit from every TupleLeaf<Is, Types>... at
// the same time with the help of the index sequence.
//
// Tuple<int, float> creates implementation:
// TupleImpl<IndexSequence<0, 1>, int, float> : TupleLeaf<0, int>, TupleLeaf<1, float>
//
// tuple_get<I> works through a template deduction for TupleLeaf<I, T>, it works because
// every tuple element is a distinct base class.

#include "core/typedefs.h"

#include <type_traits>

template <size_t I, typename T>
class TupleLeaf {
public:
	T value;

protected:
	_FORCE_INLINE_ constexpr TupleLeaf() = default;

	template <typename U>
	_FORCE_INLINE_ constexpr TupleLeaf(U &&p_value) :
			value(std::forward<U>(p_value)) {}
};

template <size_t I, typename T>
_FORCE_INLINE_ constexpr T &tuple_get(TupleLeaf<I, T> &p_tuple) {
	return p_tuple.value;
}
template <size_t I, typename T>
_FORCE_INLINE_ constexpr const T &tuple_get(const TupleLeaf<I, T> &p_tuple) {
	return p_tuple.value;
}
template <size_t I, typename T>
_FORCE_INLINE_ constexpr T &&tuple_get(TupleLeaf<I, T> &&p_tuple) {
	return std::forward<T>(p_tuple.value);
}
template <size_t I, typename T>
_FORCE_INLINE_ constexpr const T &&tuple_get(const TupleLeaf<I, T> &&p_tuple) {
	return std::forward<const T>(p_tuple.value);
}

template <size_t I, typename T>
using TupleElement = std::decay_t<decltype(tuple_get<I>(std::declval<T>()))>;

template <typename IndexSequenceT, typename... Types>
class TupleImpl;

template <>
class TupleImpl<IndexSequence<0>> {
protected:
	constexpr TupleImpl() = default;
};

template <size_t... Is, typename... Types>
class TupleImpl<IndexSequence<Is...>, Types...> : public TupleLeaf<Is, Types>... {
protected:
	_FORCE_INLINE_ constexpr TupleImpl() = default;

	template <size_t... ArgsIs, typename... Args>
	_FORCE_INLINE_ constexpr TupleImpl(IndexSequence<ArgsIs...>, Args &&...p_args) :
			TupleLeaf<ArgsIs, TupleElement<ArgsIs, TupleImpl>>(std::forward<Args>(p_args))... {}
};

template <typename... Types>
class Tuple : public TupleImpl<typename BuildIndexSequence<sizeof...(Types)>::Type, Types...> {
	using base = TupleImpl<typename BuildIndexSequence<sizeof...(Types)>::Type, Types...>;

public:
	_FORCE_INLINE_ constexpr Tuple() = default;

	template <typename Arg, typename... Args, std::enable_if_t<!std::is_same_v<Tuple, std::decay_t<Arg>>, bool> = true>
	_FORCE_INLINE_ constexpr Tuple(Arg &&p_arg, Args &&...p_args) :
			base(typename BuildIndexSequence<1 + sizeof...(Args)>::Type{}, std::forward<Arg>(p_arg), std::forward<Args>(p_args)...) {}
};

template <typename... Types>
Tuple(Types &&...p_args) -> Tuple<std::decay_t<Types>...>;

namespace std {
template <typename... Types>
struct tuple_size<::Tuple<Types...>> {
	static constexpr size_t value = sizeof...(Types);
};

template <size_t I, typename... Types>
struct tuple_element<I, ::Tuple<Types...>> {
	using type = TupleElement<I, ::Tuple<Types...>>;
};
} // namespace std

template <size_t I, typename... Types>
constexpr TupleElement<I, Tuple<Types...>> &get(Tuple<Types...> &p_tuple) {
	return tuple_get<I>(p_tuple);
}
template <size_t I, typename... Types>
constexpr const TupleElement<I, Tuple<Types...>> &get(const Tuple<Types...> &p_tuple) {
	return tuple_get<I>(p_tuple);
}
template <size_t I, typename... Types>
constexpr TupleElement<I, Tuple<Types...>> &&get(Tuple<Types...> &&p_tuple) {
	return tuple_get<I>(std::move(p_tuple));
}
template <size_t I, typename... Types>
constexpr const TupleElement<I, Tuple<Types...>> &&get(const Tuple<Types...> &&p_tuple) {
	return tuple_get<I>(std::move(p_tuple));
}

#endif // TUPLE_H
