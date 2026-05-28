script_call = """ScriptInstance *_script_instance = ((Object *)(this))->get_script_instance();\\
		if (likely(_script_instance)) {\\
			Callable::CallError ce;\\
			$CALLSIARGS\\
			$CALLSIBEGIN_script_instance->callp(_gdvirtual_##$VARNAME##_sn, $CALLSIARGPASS, ce);\\
			bool _call_ok = (ce.error == Callable::CallError::CALL_OK);\\
			if (likely(_call_ok)) {\\
				$CALLSIRET\\
			}\\
			$CALLSICLEANUP\\
			if (_call_ok) {\\
				return true;\\
			}\\
		}"""

script_has_method = """ScriptInstance *_script_instance = ((Object *)(this))->get_script_instance();\\
		if (_script_instance && _script_instance->has_method(_gdvirtual_##$VARNAME##_sn)) {\\
			return true;\\
		}"""

proto = """#define GDVIRTUAL$VER($ALIAS $RET m_name $ARG)\\
	mutable void *_gdvirtual_##$VARNAME = nullptr;\\
	_FORCE_INLINE_ bool _gdvirtual_##$VARNAME##_call($CALLARGS) $CONST {\\
		using ThisClass = std::remove_pointer_t<decltype(this)>;\\
		static const StringName _gdvirtual_##$VARNAME##_sn = _scs_create(#m_name, true);\\
		$SCRIPTCALL\\
		if (ThisClass::_get_extension()) {\\
			void *cached_virt = _gdvirtual_##$VARNAME;\\
			if (unlikely(!cached_virt)) {\\
				MethodInfo mi = ThisClass::_gdvirtual_##$VARNAME##_get_method_info();\\
				uint32_t hash = mi.get_compatibility_hash();\\
				if (ThisClass::_get_extension()->get_virtual_call_data2 && ThisClass::_get_extension()->call_virtual_with_data) {\\
					_gdvirtual_##$VARNAME = ThisClass::_get_extension()->get_virtual_call_data2(ThisClass::_get_extension()->class_userdata, &_gdvirtual_##$VARNAME##_sn, hash);\\
				} else if (ThisClass::_get_extension()->get_virtual2) {\\
					_gdvirtual_##$VARNAME = (void *)ThisClass::_get_extension()->get_virtual2(ThisClass::_get_extension()->class_userdata, &_gdvirtual_##$VARNAME##_sn, hash);\\
				}\\
				_GDVIRTUAL_GET_DEPRECATED(_gdvirtual_##$VARNAME, _gdvirtual_##$VARNAME##_sn, $COMPAT)\\
				_GDVIRTUAL_TRACK(_gdvirtual_##$VARNAME);\\
				if (!_gdvirtual_##$VARNAME) {\\
					_gdvirtual_##$VARNAME = reinterpret_cast<void*>(_INVALID_GDVIRTUAL_FUNC_ADDR);\\
				}\\
				cached_virt = _gdvirtual_##$VARNAME;\\
			}\\
			if (likely(cached_virt != reinterpret_cast<void*>(_INVALID_GDVIRTUAL_FUNC_ADDR))) {\\
				$CALLPTRARGS\\
				$CALLPTRRETDEF\\
				if (ThisClass::_get_extension()->call_virtual_with_data) {\\
					ThisClass::_get_extension()->call_virtual_with_data(ThisClass::_get_extension_instance(), &_gdvirtual_##$VARNAME##_sn, cached_virt, $CALLPTRARGPASS, $CALLPTRRETPASS);\\
					$CALLPTRRET\\
				} else {\\
					const GDExtensionClassCallVirtual call_func = reinterpret_cast<GDExtensionClassCallVirtual>(cached_virt);\\
					call_func(ThisClass::_get_extension_instance(), $CALLPTRARGPASS, $CALLPTRRETPASS);\\
					$CALLPTRRET\\
				}\\
				return true;\\
			}\\
		}\\
		$REQCHECK\\
		$RVOID\\
		return false;\\
	}\\
	_FORCE_INLINE_ bool _gdvirtual_##$VARNAME##_overridden() const {\\
		using ThisClass = std::remove_pointer_t<decltype(this)>;\\
		static const StringName _gdvirtual_##$VARNAME##_sn = _scs_create(#m_name, true);\\
		$SCRIPTHASMETHOD\\
		if (ThisClass::_get_extension()) {\\
			void *cached_virt = _gdvirtual_##$VARNAME;\\
			if (unlikely(!cached_virt)) {\\
				MethodInfo mi = ThisClass::_gdvirtual_##$VARNAME##_get_method_info();\\
				uint32_t hash = mi.get_compatibility_hash();\\
				if (ThisClass::_get_extension()->get_virtual_call_data2 && ThisClass::_get_extension()->call_virtual_with_data) {\\
					_gdvirtual_##$VARNAME = ThisClass::_get_extension()->get_virtual_call_data2(ThisClass::_get_extension()->class_userdata, &_gdvirtual_##$VARNAME##_sn, hash);\\
				} else if (ThisClass::_get_extension()->get_virtual2) {\\
					_gdvirtual_##$VARNAME = (void *)ThisClass::_get_extension()->get_virtual2(ThisClass::_get_extension()->class_userdata, &_gdvirtual_##$VARNAME##_sn, hash);\\
				}\\
				_GDVIRTUAL_GET_DEPRECATED(_gdvirtual_##$VARNAME, _gdvirtual_##$VARNAME##_sn, $COMPAT)\\
				_GDVIRTUAL_TRACK(_gdvirtual_##$VARNAME);\\
				if (!_gdvirtual_##$VARNAME) {\\
					_gdvirtual_##$VARNAME = reinterpret_cast<void*>(_INVALID_GDVIRTUAL_FUNC_ADDR);\\
				}\\
				cached_virt = _gdvirtual_##$VARNAME;\\
			}\\
			if (likely(cached_virt != reinterpret_cast<void*>(_INVALID_GDVIRTUAL_FUNC_ADDR))) {\\
				return true;\\
			}\\
		}\\
		return false;\\
	}\\
	_FORCE_INLINE_ static MethodInfo _gdvirtual_##$VARNAME##_get_method_info() {\\
		MethodInfo method_info;\\
		method_info.name = #m_name;\\
		method_info.flags = $METHOD_FLAGS;\\
		$FILL_METHOD_INFO\\
		return method_info;\\
	}

"""


