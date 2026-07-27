"""Functions used to generate source files during build time

All such functions are invoked in a subprocess on Windows to prevent build flakiness.

"""

import os.path

from typing import Optional
from methods import print_error, to_raw_cstring


class GLESHeaderStruct:
    def __init__(self):
        self.vertex_lines = []
        self.fragment_lines = []
        self.attributes = []
        self.uniforms = []
        self.fbos = []
        self.texunits = []
        self.texunit_names = []
        self.ubos = []
        self.ubo_names = []
        self.feedbacks = []

        self.vertex_included_files = []
        self.fragment_included_files = []

        self.reading = ""
        self.line_offset = 0
        self.vertex_offset = 0
        self.fragment_offset = 0
        self.variant_defines = []
        self.variant_names = []
        self.specialization_names = []
        self.specialization_values = []
        
        # TODO(GLES2): The engine pre-populates these at runtime, meaning
        # that the builder doesn't have these available when building the
        # shaders.
        # Having them here works, but now we must sync between:
        # - servers/rendering/renderer_scene_render.h
        # - servers/rendering_server.cpp
        # - drivers/gles2/rasterizer_scene_gles2.cpp (for the runtime defines)
        self.constants = {
            "MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS": 8,
            "MAX_FORWARD_LIGHTS": 8, # up to 1024, default 8

            # The actual value is 32, up to 256,
            # though I don't think we will use them all in GLES2.
            "MAX_LIGHT_DATA_STRUCTS": 8,
        }
        self.structs = {}
        self.current_struct = None


