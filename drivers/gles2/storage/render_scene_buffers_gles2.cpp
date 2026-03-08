/**************************************************************************/
/*  render_scene_buffers_gles2.cpp                                        */
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

#ifdef GLES2_ENABLED

#include "render_scene_buffers_GLES2.h"
#include "config.h"
#include "texture_storage.h"
#include "utilities.h"

#ifdef ANDROID_ENABLED
#define glFramebufferTextureMultiviewOVR GLES2::Config::get_singleton()->eglFramebufferTextureMultiviewOVR
#define glTexStorage3DMultisample GLES2::Config::get_singleton()->eglTexStorage3DMultisample
#define glFramebufferTexture2DMultisampleEXT GLES2::Config::get_singleton()->eglFramebufferTexture2DMultisampleEXT
#define glFramebufferTextureMultisampleMultiviewOVR GLES2::Config::get_singleton()->eglFramebufferTextureMultisampleMultiviewOVR
#endif // ANDROID_ENABLED

// Will only be defined if GLES 3.2 headers are included
#ifndef GL_TEXTURE_2D_MULTISAMPLE_ARRAY
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#endif

RenderSceneBuffersGLES2::RenderSceneBuffersGLES2() {

}

RenderSceneBuffersGLES2::~RenderSceneBuffersGLES2() {

}

void RenderSceneBuffersGLES2::_rt_attach_textures(GLuint p_color, GLuint p_depth, GLsizei p_samples, uint32_t p_view_count, bool p_depth_has_stencil) {

}

GLuint RenderSceneBuffersGLES2::_rt_get_cached_fbo(GLuint p_color, GLuint p_depth, GLsizei p_samples, uint32_t p_view_count) {
	return 0;
}

void RenderSceneBuffersGLES2::configure(const RenderSceneBuffersConfiguration *p_config) {

}

void RenderSceneBuffersGLES2::_check_render_buffers() {

}

void RenderSceneBuffersGLES2::configure_for_probe(Size2i p_size) {

}

void RenderSceneBuffersGLES2::_clear_msaa3d_buffers() {

}

void RenderSceneBuffersGLES2::_clear_intermediate_buffers() {

}

void RenderSceneBuffersGLES2::check_backbuffer(bool p_need_color, bool p_need_depth) {

}

void RenderSceneBuffersGLES2::_clear_back_buffers() {

}

void RenderSceneBuffersGLES2::set_apply_color_adjustments_in_post(bool p_apply_in_post) {

}

void RenderSceneBuffersGLES2::check_glow_buffers() {

}

void RenderSceneBuffersGLES2::_clear_glow_buffers() {

}

void RenderSceneBuffersGLES2::free_render_buffer_data() {

}

GLuint RenderSceneBuffersGLES2::get_render_fbo() {
	return 0;
}

#endif // GLES2_ENABLED
