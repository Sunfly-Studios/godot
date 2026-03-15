/**************************************************************************/
/*  config.cpp                                                            */
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

#include "config.h"

#include "drivers/gles1/rasterizer_gles1.h"
#include "drivers/gles_common/error_macros.h"

#ifdef WEB_ENABLED
#include <emscripten/html5_webgl.h>
#endif

using namespace GLES1;

#define _GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

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

Config *Config::singleton = nullptr;

void Config::_flush_gl_errors() {
	while (glGetError() != GL_NO_ERROR) {
		// Flush the error state
	}
}

bool Config::_probe_texture_parameterf(GLenum p_target, GLenum p_pname, GLfloat p_param) {
	_flush_gl_errors();

	GLuint dummy_tex;
	glGenTextures(1, &dummy_tex);
	glBindTexture(p_target, dummy_tex);

	glTexParameterf(p_target, p_pname, p_param);
	bool supported = (glGetError() == GL_NO_ERROR);

	glBindTexture(p_target, 0);
	glDeleteTextures(1, &dummy_tex);

	_flush_gl_errors();
	return supported;
}

bool Config::_probe_texture_parameteri(GLenum p_target, GLenum p_pname, GLint p_param) {
	_flush_gl_errors();

	GLuint dummy_tex;
	glGenTextures(1, &dummy_tex);
	glBindTexture(p_target, dummy_tex);

	glTexParameteri(p_target, p_pname, p_param);
	bool supported = (glGetError() == GL_NO_ERROR);

	glBindTexture(p_target, 0);
	glDeleteTextures(1, &dummy_tex);

	_flush_gl_errors();
	return supported;
}

bool Config::_probe_texture_envi(GLenum p_target, GLenum p_pname, GLint p_param) {
	_flush_gl_errors();

	glTexEnvi(p_target, p_pname, p_param);
	bool supported = (glGetError() == GL_NO_ERROR);

	_flush_gl_errors();
	return supported;
}

