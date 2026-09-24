/**************************************************************************/
/*  method_bind.h                                                         */
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

#ifndef METHOD_BIND_H
#define METHOD_BIND_H

#include "core/variant/binder_common.h"

VARIANT_BITFIELD_CAST(MethodFlags)

// some helpers

// Centralised Vtable
struct MethodBindVTable {
	Variant (*call)(const MethodBind *, Object *, const Variant **, int, Callable::CallError &);
	void (*validated_call)(const MethodBind *, Object *, const Variant **, Variant *);
	void (*ptrcall)(const MethodBind *, Object *, const void **, void *);

	Variant::Type (*gen_argument_type)(const MethodBind *, int);
	PropertyInfo (*gen_argument_type_info)(const MethodBind *, int);

#ifdef DEBUG_METHODS_ENABLED
	GodotTypeInfo::Metadata (*get_argument_meta)(const MethodBind *, int);
#endif

#ifdef TOOLS_ENABLED
	bool (*is_valid)(const MethodBind *);
#endif

	bool (*is_vararg)(const MethodBind *);
};

template <typename>
struct MethodBindInstanceType;

template <typename R, typename T, typename... Args >
struct MethodBindInstanceType<R (T::*)(Args...)> {
	using type = T;
};

template < typename R, typename T, typename... Args >
struct MethodBindInstanceType<R (T::*)(Args...) const> {
	using type = T;
};

class MethodBind {
	const MethodBindVTable *vtable = nullptr;
	int method_id;
	uint32_t hint_flags = METHOD_FLAGS_DEFAULT;
	StringName name;
	StringName instance_class;
	Vector<Variant> default_arguments;
	int default_argument_count = 0;
	int argument_count = 0;

	bool _static = false;
	bool _const = false;
	bool _returns = false;
	bool _returns_raw_obj_ptr = false;

protected:
	Variant::Type *argument_types = nullptr;
#ifdef DEBUG_METHODS_ENABLED
	Vector<StringName> arg_names;
#endif
	void _set_const(bool p_const);
	void _set_static(bool p_static);
	void _set_returns(bool p_returns);

	Variant::Type _gen_argument_type(int p_arg) const {
		return vtable->gen_argument_type(this, p_arg);
	}

	PropertyInfo _gen_argument_type_info(int p_arg) const {
		return vtable->gen_argument_type_info(this, p_arg);
	}

	void _generate_argument_types(int p_count);

	void set_argument_count(int p_count) { argument_count = p_count; }

public:
	_FORCE_INLINE_ const Vector<Variant> &get_default_arguments() const { return default_arguments; }
	_FORCE_INLINE_ int get_default_argument_count() const { return default_argument_count; }

	_FORCE_INLINE_ Variant has_default_argument(int p_arg) const {
		int idx = p_arg - (argument_count - default_arguments.size());

		if (idx < 0 || idx >= default_arguments.size()) {
			return false;
		} else {
			return true;
		}
	}

	_FORCE_INLINE_ Variant get_default_argument(int p_arg) const {
		int idx = p_arg - (argument_count - default_arguments.size());

		if (idx < 0 || idx >= default_arguments.size()) {
			return Variant();
		} else {
			return default_arguments[idx];
		}
	}

	_FORCE_INLINE_ Variant::Type get_argument_type(int p_argument) const {
		ERR_FAIL_COND_V(p_argument < -1 || p_argument >= argument_count, Variant::NIL);
		return argument_types[p_argument + 1];
	}

	PropertyInfo get_argument_info(int p_argument) const;
	PropertyInfo get_return_info() const;

#ifdef DEBUG_METHODS_ENABLED
	void set_argument_names(const Vector<StringName> &p_names);
	Vector<StringName> get_argument_names() const;

	GodotTypeInfo::Metadata get_argument_meta(int p_arg) const {
		return vtable->get_argument_meta(this, p_arg);
	}
#endif

	void set_hint_flags(uint32_t p_hint) { hint_flags = p_hint; }
	uint32_t get_hint_flags() const { return hint_flags | (is_const() ? METHOD_FLAG_CONST : 0) | (is_vararg() ? METHOD_FLAG_VARARG : 0) | (is_static() ? METHOD_FLAG_STATIC : 0); }
	_FORCE_INLINE_ StringName get_instance_class() const { return instance_class; }
	_FORCE_INLINE_ void set_instance_class(const StringName &p_class) { instance_class = p_class; }

	_FORCE_INLINE_ int get_argument_count() const { return argument_count; }

#ifdef TOOLS_ENABLED
	bool is_valid() const {
		if (vtable->is_valid) {
			return vtable->is_valid(this);
		}
		return true;
	}
#endif

	Variant call(Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) const {
		return vtable->call(this, p_object, p_args, p_arg_count, r_error);
	}

	void validated_call(Object *p_object, const Variant **p_args, Variant *r_ret) const {
		vtable->validated_call(this, p_object, p_args, r_ret);
	}

	void ptrcall(Object *p_object, const void **p_args, void *r_ret) const {
		vtable->ptrcall(this, p_object, p_args, r_ret);
	}

