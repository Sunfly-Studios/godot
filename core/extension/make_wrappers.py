proto_mod = """
#define MODBIND$VER($RETTYPE m_name$ARG) \\
virtual $RETVAL _##m_name($FUNCARGS) $CONST; \\
$TRAIT_DEF
template <typename T_Exact> \\
_FORCE_INLINE_ $RETVAL m_name##_call($FUNCARGS) $CONST { \\
    $CRTP_FAST_PATH \\
} \\
_FORCE_INLINE_ virtual $RETVAL m_name($FUNCARGS) $CONST override { \\
    using ThisClass = std::remove_pointer_t<decltype(this)>; \\
    $RETX ThisClass::_##m_name($CALLARGS);\\
}
"""


def generate_mod_version(argcount, const=False, returns=False):
    s = proto_mod
    sproto = str(argcount)
    if returns:
        sproto += "R"
        s = s.replace("$RETTYPE", "m_ret, ")
        s = s.replace("$RETVAL", "m_ret")
        s = s.replace("$RETX", "return")

    else:
        s = s.replace("$RETTYPE", "")
        s = s.replace("$RETVAL", "void")
        s = s.replace("$RETX", "")

    if const:
        sproto += "C"
        s = s.replace("$CONST", "const")
    else:
        s = s.replace("$CONST", "")

    s = s.replace("$VER", sproto)
    argtext = ""
    funcargs = ""
    callargs = ""
    declval_args = ""

    for i in range(argcount):
        if i > 0:
            funcargs += ", "
            callargs += ", "
            declval_args += ", "

        argtext += ", m_type" + str(i + 1)
        funcargs += "m_type" + str(i + 1) + " arg" + str(i + 1)
        callargs += "arg" + str(i + 1)
        
        # For some reason this wants to give me rvalue references.
        # This forces it to simulate lvalue references.
        declval_args += f"std::declval<std::add_lvalue_reference_t<m_type{i + 1}>>()"

    # When the return type is expected, verify
    # if the C++ method return type is compatible
    # (so we don't accidentally find duplicate methods)
    if returns:
        # `auto` is used here for the same rationale as
        # core/templates/tuple.h.
        trait_def = f"""\ttemplate <typename T_Check> \\
\tstruct _modbind_##m_name##_trait {{ \\
\t\ttemplate <typename U> static auto test(int) -> decltype(std::declval<U>()._##m_name({declval_args})); \\
\t\ttemplate <typename> static std::false_type test(...); \\
\t\tusing RetType = decltype(test<T_Check>(0)); \\
\t\tstatic constexpr bool value = !std::is_same_v<std::remove_cv_t<T_Check>, self_type> && !std::is_same_v<RetType, std::false_type> && std::is_convertible_v<RetType, m_ret>; \\
\t}}; \\"""
        crtp_fast_path = f"""\t\tT_Exact* p_instance = static_cast<T_Exact*>(const_cast<std::remove_cv_t<std::remove_pointer_t<decltype(this)>>*>(this));\\
\t\tif constexpr (_modbind_##m_name##_trait<T_Exact>::value) {{\\
\t\t\treturn (m_ret)p_instance->_##m_name({callargs});\\
\t\t}} else {{\\
\t\t\tusing ThisClass = std::remove_pointer_t<decltype(this)>; \\
\t\t\treturn ThisClass::_##m_name({callargs});\\
\t\t}}"""
    else:
        trait_def = f"""\ttemplate <typename T_Check> \\
\tstruct _modbind_##m_name##_trait {{ \\
\t\ttemplate <typename U> static auto test(int) -> decltype(std::declval<U>()._##m_name({declval_args}), std::true_type()); \\
\t\ttemplate <typename> static std::false_type test(...); \\
\t\tstatic constexpr bool value = !std::is_same_v<std::remove_cv_t<T_Check>, self_type> && decltype(test<T_Check>(0))::value; \\
\t}}; \\"""
        crtp_fast_path = f"""\t\tT_Exact* p_instance = static_cast<T_Exact*>(const_cast<std::remove_cv_t<std::remove_pointer_t<decltype(this)>>*>(this));\\
\t\tif constexpr (_modbind_##m_name##_trait<T_Exact>::value) {{\\
\t\t\tp_instance->_##m_name({callargs});\\
\t\t}} else {{\\
\t\t\tusing ThisClass = std::remove_pointer_t<decltype(this)>; \\
\t\t\tThisClass::_##m_name({callargs});\\
\t\t}}"""

    s = s.replace("$TRAIT_DEF\n", trait_def + "\n")
    s = s.replace("$CRTP_FAST_PATH", crtp_fast_path)

    if argcount:
        s = s.replace("$ARG", argtext)
        s = s.replace("$FUNCARGS", funcargs)
        s = s.replace("$CALLARGS", callargs)
    else:
        s = s.replace("$ARG", "")
        s = s.replace("$FUNCARGS", funcargs)
        s = s.replace("$CALLARGS", callargs)

    return s


