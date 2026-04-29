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

#ifndef CONFIG_GLES2_H
#define CONFIG_GLES2_H

#ifdef GLES2_ENABLED

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

namespace GLES2 {

class Config {
private:
	static Config *singleton;

	// Probing algorithms
	void _flush_gl_errors();
	bool _probe_texture_parameterf(GLenum p_target, GLenum p_pname, GLfloat p_param);
	bool _probe_texture_parameteri(GLenum p_target, GLenum p_pname, GLint p_param);

public:
	bool use_nearest_mip_filter = false;
	bool use_depth_prepass = true;
	bool support_npot_repeat_mipmap = true;

	GLint max_vertex_texture_image_units = 0;
	GLint max_texture_image_units = 0;
	GLint max_texture_size = 0;
	GLint max_viewport_size[2] = { 0, 0 };
	GLint64 max_uniform_buffer_size = 0;
	uint32_t max_shader_varyings = 0;

	int64_t max_renderable_elements = 0;
	int64_t max_renderable_lights = 0;
	int64_t max_lights_per_object = 0;

	bool generate_wireframes = false;

	HashSet<String> extensions;

	// Texture Formats
	bool float_texture_supported = false;
	bool float_texture_linear_supported = false;
	bool support_3d_textures = false; // GL_OES_texture_3D
	bool support_texture_half_float = false; // GL_OES_texture_half_float
	bool support_texture_rg = false; // GL_EXT_texture_rg

	// Compression Formats
	bool s3tc_supported = false;
	bool rgtc_supported = false;
	bool bptc_supported = false;
	bool etc2_supported = false;
	bool astc_supported = false;
	bool astc_hdr_supported = false;
	bool astc_layered_supported = false;
	bool srgb_framebuffer_supported = false;

	// Overrides / Features
	bool force_vertex_shading = false;
	bool multi_bounce_occlusion = false;

	// Filtering
	bool support_anisotropic_filter = false;
	float anisotropic_level = 0.0f;

	// MSAA & Multiview
	GLint msaa_max_samples = 0;
	bool msaa_supported = false;
	bool msaa_multiview_supported = false;
	bool rt_msaa_supported = false;
	bool rt_msaa_multiview_supported = false;
	bool multiview_supported = false;

	// Extensions
	bool external_texture_supported = false; // GL_OES_EGL_image_external
	bool support_32_bits_indices = false; // GL_OES_element_index_uint
	bool support_instancing = false; // GL_EXT_draw_instanced / GL_ANGLE_instanced_arrays
	bool support_frag_depth = false; // GL_EXT_frag_depth
	bool texture_lod_supported = true; // GL_EXT_shader_texture_lod
	bool support_vao = false; // GL_OES_vertex_array_object
	bool support_vertex_half_float = false; // GL_OES_vertex_half_float
	bool support_depth24 = false; // GL_OES_depth24
	bool support_depth32 = false; // GL_OES_depth32
	bool support_packed_depth_stencil = false; // GL_OES_packed_depth_stencil
	bool support_blend_equation_separate = false; // GL_OES_blend_equation_separate
	bool support_draw_buffers = false; // GL_EXT_draw_buffers
	bool support_transform_feedback = false; // GL_EXT_transform_feedback
	bool support_mapbuffer = false; // GL_OES_mapbuffer

	// Compatibility / Workarounds
	bool adreno_3xx_compatibility = false;
	bool disable_particles_workaround = false; // Set to 'true' to disable 'GPUParticles'.
	bool disable_transform_feedback_shader_cache = false; // PowerVR GE 8320 workaround.
	bool polyfill_half2float = true; // ANGLE shader workaround.
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

} // namespace GLES2

#endif // GLES2_ENABLED

#endif // CONFIG_GLES2_H
