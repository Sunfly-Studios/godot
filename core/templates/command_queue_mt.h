/**************************************************************************/
/*  command_queue_mt.h                                                    */
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

#ifndef COMMAND_QUEUE_MT_H
#define COMMAND_QUEUE_MT_H

#include "core/object/worker_thread_pool.h"
#include "core/os/condition_variable.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "core/templates/simple_type.h"
#include "core/templates/tuple.h"
#include "core/typedefs.h"

#include <type_traits>

class CommandQueueMT {
	static const size_t MAX_COMMAND_SIZE = 1024;

	struct CommandBase {
		typedef void (*CallThunk)(CommandBase *);
		typedef void (*DestroyThunk)(CommandBase *);

		CallThunk call_fn = nullptr;
		DestroyThunk destroy_fn = nullptr;
		bool sync = false;

		_FORCE_INLINE_ CommandBase(CallThunk p_call, DestroyThunk p_destroy, bool p_sync) :
				call_fn(p_call), destroy_fn(p_destroy), sync(p_sync) {}
	};

	// Runtime PTMF Command (kept for external compatibility)
	template <typename T, typename M, bool NeedsSync, typename... Args>
	struct Command : public CommandBase {
		T *instance;
		M method;
		Tuple<GetSimpleTypeT<Args>...> args;

		_NO_INLINE_ static void _call_thunk(CommandBase *p_base) {
			Command *self = static_cast<Command *>(p_base);
			self->call_impl(BuildIndexSequence<sizeof...(Args)>{});
		}

		_NO_INLINE_ static void _destroy_thunk(CommandBase *p_base) {
			static_cast<Command *>(p_base)->~Command();
		}

		template <typename... FwdArgs>
		_FORCE_INLINE_ Command(T *p_instance, M p_method, FwdArgs &&...p_args) :
				CommandBase(
						&_call_thunk,
						std::is_trivially_destructible_v<Tuple<GetSimpleTypeT<Args>...>> ? nullptr : &_destroy_thunk,
						NeedsSync),
				instance(p_instance),
				method(p_method),
				args(std::forward<FwdArgs>(p_args)...) {}

	private:
		template <size_t... I>
		_FORCE_INLINE_ void call_impl(IndexSequence<I...>) {
			// Move out of the Tuple, this will be destroyed as soon as the call is complete.
			(instance->*method)(std::move(get<I>())...);
		}

		// This method exists so we can call it in the parameter pack expansion in call_impl.
		template <size_t I>
		_FORCE_INLINE_ auto &get() { return ::tuple_get<I>(args); }
	};

	// Separate class from Command so we can save the space of the ret pointer for commands that don't return.
	// Runtime PTMF CommandRet (kept for external compatibility)
	template <typename T, typename M, typename R, typename... Args>
	struct CommandRet : public CommandBase {
		T *instance;
		M method;
		R *ret;
		Tuple<GetSimpleTypeT<Args>...> args;

		_NO_INLINE_ static void _call_thunk(CommandBase *p_base) {
			CommandRet *self = static_cast<CommandRet *>(p_base);
			*self->ret = self->call_impl(BuildIndexSequence<sizeof...(Args)>{});
		}

		_NO_INLINE_ static void _destroy_thunk(CommandBase *p_base) {
			static_cast<CommandRet *>(p_base)->~CommandRet();
		}

		_FORCE_INLINE_ CommandRet(T *p_instance, M p_method, R *p_ret, GetSimpleTypeT<Args>... p_args) :
				CommandBase(
						&_call_thunk,
						std::is_trivially_destructible_v<Tuple<GetSimpleTypeT<Args>...>> ? nullptr : &_destroy_thunk,
						true),
				instance(p_instance),
				method(p_method),
				ret(p_ret),
				args{ p_args... } {}

	private:
		template <size_t... I>
		_FORCE_INLINE_ R call_impl(IndexSequence<I...>) {
			// Move out of the Tuple, this will be destroyed as soon as the call is complete.
			return (instance->*method)(std::move(get<I>())...);
		}

		// This method exists so we can call it in the parameter pack expansion in call_impl.
		template <size_t I>
		_FORCE_INLINE_ auto &get() { return ::tuple_get<I>(args); }
	};

	// Compile-time NTTP CommandStatic
	template <typename T, auto Method, bool NeedsSync, typename... Args>
	struct CommandStatic : public CommandBase {
		T *instance;
		Tuple<GetSimpleTypeT<Args>...> args;

		_NO_INLINE_ static void _call_thunk(CommandBase *p_base) {
			CommandStatic *self = static_cast<CommandStatic *>(p_base);
			self->call_impl(BuildIndexSequence<sizeof...(Args)>{});
		}

