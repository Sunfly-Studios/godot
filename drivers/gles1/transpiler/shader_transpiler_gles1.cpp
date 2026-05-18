/**************************************************************************/
/*  shader_transpiler_gles1.cpp                                           */
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

// !!! STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP !!!
//
// You are currently looking at a Shader Transpiler for OpenGL ES 1.0.
// Let me say that again. A *Shader* Transpiler. For *GLES1*.
// 
// If you are reading this, you are probably either an engine maintainer
// a random developer, or a graphics programmer saying "wait, a 'transpiler' folder?"
// Then "Sorry, what? `shader_transpiler_gles1.cpp`?"
//
// Yeah, I hear you. The name "shader_transpiler" probably invoked
// "bad juju" if you're graphics programmer (it certainly did for RenderDoc, woops).
// And, understandably, was _screaming_ to be looked at. You probably thought to yourself:
//
// "Why is this here? This is tech debt, or magic. This should be cleaned up, or left alone. 
// fixed-function pipeline doesn't need an AST traversal, or this guy is breaking the laws
// of computer graphics."
// 
// If you thought the latter, then you're correct.
// 
// On May 12, 2026, however, I did the former...
// I gutted this file. As soon as I did that, the CPUParticles burned.
// Particles just, died. Okay, the didn't "die", but their colours said otherwise.
// The fire, didn't burn orange. The fire, burned a dark, cursed, blood-heart red.
// 
// I tore apart the multimesh batcher. I blamed the Texture Unit 1 combiners.
// I multiplied by the custom light multiplier, I even tried to break mathematics
// itself and try to divide by 0. The definition of insanity, kept winning.
// 
// Do you know why the fire bled? Hmmmmm??
// Because this "dead" GLES2 copypasta secretly traverses the non-existent shaders 
// just to flip three microscopic boolean flags. Without this fake transpiler, 
// `vertex_input_mask` evaluates to zero, the GLES1 state machine blinds itself 
// to `ARRAY_FORMAT_COLOR`, and the driver punishes my hubris with uninitialised memory.
// 
// This file does not transpile shaders. It is an ancient, esoteric incantation. 
// It is the duct-tape bridge between a modern 2D material system and a 
// fixed-function pipeline from before I was born.
//
// *Sigh* - I cannot even do that. I cannot even *Sigh* correctly after this.
// 
// Do not refactor it. Do not optimise it. Do not look at it.
// Close the file, and go hug your family (I certainly will)
//
// !!! STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP !!!


#include "shader_transpiler_gles1.h"

#if defined(GLES1_ENABLED)

#include "servers/rendering/shader_types.h"
#include "core/config/project_settings.h"
#include "core/string/string_buffer.h"
#include "core/string/string_builder.h"

static String _mktab(int p_level) {
	String tb;
	for (int i = 0; i < p_level; i++) {
		tb += "\t";
	}

	return tb;
}

static String _mkid(const String &p_id) {
	String id = "m_" + p_id.replace("__", "_dus_");
	return id.replace("__", "_dus_"); //doubleunderscore is reserved in glsl
}

static String f2sp0(float p_float) {
	String num = rtoss(p_float);
	if (num.find(".") == -1 && num.find("e") == -1) {
		num += ".0";
	}
	return num;
}

static String _typestr(ShaderLanguage::DataType p_type) {
	return ShaderLanguage::get_datatype_name(p_type);
}

static String _prestr(ShaderLanguage::DataPrecision p_pres) {
	switch (p_pres) {
		case ShaderLanguage::PRECISION_LOWP:
			return "lowp ";
		case ShaderLanguage::PRECISION_MEDIUMP:
			return "mediump ";
		case ShaderLanguage::PRECISION_HIGHP:
			return "highp ";
		case ShaderLanguage::PRECISION_DEFAULT:
			return "";
	}
	return "";
}

static String _qualstr(ShaderLanguage::ArgumentQualifier p_qual) {
	switch (p_qual) {
		case ShaderLanguage::ARGUMENT_QUALIFIER_IN:
			return "in ";
		case ShaderLanguage::ARGUMENT_QUALIFIER_OUT:
			return "out ";
		case ShaderLanguage::ARGUMENT_QUALIFIER_INOUT:
			return "inout ";
	}
	return "";
}

static String _opstr(ShaderLanguage::Operator p_op) {
	return ShaderLanguage::get_operator_text(p_op);
}