	StringName get_name() const;
	void set_name(const StringName &p_name);
	_FORCE_INLINE_ int get_method_id() const { return method_id; }
	_FORCE_INLINE_ bool is_const() const { return _const; }
	_FORCE_INLINE_ bool is_static() const { return _static; }
	_FORCE_INLINE_ bool has_return() const { return _returns; }

	bool is_vararg() const { return vtable->is_vararg(this); }

	_FORCE_INLINE_ bool is_return_type_raw_object_ptr() { return _returns_raw_obj_ptr; }
	_FORCE_INLINE_ void set_return_type_is_raw_object_ptr(bool p_returns_raw_obj) { _returns_raw_obj_ptr = p_returns_raw_obj; }

	void set_default_arguments(const Vector<Variant> &p_defargs);

	uint32_t get_hash() const;

	MethodBind(const MethodBindVTable *p_vtable);
	virtual ~MethodBind();
};

// MethodBindVarArg base CRTP
template <typename Derived, typename T, typename R, bool should_returns>
class MethodBindVarArgBase : public MethodBind {
protected:
	R (T::*method)(const Variant **, int, Callable::CallError &);
	MethodInfo method_info;

public:
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		const MethodBindVarArgBase *self = static_cast<const MethodBindVarArgBase *>(p_bind);
		if (p_arg < 0) {
			return Derived::_gen_return_type_info_impl();
		} else if (p_arg < self->method_info.arguments.size()) {
			return self->method_info.arguments.get(p_arg);
		} else {
			return PropertyInfo(Variant::NIL, "arg_" + itos(p_arg), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT);
		}
	}

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		return _gen_argument_type_info_bind(p_bind, p_arg).type;
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return GodotTypeInfo::METADATA_NONE;
	}
#endif

	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		ERR_FAIL_MSG("Validated call can't be used with vararg methods. This is a bug.");
	}

	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		ERR_FAIL_MSG("ptrcall can't be used with vararg methods. This is a bug.");
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return true; }

	MethodBindVarArgBase(
			R (T::*p_method)(const Variant **, int, Callable::CallError &),
			const MethodInfo &p_method_info,
			bool p_return_nil_is_variant,
			const MethodBindVTable *p_vtable) :
			MethodBind(p_vtable), method(p_method), method_info(p_method_info) {
		set_argument_count(method_info.arguments.size());
		Variant::Type *at = memnew_arr(Variant::Type, method_info.arguments.size() + 1);
		ERR_FAIL_NULL(at);
		at[0] = Derived::_gen_return_type_info_impl().type;
		if (method_info.arguments.size()) {
#ifdef DEBUG_METHODS_ENABLED
			Vector<StringName> names;
			names.resize(method_info.arguments.size());
#endif
			int i = 0;
			for (List<PropertyInfo>::ConstIterator itr = method_info.arguments.begin(); itr != method_info.arguments.end(); ++itr, ++i) {
				at[i + 1] = itr->type;
#ifdef DEBUG_METHODS_ENABLED
				names.write[i] = itr->name;
#endif
			}

#ifdef DEBUG_METHODS_ENABLED
			set_argument_names(names);
#endif
		}
		argument_types = at;
		if (p_return_nil_is_variant) {
			method_info.return_val.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		}

		_set_returns(should_returns);
	}
};

// variadic, no return
template <typename T>
class MethodBindVarArgT : public MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false> {
	friend class MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>;

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		const MethodBindVarArgT *self = static_cast<const MethodBindVarArgT *>(p_bind);
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == self->get_instance_class(), Variant(), vformat("Cannot call method bind '%s' on placeholder instance.", self->get_name()));
#endif
		(static_cast<T *>(p_object)->*self->method)(p_args, p_arg_count, r_error);
		return {};
	}

	static const MethodBindVTable dispatcher;

public:
	MethodBindVarArgT(
			void (T::*p_method)(const Variant **, int, Callable::CallError &),
			const MethodInfo &p_method_info,
			bool p_return_nil_is_variant) :
			MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>(p_method, p_method_info, p_return_nil_is_variant, &dispatcher) {
	}

private:
	static PropertyInfo _gen_return_type_info_impl() { return {}; }
};

template <typename T>
const MethodBindVTable MethodBindVarArgT<T>::dispatcher = {
	&MethodBindVarArgT::_call_bind,
	&MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>::_validated_call_bind,
	&MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>::_ptrcall_bind,
	&MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>::_gen_argument_type_bind,
	&MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindVarArgBase<MethodBindVarArgT<T>, T, void, false>::_is_vararg_bind
};

template <typename T>
MethodBind *create_vararg_method_bind(void (T::*p_method)(const Variant **, int, Callable::CallError &), const MethodInfo &p_info, bool p_return_nil_is_variant) {
	MethodBind *a = memnew((MethodBindVarArgT<T>)(p_method, p_info, p_return_nil_is_variant));
	a->set_instance_class(T::get_class_static());
	return a;
}

// variadic, return
template <typename T, typename R>
class MethodBindVarArgTR : public MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true> {
	friend class MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>;

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		const MethodBindVarArgTR *self = static_cast<const MethodBindVarArgTR *>(p_bind);
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == self->get_instance_class(), Variant(), vformat("Cannot call method bind '%s' on placeholder instance.", self->get_name()));
#endif
		return (static_cast<T *>(p_object)->*self->method)(p_args, p_arg_count, r_error);
	}

	static const MethodBindVTable dispatcher;