		_NO_INLINE_ static void _destroy_thunk(CommandBase *p_base) {
			static_cast<CommandStatic *>(p_base)->~CommandStatic();
		}

		template <typename... FwdArgs>
		_FORCE_INLINE_ CommandStatic(T *p_instance, FwdArgs &&...p_args) :
				CommandBase(
						&_call_thunk,
						std::is_trivially_destructible_v<Tuple<GetSimpleTypeT<Args>...>> ? nullptr : &_destroy_thunk,
						NeedsSync),
				instance(p_instance),
				args(std::forward<FwdArgs>(p_args)...) {}

	private:
		template <size_t... I>
		_FORCE_INLINE_ void call_impl(IndexSequence<I...>) {
			// Move out of the Tuple, this will be destroyed as soon as the call is complete.
			(instance->*Method)(std::move(get<I>())...);
		}

		// This method exists so we can call it in the parameter pack expansion in call_impl.
		template <size_t I>
		_FORCE_INLINE_ auto &get() { return ::tuple_get<I>(args); }
	};

	// Compile-time NTTP CommandRetStatic
	template <typename T, auto Method, typename R, typename... Args>
	struct CommandRetStatic : public CommandBase {
		T *instance;
		R *ret;
		Tuple<GetSimpleTypeT<Args>...> args;

		_NO_INLINE_ static void _call_thunk(CommandBase *p_base) {
			CommandRetStatic *self = static_cast<CommandRetStatic *>(p_base);
			*self->ret = self->call_impl(BuildIndexSequence<sizeof...(Args)>{});
		}

		_NO_INLINE_ static void _destroy_thunk(CommandBase *p_base) {
			static_cast<CommandRetStatic *>(p_base)->~CommandRetStatic();
		}

		_FORCE_INLINE_ CommandRetStatic(T *p_instance, R *p_ret, GetSimpleTypeT<Args>... p_args) :
				CommandBase(
						&_call_thunk,
						std::is_trivially_destructible_v<Tuple<GetSimpleTypeT<Args>...>> ? nullptr : &_destroy_thunk,
						true),
				instance(p_instance),
				ret(p_ret),
				args{ p_args... } {}

	private:
		template <size_t... I>
		_FORCE_INLINE_ R call_impl(IndexSequence<I...>) {
			// Move out of the Tuple, this will be destroyed as soon as the call is complete.
			return (instance->*Method)(std::move(get<I>())...);
		}

		// This method exists so we can call it in the parameter pack expansion in call_impl.
		template <size_t I>
		_FORCE_INLINE_ auto &get() { return ::tuple_get<I>(args); }
	};

	// Type-erased CommandThunk (Signature-based)
	template <typename T, typename ThunkFunc, bool NeedsSync, typename... Args>
	struct CommandThunk : public CommandBase {
		T *instance;
		ThunkFunc thunk;
		Tuple<GetSimpleTypeT<Args>...> args;

		_NO_INLINE_ static void _call_thunk(CommandBase *p_base) {
			CommandThunk *self = static_cast<CommandThunk *>(p_base);
			self->call_impl(BuildIndexSequence<sizeof...(Args)>{});
		}

		_NO_INLINE_ static void _destroy_thunk(CommandBase *p_base) {
			static_cast<CommandThunk *>(p_base)->~CommandThunk();
		}

		template <typename... FwdArgs>
		_FORCE_INLINE_ CommandThunk(T *p_instance, ThunkFunc p_thunk, FwdArgs &&...p_args) :
				CommandBase(
						&_call_thunk,
						std::is_trivially_destructible_v<Tuple<GetSimpleTypeT<Args>...>> ? nullptr : &_destroy_thunk,
						NeedsSync),
				instance(p_instance),
				thunk(p_thunk),
				args(std::forward<FwdArgs>(p_args)...) {}

	private:
		template <size_t... I>
		_FORCE_INLINE_ void call_impl(IndexSequence<I...>) {
			// Move out of the Tuple, this will be destroyed as soon as the call is complete.
			thunk(instance, std::move(get<I>())...);
		}

		// This method exists so we can call it in the parameter pack expansion in call_impl.
		template <size_t I>
		_FORCE_INLINE_ auto &get() { return ::tuple_get<I>(args); }
	};

	template <typename T, typename ThunkFunc, typename R, typename... Args>
	struct CommandRetThunk : public CommandBase {
		T *instance;
		ThunkFunc thunk;
		R *ret;
		Tuple<GetSimpleTypeT<Args>...> args;

		_NO_INLINE_ static void _call_thunk(CommandBase *p_base) {
			CommandRetThunk *self = static_cast<CommandRetThunk *>(p_base);
			*self->ret = self->call_impl(BuildIndexSequence<sizeof...(Args)>{});
		}

