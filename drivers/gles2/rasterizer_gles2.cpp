/**************************************************************************/
/*  rasterizer_gles2.cpp                                                  */
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

#include "rasterizer_gles2.h"
#include "storage/utilities.h"

#ifdef GLES2_ENABLED

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/image.h"
#include "core/os/os.h"
#include "storage/texture_storage.h"

#define _EXT_DEBUG_OUTPUT_SYNCHRONOUS_ARB 0x8242
#define _EXT_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_ARB 0x8243
#define _EXT_DEBUG_CALLBACK_FUNCTION_ARB 0x8244
#define _EXT_DEBUG_CALLBACK_USER_PARAM_ARB 0x8245
#define _EXT_DEBUG_SOURCE_API_ARB 0x8246
#define _EXT_DEBUG_SOURCE_WINDOW_SYSTEM_ARB 0x8247
#define _EXT_DEBUG_SOURCE_SHADER_COMPILER_ARB 0x8248
#define _EXT_DEBUG_SOURCE_THIRD_PARTY_ARB 0x8249
#define _EXT_DEBUG_SOURCE_APPLICATION_ARB 0x824A
#define _EXT_DEBUG_SOURCE_OTHER_ARB 0x824B
#define _EXT_DEBUG_TYPE_ERROR_ARB 0x824C
#define _EXT_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB 0x824D
#define _EXT_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB 0x824E
#define _EXT_DEBUG_TYPE_PORTABILITY_ARB 0x824F
#define _EXT_DEBUG_TYPE_PERFORMANCE_ARB 0x8250
#define _EXT_DEBUG_TYPE_OTHER_ARB 0x8251
#define _EXT_MAX_DEBUG_MESSAGE_LENGTH_ARB 0x9143
#define _EXT_MAX_DEBUG_LOGGED_MESSAGES_ARB 0x9144
#define _EXT_DEBUG_LOGGED_MESSAGES_ARB 0x9145
#define _EXT_DEBUG_SEVERITY_HIGH_ARB 0x9146
#define _EXT_DEBUG_SEVERITY_MEDIUM_ARB 0x9147
#define _EXT_DEBUG_SEVERITY_LOW_ARB 0x9148
#define _EXT_DEBUG_OUTPUT 0x92E0

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

#ifndef GLAPIENTRY
#if defined(WINDOWS_ENABLED)
#define GLAPIENTRY APIENTRY
#else
#define GLAPIENTRY
#endif
#endif

#if !defined(IOS_ENABLED) && !defined(WEB_ENABLED)
// We include EGL below to get debug callback on GLES2 platforms,
// but EGL is not available on iOS or the web.
#define CAN_DEBUG
#endif

#include "platform_gl.h"

#if defined(MINGW_ENABLED) || defined(_MSC_VER)
#define strcpy strcpy_s
#endif

#ifdef WINDOWS_ENABLED
bool RasterizerGLES2::screen_flipped_y = false;
#endif

bool RasterizerGLES2::gles_over_gl = true;

void RasterizerGLES2::begin_frame(double frame_step) {
	frame++;
	delta = frame_step;

	time_total += frame_step;

	double time_roll_over = GLOBAL_GET("rendering/limits/time/time_rollover_secs");
	time_total = Math::fmod(time_total, time_roll_over);

	canvas->set_time(time_total);
	scene->set_time(time_total, frame_step);

	GLES2::Utilities *utils = GLES2::Utilities::get_singleton();
	utils->_capture_timestamps_begin();

	//scene->iteration();
}

void RasterizerGLES2::end_frame(bool p_swap_buffers) {
	GLES2::Utilities *utils = GLES2::Utilities::get_singleton();
	utils->capture_timestamps_end();
}

void RasterizerGLES2::gl_end_frame(bool p_swap_buffers) {
	if (p_swap_buffers) {
		DisplayServer::get_singleton()->swap_buffers();
	} else {
		glFinish();
	}
}

void RasterizerGLES2::clear_depth(float p_depth) {
#ifdef GL_API_ENABLED
	if (is_gles_over_gl()) {
		glClearDepth(p_depth);
	}
#endif // GL_API_ENABLED
#ifdef GLES_API_ENABLED
	if (!is_gles_over_gl()) {
		glClearDepthf(p_depth);
	}
#endif // GLES_API_ENABLED
}

