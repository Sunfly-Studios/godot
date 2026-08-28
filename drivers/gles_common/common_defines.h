/**************************************************************************/
/*  common_defines.h                                                      */
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

#ifndef COMMON_DEFINES_GLES_COMMON_H
#define COMMON_DEFINES_GLES_COMMON_H

// Config access

#define GLES1_CONFIG (GLES1::Config::get_singleton())
#define GLES2_CONFIG (GLES2::Config::get_singleton())
// The GLES3 driver is left alone

// Constants

#ifndef GL_MAP_READ_BIT_OES
#define GL_MAP_READ_BIT_OES GL_MAP_READ_BIT
#endif
#ifndef GL_MAP_WRITE_BIT_OES
#define GL_MAP_WRITE_BIT_OES GL_MAP_WRITE_BIT
#endif

#ifndef GL_HALF_FLOAT_OES
#define GL_HALF_FLOAT_OES 0x8D61
#endif

// Legacy Limits
#ifndef GL_MAX_TEXTURE_UNITS
#define GL_MAX_TEXTURE_UNITS 0x84E2
#endif
#ifndef GL_MAX_LIGHTS
#define GL_MAX_LIGHTS 0x0D31
#endif
#ifndef GL_MAX_CLIP_PLANES
#define GL_MAX_CLIP_PLANES 0x0D32
#endif
#ifndef GL_MAX_MODELVIEW_STACK_DEPTH
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0D36
#endif
#ifndef GL_MAX_PROJECTION_STACK_DEPTH
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0D38
#endif
#ifndef GL_MAX_TEXTURE_STACK_DEPTH
#define GL_MAX_TEXTURE_STACK_DEPTH 0x0D39
#endif
#ifndef GL_MAX_PALETTE_MATRICES_OES
#define GL_MAX_PALETTE_MATRICES_OES 0x8842
#endif
#ifndef GL_MAX_VERTEX_UNITS_OES
#define GL_MAX_VERTEX_UNITS_OES 0x86A4
#endif

#endif // COMMON_DEFINES_GLES_COMMON_H