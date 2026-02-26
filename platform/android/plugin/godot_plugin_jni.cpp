/**************************************************************************/
/*  godot_plugin_jni.cpp                                                  */
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

#include "godot_plugin_jni.h"

#include "api/java_class_wrapper.h"
#include "api/jni_singleton.h"
#include "jni_utils.h"

#include "core/config/engine.h"
#include "core/error/error_macros.h"

static HashMap<String, JNISingleton *> jni_singletons;

void unregister_plugins_singletons() {
	for (const KeyValue<String, JNISingleton *> &E : jni_singletons) {
		Engine::get_singleton()->remove_singleton(E.key);

		if (E.value) {
			memdelete(E.value);
		}
	}
	jni_singletons.clear();
}

extern "C" {

JNIEXPORT jboolean JNICALL Java_org_godotengine_godot_plugin_GodotPlugin_nativeRegisterSingleton(JNIEnv *env, jclass clazz, jstring name, jobject obj) {
	String singname = jstring_to_string(name, env);

	ERR_FAIL_COND_V(jni_singletons.has(singname), false);

	jclass java_class = env->GetObjectClass(obj);
	Ref<JavaClass> java_class_wrapped = JavaClassWrapper::get_singleton()->wrap_jclass(java_class, true);
	env->DeleteLocalRef(java_class);

	Ref<JavaObject> plugin_object = memnew(JavaObject(java_class_wrapped, obj));
	JNISingleton *plugin_singleton = memnew(JNISingleton(plugin_object));
	jni_singletons[singname] = plugin_singleton;

	Engine::get_singleton()->add_singleton(Engine::Singleton(singname, plugin_singleton));
	return true;
}

JNIEXPORT void JNICALL Java_org_godotengine_godot_plugin_GodotPlugin_nativeRegisterMethod(JNIEnv *env, jclass clazz, jstring sname, jstring name, jstring ret, jobjectArray args) {
	String singname = jstring_to_string(sname, env);

	ERR_FAIL_COND(!jni_singletons.has(singname));

	JNISingleton *s = jni_singletons.get(singname);

	String mname = jstring_to_string(name, env);
	String retval = jstring_to_string(ret, env);
	Vector<Variant::Type> types;

	int stringCount = 0;
	if (args) {
		stringCount = env->GetArrayLength(args);
	}

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		stringCount = 0;
	}

	for (int i = 0; i < stringCount; i++) {
		jstring string = static_cast<jstring>(env->GetObjectArrayElement(args, i));
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			continue;
		}
		
		if (string) {
			const String rawString = jstring_to_string(string, env);
			types.push_back(get_jni_type(rawString));
			env->DeleteLocalRef(string);
		}
	}

	s->add_method(mname, types, get_jni_type(retval));
}

JNIEXPORT void JNICALL Java_org_godotengine_godot_plugin_GodotPlugin_nativeRegisterSignal(JNIEnv *env, jclass clazz, jstring j_plugin_name, jstring j_signal_name, jobjectArray j_signal_param_types) {
	String singleton_name = jstring_to_string(j_plugin_name, env);

	ERR_FAIL_COND(!jni_singletons.has(singleton_name));

	JNISingleton *singleton = jni_singletons.get(singleton_name);

	String signal_name = jstring_to_string(j_signal_name, env);
	Vector<Variant::Type> types;

	int stringCount = 0;
	if (j_signal_param_types) {
		stringCount = env->GetArrayLength(j_signal_param_types);
	}

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		stringCount = 0;
	}

	for (int i = 0; i < stringCount; i++) {
		jstring j_signal_param_type = static_cast<jstring>(env->GetObjectArrayElement(j_signal_param_types, i));
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			continue;
		}
		
		if (j_signal_param_type) {
			const String signal_param_type = jstring_to_string(j_signal_param_type, env);
			types.push_back(get_jni_type(signal_param_type));
			env->DeleteLocalRef(j_signal_param_type);
		}
	}

	singleton->add_signal(signal_name, types);
}

JNIEXPORT void JNICALL Java_org_godotengine_godot_plugin_GodotPlugin_nativeEmitSignal(JNIEnv *env, jclass clazz, jstring j_plugin_name, jstring j_signal_name, jobjectArray j_signal_params) {
	String singleton_name = jstring_to_string(j_plugin_name, env);

	ERR_FAIL_COND(!jni_singletons.has(singleton_name));

	JNISingleton *singleton = jni_singletons.get(singleton_name);

	String signal_name = jstring_to_string(j_signal_name, env);

	int count = 0;
	if (j_signal_params) {
		count = env->GetArrayLength(j_signal_params);
	}

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		count = 0;
	}

	Variant *variant_params = SAFE_ALLOCA_ARRAY(Variant, count);
	const Variant **args = SAFE_ALLOCA_ARRAY(const Variant *, count);

	for (int i = 0; i < count; i++) {
		jobject j_param = env->GetObjectArrayElement(j_signal_params, i);
		JNI_CHECK_EXCEPTION_CONTINUE(env);
		
		// Removed early ERR_FAIL_NULL return to prevent placement-new leak.
		// If a plugin passes null, we gracefully construct a Nil Godot Variant.
		if (j_param) {
			memnew_placement(&variant_params[i], Variant(_jobject_to_variant(env, j_param)));
			env->DeleteLocalRef(j_param);
		} else {
			memnew_placement(&variant_params[i], Variant());
		}
		
		args[i] = &variant_params[i];
	}

	singleton->emit_signalp(StringName(signal_name), args, count);

	// Manually invoke the destructor to decrease the reference counts for the variant arguments.
	for (int i = 0; i < count; i++) {
		variant_params[i].~Variant();
	}
}
}
