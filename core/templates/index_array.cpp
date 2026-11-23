/**************************************************************************/
/*  index_array.cpp                                                       */
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

#include "index_array.h"

#include <cstring>

void IndexArray::_allocate(uint32_t p_size) {
	size = p_size;
	if (size == 0) {
		small = nullptr;
		return;
	}

	const size_t alloc_size = get_allocated_size();
	small = reinterpret_cast<uint8_t *>(Memory::alloc_aligned_static(alloc_size, get_alignment()));
}

size_t IndexArray::get_allocated_size() const {
	switch (state) {
		case Small:
			return static_cast<size_t>(size) * sizeof(uint8_t);
		case Medium:
			return static_cast<size_t>(size) * sizeof(uint16_t);
		case Large:
			return static_cast<size_t>(size) * sizeof(uint32_t);
	}
	return 0;
}

size_t IndexArray::get_alignment() const {
	switch (state) {
		case Small:
			return alignof(uint8_t);
		case Medium:
			return alignof(uint16_t);
		case Large:
			return alignof(uint32_t);
	}
	return alignof(uint8_t);
}

void IndexArray::initialize(uint32_t p_size, uint32_t p_max_index) {
	reset();
	if (p_max_index <= UINT8_MAX) {
		state = Small;
	} else if (p_max_index <= UINT16_MAX) {
		state = Medium;
	} else {
		state = Large;
	}
	_allocate(p_size);
}

void IndexArray::operator=(const IndexArray &p_other) {
	if (this == &p_other) {
		return;
	}

	reset();
	state = p_other.state;
	if (p_other.small != nullptr && p_other.size > 0) {
		_allocate(p_other.size);
		memcpy(small, p_other.small, get_allocated_size());
	}
}

IndexArray::IndexArray(const IndexArray &p_other) {
	operator=(p_other);
}

void IndexArray::reset() {
	if (small != nullptr) {
		Memory::free_aligned_static(small);
		small = nullptr;
	}
	size = 0;
}
