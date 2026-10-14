/**************************************************************************/
/*  server_wrap_mt_common.h                                               */
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

#ifndef SERVER_WRAP_MT_COMMON_H
#define SERVER_WRAP_MT_COMMON_H

// TODO: To convert this file into an auto generated
// file.

#ifdef DEBUG_ENABLED
#define MAIN_THREAD_SYNC_CHECK                                                                         \
	if (unlikely(Thread::is_main_thread() && Engine::get_singleton()->notify_frame_server_synced())) { \
		MAIN_THREAD_SYNC_WARN                                                                          \
	}
#else
#define MAIN_THREAD_SYNC_CHECK
#endif

// These are now _FORCE_INLINE_ so the compiler
// fuses them into the `RenderingServerVTable`
// in `rendering_server.h`.
#define FUNC0R(m_r, m_type)                                                            \
	_FORCE_INLINE_ m_r m_type() {                                                      \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                      \
			m_r ret;                                                                   \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret); \
			SYNC_DEBUG                                                                 \
			MAIN_THREAD_SYNC_CHECK                                                     \
			return ret;                                                                \
		} else {                                                                       \
			command_queue.flush_if_pending();                                          \
			return server_name->m_type();                                              \
		}                                                                              \
	}

#define FUNCRIDSPLIT(m_type)                                                               \
	_FORCE_INLINE_ RID m_type##_create() {                                                 \
		RID ret = server_name->m_type##_allocate();                                        \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                          \
			command_queue.push_static<&ServerName::m_type##_initialize>(server_name, ret); \
		} else {                                                                           \
			server_name->m_type##_initialize(ret);                                         \
		}                                                                                  \
		return ret;                                                                        \
	}

//RID now returns directly, ensure thread safety yourself
#define FUNCRID(m_type)                        \
	_FORCE_INLINE_ RID m_type##_create() {     \
		return server_name->m_type##_create(); \
	}

#define FUNC0RC(m_r, m_type)                                                           \
	_FORCE_INLINE_ m_r m_type() const {                                                \
		WRITE_ACTION                                                                   \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                      \
			m_r ret;                                                                   \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret); \
			SYNC_DEBUG                                                                 \
			MAIN_THREAD_SYNC_CHECK                                                     \
			return ret;                                                                \
		} else {                                                                       \
			command_queue.flush_if_pending();                                          \
			return server_name->m_type();                                              \
		}                                                                              \
	}

#define FUNC0(m_type)                                                    \
	_FORCE_INLINE_ void m_type() {                                       \
		WRITE_ACTION                                                     \
		if (unlikely(Thread::get_caller_id() != server_thread)) {        \
			command_queue.push_static<&ServerName::m_type>(server_name); \
		} else {                                                         \
			command_queue.flush_if_pending();                            \
			server_name->m_type();                                       \
		}                                                                \
	}

#define FUNC0C(m_type)                                                   \
	_FORCE_INLINE_ void m_type() const {                                 \
		if (unlikely(Thread::get_caller_id() != server_thread)) {        \
			command_queue.push_static<&ServerName::m_type>(server_name); \
		} else {                                                         \
			command_queue.flush_if_pending();                            \
			server_name->m_type();                                       \
		}                                                                \
	}

#define FUNC0S(m_type)                                                            \
	_FORCE_INLINE_ void m_type() {                                                \
		WRITE_ACTION                                                              \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                 \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name); \
			SYNC_DEBUG                                                            \
			MAIN_THREAD_SYNC_CHECK                                                \
		} else {                                                                  \
			command_queue.flush_if_pending();                                     \
			server_name->m_type();                                                \
		}                                                                         \
	}

#define FUNC0SC(m_type)                                                           \
	_FORCE_INLINE_ void m_type() const {                                          \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                 \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name); \
			SYNC_DEBUG                                                            \
			MAIN_THREAD_SYNC_CHECK                                                \
		} else {                                                                  \
			command_queue.flush_if_pending();                                     \
			server_name->m_type();                                                \
		}                                                                         \
	}

///////////////////////////////////////////////

#define FUNC1R(m_r, m_type, m_arg1)                                                        \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1) {                                                 \
		WRITE_ACTION                                                                       \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                          \
			m_r ret;                                                                       \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1); \
			SYNC_DEBUG                                                                     \
			MAIN_THREAD_SYNC_CHECK                                                         \
			return ret;                                                                    \
		} else {                                                                           \
			command_queue.flush_if_pending();                                              \
			return server_name->m_type(p1);                                                \
		}                                                                                  \
	}