static String get_constant_text(ShaderLanguage::DataType p_type, const Vector<ShaderLanguage::Scalar> &p_values) {
	switch (p_type) {
		case ShaderLanguage::TYPE_BOOL:
			return p_values[0].boolean ? "true" : "false";
		case ShaderLanguage::TYPE_BVEC2:
		case ShaderLanguage::TYPE_BVEC3:
		case ShaderLanguage::TYPE_BVEC4: {
			StringBuffer<> text;

			text += "bvec";
			text += itos(p_type - ShaderLanguage::TYPE_BOOL + 1);
			text += "(";

			for (int i = 0; i < p_values.size(); i++) {
				if (i > 0) {
					text += ",";
				}

				text += p_values[i].boolean ? "true" : "false";
			}
			text += ")";
			return text.as_string();
		}

		// GLSL ES 2 doesn't support uints, so we just use signed ints instead...
		case ShaderLanguage::TYPE_UINT:
			return itos(p_values[0].uint);
		case ShaderLanguage::TYPE_UVEC2:
		case ShaderLanguage::TYPE_UVEC3:
		case ShaderLanguage::TYPE_UVEC4: {
			StringBuffer<> text;

			text += "ivec";
			text += itos(p_type - ShaderLanguage::TYPE_UINT + 1);
			text += "(";

			for (int i = 0; i < p_values.size(); i++) {
				if (i > 0) {
					text += ",";
				}

				text += itos(p_values[i].uint);
			}
			text += ")";
			return text.as_string();

		} break;

		case ShaderLanguage::TYPE_INT:
			return itos(p_values[0].sint);
		case ShaderLanguage::TYPE_IVEC2:
		case ShaderLanguage::TYPE_IVEC3:
		case ShaderLanguage::TYPE_IVEC4: {
			StringBuffer<> text;

			text += "ivec";
			text += itos(p_type - ShaderLanguage::TYPE_INT + 1);
			text += "(";

			for (int i = 0; i < p_values.size(); i++) {
				if (i > 0) {
					text += ",";
				}

				text += itos(p_values[i].sint);
			}
			text += ")";
			return text.as_string();

		} break;
		case ShaderLanguage::TYPE_FLOAT:
			return f2sp0(p_values[0].real);
		case ShaderLanguage::TYPE_VEC2:
		case ShaderLanguage::TYPE_VEC3:
		case ShaderLanguage::TYPE_VEC4: {
			StringBuffer<> text;

			text += "vec";
			text += itos(p_type - ShaderLanguage::TYPE_FLOAT + 1);
			text += "(";

			for (int i = 0; i < p_values.size(); i++) {
				if (i > 0) {
					text += ",";
				}

				text += f2sp0(p_values[i].real);
			}
			text += ")";
			return text.as_string();

		} break;
		case ShaderLanguage::TYPE_MAT2:
		case ShaderLanguage::TYPE_MAT3:
		case ShaderLanguage::TYPE_MAT4: {
			StringBuffer<> text;

			text += "mat";
			text += itos(p_type - ShaderLanguage::TYPE_MAT2 + 2);
			text += "(";

			for (int i = 0; i < p_values.size(); i++) {
				if (i > 0) {
					text += ",";
				}

				text += f2sp0(p_values[i].real);
			}
			text += ")";
			return text.as_string();

		} break;
		default:
			ERR_FAIL_V(String());
	}
}

ShaderLanguage::DataType ShaderTranspilerGLES1::_get_variable_type(const StringName &p_type) {
	RS::GlobalShaderParameterType gvt = RS::GLOBAL_VAR_TYPE_MAX;
	return static_cast<ShaderLanguage::DataType>(RS::global_shader_uniform_type_get_shader_datatype(gvt));
}