Config::Config() {
	singleton = this;

#ifdef WEB_ENABLED
	{
		char *extension_array_string = emscripten_webgl_get_supported_extensions();
		PackedStringArray extension_array = String((const char *)extension_array_string).split(" ");
		extensions.reserve(extension_array.size() * 2);
		for (const String &s : extension_array) {
			extensions.insert(s);
			extensions.insert("GL_" + s);
		}
		free(extension_array_string);
	}
#else
	const GLubyte *extension_string = glGetString(GL_EXTENSIONS);
	GL_CHECK_ERROR("GLES1::Config::setup: glGetString(GL_EXTENSIONS)");
	if (extension_string != nullptr) {
		const GLubyte *start = extension_string;
		const GLubyte *end = extension_string + strlen((const char *)extension_string);
		const GLubyte *current = start;

		while (current < end) {
			if (*current == ' ' || *current == '\0') {
				extensions.insert(String((const char *)start, (int)(current - start)));
				start = current + 1;
			}
			current++;
		}
	}
#endif

	// Fixed-Function / Context Limitations
	GLint result = 0;
	GLint result_2[2] = { 0, 0 };

	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &result);
	max_texture_units = (result >= 2) ? result : 2; // Minimum 2 texture unit required by spec
	max_texture_image_units = max_texture_units;

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &result);
	max_texture_size = (result >= 64) ? result : 1024;

	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, result_2);
	max_viewport_size[0] = (result_2[0] >= 64) ? result_2[0] : 1024;
	max_viewport_size[1] = (result_2[1] >= 64) ? result_2[1] : 1024;

	glGetIntegerv(GL_MAX_LIGHTS, &result);
	max_lights = (result >= 0) ? result : 8; // Usually 8 in GLES 1.1

	glGetIntegerv(GL_MAX_CLIP_PLANES, &result);
	max_clip_planes = (result >= 0) ? result : 6;

	glGetIntegerv(GL_MAX_MODELVIEW_STACK_DEPTH, &result);
	max_modelview_stack_depth = (result >= 0) ? result : 16;

	glGetIntegerv(GL_MAX_PROJECTION_STACK_DEPTH, &result);
	max_projection_stack_depth = (result >= 0) ? result : 2;

	glGetIntegerv(GL_MAX_TEXTURE_STACK_DEPTH, &result);
	max_texture_stack_depth = (result >= 0) ? result : 2;

	GL_CHECK_ERROR("GLES1::Config::setup: Base glGetIntegerv limits");

	// GLES1 doesn't support UBOs
	max_uniform_buffer_size = 0;

	// Extensions
	support_fbo = extensions.has("GL_OES_framebuffer_object") || extensions.has("GL_EXT_framebuffer_object");
	support_npot = extensions.has("GL_OES_texture_npot") || extensions.has("GL_ARB_texture_non_power_of_two");
	support_blend_func_separate = extensions.has("GL_OES_blend_func_separate") || extensions.has("GL_EXT_blend_func_separate");
	support_point_sprite = extensions.has("GL_OES_point_sprite") || extensions.has("GL_ARB_point_sprite");
	support_matrix_palette = extensions.has("GL_OES_matrix_palette");
	support_draw_texture = extensions.has("GL_OES_draw_texture");
	support_cubemap = extensions.has("GL_OES_texture_cube_map");
	support_generate_mipmap = extensions.has("GL_OES_generate_mipmap");
	support_depth24 = extensions.has("GL_OES_depth24");
	support_depth32 = extensions.has("GL_OES_depth32");
	support_packed_depth_stencil = extensions.has("GL_OES_packed_depth_stencil");
	support_blend_subtract = extensions.has("GL_OES_blend_subtract");
	support_blend_equation_separate = extensions.has("GL_OES_blend_equation_separate");
	support_mirrored_repeat = extensions.has("GL_OES_texture_mirrored_repeat");
	support_32_bits_indices = extensions.has("GL_OES_element_index_uint");
	support_npot_repeat_mipmap = extensions.has("GL_OES_texture_npot");

	// 3D
	support_vao = extensions.has("GL_OES_vertex_array_object");
	support_vertex_half_float = extensions.has("GL_OES_vertex_half_float");
	support_texture_env_add = extensions.has("GL_OES_texture_env_add") || extensions.has("GL_EXT_texture_env_add");
	support_texture_env_dot3 = extensions.has("GL_OES_texture_env_dot3") || extensions.has("GL_EXT_texture_env_dot3");
	support_point_size_array = extensions.has("GL_OES_point_size_array");

	// Just because the extension exists doesn't
	// mean the GLES1 wrapper accepts GL_COMBINE.
	support_texture_env_combine = extensions.has("GL_OES_texture_env_crossbar") || extensions.has("GL_ARB_texture_env_combine") || extensions.has("GL_EXT_texture_env_combine");
	if (support_texture_env_combine) {
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif
		if (!_probe_texture_envi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE)) {
			support_texture_env_combine = false;
		}
	}

	support_anisotropic_filter = extensions.has("GL_EXT_texture_filter_anisotropic");
	if (support_anisotropic_filter) {
		if (_probe_texture_parameterf(GL_TEXTURE_2D, _GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f)) {
			glGetFloatv(_GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropic_level);
			GL_CHECK_ERROR("GLES1::Config::setup: glGetFloatv Anisotropic");
			anisotropic_level = MIN(float(1 << int(GLOBAL_GET("rendering/textures/default_filters/anisotropic_filtering_level"))), anisotropic_level);
		} else {
			support_anisotropic_filter = false;
			anisotropic_level = 1.0f;
		}
	}

	if (RasterizerGLES1::is_gles_over_gl()) {
		// Emulating GLES1 over desktop GL
		float_texture_supported = true;
		etc1_supported = false;
		s3tc_supported = true;
		support_3d_textures = extensions.has("GL_EXT_texture3D") || extensions.has("GL_ARB_texture3D");

		// Desktop OpenGL almost universally supports these
		support_fbo = true;
		support_npot = true;
		support_blend_func_separate = true;
		support_texture_env_combine = true;
		support_point_sprite = true;
		support_cubemap = true;
		support_generate_mipmap = true;
		support_32_bits_indices = true;
		support_npot_repeat_mipmap = true;
		srgb_framebuffer_supported = true;
	} else {
		float_texture_supported = extensions.has("GL_OES_texture_float") || extensions.has("GL_EXT_color_buffer_float");
		etc1_supported = extensions.has("GL_OES_compressed_ETC1_RGB8_texture");
		pvrtc_supported = extensions.has("GL_IMG_texture_compression_pvrtc");
		support_3d_textures = extensions.has("GL_OES_texture_3D");

#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED)
		s3tc_supported = false; // Usually not on mobile
#else
		s3tc_supported = extensions.has("GL_EXT_texture_compression_dxt1") || extensions.has("GL_EXT_texture_compression_s3tc");
#endif
		bptc_supported = extensions.has("GL_ARB_texture_compression_bptc") || extensions.has("EXT_texture_compression_bptc");
		rgtc_supported = extensions.has("GL_EXT_texture_compression_rgtc") || extensions.has("GL_ARB_texture_compression_rgtc");
	}

	// Project Settings
	force_vertex_shading = GLOBAL_GET("rendering/shading/overrides/force_vertex_shading");
	use_nearest_mip_filter = GLOBAL_GET("rendering/textures/default_filters/use_nearest_mipmap_filter");

	max_renderable_elements = GLOBAL_GET("rendering/limits/opengl/max_renderable_elements");
	max_renderable_lights = GLOBAL_GET("rendering/limits/opengl/max_renderable_lights");
	max_lights_per_object = GLOBAL_GET("rendering/limits/opengl/max_lights_per_object");

	// Workarounds
	const GLubyte *renderer_str = glGetString(GL_RENDERER);
	GL_CHECK_ERROR("GLES1::Config::setup: glGetString(GL_RENDERER) for workarounds");

	const String rendering_device_name = renderer_str ? String::utf8((const char *)renderer_str) : String();
	if (rendering_device_name.left(13) == "Adreno (TM) 3") {
		adreno_3xx_compatibility = true;
	}

	is_android_emulator = rendering_device_name.contains("Android Emulator");

	if (OS::get_singleton()->get_current_rendering_driver_name() == "opengl1_angle" || OS::get_singleton()->has_feature("web")) {
		polyfill_half2float = false;
	}
}

Config::~Config() {
	singleton = nullptr;
}

#endif // GLES1_ENABLED