		_NO_INLINE_ static void _destroy_thunk(CommandBase *p_base) {
			static_cast<CommandRetThunk *>(p_base)->~CommandRetThunk();
		}

		_FORCE_INLINE_ CommandRetThunk(T *p_instance, ThunkFunc p_thunk, R *p_ret, GetSimpleTypeT<Args>... p_args) :
				CommandBase(
						&_call_thunk,
						std::is_trivially_destructible_v<Tuple<GetSimpleTypeT<Args>...>> ? nullptr : &_destroy_thunk,
						true),
				instance(p_instance),
				thunk(p_thunk),
				ret(p_ret),
				args{ p_args... } {}

	private:
		template <size_t... I>
		_FORCE_INLINE_ R call_impl(IndexSequence<I...>) {
			// Move out of the Tuple, this will be destroyed as soon as the call is complete.
			return thunk(instance, std::move(get<I>())...);
		}

		// This method exists so we can call it in the parameter pack expansion in call_impl.
		template <size_t I>
		_FORCE_INLINE_ auto &get() { return ::tuple_get<I>(args); }
	};

	/***** BASE *******/

	static const uint32_t DEFAULT_COMMAND_MEM_SIZE_KB = 64;

	BinaryMutex mutex;
	LocalVector<uint8_t> command_mem;
	ConditionVariable sync_cond_var;
	uint32_t sync_head = 0;
	uint32_t sync_tail = 0;
	uint32_t sync_awaiters = 0;
	WorkerThreadPool::TaskID pump_task_id = WorkerThreadPool::INVALID_TASK_ID;
	uint64_t flush_read_ptr = 0;
	std::atomic<bool> pending{ false };

	template <typename T, typename... Args>
	_FORCE_INLINE_ void create_command(Args &&...p_args) {
		// alloc size is size+T+safeguard
		constexpr uint64_t alloc_size = ((sizeof(T) + 8U - 1U) & ~(8U - 1U));
		static_assert(alloc_size < UINT32_MAX, "Type too large to fit in the command queue.");

		uint64_t size = command_mem.size();
		command_mem.resize(size + alloc_size + sizeof(uint64_t));
		unaligned_write<uint64_t>(&command_mem[size], alloc_size);

		void *cmd = &command_mem[size + sizeof(uint64_t)];
		::new (cmd) T(std::forward<Args>(p_args)...);
		pending.store(true);
	}

	template <typename T, bool NeedsSync, typename... Args>
	_NO_INLINE_ void _push_internal(Args &&...args) {
		MutexLock mlock(mutex);
		create_command<T>(std::forward<Args>(args)...);

		if (pump_task_id != WorkerThreadPool::INVALID_TASK_ID) {
			WorkerThreadPool::get_singleton()->notify_yield_over(pump_task_id);
		}

		if constexpr (NeedsSync) {
			sync_tail++;
			_wait_for_sync(mlock);
		}
	}

	_FORCE_INLINE_ void _prevent_sync_wraparound() {
		bool safe_to_reset = !sync_awaiters;
		bool already_sync_to_latest = sync_head == sync_tail;
		if (safe_to_reset && already_sync_to_latest) {
			sync_head = 0;
			sync_tail = 0;
		}
	}

	void _flush() {
		MutexLock lock(mutex);

		if (unlikely(flush_read_ptr)) {
			// Re-entrant call.
			return;
		}

		alignas(Memory::MAX_ALIGN) char cmd_backup[MAX_COMMAND_SIZE];

		while (flush_read_ptr < command_mem.size()) {
			uint64_t size = unaligned_read<uint64_t>(&command_mem[flush_read_ptr]);
			flush_read_ptr += sizeof(uint64_t);

			CommandBase *cmd = std::launder(reinterpret_cast<CommandBase *>(&command_mem[flush_read_ptr]));

			// Protect against race condition between this thread
			// during the call to the command and other threads potentially
			// invalidating the pointer due to reallocs.
			memcpy(cmd_backup, reinterpret_cast<const char *>(cmd), size);

			uint32_t allowance_id = WorkerThreadPool::thread_enter_unlock_allowance_zone(lock);
			CommandBase *backup_cmd = std::launder(reinterpret_cast<CommandBase *>(cmd_backup));
			backup_cmd->call_fn(backup_cmd);
			WorkerThreadPool::thread_exit_unlock_allowance_zone(allowance_id);

			// Handle potential realloc due to the command and unlock allowance.
			cmd = std::launder(reinterpret_cast<CommandBase *>(&command_mem[flush_read_ptr]));

			if (unlikely(cmd->sync)) {
				sync_head++;
				lock.temp_unlock(); // Give an opportunity to awaiters right away.
				sync_cond_var.notify_all();
				lock.temp_relock();
				// Handle potential realloc happened during unlock.
				cmd = std::launder(reinterpret_cast<CommandBase *>(&command_mem[flush_read_ptr]));
			}

			if (cmd->destroy_fn) {
				cmd->destroy_fn(cmd);
			}

			flush_read_ptr += size;
		}

		command_mem.clear();
		pending.store(false);
		flush_read_ptr = 0;

		_prevent_sync_wraparound();
	}