public:
	MethodBindVarArgTR(
			R (T::*p_method)(const Variant **, int, Callable::CallError &),
			const MethodInfo &p_info,
			bool p_return_nil_is_variant) :
			MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>(p_method, p_info, p_return_nil_is_variant, &dispatcher) {
	}

private:
	static PropertyInfo _gen_return_type_info_impl() { return GetTypeInfo<R>::get_class_info(); }
};

template <typename T, typename R>
const MethodBindVTable MethodBindVarArgTR<T, R>::dispatcher = {
	&MethodBindVarArgTR::_call_bind,
	&MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>::_validated_call_bind,
	&MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>::_ptrcall_bind,
	&MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>::_gen_argument_type_bind,
	&MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindVarArgBase<MethodBindVarArgTR<T, R>, T, R, true>::_is_vararg_bind
};

template <typename T, typename R>
MethodBind *create_vararg_method_bind(R (T::*p_method)(const Variant **, int, Callable::CallError &), const MethodInfo &p_info, bool p_return_nil_is_variant) {
	MethodBind *a = memnew((MethodBindVarArgTR<T, R>)(p_method, p_info, p_return_nil_is_variant));
	a->set_instance_class(T::get_class_static());
	return a;
}

/**** VARIADIC TEMPLATES ****/

#ifndef TYPED_METHOD_BIND
class __UnexistingClass;
#define MB_T __UnexistingClass
#else
#define MB_T T
#endif

// no return, not const
#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename... P>
#else
template <auto m_method, typename... P>
#endif
class MethodBindStaticT : public MethodBind {
#ifndef TYPED_METHOD_BIND
	using T = typename MethodBindInstanceType<decltype(m_method)>::type;
#endif

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		} else {
			return Variant::NIL;
		}
	}

	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		PropertyInfo pi;
		call_get_argument_type_info<P...>(p_arg, pi);
		return pi;
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return call_get_argument_metadata<P...>(p_arg);
	}
#endif

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), Variant(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_variant_args_dv(static_cast<T *>(p_object), m_method, p_args, p_arg_count, r_error, p_bind->get_default_arguments());
		return Variant();
	}

	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_validated_object_instance_args(static_cast<T *>(p_object), m_method, p_args);
	}
	
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_ptr_args<T, P...>(static_cast<T *>(p_object), m_method, p_args);
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }

	static const MethodBindVTable dispatcher;

public:
	MethodBindStaticT() :
			MethodBind(&dispatcher) {
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename... P>
const MethodBindVTable MethodBindStaticT<T, m_method, P...>::dispatcher = {
#else
template <auto m_method, typename... P>
const MethodBindVTable MethodBindStaticT<m_method, P...>::dispatcher = {
#endif
	&MethodBindStaticT::_call_bind,
	&MethodBindStaticT::_validated_call_bind,
	&MethodBindStaticT::_ptrcall_bind,
	&MethodBindStaticT::_gen_argument_type_bind,
	&MethodBindStaticT::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindStaticT::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindStaticT::_is_vararg_bind
};

// no return, const

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename... P>
#else
template <auto m_method, typename... P>
#endif
class MethodBindStaticTC : public MethodBind {
#ifndef TYPED_METHOD_BIND
	using T = typename MethodBindInstanceType<decltype(m_method)>::type;
#endif

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		} else {
			return Variant::NIL;
		}
	}

	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		PropertyInfo pi;
		call_get_argument_type_info<P...>(p_arg, pi);
		return pi;
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return call_get_argument_metadata<P...>(p_arg);
	}
#endif

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), Variant(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_variant_argsc_dv(static_cast<T *>(p_object), m_method, p_args, p_arg_count, r_error, p_bind->get_default_arguments());
		return Variant();
	}
	
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_validated_object_instance_argsc(static_cast<T *>(p_object), m_method, p_args);
	}
	
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_ptr_argsc<T, P...>(static_cast<T *>(p_object), m_method, p_args);
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindStaticTC() :
			MethodBind(&dispatcher) {
		_set_const(true);
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename... P>
const MethodBindVTable MethodBindStaticTC<T, m_method, P...>::dispatcher = {
#else
template <auto m_method, typename... P>
const MethodBindVTable MethodBindStaticTC<m_method, P...>::dispatcher = {
#endif
	&MethodBindStaticTC::_call_bind,
	&MethodBindStaticTC::_validated_call_bind,
	&MethodBindStaticTC::_ptrcall_bind,
	&MethodBindStaticTC::_gen_argument_type_bind,
	&MethodBindStaticTC::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindStaticTC::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindStaticTC::_is_vararg_bind
};

// return, not const

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename R, typename... P>
#else
template <auto m_method, typename R, typename... P>
#endif
class MethodBindStaticTR : public MethodBind {
#ifndef TYPED_METHOD_BIND
	using T = typename MethodBindInstanceType<decltype(m_method)>::type;
#endif

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		} else {
			return GetTypeInfo<R>::VARIANT_TYPE;
		}
	}

	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			PropertyInfo pi;
			call_get_argument_type_info<P...>(p_arg, pi);
			return pi;
		} else {
			return GetTypeInfo<R>::get_class_info();
		}
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0) {
			return call_get_argument_metadata<P...>(p_arg);
		} else {
			return GetTypeInfo<R>::METADATA;
		}
	}
