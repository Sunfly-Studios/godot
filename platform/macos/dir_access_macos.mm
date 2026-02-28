/**************************************************************************/
/*  dir_access_macos.mm                                                   */
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

#include "dir_access_macos.h"

#include "core/config/project_settings.h"

#if defined(UNIX_ENABLED)

#include <errno.h>

#import <AppKit/NSWorkspace.h>
#import <Foundation/Foundation.h>

String DirAccessMacOS::fix_unicode_name(const char *p_name) const {
	String fname;
	if (p_name != nullptr) {
		@autoreleasepool {
			NSString *nsstr = [[NSString stringWithUTF8String:p_name] precomposedStringWithCanonicalMapping];
			
			// Prevent C++ null dereference crash if
			// the file name contains invalid UTF-8
			if (nsstr != nil) {
				const char *utf8_str = [nsstr UTF8String];
				if (utf8_str != nullptr) {
					fname.parse_utf8(utf8_str);
				}
			} else {
				// Fallback if conversion fails
				fname.parse_utf8(p_name); 
			}
		}
	}
	return fname;
}

int DirAccessMacOS::get_drive_count() {
	@autoreleasepool {
		NSArray *res_keys = [NSArray arrayWithObjects:NSURLVolumeURLKey, NSURLIsSystemImmutableKey, nil];
		NSArray *vols = [[NSFileManager defaultManager] mountedVolumeURLsIncludingResourceValuesForKeys:res_keys options:NSVolumeEnumerationSkipHiddenVolumes];
		return (int)[vols count];
	}
}

String DirAccessMacOS::get_drive(int p_drive) {
	@autoreleasepool {
		NSArray *res_keys = [NSArray arrayWithObjects:NSURLVolumeURLKey, NSURLIsSystemImmutableKey, nil];
		NSArray *vols = [[NSFileManager defaultManager] mountedVolumeURLsIncludingResourceValuesForKeys:res_keys options:NSVolumeEnumerationSkipHiddenVolumes];
		int count = (int)[vols count];

		ERR_FAIL_INDEX_V(p_drive, count, "");

		String volname;
		NSString *path = [vols[p_drive] path];
		
		if (path != nil) {
			const char *utf8_str = [path UTF8String];
			if (utf8_str != nullptr) {
				volname.parse_utf8(utf8_str);
			}
		}

		return volname;
	}
}

bool DirAccessMacOS::is_hidden(const String &p_name) {
	@autoreleasepool {
		String f = get_current_dir().path_join(p_name);
		NSString *ns_path = [NSString stringWithUTF8String:f.utf8().get_data()];
		
		// Defend against invalid UTF-8 paths returning nil
		if (!ns_path) {
			return DirAccessUnix::is_hidden(p_name);
		}

		NSURL *url = [NSURL fileURLWithPath:ns_path];
		if (!url) {
			return DirAccessUnix::is_hidden(p_name);
		}

		NSNumber *hidden = nil;
		if (![url getResourceValue:&hidden forKey:NSURLIsHiddenKey error:nil]) {
			return DirAccessUnix::is_hidden(p_name);
		}
		return [hidden boolValue];
	}
}

bool DirAccessMacOS::is_case_sensitive(const String &p_path) const {
	@autoreleasepool {
		String f = p_path;
		if (!f.is_absolute_path()) {
			f = get_current_dir().path_join(f);
		}
		f = fix_path(f);

		NSString *ns_path = [NSString stringWithUTF8String:f.utf8().get_data()];
		if (!ns_path) {
			return false;
		}

		NSURL *url = [NSURL fileURLWithPath:ns_path];
		if (!url) {
			return false;
		}

		NSNumber *cs = nil;
		if (![url getResourceValue:&cs forKey:NSURLVolumeSupportsCaseSensitiveNamesKey error:nil]) {
			return false;
		}
		return [cs boolValue];
	}
}

bool DirAccessMacOS::is_bundle(const String &p_file) const {
	@autoreleasepool {
		String f = p_file;
		if (!f.is_absolute_path()) {
			f = get_current_dir().path_join(f);
		}
		f = fix_path(f);

		NSString *ns_path = [NSString stringWithUTF8String:f.utf8().get_data()];
		
		// Prevent passing nil to isFilePackageAtPath
		if (!ns_path) {
			return false;
		}

		return [[NSWorkspace sharedWorkspace] isFilePackageAtPath:ns_path];
	}
}

#endif // UNIX_ENABLED
