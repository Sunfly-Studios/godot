/**************************************************************************/
/*  error_macros.h                                                        */
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

#ifndef ERROR_MACROS_GLES_COMMON_H
#define ERROR_MACROS_GLES_COMMON_H

// Macros to safely check for OpenGL errors.
// These are stripped out in release builds to maintain performance.
#ifdef DEBUG_ENABLED

#define GL_CHECK_ERROR(msg) \
	{ \
		GLenum err = glGetError(); \
		if (unlikely(err != GL_NO_ERROR)) { \
			ERR_PRINT(vformat("GLES Error: 0x%04X at %s", err, msg)); \
		} \
	}

#define GL_CHECK_ERROR_V(msg, ret_val) \
	{ \
		GLenum err = glGetError(); \
		if (unlikely(err != GL_NO_ERROR)) { \
			ERR_PRINT(vformat("GLES Error: 0x%04X at %s", err, msg)); \
			return ret_val; \
		} \
	}

#define GL_CHECK_ERROR_CLEANUP_V(msg, return_val, cleanup_code) \
	{ \
		GLenum err = glGetError(); \
		if (unlikely(err != GL_NO_ERROR)) { \
			ERR_PRINT(vformat("GLES Error: 0x%04X at %s", err, msg)); \
			cleanup_code; \
			return return_val; \
		} \
	}

#else

#define GL_CHECK_ERROR(msg)
#define GL_CHECK_ERROR_V(msg, ret_val)
#define GL_CHECK_ERROR_CLEANUP_V(msg, return_val, cleanup_code)

#endif

#endif // ERROR_MACROS_GLES_COMMON_H