#endif

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		Variant ret;
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), ret, vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_variant_args_ret_dv(static_cast<T *>(p_object), m_method, p_args, p_arg_count, ret, r_error, p_bind->get_default_arguments());
		return ret;
	}

	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_validated_object_instance_args_ret(static_cast<T *>(p_object), m_method, p_args, r_ret);
	}

	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_ptr_args_ret<T, R, P...>(static_cast<T *>(p_object), m_method, p_args, r_ret);
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindStaticTR() :
			MethodBind(&dispatcher) {
		_set_returns(true);
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename R, typename... P>
const MethodBindVTable MethodBindStaticTR<T, m_method, R, P...>::dispatcher = {
#else
template <auto m_method, typename R, typename... P>
const MethodBindVTable MethodBindStaticTR<m_method, R, P...>::dispatcher = {
#endif
	&MethodBindStaticTR::_call_bind,
	&MethodBindStaticTR::_validated_call_bind,
	&MethodBindStaticTR::_ptrcall_bind,
	&MethodBindStaticTR::_gen_argument_type_bind,
	&MethodBindStaticTR::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindStaticTR::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindStaticTR::_is_vararg_bind
};

// return, const

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename R, typename... P>
#else
template <auto m_method, typename R, typename... P>
#endif
class MethodBindStaticTRC : public MethodBind {
#ifndef TYPED_METHOD_BIND
	using T = typename MethodBindInstanceType<decltype(m_method)>::type;
#endif

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		} else {
			return GetTypeInfo<R>::VARIANT_TYPE;
		}
	}

	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			PropertyInfo pi;
			call_get_argument_type_info<P...>(p_arg, pi);
			return pi;
		} else {
			return GetTypeInfo<R>::get_class_info();
		}
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0) {
			return call_get_argument_metadata<P...>(p_arg);
		} else {
			return GetTypeInfo<R>::METADATA;
		}
	}
#endif

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		Variant ret;
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), ret, vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_variant_args_retc_dv(static_cast<T* >(p_object), m_method, p_args, p_arg_count, ret, r_error, p_bind->get_default_arguments());
		return ret;
	}

	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_validated_object_instance_args_retc(static_cast<T* >(p_object), m_method, p_args, r_ret);
	}

	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == p_bind->get_instance_class(), vformat("Cannot call method bind '%s' on placeholder instance.", p_bind->get_name()));
#endif
		call_with_ptr_args_retc<T, R, P...>(static_cast<T* >(p_object), m_method, p_args, r_ret);
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindStaticTRC() :
			MethodBind(&dispatcher) {
		_set_returns(true);
		_set_const(true);
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, auto m_method, typename R, typename... P>
const MethodBindVTable MethodBindStaticTRC<T, m_method, R, P...>::dispatcher = {
#else
template <auto m_method, typename R, typename... P>
const MethodBindVTable MethodBindStaticTRC<m_method, R, P...>::dispatcher = {
#endif
	&MethodBindStaticTRC::_call_bind,
	&MethodBindStaticTRC::_validated_call_bind,
	&MethodBindStaticTRC::_ptrcall_bind,
	&MethodBindStaticTRC::_gen_argument_type_bind,
	&MethodBindStaticTRC::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindStaticTRC::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindStaticTRC::_is_vararg_bind
};

/* STATIC BINDS */

// no return

template <auto m_method, typename... P>
class MethodBindStaticTS : public MethodBind {
	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		} else {
			return Variant::NIL;
		}
	}

	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		PropertyInfo pi;
		call_get_argument_type_info<P...>(p_arg, pi);
		return pi;
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return call_get_argument_metadata<P...>(p_arg);
	}
#endif

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		(void)p_object; // unused
		call_with_variant_args_static_dv(m_method, p_args, p_arg_count, r_error, p_bind->get_default_arguments());
		return Variant();
	}

	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		call_with_validated_variant_args_static_method(m_method, p_args);
	}

	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		(void)p_object;
		(void)r_ret;
		call_with_ptr_args_static_method(m_method, p_args);
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindStaticTS() :
			MethodBind(&dispatcher) {
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
		_set_static(true);
	}
};

template <auto m_method, typename... P>
const MethodBindVTable MethodBindStaticTS<m_method, P...>::dispatcher = {
	&MethodBindStaticTS::_call_bind,
	&MethodBindStaticTS::_validated_call_bind,
	&MethodBindStaticTS::_ptrcall_bind,
	&MethodBindStaticTS::_gen_argument_type_bind,
	&MethodBindStaticTS::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindStaticTS::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindStaticTS::_is_vararg_bind
};

// return

template <auto m_method, typename R, typename... P>
class MethodBindStaticTRS : public MethodBind {
	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		} else {
			return GetTypeInfo<R>::VARIANT_TYPE;
		}
	}

	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			PropertyInfo pi;
			call_get_argument_type_info<P...>(p_arg, pi);
			return pi;
		} else {
			return GetTypeInfo<R>::get_class_info();
		}
	}

#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0) {
			return call_get_argument_metadata<P...>(p_arg);
		} else {
			return GetTypeInfo<R>::METADATA;
		}
	}