def include_file_in_gles_header(filename: str, header_data: GLESHeaderStruct, depth: int):
    fs = open(filename, "r")
    line = fs.readline()

    while line:
        line_stripped = line.strip()

        # Parse inline defines
        if line_stripped.startswith("#define "):
            parts = line_stripped.split()
            if len(parts) >= 3:
                header_data.constants[parts[1]] = parts[2]

        if line_stripped.startswith("struct ") and line_stripped.find("{") != -1:
            struct_name = line_stripped.replace("struct ", "").split("{")[0].strip()
            header_data.structs[struct_name] = []
            header_data.current_struct = struct_name
        elif header_data.current_struct is not None:
            if line_stripped.startswith("}"):
                header_data.current_struct = None
            elif line_stripped.endswith(";"):
                member_line = line_stripped.replace(";", "").strip()
                parts = member_line.split()
                if len(parts) >= 2:
                    m_type = parts[-2]
                    m_name = parts[-1]
                    m_arr_size = 0
                    if "[" in m_name:
                        size_str = m_name[m_name.find("[")+1:m_name.find("]")]
                        m_name = m_name[:m_name.find("[")]
                        if size_str in header_data.constants:
                            size_str = header_data.constants[size_str]
                        try:
                            m_arr_size = int(size_str)
                        except ValueError:
                            m_arr_size = 1
                    header_data.structs[header_data.current_struct].append((m_type, m_name, m_arr_size))

        if line.find("=") != -1 and header_data.reading == "":
            # Mode
            eqpos = line.find("=")
            defname = line[:eqpos].strip().upper()
            define = line[eqpos + 1 :].strip()
            header_data.variant_names.append(defname)
            header_data.variant_defines.append(define)
            line = fs.readline()
            header_data.line_offset += 1
            header_data.vertex_offset = header_data.line_offset
            continue

        if line.find("=") != -1 and header_data.reading == "specializations":
            # Specialization
            eqpos = line.find("=")
            specname = line[:eqpos].strip()
            specvalue = line[eqpos + 1 :]
            header_data.specialization_names.append(specname)
            header_data.specialization_values.append(specvalue)
            line = fs.readline()
            header_data.line_offset += 1
            header_data.vertex_offset = header_data.line_offset
            continue

        if line.find("#[modes]") != -1:
            # Nothing really, just skip
            line = fs.readline()
            header_data.line_offset += 1
            header_data.vertex_offset = header_data.line_offset
            continue

        if line.find("#[specializations]") != -1:
            header_data.reading = "specializations"
            line = fs.readline()
            header_data.line_offset += 1
            header_data.vertex_offset = header_data.line_offset
            continue

        if line.find("#[vertex]") != -1:
            header_data.reading = "vertex"
            line = fs.readline()
            header_data.line_offset += 1
            header_data.vertex_offset = header_data.line_offset
            continue

        if line.find("#[fragment]") != -1:
            header_data.reading = "fragment"
            line = fs.readline()
            header_data.line_offset += 1
            header_data.fragment_offset = header_data.line_offset
            continue

        while line.find("#include ") != -1:
            includeline = line.replace("#include ", "").strip()[1:-1]

            dir = os.path.dirname(filename)
            if dir == "":
                dir = "."

            included_file = os.path.relpath(dir + "/" + includeline)
            if not included_file in header_data.vertex_included_files and header_data.reading == "vertex":
                header_data.vertex_included_files += [included_file]
                if include_file_in_gles_header(included_file, header_data, depth + 1) is None:
                    print_error("Error in file '" + filename + "': #include " + includeline + "could not be found!")
            elif not included_file in header_data.fragment_included_files and header_data.reading == "fragment":
                header_data.fragment_included_files += [included_file]
                if include_file_in_gles_header(included_file, header_data, depth + 1) is None:
                    print_error("Error in file '" + filename + "': #include " + includeline + "could not be found!")

            line = fs.readline()

        if line.find("uniform") != -1 and line.lower().find("texunit:") != -1:
            # texture unit
            texunitstr = line[line.find(":") + 1 :].strip()
            if texunitstr == "auto":
                texunit = "-1"
            else:
                texunit = str(int(texunitstr))
            uline = line[: line.lower().find("//")]
            uline = uline.replace("uniform", "")
            uline = uline.replace("highp", "")
            uline = uline.replace(";", "")
            lines = uline.split(",")
            for x in lines:
                x = x.strip()
                x = x[x.rfind(" ") + 1 :]
                if x.find("[") != -1:
                    # unfiorm array
                    x = x[: x.find("[")]

                if not x in header_data.texunit_names:
                    header_data.texunits += [(x, texunit)]
                    header_data.texunit_names += [x]

        elif line.find("uniform") != -1 and line.lower().find("ubo:") != -1:
            # uniform buffer object
            ubostr = line[line.find(":") + 1 :].strip()
            ubo = str(int(ubostr))
            uline = line[: line.lower().find("//")]
            uline = uline[uline.find("uniform") + len("uniform") :]
            uline = uline.replace("highp", "")
            uline = uline.replace(";", "")
            uline = uline.replace("{", "").strip()
            lines = uline.split(",")
            for x in lines:
                x = x.strip()
                x = x[x.rfind(" ") + 1 :]
                if x.find("[") != -1:
                    # unfiorm array
                    x = x[: x.find("[")]

                if not x in header_data.ubo_names:
                    header_data.ubos += [(x, ubo)]
                    header_data.ubo_names += [x]

        elif line.find("uniform") != -1 and line.find("{") == -1 and line.find(";") != -1:
            uline = line.replace("uniform", "").replace(";", "").strip()
            parts = uline.split()
            if len(parts) >= 2:
                type_name = parts[0]
                if type_name in ["highp", "mediump", "lowp"] and len(parts) >= 3:
                    type_name = parts[1]
                
                type_name_idx = uline.find(type_name) + len(type_name)
                vars_str = uline[type_name_idx:]
                var_lines = vars_str.split(",")
                
                for x in var_lines:
                    var_name = x.strip()
                    arr_size = 0
                    if var_name.find("[") != -1:
                        size_str = var_name[var_name.find("[")+1:var_name.find("]")]
                        var_name = var_name[:var_name.find("[")]
                        if size_str in header_data.constants:
                            size_str = header_data.constants[size_str]
                        try:
                            arr_size = int(size_str)
                        except ValueError:
                            arr_size = 1
                            
                    if type_name in header_data.structs:
                        def unroll_struct(s_type, prefix, s_dict, out_uniforms):
                            if s_type not in s_dict:
                                if prefix not in out_uniforms:
                                    out_uniforms.append(prefix)
                                return
                            for m_type, m_name, m_size in s_dict[s_type]:
                                if m_size > 0:
                                    for i in range(m_size):
                                        unroll_struct(m_type, f"{prefix}.{m_name}[{i}]", s_dict, out_uniforms)
                                else:
                                    unroll_struct(m_type, f"{prefix}.{m_name}", s_dict, out_uniforms)
                        
                        if arr_size > 0:
                            for i in range(arr_size):
                                unroll_struct(type_name, f"{var_name}[{i}]", header_data.structs, header_data.uniforms)
                        else:
                            unroll_struct(type_name, var_name, header_data.structs, header_data.uniforms)
                    else:
                        if var_name not in header_data.uniforms:
                            header_data.uniforms.append(var_name)

        if line.strip().find("attribute ") == 0 and line.find("attrib:") != -1:
            uline = line.replace("in ", "")
            uline = uline.replace("attribute ", "")
            uline = uline.replace("highp ", "")
            uline = uline.replace(";", "")
            uline = uline[uline.find(" ") :].strip()

            if uline.find("//") != -1:
                name, bind = uline.split("//")
                if bind.find("attrib:") != -1:
                    name = name.strip()
                    bind = bind.replace("attrib:", "").strip()
                    header_data.attributes += [(name, bind)]

        if (line.strip().find("out ") == 0 or line.strip().find("flat ") == 0 or line.strip().find("varying ") == 0) and line.find("tfb:") != -1:
            uline = line.replace("flat ", "")
            uline = uline.replace("out ", "")
            uline = uline.replace("varying ", "")
            uline = uline.replace("highp ", "")
            uline = uline.replace(";", "")
            uline = uline[uline.find(" ") :].strip()

            if uline.find("//") != -1:
                name, bind = uline.split("//")
                if bind.find("tfb:") != -1:
                    name = name.strip()
                    bind = bind.replace("tfb:", "").strip()
                    header_data.feedbacks += [(name, bind)]

        line = line.replace("\r", "")
        line = line.replace("\n", "")

        if header_data.reading == "vertex":
            header_data.vertex_lines += [line]
        if header_data.reading == "fragment":
            header_data.fragment_lines += [line]

        line = fs.readline()
        header_data.line_offset += 1

    fs.close()

    return header_data


