/**************************************************************************/
/*  polyfill_gles1.cpp                                                    */
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

#define POLYFILL_GLES1_IMPL
#include "polyfill_gles1.h"

namespace GLES1 {
	// Define the static pointers

	// FBOs
	PFNGLBINDFRAMEBUFFERPROC Polyfill::bindFramebuffer = nullptr;
	PFNGLISFRAMEBUFFERPROC Polyfill::isFramebuffer = nullptr;
	PFNGLBLENDFUNCSEPARATEPROC Polyfill::blendFuncSeparate = nullptr;
	PFNGLCHECKFRAMEBUFFERSTATUSPROC Polyfill::checkFrameBufferStatus = nullptr;
	PFNGLDELETEFRAMEBUFFERSPROC Polyfill::deleteFrameBuffers = nullptr;
	PFNGLGENFRAMEBUFFERSPROC Polyfill::genFrameBuffers = nullptr;
	PFNGLGENRENDERBUFFERSPROC Polyfill::genRenderBuffers = nullptr;
	PFNGLFRAMEBUFFERTEXTURE2DPROC Polyfill::framebufferTexture2D = nullptr;
	PFNGLBINDRENDERBUFFERPROC Polyfill::bindRenderBuffer = nullptr;
	PFNGLRENDERBUFFERSTORAGEPROC Polyfill::renderBufferStorage = nullptr;
	PFNGLFRAMEBUFFERRENDERBUFFERPROC Polyfill::frameBufferRenderBuffer = nullptr;
	PFNGLDELETERENDERBUFFERSPROC Polyfill::deleteRenderBuffers = nullptr;

	// VBOs
	PFNGLGENBUFFERSPROC Polyfill::genBuffers = nullptr;
	PFNGLBINDBUFFERPROC Polyfill::bindBuffer = nullptr;
	PFNGLBUFFERDATAPROC Polyfill::bufferData = nullptr;
	PFNGLBUFFERSUBDATAPROC Polyfill::bufferSubData = nullptr;
	PFNGLDELETEBUFFERSPROC Polyfill::deleteBuffers = nullptr;
	PFNGLISBUFFERPROC Polyfill::isBuffer = nullptr;

	// Multitexture
	PFNGLACTIVETEXTUREPROC Polyfill::activeTexture = nullptr;
	PFNGLCLIENTACTIVETEXTUREPROC Polyfill::clientActiveTexture = nullptr;

Polyfill::Polyfill() {
#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED)
	// ===============================
	// Mobile Static Assignment
	// ===============================
	
	// FBOs (OES extensions in mobile)
	bindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)glBindFramebufferOES;
	isFramebuffer = (PFNGLISFRAMEBUFFERPROC)glIsFramebufferOES;
	blendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)glBlendFuncSeparateOES;
	checkFrameBufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glCheckFramebufferStatusOES;
	deleteFrameBuffers = (PFNGLDELETEFRAMEBUFFERSPROC)glDeleteFramebuffersOES;
	genFrameBuffers = (PFNGLGENFRAMEBUFFERSPROC)glGenFramebuffersOES;
	genRenderBuffers = (PFNGLGENRENDERBUFFERSPROC)glGenRenderbuffersOES;
	renderBufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)glRenderbufferStorageOES;
	deleteRenderBuffers = (PFNGLDELETERENDERBUFFERSPROC)glDeleteRenderbuffersOES;
	bindRenderBuffer = (PFNGLBINDRENDERBUFFERPROC)glBindRenderbufferOES;
	frameBufferRenderBuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)glFramebufferRenderbufferOES;
	framebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)glFramebufferTexture2DOES;

	// VBOs (Core in mobile)
	genBuffers = glGenBuffers;
	bindBuffer = glBindBuffer;
	bufferData = glBufferData;
	bufferSubData = glBufferSubData;
	deleteBuffers = glDeleteBuffers;
	isBuffer = glIsBuffer;

	// Multitexture (Core in mobile)
	activeTexture = glActiveTexture;
	clientActiveTexture = glClientActiveTexture;