#define FUNC1RC(m_r, m_type, m_arg1)                                                       \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1) const {                                           \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                          \
			m_r ret;                                                                       \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1); \
			SYNC_DEBUG                                                                     \
			MAIN_THREAD_SYNC_CHECK                                                         \
			return ret;                                                                    \
		} else {                                                                           \
			command_queue.flush_if_pending();                                              \
			return server_name->m_type(p1);                                                \
		}                                                                                  \
	}

#define FUNC1S(m_type, m_arg1)                                                        \
	_FORCE_INLINE_ void m_type(m_arg1 p1) {                                           \
		WRITE_ACTION                                                                  \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                     \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1); \
			SYNC_DEBUG                                                                \
			MAIN_THREAD_SYNC_CHECK                                                    \
		} else {                                                                      \
			command_queue.flush_if_pending();                                         \
			server_name->m_type(p1);                                                  \
		}                                                                             \
	}

#define FUNC1SC(m_type, m_arg1)                                                       \
	_FORCE_INLINE_ void m_type(m_arg1 p1) const {                                     \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                     \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1); \
			SYNC_DEBUG                                                                \
			MAIN_THREAD_SYNC_CHECK                                                    \
		} else {                                                                      \
			command_queue.flush_if_pending();                                         \
			server_name->m_type(p1);                                                  \
		}                                                                             \
	}

#define FUNC1(m_type, m_arg1)                                                \
	_FORCE_INLINE_ void m_type(m_arg1 p1) {                                  \
		WRITE_ACTION                                                         \
		if (unlikely(Thread::get_caller_id() != server_thread)) {            \
			command_queue.push_static<&ServerName::m_type>(server_name, p1); \
		} else {                                                             \
			command_queue.flush_if_pending();                                \
			server_name->m_type(p1);                                         \
		}                                                                    \
	}

#define FUNC1C(m_type, m_arg1)                                               \
	_FORCE_INLINE_ void m_type(m_arg1 p1) const {                            \
		if (unlikely(Thread::get_caller_id() != server_thread)) {            \
			command_queue.push_static<&ServerName::m_type>(server_name, p1); \
		} else {                                                             \
			command_queue.flush_if_pending();                                \
			server_name->m_type(p1);                                         \
		}                                                                    \
	}

#define FUNC2R(m_r, m_type, m_arg1, m_arg2)                                                    \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2) {                                          \
		WRITE_ACTION                                                                           \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                              \
			m_r ret;                                                                           \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2); \
			SYNC_DEBUG                                                                         \
			MAIN_THREAD_SYNC_CHECK                                                             \
			return ret;                                                                        \
		} else {                                                                               \
			command_queue.flush_if_pending();                                                  \
			return server_name->m_type(p1, p2);                                                \
		}                                                                                      \
	}

#define FUNC2RC(m_r, m_type, m_arg1, m_arg2)                                                   \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2) const {                                    \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                              \
			m_r ret;                                                                           \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2); \
			SYNC_DEBUG                                                                         \
			MAIN_THREAD_SYNC_CHECK                                                             \
			return ret;                                                                        \
		} else {                                                                               \
			command_queue.flush_if_pending();                                                  \
			return server_name->m_type(p1, p2);                                                \
		}                                                                                      \
	}

#define FUNC2S(m_type, m_arg1, m_arg2)                                                    \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2) {                                    \
		WRITE_ACTION                                                                      \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                         \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2); \
			SYNC_DEBUG                                                                    \
			MAIN_THREAD_SYNC_CHECK                                                        \
		} else {                                                                          \
			command_queue.flush_if_pending();                                             \
			server_name->m_type(p1, p2);                                                  \
		}                                                                                 \
	}

#define FUNC2SC(m_type, m_arg1, m_arg2)                                                   \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2) const {                              \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                         \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2); \
			SYNC_DEBUG                                                                    \
			MAIN_THREAD_SYNC_CHECK                                                        \
		} else {                                                                          \
			command_queue.flush_if_pending();                                             \
			server_name->m_type(p1, p2);                                                  \
		}                                                                                 \
	}

#define FUNC2(m_type, m_arg1, m_arg2)                                            \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2) {                           \
		WRITE_ACTION                                                             \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2); \
		} else {                                                                 \
			command_queue.flush_if_pending();                                    \
			server_name->m_type(p1, p2);                                         \
		}                                                                        \
	}