String ShaderTranspilerGLES1::_dump_node_code(ShaderLanguage::Node *p_node, int p_level, ShaderCompiler::GeneratedCode &r_gen_code, ShaderCompiler::IdentifierActions &p_actions, const ShaderCompiler::DefaultIdentifierActions &p_default_actions, bool p_assigning, bool p_use_scope) {
	if (!p_node) {
		return "";
	}
	StringBuilder code;

	switch (p_node->type) {
		default: {
		} break;
		case ShaderLanguage::Node::NODE_TYPE_SHADER: {
			ShaderLanguage::ShaderNode *snode = (ShaderLanguage::ShaderNode *)p_node;

			for (int i = 0; i < snode->render_modes.size(); i++) {
				if (p_default_actions.render_mode_defines.has(snode->render_modes[i]) && !used_rmode_defines.has(snode->render_modes[i])) {
					r_gen_code.defines.push_back(p_default_actions.render_mode_defines[snode->render_modes[i]]);
					used_rmode_defines.insert(snode->render_modes[i]);
				}

				if (p_actions.render_mode_flags.has(snode->render_modes[i])) {
					*p_actions.render_mode_flags[snode->render_modes[i]] = true;
				}

				if (p_actions.render_mode_values.has(snode->render_modes[i])) {
					Pair<int *, int> &p = p_actions.render_mode_values[snode->render_modes[i]];
					*p.first = p.second;
				}
			}

			int max_texture_uniforms = 0;
			for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : snode->uniforms) {
				if (ShaderLanguage::is_sampler_type(E.value.type)) {
					max_texture_uniforms = MAX(max_texture_uniforms, E.value.texture_order + 1);
				}
			}
			r_gen_code.texture_uniforms.resize(max_texture_uniforms);

			StringBuilder vertex_global;
			StringBuilder fragment_global;
			StringBuilder uniform_string;

			for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : snode->uniforms) {
				StringBuffer<> uniform_code;

				ShaderLanguage::DataPrecision precision = E.value.precision;
				if (precision == ShaderLanguage::PRECISION_DEFAULT && E.value.type != ShaderLanguage::TYPE_BOOL) {
					precision = ShaderLanguage::PRECISION_HIGHP;
				}

				uniform_code += "uniform ";
				uniform_code += _prestr(precision);
				uniform_code += _typestr(E.value.type);
				uniform_code += " ";
				uniform_code += _mkid(E.key);
				uniform_code += ";\n";

				if (ShaderLanguage::is_sampler_type(E.value.type)) {
					ShaderCompiler::GeneratedCode::Texture tex;
					tex.name = E.key;
					tex.hint = E.value.hint;
					tex.type = E.value.type;
					tex.array_size = E.value.array_size;
					tex.global = E.value.scope == ShaderLanguage::ShaderNode::Uniform::SCOPE_GLOBAL;

					// Assign to the index so the 2D batcher knows where it is.
					r_gen_code.texture_uniforms.write[E.value.texture_order] = tex;
				}

				uniform_string += uniform_code.as_string();

				if (p_actions.uniforms) {
					p_actions.uniforms->insert(E.key, E.value);
				}
			}

			// Calculate UBO offsets.
			r_gen_code.uniform_offsets.clear();
			
			struct ScalarData {
				StringName name;
				ShaderLanguage::ShaderNode::Uniform uniform;
			};
			LocalVector<ScalarData> scalars;

			int max_scalar_uniforms = 0;
			for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Uniform> &E : snode->uniforms) {
				if (!ShaderLanguage::is_sampler_type(E.value.type)) {
					scalars.push_back({ E.key, E.value });
					max_scalar_uniforms = MAX(max_scalar_uniforms, E.value.order + 1);
				}
			}

			r_gen_code.uniform_offsets.resize(max_scalar_uniforms);
			r_gen_code.uniform_offsets.fill(0);

			// Sort by order
			struct UniformOrderComparator {
				bool operator()(const ScalarData &a, const ScalarData &b) const {
					return a.uniform.order < b.uniform.order;
				}
			};
			scalars.sort_custom<UniformOrderComparator>();

			int ubo_offset = 0;
			for (uint32_t i = 0; i < scalars.size(); i++) {
				ShaderLanguage::DataType type = scalars[i].uniform.type;
				int size = 4;
				int align = 4;

				// Map GLSL sizes to std140 alignment rules
				if (type == ShaderLanguage::TYPE_BVEC2 || type == ShaderLanguage::TYPE_IVEC2 || type == ShaderLanguage::TYPE_UVEC2 || type == ShaderLanguage::TYPE_VEC2) {
					size = 8;
					align = 8;
				} else if (type == ShaderLanguage::TYPE_BVEC3 || type == ShaderLanguage::TYPE_IVEC3 || type == ShaderLanguage::TYPE_UVEC3 || type == ShaderLanguage::TYPE_VEC3) {
					size = 12;
					align = 16;
				} else if (type == ShaderLanguage::TYPE_BVEC4 || type == ShaderLanguage::TYPE_IVEC4 || type == ShaderLanguage::TYPE_UVEC4 || type == ShaderLanguage::TYPE_VEC4) {
					size = 16;
					align = 16;
				} else if (type == ShaderLanguage::TYPE_MAT2) {
					size = 32;
					align = 16;
				} else if (type == ShaderLanguage::TYPE_MAT3) {
					size = 48;
					align = 16;
				} else if (type == ShaderLanguage::TYPE_MAT4) {
					size = 64;
					align = 16;
				}

				if (scalars[i].uniform.array_size > 0) {
					align = 16; // std140 arrays are always aligned to vec4
					size = align * scalars[i].uniform.array_size;
				}

				if (ubo_offset % align != 0) {
					ubo_offset += align - (ubo_offset % align);
				}

				r_gen_code.uniform_offsets.write[scalars[i].uniform.order] = ubo_offset;
				ubo_offset += size;
			}

			if (ubo_offset % 16 != 0) {
				ubo_offset += 16 - (ubo_offset % 16);
			}
			r_gen_code.uniform_total_size = ubo_offset;
			r_gen_code.uniforms = uniform_string.as_string();

			// varyings
			for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Varying> &E : snode->varyings) {
				StringBuffer<> varying_code;

				varying_code += "varying ";
				varying_code += _prestr(E.value.precision);
				varying_code += _typestr(E.value.type);
				varying_code += " ";
				varying_code += _mkid(E.key);
				if (E.value.array_size > 0) {
					varying_code += "[";
					varying_code += itos(E.value.array_size);
					varying_code += "]";
				}
				varying_code += ";\n";

				String final_code = varying_code.as_string();

				vertex_global += final_code;
				fragment_global += final_code;
			}

			// constants
			for (int i = 0; i < snode->vconstants.size(); i++) {
				String gcode;
				gcode += "const ";
				gcode += _prestr(snode->vconstants[i].precision);
				gcode += _typestr(snode->vconstants[i].type);
				gcode += " " + _mkid(String(snode->vconstants[i].name));
				gcode += "=";
				gcode += _dump_node_code(snode->vconstants[i].initializer, p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				gcode += ";\n";
				vertex_global += gcode;
				fragment_global += gcode;
			}

			// functions
			HashMap<StringName, String> function_code;

			for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Function> &E : snode->functions) {
				ShaderLanguage::FunctionNode *fnode = E.value.function;
				current_func_name = fnode->name;
				function_code[fnode->name] = _dump_node_code(fnode->body, 1, r_gen_code, p_actions, p_default_actions, p_assigning);
			}

			StringBuilder forward_decls;
			StringBuilder custom_functions;

			for (const KeyValue<StringName, ShaderLanguage::ShaderNode::Function> &E : snode->functions) {
				ShaderLanguage::FunctionNode *fnode = E.value.function;
				StringName name = fnode->name;

				// Skip the entry points.
				if (name == vertex_name || name == fragment_name || name == light_name) {
					continue;
				}

				StringBuffer<128> header;
				header += _typestr(fnode->return_type);
				header += " ";
				header += _mkid(name);
				header += "(";

				for (int i = 0; i < fnode->arguments.size(); i++) {
					if (i > 0) {
						header += ", ";
					}
					header += _qualstr(fnode->arguments[i].qualifier);
					header += _prestr(fnode->arguments[i].precision);
					header += _typestr(fnode->arguments[i].type);
					header += " ";
					header += _mkid(fnode->arguments[i].name);
				}
				header += ")";

				forward_decls += header.as_string();
				forward_decls += ";\n";

				custom_functions += header.as_string();
				custom_functions += "\n";
				custom_functions += function_code[name];
				custom_functions += "\n";
			}

			// Inject the custom functions into the global scope
			vertex_global += forward_decls.as_string();
			vertex_global += custom_functions.as_string();

			fragment_global += forward_decls.as_string();
			fragment_global += custom_functions.as_string();

			// Hook up the entry points
			if (snode->functions.has(vertex_name)) {
				r_gen_code.code["vertex"] = function_code[vertex_name];
			}
			if (snode->functions.has(fragment_name)) {
				r_gen_code.code["fragment"] = function_code[fragment_name];
			}
			if (snode->functions.has(light_name)) {
				r_gen_code.code["light"] = function_code[light_name];
			}

			r_gen_code.stage_globals[ShaderCompiler::STAGE_VERTEX] = vertex_global.as_string();
			r_gen_code.stage_globals[ShaderCompiler::STAGE_FRAGMENT] = fragment_global.as_string();

		} break;
		case ShaderLanguage::Node::NODE_TYPE_FUNCTION: {
		} break;
		case ShaderLanguage::Node::NODE_TYPE_BLOCK: {
			ShaderLanguage::BlockNode *bnode = (ShaderLanguage::BlockNode *)p_node;

			// Intercept for loop headers so they use commas,
			// no braces, and no trailing semicolons.
			bool is_for_header = (
				bnode->block_type == ShaderLanguage::BlockNode::BLOCK_TYPE_FOR_INIT ||
				bnode->block_type == ShaderLanguage::BlockNode::BLOCK_TYPE_FOR_CONDITION ||
				bnode->block_type == ShaderLanguage::BlockNode::BLOCK_TYPE_FOR_EXPRESSION
			);

			if (is_for_header || bnode->use_comma_between_statements) {
				for (int i = 0; bnode->statements.size() > i; ++i) {
					if (i > 0) {
						code += ", ";
					}
					code += _dump_node_code(bnode->statements.get(i), p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
			} else {
				// Standard block handling (loop bodies, if statements, etc.)
				if (!bnode->single_statement) {
					code += _mktab(p_level - 1);
					code += "{\n";
				}

				for (int i = 0; bnode->statements.size() > i; ++i) {
					String statement_code = _dump_node_code(bnode->statements.get(i), p_level, r_gen_code, p_actions, p_default_actions, p_assigning);

					if (bnode->statements.get(i)->type == ShaderLanguage::Node::NODE_TYPE_CONTROL_FLOW || bnode->single_statement) {
						code += statement_code;
					} else {
						code += _mktab(p_level);
						code += statement_code;
						code += ";\n";
					}
				}

				if (!bnode->single_statement) {
					code += _mktab(p_level - 1);
					code += "}\n";
				}
			}
		} break;
		case ShaderLanguage::Node::NODE_TYPE_VARIABLE_DECLARATION: {
			ShaderLanguage::VariableDeclarationNode *var_dec_node = (ShaderLanguage::VariableDeclarationNode *)p_node;

			StringBuffer<> declaration;
			if (var_dec_node->is_const) {
				declaration += "const ";
			}
			declaration += _prestr(var_dec_node->precision);

			// Godot 4 also supports custom structs, so we must check the datatype
			if (var_dec_node->datatype == ShaderLanguage::TYPE_STRUCT) {
				declaration += _mkid(var_dec_node->struct_name);
			} else {
				declaration += _typestr(var_dec_node->datatype);
			}

			for (int i = 0; i < var_dec_node->declarations.size(); i++) {
				if (i > 0) {
					declaration += ",";
				}

				declaration += " ";
				declaration += _mkid(var_dec_node->declarations[i].name);
				
				if (var_dec_node->declarations[i].size > 0 || var_dec_node->declarations[i].size_expression) {
					declaration += "[";
					if (var_dec_node->declarations[i].size_expression) {
						declaration += _dump_node_code(var_dec_node->declarations[i].size_expression, p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					} else {
						declaration += itos(var_dec_node->declarations[i].size);
					}
					declaration += "]";
				}

				if (!var_dec_node->declarations[i].initializer.is_empty()) {
					declaration += " = ";

					if (var_dec_node->declarations[i].single_expression) {
						// Standard assignment (e.g., float a = 1.0;)
						declaration += _dump_node_code(var_dec_node->declarations[i].initializer[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					} else {
						// Initializer list constructor (e.g., vec2(1.0, 2.0) or float[](1.0, 2.0))
						if (var_dec_node->datatype == ShaderLanguage::TYPE_STRUCT) {
							declaration += _mkid(var_dec_node->struct_name);
						} else {
							declaration += _typestr(var_dec_node->datatype);
						}

						if (var_dec_node->declarations[i].size > 0 || var_dec_node->declarations[i].size_expression) {
							declaration += "[]";
						}

						declaration += "(";
						for (int j = 0; j < var_dec_node->declarations[i].initializer.size(); j++) {
							if (j > 0) {
								declaration += ", ";
							}
							declaration += _dump_node_code(var_dec_node->declarations[i].initializer[j], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
						}
						declaration += ")";
					}
				}
			}

			code += declaration.as_string();
		} break;
		case ShaderLanguage::Node::NODE_TYPE_VARIABLE: {
			ShaderLanguage::VariableNode *var_node = (ShaderLanguage::VariableNode *)p_node;

			if (p_assigning && p_actions.write_flag_pointers.has(var_node->name)) {
				*p_actions.write_flag_pointers[var_node->name] = true;
			}

			if (p_default_actions.usage_defines.has(var_node->name) && !used_name_defines.has(var_node->name)) {
				String define = p_default_actions.usage_defines[var_node->name];
				String node_name = define.substr(1, define.length());

				if (define.begins_with("@")) {
					define = p_default_actions.usage_defines[node_name];
				}

				if (!used_name_defines.has(node_name)) {
					r_gen_code.defines.push_back(define);
				}
				used_name_defines.insert(var_node->name);
			}

			if (p_actions.usage_flag_pointers.has(var_node->name) && !used_flag_pointers.has(var_node->name)) {
				*p_actions.usage_flag_pointers[var_node->name] = true;
				used_flag_pointers.insert(var_node->name);
			}

			if (p_default_actions.renames.has(var_node->name)) {
				code += p_default_actions.renames[var_node->name];
			} else {
				code += _mkid(var_node->name);
			}

			if (var_node->name == time_name) {
				if (current_func_name == vertex_name) {
					r_gen_code.uses_vertex_time = true;
				}
				if (current_func_name == fragment_name || current_func_name == light_name) {
					r_gen_code.uses_fragment_time = true;
				}
			}
		} break;
		case ShaderLanguage::Node::NODE_TYPE_ARRAY: {
			ShaderLanguage::ArrayNode *arr_node = (ShaderLanguage::ArrayNode *)p_node;

			if (p_assigning && p_actions.write_flag_pointers.has(arr_node->name)) {
				*p_actions.write_flag_pointers[arr_node->name] = true;
			}

			if (p_default_actions.usage_defines.has(arr_node->name) && !used_name_defines.has(arr_node->name)) {
				String define = p_default_actions.usage_defines[arr_node->name];
				String node_name = define.substr(1, define.length());

				if (define.begins_with("@")) {
					define = p_default_actions.usage_defines[node_name];
				}

				if (!used_name_defines.has(node_name)) {
					r_gen_code.defines.push_back(define);
				}
				used_name_defines.insert(arr_node->name);
			}

			if (p_actions.usage_flag_pointers.has(arr_node->name) && !used_flag_pointers.has(arr_node->name)) {
				*p_actions.usage_flag_pointers[arr_node->name] = true;
				used_flag_pointers.insert(arr_node->name);
			}

			if (p_default_actions.renames.has(arr_node->name)) {
				code += p_default_actions.renames[arr_node->name];
			} else {
				code += _mkid(arr_node->name);
			}

			if (arr_node->call_expression != nullptr) {
				code += ".";
				code += _dump_node_code(arr_node->call_expression, p_level, r_gen_code, p_actions, p_default_actions, p_assigning, false);
			}

			if (arr_node->index_expression != nullptr) {
				code += "[";
				code += _dump_node_code(arr_node->index_expression, p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				code += "]";
			}

			if (arr_node->name == time_name) {
				if (current_func_name == vertex_name) {
					r_gen_code.uses_vertex_time = true;
				}
				if (current_func_name == fragment_name || current_func_name == light_name) {
					r_gen_code.uses_fragment_time = true;
				}
			}

		} break;
		case ShaderLanguage::Node::NODE_TYPE_CONSTANT: {
			ShaderLanguage::ConstantNode *const_node = (ShaderLanguage::ConstantNode *)p_node;

			return get_constant_text(const_node->datatype, const_node->values);
		} break;
		case ShaderLanguage::Node::NODE_TYPE_OPERATOR: {
			ShaderLanguage::OperatorNode *op_node = (ShaderLanguage::OperatorNode *)p_node;

			switch (op_node->op) {
				case ShaderLanguage::OP_ASSIGN:
				case ShaderLanguage::OP_ASSIGN_ADD:
				case ShaderLanguage::OP_ASSIGN_SUB:
				case ShaderLanguage::OP_ASSIGN_MUL:
				case ShaderLanguage::OP_ASSIGN_DIV:
				case ShaderLanguage::OP_ASSIGN_SHIFT_LEFT:
				case ShaderLanguage::OP_ASSIGN_SHIFT_RIGHT:
				case ShaderLanguage::OP_ASSIGN_BIT_AND:
				case ShaderLanguage::OP_ASSIGN_BIT_OR:
				case ShaderLanguage::OP_ASSIGN_BIT_XOR: {
					code += _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, true);
					code += " ";
					code += _opstr(op_node->op);
					code += " ";
					code += _dump_node_code(op_node->arguments[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				} break;

				case ShaderLanguage::OP_ASSIGN_MOD: {
					String a = _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					String n = _dump_node_code(op_node->arguments[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += a + " = " + n + " == 0 ? 0 : ";
					code += a + " - " + n + " * (" + a + " / " + n + ")";
				} break;
				case ShaderLanguage::OP_EMPTY: {
				} break;
				case ShaderLanguage::OP_BIT_INVERT:
				case ShaderLanguage::OP_NEGATE:
				case ShaderLanguage::OP_NOT:
				case ShaderLanguage::OP_DECREMENT:
				case ShaderLanguage::OP_INCREMENT: {
					if (op_node->op == ShaderLanguage::OP_INCREMENT) {
						code += "++";
					} else if (op_node->op == ShaderLanguage::OP_DECREMENT) {
						code += "--";
					} else {
						code += _opstr(op_node->op);
					}
					code += _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				} break;

				case ShaderLanguage::OP_POST_DECREMENT:
				case ShaderLanguage::OP_POST_INCREMENT: {
					code += _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					if (op_node->op == ShaderLanguage::OP_POST_INCREMENT) {
						code += "++";
					} else if (op_node->op == ShaderLanguage::OP_POST_DECREMENT) {
						code += "--";
					} else {
						code += _opstr(op_node->op);
					}
				} break;

				case ShaderLanguage::OP_CALL:
				case ShaderLanguage::OP_CONSTRUCT: {
					ERR_FAIL_COND_V(
						op_node->arguments[0]->type != ShaderLanguage::Node::NODE_TYPE_VARIABLE,
						String()
					);

					ShaderLanguage::VariableNode *var_node = (ShaderLanguage::VariableNode *)op_node->arguments[0];

					if (op_node->op == ShaderLanguage::OP_CONSTRUCT) {
						code += var_node->name;
					} else {
						if (var_node->name == "texture") {
							// emit texture call

							if (op_node->arguments[1]->get_datatype() == ShaderLanguage::TYPE_SAMPLER2D) { // ||
								//									op_node->arguments[1]->get_datatype() == ShaderLanguage::TYPE_SAMPLEREXT) {
								code += "texture2D";
							} else if (op_node->arguments[1]->get_datatype() == ShaderLanguage::TYPE_SAMPLERCUBE) {
								code += "textureCube";
							}

						} else if (var_node->name == "textureLod") {
							// emit texture call

							if (op_node->arguments[1]->get_datatype() == ShaderLanguage::TYPE_SAMPLER2D) {
								code += "texture2DLod";
							} else if (op_node->arguments[1]->get_datatype() == ShaderLanguage::TYPE_SAMPLERCUBE) {
								code += "textureCubeLod";
							}

						} else if (var_node->name == "mix") {
							switch (op_node->arguments[3]->get_datatype()) {
								case ShaderLanguage::TYPE_BVEC2: {
									code += "select2";
								} break;

								case ShaderLanguage::TYPE_BVEC3: {
									code += "select3";
								} break;

								case ShaderLanguage::TYPE_BVEC4: {
									code += "select4";
								} break;

								case ShaderLanguage::TYPE_VEC2:
								case ShaderLanguage::TYPE_VEC3:
								case ShaderLanguage::TYPE_VEC4:
								case ShaderLanguage::TYPE_FLOAT: {
									code += "mix";
								} break;

								default: {
									ShaderLanguage::DataType type = op_node->arguments[3]->get_datatype();
									// FIXME: Proper error print or graceful handling
									print_line(String("uhhhh invalid mix with type: ") + itos(type));
								} break;
							}

						} else if (p_default_actions.renames.has(var_node->name)) {
							code += p_default_actions.renames[var_node->name];
						} else if (internal_functions.has(var_node->name)) {
							code += var_node->name;
						} else {
							code += _mkid(var_node->name);
						}
					}

					code += "(";

					for (int i = 1; i < op_node->arguments.size(); i++) {
						if (i > 1) {
							code += ", ";
						}

						code += _dump_node_code(op_node->arguments[i], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					}

					code += ")";

					if (p_default_actions.usage_defines.has(var_node->name) && !used_name_defines.has(var_node->name)) {
						String define = p_default_actions.usage_defines[var_node->name];
						String node_name = define.substr(1, define.length());

						if (define.begins_with("@")) {
							define = p_default_actions.usage_defines[node_name];
						}

						if (!used_name_defines.has(node_name)) {
							r_gen_code.defines.push_back(define);
						}
						used_name_defines.insert(var_node->name);
					}

				} break;

				case ShaderLanguage::OP_INDEX: {
					code += _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += "[";
					code += _dump_node_code(op_node->arguments[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += "]";
				} break;

				case ShaderLanguage::OP_SELECT_IF: {
					code += "(";
					code += _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += " ? ";
					code += _dump_node_code(op_node->arguments[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += " : ";
					code += _dump_node_code(op_node->arguments[2], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += ")";
				} break;

				case ShaderLanguage::OP_MOD: {
					String a = _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					String n = _dump_node_code(op_node->arguments[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += "(" + n + " == 0 ? 0 : ";
					code += a + " - " + n + " * (" + a + " / " + n + "))";
				} break;

				default: {
					if (p_use_scope) {
						code += "(";
					}
					code += _dump_node_code(op_node->arguments[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					code += " ";
					code += _opstr(op_node->op);
					code += " ";
					code += _dump_node_code(op_node->arguments[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
					if (p_use_scope) {
						code += ")";
					}
				} break;
			}
		} break;
		case ShaderLanguage::Node::NODE_TYPE_CONTROL_FLOW: {
			ShaderLanguage::ControlFlowNode *cf_node = (ShaderLanguage::ControlFlowNode *)p_node;

			if (cf_node->flow_op == ShaderLanguage::FLOW_OP_IF) {
				code += _mktab(p_level);
				code += "if (";
				code += _dump_node_code(cf_node->expressions[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				code += ")\n";
				code += _dump_node_code(cf_node->blocks[0], p_level + 1, r_gen_code, p_actions, p_default_actions, p_assigning);

				if (cf_node->blocks.size() == 2) {
					code += _mktab(p_level);
					code += "else\n";
					code += _dump_node_code(cf_node->blocks[1], p_level + 1, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_DO) {
				code += _mktab(p_level);
				code += "do";
				code += _dump_node_code(cf_node->blocks[0], p_level + 1, r_gen_code, p_actions, p_default_actions, p_assigning);
				code += _mktab(p_level);
				code += "while (";
				code += _dump_node_code(cf_node->expressions[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				code += ");";
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_WHILE) {
				code += _mktab(p_level);
				code += "while (";
				code += _dump_node_code(cf_node->expressions[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				code += ")\n";
				code += _dump_node_code(cf_node->blocks[0], p_level + 1, r_gen_code, p_actions, p_default_actions, p_assigning);
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_FOR) {
				code += _mktab(p_level);
				code += "for (";

				// Initialization
				if (cf_node->blocks.size() > 0 && cf_node->blocks[0]) {
					code += _dump_node_code(cf_node->blocks[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
				code += "; ";

				// Condition
				if (cf_node->blocks.size() > 1 && cf_node->blocks[1]) {
					code += _dump_node_code(cf_node->blocks[1], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
				code += "; ";

				// Increment
				if (cf_node->blocks.size() > 2 && cf_node->blocks[2]) {
					code += _dump_node_code(cf_node->blocks[2], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
				code += ")\n";

				// Loop body
				if (cf_node->blocks.size() > 3 && cf_node->blocks[3]) {
					code += _dump_node_code(cf_node->blocks[3], p_level + 1, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_RETURN) {
				code += _mktab(p_level);
				code += "return";

				if (cf_node->expressions.size()) {
					code += " ";
					code += _dump_node_code(cf_node->expressions[0], p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
				}
				code += ";\n";
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_DISCARD) {
				if (p_actions.usage_flag_pointers.has("DISCARD") && !used_flag_pointers.has("DISCARD")) {
					*p_actions.usage_flag_pointers["DISCARD"] = true;
					used_flag_pointers.insert("DISCARD");
				}
				code += "discard;";
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_CONTINUE) {
				code += "continue;";
			} else if (cf_node->flow_op == ShaderLanguage::FLOW_OP_BREAK) {
				code += "break;";
			}
		} break;
		case ShaderLanguage::Node::NODE_TYPE_MEMBER: {
			ShaderLanguage::MemberNode *member_node = (ShaderLanguage::MemberNode *)p_node;
			code += _dump_node_code(member_node->owner, p_level, r_gen_code, p_actions, p_default_actions, p_assigning);
			code += ".";
			code += member_node->name;
		} break;
	}

	return code.as_string();
}

Error ShaderTranspilerGLES1::compile(RS::ShaderMode p_mode, const String &p_code, ShaderCompiler::IdentifierActions *p_actions, const String &p_path, ShaderCompiler::GeneratedCode &r_gen_code) {
	ShaderLanguage::ShaderCompileInfo com_info;

	com_info.global_shader_uniform_type_func = _get_variable_type;
	com_info.functions = ShaderTypes::get_singleton()->get_functions(p_mode);
	com_info.render_modes = ShaderTypes::get_singleton()->get_modes(p_mode);
	com_info.shader_types = ShaderTypes::get_singleton()->get_types();

	Error err = parser.compile(p_code, com_info);

	if (err != OK) {
		Vector<String> shader = p_code.split("\n");
		for (int i = 0; i < shader.size(); i++) {
			print_line(itos(i + 1) + " " + shader[i]);
		}

		_err_print_error(nullptr, p_path.utf8().get_data(), parser.get_error_line(), parser.get_error_text().utf8().get_data(), ERR_HANDLER_SHADER);
		return err;
	}

	r_gen_code.defines.clear();
	r_gen_code.uniforms = String();
	r_gen_code.texture_uniforms.clear();

	r_gen_code.code.clear();
	r_gen_code.stage_globals[ShaderCompiler::STAGE_VERTEX] = String();
	r_gen_code.stage_globals[ShaderCompiler::STAGE_FRAGMENT] = String();
	r_gen_code.stage_globals[ShaderCompiler::STAGE_COMPUTE] = String();

	r_gen_code.uses_fragment_time = false;
	r_gen_code.uses_vertex_time = false;
	r_gen_code.uses_screen_texture = false;
	r_gen_code.uses_screen_texture_mipmaps = false;

	used_name_defines.clear();
	used_rmode_defines.clear();
	used_flag_pointers.clear();

	_dump_node_code(parser.get_shader(), 1, r_gen_code, *p_actions, actions, false);

	return OK;
}

void ShaderTranspilerGLES1::initialize(const ShaderCompiler::DefaultIdentifierActions &p_actions) {
	actions = p_actions;

	vertex_name = "vertex";
	fragment_name = "fragment";
	light_name = "light";
	time_name = "TIME";

	List<String> func_list;
	ShaderLanguage::get_builtin_funcs(&func_list);

	for (const String &E : func_list) {
		internal_functions.insert(E);
	}

}

ShaderTranspilerGLES1::ShaderTranspilerGLES1() {
}

#endif // GLES1_ENABLED
