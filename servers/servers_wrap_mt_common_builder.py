import os
import sys

HEADER = """
/* THIS FILE IS AUTO-GENERATED. DO NOT EDIT DIRECTLY. */

#ifndef SERVERS_GEN_SERVER_WRAP_MT_COMMON_GEN_H
#define SERVERS_GEN_SERVER_WRAP_MT_COMMON_GEN_H

#include "core/object/class_temperature.h"

#ifdef DEBUG_ENABLED
#define MAIN_THREAD_SYNC_CHECK \\
	if (unlikely(Thread::is_main_thread() && Engine::get_singleton()->notify_frame_server_synced())) { \\
		MAIN_THREAD_SYNC_WARN \\
	}
#else
#define MAIN_THREAD_SYNC_CHECK
#endif

template <typename T, auto Method, typename... Args>
struct ServerWrapThunk {
	static void call(T *p_instance, Args... p_args) {
		(p_instance->*Method)(p_args...);
	}
};

template <typename T, auto Method, typename R, typename... Args>
struct ServerWrapRetThunk {
	static R call(T *p_instance, Args... p_args) {
		return (p_instance->*Method)(p_args...);
	}
};

#define FUNCRIDSPLIT(m_type) \\
	inline RID m_type##_create() { \\
		RID ret = server_name->m_type##_allocate(); \\
		if (unlikely(Thread::get_caller_id() != server_thread)) { \\
			command_queue.push_thunk(server_name, &ServerWrapThunk<ServerName, &ServerName::m_type##_initialize, RID>::call, ret); \\
		} else { \\
			server_name->m_type##_initialize(ret); \\
		} \\
		return ret; \\
	}

#define FUNCRID(m_type) \\
	inline RID m_type##_create() { \\
		return server_name->m_type##_create(); \\
	}
"""

FOOTER = """
#endif // SERVERS_GEN_SERVER_WRAP_MT_COMMON_GEN_H
"""

def get_args_decl(n, has_ret):
    args = []
    if has_ret:
        args.append("m_r")
    args.append("m_type")
    args.extend([f"m_arg{i+1}" for i in range(n)])
    return ", ".join(args)

def get_params_decl(n):
    return ", ".join([f"m_arg{i+1} p{i+1}" for i in range(n)])

def get_params_pass(n):
    return "".join([f", p{i+1}" for i in range(n)])

def get_thunk_args(n, has_ret):
    args = []
    if has_ret:
        args.append("m_r")
    args.extend([f"m_arg{i+1}" for i in range(n)])
    return ", ".join(args)

def generate_macro(n, suffix, is_const, is_sync, has_ret):
    macro_name = f"FUNC{n}{suffix}"
    args_decl = get_args_decl(n, has_ret)
    params_decl = get_params_decl(n)
    params_pass = get_params_pass(n)
    thunk_args = get_thunk_args(n, has_ret)
    
    const_qualifier = " const" if is_const else ""
    write_action = "\t\tWRITE_ACTION \\\n" if not is_const else ""
    
    ret_type = "m_r" if has_ret else "void"
    
    if thunk_args:
        thunk_template_args = f", {thunk_args}"
    else:
        thunk_template_args = ""

    if has_ret:
        push_func = "push_and_ret_thunk"
        thunk_struct = "ServerWrapRetThunk"
        ret_decl = f"\t\t\tm_r ret; \\\n"
        ret_pass = ", &ret"
        sync_logic = "\t\t\tSYNC_DEBUG \\\n\t\t\tMAIN_THREAD_SYNC_CHECK \\\n\t\t\treturn ret; \\\n"
        direct_call = f"return server_name->m_type({params_pass[2:] if n > 0 else ''});"
    elif is_sync:
        push_func = "push_and_sync_thunk"
        thunk_struct = "ServerWrapThunk"
        ret_decl = ""
        ret_pass = ""
        sync_logic = "\t\t\tSYNC_DEBUG \\\n\t\t\tMAIN_THREAD_SYNC_CHECK \\\n"
        direct_call = f"server_name->m_type({params_pass[2:] if n > 0 else ''});"
    else:
        push_func = "push_thunk"
        thunk_struct = "ServerWrapThunk"
        ret_decl = ""
        ret_pass = ""
        sync_logic = ""
        direct_call = f"server_name->m_type({params_pass[2:] if n > 0 else ''});"

    out = f"#define {macro_name}({args_decl}) \\\n"
    out += f"\tinline {ret_type} m_type({params_decl}){const_qualifier} {{ \\\n"
    out += write_action
    out += f"\t\tif (unlikely(Thread::get_caller_id() != server_thread)) {{ \\\n"
    out += ret_decl
    out += f"\t\t\tcommand_queue.{push_func}(server_name, &{thunk_struct}<ServerName, &ServerName::m_type{thunk_template_args}>::call{ret_pass}{params_pass}); \\\n"
    out += sync_logic
    out += f"\t\t}} else {{ \\\n"
    out += f"\t\t\tcommand_queue.flush_if_pending(); \\\n"
    out += f"\t\t\t{direct_call} \\\n"
    out += f"\t\t}} \\\n"
    out += f"\t}}\n\n"
    return out

def make_wrap_mt_builder(target, source, env):
    output_path = str(target[0])
    output_dir = os.path.dirname(output_path)

    try:
        os.makedirs(output_dir, exist_ok=True)
        
        with open(output_path, "w") as f:
            f.write(HEADER + "\n")
            
            for n in range(16):
                f.write(f"// --- ARITY {n} ---\n\n")
                f.write(generate_macro(n, "R",   is_const=False, is_sync=True,  has_ret=True))
                f.write(generate_macro(n, "RC",  is_const=True,  is_sync=True,  has_ret=True))
                f.write(generate_macro(n, "S",   is_const=False, is_sync=True,  has_ret=False))
                f.write(generate_macro(n, "SC",  is_const=True,  is_sync=True,  has_ret=False))
                f.write(generate_macro(n, "",    is_const=False, is_sync=False, has_ret=False))
                f.write(generate_macro(n, "C",   is_const=True,  is_sync=False, has_ret=False))
                
            f.write(FOOTER)
        print(f"Successfully generated {output_path}")
    except Exception as e:
        print(f"Error generating file: {e}")
