/**************************************************************************/
/*  shader_gles2.cpp                                                      */
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

#include "drivers/gles_common/error_macros.h"
#include "drivers/gles2/rasterizer_gles2.h"
#include "drivers/gles2/storage/config.h"

static String _mkid(const String &p_id) {
	String id = "m_" + p_id.replace("__", "_dus_");
	return id.replace("__", "_dus_"); // doubleunderscore is reserved in glsl
}

void ShaderGLES2::_add_stage(const char *p_code, StageType p_stage_type) {
	Vector<String> lines = String(p_code).split("\n");
	StringBuilder text;

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
			text.append(l);
			text.append("\n");
		}

		if (push_chunk) {
			String text_str = text.as_string();
			if (!text_str.is_empty()) {
				StageTemplate::Chunk text_chunk;
				text_chunk.type = StageTemplate::Chunk::TYPE_TEXT;
				text_chunk.text = text_str.utf8();
				stage_templates[p_stage_type].chunks.push_back(text_chunk);
				text = StringBuilder();
			}
			stage_templates[p_stage_type].chunks.push_back(chunk);
		}
	}

	String final_text_str = text.as_string();
	if (!final_text_str.is_empty()) {
		StageTemplate::Chunk text_chunk;
		text_chunk.type = StageTemplate::Chunk::TYPE_TEXT;
		text_chunk.text = final_text_str.utf8();
		stage_templates[p_stage_type].chunks.push_back(text_chunk);
	}
}