	_FORCE_INLINE_ void _wait_for_sync(MutexLock<BinaryMutex> &p_lock) {
		sync_awaiters++;
		uint32_t sync_head_goal = sync_tail;
		do {
			sync_cond_var.wait(p_lock);
		} while (sync_head < sync_head_goal);
		sync_awaiters--;
		_prevent_sync_wraparound();
	}

	void _no_op() {}

public:
	// Legacy runtime-PTMF API (kept for external compatibility).
	template <typename T, typename M, typename... Args>
	_FORCE_INLINE_ void push(T *p_instance, M p_method, Args &&...p_args) {
		using CommandType = Command<T, M, false, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, false>(p_instance, p_method, std::forward<Args>(p_args)...);
	}

	template <typename T, typename M, typename... Args>
	_FORCE_INLINE_ void push_and_sync(T *p_instance, M p_method, Args... p_args) {
		using CommandType = Command<T, M, true, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, true>(p_instance, p_method, std::forward<Args>(p_args)...);
	}

	template <typename T, typename M, typename R, typename... Args>
	_FORCE_INLINE_ void push_and_ret(T *p_instance, M p_method, R *r_ret, Args... p_args) {
		using CommandType = CommandRet<T, M, R, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, true>(p_instance, p_method, r_ret, std::forward<Args>(p_args)...);
	}

	// Compile-time NTTP API (recommended)
	template <auto Method, typename T, typename... Args>
	_FORCE_INLINE_ void push_static(T *p_instance, Args &&...p_args) {
		// Standard command, no sync.
		using CommandType = CommandStatic<T, Method, false, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, false>(p_instance, std::forward<Args>(p_args)...);
	}

	template <auto Method, typename T, typename... Args>
	_FORCE_INLINE_ void push_and_sync_static(T *p_instance, Args... p_args) {
		// Standard command, sync.
		using CommandType = CommandStatic<T, Method, true, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, true>(p_instance, std::forward<Args>(p_args)...);
	}

	template <auto Method, typename T, typename R, typename... Args>
	_FORCE_INLINE_ void push_and_ret_static(T *p_instance, R *r_ret, Args... p_args) {
		// Command with return value, sync.
		using CommandType = CommandRetStatic<T, Method, R, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, true>(p_instance, r_ret, std::forward<Args>(p_args)...);
	}

	// Signature-based Thunk API
	template <typename T, typename ThunkFunc, typename... Args>
	_FORCE_INLINE_ void push_thunk(T *p_instance, ThunkFunc p_thunk, Args &&...p_args) {
		using CommandType = CommandThunk<T, ThunkFunc, false, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, false>(p_instance, p_thunk, std::forward<Args>(p_args)...);
	}

	template <typename T, typename ThunkFunc, typename... Args>
	_FORCE_INLINE_ void push_and_sync_thunk(T *p_instance, ThunkFunc p_thunk, Args... p_args) {
		using CommandType = CommandThunk<T, ThunkFunc, true, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, true>(p_instance, p_thunk, std::forward<Args>(p_args)...);
	}

	template <typename T, typename ThunkFunc, typename R, typename... Args>
	_FORCE_INLINE_ void push_and_ret_thunk(T *p_instance, ThunkFunc p_thunk, R *r_ret, Args... p_args) {
		using CommandType = CommandRetThunk<T, ThunkFunc, R, Args...>;
		static_assert(sizeof(CommandType) <= MAX_COMMAND_SIZE);
		_push_internal<CommandType, true>(p_instance, p_thunk, r_ret, std::forward<Args>(p_args)...);
	}

	_FORCE_INLINE_ void flush_if_pending() {
		if (unlikely(pending.load())) {
			_flush();
		}
	}

	void flush_all() {
		_flush();
	}

	void sync() {
		push_and_sync_static<&CommandQueueMT::_no_op>(this);
	}

	void wait_and_flush() {
		ERR_FAIL_COND(pump_task_id == WorkerThreadPool::INVALID_TASK_ID);
		WorkerThreadPool::get_singleton()->wait_for_task_completion(pump_task_id);
		_flush();
	}

	void set_pump_task_id(WorkerThreadPool::TaskID p_task_id) {
		MutexLock lock(mutex);
		pump_task_id = p_task_id;
	}

	CommandQueueMT();
	~CommandQueueMT();
};

#endif // COMMAND_QUEUE_MT_H
