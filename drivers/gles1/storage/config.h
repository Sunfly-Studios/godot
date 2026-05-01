/**************************************************************************/
/*  config.h                                                              */
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

#ifndef CONFIG_GLES1_H
#define CONFIG_GLES1_H

#ifdef GLES1_ENABLED

#include "core/config/project_settings.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

#include "platform_gl.h"

#ifdef ANDROID_ENABLED
typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
typedef void (*PFNGLTEXSTORAGE3DMULTISAMPLEPROC)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei);
typedef void (*PFNEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum, void *);
#endif

namespace GLES1 {

class Config {
private:
	static Config *singleton;

	// Probing algorithms
	void _flush_gl_errors();
	bool _probe_texture_parameterf(GLenum p_target, GLenum p_pname, GLfloat p_param);
	bool _probe_texture_parameteri(GLenum p_target, GLenum p_pname, GLint p_param);
	bool _probe_texture_envi(GLenum p_target, GLenum p_pname, GLint p_param);

public:
	// General Settings
	bool use_nearest_mip_filter = false;
	bool use_depth_prepass = true;
	bool generate_wireframes = false;
	bool force_vertex_shading = false;
	bool multi_bounce_occlusion = false;
	bool support_npot_repeat_mipmap = false;
	bool srgb_framebuffer_supported = false; // GL_ARB_framebuffer_sRGB

	// Limits
	GLint max_texture_units = 0;
	GLint max_texture_size = 0;
	GLint max_viewport_size[2] = { 0, 0 };
	GLint max_lights = 0;
	GLint max_clip_planes = 0;
	GLint max_modelview_stack_depth = 0;
	GLint max_projection_stack_depth = 0;
	GLint max_texture_stack_depth = 0;
	GLint max_palette_matrices = 0; // For GL_OES_matrix_palette
	GLint max_vertex_units = 0; // For GL_OES_matrix_palette
	GLint max_texture_image_units = 0;
	GLint max_uniform_buffer_size = 0; // Always 0.

	// Limits for Godot
	int64_t max_renderable_elements = 0;
	int64_t max_renderable_lights = 0;
	int64_t max_lights_per_object = 0;
	float anisotropic_level = 0.0f;

	// Extension Tracking
	HashSet<String> extensions;

	// Extensions
	bool support_fbo = false; // GL_OES_framebuffer_object
	bool support_vbo = false; // GL_ARB_vertex_buffer_object
	bool support_npot = false; // GL_OES_texture_npot
	bool support_blend_func_separate = false; // GL_OES_blend_func_separate
	bool support_texture_env_combine = false; // GL_OES_texture_env_crossbar
	bool support_point_sprite = false; // GL_OES_point_sprite
	bool support_matrix_palette = false; // GL_OES_matrix_palette
	bool support_draw_texture = false; // GL_OES_draw_texture
	bool support_cubemap = false; // GL_OES_texture_cube_map
	bool support_generate_mipmap = false; // GL_OES_generate_mipmap
	bool support_depth24 = false; // GL_OES_depth24
	bool support_depth32 = false; // GL_OES_depth32
	bool support_packed_depth_stencil = false; // GL_OES_packed_depth_stencil
	bool support_blend_subtract = false; // GL_OES_blend_subtract
	bool support_blend_equation_separate = false; // GL_OES_blend_equation_separate
	bool support_mirrored_repeat = false; // GL_OES_texture_mirrored_repeat
	bool support_anisotropic_filter = false; // GL_EXT_texture_filter_anisotropic
	bool support_mapbuffer = false; // GL_OES_mapbuffer

	// 3D
	bool support_vao = false; // GL_OES_vertex_array_object
	bool support_vertex_half_float = false; // GL_OES_vertex_half_float
	bool support_texture_env_add = false; // GL_OES_texture_env_add / GL_EXT_texture_env_add
	bool support_texture_env_dot3 = false; // GL_OES_texture_env_dot3 / GL_EXT_texture_env_dot3
	bool support_point_size_array = false; // GL_OES_point_size_array

	// Texture Formats
	bool float_texture_supported = false; // GL_OES_texture_float
	bool support_32_bits_indices = false; // GL_OES_element_index_uint
	bool support_3d_textures = false; // GL_OES_texture_3D
	
	// Compression Formats
	bool etc1_supported = false; // GL_OES_compressed_ETC1_RGB8_texture
	bool pvrtc_supported = false; // GL_IMG_texture_compression_pvrtc
	bool s3tc_supported = false;
	bool bptc_supported = false;
	bool rgtc_supported = false;

	// Compatibility / Workarounds
	bool adreno_3xx_compatibility = false;
	bool disable_particles_workaround = false;
	bool disable_transform_feedback_shader_cache = false;
	bool polyfill_half2float = true;

	// Flag for older, buggy Android emulators
	// (Nexus 5 API 19 and similar).
	bool is_android_emulator = false;

#ifdef ANDROID_ENABLED
	PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC eglFramebufferTextureMultiviewOVR = nullptr;
	PFNGLTEXSTORAGE3DMULTISAMPLEPROC eglTexStorage3DMultisample = nullptr;
	PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC eglFramebufferTexture2DMultisampleEXT = nullptr;
	PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC eglFramebufferTextureMultisampleMultiviewOVR = nullptr;
	PFNEGLIMAGETARGETTEXTURE2DOESPROC eglEGLImageTargetTexture2DOES = nullptr;
#endif

	static Config *get_singleton() { return singleton; }

	Config();
	~Config();
};

} // namespace GLES1

#endif // GLES1_ENABLED

#endif // CONFIG_GLES1_H