def generate_version(argcount, const=False, returns=False, required=False, compat=False):
    s = proto
    if compat:
        s = s.replace("$SCRIPTCALL", "")
        s = s.replace("$SCRIPTHASMETHOD", "")
    else:
        s = s.replace("$SCRIPTCALL", script_call)
        s = s.replace("$SCRIPTHASMETHOD", script_has_method)

    sproto = str(argcount)
    method_info = ""
    method_flags = "METHOD_FLAG_VIRTUAL"
    if returns:
        sproto += "R"
        s = s.replace("$RET", "m_ret,")
        s = s.replace("$RVOID", "(void)r_ret;")  # If required, may lead to uninitialized errors
        # Allocate and zero-initialize the return memory
        s = s.replace("$CALLPTRRETDEF", "PtrToArg<m_ret>::EncodeT *ret = SAFE_ALLOCA_SINGLE(PtrToArg<m_ret>::EncodeT);\\\n\t\t\t\tunaligned_write(ret, PtrToArg<m_ret>::EncodeT{});")
        method_info += "method_info.return_val = GetTypeInfo<m_ret>::get_class_info();\\\n"
        method_info += "\t\tmethod_info.return_val_metadata = GetTypeInfo<m_ret>::METADATA;"
    else:
        s = s.replace("$RET ", "")
        s = s.replace("\t\t$RVOID\\\n", "")
        s = s.replace("\t\t\t\t$CALLPTRRETDEF\\\n", "")
        s = s.replace("$CALLPTRRETDEF", "")

    if const:
        sproto += "C"
        method_flags += " | METHOD_FLAG_CONST"
        s = s.replace("$CONST", "const")
    else:
        s = s.replace("$CONST ", "")

    if required:
        sproto += "_REQUIRED"
        method_flags += " | METHOD_FLAG_VIRTUAL_REQUIRED"
        # Devirtualise get_class() resolving to bypass vtable
        s = s.replace(
            "$REQCHECK",
            'ERR_PRINT_ONCE("Required virtual method " + ThisClass::get_class_static() + "::" + #m_name + " must be overridden before calling.");',
        )
    else:
        s = s.replace("\t\t$REQCHECK\\\n", "")
        s = s.replace("$REQCHECK", "")

    if compat:
        sproto += "_COMPAT"
        s = s.replace("$COMPAT", "true")
        s = s.replace("$ALIAS", "m_alias,")
        s = s.replace("$VARNAME", "m_alias")
    else:
        s = s.replace("$COMPAT", "false")
        s = s.replace("$ALIAS ", "")
        s = s.replace("$VARNAME", "m_name")

    s = s.replace("$METHOD_FLAGS", method_flags)
    s = s.replace("$VER", sproto)
    argtext = ""
    callargtext = ""
    
    callsiargs = ""
    callsiargptrs = ""
    callsi_cleanup = ""
    
    callptrargs = ""
    callptrargsptr = ""

    if argcount > 0:
        argtext += ", "
        # Allocate memory on the stack
        callsiargs = f"\t\t\tVariant *vargs = SAFE_ALLOCA_ARRAY(Variant, {argcount});\\\n"
        callsiargptrs = f"\t\t\tconst Variant **vargptrs = SAFE_ALLOCA_ARRAY(const Variant *, {argcount});\\\n"
        callptrargsptr = f"\t\t\tGDExtensionConstTypePtr *argptrs = SAFE_ALLOCA_ARRAY(GDExtensionConstTypePtr, {argcount});\\\n"
        
    for i in range(argcount):
        if i > 0:
            argtext += ", "
            callargtext += ", "
            
        argtext += f"m_type{i + 1}"
        callargtext += f"m_type{i + 1} arg{i + 1}"
        
        # Script arguments
        callsiargs += f"\t\t\tmemnew_placement(&vargs[{i}], Variant);\\\n"
        callsiargs += f"\t\t\tvargs[{i}] = arg{i + 1};\\\n"
        callsiargptrs += f"\t\t\tvargptrs[{i}] = &vargs[{i}];\\\n"
        callsi_cleanup += f"\t\t\tvargs[{i}].~Variant();\\\n" # Manual destrunction for RefCount leak

        # Extension arguments
        callptrargs += f"\t\t\tPtrToArg<m_type{i + 1}>::EncodeT *argval{i + 1} = SAFE_ALLOCA_SINGLE(PtrToArg<m_type{i + 1}>::EncodeT);\\\n"
        callptrargs += f"\t\t\tunaligned_construct<PtrToArg<m_type{i + 1}>::EncodeT>(argval{i + 1}, (PtrToArg<m_type{i + 1}>::EncodeT)arg{i + 1});\\\n"
        callptrargsptr += f"\t\t\targptrs[{i}] = argval{i + 1};\\\n"
        
        if method_info:
            method_info += "\\\n\t\t"
        method_info += f"method_info.arguments.push_back(GetTypeInfo<m_type{i + 1}>::get_class_info());\\\n"
        method_info += f"\t\tmethod_info.arguments_metadata.push_back(GetTypeInfo<m_type{i + 1}>::METADATA);"

    if argcount:
        s = s.replace("$CALLSIARGS", callsiargs + callsiargptrs)
        s = s.replace("$CALLSIARGPASS", f"(const Variant **)vargptrs, {argcount}")
        s = s.replace("$CALLSICLEANUP", callsi_cleanup)
        
        s = s.replace("$CALLPTRARGS", callptrargs + callptrargsptr)
        s = s.replace("$CALLPTRARGPASS", "argptrs")
    else:
        s = s.replace("\t\t\t$CALLSIARGS\\\n", "")
        s = s.replace("$CALLSIARGS", "")
        s = s.replace("$CALLSIARGPASS", "nullptr, 0")
        s = s.replace("\t\t\t$CALLSICLEANUP\\\n", "")
        s = s.replace("$CALLSICLEANUP", "")
        
        s = s.replace("\t\t\t$CALLPTRARGS\\\n", "")
        s = s.replace("$CALLPTRARGS", "")
        s = s.replace("$CALLPTRARGPASS", "nullptr")

    if returns:
        if argcount > 0:
            callargtext += ", "
        callargtext += "m_ret &r_ret"
        s = s.replace("$CALLSIBEGIN", "Variant ret = ")
        s = s.replace("$CALLSIRET", "r_ret = VariantCaster<m_ret>::cast(ret);")
        
        s = s.replace("$CALLPTRRETPASS", "ret")
        s = s.replace("$CALLPTRRET", "r_ret = (m_ret)unaligned_read<PtrToArg<m_ret>::EncodeT>(ret);")
    else:
        s = s.replace("$CALLSIBEGIN", "")
        s = s.replace("\t\t\t\t$CALLSIRET\\\n", "")
        s = s.replace("$CALLSIRET", "")
        
        s = s.replace("$CALLPTRRETPASS", "nullptr")
        s = s.replace("\t\t\t\t\t$CALLPTRRET\\\n", "")
        s = s.replace("$CALLPTRRET", "")

    s = s.replace(" $ARG", argtext)
    s = s.replace("$CALLARGS", callargtext)
    if method_info:
        s = s.replace("$FILL_METHOD_INFO", method_info)
    else:
        s = s.replace("\t\t$FILL_METHOD_INFO\\\n", method_info)
        s = s.replace("$FILL_METHOD_INFO", method_info)

    return s


