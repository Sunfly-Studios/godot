/**************************************************************************/
/*  render_scene_buffers_gles1.cpp                                        */
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

#include "render_scene_buffers_gles1.h"
#include "config.h"
#include "texture_storage.h"
#include "utilities.h"

RenderSceneBuffersGLES1::RenderSceneBuffersGLES1() {
	for (int i = 0; i < 4; i++) {
		glow.levels[i].color = 0;
		glow.levels[i].fbo = 0;
	}
}

RenderSceneBuffersGLES1::~RenderSceneBuffersGLES1() {
	free_render_buffer_data();
}



void RenderSceneBuffersGLES1::configure(const RenderSceneBuffersConfiguration *p_config) {
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();

	free_render_buffer_data();

	internal_size = p_config->get_internal_size();
	target_size = p_config->get_target_size();
	scaling_3d_mode = p_config->get_scaling_3d_mode();
	render_target = p_config->get_render_target();
	msaa3d.mode = p_config->get_msaa_3d();
	view_count = 1; // Explicitly restrict view_count to 1 (No Multiview / XR)

	if (render_target.is_valid()) {
		color_internal_format = texture_storage->render_target_get_color_internal_format(render_target);
		color_format = texture_storage->render_target_get_color_format(render_target);
		color_type = texture_storage->render_target_get_color_type(render_target);
		color_format_size = texture_storage->render_target_get_color_format_size(render_target);
	} else {
		color_internal_format = GL_RGBA;
		color_format = GL_RGBA;
		color_type = GL_UNSIGNED_BYTE;
		color_format_size = 4;
	}

	if (scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_OFF && internal_size.x == 0 && internal_size.y == 0) {
		scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_OFF;
	} else if (scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_OFF && internal_size == target_size) {
		scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_OFF;
	} else if (scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_OFF && scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_BILINEAR) {
		WARN_PRINT_ONCE("GLES1 only supports bilinear scaling.");
		scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_BILINEAR;
	}

	// GLES1 standard framebuffer MSAA is not natively supported without driver extensions
	if (msaa3d.mode != RS::VIEWPORT_MSAA_DISABLED) {
		WARN_PRINT_ONCE("MSAA is not supported on GLES1.");
		msaa3d.mode = RS::VIEWPORT_MSAA_DISABLED;
	}
}

void RenderSceneBuffersGLES1::_rt_attach_textures(GLuint p_color, GLuint p_depth, GLsizei p_samples, uint32_t p_view_count, bool p_depth_has_stencil) {
	glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, p_color, 0);
	GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::_rt_attach_textures: glFramebufferTexture2DOES color");

	GLenum depth_attachment = p_depth_has_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT_OES;
	glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, depth_attachment, GL_TEXTURE_2D, p_depth, 0);
	GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::_rt_attach_textures: glFramebufferTexture2DOES depth");
}

GLuint RenderSceneBuffersGLES1::_rt_get_cached_fbo(GLuint p_color, GLuint p_depth, GLsizei p_samples, uint32_t p_view_count) {
	if (!GLES1_CONFIG->support_fbo) {
		return 0;
	}

	FBDEF new_fbo;

	for (const FBDEF &cached_fbo : msaa3d.cached_fbos) {
		if (cached_fbo.color == p_color && cached_fbo.depth == p_depth) {
			return cached_fbo.fbo;
		}
	}

	new_fbo.color = p_color;
	new_fbo.depth = p_depth;

	glGenFramebuffersOES(1, &new_fbo.fbo);
	GLES1::TextureStorage::get_singleton()->bind_framebuffer(new_fbo.fbo);

	_rt_attach_textures(p_color, p_depth, p_samples, p_view_count, true);

	GLenum status = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
	if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
		WARN_PRINT("Could not create 3D framebuffer, status: " + GLES1::TextureStorage::get_singleton()->get_framebuffer_error(status));
		glDeleteFramebuffersOES(1, &new_fbo.fbo);
		new_fbo.fbo = 0;
	} else {
		msaa3d.cached_fbos.push_back(new_fbo);
	}
	GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::_rt_get_cached_fbo: framebuffer setup");

	GLES1::TextureStorage::get_singleton()->bind_framebuffer_system();

	return new_fbo.fbo;
}