#define FUNC2C(m_type, m_arg1, m_arg2)                                           \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2) const {                     \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2); \
		} else {                                                                 \
			command_queue.flush_if_pending();                                    \
			server_name->m_type(p1, p2);                                         \
		}                                                                        \
	}

#define FUNC3R(m_r, m_type, m_arg1, m_arg2, m_arg3)                                                \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3) {                                   \
		WRITE_ACTION                                                                               \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                  \
			m_r ret;                                                                               \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3); \
			SYNC_DEBUG                                                                             \
			MAIN_THREAD_SYNC_CHECK                                                                 \
			return ret;                                                                            \
		} else {                                                                                   \
			command_queue.flush_if_pending();                                                      \
			return server_name->m_type(p1, p2, p3);                                                \
		}                                                                                          \
	}

#define FUNC3RC(m_r, m_type, m_arg1, m_arg2, m_arg3)                                               \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3) const {                             \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                  \
			m_r ret;                                                                               \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3); \
			SYNC_DEBUG                                                                             \
			MAIN_THREAD_SYNC_CHECK                                                                 \
			return ret;                                                                            \
		} else {                                                                                   \
			command_queue.flush_if_pending();                                                      \
			return server_name->m_type(p1, p2, p3);                                                \
		}                                                                                          \
	}

#define FUNC3S(m_type, m_arg1, m_arg2, m_arg3)                                                \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3) {                             \
		WRITE_ACTION                                                                          \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                             \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3); \
			SYNC_DEBUG                                                                        \
			MAIN_THREAD_SYNC_CHECK                                                            \
		} else {                                                                              \
			command_queue.flush_if_pending();                                                 \
			server_name->m_type(p1, p2, p3);                                                  \
		}                                                                                     \
	}

#define FUNC3SC(m_type, m_arg1, m_arg2, m_arg3)                                               \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3) const {                       \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                             \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3); \
			SYNC_DEBUG                                                                        \
			MAIN_THREAD_SYNC_CHECK                                                            \
		} else {                                                                              \
			command_queue.flush_if_pending();                                                 \
			server_name->m_type(p1, p2, p3);                                                  \
		}                                                                                     \
	}

#define FUNC3(m_type, m_arg1, m_arg2, m_arg3)                                        \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3) {                    \
		WRITE_ACTION                                                                 \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                    \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3); \
		} else {                                                                     \
			command_queue.flush_if_pending();                                        \
			server_name->m_type(p1, p2, p3);                                         \
		}                                                                            \
	}

#define FUNC3C(m_type, m_arg1, m_arg2, m_arg3)                                       \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3) const {              \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                    \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3); \
		} else {                                                                     \
			command_queue.flush_if_pending();                                        \
			server_name->m_type(p1, p2, p3);                                         \
		}                                                                            \
	}

#define FUNC4R(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4)                                            \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4) {                            \
		WRITE_ACTION                                                                                   \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                      \
			m_r ret;                                                                                   \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4); \
			SYNC_DEBUG                                                                                 \
			MAIN_THREAD_SYNC_CHECK                                                                     \
			return ret;                                                                                \
		} else {                                                                                       \
			command_queue.flush_if_pending();                                                          \
			return server_name->m_type(p1, p2, p3, p4);                                                \
		}                                                                                              \
	}

#define FUNC4RC(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4)                                           \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4) const {                      \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                      \
			m_r ret;                                                                                   \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4); \
			SYNC_DEBUG                                                                                 \
			MAIN_THREAD_SYNC_CHECK                                                                     \
			return ret;                                                                                \
		} else {                                                                                       \
			command_queue.flush_if_pending();                                                          \
			return server_name->m_type(p1, p2, p3, p4);                                                \
		}                                                                                              \
	}

#define FUNC4S(m_type, m_arg1, m_arg2, m_arg3, m_arg4)                                            \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4) {                      \
		WRITE_ACTION                                                                              \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                 \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4); \
			SYNC_DEBUG                                                                            \
			MAIN_THREAD_SYNC_CHECK                                                                \
		} else {                                                                                  \
			command_queue.flush_if_pending();                                                     \
			server_name->m_type(p1, p2, p3, p4);                                                  \
		}                                                                                         \
	}

#define FUNC4SC(m_type, m_arg1, m_arg2, m_arg3, m_arg4)                                           \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4) const {                \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                 \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4); \
			SYNC_DEBUG                                                                            \
			MAIN_THREAD_SYNC_CHECK                                                                \
		} else {                                                                                  \
			command_queue.flush_if_pending();                                                     \
			server_name->m_type(p1, p2, p3, p4);                                                  \
		}                                                                                         \
	}