#else
	// ===============================
	// Desktop Dynamic Loading (GLAD)
	// ===============================
	
	// =========================================
	// Framebuffer
	// =========================================
	if (glBindFramebuffer != nullptr) {
		bindFramebuffer = glBindFramebuffer;
	} else if (glBindFramebufferOES != nullptr) {
		bindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)glBindFramebufferOES;
	} else if (glBindFramebufferEXT != nullptr) {
		bindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)glBindFramebufferEXT;
	}

	if (glIsFramebuffer != nullptr) {
		isFramebuffer = glIsFramebuffer;
	} else if (glIsFramebufferOES != nullptr) {
		isFramebuffer = (PFNGLISFRAMEBUFFERPROC)glIsFramebufferOES;
	} else if (glIsFramebufferEXT != nullptr) {
		isFramebuffer = (PFNGLISFRAMEBUFFERPROC)glIsFramebufferEXT;
	}

	if (glBlendFuncSeparate != nullptr) {
		blendFuncSeparate = glBlendFuncSeparate;
	} else if (glBlendFuncSeparateOES != nullptr) {
		blendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)glBlendFuncSeparateOES;
	} else if (glBlendFuncSeparateEXT != nullptr) {
		blendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)glBlendFuncSeparateEXT;
	}

	if (glCheckFramebufferStatus != nullptr) {
		checkFrameBufferStatus = glCheckFramebufferStatus;
	} else if (glCheckFramebufferStatusOES != nullptr) {
		checkFrameBufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glCheckFramebufferStatusOES;
	} else if (glCheckFramebufferStatusEXT != nullptr) {
		checkFrameBufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glCheckFramebufferStatusEXT;
	}

	if (glDeleteFramebuffers != nullptr) {
		deleteFrameBuffers = glDeleteFramebuffers;
	} else if (glDeleteFramebuffersOES != nullptr) {
		deleteFrameBuffers = (PFNGLDELETEFRAMEBUFFERSPROC)glDeleteFramebuffersOES;
	} else if (glDeleteFramebuffersEXT != nullptr) {
		deleteFrameBuffers = (PFNGLDELETEFRAMEBUFFERSPROC)glDeleteFramebuffersEXT;
	}

	if (glGenFramebuffers != nullptr) {
		genFrameBuffers = glGenFramebuffers;
	} else if (glGenFramebuffersOES != nullptr) {
		genFrameBuffers = (PFNGLGENFRAMEBUFFERSPROC)glGenFramebuffersOES;
	} else if (glGenFramebuffersEXT != nullptr) {
		genFrameBuffers = (PFNGLGENFRAMEBUFFERSPROC)glGenFramebuffersEXT;
	}

	if (glGenRenderbuffers != nullptr) {
		genRenderBuffers = glGenRenderbuffers;
	} else if (glGenRenderbuffersOES != nullptr) {
		genRenderBuffers = (PFNGLGENRENDERBUFFERSPROC)glGenRenderbuffersOES;
	} else if (glGenRenderbuffersEXT != nullptr) {
		genRenderBuffers = (PFNGLGENRENDERBUFFERSPROC)glGenRenderbuffersEXT;
	}

	if (glRenderbufferStorage != nullptr) {
		renderBufferStorage = glRenderbufferStorage;
	} else if (glRenderbufferStorageOES != nullptr) {
		renderBufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)glRenderbufferStorageOES;
	} else if (glRenderbufferStorageEXT != nullptr) {
		renderBufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)glRenderbufferStorageEXT;
	}

	if (glDeleteRenderbuffers != nullptr) {
		deleteRenderBuffers = glDeleteRenderbuffers;
	} else if (glDeleteRenderbuffersOES != nullptr) {
		deleteRenderBuffers = (PFNGLDELETERENDERBUFFERSPROC)glDeleteRenderbuffersOES;
	} else if (glDeleteRenderbuffersEXT != nullptr) {
		deleteRenderBuffers = (PFNGLDELETERENDERBUFFERSPROC)glDeleteRenderbuffersEXT;
	}

	if (glBindRenderbuffer != nullptr) {
		bindRenderBuffer = glBindRenderbuffer;
	} else if (glBindRenderbufferOES != nullptr) {
		bindRenderBuffer = (PFNGLBINDRENDERBUFFERPROC)glBindRenderbufferOES;
	} else if (glBindRenderbufferEXT != nullptr) {
		bindRenderBuffer = (PFNGLBINDRENDERBUFFERPROC)glBindRenderbufferEXT;
	}

	if (glFramebufferRenderbuffer != nullptr) {
		frameBufferRenderBuffer = glFramebufferRenderbuffer;
	} else if (glFramebufferRenderbufferOES != nullptr) {
		frameBufferRenderBuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)glFramebufferRenderbufferOES;
	} else if (glFramebufferRenderbufferEXT != nullptr) {
		frameBufferRenderBuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)glFramebufferRenderbufferEXT;
	}

	if (glFramebufferTexture2D != nullptr) {
		framebufferTexture2D = glFramebufferTexture2D;
	} else if (glFramebufferTexture2DOES != nullptr) {
		framebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)glFramebufferTexture2DOES;
	} else if (glFramebufferTexture2DEXT != nullptr) {
		framebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)glFramebufferTexture2DEXT;
	}

	// =========================================
	// VBOs
	// =========================================
	if (glGenBuffers != nullptr) {
		genBuffers = glGenBuffers;
	} else if (glGenBuffersARB != nullptr) {
		genBuffers = (PFNGLGENBUFFERSPROC)glGenBuffersARB;
	}

	if (glBindBuffer != nullptr) {
		bindBuffer = glBindBuffer;
	} else if (glBindBufferARB != nullptr) {
		bindBuffer = (PFNGLBINDBUFFERPROC)glBindBufferARB;
	}

	if (glBufferData != nullptr) {
		bufferData = glBufferData;
	} else if (glBufferDataARB != nullptr) {
		bufferData = (PFNGLBUFFERDATAPROC)glBufferDataARB;
	}

	if (glBufferSubData != nullptr) {
		bufferSubData = glBufferSubData;
	} else if (glBufferSubDataARB != nullptr) {
		bufferSubData = (PFNGLBUFFERSUBDATAPROC)glBufferSubDataARB;
	}

	if (glDeleteBuffers != nullptr) {
		deleteBuffers = glDeleteBuffers;
	} else if (glDeleteBuffersARB != nullptr) {
		deleteBuffers = (PFNGLDELETEBUFFERSPROC)glDeleteBuffersARB;
	}

	if (glIsBuffer != nullptr) {
		isBuffer = glIsBuffer;
	} else if (glIsBufferARB != nullptr) {
		isBuffer = (PFNGLISBUFFERPROC)glIsBufferARB;
	}

	// =========================================
	// Multitexture
	// =========================================
	if (glActiveTexture != nullptr) {
		activeTexture = glActiveTexture;
	} else if (glActiveTextureARB != nullptr) {
		activeTexture = (PFNGLACTIVETEXTUREPROC)glActiveTextureARB;
	}

	if (glClientActiveTexture != nullptr) {
		clientActiveTexture = glClientActiveTexture;
	} else if (glClientActiveTextureARB != nullptr) {
		clientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)glClientActiveTextureARB;
	}
#endif
}

} //namespace GLES1