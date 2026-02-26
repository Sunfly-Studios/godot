/**************************************************************************/
/*  jni_utils.cpp                                                         */
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

#include "jni_utils.h"

#include "api/java_class_wrapper.h"

jobject callable_to_jcallable(JNIEnv *p_env, const Variant &p_callable) {
	ERR_FAIL_NULL_V(p_env, nullptr);
	if (p_callable.get_type() != Variant::CALLABLE) {
		return nullptr;
	}

	Variant *callable_jcopy = memnew(Variant(p_callable));

	jclass bclass = p_env->FindClass("org/godotengine/godot/variant/Callable");
	JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, nullptr, memdelete(callable_jcopy));

	jmethodID ctor = p_env->GetMethodID(bclass, "<init>", "(J)V");
	JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, nullptr, {
		p_env->DeleteLocalRef(bclass);
		memdelete(callable_jcopy);
	});
	
	jobject jcallable = p_env->NewObject(bclass, ctor, reinterpret_cast<int64_t>(callable_jcopy));
	JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, nullptr, {
		p_env->DeleteLocalRef(bclass);
		memdelete(callable_jcopy);
	});

	p_env->DeleteLocalRef(bclass);

	return jcallable;
}

Callable jcallable_to_callable(JNIEnv *p_env, jobject p_jcallable_obj) {
	ERR_FAIL_NULL_V(p_env, Callable());

	const Variant *callable_variant = nullptr;
	jclass callable_class = p_env->FindClass("org/godotengine/godot/variant/Callable");
	JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, Callable(), p_env->DeleteLocalRef(callable_class));
	
	if (callable_class) {
		if (p_env->IsInstanceOf(p_jcallable_obj, callable_class)) {
			jmethodID get_native_pointer = p_env->GetMethodID(callable_class, "getNativePointer", "()J");
			JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, Callable(), p_env->DeleteLocalRef(callable_class));
			jlong native_callable = p_env->CallLongMethod(p_jcallable_obj, get_native_pointer);

			JNI_CHECK_EXCEPTION_CONTINUE(p_env) else {
				callable_variant = reinterpret_cast<const Variant *>(native_callable);
			}
		}
		p_env->DeleteLocalRef(callable_class);
	}

	ERR_FAIL_NULL_V(callable_variant, Callable());
	return *callable_variant;
}

String charsequence_to_string(JNIEnv *p_env, jobject p_charsequence) {
	ERR_FAIL_NULL_V(p_env, String());

	String result;
	jclass bclass = p_env->FindClass("java/lang/CharSequence");
	JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, String(), p_env->DeleteLocalRef(bclass));

	if (bclass) {
		if (p_env->IsInstanceOf(p_charsequence, bclass)) {
			jmethodID to_string = p_env->GetMethodID(bclass, "toString", "()Ljava/lang/String;");
			JNI_CHECK_EXCEPTION_CLEANUP_V(p_env, String(), p_env->DeleteLocalRef(bclass));
			jstring obj_string = static_cast<jstring>(p_env->CallObjectMethod(p_charsequence, to_string));

			JNI_CHECK_EXCEPTION_CONTINUE(p_env) else if (obj_string) {
				result = jstring_to_string(obj_string, p_env);
				p_env->DeleteLocalRef(obj_string);
			}
		}
		p_env->DeleteLocalRef(bclass);
	}
	return result;
}

