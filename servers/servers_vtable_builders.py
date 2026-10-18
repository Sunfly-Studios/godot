"""Functions used to generate static vtable dispatchers for server/rendering classes during build time."""

import os
import re

C_PRIMITIVE_TYPES = {
    "void",
    "bool",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "signed",
    "unsigned",
    "int8_t",
    "uint8_t",
    "int16_t",
    "uint16_t",
    "int32_t",
    "uint32_t",
    "int64_t",
    "uint64_t",
    "size_t",
    "intptr_t",
    "uintptr_t",
    "real_t",
    "ObjectID",
    "RID",
}


def _strip_comments(text: str) -> str:
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return text


def _split_top_level(text: str, sep: str = ",") -> list[str]:
    parts = []
    depth = 0
    current = []
    for ch in text:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    if current:
        parts.append("".join(current).strip())
    return [p for p in parts if p]


def _qualify_nested_types(type_str: str, class_name: str, nested_types: set[str]) -> str:
    if not nested_types:
        return type_str
    pattern = re.compile(
        rf"(?<!{re.escape(class_name)}::)(?<!::)\b("
        + "|".join(re.escape(t) for t in sorted(nested_types, key=len, reverse=True))
        + r")\b"
    )
    return pattern.sub(rf"{class_name}::\1", type_str)


def _parse_args(arg_str: str, class_name: str, nested_types: set[str]) -> list[tuple[str, str]]:
    arg_str = arg_str.strip()
    if not arg_str or arg_str == "void":
        return []

    raw_args = _split_top_level(arg_str, ",")
    parsed = []

    for idx, raw in enumerate(raw_args):
        # Strip default parameter value at top level.
        decl = _split_top_level(raw, "=")[0].strip()

        match = re.match(r"^(.*?)([*&>\s]+)([a-zA-Z_]\w*)$", decl)
        if match:
            candidate_type = match.group(1) + match.group(2)
            candidate_name = match.group(3)
            if candidate_name in nested_types or candidate_name in C_PRIMITIVE_TYPES:
                arg_type = decl
                arg_name = f"p_arg{idx + 1}"
            else:
                arg_type = candidate_type.strip()
                arg_name = candidate_name
        else:
            arg_type = decl
            arg_name = f"p_arg{idx + 1}"

        qualified_type = _qualify_nested_types(arg_type, class_name, nested_types)
        parsed.append((qualified_type, arg_name))

    return parsed


def _extract_nested_type_decl(stmt: str, nested_types: set[str]) -> None:
    """Dynamically tracks enum, struct, class, using, and typedef names declared inside the class."""
    # Match enum [class] Name, struct Name, class Name
    m = re.match(r"^(?:enum(?:\s+class)?|struct|class)\s+([a-zA-Z_]\w*)\b", stmt)
    if m:
        nested_types.add(m.group(1))
        return

    # Match using Name = ...
    m = re.match(r"^using\s+([a-zA-Z_]\w*)\s*=", stmt)
    if m:
        nested_types.add(m.group(1))
        return

    # Match typedef ... Name; or function pointer typedefs: typedef ret (*Name)(args);
    if stmt.startswith("typedef "):
        fn_ptr_match = re.search(r"\(\*\s*([a-zA-Z_]\w*)\s*\)\s*\(", stmt)
        if fn_ptr_match:
            nested_types.add(fn_ptr_match.group(1))
        else:
            tokens = re.findall(r"[a-zA-Z_]\w*", stmt[:-1])
            if tokens:
                nested_types.add(tokens[-1])


def _default_vtable_member_name(class_name: str) -> str:
    """Derives a vtable pointer name if not explicitly provided (e.g. RenderingServer -> rs_vtable)."""
    if class_name == "RenderingServer":
        # Maintain compatibility
        return "rs_vtable"
    # Fallback: snake_case initials + _vtable (e.g., RenderingMethod -> rm_vtable)
    initials = "".join(ch.lower() for ch in class_name if ch.isupper())
    return f"{initials}_vtable" if initials else "vtable"


