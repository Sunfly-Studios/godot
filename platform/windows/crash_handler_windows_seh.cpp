/**************************************************************************/
/*  crash_handler_windows_seh.cpp                                         */
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

#include "crash_handler_windows.h"

#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/version.h"
#include "main/main.h"

#ifdef CRASH_HANDLER_EXCEPTION

// Backtrace code based on: https://stackoverflow.com/questions/6205981/windows-c-stack-trace-from-a-running-app

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include <psapi.h>

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#pragma pack(push, before_imagehlp, 8)
#include <imagehlp.h>
#pragma pack(pop, before_imagehlp)

struct module_data {
	std::string image_name;
	std::string module_name;
	void *base_address = nullptr;
	DWORD load_size = 0;
};

class symbol {
	typedef IMAGEHLP_SYMBOL64 sym_type;
	sym_type *sym = nullptr;
	static const int max_name_len = 1024;

public:
	symbol(HANDLE process, DWORD64 address) {
		// Use malloc/free instead of new/delete.
		// Using 'new' can throw exceptions (std::bad_alloc), which is dangerous
		// if the crash was caused by heap corruption or runtime failure.
		sym = (sym_type *)malloc(sizeof(sym_type) + max_name_len);
		if (sym) {
			memset(sym, '\0', sizeof(sym_type) + max_name_len);
			sym->SizeOfStruct = sizeof(sym_type);
			sym->MaxNameLength = max_name_len;
			DWORD64 displacement;

			if (!SymGetSymFromAddr64(process, address, &displacement, sym)) {
				// If lookup fails, ensure we don't return garbage
				sym->Name[0] = '\0';
			}
		}
	}

	// Add destructor to prevent leaking ~1KB per frame printed.
	~symbol() {
		if (sym) {
			free(sym);
		}
	}

	std::string name() {
		if (!sym) {
			return "<alloc error>";
		}
		return std::string(sym->Name);
	}

	std::string undecorated_name() {
		if (!sym || *sym->Name == '\0') {
			return "<couldn't map PC to fn name>";
		}
		std::vector<char> und_name(max_name_len);
		if (UnDecorateSymbolName(sym->Name, &und_name[0], max_name_len, UNDNAME_COMPLETE)) {
			return std::string(&und_name[0]);
		}
		return std::string(sym->Name);
	}
};

class get_mod_info {
	HANDLE process;

public:
	get_mod_info(HANDLE h) :
			process(h) {}

	module_data operator()(HMODULE module) {
		module_data ret = {};

		// If the crash is EXCEPTION_STACK_OVERFLOW, allocating 4KB on stack
		// would causes a double-fault and instantaneous termination (no log).
		char *temp = (char *)malloc(4096);
		if (!temp) {
			return ret; // If we can't allocate 4KB, we can't resolve names. Return empty.
		}
		memset(temp, 0, 4096);

		MODULEINFO mi = {};
		if (GetModuleInformation(process, module, &mi, sizeof(mi))) {
			ret.base_address = mi.lpBaseOfDll;
			ret.load_size = mi.SizeOfImage;
		}

		if (GetModuleFileNameEx(process, module, temp, 4096)) {
			ret.image_name = temp;
		}
		if (GetModuleBaseName(process, module, temp, 4096)) {
			ret.module_name = temp;
		}

		// Free the temp buffer immediately.
		free(temp);
		SymLoadModule64(process, nullptr, ret.image_name.c_str(), ret.module_name.c_str(), (DWORD64)ret.base_address, ret.load_size);

		return ret;
	}
};