jvalret _variant_to_jvalue(JNIEnv *env, Variant::Type p_type, const Variant *p_arg, bool force_jobject) {
	jvalret v;
	v.val.i = 0; // Initialize union
	v.obj = nullptr;

	switch (p_type) {
		case Variant::BOOL: {
			if (force_jobject) {
				jclass bclass = env->FindClass("java/lang/Boolean");
				JNI_CHECK_EXCEPTION_V(env, v);
				jmethodID ctor = env->GetMethodID(bclass, "<init>", "(Z)V");
				JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(bclass));
				jvalue val;
				val.z = (bool)(*p_arg);
				jobject obj = env->NewObjectA(bclass, ctor, &val);
				JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(bclass));
				v.val.l = obj;
				v.obj = obj;
				env->DeleteLocalRef(bclass);
			} else {
				v.val.z = *p_arg;
			}
		} break;
		case Variant::INT: {
			if (force_jobject) {
				jclass bclass = env->FindClass("java/lang/Integer");
				JNI_CHECK_EXCEPTION_V(env, v);
				jmethodID ctor = env->GetMethodID(bclass, "<init>", "(I)V");
				JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(bclass));
				jvalue val;
				val.i = (int)(*p_arg);
				jobject obj = env->NewObjectA(bclass, ctor, &val);
				JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(bclass));
				v.val.l = obj;
				v.obj = obj;
				env->DeleteLocalRef(bclass);
			} else {
				v.val.i = *p_arg;
			}
		} break;
		case Variant::FLOAT: {
			if (force_jobject) {
				jclass bclass = env->FindClass("java/lang/Double");
				JNI_CHECK_EXCEPTION_V(env, v);
				jmethodID ctor = env->GetMethodID(bclass, "<init>", "(D)V");
				JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(bclass));
				jvalue val;
				val.d = (double)(*p_arg);
				jobject obj = env->NewObjectA(bclass, ctor, &val);
				JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(bclass));
				v.val.l = obj;
				v.obj = obj;
				env->DeleteLocalRef(bclass);
			} else {
				v.val.f = *p_arg;
			}
		} break;
		case Variant::STRING: {
			String s = *p_arg;
			CharString utf8_str = s.utf8();
			jstring jStr = env->NewStringUTF(utf8_str.get_data());
			JNI_CHECK_EXCEPTION_V(env, v);
			v.val.l = jStr;
			v.obj = jStr;
		} break;
		case Variant::PACKED_STRING_ARRAY: {
			Vector<String> sarray = *p_arg;
			jclass string_cls = env->FindClass("java/lang/String");
			JNI_CHECK_EXCEPTION_V(env, v);
			
			jstring empty_str = env->NewStringUTF("");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(string_cls));
			
			jobjectArray arr = env->NewObjectArray(sarray.size(), string_cls, empty_str);
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, { 
				env->DeleteLocalRef(string_cls); 
				env->DeleteLocalRef(empty_str); 
			});

			for (int j = 0; j < sarray.size(); j++) {
				CharString utf8_str = sarray[j].utf8();
				jstring str = env->NewStringUTF(utf8_str.get_data());
				if (env->ExceptionCheck()) {
					env->ExceptionClear();
					continue;
				}
				env->SetObjectArrayElement(arr, j, str);
				// Catch ArrayStoreExceptions
				JNI_CHECK_EXCEPTION_CONTINUE(env);
				env->DeleteLocalRef(str);
			}
			
			env->DeleteLocalRef(string_cls);
			env->DeleteLocalRef(empty_str);
			
			v.val.l = arr;
			v.obj = arr;
		} break;
		case Variant::CALLABLE: {
			jobject jcallable = callable_to_jcallable(env, *p_arg);
			v.val.l = jcallable;
			v.obj = jcallable;
		} break;
		case Variant::DICTIONARY: {
			Dictionary dict = *p_arg;
			jclass dclass = env->FindClass("org/godotengine/godot/Dictionary");
			JNI_CHECK_EXCEPTION_V(env, v);

			jmethodID ctor = env->GetMethodID(dclass, "<init>", "()V");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(dclass));

			jobject jdict = env->NewObject(dclass, ctor);
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, env->DeleteLocalRef(dclass));

			Array keys = dict.keys();

			jclass string_cls = env->FindClass("java/lang/String");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});

			jstring empty_str = env->NewStringUTF("");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(string_cls);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});

			jobjectArray jkeys = env->NewObjectArray(keys.size(), string_cls, empty_str);
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(empty_str);
				env->DeleteLocalRef(string_cls);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});
			
			for (int j = 0; j < keys.size(); j++) {
				CharString utf8_str = String(keys[j]).utf8();
				jstring str = env->NewStringUTF(utf8_str.get_data());
				if (env->ExceptionCheck()) {
					env->ExceptionClear();
					continue;
				}
				env->SetObjectArrayElement(jkeys, j, str);
				// Catch ArrayStoreExceptions
				JNI_CHECK_EXCEPTION_CONTINUE(env);
				env->DeleteLocalRef(str);
			}
			env->DeleteLocalRef(string_cls);
			env->DeleteLocalRef(empty_str);

			jmethodID set_keys = env->GetMethodID(dclass, "set_keys", "([Ljava/lang/String;)V");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(jkeys);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});

			jvalue val;
			val.l = jkeys;
			env->CallVoidMethodA(jdict, set_keys, &val);
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(jkeys);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});
			env->DeleteLocalRef(jkeys);

			jclass obj_cls = env->FindClass("java/lang/Object");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});

			jobjectArray jvalues = env->NewObjectArray(keys.size(), obj_cls, nullptr);
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(obj_cls);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});
			env->DeleteLocalRef(obj_cls);

			for (int j = 0; j < keys.size(); j++) {
				Variant var = dict[keys[j]];
				jvalret valret = _variant_to_jvalue(env, var.get_type(), &var, true);
				env->SetObjectArrayElement(jvalues, j, valret.val.l);
				// Catch ArrayStoreExceptions
				JNI_CHECK_EXCEPTION_CONTINUE(env);

				if (valret.obj) {
					env->DeleteLocalRef(valret.obj);
				}
			}

			jmethodID set_values = env->GetMethodID(dclass, "set_values", "([Ljava/lang/Object;)V");
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(jvalues);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});

			val.l = jvalues;
			env->CallVoidMethodA(jdict, set_values, &val);
			JNI_CHECK_EXCEPTION_CLEANUP_V(env, v, {
				env->DeleteLocalRef(jvalues);
				env->DeleteLocalRef(jdict);
				env->DeleteLocalRef(dclass);
			});
			
			env->DeleteLocalRef(jvalues);
			env->DeleteLocalRef(dclass);

			v.val.l = jdict;
			v.obj = jdict;
		} break;
		case Variant::PACKED_INT32_ARRAY: {
			Vector<int> array = *p_arg;
			jintArray arr = env->NewIntArray(array.size());
			JNI_CHECK_EXCEPTION_V(env, v);
			const int *r = array.ptr();
			env->SetIntArrayRegion(arr, 0, array.size(), r);
			v.val.l = arr;
			v.obj = arr;
		} break;
		case Variant::PACKED_INT64_ARRAY: {
			Vector<int64_t> array = *p_arg;
			jlongArray arr = env->NewLongArray(array.size());
			JNI_CHECK_EXCEPTION_V(env, v);
			const int64_t *r = array.ptr();
			env->SetLongArrayRegion(arr, 0, array.size(), r);
			v.val.l = arr;
			v.obj = arr;
		} break;
		case Variant::PACKED_BYTE_ARRAY: {
			Vector<uint8_t> array = *p_arg;
			jbyteArray arr = env->NewByteArray(array.size());
			JNI_CHECK_EXCEPTION_V(env, v);
			const uint8_t *r = array.ptr();
			env->SetByteArrayRegion(arr, 0, array.size(), reinterpret_cast<const signed char *>(r));
			v.val.l = arr;
			v.obj = arr;
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			Vector<float> array = *p_arg;
			jfloatArray arr = env->NewFloatArray(array.size());
			JNI_CHECK_EXCEPTION_V(env, v);
			const float *r = array.ptr();
			env->SetFloatArrayRegion(arr, 0, array.size(), r);
			v.val.l = arr;
			v.obj = arr;
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			Vector<double> array = *p_arg;
			jdoubleArray arr = env->NewDoubleArray(array.size());
			JNI_CHECK_EXCEPTION_V(env, v);
			const double *r = array.ptr();
			env->SetDoubleArrayRegion(arr, 0, array.size(), r);
			v.val.l = arr;
			v.obj = arr;
		} break;
		case Variant::OBJECT: {
			Ref<JavaObject> generic_object = *p_arg;
			if (generic_object.is_valid()) {
				jobject obj = env->NewLocalRef(generic_object->get_instance());
				JNI_CHECK_EXCEPTION_V(env, v);
				v.val.l = obj;
				v.obj = obj;
			}
		} break;
		default: break;
	}
	return v;
}