def run(target, source, env):
    max_versions = 12

    txt = """/* THIS FILE IS GENERATED DO NOT EDIT */
#ifndef GDVIRTUAL_GEN_H
#define GDVIRTUAL_GEN_H

#include "core/object/script_instance.h"

#include <utility>
inline constexpr uintptr_t _INVALID_GDVIRTUAL_FUNC_ADDR = static_cast<uintptr_t>(-1);

#ifdef TOOLS_ENABLED
#define _GDVIRTUAL_TRACK(m_virtual)\\
	if (ThisClass::_get_extension()->reloadable) {\\
		VirtualMethodTracker *tracker = memnew(VirtualMethodTracker);\\
		tracker->method = (void **)&m_virtual;\\
		tracker->next = virtual_method_list;\\
		virtual_method_list = tracker;\\
	}
#else
#define _GDVIRTUAL_TRACK(m_virtual)
#endif

#ifndef DISABLE_DEPRECATED
#define _GDVIRTUAL_GET_DEPRECATED(m_virtual, m_name_sn, m_compat)\\
	else if (m_compat || ClassDB::get_virtual_method_compatibility_hashes(ThisClass::get_class_static(), m_name_sn).size() == 0) {\\
		if (ThisClass::_get_extension()->get_virtual_call_data && ThisClass::_get_extension()->call_virtual_with_data) {\\
			m_virtual = ThisClass::_get_extension()->get_virtual_call_data(ThisClass::_get_extension()->class_userdata, &m_name_sn);\\
		} else if (ThisClass::_get_extension()->get_virtual) {\\
			m_virtual = (void *)ThisClass::_get_extension()->get_virtual(ThisClass::_get_extension()->class_userdata, &m_name_sn);\\
		}\\
	}
#else
#define _GDVIRTUAL_GET_DEPRECATED(m_virtual, m_name_sn, m_compat)
#endif

"""

    for i in range(max_versions + 1):
        txt += f"/* {i} Arguments */\n\n"
        txt += generate_version(i, False, False)
        txt += generate_version(i, False, True)
        txt += generate_version(i, True, False)
        txt += generate_version(i, True, True)
        txt += generate_version(i, False, False, True)
        txt += generate_version(i, False, True, True)
        txt += generate_version(i, True, False, True)
        txt += generate_version(i, True, True, True)
        txt += generate_version(i, False, False, False, True)
        txt += generate_version(i, False, True, False, True)
        txt += generate_version(i, True, False, False, True)
        txt += generate_version(i, True, True, False, True)

    txt += "#endif // GDVIRTUAL_GEN_H\n"

    with open(str(target[0]), "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)