def _parse_class_body(class_name: str, body_lines: list[str]) -> tuple[list[dict], str]:
    brace_depth = 1
    access = "private"
    pp_stack = []
    methods = []
    nested_types = set()
    vtable_member = None
    stmt_buffer = ""

    for line in body_lines:
        stripped = line.strip()
        if not stripped:
            continue

        # Track preprocessor directives at class scope.
        if stripped.startswith("#if"):
            if brace_depth == 1:
                pp_stack.append(stripped)
            continue
        elif stripped.startswith("#elif") or stripped.startswith("#else"):
            if brace_depth == 1 and pp_stack:
                pp_stack[-1] = stripped
            continue
        elif stripped.startswith("#endif"):
            if brace_depth == 1 and pp_stack:
                pp_stack.pop()
            continue
        elif stripped.startswith("#"):
            continue

        open_braces = stripped.count("{")
        close_braces = stripped.count("}")

        if brace_depth == 1 and re.match(r"^(public|protected|private)\s*:", stripped):
            access = stripped.split(":", 1)[0].strip()
            stripped = stripped.split(":", 1)[1].strip()
            if not stripped:
                continue

        # Detect nested enum/struct/class blocks opening at class depth 1.
        if brace_depth == 1 and open_braces > 0:
            _extract_nested_type_decl(stripped, nested_types)

        if brace_depth == 1 and open_braces == 0 and close_braces == 0:
            stmt_buffer += " " + stripped
            if not stripped.endswith(";"):
                continue

            stmt = " ".join(stmt_buffer.split())
            stmt_buffer = ""

            _extract_nested_type_decl(stmt, nested_types)

            # Detect vtable pointer member if present in class (e.g. `const RenderingServerVTable *rs_vtable = nullptr;`)
            vt_match = re.search(rf"\b{re.escape(class_name)}VTable\s*\*\s*([a-zA-Z_]\w*)", stmt)
            if vt_match:
                vtable_member = vt_match.group(1)
                continue

            if access != "public":
                continue

            if stmt.startswith(("static ", "typedef ", "using ", "friend ")):
                continue

            is_pure_virt = stmt.startswith("virtual ") and stmt.endswith("= 0;")
            is_force_inline = stmt.startswith("_FORCE_INLINE_ ") and not stmt.endswith("}")

            if not (is_pure_virt or is_force_inline):
                continue

            if is_pure_virt:
                clean = stmt[len("virtual ") : -len("= 0;")].strip()
            else:
                clean = stmt[len("_FORCE_INLINE_ ") : -1].strip()

            # Ignore destructors.
            if clean.startswith("~"):
                continue

            is_const = False
            if re.search(r"\)\s*const$", clean):
                is_const = True
                clean = re.sub(r"\s*const$", "", clean).strip()

            paren_idx = clean.find("(")
            r_paren_idx = clean.rfind(")")
            if paren_idx == -1 or r_paren_idx == -1:
                continue

            ret_and_name = clean[:paren_idx].strip()
            args_str = clean[paren_idx + 1 : r_paren_idx]

            rn_match = re.match(r"^(.*?)([*&>\s]+)([a-zA-Z_]\w*)$", ret_and_name)
            if not rn_match:
                continue

            raw_ret_type = (rn_match.group(1) + rn_match.group(2)).strip()
            ret_type = _qualify_nested_types(raw_ret_type, class_name, nested_types)
            func_name = rn_match.group(3).strip()
            args = _parse_args(args_str, class_name, nested_types)

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

    if not vtable_member:
        vtable_member = _default_vtable_member_name(class_name)

    return methods, vtable_member