String _get_class_name(JNIEnv *env, jclass cls, bool *array) {
	jclass cclass = env->FindClass("java/lang/Class");
	JNI_CHECK_EXCEPTION_V(env, String());
	jmethodID getName = env->GetMethodID(cclass, "getName", "()Ljava/lang/String;");
	JNI_CHECK_EXCEPTION_CLEANUP_V(env, String(), env->DeleteLocalRef(cclass));
	jstring clsName = static_cast<jstring>(env->CallObjectMethod(cls, getName));
	JNI_CHECK_EXCEPTION_CLEANUP_V(env, String(), {
		env->DeleteLocalRef(clsName);
		env->DeleteLocalRef(cclass);
	});

	if (array) {
		jmethodID isArray = env->GetMethodID(cclass, "isArray", "()Z");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, String(), {
			env->DeleteLocalRef(clsName);
			env->DeleteLocalRef(cclass);
		});
		jboolean isarr = env->CallBooleanMethod(cls, isArray);
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, String(), {
			env->DeleteLocalRef(clsName);
			env->DeleteLocalRef(cclass);
		});

		// JNI_FALSE is safer than 0
		*array = (isarr != JNI_FALSE);
	}
	String name = jstring_to_string(clsName, env);
	
	env->DeleteLocalRef(clsName);
	env->DeleteLocalRef(cclass);

	return name;
}