#ifdef CAN_DEBUG
static void GLAPIENTRY _gl_debug_print(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const GLvoid *userParam) {
	// These are ultimately annoying, so removing for now.
	if (type == _EXT_DEBUG_TYPE_OTHER_ARB || type == _EXT_DEBUG_TYPE_PERFORMANCE_ARB) {
		return;
	}

	char debSource[256], debType[256], debSev[256];

	if (source == _EXT_DEBUG_SOURCE_API_ARB) {
		strcpy(debSource, "OpenGL");
	} else if (source == _EXT_DEBUG_SOURCE_WINDOW_SYSTEM_ARB) {
		strcpy(debSource, "Windows");
	} else if (source == _EXT_DEBUG_SOURCE_SHADER_COMPILER_ARB) {
		strcpy(debSource, "Shader Compiler");
	} else if (source == _EXT_DEBUG_SOURCE_THIRD_PARTY_ARB) {
		strcpy(debSource, "Third Party");
	} else if (source == _EXT_DEBUG_SOURCE_APPLICATION_ARB) {
		strcpy(debSource, "Application");
	} else if (source == _EXT_DEBUG_SOURCE_OTHER_ARB) {
		strcpy(debSource, "Other");
	} else {
		ERR_FAIL_MSG(vformat("GL ERROR: Invalid or unhandled source '%d' in debug callback.", source));
	}

	if (type == _EXT_DEBUG_TYPE_ERROR_ARB) {
		strcpy(debType, "Error");
	} else if (type == _EXT_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB) {
		strcpy(debType, "Deprecated behavior");
	} else if (type == _EXT_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB) {
		strcpy(debType, "Undefined behavior");
	} else if (type == _EXT_DEBUG_TYPE_PORTABILITY_ARB) {
		strcpy(debType, "Portability");
	} else {
		ERR_FAIL_MSG(vformat("GL ERROR: Invalid or unhandled type '%d' in debug callback.", type));
	}

	if (severity == _EXT_DEBUG_SEVERITY_HIGH_ARB) {
		strcpy(debSev, "High");
	} else if (severity == _EXT_DEBUG_SEVERITY_MEDIUM_ARB) {
		strcpy(debSev, "Medium");
	} else if (severity == _EXT_DEBUG_SEVERITY_LOW_ARB) {
		strcpy(debSev, "Low");
	} else {
		ERR_FAIL_MSG(vformat("GL ERROR: Invalid or unhandled severity '%d' in debug callback.", severity));
	}

	String output = String() + "GL ERROR: Source: " + debSource + "\tType: " + debType + "\tID: " + itos(id) + "\tSeverity: " + debSev + "\tMessage: " + message;

	ERR_PRINT(output);
}
#endif

typedef void(GLAPIENTRY *DEBUGPROCARB)(GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		GLsizei length,
		const char *message,
		const void *userParam);

typedef void(GLAPIENTRY *DebugMessageCallbackARB)(DEBUGPROCARB callback, const void *userParam);

void RasterizerGLES2::initialize() {
	Engine::get_singleton()->print_header(vformat("OpenGL API %s - Legacy - Using Device: %s - %s", RS::get_singleton()->get_video_adapter_api_version(), RS::get_singleton()->get_video_adapter_vendor(), RS::get_singleton()->get_video_adapter_name()));
}

void RasterizerGLES2::finalize() {
	memdelete(scene);
	memdelete(canvas);
	memdelete(gi);
	memdelete(fog);
	memdelete(post_effects);
	memdelete(glow);
	memdelete(cubemap_filter);
	memdelete(copy_effects);
	memdelete(feed_effects);
	memdelete(light_storage);
	memdelete(particles_storage);
	memdelete(mesh_storage);
	memdelete(material_storage);
	memdelete(texture_storage);
	memdelete(utilities);
	memdelete(config);
}

RasterizerGLES2 *RasterizerGLES2::singleton = nullptr;

#ifdef EGL_ENABLED
void *_egl_load_function_wrapper_gles2(const char *p_name) {
	return (void *)eglGetProcAddress(p_name);
}
#endif

RasterizerGLES2::RasterizerGLES2() {
	singleton = this;

#ifdef GLAD_ENABLED
	bool glad_loaded = false;

#ifdef EGL_ENABLED
	// There should be a more flexible system for getting the GL pointer, as
	// different DisplayServers can have different ways. We can just use the GLAD
	// version global to see if it loaded for now though, otherwise we fall back to
	// the generic loader below.
#if defined(EGL_STATIC)
	bool has_egl = true;
#else
	bool has_egl = (eglGetProcAddress != nullptr);
#endif

	if (gles_over_gl) {
		if (has_egl && !glad_loaded && gladLoadGL((GLADloadfunc)&_egl_load_function_wrapper_gles2)) {
			glad_loaded = true;
		}
	} else {
		if (has_egl && !glad_loaded && gladLoadGLES2((GLADloadfunc)&_egl_load_function_wrapper_gles2)) {
			glad_loaded = true;
		}
	}
#endif // EGL_ENABLED

	if (gles_over_gl) {
		if (!glad_loaded && gladLoaderLoadGL()) {
			glad_loaded = true;
		}
	} else {
		if (!glad_loaded && gladLoaderLoadGLES2()) {
			glad_loaded = true;
		}
	}

	// FIXME this is an early return from a constructor.  Any other code using this instance will crash or the finalizer will crash, because none of
	// the members of this instance are initialized, so this just makes debugging harder.  It should either crash here intentionally,
	// or we need to actually test for this situation before constructing this.
	ERR_FAIL_COND_MSG(!glad_loaded, "Error initializing GLAD.");

	if (gles_over_gl) {
		if (OS::get_singleton()->is_stdout_verbose()) {
			if (GLAD_GL_ARB_debug_output) {
				glEnable(_EXT_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
				glDebugMessageCallbackARB((GLDEBUGPROCARB)_gl_debug_print, nullptr);
				glEnable(_EXT_DEBUG_OUTPUT);
			} else {
				print_line("OpenGL debugging not supported!");
			}
		}
	}
#endif // GLAD_ENABLED

	// For debugging
#ifdef CAN_DEBUG
#ifdef GL_API_ENABLED
	if (gles_over_gl) {
		if (OS::get_singleton()->is_stdout_verbose() && GLAD_GL_ARB_debug_output) {
			glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_ERROR_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, nullptr, GL_TRUE);
			glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, nullptr, GL_TRUE);
			glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, nullptr, GL_TRUE);
			glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_PORTABILITY_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, nullptr, GL_TRUE);
			glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_PERFORMANCE_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, nullptr, GL_TRUE);
			glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_OTHER_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, nullptr, GL_TRUE);
		}
	}