def parse_header_classes(header_text: str) -> list[dict]:
    clean_text = _strip_comments(header_text)
    class_pattern = re.compile(r"\bclass\s+([a-zA-Z_]\w*)\s*(?::\s*[^{]+)?\{")

    parsed_classes = []
    for match in class_pattern.finditer(clean_text):
        class_name = match.group(1)
        body_lines = clean_text[match.end() :].splitlines()
        methods, vtable_member = _parse_class_body(class_name, body_lines)
        if methods:
            parsed_classes.append({
                "class_name": class_name,
                "vtable_member": vtable_member,
                "methods": methods,
            })

    if not parsed_classes:
        raise RuntimeError("No eligible classes with pure virtual or _FORCE_INLINE_ dispatch methods found.")

    return parsed_classes


# Wrap Pre Processor
def _wrap_pp(lines: list[str], pp_stack: list[str]) -> list[str]:
    if not pp_stack:
        return lines
    out = list(pp_stack)
    out.extend(lines)
    out.extend(["#endif" for _ in pp_stack])
    return out


def _generate_class_vtable_code(class_info: dict) -> str:
    class_name = class_info["class_name"]
    vtable_member = class_info["vtable_member"]
    methods = class_info["methods"]

    vtable_struct = f"{class_name}VTable"
    dispatcher_struct = f"{class_name}Dispatcher"

    vtable_lines = [f"struct {vtable_struct} {{"]
    forwarder_lines = []
    dispatcher_lines = ["template <typename T>", f"struct {dispatcher_struct} {{"]
    vtable_init_lines = [
        "template <typename T>",
        f"const {vtable_struct} {dispatcher_struct}<T>::vtable = {{",
    ]

    for m in methods:
        ret = m["ret"]
        name = m["name"]
        args = m["args"]
        is_const = m["const"]
        pp = m["pp"] # Pre Processors

        self_ptr_type = f"const {class_name} *" if is_const else f"{class_name} *"
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

        vtable_lines.extend(_wrap_pp([f"\t{ret} (*{name})({vt_params});"], pp))

        forwarder_lines.extend(
            _wrap_pp(
                [
                    f"_FORCE_INLINE_ {ret} {class_name}::{name}({fn_params}){const_qual} {{",
                    f"\t{ret_prefix}{vtable_member}->{name}({call_args_vt});",
                    "}",
                ],
                pp,
            )
        )

        dispatcher_lines.extend(
            _wrap_pp(
                [
                    f"\t_NO_INLINE_ static {ret} {name}({disp_params}) {{",
                    f"\t\t{ret_prefix}static_cast<{cast_type}>(p_self)->{name}({call_args_impl});",
                    "\t}",
                ],
                pp,
            )
        )

        vtable_init_lines.extend(_wrap_pp([f"\t&{dispatcher_struct}<T>::{name},"], pp))

    vtable_lines.append("};\n")
    dispatcher_lines.append(f"\tstatic const {vtable_struct} vtable;\n}};\n")
    vtable_init_lines.append("};\n")

    return "\n".join([
        "\n".join(vtable_lines),
        "\n".join(forwarder_lines),
        "",
        "\n".join(dispatcher_lines),
        "\n".join(vtable_init_lines),
    ])


def make_static_vtable_builder(target, source, env):
    target_path = str(target[0])
    source_path = str(source[0])

    with open(source_path, "r", encoding="utf-8") as f:
        header_text = f.read()

    parsed_classes = parse_header_classes(header_text)

    # Transforms "gen/rendering_server_binds.gen.inc" into "RENDERING_SERVER_BINDS_GEN_INC"
    guard_name = re.sub(r"[^A-Za-z0-9]+", "_", os.path.basename(target_path)).upper().strip("_")

    sections = [
        "/* THIS FILE IS GENERATED. EDITS WILL BE LOST. */",
        "",
        f"#ifndef {guard_name}",
        f"#define {guard_name}",
        "",
    ]

    for class_info in parsed_classes:
        sections.append(_generate_class_vtable_code(class_info))

    sections.append(f"#endif // {guard_name}\n")

    with open(target_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(sections))