Variant _jobject_to_variant(JNIEnv *env, jobject obj) {
	if (obj == nullptr) {
		return Variant();
	}

	jclass c = env->GetObjectClass(obj);
	JNI_CHECK_EXCEPTION_V(env, Variant());
	bool array;
	String name = _get_class_name(env, c, &array);
	Variant result;

	if (name == "java.lang.String") {
		result = jstring_to_string(static_cast<jstring>(obj), env);
	} else if (name == "java.lang.CharSequence") {
		result = charsequence_to_string(env, obj);
	} else if (name == "[Ljava.lang.String;") {
		jobjectArray arr = static_cast<jobjectArray>(obj);
		int stringCount = env->GetArrayLength(arr);
		Vector<String> sarr;
		for (int i = 0; i < stringCount; i++) {
			jstring string = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
			JNI_CHECK_EXCEPTION_CONTINUE(env);
			
			if (string != nullptr) {
				sarr.push_back(jstring_to_string(string, env));
				env->DeleteLocalRef(string);
			}
		}
		result = sarr;
	} else if (name == "[Ljava.lang.CharSequence;") {
		jobjectArray arr = static_cast<jobjectArray>(obj);
		int stringCount = env->GetArrayLength(arr);
		Vector<String> sarr;
		for (int i = 0; i < stringCount; i++) {
			jobject charsequence = env->GetObjectArrayElement(arr, i);
			JNI_CHECK_EXCEPTION_CONTINUE(env);
			
			if (charsequence != nullptr) {
				sarr.push_back(charsequence_to_string(env, charsequence));
				env->DeleteLocalRef(charsequence);
			}
		}
		result = sarr;
	} else if (name == "java.lang.Boolean") {
		jmethodID boolValue = env->GetMethodID(c, "booleanValue", "()Z");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		result = (bool)env->CallBooleanMethod(obj, boolValue);
		if (env->ExceptionCheck()) env->ExceptionClear(); // Clear if CallMethod throws
	} else if (name == "java.lang.Integer" || name == "java.lang.Long") {
		jclass nclass = env->FindClass("java/lang/Number");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		
		jmethodID longValue = env->GetMethodID(nclass, "longValue", "()J");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), {
			env->DeleteLocalRef(nclass);
			env->DeleteLocalRef(c);
		});
		
		result = (int64_t)env->CallLongMethod(obj, longValue);
		if (env->ExceptionCheck()) env->ExceptionClear();
		
		env->DeleteLocalRef(nclass);
	} else if (name == "[I") {
		jintArray arr = static_cast<jintArray>(obj);
		int fCount = env->GetArrayLength(arr);
		Vector<int> sarr;
		sarr.resize(fCount);
		env->GetIntArrayRegion(arr, 0, fCount, sarr.ptrw());
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		result = sarr;
	} else if (name == "[J") {
		jlongArray arr = static_cast<jlongArray>(obj);
		int fCount = env->GetArrayLength(arr);
		Vector<int64_t> sarr;
		sarr.resize(fCount);
		env->GetLongArrayRegion(arr, 0, fCount, sarr.ptrw());
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		result = sarr;
	} else if (name == "[B") {
		jbyteArray arr = static_cast<jbyteArray>(obj);
		int fCount = env->GetArrayLength(arr);
		Vector<uint8_t> sarr;
		sarr.resize(fCount);
		env->GetByteArrayRegion(arr, 0, fCount, reinterpret_cast<signed char *>(sarr.ptrw()));
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		result = sarr;
	} else if (name == "java.lang.Float" || name == "java.lang.Double") {
		jclass nclass = env->FindClass("java/lang/Number");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		
		jmethodID doubleValue = env->GetMethodID(nclass, "doubleValue", "()D");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), {
			env->DeleteLocalRef(nclass);
			env->DeleteLocalRef(c);
		});
		
		result = (double)env->CallDoubleMethod(obj, doubleValue);
		if (env->ExceptionCheck()) env->ExceptionClear();
		
		env->DeleteLocalRef(nclass);
	} else if (name == "[D") {
		jdoubleArray arr = static_cast<jdoubleArray>(obj);
		int fCount = env->GetArrayLength(arr);
		PackedFloat64Array packed_array;
		packed_array.resize(fCount);
		env->GetDoubleArrayRegion(arr, 0, fCount, packed_array.ptrw());
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		result = packed_array;
	} else if (name == "[F") {
		jfloatArray arr = static_cast<jfloatArray>(obj);
		int fCount = env->GetArrayLength(arr);
		PackedFloat32Array packed_array;
		packed_array.resize(fCount);
		env->GetFloatArrayRegion(arr, 0, fCount, packed_array.ptrw());
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		result = packed_array;
	} else if (name == "[Ljava.lang.Object;") {
		jobjectArray arr = static_cast<jobjectArray>(obj);
		int objCount = env->GetArrayLength(arr);
		Array varr;
		for (int i = 0; i < objCount; i++) {
			jobject jobj = env->GetObjectArrayElement(arr, i);
			JNI_CHECK_EXCEPTION_CONTINUE(env); // Safe: Inside a loop
			
			if (jobj != nullptr) {
				varr.push_back(_jobject_to_variant(env, jobj));
				env->DeleteLocalRef(jobj);
			}
		}
		result = varr;
	} else if (name == "java.util.HashMap" || name == "org.godotengine.godot.Dictionary") {
		Dictionary ret;
		jmethodID get_keys = env->GetMethodID(c, "get_keys", "()[Ljava/lang/String;");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		
		jobjectArray arr_keys = static_cast<jobjectArray>(env->CallObjectMethod(obj, get_keys));
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		
		PackedStringArray keys = _jobject_to_variant(env, arr_keys);
		env->DeleteLocalRef(arr_keys);

		jmethodID get_values = env->GetMethodID(c, "get_values", "()[Ljava/lang/Object;");
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));

		jobjectArray arr_vals = static_cast<jobjectArray>(env->CallObjectMethod(obj, get_values));
		JNI_CHECK_EXCEPTION_CLEANUP_V(env, Variant(), env->DeleteLocalRef(c));
		
		Array vals = _jobject_to_variant(env, arr_vals);
		env->DeleteLocalRef(arr_vals);

		for (int i = 0; i < keys.size(); i++) {
			ret[keys[i]] = vals[i];
		}
		result = ret;
	} else if (name == "org.godotengine.godot.variant.Callable") {
		result = jcallable_to_callable(env, obj);
	} else {
		result = Ref<JavaObject>(memnew(JavaObject(JavaClassWrapper::get_singleton()->wrap(name), obj)));
	}

	// Delete the class local reference from the top of the function.
	env->DeleteLocalRef(c);

	return result;
}