#endif

	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		Variant ret;
		call_with_variant_args_static_ret_dv(m_method, p_args, p_arg_count, ret, r_error, p_bind->get_default_arguments());
		return ret;
	}

	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		call_with_validated_variant_args_static_method_ret(m_method, p_args, r_ret);
	}

	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		(void)p_object;
		call_with_ptr_args_static_method_ret(m_method, p_args, r_ret);
	}

	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindStaticTRS() :
			MethodBind(&dispatcher) {
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
		_set_static(true);
		_set_returns(true);
	}
};

template <auto m_method, typename R, typename... P>
const MethodBindVTable MethodBindStaticTRS<m_method, R, P...>::dispatcher = {
	&MethodBindStaticTRS::_call_bind,
	&MethodBindStaticTRS::_validated_call_bind,
	&MethodBindStaticTRS::_ptrcall_bind,
	&MethodBindStaticTRS::_gen_argument_type_bind,
	&MethodBindStaticTRS::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindStaticTRS::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindStaticTRS::_is_vararg_bind
};

/* RUNTIME METHOD BINDS */

// no return, not const
#ifdef TYPED_METHOD_BIND
template <typename T, typename... P>
#else
template <typename... P>
#endif
class MethodBindT : public MethodBind {
	void (MB_T::*method)(P...);

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		}
		return Variant::NIL;
	}
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		PropertyInfo pi;
		call_get_argument_type_info<P...>(p_arg, pi);
		return pi;
	}
#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return call_get_argument_metadata<P...>(p_arg);
	}
#endif
	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		const MethodBindT *self = static_cast<const MethodBindT *>(p_bind);
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == self->get_instance_class(), Variant(), vformat("Cannot call method bind '%s' on placeholder instance.", self->get_name()));
#endif
#ifdef TYPED_METHOD_BIND
		call_with_variant_args_dv(static_cast<T *>(p_object), self->method, p_args, p_arg_count, r_error, self->get_default_arguments());
#else
		call_with_variant_args_dv(reinterpret_cast<MB_T *>(p_object), self->method, p_args, p_arg_count, r_error, self->get_default_arguments());
#endif
		return Variant();
	}
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		const MethodBindT *self = static_cast<const MethodBindT *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_validated_object_instance_args(static_cast<T *>(p_object), self->method, p_args);
#else
		call_with_validated_object_instance_args(reinterpret_cast<MB_T *>(p_object), self->method, p_args);
#endif
	}
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		const MethodBindT *self = static_cast<const MethodBindT *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_ptr_args<T, P...>(static_cast<T *>(p_object), self->method, p_args);
#else
		call_with_ptr_args<MB_T, P...>(reinterpret_cast<MB_T *>(p_object), self->method, p_args);
#endif
	}
	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindT(void (MB_T::*p_method)(P...)) :
			MethodBind(&dispatcher) {
		method = p_method;
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, typename... P>
const MethodBindVTable MethodBindT<T, P...>::dispatcher = {
#else
template <typename... P>
const MethodBindVTable MethodBindT<P...>::dispatcher = {
#endif
	&MethodBindT::_call_bind, &MethodBindT::_validated_call_bind, &MethodBindT::_ptrcall_bind, &MethodBindT::_gen_argument_type_bind, &MethodBindT::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindT::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindT::_is_vararg_bind
};

template <typename T, typename... P>
MethodBind *create_method_bind(void (T::*p_method)(P...)) {
#ifdef TYPED_METHOD_BIND
	MethodBind *a = memnew((MethodBindT<T, P...>)(p_method));
#else
	MethodBind *a = memnew((MethodBindT<P...>)(reinterpret_cast<void (MB_T::*)(P...)>(p_method)));
#endif
	a->set_instance_class(T::get_class_static());
	return a;
}

// no return, const
#ifdef TYPED_METHOD_BIND
template <typename T, typename... P>
#else
template <typename... P>
#endif
class MethodBindTC : public MethodBind {
	void (MB_T::*method)(P...) const;

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		}
		return Variant::NIL;
	}
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		PropertyInfo pi;
		call_get_argument_type_info<P...>(p_arg, pi);
		return pi;
	}
#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return call_get_argument_metadata<P...>(p_arg);
	}
#endif
	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		const MethodBindTC *self = static_cast<const MethodBindTC *>(p_bind);
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == self->get_instance_class(), Variant(), vformat("Cannot call method bind '%s' on placeholder instance.", self->get_name()));
#endif
#ifdef TYPED_METHOD_BIND
		call_with_variant_argsc_dv(static_cast<T *>(p_object), self->method, p_args, p_arg_count, r_error, self->get_default_arguments());
#else
		call_with_variant_argsc_dv(reinterpret_cast<MB_T *>(p_object), self->method, p_args, p_arg_count, r_error, self->get_default_arguments());
#endif
		return Variant();
	}
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		const MethodBindTC *self = static_cast<const MethodBindTC *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_validated_object_instance_argsc(static_cast<T *>(p_object), self->method, p_args);
#else
		call_with_validated_object_instance_argsc(reinterpret_cast<MB_T *>(p_object), self->method, p_args);