proto_ex = """
#define EXBIND$VER($RETTYPE m_name$ARG) \\
GDVIRTUAL$VER_REQUIRED($RETTYPE_##m_name$ARG)\\
template <typename T_Exact> \\
_FORCE_INLINE_ $RETVAL m_name##_call($FUNCARGS) $CONST { \\
    $EXBIND_CRTP\\
} \\
virtual $RETVAL m_name($FUNCARGS) $CONST override { \\
    $RETPRE\\
    GDVIRTUAL_CALL(_##m_name$CALLARGS$RETREF);\\
    $RETPOST\\
}
"""


def generate_ex_version(argcount, const=False, returns=False):
    s = proto_ex
    sproto = str(argcount)
    if returns:
        sproto += "R"
        s = s.replace("$RETTYPE", "m_ret, ")
        s = s.replace("$RETVAL", "m_ret")
    else:
        s = s.replace("$RETTYPE", "")
        s = s.replace("$RETVAL", "void")

    if const:
        sproto += "C"
        s = s.replace("$CONST", "const")
    else:
        s = s.replace("$CONST", "")

    s = s.replace("$VER", sproto)
    argtext = ""
    funcargs = ""
    callargs = ""
    callargs_raw = ""

    for i in range(argcount):
        if i > 0:
            funcargs += ", "
            callargs_raw += ", "

        argtext += ", m_type" + str(i + 1)
        funcargs += "m_type" + str(i + 1) + " arg" + str(i + 1)
        callargs += ", arg" + str(i + 1)
        callargs_raw += "arg" + str(i + 1)

    if returns:
        exbind_crtp = f"\t$RETPRE\\\n\t\t_gdvirtual__##m_name##_call<T_Exact>({callargs_raw}{', ' if argcount else ''}*ret_ptr);\\\n\t\t$RETPOST"
    else:
        exbind_crtp = f"\t\t_gdvirtual__##m_name##_call<T_Exact>({callargs_raw});"

    s = s.replace("$EXBIND_CRTP", exbind_crtp)

    if returns:
        s = s.replace("$RETPRE", 
            "using RetT = m_ret;\\\n"
            "\t\tRetT *ret_ptr = (RetT *)SAFE_ALLOCA_SINGLE(RetT);\\\n"
            "\t\t::new ((void *)ret_ptr) RetT();")
        s = s.replace("$RETPOST", 
            "m_ret ret_val = (m_ret)*ret_ptr;\\\n"
            "\t\tret_ptr->~RetT();\\\n"
            "\t\treturn ret_val;")
    else:
        s = s.replace("$RETPRE", "")
        s = s.replace("$RETPOST", "return;")

    if argcount:
        s = s.replace("$ARG", argtext)
        s = s.replace("$FUNCARGS", funcargs)
        s = s.replace("$CALLARGS", callargs)
    else:
        s = s.replace("$ARG", "")
        s = s.replace("$FUNCARGS", funcargs)
        s = s.replace("$CALLARGS", callargs)

    if returns:
        s = s.replace("$RETREF", ", *ret_ptr")
    else:
        s = s.replace("$RETREF", "")

    return s


def run(target, source, env):
    max_versions = 12

    txt = """/* THIS FILE IS GENERATED DO NOT EDIT */
#ifndef GDEXTENSION_WRAPPERS_GEN_H
#define GDEXTENSION_WRAPPERS_GEN_H

#include "core/os/memory.h"
#include <type_traits>
"""

    for i in range(max_versions + 1):
        txt += "\n/* Extension Wrapper " + str(i) + " Arguments */\n"
        txt += generate_ex_version(i, False, False)
        txt += generate_ex_version(i, False, True)
        txt += generate_ex_version(i, True, False)
        txt += generate_ex_version(i, True, True)

    for i in range(max_versions + 1):
        txt += "\n/* Module Wrapper " + str(i) + " Arguments */\n"
        txt += generate_mod_version(i, False, False)
        txt += generate_mod_version(i, False, True)
        txt += generate_mod_version(i, True, False)
        txt += generate_mod_version(i, True, True)

    txt += "\n#endif\n"

    with open(str(target[0]), "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)
