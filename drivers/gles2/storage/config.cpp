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

#ifdef GLES2_ENABLED

#include "config.h"

#include "drivers/gles2/rasterizer_gles2.h"
#include "drivers/gles_common/error_macros.h"

#ifdef WEB_ENABLED
#include <emscripten/html5_webgl.h>
#endif

using namespace GLES2;

#define _GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF

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
	GL_CHECK_ERROR("GLES2::Config::setup: glGetString(GL_EXTENSIONS)");
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

	// Base extensions
	bptc_supported = extensions.has("GL_ARB_texture_compression_bptc") || extensions.has("EXT_texture_compression_bptc");
	astc_supported = extensions.has("GL_KHR_texture_compression_astc") || extensions.has("GL_OES_texture_compression_astc") || extensions.has("GL_KHR_texture_compression_astc_ldr") || extensions.has("GL_KHR_texture_compression_astc_hdr");
	astc_hdr_supported = extensions.has("GL_KHR_texture_compression_astc_hdr");
	astc_layered_supported = extensions.has("GL_KHR_texture_compression_astc_sliced_3d");
	rgtc_supported = extensions.has("GL_EXT_texture_compression_rgtc") || extensions.has("GL_ARB_texture_compression_rgtc") || extensions.has("EXT_texture_compression_rgtc");
	support_instancing = extensions.has("GL_EXT_draw_instanced") || extensions.has("GL_ANGLE_instanced_arrays") || extensions.has("GL_ARB_draw_instanced");
	support_frag_depth = extensions.has("GL_EXT_frag_depth");
	texture_lod_supported = extensions.has("GL_EXT_shader_texture_lod");

	// More extensions
	support_vao = extensions.has("GL_OES_vertex_array_object") || extensions.has("GL_ARB_vertex_array_object");
	support_vertex_half_float = extensions.has("GL_OES_vertex_half_float");
	support_texture_half_float = extensions.has("GL_OES_texture_half_float");
	support_depth24 = extensions.has("GL_OES_depth24");
	support_depth32 = extensions.has("GL_OES_depth32");
	support_packed_depth_stencil = extensions.has("GL_OES_packed_depth_stencil");
	support_blend_equation_separate = extensions.has("GL_OES_blend_equation_separate");
	support_draw_buffers = extensions.has("GL_EXT_draw_buffers");
	support_texture_rg = extensions.has("GL_EXT_texture_rg");
	external_texture_supported = extensions.has("GL_OES_EGL_image_external");
	support_transform_feedback = extensions.has("GL_EXT_transform_feedback") || extensions.has("GL_NV_transform_feedback") || extensions.has("GL_ARB_transform_feedback2");
	support_mapbuffer = extensions.has("GL_OES_mapbuffer") || extensions.has("GL_NV_copy_buffer");

	if (RasterizerGLES2::is_gles_over_gl()) {
		float_texture_supported = true;
		etc2_supported = false;
		s3tc_supported = true;
		support_npot_repeat_mipmap = true;
		support_32_bits_indices = true;
		texture_lod_supported = true;

		// Desktop OpenGL almost universally supports these
		support_vao = true;
		support_depth24 = true;
		support_packed_depth_stencil = true;
		support_blend_equation_separate = true;
		support_draw_buffers = true;
		support_mapbuffer = true;
	} else {
		float_texture_supported = extensions.has("GL_OES_texture_float") || extensions.has("GL_EXT_color_buffer_float");
		etc2_supported = true;
#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED)
		// Some Android devices report support for S3TC but we don't expect that
		// and don't export the textures.
		s3tc_supported = false;
#else
		s3tc_supported = extensions.has("GL_EXT_texture_compression_dxt1") || extensions.has("GL_EXT_texture_compression_s3tc") || extensions.has("WEBGL_compressed_texture_s3tc");
#endif
		support_npot_repeat_mipmap = extensions.has("GL_OES_texture_npot");
		support_32_bits_indices = extensions.has("GL_OES_element_index_uint");
	}

	GLint result = 0;
	GLint result_2[2] = { 0, 0 };

	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &result);
	max_vertex_texture_image_units = (result >= 0) ? result : 0;

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &result);
	max_texture_image_units = (result >= 8) ? result : 8;

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &result);
	max_texture_size = (result >= 64) ? result : 2048;

	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, result_2);
	max_viewport_size[0] = (result_2[0] >= 64) ? result_2[0] : 2048;
	max_viewport_size[1] = (result_2[1] >= 64) ? result_2[1] : 2048;

	GL_CHECK_ERROR("GLES2::Config::setup: Base glGetIntegerv limits");

	// GLES2 does not support UBO
	max_uniform_buffer_size = 0;

	GLint max_varyings = 0;
	glGetIntegerv(GL_MAX_VARYING_VECTORS, &max_varyings);
	max_shader_varyings = (uint32_t)max_varyings;

	support_anisotropic_filter = extensions.has("GL_EXT_texture_filter_anisotropic");
	if (support_anisotropic_filter) {
		// Probe Anisotropic filter support to ensure
		// it doesn't crash despite the extension string
		if (_probe_texture_parameterf(GL_TEXTURE_2D, _GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f)) {
			glGetFloatv(_GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropic_level);
			GL_CHECK_ERROR("GLES2::Config::setup: glGetFloatv Anisotropic");
			anisotropic_level = MIN(float(1 << int(GLOBAL_GET("rendering/textures/default_filters/anisotropic_filtering_level"))), anisotropic_level);
		} else {
			support_anisotropic_filter = false;
			anisotropic_level = 1.0f;
		}
	}

	// Cache 3D texture support
	support_3d_textures = extensions.has("GL_OES_texture_3D");

	if (!support_3d_textures) {
		// Some drivers expose it under the ARB or
		// standard GL string if they are desktop wrappers
		support_3d_textures = extensions.has("GL_EXT_texture3D") || extensions.has("GL_ARB_texture3D");
	}