#endif
	}
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		const MethodBindTC *self = static_cast<const MethodBindTC *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_ptr_argsc<T, P...>(static_cast<T *>(p_object), self->method, p_args);
#else
		call_with_ptr_argsc<MB_T, P...>(reinterpret_cast<MB_T *>(p_object), self->method, p_args);
#endif
	}
	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindTC(void (MB_T::*p_method)(P...) const) :
			MethodBind(&dispatcher) {
		method = p_method;
		_set_const(true);
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, typename... P>
const MethodBindVTable MethodBindTC<T, P...>::dispatcher = {
#else
template <typename... P>
const MethodBindVTable MethodBindTC<P...>::dispatcher = {
#endif
	&MethodBindTC::_call_bind, &MethodBindTC::_validated_call_bind, &MethodBindTC::_ptrcall_bind, &MethodBindTC::_gen_argument_type_bind, &MethodBindTC::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindTC::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindTC::_is_vararg_bind
};

template <typename T, typename... P>
MethodBind *create_method_bind(void (T::*p_method)(P...) const) {
#ifdef TYPED_METHOD_BIND
	MethodBind *a = memnew((MethodBindTC<T, P...>)(p_method));
#else
	MethodBind *a = memnew((MethodBindTC<P...>)(reinterpret_cast<void (MB_T::*)(P...) const>(p_method)));
#endif
	a->set_instance_class(T::get_class_static());
	return a;
}

// return, not const
#ifdef TYPED_METHOD_BIND
template <typename T, typename R, typename... P>
#else
template <typename R, typename... P>
#endif
class MethodBindTR : public MethodBind {
	R (MB_T::*method)(P...);

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		}
		return GetTypeInfo<R>::VARIANT_TYPE;
	}
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			PropertyInfo pi;
			call_get_argument_type_info<P...>(p_arg, pi);
			return pi;
		}
		return GetTypeInfo<R>::get_class_info();
	}
#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0) {
			return call_get_argument_metadata<P...>(p_arg);
		}
		return GetTypeInfo<R>::METADATA;
	}
#endif
	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		Variant ret;
		const MethodBindTR *self = static_cast<const MethodBindTR *>(p_bind);
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == self->get_instance_class(), ret, vformat("Cannot call method bind '%s' on placeholder instance.", self->get_name()));
#endif
#ifdef TYPED_METHOD_BIND
		call_with_variant_args_ret_dv(static_cast<T *>(p_object), self->method, p_args, p_arg_count, ret, r_error, self->get_default_arguments());
#else
		call_with_variant_args_ret_dv(reinterpret_cast<MB_T *>(p_object), self->method, p_args, p_arg_count, ret, r_error, self->get_default_arguments());
#endif
		return ret;
	}
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		const MethodBindTR *self = static_cast<const MethodBindTR *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_validated_object_instance_args_ret(static_cast<T *>(p_object), self->method, p_args, r_ret);
#else
		call_with_validated_object_instance_args_ret(reinterpret_cast<MB_T *>(p_object), self->method, p_args, r_ret);
#endif
	}
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		const MethodBindTR *self = static_cast<const MethodBindTR *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_ptr_args_ret<T, R, P...>(static_cast<T *>(p_object), self->method, p_args, r_ret);
#else
		call_with_ptr_args_ret<MB_T, R, P...>(reinterpret_cast<MB_T *>(p_object), self->method, p_args, r_ret);
#endif
	}
	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindTR(R (MB_T::*p_method)(P...)) :
			MethodBind(&dispatcher) {
		method = p_method;
		_set_returns(true);
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, typename R, typename... P>
const MethodBindVTable MethodBindTR<T, R, P...>::dispatcher = {
#else
template <typename R, typename... P>
const MethodBindVTable MethodBindTR<R, P...>::dispatcher = {
#endif
	&MethodBindTR::_call_bind, &MethodBindTR::_validated_call_bind, &MethodBindTR::_ptrcall_bind, &MethodBindTR::_gen_argument_type_bind, &MethodBindTR::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindTR::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindTR::_is_vararg_bind
};

template <typename T, typename R, typename... P>
MethodBind *create_method_bind(R (T::*p_method)(P...)) {
#ifdef TYPED_METHOD_BIND
	MethodBind *a = memnew((MethodBindTR<T, R, P...>)(p_method));
#else
	MethodBind *a = memnew((MethodBindTR<R, P...>)(reinterpret_cast<R (MB_T::*)(P...)>(p_method)));
#endif
	a->set_instance_class(T::get_class_static());
	return a;
}

// return, const
#ifdef TYPED_METHOD_BIND
template <typename T, typename R, typename... P>
#else
template <typename R, typename... P>
#endif
class MethodBindTRC : public MethodBind {
	R (MB_T::*method)(P...) const;

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		}
		return GetTypeInfo<R>::VARIANT_TYPE;
	}
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			PropertyInfo pi;
			call_get_argument_type_info<P...>(p_arg, pi);
			return pi;
		}
		return GetTypeInfo<R>::get_class_info();
	}
#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0) {
			return call_get_argument_metadata<P...>(p_arg);
		}
		return GetTypeInfo<R>::METADATA;
	}
#endif
	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		Variant ret;
		const MethodBindTRC *self = static_cast<const MethodBindTRC *>(p_bind);
