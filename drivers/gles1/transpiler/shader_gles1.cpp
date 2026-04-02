/**************************************************************************/
/*  shader_gles1.cpp                                                      */
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

#include "shader_gles1.h"

#ifdef GLES1_ENABLED

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "drivers/gles1/rasterizer_gles1.h"
#include "drivers/gles1/storage/config.h"

String _mkid(const String &p_id) {
	String id = "m_" + p_id.replace("__", "_dus_");
	return id.replace("__", "_dus_"); // doubleunderscore is reserved in glsl
}

void ShaderGLES1::_add_stage(const char *p_code, StageType p_stage_type) {
	Vector<String> lines = String(p_code).split("\n");
	String text;

	for (int i = 0; i < lines.size(); i++) {
		const String &l = lines[i];
		bool push_chunk = false;

		StageTemplate::Chunk chunk;

		if (l.begins_with("#GLOBALS")) {
			chunk.type = (p_stage_type == STAGE_TYPE_VERTEX) ? StageTemplate::Chunk::TYPE_VERTEX_GLOBALS : StageTemplate::Chunk::TYPE_FRAGMENT_GLOBALS;
			push_chunk = true;
		} else if (l.begins_with("#MATERIAL_UNIFORMS")) {
			chunk.type = StageTemplate::Chunk::TYPE_MATERIAL_UNIFORMS;
			push_chunk = true;
		} else if (l.begins_with("#CODE")) {
			chunk.type = StageTemplate::Chunk::TYPE_CODE;
			push_chunk = true;
			chunk.code = l.replace_first("#CODE", String()).replace(":", "").strip_edges().to_upper();
		} else {
			text += l + "\n";
		}

		if (push_chunk) {
			if (!text.is_empty()) {
				StageTemplate::Chunk text_chunk;
				text_chunk.type = StageTemplate::Chunk::TYPE_TEXT;
				text_chunk.text = text.utf8();
				stage_templates[p_stage_type].chunks.push_back(text_chunk);
				text = String();
			}
			stage_templates[p_stage_type].chunks.push_back(chunk);
		}
	}

	if (!text.is_empty()) {
		StageTemplate::Chunk text_chunk;
		text_chunk.type = StageTemplate::Chunk::TYPE_TEXT;
		text_chunk.text = text.utf8();
		stage_templates[p_stage_type].chunks.push_back(text_chunk);
	}
}

void ShaderGLES1::_setup(
		const char *p_vertex_code, const char *p_fragment_code, const char *p_name,
		int p_uniform_count, const char **p_uniforms,
		int p_attribute_count, const AttributePair *p_attributes,
		int p_texunit_count, const TexUnitPair *p_texunits,
		int p_specialization_count, const Specialization *p_specializations,
		int p_variant_count, const char **p_variants) {
	name = p_name;

	if (p_vertex_code) {
		_add_stage(p_vertex_code, STAGE_TYPE_VERTEX);
	}
	if (p_fragment_code) {
		_add_stage(p_fragment_code, STAGE_TYPE_FRAGMENT);
	}

	uniform_names = p_uniforms;
	uniform_count = p_uniform_count;
	attribute_pairs = p_attributes;
	attribute_pair_count = p_attribute_count;
	texunit_pairs = p_texunits;
	texunit_pair_count = p_texunit_count;
	specializations = p_specializations;
	specialization_count = p_specialization_count;

	specialization_default_mask = 0;
	for (int i = 0; i < specialization_count; i++) {
		if (specializations[i].default_value) {
			specialization_default_mask |= (uint64_t(1) << uint64_t(i));
		}
	}

	variant_defines = p_variants;
	variant_count = p_variant_count;

	StringBuilder tohash;
	tohash.append("[Vertex]");
	tohash.append(p_vertex_code ? p_vertex_code : "");
	tohash.append("[Fragment]");
	tohash.append(p_fragment_code ? p_fragment_code : "");

	tohash.append("[gl_implementation]");
	const String &vendor = String::utf8((const char *)glGetString(GL_VENDOR));
	tohash.append(vendor.is_empty() ? vendor : "unknown");
	const String &renderer = String::utf8((const char *)glGetString(GL_RENDERER));
	tohash.append(renderer.is_empty() ? renderer : "unknown");
	const String &version = String::utf8((const char *)glGetString(GL_VERSION));
	tohash.append(version.is_empty() ? version : "unknown");

	base_sha256 = tohash.as_string().sha256_text();
}

RID ShaderGLES1::version_create() {
	// initialize() was never called
	ERR_FAIL_COND_V(variant_count == 0, RID());

	Version version;
	return version_owner.make_rid(version);
}

void ShaderGLES1::_build_variant_code(StringBuilder &builder, uint32_t p_variant, const Version *p_version, StageType p_stage_type, uint64_t p_specialization) {

}


void _display_error_with_code(const String &p_error, const String &p_code) {
	int line = 1;
	Vector<String> lines = p_code.split("\n");

	for (int j = 0; j < lines.size(); j++) {
		print_line(itos(line) + ": " + lines[j]);
		line++;
	}

	ERR_PRINT(p_error);
}

void ShaderGLES1::_get_uniform_locations(Version::Specialization &spec, Version *p_version) {

}

