/**************************************************************************/
/*  polyfill_gles1.h                                                      */
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

#ifndef POLYFILL_GLES1_H
#define POLYFILL_GLES1_H

// This polyfill handles function resolution
// between Desktop, OES, and EXT contexts transparently
// in the background. Since one, OR the other, OR the other
// may be available at any given time.
//
// !!! NOTE !!!
// When using a new GLES1 function that isn't
// here, ADD IT HERE AND ROUTE IT. Otherwise it may crash
// with nullptr exceptions.

#include "thirdparty/glad/glad/gl.h"

namespace GLES1 {
    struct Polyfill {
        // Framebuffer extensions
		static PFNGLBINDFRAMEBUFFERPROC bindFramebuffer;
		static PFNGLISFRAMEBUFFERPROC isFramebuffer;
		static PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFrameBufferStatus;
		static PFNGLDELETEBUFFERSPROC deleteFrameBuffers;
		static PFNGLGENFRAMEBUFFERSPROC genFrameBuffers;
		static PFNGLGENRENDERBUFFERSPROC genRenderBuffers;
		static PFNGLFRAMEBUFFERTEXTURE2DPROC framebufferTexture2D;
		static PFNGLBINDRENDERBUFFERPROC bindRenderBuffer;
		static PFNGLRENDERBUFFERSTORAGEPROC renderBufferStorage;
		static PFNGLFRAMEBUFFERRENDERBUFFERPROC frameBufferRenderBuffer;
		static PFNGLDELETERENDERBUFFERSPROC deleteRenderBuffers;

		// Blend extensions
		static PFNGLBLENDFUNCSEPARATEPROC blendFuncSeparate;

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
#ifndef POLYFILL_GLES1_IMPL

#undef glBindFramebufferOES
#define glBindFramebufferOES(target, framebuffer)                  \
	do {                                                           \
		if (likely(GLES1::Polyfill::bindFramebuffer))              \
			GLES1::Polyfill::bindFramebuffer(target, framebuffer); \
	} while (0)

#undef glGenFramebuffersOES
#define glGenFramebuffersOES(n, framebuffers)                  \
	do {                                                       \
		if (likely(GLES1::Polyfill::genFrameBuffers))          \
			GLES1::Polyfill::genFrameBuffers(n, framebuffers); \
	} while (0)

#undef glDeleteFramebuffersOES
#define glDeleteFramebuffersOES(n, framebuffers)                  \
	do {                                                          \
		if (likely(GLES1::Polyfill::deleteFrameBuffers))          \
			GLES1::Polyfill::deleteFrameBuffers(n, framebuffers); \
	} while (0)

#undef glFramebufferTexture2DOES
#define glFramebufferTexture2DOES(target, attachment, textarget, texture, level)                  \
	do {                                                                                          \
		if (likely(GLES1::Polyfill::framebufferTexture2D))                                        \
			GLES1::Polyfill::framebufferTexture2D(target, attachment, textarget, texture, level); \
	} while (0)

#undef glGenRenderbuffersOES
#define glGenRenderbuffersOES(n, renderbuffers)                  \
	do {                                                         \
		if (likely(GLES1::Polyfill::genRenderBuffers))           \
			GLES1::Polyfill::genRenderBuffers(n, renderbuffers); \
	} while (0)

#undef glBindRenderbufferOES
#define glBindRenderbufferOES(target, renderbuffer)                  \
	do {                                                             \
		if (likely(GLES1::Polyfill::bindRenderBuffer))               \
			GLES1::Polyfill::bindRenderBuffer(target, renderbuffer); \
	} while (0)

#undef glRenderbufferStorageOES
#define glRenderbufferStorageOES(target, internalformat, width, height)                  \
	do {                                                                                 \
		if (likely(GLES1::Polyfill::renderBufferStorage))                                \
			GLES1::Polyfill::renderBufferStorage(target, internalformat, width, height); \
	} while (0)

#undef glFramebufferRenderbufferOES
#define glFramebufferRenderbufferOES(target, attachment, renderbuffertarget, renderbuffer)                  \
	do {                                                                                                    \
		if (likely(GLES1::Polyfill::frameBufferRenderBuffer))                                               \
			GLES1::Polyfill::frameBufferRenderBuffer(target, attachment, renderbuffertarget, renderbuffer); \
	} while (0)

#undef glDeleteRenderbuffersOES
#define glDeleteRenderbuffersOES(n, renderbuffers)                  \
	do {                                                            \
		if (likely(GLES1::Polyfill::deleteRenderBuffers))           \
			GLES1::Polyfill::deleteRenderBuffers(n, renderbuffers); \
	} while (0)

#undef glBlendFuncSeparateOES
#define glBlendFuncSeparateOES(srcRGB, dstRGB, srcAlpha, dstAlpha)                  \
	do {                                                                            \
		if (likely(GLES1::Polyfill::blendFuncSeparate))                             \
			GLES1::Polyfill::blendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha); \
	} while (0)

#undef glIsFramebufferOES
#define glIsFramebufferOES(framebuffer) \
	(likely(GLES1::Polyfill::isFramebuffer) ? GLES1::Polyfill::isFramebuffer(framebuffer) : GL_FALSE)

#undef glCheckFramebufferStatusOES
#define glCheckFramebufferStatusOES(target) \
	(likely(GLES1::Polyfill::checkFrameBufferStatus) ? GLES1::Polyfill::checkFrameBufferStatus(target) : 0)

#endif // POLYFILL_GLES1_IMPL

#endif // POLYFILL_GLES1_H
