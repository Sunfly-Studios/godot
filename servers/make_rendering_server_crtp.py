import re

RS_NESTED_TYPES = {
    "TextureType", "TextureLayeredType", "CubeMapLayer",
    "TextureDetectCallback", "TextureDetectRoughnessChannel", "TextureDetectRoughnessCallback",
    "TextureInfo", "PipelineSource", "ShaderMode", "CullMode", "ShaderNativeSourceCode",
    "ArrayType", "ArrayCustomFormat", "ArrayFormat", "PrimitiveType", "SurfaceData",
    "BlendShapeMode", "MultimeshTransformFormat", "MultimeshPhysicsInterpolationQuality",
    "LightType", "LightParam", "LightBakeMode", "LightOmniShadowMode",
    "LightDirectionalShadowMode", "LightDirectionalSkyMode", "ShadowQuality",
    "LightProjectorFilter", "ReflectionProbeUpdateMode", "ReflectionProbeAmbientMode",
    "DecalTexture", "DecalFilter", "VoxelGIQuality", "ShadowmaskMode",
    "ParticlesMode", "ParticlesTransformAlign", "ParticlesEmitFlags", "ParticlesDrawOrder",
    "ParticlesCollisionType", "ParticlesCollisionHeightfieldResolution",
    "FogVolumeShape", "CanvasItemTextureFilter", "CanvasItemTextureRepeat",
    "ViewportScaling3DMode", "ViewportAnisotropicFiltering", "ViewportScaling3DType",
    "ViewportUpdateMode", "ViewportClearMode", "ViewportEnvironmentMode",
    "ViewportSDFOversize", "ViewportSDFScale", "ViewportMSAA", "ViewportScreenSpaceAA",
    "ViewportOcclusionCullingBuildQuality", "ViewportRenderInfo", "ViewportRenderInfoType",
    "ViewportDebugDraw", "ViewportVRSMode", "ViewportVRSUpdateMode", "SkyMode",
    "CompositorEffectFlags", "CompositorEffectCallbackType", "EnvironmentBG",
    "EnvironmentAmbientSource", "EnvironmentReflectionSource", "EnvironmentGlowBlendMode",
    "EnvironmentToneMapper", "EnvironmentSSRRoughnessQuality", "EnvironmentSSAOQuality",
    "EnvironmentSSILQuality", "EnvironmentSDFGIYScale", "EnvironmentSDFGIRayCount",
    "EnvironmentSDFGIFramesToConverge", "EnvironmentSDFGIFramesToUpdateLight",
    "EnvironmentFogMode", "SubSurfaceScatteringQuality", "DOFBlurQuality", "DOFBokehShape",
    "InstanceType", "InstanceFlags", "ShadowCastingSetting", "VisibilityRangeFadeMode",
    "BakeChannels", "CanvasTextureChannel", "NinePatchAxisMode", "CanvasGroupMode",
    "CanvasLightMode", "CanvasLightBlendMode", "CanvasLightShadowFilter",
    "CanvasOccluderPolygonCullMode", "GlobalShaderParameterType", "RenderingInfo",
    "FrameProfileArea", "Features", "SurfaceUpgradeCallback",
}

QUALIFY_REGEX = re.compile(
    r"(?<!RenderingServer::)\b(" + "|".join(RS_NESTED_TYPES) + r")\b"
)


def qualify_types(type_str):
    return QUALIFY_REGEX.sub(r"RenderingServer::\1", type_str)


def split_top_level(s, sep=","):
    parts = []
    depth = 0
    current = []
    for ch in s:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    if current:
        parts.append("".join(current).strip())
    return [p for p in parts if p]


def parse_args(arg_str):
    arg_str = arg_str.strip()
    if not arg_str or arg_str == "void":
        return []
    raw_args = split_top_level(arg_str, ",")
    parsed = []
    for idx, raw in enumerate(raw_args):
        # Strip default value at top level
        eq_parts = split_top_level(raw, "=")
        decl = eq_parts[0].strip()

        # Check if parameter has a name or is just a type (e.g. "Image::Format", "const StringName &")
        m = re.match(r"^(.*?)([*&>\s]+)([a-zA-Z_]\w*)$", decl)
        if m:
            candidate_type = m.group(1) + m.group(2)
            candidate_name = m.group(3)
            # If the "name" is actually a known type or C++ keyword, it's an unnamed parameter
            if candidate_name in RS_NESTED_TYPES or candidate_name in {
                "int", "float", "double", "bool", "uint32_t", "uint64_t", "int64_t", "real_t"
            }:
                arg_type = decl
                arg_name = f"p_arg{idx + 1}"
            else:
                arg_type = candidate_type.strip()
                arg_name = candidate_name
        else:
            arg_type = decl
            arg_name = f"p_arg{idx + 1}"

        parsed.append((qualify_types(arg_type), arg_name))
    return parsed