void ShaderGLES1::_compile_specialization(Version::Specialization &spec, uint32_t p_variant, Version *p_version, uint64_t p_specialization) {
	// Dummy ID.
	spec.id = 1;
	spec.vert_id = 0;
	spec.frag_id = 0;
	spec.ok = true;
}

RS::ShaderNativeSourceCode ShaderGLES1::version_get_native_source_code(RID p_version) {
	Version *version = version_owner.get_or_null(p_version);
	RS::ShaderNativeSourceCode source_code;
	ERR_FAIL_NULL_V(version, source_code);

	source_code.versions.resize(variant_count);

	for (int i = 0; i < source_code.versions.size(); i++) {
		// Vertex stage
		{
			StringBuilder builder;
			_build_variant_code(builder, i, version, STAGE_TYPE_VERTEX, specialization_default_mask);

			RS::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "vertex";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}

		// Fragment stage
		{
			StringBuilder builder;
			_build_variant_code(builder, i, version, STAGE_TYPE_FRAGMENT, specialization_default_mask);

			RS::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "fragment";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}
	}

	return source_code;
}

String ShaderGLES1::_version_get_sha1(Version *p_version) const {
	return String("GLES1").sha1_text();
}

#ifndef WEB_ENABLED // not supported in webgl
// static const char *shader_file_header = "GLSC";
// static const uint32_t cache_file_version = 3;
#endif

bool ShaderGLES1::_load_from_cache(Version *p_version) {
	return false;
}

void ShaderGLES1::_save_to_cache(Version *p_version) {

}

void ShaderGLES1::_clear_version(Version *p_version) {
	p_version->variants.clear();
}

void ShaderGLES1::_initialize_version(Version *p_version) {
	ERR_FAIL_COND(p_version->variants.size() > 0);

	// Note: Cache loading logic bypassed for GLES2 due
	// to poor glProgramBinary support

	p_version->variants.reserve(variant_count);
	for (int i = 0; i < variant_count; i++) {
		OAHashMap<uint64_t, Version::Specialization> variant;
		p_version->variants.push_back(variant);
		Version::Specialization spec;

		_compile_specialization(spec, i, p_version, specialization_default_mask);
		p_version->variants[i].insert(specialization_default_mask, spec);
	}
}

void ShaderGLES1::version_set_code(RID p_version, const HashMap<String, String> &p_code, const String &p_uniforms, const String &p_vertex_globals, const String &p_fragment_globals, const Vector<String> &p_custom_defines, const LocalVector<ShaderGLES1::TextureUniformData> &p_texture_uniforms, bool p_initialize) {
	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL(version);

	_clear_version(version); // clear if existing

	version->vertex_globals = p_vertex_globals.utf8();
	version->fragment_globals = p_fragment_globals.utf8();
	version->uniforms = p_uniforms.utf8();
	version->code_sections.clear();
	version->texture_uniforms = p_texture_uniforms;

	for (const KeyValue<String, String> &E : p_code) {
		version->code_sections[StringName(E.key.to_upper())] = E.value.utf8();
	}

	version->custom_defines.clear();
	for (int i = 0; i < p_custom_defines.size(); i++) {
		version->custom_defines.push_back(p_custom_defines[i].utf8());
	}

	if (p_initialize) {
		_initialize_version(version);
	}
}

bool ShaderGLES1::version_is_valid(RID p_version) {
	Version *version = version_owner.get_or_null(p_version);
	return version != nullptr;
}

bool ShaderGLES1::version_free(RID p_version) {
	if (version_owner.owns(p_version)) {
		Version *version = version_owner.get_or_null(p_version);
		_clear_version(version);
		version_owner.free(p_version);
	} else {
		return false;
	}

	return true;
}

bool ShaderGLES1::shader_cache_cleanup_on_start = false;

ShaderGLES1::ShaderGLES1() {
}

void ShaderGLES1::initialize(const String &p_general_defines, int p_base_texture_index) {
	general_defines = p_general_defines.utf8();
	base_texture_index = p_base_texture_index;

	_init();

	max_image_units = GLES1::Config::get_singleton()->max_texture_image_units;
}

void ShaderGLES1::set_shader_cache_dir(const String &p_dir) {
	shader_cache_dir = p_dir;
}

void ShaderGLES1::set_shader_cache_save_compressed(bool p_enable) {
	shader_cache_save_compressed = p_enable;
}

void ShaderGLES1::set_shader_cache_save_compressed_zstd(bool p_enable) {
	shader_cache_save_compressed_zstd = p_enable;
}

void ShaderGLES1::set_shader_cache_save_debug(bool p_enable) {
	shader_cache_save_debug = p_enable;
}

String ShaderGLES1::shader_cache_dir;
bool ShaderGLES1::shader_cache_save_compressed = true;
bool ShaderGLES1::shader_cache_save_compressed_zstd = true;
bool ShaderGLES1::shader_cache_save_debug = true;

ShaderGLES1::~ShaderGLES1() {
	List<RID> remaining;
	version_owner.get_owned_list(&remaining);
	if (remaining.size()) {
		ERR_PRINT(itos(remaining.size()) + " shaders of type " + name + " were never freed");
		while (remaining.size()) {
			version_free(remaining.front()->get());
			remaining.pop_front();
		}
	}
}
#endif
