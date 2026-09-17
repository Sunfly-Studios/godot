/**************************************************************************/
/*  function_router_gles1.cpp                                             */
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

#include "function_router_gles1.h"
#include "rasterizer_gles1.h"

#ifdef GLES1_ENABLED

using namespace GLES1;

void Router::clear_depth(float p_depth) {
#ifdef GL_API_ENABLED
	if (RasterizerGLES1::is_gles_over_gl()) {
		glClearDepth(p_depth);
		GL_CHECK_ERROR("GLES1::RasterizerGLES1::glClearDepth");
	}
#endif // GL_API_ENABLED
#ifdef GLES_API_ENABLED
	if (!RasterizerGLES1::is_gles_over_gl()) {
		glClearDepthf(p_depth);
		GL_CHECK_ERROR("GLES1::RasterizerGLES1::glClearDepthf");
	}
#endif // GLES_API_ENABLED
}

void Router::clip_plane(GLenum plane_enum, const GLfloat *plane_eqs) {
#ifdef GL_API_ENABLED
	if (RasterizerGLES1::is_gles_over_gl()) {
		// In desktop, for some reason glClipPlane wants double values
		// while the spec only specifies float values.
		// Must be converted manually.

		// OpenGL clip planes always have 4 coefficients
		constexpr int SIZE = 4;
		GLdouble *double_ptr = SAFE_ALLOCA_ARRAY(GLdouble, SIZE);
		if (!double_ptr) {
			return;
		}

		for (int i = 0; i < SIZE; i++) {
			double_ptr[i] = static_cast<GLdouble>(plane_eqs[i]);
		}

		glClipPlane(plane_enum, double_ptr);
		GL_CHECK_ERROR("GLES1::RasterizerGLES1::glClearDepth");
	}
#endif // GL_API_ENABLED
#ifdef GLES_API_ENABLED
	if (!RasterizerGLES1::is_gles_over_gl()) {
		glClipPlanef(plane_enum, plane_eqs);
		GL_CHECK_ERROR("GLES1::RasterizerGLES1::glClearDepthf");
	}
#endif // GLES_API_ENABLED
}

#endif // GLES1_ENABLED