void ShaderGLES2::_setup(
		const char *p_vertex_code, const char *p_fragment_code, const char *p_name,
		int p_uniform_count, const char **p_uniforms,
		int p_attribute_count, const AttributePair *p_attributes,
		int p_feedback_count, const Feedback *p_feedbacks,
		int p_texunit_count, const TexUnitPair *p_texunits,
		int p_specialization_count, const Specialization *p_specializations,
		int p_variant_count, const char **p_variants) {
	name = p_name;

	// Clear the chunks
	stage_templates[STAGE_TYPE_VERTEX].chunks.clear();
	stage_templates[STAGE_TYPE_FRAGMENT].chunks.clear();

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
	feedbacks = p_feedbacks;
	feedback_count = p_feedback_count;
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

RID ShaderGLES2::version_create() {
	// initialize() was never called
	ERR_FAIL_COND_V(variant_count == 0, RID());

	Version version;
	return version_owner.make_rid(version);
}

void ShaderGLES2::_build_variant_code(StringBuilder &builder, uint32_t p_variant, const Version *p_version, StageType p_stage_type, uint64_t p_specialization) {
	if (RasterizerGLES2::is_gles_over_gl()) {
		builder.append("#version 120\n");
		builder.append("#define USE_GLES_OVER_GL\n");
	} else {
		builder.append("#version 100\n");
	}

	// Hardware extensions
	if (p_stage_type == STAGE_TYPE_FRAGMENT && GLES2::Config::get_singleton()->support_instancing) {
		builder.append("#extension GL_EXT_draw_instanced : enable\n");
	}
	if (p_stage_type == STAGE_TYPE_FRAGMENT && GLES2::Config::get_singleton()->support_frag_depth) {
		builder.append("#extension GL_EXT_frag_depth : enable\n");
	}
	if (p_stage_type == STAGE_TYPE_FRAGMENT && GLES2::Config::get_singleton()->texture_lod_supported) {
		builder.append("#extension GL_EXT_shader_texture_lod : enable\n");
	}
	if (p_stage_type == STAGE_TYPE_VERTEX && GLES2::Config::get_singleton()->support_transform_feedback) {
		if (!RasterizerGLES2::is_gles_over_gl()) {
			builder.append("#extension GL_EXT_transform_feedback : enable\n");
		}
	}
	if (GLES2::Config::get_singleton()->support_transform_feedback) {
		// Only require the EXT extension if we are on mobile/web GLES.
		if (!RasterizerGLES2::is_gles_over_gl()) {
			builder.append("#extension GL_EXT_transform_feedback : enable\n");
		}
	}

	for (int i = 0; i < specialization_count; i++) {
		if (p_specialization & (uint64_t(1) << uint64_t(i))) {
			builder.append(String("#define ") + specializations[i].name + "\n");
		}
	}

	if (p_version->uniforms.length() > 0) {
		builder.append("#define MATERIAL_UNIFORMS_USED\n");
	}
	if (GLES2::Config::get_singleton()->max_vertex_texture_image_units == 0) {
		builder.append("#define USE_SKELETON_UNIFORM\n");
	}

	for (const KeyValue<StringName, CharString> &E : p_version->code_sections) {
		builder.append(String("#define ") + String(E.key) + "_CODE_USED\n");
	}

	builder.append("\n"); // make sure defines begin at newline

	// Polyfills
	if (p_stage_type == STAGE_TYPE_VERTEX && GLES2::Config::get_singleton()->support_instancing) {
		builder.append("#define gl_InstanceID gl_InstanceIDEXT\n");
	}
	if (p_stage_type == STAGE_TYPE_FRAGMENT && GLES2::Config::get_singleton()->support_frag_depth) {
		builder.append("#define gl_FragDepth gl_FragDepthEXT\n");
	}
	if (GLES2::Config::get_singleton()->support_transform_feedback) {
		builder.append("#define USE_TRANSFORM_FEEDBACK\n");
	}

	// GLES2 Texture Polyfills
	builder.append("#define texture texture2D\n");
	builder.append("#define textureProj texture2DProj\n");
	builder.append("#define textureLod texture2DLodEXT\n");
	builder.append("#define textureProjLod texture2DProjLodEXT\n");

	builder.append(general_defines.get_data());
	if (variant_defines != nullptr) {
		builder.append(variant_defines[p_variant]);
	}
	builder.append("\n");

	for (int j = 0; j < p_version->custom_defines.size(); j++) {
		builder.append(p_version->custom_defines[j].get_data());
	}
	builder.append("\n");

	// Default to highp precision unless specified otherwise.
	builder.append("#ifndef USE_GLES_OVER_GL\n");
	builder.append("#ifdef GL_FRAGMENT_PRECISION_HIGH\n");
	builder.append("precision highp float;\n");
	builder.append("precision highp int;\n");
	builder.append("precision highp sampler2D;\n");
	builder.append("precision highp samplerCube;\n");
	builder.append("#else\n");
	builder.append("precision mediump float;\n");
	builder.append("precision mediump int;\n");
	builder.append("precision mediump sampler2D;\n");
	builder.append("precision mediump samplerCube;\n");
	builder.append("#endif\n");
	builder.append("#else\n");
	// Globally strip precision qualifiers for desktop GLSL 1.20
	builder.append("#define lowp\n");
	builder.append("#define mediump\n");
	builder.append("#define highp\n");
	builder.append("#endif\n");

	const StageTemplate &stage_template = stage_templates[p_stage_type];
	for (uint32_t i = 0; i < stage_template.chunks.size(); i++) {
		const StageTemplate::Chunk &chunk = stage_template.chunks[i];
		switch (chunk.type) {
			case StageTemplate::Chunk::TYPE_MATERIAL_UNIFORMS: {
				builder.append(p_version->uniforms.get_data());
			} break;
			case StageTemplate::Chunk::TYPE_VERTEX_GLOBALS: {
				builder.append(p_version->vertex_globals.get_data());
			} break;
			case StageTemplate::Chunk::TYPE_FRAGMENT_GLOBALS: {
				builder.append(p_version->fragment_globals.get_data());
			} break;
			case StageTemplate::Chunk::TYPE_CODE: {
				if (p_version->code_sections.has(chunk.code)) {
					builder.append(p_version->code_sections[chunk.code].get_data());
				}
			} break;
			case StageTemplate::Chunk::TYPE_TEXT: {
				builder.append(chunk.text.get_data());
			} break;
		}
	}
}

static void _display_error_with_code(const String &p_error, const String &p_code) {
	int line = 1;
	Vector<String> lines = p_code.split("\n");

	for (int j = 0; j < lines.size(); j++) {
		print_line(itos(line) + ": " + lines[j]);
		line++;
	}

	ERR_PRINT(p_error);
}

void ShaderGLES2::_get_uniform_locations(Version::Specialization &spec, Version *p_version) {
	GLint active_program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &active_program);
	GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: glGetIntegerv");

	glUseProgram(spec.id);
	GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: glUseProgram");

	spec.uniform_location.resize(uniform_count);
	for (int i = 0; i < uniform_count; i++) {
		spec.uniform_location[i] = glGetUniformLocation(spec.id, uniform_names[i]);
	}

	for (int i = 0; i < texunit_pair_count; i++) {
		GLint loc = glGetUniformLocation(spec.id, texunit_pairs[i].name);
		if (loc >= 0) {
			if (texunit_pairs[i].index < 0) {
				glUniform1i(loc, max_image_units + texunit_pairs[i].index);
			} else {
				glUniform1i(loc, texunit_pairs[i].index);
			}
			// Catch wrap-around or out-of-bounds texture units
			GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: glUniform1i (texunit_pairs)");
		}
	}

	// Textures
	int texture_index = 0;
	for (uint32_t i = 0; i < p_version->texture_uniforms.size(); i++) {
		String native_uniform_name = _mkid(p_version->texture_uniforms[i].name);

		GLint location = glGetUniformLocation(spec.id, native_uniform_name.ascii().get_data());
		if (location >= 0) {
			Vector<int32_t> texture_uniform_bindings;
			// A size of 0 means it's a single texture, not an array.
			// At least 1 binding is required.
			int texture_count = MAX(1, p_version->texture_uniforms[i].array_size);
			for (int j = 0; j < texture_count; j++) {
				texture_uniform_bindings.append(texture_index + base_texture_index);
				texture_index++;
			}
			glUniform1iv(location, texture_uniform_bindings.size(), texture_uniform_bindings.ptr());
		} else {
			// Location is -1 if aliased via hint.
			// Must advance texture_index to maintain the
			// correct slot offset for subsequent material textures.
			int texture_count = MAX(1, p_version->texture_uniforms[i].array_size);
			texture_index += texture_count;
		}
	}

	// Map internal textures to the backend FBO slots
	GLint screen_loc = glGetUniformLocation(spec.id, "godot_screen_texture");
	if (screen_loc >= 0) {
		glUniform1i(screen_loc, max_image_units - 4);
		GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: godot_screen_texture");
	}

	GLint depth_loc = glGetUniformLocation(spec.id, "godot_depth_texture");
	if (depth_loc >= 0) {
		glUniform1i(depth_loc, max_image_units - 4);
		GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: godot_depth_texture");
	}

	GLint normal_loc = glGetUniformLocation(spec.id, "godot_normal_texture");
	if (normal_loc >= 0) {
		glUniform1i(normal_loc, max_image_units - 6);
		GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: godot_normal_texture");
	}

	glUseProgram(active_program);
	GL_CHECK_ERROR("ShaderGLES2::_get_uniform_locations: glUseProgram (restore)");
}