#define FUNC4(m_type, m_arg1, m_arg2, m_arg3, m_arg4)                                    \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4) {             \
		WRITE_ACTION                                                                     \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                        \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4); \
		} else {                                                                         \
			command_queue.flush_if_pending();                                            \
			server_name->m_type(p1, p2, p3, p4);                                         \
		}                                                                                \
	}

#define FUNC4C(m_type, m_arg1, m_arg2, m_arg3, m_arg4)                                   \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4) const {       \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                        \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4); \
		} else {                                                                         \
			command_queue.flush_if_pending();                                            \
			server_name->m_type(p1, p2, p3, p4);                                         \
		}                                                                                \
	}

#define FUNC5R(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5)                                        \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5) {                     \
		WRITE_ACTION                                                                                       \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                          \
			m_r ret;                                                                                       \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5); \
			SYNC_DEBUG                                                                                     \
			MAIN_THREAD_SYNC_CHECK                                                                         \
			return ret;                                                                                    \
		} else {                                                                                           \
			command_queue.flush_if_pending();                                                              \
			return server_name->m_type(p1, p2, p3, p4, p5);                                                \
		}                                                                                                  \
	}

#define FUNC5RC(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5)                                       \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5) const {               \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                          \
			m_r ret;                                                                                       \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5); \
			SYNC_DEBUG                                                                                     \
			MAIN_THREAD_SYNC_CHECK                                                                         \
			return ret;                                                                                    \
		} else {                                                                                           \
			command_queue.flush_if_pending();                                                              \
			return server_name->m_type(p1, p2, p3, p4, p5);                                                \
		}                                                                                                  \
	}

#define FUNC5S(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5)                                        \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5) {               \
		WRITE_ACTION                                                                                  \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                     \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5); \
			SYNC_DEBUG                                                                                \
			MAIN_THREAD_SYNC_CHECK                                                                    \
		} else {                                                                                      \
			command_queue.flush_if_pending();                                                         \
			server_name->m_type(p1, p2, p3, p4, p5);                                                  \
		}                                                                                             \
	}

#define FUNC5SC(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5)                                       \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5) const {         \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                     \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5); \
			SYNC_DEBUG                                                                                \
			MAIN_THREAD_SYNC_CHECK                                                                    \
		} else {                                                                                      \
			command_queue.flush_if_pending();                                                         \
			server_name->m_type(p1, p2, p3, p4, p5);                                                  \
		}                                                                                             \
	}

#define FUNC5(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5)                                \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5) {      \
		WRITE_ACTION                                                                         \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                            \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5); \
		} else {                                                                             \
			command_queue.flush_if_pending();                                                \
			server_name->m_type(p1, p2, p3, p4, p5);                                         \
		}                                                                                    \
	}

#define FUNC5C(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5)                                \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                             \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5);  \
		} else {                                                                              \
			command_queue.flush_if_pending();                                                 \
			server_name->m_type(p1, p2, p3, p4, p5);                                          \
		}                                                                                     \
	}

#define FUNC6R(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6)                                    \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6) {              \
		WRITE_ACTION                                                                                           \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                              \
			m_r ret;                                                                                           \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5, p6); \
			SYNC_DEBUG                                                                                         \
			MAIN_THREAD_SYNC_CHECK                                                                             \
			return ret;                                                                                        \
		} else {                                                                                               \
			command_queue.flush_if_pending();                                                                  \
			return server_name->m_type(p1, p2, p3, p4, p5, p6);                                                \
		}                                                                                                      \
	}

#define FUNC6RC(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6)                                   \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6) const {        \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                              \
			m_r ret;                                                                                           \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5, p6); \
			SYNC_DEBUG                                                                                         \
			MAIN_THREAD_SYNC_CHECK                                                                             \
			return ret;                                                                                        \
		} else {                                                                                               \
			command_queue.flush_if_pending();                                                                  \
			return server_name->m_type(p1, p2, p3, p4, p5, p6);                                                \
		}                                                                                                      \
	}

#define FUNC6S(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6)                                    \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6) {        \
		WRITE_ACTION                                                                                      \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                         \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6); \
			SYNC_DEBUG                                                                                    \
			MAIN_THREAD_SYNC_CHECK                                                                        \
		} else {                                                                                          \
			command_queue.flush_if_pending();                                                             \
			server_name->m_type(p1, p2, p3, p4, p5, p6);                                                  \
		}                                                                                                 \
	}