DWORD CrashHandlerException(EXCEPTION_POINTERS *ep) {
	HANDLE process = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();
	DWORD offset_from_symbol = 0;
	IMAGEHLP_LINE64 line = {};
	std::vector<module_data> modules;
	DWORD cbNeeded = {};
	std::vector<HMODULE> module_handles(1);

	// Check OS singleton existence before accessing
	if (!OS::get_singleton() || OS::get_singleton()->is_disable_crash_handler() || IsDebuggerPresent()) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (OS::get_singleton()->is_crash_handler_silent()) {
		std::_Exit(0);
	}

	String msg;
	const ProjectSettings *proj_settings = ProjectSettings::get_singleton();
	if (proj_settings) {
		msg = proj_settings->get("debug/settings/crash_handler/message");
	}

	// Tell MainLoop about the crash. This can be handled by users too in Node.
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_CRASH);
	}

	print_error("\n================================================================");
	print_error(vformat("%s: Program crashed", __FUNCTION__));

	// Print the engine version just before, so that people are reminded to include the version in backtrace reports.
	if (String(VERSION_HASH).is_empty()) {
		print_error(vformat("Engine version: %s", VERSION_FULL_NAME));
	} else {
		print_error(vformat("Engine version: %s (%s)", VERSION_FULL_NAME, VERSION_HASH));
	}
	print_error(vformat("Dumping the backtrace. %s", msg));

	// Load the symbols:
	if (!SymInitialize(process, nullptr, false)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_EXACT_SYMBOLS);

	// Ensure we don't crash resizing vectors if heap is corrupted,
	// though we can't do much if it is.
	if (EnumProcessModules(process, &module_handles[0], module_handles.size() * sizeof(HMODULE), &cbNeeded)) {
		module_handles.resize(cbNeeded / sizeof(HMODULE));
		EnumProcessModules(process, &module_handles[0], module_handles.size() * sizeof(HMODULE), &cbNeeded);
		std::transform(module_handles.begin(), module_handles.end(), std::back_inserter(modules), get_mod_info(process));
	}

	void *base = nullptr;
	if (!modules.empty()) {
		base = modules[0].base_address;
	}

	// Setup stuff:
	CONTEXT *context = ep->ContextRecord;
	STACKFRAME64 frame = {};
	bool skip_first = false;

	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrStack.Mode = AddrModeFlat;
	frame.AddrFrame.Mode = AddrModeFlat;

#if defined(_M_X64)
	frame.AddrPC.Offset = context->Rip;
	frame.AddrStack.Offset = context->Rsp;
	frame.AddrFrame.Offset = context->Rbp;
#elif defined(_M_ARM64) || defined(_M_ARM64EC)
	frame.AddrPC.Offset = context->Pc;
	frame.AddrStack.Offset = context->Sp;
	frame.AddrFrame.Offset = context->Fp;
#elif defined(_M_ARM)
	frame.AddrPC.Offset = context->Pc;
	frame.AddrStack.Offset = context->Sp;
	frame.AddrFrame.Offset = context->R11;
#else
	frame.AddrPC.Offset = context->Eip;
	frame.AddrStack.Offset = context->Esp;
	frame.AddrFrame.Offset = context->Ebp;

	// Skip the first one to avoid a duplicate on 32-bit mode
	skip_first = true;
#endif

	line.SizeOfStruct = sizeof(line);

	DWORD image_type = IMAGE_FILE_MACHINE_UNKNOWN;
	if (base) {
		IMAGE_NT_HEADERS *h = ImageNtHeader(base);
		if (h) {
			image_type = h->FileHeader.Machine;
		}
	}

	int n = 0;
	do {
		if (skip_first) {
			skip_first = false;
		} else {
			if (frame.AddrPC.Offset != 0) {
				std::string fnName = symbol(process, frame.AddrPC.Offset).undecorated_name();

				if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &offset_from_symbol, &line)) {
					print_error(vformat("[%d] %s (%s:%d)", n, fnName.c_str(), (char *)line.FileName, (int)line.LineNumber));
				} else {
					print_error(vformat("[%d] %s", n, fnName.c_str()));
				}
			} else {
				print_error(vformat("[%d] ???", n));
			}

			n++;
		}

		if (!StackWalk64(image_type, process, hThread, &frame, context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
			break;
		}
	} while (frame.AddrReturn.Offset != 0 && n < 256);

	print_error("-- END OF BACKTRACE --");
	print_error("================================================================");

	SymCleanup(process);

	// Pass the exception to the OS
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

CrashHandler::CrashHandler() {
	disabled = false;
}

CrashHandler::~CrashHandler() {
}

void CrashHandler::disable() {
	if (disabled) {
		return;
	}

	disabled = true;
}

void CrashHandler::initialize() {
}
