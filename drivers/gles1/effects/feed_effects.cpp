/**************************************************************************/
/*  feed_effects.cpp                                                      */
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

#ifdef GLES1_ENABLED

#include "feed_effects.h"

#ifdef ANDROID_ENABLED
#include <GLES/glext.h>
#endif

#define GL_PROGRAM_POINT_SIZE 0x8642

using namespace GLES1;

FeedEffects *FeedEffects::singleton = nullptr;

FeedEffects *FeedEffects::get_singleton() {
	return singleton;
}

FeedEffects::FeedEffects() {
	singleton = this;
}

FeedEffects::~FeedEffects() {
	singleton = nullptr;
}

[[maybe_unused]] static Transform3D transform3D_from_mat4(const float *p_mat4) {
    return Transform3D();
}

void FeedEffects::draw() {

}

void FeedEffects::draw_screen_triangle() {
    
}

#endif // GLES1_ENABLED
