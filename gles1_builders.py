"""Functions used to generate source files during build time

All such functions are invoked in a subprocess on Windows to prevent build flakiness.

Unlike the other builders, this takes the modern GLSL Godot expects
and translates it heuristically to the required C++ state machine
needed
"""

import os.path

from typing import Optional
from methods import print_error, to_raw_cstring

GL_1_5_MAPPINGS = {
    "projection_matrix": (
        "glMatrixMode(GL_PROJECTION);\n"
        "\t\t\t\tProjection p = p_value;\n"
        "\t\t\t\tGLfloat matrix[16]={};\n"
        "\t\t\t\tfor (int i = 0; i < 4; i++) {\n"
        "\t\t\t\t\tfor (int j = 0; j < 4; j++) {\n"
        "\t\t\t\t\t\tmatrix[i * 4 + j] = p.columns[i][j];\n"
        "\t\t\t\t\t}\n"
        "\t\t\t\t}\n"
        "\t\t\t\tglLoadMatrixf(matrix);\n"
        "\t\t\t\tglMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: projection_matrix\");"
    ),
    "modelview_matrix": (
        "glMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tGLfloat matrix[16]={};\n"
        "\t\t\t\tif (p_value.get_type() == Variant::TRANSFORM2D) {\n"
        "\t\t\t\t\tTransform2D tr = p_value;\n"
        "\t\t\t\t\tmatrix[0]=tr.columns[0][0]; matrix[1]=tr.columns[0][1]; matrix[2]=0; matrix[3]=0;\n"
        "\t\t\t\t\tmatrix[4]=tr.columns[1][0]; matrix[5]=tr.columns[1][1]; matrix[6]=0; matrix[7]=0;\n"
        "\t\t\t\t\tmatrix[8]=0; matrix[9]=0; matrix[10]=1; matrix[11]=0;\n"
        "\t\t\t\t\tmatrix[12]=tr.columns[2][0]; matrix[13]=tr.columns[2][1]; matrix[14]=0; matrix[15]=1;\n"
        "\t\t\t\t} else {\n"
        "\t\t\t\t\tTransform3D tr = p_value;\n"
        "\t\t\t\t\tmatrix[0]=tr.basis.rows[0][0]; matrix[1]=tr.basis.rows[1][0]; matrix[2]=tr.basis.rows[2][0]; matrix[3]=0;\n"
        "\t\t\t\t\tmatrix[4]=tr.basis.rows[0][1]; matrix[5]=tr.basis.rows[1][1]; matrix[6]=tr.basis.rows[2][1]; matrix[7]=0;\n"
        "\t\t\t\t\tmatrix[8]=tr.basis.rows[0][2]; matrix[9]=tr.basis.rows[1][2]; matrix[10]=tr.basis.rows[2][2]; matrix[11]=0;\n"
        "\t\t\t\t\tmatrix[12]=tr.origin.x; matrix[13]=tr.origin.y; matrix[14]=tr.origin.z; matrix[15]=1;\n"
        "\t\t\t\t}\n"
        "\t\t\t\tglLoadMatrixf(matrix);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: modelview_matrix\");"
    ),
    "extra_matrix": ( # Godot canvas applies extra_matrix after modelview
        "glMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tGLfloat matrix[16]={};\n"
        "\t\t\t\tif (p_value.get_type() == Variant::TRANSFORM2D) {\n"
        "\t\t\t\t\tTransform2D tr = p_value;\n"
        "\t\t\t\t\tmatrix[0]=tr.columns[0][0]; matrix[1]=tr.columns[0][1]; matrix[2]=0; matrix[3]=0;\n"
        "\t\t\t\t\tmatrix[4]=tr.columns[1][0]; matrix[5]=tr.columns[1][1]; matrix[6]=0; matrix[7]=0;\n"
        "\t\t\t\t\tmatrix[8]=0; matrix[9]=0; matrix[10]=1; matrix[11]=0;\n"
        "\t\t\t\t\tmatrix[12]=tr.columns[2][0]; matrix[13]=tr.columns[2][1]; matrix[14]=0; matrix[15]=1;\n"
        "\t\t\t\t} else {\n"
        "\t\t\t\t\tTransform3D tr = p_value;\n"
        "\t\t\t\t\tmatrix[0]=tr.basis.rows[0][0]; matrix[1]=tr.basis.rows[1][0]; matrix[2]=tr.basis.rows[2][0]; matrix[3]=0;\n"
        "\t\t\t\t\tmatrix[4]=tr.basis.rows[0][1]; matrix[5]=tr.basis.rows[1][1]; matrix[6]=tr.basis.rows[2][1]; matrix[7]=0;\n"
        "\t\t\t\t\tmatrix[8]=tr.basis.rows[0][2]; matrix[9]=tr.basis.rows[1][2]; matrix[10]=tr.basis.rows[2][2]; matrix[11]=0;\n"
        "\t\t\t\t\tmatrix[12]=tr.origin.x; matrix[13]=tr.origin.y; matrix[14]=tr.origin.z; matrix[15]=1;\n"
        "\t\t\t\t}\n"
        "\t\t\t\tglMultMatrixf(matrix);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: extra_matrix\");"
    ),
    "modulate": (
        "Color c = p_value;\n"
        "\t\t\t\tglColor4f(c.r, c.g, c.b, c.a);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: modulate\");"
    ),
    "final_modulate": (
        "Color c = p_value;\n"
        "\t\t\t\tglColor4f(c.r, c.g, c.b, c.a);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: final_modulate\");"
    ),
    "color_in": (
        "Color c = p_value;\n"
        "\t\t\t\tglColor4f(c.r, c.g, c.b, c.a);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: color_in\");"
    ),
    "point_size": (
        "glPointSize((float)p_value);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: point_size\");"
    ),
    
    # copy.glsl specific overrides
    "copy_section": (
        "Vector4 cs = p_value;\n"
        "\t\t\t\tglMatrixMode(GL_PROJECTION);\n"
        "\t\t\t\tglLoadIdentity();\n"
        "\t\t\t\tglMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tglLoadIdentity();\n"
        "\t\t\t\t// copy_section is x,y,w,h in 0-1. Map -1..1 NDC to this rect.\n"
        "\t\t\t\tglTranslatef(-1.0f + (cs.x * 2.0f) + cs.z, -1.0f + (cs.y * 2.0f) + cs.w, 0.0f);\n"
        "\t\t\t\tglScalef(cs.z, cs.w, 1.0f);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: copy_section\");"
    ),
    "source_section": (
        "Vector4 ss = p_value;\n"
        "\t\t\t\tglMatrixMode(GL_TEXTURE);\n"
        "\t\t\t\tglLoadIdentity();\n"
        "\t\t\t\tglTranslatef(ss.x, ss.y, 0.0f);\n"
        "\t\t\t\tglScalef(ss.z, ss.w, 1.0f);\n"
        "\t\t\t\tglMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: source_section\");"
    ),
    "dst_rect": (
        "Rect2 dr;\n"
        "\t\t\t\tif (p_value.get_type() == Variant::RECT2) { dr = p_value; }\n"
        "\t\t\t\telse { Color c = p_value; dr = Rect2(c.r, c.g, c.b, c.a); }\n"
        "\t\t\t\tglMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tglTranslatef(dr.position.x, dr.position.y, 0.0f);\n"
        "\t\t\t\tglScalef(Math::abs(dr.size.x), Math::abs(dr.size.y), 1.0f);\n"
        "\t\t\t\tif (dr.size.x < 0) {\n"
        "\t\t\t\t\t// Emulate shader 'vertex.xy = vertex.yx' via local matrix swap\n"
        "\t\t\t\t\tGLfloat transpose[16] = {0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1};\n"
        "\t\t\t\t\tglMultMatrixf(transpose);\n"
        "\t\t\t\t}\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: dst_rect\");"
    ),
    "src_rect": (
        "Rect2 sr;\n"
        "\t\t\t\tif (p_value.get_type() == Variant::RECT2) { sr = p_value; }\n"
        "\t\t\t\telse { Color c = p_value; sr = Rect2(c.r, c.g, c.b, c.a); }\n"
        "\t\t\t\tglMatrixMode(GL_TEXTURE);\n"
        "\t\t\t\tglLoadIdentity();\n"
        "\t\t\t\tglTranslatef(sr.position.x, sr.position.y, 0.0f);\n"
        "\t\t\t\tglScalef(sr.size.x, sr.size.y, 1.0f);\n"
        "\t\t\t\tglMatrixMode(GL_MODELVIEW);\n"
        "\t\t\t\tGL_CHECK_ERROR(\"ShaderGLES1::_apply_gles1_state: src_rect\");"
    ),
}

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