def build_gles_header(
    filename: str,
    include: str,
    class_suffix: str,
    optional_output_filename: str = None,
    header_data: Optional[GLESHeaderStruct] = None,
    gles_version: int = 3
):
    header_data = header_data or GLESHeaderStruct()
    include_file_in_gles_header(filename, header_data, 0)

    if optional_output_filename is None:
        out_file = filename + ".gen.h"
    else:
        out_file = optional_output_filename

    fd = open(out_file, "w")
    defspec = 0
    defvariant = ""

    fd.write("/* WARNING, THIS FILE WAS GENERATED, DO NOT EDIT */\n")

    out_file_base = out_file
    out_file_base = out_file_base[out_file_base.rfind("/") + 1 :]
    out_file_base = out_file_base[out_file_base.rfind("\\") + 1 :]
    out_file_ifdef = out_file_base.replace(".", "_").upper()
    fd.write("#ifndef " + out_file_ifdef + class_suffix + "_GLES" + str(gles_version) + "\n")
    fd.write("#define " + out_file_ifdef + class_suffix + "_GLES" + str(gles_version) + "\n")

    out_file_class = (
        out_file_base.replace(".glsl.gen.h", "").title().replace("_", "").replace(".", "") + "Shader" + class_suffix
    )
    fd.write("\n\n")
    fd.write('#include "' + include + '"\n\n\n')
    fd.write("class " + out_file_class + " : public Shader" + class_suffix + " {\n\n")

    fd.write("public:\n\n")

    if header_data.uniforms:
        fd.write("\tenum Uniforms {\n")
        for x in header_data.uniforms:
            enum_name = x.replace(".", "_").replace("[", "_").replace("]", "").upper()
            fd.write("\t\t" + enum_name + ",\n")
        fd.write("\t};\n\n")

    if header_data.variant_names:
        fd.write("\tenum ShaderVariant {\n")
        for x in header_data.variant_names:
            fd.write("\t\t" + x + ",\n")
        fd.write("\t};\n\n")
    else:
        fd.write("\tenum ShaderVariant { DEFAULT };\n\n")
        defvariant = "=DEFAULT"

    if header_data.specialization_names:
        fd.write("\tenum Specializations {\n")
        counter = 0
        for x in header_data.specialization_names:
            fd.write("\t\t" + x.upper() + "=" + str(1 << counter) + ",\n")
            counter += 1
        fd.write("\t};\n\n")

    for i in range(len(header_data.specialization_names)):
        defval = header_data.specialization_values[i].strip()
        if defval.upper() == "TRUE" or defval == "1":
            defspec |= 1 << i

    fd.write(
        "\t_FORCE_INLINE_ bool version_bind_shader(RID p_version,ShaderVariant p_variant"
        + defvariant
        + ",uint64_t p_specialization="
        + str(defspec)
        + ") { return _version_bind_shader(p_version,p_variant,p_specialization); }\n\n"
    )

    if header_data.uniforms:
        fd.write(
            "\t_FORCE_INLINE_ int version_get_uniform(Uniforms p_uniform,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { return _version_get_uniform(p_uniform,p_version,p_variant,p_specialization); }\n\n"
        )

        fd.write(
            "\t#define _FU if (version_get_uniform(p_uniform,p_version,p_variant,p_specialization)<0) return; \n\n "
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform1f(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, double p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform1f(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
        )
        if gles_version >= 3:
            fd.write(
                "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, uint8_t p_value,RID p_version,ShaderVariant p_variant"
                + defvariant
                + ",uint64_t p_specialization="
                + str(defspec)
                + ") { _FU glUniform1ui(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
            )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, int8_t p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform1i(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
        )
        if gles_version >= 3:
            fd.write(
                "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, uint16_t p_value,RID p_version,ShaderVariant p_variant"
                + defvariant
                + ",uint64_t p_specialization="
                + str(defspec)
                + ") { _FU glUniform1ui(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
            )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, int16_t p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform1i(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
        )
        if gles_version >= 3:
            fd.write(
                "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, uint32_t p_value,RID p_version,ShaderVariant p_variant"
                + defvariant
                + ",uint64_t p_specialization="
                + str(defspec)
                + ") { _FU glUniform1ui(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
            )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, int32_t p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform1i(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_value); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Color& p_color,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU GLfloat col[4]={p_color.r,p_color.g,p_color.b,p_color.a}; glUniform4fv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,col); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Vector2& p_vec2,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU GLfloat vec2[2]={float(p_vec2.x),float(p_vec2.y)}; glUniform2fv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,vec2); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Size2i& p_vec2,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU GLint vec2[2]={GLint(p_vec2.x),GLint(p_vec2.y)}; glUniform2iv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,vec2); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Vector3& p_vec3,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU GLfloat vec3[3]={float(p_vec3.x),float(p_vec3.y),float(p_vec3.z)}; glUniform3fv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,vec3); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Vector4& p_vec4,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU GLfloat vec4[4]={float(p_vec4.x),float(p_vec4.y),float(p_vec4.z),float(p_vec4.w)}; glUniform4fv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,vec4); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_a, float p_b,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform2f(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_a,p_b); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_a, float p_b, float p_c,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform3f(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_a,p_b,p_c); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_a, float p_b, float p_c, float p_d,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _FU glUniform4f(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),p_a,p_b,p_c,p_d); }\n\n"
        )

        fd.write(
            """\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Transform3D& p_transform,RID p_version,ShaderVariant p_variant"""
            + defvariant
            + """,uint64_t p_specialization="""
            + str(defspec)
            + """) {  _FU

            const Transform3D &tr = p_transform;

            GLfloat matrix[16]={ /* build a 16x16 matrix */
                (GLfloat)tr.basis.rows[0][0],
                (GLfloat)tr.basis.rows[1][0],
                (GLfloat)tr.basis.rows[2][0],
                (GLfloat)0,
                (GLfloat)tr.basis.rows[0][1],
                (GLfloat)tr.basis.rows[1][1],
                (GLfloat)tr.basis.rows[2][1],
                (GLfloat)0,
                (GLfloat)tr.basis.rows[0][2],
                (GLfloat)tr.basis.rows[1][2],
                (GLfloat)tr.basis.rows[2][2],
                (GLfloat)0,
                (GLfloat)tr.origin.x,
                (GLfloat)tr.origin.y,
                (GLfloat)tr.origin.z,
                (GLfloat)1
            };

                    glUniformMatrix4fv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,false,matrix);

        }

        """
        )

        fd.write(
            """_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Transform2D& p_transform,RID p_version,ShaderVariant p_variant"""
            + defvariant
            + """,uint64_t p_specialization="""
            + str(defspec)
            + """) {  _FU

            const Transform2D &tr = p_transform;

        GLfloat matrix[16]={ /* build a 16x16 matrix */
            (GLfloat)tr.columns[0][0],
            (GLfloat)tr.columns[0][1],
            (GLfloat)0,
            (GLfloat)0,
            (GLfloat)tr.columns[1][0],
            (GLfloat)tr.columns[1][1],
            (GLfloat)0,
            (GLfloat)0,
            (GLfloat)0,
            (GLfloat)0,
            (GLfloat)1,
            (GLfloat)0,
            (GLfloat)tr.columns[2][0],
            (GLfloat)tr.columns[2][1],
            (GLfloat)0,
            (GLfloat)1
        };

            glUniformMatrix4fv(version_get_uniform(p_uniform,p_version,p_variant,p_specialization),1,false,matrix);

        }

        """
        )

        fd.write(
            """_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Projection& p_matrix, RID p_version, ShaderVariant p_variant"""
            + defvariant
            + """,uint64_t p_specialization="""
            + str(defspec)
            + """) {  _FU

            GLfloat matrix[16];

            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    matrix[i * 4 + j] = p_matrix.columns[i][j];
                }
            }

            glUniformMatrix4fv(version_get_uniform(p_uniform, p_version, p_variant, p_specialization), 1, false, matrix);
    }"""
        )

        fd.write("\n\n#undef _FU\n\n\n")

    fd.write("protected:\n\n")

    fd.write("\tvirtual void _init() override {\n\n")

    if header_data.uniforms:
        fd.write("\t\tstatic const char* _uniform_strings[]={\n")
        if header_data.uniforms:
            for x in header_data.uniforms:
                fd.write('\t\t\t"' + x + '",\n')
        fd.write("\t\t};\n\n")
    else:
        fd.write("\t\tstatic const char **_uniform_strings=nullptr;\n")

    variant_count = 1
    if len(header_data.variant_defines) > 0:
        fd.write("\t\tstatic const char* _variant_defines[]={\n")
        for x in header_data.variant_defines:
            fd.write('\t\t\t"' + x + '",\n')
        fd.write("\t\t};\n\n")
        variant_count = len(header_data.variant_defines)
    else:
        fd.write("\t\tstatic const char **_variant_defines=nullptr;\n")

    if header_data.texunits:
        fd.write("\t\tstatic TexUnitPair _texunit_pairs[]={\n")
        for x in header_data.texunits:
            fd.write('\t\t\t{"' + x[0] + '",' + x[1] + "},\n")
        fd.write("\t\t};\n\n")
    else:
        fd.write("\t\tstatic TexUnitPair *_texunit_pairs=nullptr;\n")

    # GLES2 does not support UBOs.