void ShaderGLES2::_compile_specialization(Version::Specialization &spec, uint32_t p_variant, Version *p_version, uint64_t p_specialization) {
	spec.id = glCreateProgram();
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glCreateProgram");
	spec.ok = false;
	GLint status;

	// Vertex stage
	{
		StringBuilder builder;
		_build_variant_code(builder, p_variant, p_version, STAGE_TYPE_VERTEX, p_specialization);

		spec.vert_id = glCreateShader(GL_VERTEX_SHADER);
		GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glCreateShader (Vertex)");
		String builder_string = builder.as_string();
		if (builder_string.contains("#extension")) {
			// This path is for even stricter GPUs
			// (like MALI-400) that demand that
			// extension directives must occur before any non-preprocessor tokens.
			Vector<String> lines = builder_string.split("\n");
			StringBuilder extensions_string;
			StringBuilder final_string;

			for (int j = 0; j < lines.size(); j++) {
				String line_stripped = lines[j].strip_edges();
				if (line_stripped.begins_with("#extension")) {
					extensions_string.append(lines[j]);
					extensions_string.append("\n");
				} else {
					final_string.append(lines[j]);
					final_string.append("\n");
				}
			}

			String ext_str = extensions_string.as_string();
			if (!ext_str.is_empty()) {
				String final_str = final_string.as_string();
				final_str = final_str.replace("#version 100\n", "#version 100\n" + ext_str);
				final_str = final_str.replace("#version 120\n", "#version 120\n" + ext_str);
				builder_string = final_str;
			}
		}
		CharString cs = builder_string.utf8();
		const char *cstr = cs.ptr();
		glShaderSource(spec.vert_id, 1, &cstr, nullptr);
		glCompileShader(spec.vert_id);
		GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glCompileShader (Vertex)");

		glGetShaderiv(spec.vert_id, GL_COMPILE_STATUS, &status);
		if (status == GL_FALSE) {
			GLsizei iloglen;
			glGetShaderiv(spec.vert_id, GL_INFO_LOG_LENGTH, &iloglen);

			if (iloglen < 0) {
				glDeleteShader(spec.vert_id);
				glDeleteProgram(spec.id);
				spec.id = 0;
				ERR_PRINT("No OpenGL vertex shader compiler log.");
			} else {
				if (iloglen == 0) {
					iloglen = 4096;
				}
				char *ilogmem = (char *)Memory::alloc_static(iloglen + 1);
				ERR_FAIL_NULL_MSG(ilogmem, "Vertex shader compilation failed: out of memory");
				memset(ilogmem, 0, iloglen + 1);

				GLsizei returned_length = 0;
				glGetShaderInfoLog(spec.vert_id, iloglen, &returned_length, ilogmem);

				String err_string = name + ": Vertex shader compilation failed:\n" + ilogmem;
				_display_error_with_code(err_string, builder_string);

				Memory::free_static(ilogmem);
				glDeleteShader(spec.vert_id);
				glDeleteProgram(spec.id);
				spec.id = 0;
			}
			ERR_FAIL();
		}
	}

	// Fragment stage
	{
		StringBuilder builder;
		_build_variant_code(builder, p_variant, p_version, STAGE_TYPE_FRAGMENT, p_specialization);

		spec.frag_id = glCreateShader(GL_FRAGMENT_SHADER);
		GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glCreateShader (Fragment)");
		String builder_string = builder.as_string();
		if (builder_string.contains("#extension")) {
			Vector<String> lines = builder_string.split("\n");
			StringBuilder extensions_string;
			StringBuilder final_string;

			for (int j = 0; j < lines.size(); j++) {
				String line_stripped = lines[j].strip_edges();
				if (line_stripped.begins_with("#extension")) {
					extensions_string.append(lines[j]);
					extensions_string.append("\n");
				} else {
					final_string.append(lines[j]);
					final_string.append("\n");
				}
			}

			String ext_str = extensions_string.as_string();
			if (!ext_str.is_empty()) {
				String final_str = final_string.as_string();
				final_str = final_str.replace("#version 100\n", "#version 100\n" + ext_str);
				final_str = final_str.replace("#version 120\n", "#version 120\n" + ext_str);
				builder_string = final_str;
			}
		}
		CharString cs = builder_string.utf8();
		const char *cstr = cs.ptr();
		glShaderSource(spec.frag_id, 1, &cstr, nullptr);
		glCompileShader(spec.frag_id);
		GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glCompileShader (Fragment)");

		glGetShaderiv(spec.frag_id, GL_COMPILE_STATUS, &status);
		if (status == GL_FALSE) {
			GLsizei iloglen;
			glGetShaderiv(spec.frag_id, GL_INFO_LOG_LENGTH, &iloglen);

			if (iloglen < 0) {
				glDeleteShader(spec.frag_id);
				if (spec.vert_id != 0) {
					glDeleteShader(spec.vert_id);
					spec.vert_id = 0;
				}
				glDeleteProgram(spec.id);
				spec.id = 0;
				ERR_PRINT("No OpenGL fragment shader compiler log.");
			} else {
				if (iloglen == 0) {
					iloglen = 4096; // buggy driver fallback
				}
				char *ilogmem = (char *)Memory::alloc_static(iloglen + 1);
				ERR_FAIL_NULL_MSG(ilogmem, "Fragment shader compilation failed: out of memory");
				memset(ilogmem, 0, iloglen + 1);

				GLsizei returned_length = 0;
				glGetShaderInfoLog(spec.frag_id, iloglen, &returned_length, ilogmem);

				String err_string = name + ": Fragment shader compilation failed:\n" + ilogmem;
				_display_error_with_code(err_string, builder_string);

				Memory::free_static(ilogmem);
				glDeleteShader(spec.frag_id);

				if (spec.vert_id != 0) {
					glDeleteShader(spec.vert_id);
					spec.vert_id = 0;
				}
				
				glDeleteProgram(spec.id);
				spec.id = 0;
			}
			ERR_FAIL();
		}
	}

	glAttachShader(spec.id, spec.frag_id);
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glAttachShader (Fragment)");

	glAttachShader(spec.id, spec.vert_id);
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glAttachShader (Vertex)");

	// Bind attributes before linking
	GLint max_attribs = 8;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_attribs);

	for (int i = 0; i < attribute_pair_count; i++) {
		if (attribute_pairs[i].index < max_attribs) {
			glBindAttribLocation(spec.id, attribute_pairs[i].index, attribute_pairs[i].name);
		}
	}
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glBindAttribLocation");

	// Bind transform feedback varyings before linking
	if (feedback_count > 0 && GLES2::Config::get_singleton()->support_transform_feedback) {
		LocalVector<const char *> feedback_names;
		for (int i = 0; i < feedback_count; i++) {
			if (feedbacks[i].specialization == 0 || (p_specialization & feedbacks[i].specialization)) {
				feedback_names.push_back(feedbacks[i].name);
			}
		}

		if (feedback_names.size() > 0) {
			glTransformFeedbackVaryingsEXT(spec.id, feedback_names.size(), feedback_names.ptr(), GL_INTERLEAVED_ATTRIBS_EXT);
			GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glTransformFeedbackVaryingsEXT");
		}
	}

	glLinkProgram(spec.id);
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glLinkProgram");

	glGetProgramiv(spec.id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		GLsizei iloglen;
		glGetProgramiv(spec.id, GL_INFO_LOG_LENGTH, &iloglen);

		if (iloglen < 0) {
			glDeleteShader(spec.frag_id);
			glDeleteShader(spec.vert_id);
			glDeleteProgram(spec.id);
			spec.id = 0;
			ERR_PRINT("No OpenGL program link log. Something is wrong.");
			ERR_FAIL();
		}

		if (iloglen == 0) {
			iloglen = 4096;
		}
		char *ilogmem = (char *)Memory::alloc_static(iloglen + 1);
		ERR_FAIL_NULL_MSG(ilogmem, "Program linking failed: out of memory");
		memset(ilogmem, 0, iloglen + 1);

		GLsizei returned_length = 0;
		glGetProgramInfoLog(spec.id, iloglen, &returned_length, ilogmem);

		String err_string = name + ": Program linking failed:\n" + ilogmem;
		_display_error_with_code(err_string, String());

		Memory::free_static(ilogmem);
		glDeleteShader(spec.frag_id);
		glDeleteShader(spec.vert_id);
		glDeleteProgram(spec.id);
		spec.id = 0;
		ERR_FAIL();
	}

	// Detach and delete shaders
	glDetachShader(spec.id, spec.frag_id);
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glDetachShader (Fragment)");
	glDeleteShader(spec.frag_id);
	spec.frag_id = 0;

	glDetachShader(spec.id, spec.vert_id);
	GL_CHECK_ERROR("ShaderGLES2::_compile_specialization: glDetachShader (Vertex)");
	glDeleteShader(spec.vert_id);
	spec.vert_id = 0;

	_get_uniform_locations(spec, p_version);
	spec.ok = true;
}