#ifdef WEB_ENABLED
	glGetIntegerv(GL_MAX_SAMPLES, &msaa_max_samples);
	msaa_supported = (msaa_max_samples > 0);
#else
	msaa_supported = extensions.has("GL_EXT_framebuffer_multisample");
#endif

	force_vertex_shading = GLOBAL_GET("rendering/shading/overrides/force_vertex_shading");
	use_nearest_mip_filter = GLOBAL_GET("rendering/textures/default_filters/use_nearest_mipmap_filter");

	max_renderable_elements = GLOBAL_GET("rendering/limits/opengl/max_renderable_elements");
	max_renderable_lights = GLOBAL_GET("rendering/limits/opengl/max_renderable_lights");
	max_lights_per_object = GLOBAL_GET("rendering/limits/opengl/max_lights_per_object");

	// Adreno 3xx Compatibility / Workarounds
	const GLubyte *renderer_str = glGetString(GL_RENDERER);
	GL_CHECK_ERROR("GLES2::Config::setup: glGetString(GL_RENDERER) for workarounds");

	const String rendering_device_name = renderer_str ? String::utf8((const char *)renderer_str) : String();
	if (rendering_device_name.left(13) == "Adreno (TM) 3") {
		// Adreno 3xx devices are notorious for breaking with complex shader paths
		adreno_3xx_compatibility = true;
	} else if (rendering_device_name.contains("PowerVR")) {
		disable_transform_feedback_shader_cache = true;
	}

	is_android_emulator = rendering_device_name.contains("Android Emulator");

	if (OS::get_singleton()->get_current_rendering_driver_name() == "opengl2_angle" || OS::get_singleton()->has_feature("web")) {
		polyfill_half2float = false;
	}
}

Config::~Config() {
	singleton = nullptr;
}

#endif // GLES2_ENABLED
