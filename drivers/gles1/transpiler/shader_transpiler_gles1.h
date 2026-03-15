/**************************************************************************/
/*  shader_transpiler_gles1.h                                             */
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

#ifndef SHADER_TRANSPILER_GLES1_H
#define SHADER_TRANSPILER_GLES1_H

#ifdef GLES1_ENABLED

#include "servers/rendering/shader_compiler.h"
#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_types.h"
#include "core/string/string_builder.h"

class ShaderTranspilerGLES1 {

private:
	ShaderLanguage parser;

	String _dump_node_code(ShaderLanguage::Node *p_node, int p_level, ShaderCompiler::GeneratedCode &r_gen_code, ShaderCompiler::IdentifierActions &p_actions, const ShaderCompiler::DefaultIdentifierActions &p_default_actions, bool p_assigning, bool p_use_scope = true);

	StringName current_func_name;
	StringName vertex_name;
	StringName fragment_name;
	StringName light_name;
	StringName time_name;

	HashSet<StringName> used_name_defines;
	HashSet<StringName> used_flag_pointers;
	HashSet<StringName> used_rmode_defines;
	HashSet<StringName> internal_functions;

	ShaderCompiler::DefaultIdentifierActions actions;

	// compatibility with godot 4
	static ShaderLanguage::DataType _get_variable_type(const StringName &p_type);

public:
	Error compile(RS::ShaderMode p_mode, const String &p_code, ShaderCompiler::IdentifierActions *p_actions, const String &p_path, ShaderCompiler::GeneratedCode &r_gen_code);

	void initialize(const ShaderCompiler::DefaultIdentifierActions &p_actions);

	ShaderTranspilerGLES1();
};

#endif // GLES1_ENABLED
#endif // SHADER_COMPILER_GLES1_H