RS::ShaderNativeSourceCode ShaderGLES2::version_get_native_source_code(RID p_version) {
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

String ShaderGLES2::_version_get_sha1(Version *p_version) const {
	StringBuilder hash_build;

	hash_build.append("[uniforms]");
	hash_build.append(p_version->uniforms.get_data());
	hash_build.append("[vertex_globals]");
	hash_build.append(p_version->vertex_globals.get_data());
	hash_build.append("[fragment_globals]");
	hash_build.append(p_version->fragment_globals.get_data());

	Vector<StringName> code_sections;
	for (const KeyValue<StringName, CharString> &E : p_version->code_sections) {
		code_sections.push_back(E.key);
	}
	code_sections.sort_custom<StringName::AlphCompare>();

	for (int i = 0; i < code_sections.size(); i++) {
		hash_build.append(String("[code:") + String(code_sections[i]) + "]");
		hash_build.append(p_version->code_sections[code_sections[i]].get_data());
	}
	for (int i = 0; i < p_version->custom_defines.size(); i++) {
		hash_build.append("[custom_defines:" + itos(i) + "]");
		hash_build.append(p_version->custom_defines[i].get_data());
	}

	if (RasterizerGLES2::is_gles_over_gl()) {
		hash_build.append("[gl2]");
	} else {
		hash_build.append("[gles2]");
	}


	return hash_build.as_string().sha1_text();
}

#ifndef WEB_ENABLED // not supported in webgl
// static const char *shader_file_header = "GLSC";
// static const uint32_t cache_file_version = 3;
#endif

bool ShaderGLES2::_load_from_cache(Version *p_version) {
	return false;
}

void ShaderGLES2::_save_to_cache(Version *p_version) {
}

void ShaderGLES2::_clear_version(Version *p_version) {
	// Variants not compiled yet, just return
	if (p_version->variants.size() == 0) {
		return;
	}

	for (int i = 0; i < variant_count; i++) {
		for (OAHashMap<uint64_t, Version::Specialization>::Iterator it = p_version->variants[i].iter(); it.valid; it = p_version->variants[i].next_iter(it)) {
			if (it.value->vert_id != 0) {
				glDeleteShader(it.value->vert_id);
				it.value->vert_id = 0;
			}
			if (it.value->frag_id != 0) {
				glDeleteShader(it.value->frag_id);
				it.value->frag_id = 0;
			}
			if (it.value->id != 0) {
				glDeleteProgram(it.value->id);
				it.value->id = 0;
			}
		}
	}

	p_version->variants.clear();
}

void ShaderGLES2::_initialize_version(Version *p_version) {
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

void ShaderGLES2::version_set_code(RID p_version, const HashMap<String, String> &p_code, const String &p_uniforms, const String &p_vertex_globals, const String &p_fragment_globals, const Vector<String> &p_custom_defines, const LocalVector<ShaderGLES2::TextureUniformData> &p_texture_uniforms, bool p_initialize) {
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

bool ShaderGLES2::version_is_valid(RID p_version) {
	Version *version = version_owner.get_or_null(p_version);
	return version != nullptr;
}

bool ShaderGLES2::version_free(RID p_version) {
	if (version_owner.owns(p_version)) {
		Version *version = version_owner.get_or_null(p_version);
		_clear_version(version);
		version_owner.free(p_version);
	} else {
		return false;
	}

	return true;
}

bool ShaderGLES2::shader_cache_cleanup_on_start = false;

ShaderGLES2::ShaderGLES2() {
}

void ShaderGLES2::initialize(const String &p_general_defines, int p_base_texture_index) {
	general_defines = p_general_defines.utf8();
	base_texture_index = p_base_texture_index;

	_init();

	max_image_units = GLES2::Config::get_singleton()->max_texture_image_units;
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