#endif // GL_API_ENABLED
#ifdef GLES_API_ENABLED
	if (!gles_over_gl) {
		if (OS::get_singleton()->is_stdout_verbose()) {
			DebugMessageCallbackARB callback = (DebugMessageCallbackARB)eglGetProcAddress("glDebugMessageCallback");
			if (!callback) {
				callback = (DebugMessageCallbackARB)eglGetProcAddress("glDebugMessageCallbackKHR");
			}

			if (callback) {
				print_line("godot: ENABLING GL DEBUG");
				glEnable(_EXT_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
				callback((DEBUGPROCARB)_gl_debug_print, nullptr);
				glEnable(_EXT_DEBUG_OUTPUT);
			}
		}
	}
#endif // GLES_API_ENABLED
#endif // CAN_DEBUG

	{
		String shader_cache_dir = Engine::get_singleton()->get_shader_cache_path();
		if (shader_cache_dir.is_empty()) {
			shader_cache_dir = "user://";
		}
		Ref<DirAccess> da = DirAccess::open(shader_cache_dir);
		if (da.is_null()) {
			ERR_PRINT("Can't create shader cache folder, no shader caching will happen: " + shader_cache_dir);
		} else {
			Error err = da->change_dir("shader_cache");
			if (err != OK) {
				err = da->make_dir("shader_cache");
			}
			if (err != OK) {
				ERR_PRINT("Can't create shader cache folder, no shader caching will happen: " + shader_cache_dir);
			} else {
				shader_cache_dir = shader_cache_dir.path_join("shader_cache");

				bool shader_cache_enabled = GLOBAL_GET("rendering/shader_compiler/shader_cache/enabled");
				if (!Engine::get_singleton()->is_editor_hint() && !shader_cache_enabled) {
					shader_cache_dir = String(); //disable only if not editor
				}

				if (!shader_cache_dir.is_empty()) {
					ShaderGLES2::set_shader_cache_dir(shader_cache_dir);
				}
			}
		}
	}

	// OpenGL needs to be initialized before initializing the Rasterizers
	config = memnew(GLES2::Config);
	utilities = memnew(GLES2::Utilities);
	texture_storage = memnew(GLES2::TextureStorage);
	material_storage = memnew(GLES2::MaterialStorage);
	mesh_storage = memnew(GLES2::MeshStorage);
	particles_storage = memnew(GLES2::ParticlesStorage);
	light_storage = memnew(GLES2::LightStorage);
	copy_effects = memnew(GLES2::CopyEffects);
	cubemap_filter = memnew(GLES2::CubemapFilter);
	glow = memnew(GLES2::Glow);
	post_effects = memnew(GLES2::PostEffects);
	feed_effects = memnew(GLES2::FeedEffects);
	gi = memnew(GLES2::GI);
	fog = memnew(GLES2::Fog);
	canvas = memnew(RasterizerCanvasGLES2());
	scene = memnew(RasterizerSceneGLES2());

	// Disable OpenGL linear to sRGB conversion, because Godot will always do this conversion itself.
	if (config->srgb_framebuffer_supported) {
		glDisable(GL_FRAMEBUFFER_SRGB);
	}
}

RasterizerGLES2::~RasterizerGLES2() {
}

void RasterizerGLES2::_blit_render_target_to_screen(RID p_render_target, DisplayServer::WindowID p_screen, const Rect2 &p_screen_rect, uint32_t p_layer, bool p_first) {

}

// is this p_screen useless in a multi window environment?
void RasterizerGLES2::blit_render_targets_to_screen(DisplayServer::WindowID p_screen, const BlitToScreen *p_render_targets, int p_amount) {

}

void RasterizerGLES2::set_boot_image(const Ref<Image> &p_image, const Color &p_color, bool p_scale, bool p_use_filter) {

}

#endif // GLES2_ENABLED
