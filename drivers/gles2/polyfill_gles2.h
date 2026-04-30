/**************************************************************************/
/*  polyfill_gles2.h                                                      */
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

#ifndef POLYFILL_GLES2_H
#define POLYFILL_GLES2_H

// This polyfill handles function resolution
// between Desktop, OES, and EXT contexts transparently
// in the background. Since one, OR the other, OR the other
// may be available at any given time.
//
// !!! NOTE !!!
// When using a new GLES2 function that isn't
// here, ADD IT HERE AND ROUTE IT. Otherwise it may crash
// with nullptr exceptions.

#include "thirdparty/glad/glad/gl.h"

namespace GLES2 {
	struct Polyfill {
		// Framebuffer extensions
		static PFNGLBINDBUFFERBASEPROC bindBufferBase;
		static PFNGLBEGINTRANSFORMFEEDBACKPROC beginTransformFeedback;
		static PFNGLENDTRANSFORMFEEDBACKPROC endTransformFeedback;
		static PFNGLBINDBUFFERRANGEPROC bindBufferRange;
		static PFNGLTRANSFORMFEEDBACKVARYINGSPROC transformFeedbackVaryings;

		Polyfill();
		~Polyfill() = default;
	};
}

// The engine code needs the macros to point to these,
// but our polyfill needs the macros to point to the
// original GLAD pointers.
//
// The solution to this is to #undef the GLAD macros in the header,
// but hide that part from the implementation file.
#ifndef POLYFILL_GLES2_IMPL

#undef glBindBufferBaseEXT
#define glBindBufferBaseEXT(target, index, buffer)                  \
	do {                                                           \
		if (likely(GLES2::Polyfill::bindBufferBase))              \
			GLES2::Polyfill::bindBufferBase(target, index, buffer); \
	} while (0)

#undef glBeginTransformFeedbackEXT
#define glBeginTransformFeedbackEXT(primitiveMode) \
	do {                                                                \
		if (likely(GLES2::Polyfill::beginTransformFeedback))                    \
			GLES2::Polyfill::beginTransformFeedback(primitiveMode);     \
	} while (0)

#undef glEndTransformFeedbackEXT
#define glEndTransformFeedbackEXT()                        \
	do {                                                   \
		if (likely(GLES2::Polyfill::endTransformFeedback)) \
			GLES2::Polyfill::endTransformFeedback();       \
	} while (0)
	
#undef glBindBufferRangeEXT
#define glBindBufferRangeEXT(target, index, buffer, offset, size)                  \
	do {                                                                           \
		if (likely(GLES2::Polyfill::bindBufferRange))                              \
			GLES2::Polyfill::bindBufferRange(target, index, buffer, offset, size); \
	} while (0)
	
#undef glTransformFeedbackVaryingsEXT
#define glTransformFeedbackVaryingsEXT(program, count, varyings, bufferMode)                  \
	do {                                                                                      \
		if (likely(GLES2::Polyfill::transformFeedbackVaryings))                               \
			GLES2::Polyfill::transformFeedbackVaryings(program, count, varyings, bufferMode); \
	} while (0)

#endif // POLYFILL_GLES2_IMPL

#endif // POLYFILL_GLES2_H
