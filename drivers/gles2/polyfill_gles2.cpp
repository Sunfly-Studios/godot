/**************************************************************************/
/*  polyfill_gles2.cpp                                                    */
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

#define POLYFILL_GLES2_IMPL
#include "polyfill_gles2.h"

namespace GLES2 {
// Define the static pointers
PFNGLBINDBUFFERBASEPROC Polyfill::bindBufferBase = nullptr;
PFNGLBEGINTRANSFORMFEEDBACKPROC Polyfill::beginTransformFeedback = nullptr;
PFNGLENDTRANSFORMFEEDBACKPROC Polyfill::endTransformFeedback = nullptr;
PFNGLBINDBUFFERRANGEPROC Polyfill::bindBufferRange = nullptr;
PFNGLTRANSFORMFEEDBACKVARYINGSPROC Polyfill::transformFeedbackVaryings = nullptr;

#if !defined(WEB_ENABLED)
PFNGLUNMAPBUFFERPROC Polyfill::unmapBuffer = nullptr;
#endif

Polyfill::Polyfill() {
#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED) || defined(WEB_ENABLED)
	// On mobile, these are statically linked in the headers
	// and do not require runtime null checks or EXT fallbacks.
	bindBufferBase = glBindBufferBase;
	bindBufferRange = glBindBufferRange;
	beginTransformFeedback = glBeginTransformFeedback;
	endTransformFeedback = glEndTransformFeedback;
	transformFeedbackVaryings = glTransformFeedbackVaryings;
#else
	// BindBufferBase
	if (glBindBufferBase != nullptr) {
		bindBufferBase = glBindBufferBase;
	} else if (glBindBufferBaseEXT != nullptr) {
		bindBufferBase = (PFNGLBINDBUFFERBASEPROC)glBindBufferBaseEXT;
	}

	// BindBufferRange
	if (glBindBufferRange != nullptr) {
		bindBufferRange = glBindBufferRange;
	} else if (glBindBufferRangeEXT != nullptr) {
		bindBufferRange = (PFNGLBINDBUFFERRANGEPROC)glBindBufferRangeEXT;
	}

	// BeginTransformFeedback
	if (glBeginTransformFeedback != nullptr) {
		beginTransformFeedback = glBeginTransformFeedback;
	} else if (glBeginTransformFeedbackEXT != nullptr) {
		beginTransformFeedback = (PFNGLBEGINTRANSFORMFEEDBACKPROC)glBeginTransformFeedbackEXT;
	}

	// EndTransformFeedback
	if (glEndTransformFeedback != nullptr) {
		endTransformFeedback = glEndTransformFeedback;
	} else if (glEndTransformFeedbackEXT != nullptr) {
		endTransformFeedback = (PFNGLENDTRANSFORMFEEDBACKPROC)glEndTransformFeedbackEXT;
	}

	// TransformFeedbackVaryings
	if (glTransformFeedbackVaryings != nullptr) {
		transformFeedbackVaryings = glTransformFeedbackVaryings;
	} else if (glTransformFeedbackVaryingsEXT != nullptr) {
		transformFeedbackVaryings = (PFNGLTRANSFORMFEEDBACKVARYINGSPROC)glTransformFeedbackVaryingsEXT;
	}

	// UnmapBuffer
	if (glUnmapBuffer != nullptr) {
		unmapBuffer = glUnmapBuffer;
	} else if (glUnmapBufferOES != nullptr) {
		unmapBuffer = (PFNGLUNMAPBUFFERPROC)glUnmapBufferOES;
	}

#endif
}
} //namespace GLES2