void RenderSceneBuffersGLES1::_check_render_buffers() {
	if (!GLES1_CONFIG->support_fbo) {
		return;
	}
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();

	bool use_internal_buffer = scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_OFF || apply_color_adjustments_in_post;
	uint32_t depth_format_size = 2; // 16-bit depth component for standard GLES1

	if ((!use_internal_buffer || internal3d.color != 0)) {
		return;
	}

	if (use_internal_buffer && internal3d.color == 0) {
		GLenum texture_target = GL_TEXTURE_2D;

		// Color Buffer
		glGenTextures(1, &internal3d.color);
		glBindTexture(texture_target, internal3d.color);
		glTexImage2D(texture_target, 0, color_internal_format, internal_size.x, internal_size.y, 0, color_format, color_type, nullptr);
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::_check_render_buffers: color glTexImage2D");

		glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		GLES1::Utilities::get_singleton()->texture_allocated_data(internal3d.color, internal_size.x * internal_size.y * color_format_size, "3D color texture");

		// Depth Buffer
		glGenTextures(1, &internal3d.depth);
		glBindTexture(texture_target, internal3d.depth);
		glTexImage2D(texture_target, 0, GL_DEPTH_COMPONENT, internal_size.x, internal_size.y, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::_check_render_buffers: depth glTexImage2D");

		glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		GLES1::Utilities::get_singleton()->texture_allocated_data(internal3d.depth, internal_size.x * internal_size.y * depth_format_size, "3D depth texture");

		// Framebuffer
		glGenFramebuffersOES(1, &internal3d.fbo);
		GLES1::TextureStorage::get_singleton()->bind_framebuffer(internal3d.fbo);

		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, texture_target, internal3d.color, 0);
		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT, texture_target, internal3d.depth, 0);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_OES);
		if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
			_clear_intermediate_buffers();
			WARN_PRINT("Could not create 3D internal buffers, status: " + texture_storage->get_framebuffer_error(status));
		}
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::_check_render_buffers: framebuffer setup");

		glBindTexture(texture_target, 0);
		GLES1::TextureStorage::get_singleton()->bind_framebuffer_system();
	}
}

void RenderSceneBuffersGLES1::configure_for_probe(Size2i p_size) {
	internal_size = p_size;
	target_size = p_size;
	scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_OFF;
	view_count = 1;
}

void RenderSceneBuffersGLES1::_clear_msaa3d_buffers() {
	for (const FBDEF &cached_fbo : msaa3d.cached_fbos) {
		GLuint fbo = cached_fbo.fbo;
		glDeleteFramebuffersOES(1, &fbo);
	}
	msaa3d.cached_fbos.clear();

	if (msaa3d.fbo) {
		glDeleteFramebuffersOES(1, &msaa3d.fbo);
		msaa3d.fbo = 0;
	}

	if (msaa3d.color != 0) {
		GLES1::Utilities::get_singleton()->render_buffer_free_data(msaa3d.color);
		msaa3d.color = 0;
	}

	if (msaa3d.depth != 0) {
		GLES1::Utilities::get_singleton()->render_buffer_free_data(msaa3d.depth);
		msaa3d.depth = 0;
	}
}

void RenderSceneBuffersGLES1::_clear_intermediate_buffers() {
	if (internal3d.fbo) {
		glDeleteFramebuffersOES(1, &internal3d.fbo);
		internal3d.fbo = 0;
	}

	if (internal3d.color != 0) {
		GLES1::Utilities::get_singleton()->texture_free_data(internal3d.color);
		internal3d.color = 0;
	}

	if (internal3d.depth != 0) {
		GLES1::Utilities::get_singleton()->texture_free_data(internal3d.depth);
		internal3d.depth = 0;
	}
}

void RenderSceneBuffersGLES1::check_backbuffer(bool p_need_color, bool p_need_depth) {
	if (!GLES1_CONFIG->support_fbo) {
		return;
	}
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();

	if (backbuffer3d.fbo == 0) {
		glGenFramebuffersOES(1, &backbuffer3d.fbo);
	}

	GLES1::TextureStorage::get_singleton()->bind_framebuffer(backbuffer3d.fbo);

	GLenum texture_target = GL_TEXTURE_2D;
	uint32_t depth_format_size = 2;

	if (backbuffer3d.color == 0 && p_need_color) {
		glGenTextures(1, &backbuffer3d.color);
		glBindTexture(texture_target, backbuffer3d.color);

		glTexImage2D(texture_target, 0, color_internal_format, internal_size.x, internal_size.y, 0, color_format, color_type, nullptr);
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::check_backbuffer: color glTexImage2D");

		glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		GLES1::Utilities::get_singleton()->texture_allocated_data(backbuffer3d.color, internal_size.x * internal_size.y * color_format_size, "3D Back buffer color texture");

		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, texture_target, backbuffer3d.color, 0);
	}

	if (backbuffer3d.depth == 0 && p_need_depth) {
		glGenTextures(1, &backbuffer3d.depth);
		glBindTexture(texture_target, backbuffer3d.depth);

		glTexImage2D(texture_target, 0, GL_DEPTH_COMPONENT, internal_size.x, internal_size.y, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::check_backbuffer: depth glTexImage2D");

		glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		GLES1::Utilities::get_singleton()->texture_allocated_data(backbuffer3d.depth, internal_size.x * internal_size.y * depth_format_size, "3D back buffer depth texture");

		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, texture_target, backbuffer3d.depth, 0);
	}

	GLenum status = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
	if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
		_clear_back_buffers();
		WARN_PRINT("Could not create 3D back buffers, status: " + texture_storage->get_framebuffer_error(status));
	}
	GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::check_backbuffer: framebuffer setup");

	glBindTexture(texture_target, 0);
	GLES1::TextureStorage::get_singleton()->bind_framebuffer_system();
}