def parse_rendering_server(header_text):
    # Strip C/C++ comments
    header_text = re.sub(r"//.*", "", header_text)
    header_text = re.sub(r"/\*.*?\*/", "", header_text, flags=re.DOTALL)

    class_match = re.search(r"class\s+RenderingServer\s*:\s*public\s+Object\s*\{", header_text)
    if not class_match:
        raise RuntimeError("Could not find class RenderingServer in header.")

    pos = class_match.end()
    brace_depth = 1
    access = "private"
    pp_stack = []
    methods = []

    lines = header_text[pos:].splitlines()
    stmt_buffer = ""

    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue

        # Track preprocessor directives inside class body
        if stripped.startswith("#if"):
            if brace_depth == 1:
                pp_stack.append(stripped)
            continue
        elif stripped.startswith("#endif"):
            if brace_depth == 1 and pp_stack:
                pp_stack.pop()
            continue
        elif stripped.startswith("#"):
            continue

        # Track brace depth
        open_braces = stripped.count("{")
        close_braces = stripped.count("}")

        if brace_depth == 1 and re.match(r"^(public|protected|private)\s*:", stripped):
            access = stripped.split(":")[0].strip()
            stripped = stripped.split(":", 1)[1].strip()
            if not stripped:
                continue

        if brace_depth == 1 and open_braces == 0 and close_braces == 0:
            stmt_buffer += " " + stripped
            if not stripped.endswith(";"):
                continue
            stmt = " ".join(stmt_buffer.split())
            stmt_buffer = ""

            if access != "public":
                continue

            # Ignore static methods and typedefs
            if stmt.startswith("static ") or stmt.startswith("typedef "):
                continue

            is_pure_virt = stmt.startswith("virtual ") and stmt.endswith("= 0;")
            is_force_inline = stmt.startswith("_FORCE_INLINE_ ") and not stmt.endswith("}")

            if not (is_pure_virt or is_force_inline):
                continue

            # Extract signature
            clean = stmt
            if is_pure_virt:
                clean = clean[len("virtual ") : -len("= 0;")].strip()
            else:
                clean = clean[len("_FORCE_INLINE_ ") : -1].strip()

            is_const = False
            if clean.endswith(" const"):
                is_const = True
                clean = clean[: -len(" const")].strip()

            paren_idx = clean.find("(")
            r_paren_idx = clean.rfind(")")
            if paren_idx == -1 or r_paren_idx == -1:
                continue

            ret_and_name = clean[:paren_idx].strip()
            args_str = clean[paren_idx + 1 : r_paren_idx]

            rn_match = re.match(r"^(.*?)([*&>\s]+)([a-zA-Z_]\w*)$", ret_and_name)
            if not rn_match:
                continue

            ret_type = qualify_types((rn_match.group(1) + rn_match.group(2)).strip())
            func_name = rn_match.group(3).strip()
            args = parse_args(args_str)

            methods.append({
                "ret": ret_type,
                "name": func_name,
                "args": args,
                "const": is_const,
                "pp": list(pp_stack),
            })
        else:
            stmt_buffer = ""

        brace_depth += open_braces - close_braces
        if brace_depth <= 0:
            break

    return methods


def wrap_pp(lines, pp_stack):
    if not pp_stack:
        return lines
    out = list(pp_stack)
    out.extend(lines)
    out.extend(["#endif" for _ in pp_stack])
    return out


def run(target, source, env):
    with open(str(source[0]), "r", encoding="utf-8") as f:
        header_text = f.read()

    methods = parse_rendering_server(header_text)

    vtable_lines = ["struct RenderingServerVTable {"]
    forwarder_lines = []
    dispatcher_lines = ["template <typename T>", "struct RenderingServerDispatcher {"]
    vtable_init_lines = [
        "template <typename T>",
        "const RenderingServerVTable RenderingServerDispatcher<T>::vtable = {",
    ]

    for m in methods:
        ret = m["ret"]
        name = m["name"]
        args = m["args"]
        is_const = m["const"]
        pp = m["pp"]

        self_ptr_type = "const RenderingServer *" if is_const else "RenderingServer *"
        cast_type = "const T *" if is_const else "T *"
        const_qual = " const" if is_const else ""
        ret_prefix = "" if ret == "void" else "return "

        arg_types = [a[0] for a in args]
        arg_decls = [f"{a[0]} {a[1]}" for a in args]
        arg_names = [a[1] for a in args]

        vt_params = ", ".join([self_ptr_type] + arg_types)
        fn_params = ", ".join(arg_decls)
        disp_params = ", ".join([f"{self_ptr_type}p_self"] + arg_decls)
        call_args_vt = ", ".join(["this"] + arg_names)
        call_args_impl = ", ".join(arg_names)

        vtable_lines.extend(
            wrap_pp([f"\t{ret} (*{name})({vt_params});"], pp)
        )

        forwarder_lines.extend(
            wrap_pp([
                f"_FORCE_INLINE_ {ret} RenderingServer::{name}({fn_params}){const_qual} {{",
                f"\t{ret_prefix}rs_vtable->{name}({call_args_vt});",
                "}",
            ], pp)
        )

        dispatcher_lines.extend(
            wrap_pp([
                f"\t_NO_INLINE_ static {ret} {name}({disp_params}) {{",
                f"\t\t{ret_prefix}static_cast<{cast_type}>(p_self)->{name}({call_args_impl});",
                "\t}",
            ], pp)
        )

        vtable_init_lines.extend(
            wrap_pp([f"\t&RenderingServerDispatcher<T>::{name},"], pp)
        )

    vtable_lines.append("};\n")
    dispatcher_lines.append("\tstatic const RenderingServerVTable vtable;\n};\n")
    vtable_init_lines.append("};\n")

    output = [
        "/* THIS FILE IS AUTO-GENERATED DO NOT EDIT */",
        "#ifndef RENDERING_SERVER_BINDS_GEN_INC",
        "#define RENDERING_SERVER_BINDS_GEN_INC\n",
        "\n".join(vtable_lines),
        "\n".join(forwarder_lines),
        "",
        "\n".join(dispatcher_lines),
        "\n".join(vtable_init_lines),
        "#endif // RENDERING_SERVER_BINDS_GEN_INC\n",
    ]

    with open(str(target[0]), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(output))