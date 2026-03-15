/**************************************************************************/
/*  shader_gles3.cpp                                                      */
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

#include "shader_gles2.h"

#ifdef GLES2_ENABLED

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "drivers/gles2/rasterizer_gles2.h"
#include "drivers/gles2/storage/config.h"

[[maybe_unused]] static String _mkid(const String &p_id) {
    return "";
}

void ShaderGLES2::_add_stage(const char *p_code, StageType p_stage_type) {

}

void ShaderGLES2::_setup(
	const char *p_vertex_code, const char *p_fragment_code, const char *p_name, 
	int p_uniform_count, const char **p_uniforms, 
	int p_attribute_count, const AttributePair *p_attributes, 
	int p_texunit_count, const TexUnitPair *p_texunits, 
	int p_specialization_count, const Specialization *p_specializations, 
	int p_variant_count, const char **p_variants) {

}

RID ShaderGLES2::version_create() {
    return RID();
}

void ShaderGLES2::_build_variant_code(StringBuilder &builder, uint32_t p_variant, const Version *p_version, StageType p_stage_type, uint64_t p_specialization) {

}

[[maybe_unused]] static void _display_error_with_code(const String &p_error, const String &p_code) {

}

void ShaderGLES2::_get_uniform_locations(Version::Specialization &spec, Version *p_version) {

}

void ShaderGLES2::_compile_specialization(Version::Specialization &spec, uint32_t p_variant, Version *p_version, uint64_t p_specialization) {

}

RS::ShaderNativeSourceCode ShaderGLES2::version_get_native_source_code(RID p_version) {
	RS::ShaderNativeSourceCode source_code;

    return source_code;
}

String ShaderGLES2::_version_get_sha1(Version *p_version) const {
    return String();
}

#ifndef WEB_ENABLED // not supported in webgl
// static const char *shader_file_header = "GLSC";
// static const uint32_t cache_file_version = 3;
#endif

bool ShaderGLES2::_load_from_cache(Version *p_version) {
//#ifdef WEB_ENABLED // not supported in webgl
	//return false;
//#else
//#if !defined(ANDROID_ENABLED) && !defined(IOS_ENABLED)
//	if (RasterizerGLES3::is_gles_over_gl() && (glProgramBinary == nullptr)) { // ARB_get_program_binary extension not available.
//		return false;
//	}
//#endif
	return false;
// #endif // WEB_ENABLED
}

void ShaderGLES2::_save_to_cache(Version *p_version) {
// #ifdef WEB_ENABLED // not supported in webgl
// 	return;
// #else
// 	ERR_FAIL_COND(!shader_cache_dir_valid);
// #if !defined(ANDROID_ENABLED) && !defined(IOS_ENABLED)
// 	if (RasterizerGLES3::is_gles_over_gl() && (glGetProgramBinary == nullptr)) { // ARB_get_program_binary extension not available.
// 		return;
// 	}
// #endif
// #endif // WEB_ENABLED
}

void ShaderGLES2::_clear_version(Version *p_version) {

}

void ShaderGLES2::_initialize_version(Version *p_version) {

}

void ShaderGLES2::version_set_code(RID p_version, const HashMap<String, String> &p_code, const String &p_uniforms, const String &p_vertex_globals, const String &p_fragment_globals, const Vector<String> &p_custom_defines, const LocalVector<ShaderGLES2::TextureUniformData> &p_texture_uniforms, bool p_initialize) {

}

bool ShaderGLES2::version_is_valid(RID p_version) {
    return false;
}

bool ShaderGLES2::version_free(RID p_version) {
    return false;
}

bool ShaderGLES2::shader_cache_cleanup_on_start = false;

ShaderGLES2::ShaderGLES2() {
}

void ShaderGLES2::initialize(const String &p_general_defines, int p_base_texture_index) {

}

void ShaderGLES2::set_shader_cache_dir(const String &p_dir) {
	shader_cache_dir = p_dir;
}

void ShaderGLES2::set_shader_cache_save_compressed(bool p_enable) {
	shader_cache_save_compressed = p_enable;
}

void ShaderGLES2::set_shader_cache_save_compressed_zstd(bool p_enable) {
	shader_cache_save_compressed_zstd = p_enable;
}

void ShaderGLES2::set_shader_cache_save_debug(bool p_enable) {
	shader_cache_save_debug = p_enable;
}

String ShaderGLES2::shader_cache_dir;
bool ShaderGLES2::shader_cache_save_compressed = true;
bool ShaderGLES2::shader_cache_save_compressed_zstd = true;
bool ShaderGLES2::shader_cache_save_debug = true;

ShaderGLES2::~ShaderGLES2() {

}
#endif