#     if header_data.ubos and gles_version >= 3:
#         fd.write("\t\tstatic UBOPair _ubo_pairs[]={\n")
#         for x in header_data.ubos:
#             fd.write('\t\t\t{"' + x[0] + '",' + x[1] + "},\n")
#         fd.write("\t\t};\n\n")
#     else:
#
#         fd.write("\t\tstatic UBOPair *_ubo_pairs=nullptr;\n")

    specializations_found = []

    if header_data.specialization_names:
        fd.write("\t\tstatic Specialization _spec_pairs[]={\n")
        for i in range(len(header_data.specialization_names)):
            defval = header_data.specialization_values[i].strip()
            if defval.upper() == "TRUE" or defval == "1":
                defval = "true"
            else:
                defval = "false"

            fd.write('\t\t\t{"' + header_data.specialization_names[i] + '",' + defval + "},\n")
            specializations_found.append(header_data.specialization_names[i])
        fd.write("\t\t};\n\n")
    else:
        fd.write("\t\tstatic Specialization *_spec_pairs=nullptr;\n")

    if gles_version <= 2:
        if header_data.attributes:
            fd.write("\t\tstatic AttributePair _attribute_pairs[]={\n")
            for x in header_data.attributes:
                fd.write('\t\t\t{"' + x[0] + '",' + x[1] + "},\n")
            fd.write("\t\t};\n\n")
        else:
            fd.write("\t\tstatic AttributePair *_attribute_pairs=nullptr;\n")

    feedback_count = 0
    if header_data.feedbacks:
        fd.write("\t\tstatic const Feedback _feedbacks[]={\n")
        for x in header_data.feedbacks:
            name = x[0]
            spec = x[1]
            if spec in specializations_found:
                fd.write('\t\t\t{"' + name + '",' + str(1 << specializations_found.index(spec)) + "},\n")
            else:
                fd.write('\t\t\t{"' + name + '",0},\n')

            feedback_count += 1

        fd.write("\t\t};\n\n")
    else:
        fd.write("\t\tstatic const Feedback* _feedbacks=nullptr;\n")

    readable_vert = "\n".join(header_data.vertex_lines).replace("*/", "* /")
    fd.write(f"/*\n=== VERTEX CODE ===\n{readable_vert}\n=============================\n*/\n")
    fd.write("\t\tstatic const char _vertex_code[]={\n")
    fd.write(to_byte_array(header_data.vertex_lines))
    fd.write("\n\t\t};\n\n")

    readable_frag = "\n".join(header_data.fragment_lines).replace("*/", "* /")
    fd.write(f"/*\n=== FRAGMENT CODE ===\n{readable_frag}\n===============================\n*/\n")
    fd.write("\t\tstatic const char _fragment_code[]={\n")
    fd.write(to_byte_array(header_data.fragment_lines))
    fd.write("\n\t\t};\n\n")

    if gles_version >= 3:
        fd.write(
            '\t\t_setup(_vertex_code,_fragment_code,"'
            + out_file_class
            + '",'
            + str(len(header_data.uniforms))
            + ",_uniform_strings,"
            + str(len(header_data.ubos))
            + ",_ubo_pairs,"
            + str(feedback_count)
            + ",_feedbacks,"
            + str(len(header_data.texunits))
            + ",_texunit_pairs,"
            + str(len(header_data.specialization_names))
            + ",_spec_pairs,"
            + str(variant_count)
            + ",_variant_defines);\n"
        )
    else:
        fd.write(
            '\t\t_setup(_vertex_code,_fragment_code,"'
            + out_file_class
            + '",'
            + str(len(header_data.uniforms))
            + ",_uniform_strings,"
            + str(len(header_data.attributes))
            + ",_attribute_pairs,"
            + str(feedback_count)
            + ",_feedbacks,"
            + str(len(header_data.texunits))
            + ",_texunit_pairs,"
            + str(len(header_data.specialization_names))
            + ",_spec_pairs,"
            + str(variant_count)
            + ",_variant_defines);\n"
        )

    fd.write("\t}\n\n")

    fd.write("};\n\n")
    fd.write("#endif\n\n")
    fd.close()


def build_gles2_headers(target, source, env):
    for t, s in zip(target, source):
        out_path = str(t)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        build_gles_header(
            filename=str(s), 
            include="drivers/gles2/compiler/shader_gles2.h", 
            class_suffix="GLES2", 
            optional_output_filename=out_path, 
            gles_version=2
        )


def to_byte_array(lines):
    # Join lines and explicitly add the null terminator 
    raw_str = "\n".join(lines) + "\0"
    return ", ".join(str(b) for b in raw_str.encode("utf-8"))