#define FUNC6SC(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6)                                   \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6) const {  \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                         \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6); \
			SYNC_DEBUG                                                                                    \
			MAIN_THREAD_SYNC_CHECK                                                                        \
		} else {                                                                                          \
			command_queue.flush_if_pending();                                                             \
			server_name->m_type(p1, p2, p3, p4, p5, p6);                                                  \
		}                                                                                                 \
	}

#define FUNC6(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6)                              \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6) { \
		WRITE_ACTION                                                                               \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                  \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6);   \
		} else {                                                                                   \
			command_queue.flush_if_pending();                                                      \
			server_name->m_type(p1, p2, p3, p4, p5, p6);                                           \
		}                                                                                          \
	}

#define FUNC6C(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6)                                   \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                        \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6);         \
		} else {                                                                                         \
			command_queue.flush_if_pending();                                                            \
			server_name->m_type(p1, p2, p3, p4, p5, p6);                                                 \
		}                                                                                                \
	}

#define FUNC7R(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7)                                \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7) {       \
		WRITE_ACTION                                                                                               \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                  \
			m_r ret;                                                                                               \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5, p6, p7); \
			SYNC_DEBUG                                                                                             \
			MAIN_THREAD_SYNC_CHECK                                                                                 \
			return ret;                                                                                            \
		} else {                                                                                                   \
			command_queue.flush_if_pending();                                                                      \
			return server_name->m_type(p1, p2, p3, p4, p5, p6, p7);                                                \
		}                                                                                                          \
	}

#define FUNC7RC(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7)                               \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                  \
			m_r ret;                                                                                               \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5, p6, p7); \
			SYNC_DEBUG                                                                                             \
			MAIN_THREAD_SYNC_CHECK                                                                                 \
			return ret;                                                                                            \
		} else {                                                                                                   \
			command_queue.flush_if_pending();                                                                      \
			return server_name->m_type(p1, p2, p3, p4, p5, p6, p7);                                                \
		}                                                                                                          \
	}

#define FUNC7S(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7)                                \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7) { \
		WRITE_ACTION                                                                                          \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                             \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7); \
			SYNC_DEBUG                                                                                        \
			MAIN_THREAD_SYNC_CHECK                                                                            \
		} else {                                                                                              \
			command_queue.flush_if_pending();                                                                 \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7);                                                  \
		}                                                                                                     \
	}

#define FUNC7SC(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7)                                     \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                   \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7);       \
			SYNC_DEBUG                                                                                              \
			MAIN_THREAD_SYNC_CHECK                                                                                  \
		} else {                                                                                                    \
			command_queue.flush_if_pending();                                                                       \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7);                                                        \
		}                                                                                                           \
	}

#define FUNC7(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7)                                 \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7) { \
		WRITE_ACTION                                                                                          \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                             \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7);          \
		} else {                                                                                              \
			command_queue.flush_if_pending();                                                                 \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7);                                                  \
		}                                                                                                     \
	}

#define FUNC7C(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7)                                      \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                   \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7);                \
		} else {                                                                                                    \
			command_queue.flush_if_pending();                                                                       \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7);                                                        \
		}                                                                                                           \
	}

#define FUNC8R(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8)                             \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8) { \
		WRITE_ACTION                                                                                                    \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                       \
			m_r ret;                                                                                                    \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5, p6, p7, p8);  \
			SYNC_DEBUG                                                                                                  \
			MAIN_THREAD_SYNC_CHECK                                                                                      \
			return ret;                                                                                                 \
		} else {                                                                                                        \
			command_queue.flush_if_pending();                                                                           \
			return server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8);                                                 \
		}                                                                                                               \
	}

#define FUNC8RC(m_r, m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8)                                  \
	_FORCE_INLINE_ m_r m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                             \
			m_r ret;                                                                                                          \
			command_queue.push_and_ret_static<&ServerName::m_type>(server_name, &ret, p1, p2, p3, p4, p5, p6, p7, p8);        \
			SYNC_DEBUG                                                                                                        \
			MAIN_THREAD_SYNC_CHECK                                                                                            \
			return ret;                                                                                                       \
		} else {                                                                                                              \
			command_queue.flush_if_pending();                                                                                 \
			return server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8);                                                       \
		}                                                                                                                     \
	}

#define FUNC8S(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8)                                   \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8) { \
		WRITE_ACTION                                                                                                     \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                        \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8);        \
			SYNC_DEBUG                                                                                                   \
			MAIN_THREAD_SYNC_CHECK                                                                                       \
		} else {                                                                                                         \
			command_queue.flush_if_pending();                                                                            \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8);                                                         \
		}                                                                                                                \
	}

