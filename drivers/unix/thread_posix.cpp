/**************************************************************************/
/*  thread_posix.cpp                                                      */
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

#if defined(UNIX_ENABLED)

#include "thread_posix.h"

#include "core/os/thread.h"
#include "core/string/ustring.h"

#ifdef PTHREAD_BSD_SET_NAME
#include <pthread_np.h>
#endif

#if defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ < 12

#include "core/templates/hash_map.h"
#include "core/os/mutex.h"

// Make sure that type stays consistent
// between architectures.
using ThreadID = uint64_t;

static HashMap<ThreadID, String> thread_names;
static Mutex thread_names_mutex;

// Make sure that type stays consistent
// between architectures.
static ThreadID convert_pthread_to_key(pthread_t thread_id) {
	return static_cast<ThreadID>(thread_id);
}

static void set_thread_name(pthread_t thread_id, const String &p_name) {
    MutexLock lock(thread_names_mutex);
    ThreadID key = convert_pthread_to_key(thread_id);
    thread_names[key] = p_name;
}

static String get_thread_name(pthread_t thread_id) {
	MutexLock lock(thread_names_mutex);
	
	ThreadID key = convert_pthread_to_key(thread_id);
    HashMap<ThreadID, String>::ConstIterator it = thread_names.find(key);
    if (it != thread_names.end()) {
        return it->value;
    }
    return String("");
}

#endif

static Error set_name(const String &p_name) {
#ifdef PTHREAD_NO_RENAME
	return ERR_UNAVAILABLE;

#else

#ifdef PTHREAD_RENAME_SELF

	// check if thread is the same as caller
	int err = pthread_setname_np(p_name.utf8().get_data());

#else

	pthread_t running_thread = pthread_self();
#ifdef PTHREAD_BSD_SET_NAME
	pthread_set_name_np(running_thread, p_name.utf8().get_data());
	int err = 0; // Open/FreeBSD ignore errors in this function
#elif defined(PTHREAD_NETBSD_SET_NAME)
	int err = pthread_setname_np(running_thread, "%s", const_cast<char *>(p_name.utf8().get_data()));
#else
    #if defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ < 12
        // Older distros support.
        set_thread_name(running_thread, p_name.utf8().get_data());
        int err = 0;
    #else
    	int err = pthread_setname_np(running_thread, p_name.utf8().get_data());
    #endif
#endif // PTHREAD_BSD_SET_NAME

#endif // PTHREAD_RENAME_SELF

	return err == 0 ? OK : ERR_INVALID_PARAMETER;

#endif // PTHREAD_NO_RENAME
}

void init_thread_posix() {
	Thread::_set_platform_functions({ .set_name = set_name });
}

#endif // UNIX_ENABLED