#ifdef TOOLS_ENABLED
		ERR_FAIL_COND_V_MSG(p_object && p_object->is_extension_placeholder() && p_object->get_class_name() == self->get_instance_class(), ret, vformat("Cannot call method bind '%s' on placeholder instance.", self->get_name()));
#endif
#ifdef TYPED_METHOD_BIND
		call_with_variant_args_retc_dv(static_cast<T *>(p_object), self->method, p_args, p_arg_count, ret, r_error, self->get_default_arguments());
#else
		call_with_variant_args_retc_dv(reinterpret_cast<MB_T *>(p_object), self->method, p_args, p_arg_count, ret, r_error, self->get_default_arguments());
#endif
		return ret;
	}
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		const MethodBindTRC *self = static_cast<const MethodBindTRC *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_validated_object_instance_args_retc(static_cast<T *>(p_object), self->method, p_args, r_ret);
#else
		call_with_validated_object_instance_args_retc(reinterpret_cast<MB_T *>(p_object), self->method, p_args, r_ret);
#endif
	}
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		const MethodBindTRC *self = static_cast<const MethodBindTRC *>(p_bind);
#ifdef TYPED_METHOD_BIND
		call_with_ptr_args_retc<T, R, P...>(static_cast<T *>(p_object), self->method, p_args, r_ret);
#else
		call_with_ptr_args_retc<MB_T, R, P...>(reinterpret_cast<MB_T *>(p_object), self->method, p_args, r_ret);
#endif
	}
	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindTRC(R (MB_T::*p_method)(P...) const) :
			MethodBind(&dispatcher) {
		method = p_method;
		_set_returns(true);
		_set_const(true);
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
	}
};

#ifdef TYPED_METHOD_BIND
template <typename T, typename R, typename... P>
const MethodBindVTable MethodBindTRC<T, R, P...>::dispatcher = {
#else
template <typename R, typename... P>
const MethodBindVTable MethodBindTRC<R, P...>::dispatcher = {
#endif
	&MethodBindTRC::_call_bind, &MethodBindTRC::_validated_call_bind, &MethodBindTRC::_ptrcall_bind, &MethodBindTRC::_gen_argument_type_bind, &MethodBindTRC::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindTRC::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindTRC::_is_vararg_bind
};

template <typename T, typename R, typename... P>
MethodBind *create_method_bind(R (T::*p_method)(P...) const) {
#ifdef TYPED_METHOD_BIND
	MethodBind *a = memnew((MethodBindTRC<T, R, P...>)(p_method));
#else
	MethodBind *a = memnew((MethodBindTRC<R, P...>)(reinterpret_cast<R (MB_T::*)(P...) const>(p_method)));
#endif
	a->set_instance_class(T::get_class_static());
	return a;
}

// static no return
template <typename... P>
class MethodBindTS : public MethodBind {
	void (*function)(P...);

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		}
		return Variant::NIL;
	}
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		PropertyInfo pi;
		call_get_argument_type_info<P...>(p_arg, pi);
		return pi;
	}
#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		return call_get_argument_metadata<P...>(p_arg);
	}
#endif
	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		const MethodBindTS *self = static_cast<const MethodBindTS *>(p_bind);
		(void)p_object;
		call_with_variant_args_static_dv(self->function, p_args, p_arg_count, r_error, self->get_default_arguments());
		return Variant();
	}
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		const MethodBindTS *self = static_cast<const MethodBindTS *>(p_bind);
		call_with_validated_variant_args_static_method(self->function, p_args);
	}
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		const MethodBindTS *self = static_cast<const MethodBindTS *>(p_bind);
		(void)p_object;
		(void)r_ret;
		call_with_ptr_args_static_method(self->function, p_args);
	}
	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindTS(void (*p_function)(P...)) :
			MethodBind(&dispatcher) {
		function = p_function;
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
		_set_static(true);
	}
};

template <typename... P>
const MethodBindVTable MethodBindTS<P...>::dispatcher = {
	&MethodBindTS::_call_bind, &MethodBindTS::_validated_call_bind, &MethodBindTS::_ptrcall_bind, &MethodBindTS::_gen_argument_type_bind, &MethodBindTS::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindTS::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindTS::_is_vararg_bind
};

template <typename... P>
MethodBind *create_static_method_bind(void (*p_method)(P...)) {
	MethodBind *a = memnew((MethodBindTS<P...>)(p_method));
	return a;
}

// static return
template <typename R, typename... P>
class MethodBindTRS : public MethodBind {
	R (*function)(P...);

	static Variant::Type _gen_argument_type_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			return call_get_argument_type<P...>(p_arg);
		}
		return GetTypeInfo<R>::VARIANT_TYPE;
	}
	static PropertyInfo _gen_argument_type_info_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0 && p_arg < (int)sizeof...(P)) {
			PropertyInfo pi;
			call_get_argument_type_info<P...>(p_arg, pi);
			return pi;
		}
		return GetTypeInfo<R>::get_class_info();
	}
#ifdef DEBUG_METHODS_ENABLED
	static GodotTypeInfo::Metadata _get_argument_meta_bind(const MethodBind *p_bind, int p_arg) {
		if (p_arg >= 0) {
			return call_get_argument_metadata<P...>(p_arg);
		}
		return GetTypeInfo<R>::METADATA;
	}