void RenderSceneBuffersGLES1::_clear_back_buffers() {
	if (backbuffer3d.fbo) {
		glDeleteFramebuffersOES(1, &backbuffer3d.fbo);
		backbuffer3d.fbo = 0;
	}

	if (backbuffer3d.color != 0) {
		GLES1::Utilities::get_singleton()->texture_free_data(backbuffer3d.color);
		backbuffer3d.color = 0;
	}

	if (backbuffer3d.depth != 0) {
		GLES1::Utilities::get_singleton()->texture_free_data(backbuffer3d.depth);
		backbuffer3d.depth = 0;
	}
}

void RenderSceneBuffersGLES1::set_apply_color_adjustments_in_post(bool p_apply_in_post) {
	apply_color_adjustments_in_post = p_apply_in_post;
}

void RenderSceneBuffersGLES1::check_glow_buffers() {
	if (!GLES1_CONFIG->support_fbo) {
		return;
	}

	if (glow.levels[0].color != 0) {
		return;
	}

	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	Size2i level_size = internal_size;
	for (int i = 0; i < 4; i++) {
		level_size = Size2i(level_size.x >> 1, level_size.y >> 1).maxi(4);

		glow.levels[i].size = level_size;

		glGenTextures(1, &glow.levels[i].color);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, glow.levels[i].color);

		glTexImage2D(GL_TEXTURE_2D, 0, color_internal_format, level_size.x, level_size.y, 0, color_format, color_type, nullptr);
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::check_glow_buffers: glTexImage2D");

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		GLES1::Utilities::get_singleton()->texture_allocated_data(glow.levels[i].color, level_size.x * level_size.y * color_format_size, String("Glow buffer ") + String::num_int64(i));

		glGenFramebuffersOES(1, &glow.levels[i].fbo);
		GLES1::TextureStorage::get_singleton()->bind_framebuffer(glow.levels[i].fbo);

		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, glow.levels[i].color, 0);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_OES);
		if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
			WARN_PRINT("Could not create glow buffers, status: " + texture_storage->get_framebuffer_error(status));
			_clear_glow_buffers();
			break;
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	GLES1::TextureStorage::get_singleton()->bind_framebuffer_system();
	GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::check_glow_buffers: reset state");
}

void RenderSceneBuffersGLES1::_clear_glow_buffers() {
	for (int i = 0; i < 4; i++) {
		if (glow.levels[i].fbo != 0) {
			glDeleteFramebuffersOES(1, &glow.levels[i].fbo);
			glow.levels[i].fbo = 0;
		}

		if (glow.levels[i].color != 0) {
			GLES1::Utilities::get_singleton()->texture_free_data(glow.levels[i].color);
			glow.levels[i].color = 0;
		}
	}
}

void RenderSceneBuffersGLES1::free_render_buffer_data() {
	_clear_msaa3d_buffers();
	_clear_intermediate_buffers();
	_clear_back_buffers();
	_clear_glow_buffers();
}

GLuint RenderSceneBuffersGLES1::get_render_fbo() {
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	GLuint rt_fbo = 0;

	_check_render_buffers();

	if (internal3d.fbo != 0) {
		return internal3d.fbo;
	} else {
		rt_fbo = texture_storage->render_target_get_fbo(render_target);
	}

	if (texture_storage->render_target_is_reattach_textures(render_target)) {
		GLuint color = texture_storage->render_target_get_color(render_target);
		GLuint depth = texture_storage->render_target_get_depth(render_target);
		bool depth_has_stencil = texture_storage->render_target_get_depth_has_stencil(render_target);

		GLES1::TextureStorage::get_singleton()->bind_framebuffer(rt_fbo);
		_rt_attach_textures(color, depth, 1, 1, depth_has_stencil);
		GLES1::TextureStorage::get_singleton()->bind_framebuffer_system();
		GL_CHECK_ERROR("GLES1::RenderSceneBuffersGLES1::get_render_fbo: reattach textures");
	}

	return rt_fbo;
}

#endif // GLES1_ENABLED