def include_file_in_gles_header(filename: str, header_data: GLESHeaderStruct, depth: int):
    fs = open(filename, "r")
    line = fs.readline()

    while line:
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
            uline = line.replace("uniform", "")
            uline = uline.replace(";", "")
            lines = uline.split(",")
            for x in lines:
                x = x.strip()
                x = x[x.rfind(" ") + 1 :]
                if x.find("[") != -1:
                    # unfiorm array
                    x = x[: x.find("[")]

                if not x in header_data.uniforms:
                    header_data.uniforms += [x]

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

        if (line.strip().find("out ") == 0 or line.strip().find("flat ") == 0) and line.find("tfb:") != -1:
            uline = line.replace("flat ", "")
            uline = uline.replace("out ", "")
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
    fd.write('#include "' + include + '"\n')
    fd.write('#include "drivers/gles_common/error_macros.h"' + '\n\n\n') # for error checking
    fd.write("class " + out_file_class + " : public Shader" + class_suffix + " {\n\n")

    fd.write("public:\n\n")

    if header_data.uniforms:
        fd.write("\tenum Uniforms {\n")
        for x in header_data.uniforms:
            fd.write("\t\t" + x.upper() + ",\n")
        fd.write("\t};\n\n")

        # "Hidden" state machine router
        fd.write("\t_FORCE_INLINE_ void _apply_gles1_state(Uniforms p_uniform, const Variant& p_value) {\n")
        fd.write("\t\tswitch(p_uniform) {\n")
        for x in header_data.uniforms:
            fd.write("\t\t\tcase " + x.upper() + ": {\n")
            if x in GL_1_5_MAPPINGS:
                for line in GL_1_5_MAPPINGS[x].split("\n"):
                    fd.write("\t\t\t\t" + line + "\n")
            else:
                fd.write("\t\t\t\t// Unmapped uniform: " + x + " - ignored\n")
            fd.write("\t\t\t} break;\n")
        fd.write("\t\t\tdefault: break;\n")
        fd.write("\t\t}\n")
        fd.write("\t}\n\n")

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
        # We keep version_get_uniform for compatibility if something else in the engine explicitly queries it,
        # but our setters ignore it.
        fd.write(
            "\t_FORCE_INLINE_ int version_get_uniform(Uniforms p_uniform,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { return _version_get_uniform(p_uniform,p_version,p_variant,p_specialization); }\n\n"
        )

        # We replace the glUniform* calls with our Variant router. 
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, double p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
        )
        if gles_version >= 3:
            fd.write(
                "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, uint8_t p_value,RID p_version,ShaderVariant p_variant"
                + defvariant
                + ",uint64_t p_specialization="
                + str(defspec)
                + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
            )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, int8_t p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
        )
        if gles_version >= 3:
            fd.write(
                "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, uint16_t p_value,RID p_version,ShaderVariant p_variant"
                + defvariant
                + ",uint64_t p_specialization="
                + str(defspec)
                + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
            )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, int16_t p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
        )
        if gles_version >= 3:
            fd.write(
                "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, uint32_t p_value,RID p_version,ShaderVariant p_variant"
                + defvariant
                + ",uint64_t p_specialization="
                + str(defspec)
                + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
            )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, int32_t p_value,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_value)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Color& p_color,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_color)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Vector2& p_vec2,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_vec2)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Size2i& p_vec2,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(Vector2i(p_vec2))); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Vector3& p_vec3,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_vec3)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Vector4& p_vec4,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_vec4)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_a, float p_b,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(Vector2(p_a, p_b))); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_a, float p_b, float p_c,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(Vector3(p_a, p_b, p_c))); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, float p_a, float p_b, float p_c, float p_d,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(Vector4(p_a, p_b, p_c, p_d))); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Transform3D& p_transform,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_transform)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Transform2D& p_transform,RID p_version,ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_transform)); }\n\n"
        )
        fd.write(
            "\t_FORCE_INLINE_ void version_set_uniform(Uniforms p_uniform, const Projection& p_matrix, RID p_version, ShaderVariant p_variant"
            + defvariant
            + ",uint64_t p_specialization="
            + str(defspec)
            + ") { _apply_gles1_state(p_uniform, Variant(p_matrix)); }\n\n"
        )

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
            + "0," # feedback count bypassed for clean build
            + "nullptr," # feedbacks bypassed
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


def build_gles1_headers(target, source, env):
    for t, s in zip(target, source):
        out_path = str(t)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        build_gles_header(
            filename=str(s), 
            include="drivers/gles1/transpiler/shader_gles1.h", 
            class_suffix="GLES1", 
            optional_output_filename=out_path, 
            gles_version=1
        )


def to_byte_array(lines):
    # Join lines and explicitly add the null terminator 
    raw_str = "\n".join(lines) + "\0"
    return ", ".join(str(b) for b in raw_str.encode("utf-8"))