#define FUNC8SC(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8)                                        \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                              \
			command_queue.push_and_sync_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8);              \
			SYNC_DEBUG                                                                                                         \
			MAIN_THREAD_SYNC_CHECK                                                                                             \
		} else {                                                                                                               \
			command_queue.flush_if_pending();                                                                                  \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8);                                                               \
		}                                                                                                                      \
	}

#define FUNC8(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8)                                    \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8) { \
		WRITE_ACTION                                                                                                     \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                        \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8);                 \
		} else {                                                                                                         \
			command_queue.flush_if_pending();                                                                            \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8);                                                         \
		}                                                                                                                \
	}

#define FUNC8C(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8)                                         \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8) const { \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                              \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8);                       \
		} else {                                                                                                               \
			command_queue.flush_if_pending();                                                                                  \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8);                                                               \
		}                                                                                                                      \
	}

#define FUNC9(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9)                                       \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9) { \
		WRITE_ACTION                                                                                                                \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                   \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9);                        \
		} else {                                                                                                                    \
			command_queue.flush_if_pending();                                                                                       \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9);                                                                \
		}                                                                                                                           \
	}

#define FUNC10(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9, m_arg10)                                          \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9, m_arg10 p10) { \
		WRITE_ACTION                                                                                                                             \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                                \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);                                \
		} else {                                                                                                                                 \
			command_queue.flush_if_pending();                                                                                                    \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);                                                                        \
		}                                                                                                                                        \
	}

#define FUNC11(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9, m_arg10, m_arg11)                                              \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9, m_arg10 p10, m_arg11 p11) { \
		WRITE_ACTION                                                                                                                                          \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                                             \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);                                        \
		} else {                                                                                                                                              \
			command_queue.flush_if_pending();                                                                                                                 \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);                                                                                \
		}                                                                                                                                                     \
	}

#define FUNC12(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9, m_arg10, m_arg11, m_arg12)                                                  \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9, m_arg10 p10, m_arg11 p11, m_arg12 p12) { \
		WRITE_ACTION                                                                                                                                                       \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                                                          \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);                                                \
		} else {                                                                                                                                                           \
			command_queue.flush_if_pending();                                                                                                                              \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);                                                                                        \
		}                                                                                                                                                                  \
	}

#define FUNC13(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9, m_arg10, m_arg11, m_arg12, m_arg13)                                                      \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9, m_arg10 p10, m_arg11 p11, m_arg12 p12, m_arg13 p13) { \
		WRITE_ACTION                                                                                                                                                                    \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                                                                       \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);                                                        \
		} else {                                                                                                                                                                        \
			command_queue.flush_if_pending();                                                                                                                                           \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);                                                                                                \
		}                                                                                                                                                                               \
	}

#define FUNC14(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9, m_arg10, m_arg11, m_arg12, m_arg13, m_arg14)                                                          \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9, m_arg10 p10, m_arg11 p11, m_arg12 p12, m_arg13 p13, m_arg14 p14) { \
		WRITE_ACTION                                                                                                                                                                                 \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                                                                                    \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);                                                                \
		} else {                                                                                                                                                                                     \
			command_queue.flush_if_pending();                                                                                                                                                        \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);                                                                                                        \
		}                                                                                                                                                                                            \
	}

#define FUNC15(m_type, m_arg1, m_arg2, m_arg3, m_arg4, m_arg5, m_arg6, m_arg7, m_arg8, m_arg9, m_arg10, m_arg11, m_arg12, m_arg13, m_arg14, m_arg15)                                                              \
	_FORCE_INLINE_ void m_type(m_arg1 p1, m_arg2 p2, m_arg3 p3, m_arg4 p4, m_arg5 p5, m_arg6 p6, m_arg7 p7, m_arg8 p8, m_arg9 p9, m_arg10 p10, m_arg11 p11, m_arg12 p12, m_arg13 p13, m_arg14 p14, m_arg15 p15) { \
		WRITE_ACTION                                                                                                                                                                                              \
		if (unlikely(Thread::get_caller_id() != server_thread)) {                                                                                                                                                 \
			command_queue.push_static<&ServerName::m_type>(server_name, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);                                                                        \
		} else {                                                                                                                                                                                                  \
			command_queue.flush_if_pending();                                                                                                                                                                     \
			server_name->m_type(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);                                                                                                                \
		}                                                                                                                                                                                                         \
	}

#endif // SERVER_WRAP_MT_COMMON_H
