/**************************************************************************/
/*  godot_ios.mm                                                          */
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

#import "os_ios.h"

#include "core/string/ustring.h"
#include "main/main.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

static OS_IOS *os = nullptr;
// Use a vector to safely manage dynamic argument memory for the lifetime of the engine.
static std::vector<char *> dynamic_args;

void add_path(std::vector<char *> &p_args) {
	@autoreleasepool {
		id obj = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"godot_path"];
		if (!obj || ![obj isKindOfClass:[NSString class]]) {
			return;
		}

		NSString *str = (NSString *)obj;
		// Use strdup so the C-string survives past the autorelease pool
		p_args.push_back(strdup("--path"));
		p_args.push_back(strdup([str UTF8String]));
	}
}

void add_cmdline(std::vector<char *> &p_args) {
	@autoreleasepool {
		id obj = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"godot_cmdline"];
		if (!obj || ![obj isKindOfClass:[NSArray class]]) {
			return;
		}

		NSArray *arr = (NSArray *)obj;
		for (NSUInteger i = 0; i < [arr count]; i++) {
			id item = [arr objectAtIndex:i];
			if (!item || ![item isKindOfClass:[NSString class]]) {
				continue;
			}
			
			NSString *str = (NSString *)item;
			p_args.push_back(strdup([str UTF8String]));
		}
	}
}

int ios_main(int argc, char **argv) {
	if (argc > 0 && argv[0] != nullptr) {
		const char *last_slash = strrchr(argv[0], '/');
		if (last_slash != nullptr) {
			size_t len = last_slash - argv[0];
			char path[512] = {};
			
			// Protect against excessively long paths causing buffer overflows
			size_t safe_len = len >= sizeof(path) ? sizeof(path) - 1 : len;
			memcpy(path, argv[0], safe_len);
			path[safe_len] = '\0';
			
			chdir(path);
		}
	}

	os = new OS_IOS();

	// We must override main when testing is enabled
	TEST_MAIN_OVERRIDE

	// Prevent stack-smashing by pushing arguments into a dynamic vector
	for (int i = 0; i < argc; i++) {
		if (argv[i] != nullptr) {
			dynamic_args.push_back(strdup(argv[i]));
		}
	}

	add_path(dynamic_args);
	add_cmdline(dynamic_args);

	// Reconstruct safely bounded arguments for Main::setup
	int setup_argc = (int)dynamic_args.size();
	char *exec_path = setup_argc > 0 ? dynamic_args[0] : strdup("");
	
	// Main::setup expects: execpath, argc (excluding execpath), and argv (starting after execpath)
	int engine_argc = setup_argc > 1 ? setup_argc - 1 : 0;
	char **engine_argv = setup_argc > 1 ? &dynamic_args[1] : nullptr;

	Error err = Main::setup(exec_path, engine_argc, engine_argv, false);

	if (err != OK) {
		if (err == ERR_HELP) { // Returned by --help and --version, so success.
			return EXIT_SUCCESS;
		}
		return EXIT_FAILURE;
	}

	os->initialize_modules();

	return os->get_exit_code();
}

void ios_finish() {
	Main::cleanup();
	delete os;
	
	// Clean up the duplicated strings
	for (char *arg : dynamic_args) {
		free(arg);
	}
	dynamic_args.clear();
}