Variant::Type get_jni_type(const String &p_type) {
	static struct {
		const char *name;
		Variant::Type type;
	} _type_to_vtype[] = {
		{ "void", Variant::NIL },
		{ "boolean", Variant::BOOL },
		{ "int", Variant::INT },
		{ "long", Variant::INT },
		{ "float", Variant::FLOAT },
		{ "double", Variant::FLOAT },
		{ "java.lang.String", Variant::STRING },
		{ "java.lang.CharSequence", Variant::STRING },
		{ "[I", Variant::PACKED_INT32_ARRAY },
		{ "[J", Variant::PACKED_INT64_ARRAY },
		{ "[B", Variant::PACKED_BYTE_ARRAY },
		{ "[F", Variant::PACKED_FLOAT32_ARRAY },
		{ "[D", Variant::PACKED_FLOAT64_ARRAY },
		{ "[Ljava.lang.String;", Variant::PACKED_STRING_ARRAY },
		{ "[Ljava.lang.CharSequence;", Variant::PACKED_STRING_ARRAY },
		{ "org.godotengine.godot.Dictionary", Variant::DICTIONARY },
		{ "org.godotengine.godot.variant.Callable", Variant::CALLABLE },
		{ nullptr, Variant::NIL }
	};

	int idx = 0;

	while (_type_to_vtype[idx].name) {
		if (p_type == _type_to_vtype[idx].name) {
			return _type_to_vtype[idx].type;
		}

		idx++;
	}

	return Variant::OBJECT;
}