#endif
	static Variant _call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
		Variant ret;
		const MethodBindTRS *self = static_cast<const MethodBindTRS *>(p_bind);
		call_with_variant_args_static_ret_dv(self->function, p_args, p_arg_count, ret, r_error, self->get_default_arguments());
		return ret;
	}
	static void _validated_call_bind(const MethodBind *p_bind, Object *p_object, const Variant **p_args, Variant *r_ret) {
		const MethodBindTRS *self = static_cast<const MethodBindTRS *>(p_bind);
		call_with_validated_variant_args_static_method_ret(self->function, p_args, r_ret);
	}
	static void _ptrcall_bind(const MethodBind *p_bind, Object *p_object, const void **p_args, void *r_ret) {
		const MethodBindTRS *self = static_cast<const MethodBindTRS *>(p_bind);
		(void)p_object;
		call_with_ptr_args_static_method_ret(self->function, p_args, r_ret);
	}
	static bool _is_vararg_bind(const MethodBind *p_bind) { return false; }
	static const MethodBindVTable dispatcher;

public:
	MethodBindTRS(R (*p_function)(P...)) :
			MethodBind(&dispatcher) {
		function = p_function;
		_generate_argument_types(sizeof...(P));
		set_argument_count(sizeof...(P));
		_set_static(true);
		_set_returns(true);
	}
};

template <typename R, typename... P>
const MethodBindVTable MethodBindTRS<R, P...>::dispatcher = {
	&MethodBindTRS::_call_bind, &MethodBindTRS::_validated_call_bind, &MethodBindTRS::_ptrcall_bind, &MethodBindTRS::_gen_argument_type_bind, &MethodBindTRS::_gen_argument_type_info_bind,
#ifdef DEBUG_METHODS_ENABLED
	&MethodBindTRS::_get_argument_meta_bind,
#endif
#ifdef TOOLS_ENABLED
	nullptr,
#endif
	&MethodBindTRS::_is_vararg_bind
};

template <typename R, typename... P>
MethodBind *create_static_method_bind(R (*p_method)(P...)) {
	MethodBind *a = memnew((MethodBindTRS<R, P...>)(p_method));
	return a;
}

/* COMPILE-TIME FACTORY BUILDERS */

template <typename M, M m_method>
struct MethodBindStaticBuilder;

template <typename T, typename... P, void (T::*m_method)(P...)>
struct MethodBindStaticBuilder<void (T::*)(P...), m_method> {
	static MethodBind *build() {
#ifdef TYPED_METHOD_BIND
		MethodBind *a = memnew((MethodBindStaticT<T, m_method, P...>)());
#else
		MethodBind *a = memnew((MethodBindStaticT<m_method, P...>)());
#endif
		a->set_instance_class(T::get_class_static());
		return a;
	}
};

template <typename T, typename... P, void (T::*m_method)(P...) const>
struct MethodBindStaticBuilder<void (T::*)(P...) const, m_method> {
	static MethodBind *build() {
#ifdef TYPED_METHOD_BIND
		MethodBind *a = memnew((MethodBindStaticTC<T, m_method, P...>)());
#else
		MethodBind *a = memnew((MethodBindStaticTC<m_method, P...>)());
#endif
		a->set_instance_class(T::get_class_static());
		return a;
	}
};

template <typename T, typename R, typename... P, R (T::*m_method)(P...)>
struct MethodBindStaticBuilder<R (T::*)(P...), m_method> {
	static MethodBind *build() {
#ifdef TYPED_METHOD_BIND
		MethodBind *a = memnew((MethodBindStaticTR<T, m_method, R, P...>)());
#else
		MethodBind *a = memnew((MethodBindStaticTR<m_method, R, P...>)());
#endif
		a->set_instance_class(T::get_class_static());
		return a;
	}
};

template <typename T, typename R, typename... P, R (T::*m_method)(P...) const>
struct MethodBindStaticBuilder<R (T::*)(P...) const, m_method> {
	static MethodBind *build() {
#ifdef TYPED_METHOD_BIND
		MethodBind *a = memnew((MethodBindStaticTRC<T, m_method, R, P...>)());
#else
		MethodBind *a = memnew((MethodBindStaticTRC<m_method, R, P...>)());
#endif
		a->set_instance_class(T::get_class_static());
		return a;
	}
};

template <typename... P, void (*m_method)(P...)>
struct MethodBindStaticBuilder<void (*)(P...), m_method> {
	static MethodBind *build() {
		MethodBind *a = memnew((MethodBindStaticTS<m_method, P...>)());
		return a;
	}
};

template <typename R, typename... P, R (*m_method)(P...)>
struct MethodBindStaticBuilder<R (*)(P...), m_method> {
	static MethodBind *build() {
		MethodBind *a = memnew((MethodBindStaticTRS<m_method, R, P...>)());
		return a;
	}
};

/* FACTORY FUNCTIONS */

template <typename M, M m_method>
MethodBind *create_method_bind_static() {
	return MethodBindStaticBuilder<M, m_method>::build();
}

template <typename M, M m_method>
MethodBind *create_static_method_bind_static() {
	return MethodBindStaticBuilder<M, m_method>::build();
}

#endif // METHOD_BIND_H
