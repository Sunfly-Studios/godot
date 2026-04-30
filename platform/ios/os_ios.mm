/**************************************************************************/
/*  os_ios.mm                                                             */
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

#ifdef IOS_ENABLED

#import "app_delegate.h"
#import "display_server_ios.h"
#import "godot_view.h"
#import "ios_terminal_logger.h"
#import "view_controller.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "drivers/unix/syslog_logger.h"
#include "main/main.h"

#import <AudioToolbox/AudioServices.h>
#import <CoreText/CoreText.h>
#import <UIKit/UIKit.h>
#import <dlfcn.h>
#include <sys/sysctl.h>
#include <mutex>

#if defined(RD_ENABLED)
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#import <QuartzCore/CAMetalLayer.h>

#if defined(VULKAN_ENABLED)
#include "drivers/vulkan/godot_vulkan.h"
#endif // VULKAN_ENABLED
#endif

// Initialization order between compilation units is not guaranteed,
// so we use this as a hack to ensure certain code is called before
// everything else, but after all units are initialized.
typedef void (*init_callback)();
static init_callback *ios_init_callbacks = nullptr;
static int ios_init_callbacks_count = 0;
static int ios_init_callbacks_capacity = 0;

static std::mutex dynamic_symbol_mutex;
HashMap<String, void *> OS_IOS::dynamic_symbol_lookup_table;

void add_ios_init_callback(init_callback cb) {
	if (ios_init_callbacks_count == ios_init_callbacks_capacity) {
		int new_capacity = ios_init_callbacks_capacity + 32;
		// Store in a temporary pointer to prevent leaking the original 
		// memory block if realloc fails.
		void *new_ptr = realloc(ios_init_callbacks, sizeof(cb) * new_capacity);
		if (new_ptr) {
			ios_init_callbacks = (init_callback *)(new_ptr);
			ios_init_callbacks_capacity = new_capacity;
		} else {
			ERR_FAIL_MSG("Unable to allocate memory for extension callbacks.");
			return;
		}
	}
	ios_init_callbacks[ios_init_callbacks_count++] = cb;
}

void register_dynamic_symbol(char *name, void *address) {
	// Lock the map to prevent memory corruption if multiple GDExtensions 
    // load concurrently on background threads.
    std::lock_guard<std::mutex> lock(dynamic_symbol_mutex);
	OS_IOS::dynamic_symbol_lookup_table[String(name)] = address;
}

Rect2 fit_keep_aspect_centered(const Vector2 &p_container, const Vector2 &p_rect) {
	if (p_container.height <= 0.0f || p_rect.height <= 0.0f) {
        return Rect2();
    }
	real_t available_ratio = p_container.width / p_container.height;
	real_t fit_ratio = p_rect.width / p_rect.height;
	Rect2 result;
	if (fit_ratio < available_ratio) {
		// Fit height - we'll have horizontal gaps
		result.size.height = p_container.height;
		result.size.width = p_container.height * fit_ratio;
		result.position.y = 0;
		result.position.x = (p_container.width - result.size.width) * 0.5f;
	} else {
		// Fit width - we'll have vertical gaps
		result.size.width = p_container.width;
		result.size.height = p_container.width / fit_ratio;
		result.position.x = 0;
		result.position.y = (p_container.height - result.size.height) * 0.5f;
	}
	return result;
}

Rect2 fit_keep_aspect_covered(const Vector2 &p_container, const Vector2 &p_rect) {
	if (p_container.height <= 0.0f || p_rect.height <= 0.0f) {
        return Rect2();
    }
	real_t available_ratio = p_container.width / p_container.height;
	real_t fit_ratio = p_rect.width / p_rect.height;
	Rect2 result;
	if (fit_ratio < available_ratio) {
		// Need to scale up to fit width, and crop height
		result.size.width = p_container.width;
		result.size.height = p_container.width / fit_ratio;
		result.position.x = 0;
		result.position.y = (p_container.height - result.size.height) * 0.5f;
	} else {
		// Need to scale up to fit height, and crop width
		result.size.width = p_container.height * fit_ratio;
		result.size.height = p_container.height;
		result.position.x = (p_container.width - result.size.width) * 0.5f;
		result.position.y = 0;
	}
	return result;
}

OS_IOS *OS_IOS::get_singleton() {
	return (OS_IOS *)OS::get_singleton();
}

OS_IOS::OS_IOS() {
	for (int i = 0; i < ios_init_callbacks_count; ++i) {
		ios_init_callbacks[i]();
	}
	free(ios_init_callbacks);
	ios_init_callbacks = nullptr;
	ios_init_callbacks_count = 0;
	ios_init_callbacks_capacity = 0;

	main_loop = nullptr;

	Vector<Logger *> loggers;
	loggers.push_back(memnew(IOSTerminalLogger));
	_set_logger(memnew(CompositeLogger(loggers)));

	AudioDriverManager::add_driver(&audio_driver);

	DisplayServerIOS::register_ios_driver();
}

OS_IOS::~OS_IOS() {}

void OS_IOS::alert(const String &p_alert, const String &p_title) {
	const CharString utf8_alert = p_alert.utf8();
	const CharString utf8_title = p_title.utf8();
	iOS::alert(utf8_alert.get_data(), utf8_title.get_data());
}

void OS_IOS::initialize_core() {
	OS_Unix::initialize_core();
}

void OS_IOS::initialize() {
	initialize_core();
}

void OS_IOS::initialize_joypads() {
	joypad_apple = memnew(JoypadApple);
}

void OS_IOS::initialize_modules() {
	ios = memnew(iOS);
	Engine::get_singleton()->add_singleton(Engine::Singleton("iOS", ios));
}

void OS_IOS::deinitialize_modules() {
	if (joypad_apple) {
		memdelete(joypad_apple);
	}

	if (ios) {
		memdelete(ios);
	}
}

void OS_IOS::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

MainLoop *OS_IOS::get_main_loop() const {
	return main_loop;
}

void OS_IOS::delete_main_loop() {
	if (main_loop) {
		main_loop->finalize();
		memdelete(main_loop);
	}

	main_loop = nullptr;
}

bool OS_IOS::iterate() {
	if (!main_loop) {
		return true;
	}

	if (DisplayServer::get_singleton()) {
		DisplayServer::get_singleton()->process_events();
	}

	joypad_apple->process_joypads();

	return Main::iteration();
}

void OS_IOS::start() {
	if (Main::start() == EXIT_SUCCESS) {
		main_loop->initialize();
	}
}

void OS_IOS::finalize() {
	deinitialize_modules();

	// Already gets called
	//delete_main_loop();
}

// MARK: Dynamic Libraries

_FORCE_INLINE_ String OS_IOS::get_framework_executable(const String &p_path) {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
    String result = p_path; // Default fallback

    @autoreleasepool {
        const char *path_utf8 = p_path.utf8().get_data();
        if (path_utf8) {
            NSURL *url = [NSURL fileURLWithPath:@(path_utf8)];
            if (url) {
                NSBundle *bundle = [NSBundle bundleWithURL:url];
                if (bundle) {
                    NSString *exe_path_ns = [bundle executablePath];
                    // Verify framework bundle is not malformed.
                    if (exe_path_ns) {
                        const char *exe_path_utf8 = [exe_path_ns UTF8String];
                        if (exe_path_utf8) {
                            String exe_path = String::utf8(exe_path_utf8);
                            if (da->file_exists(exe_path)) {
                                return exe_path;
                            }
                        }
                    }
                }
            }
        }
    }

	// Try default executable name (invalid framework).
	if (da->dir_exists(p_path) && da->file_exists(p_path.path_join(p_path.get_file().get_basename()))) {
		return p_path.path_join(p_path.get_file().get_basename());
	}

	// Not a framework, try loading as .dylib.
	return result;
}

Error OS_IOS::open_dynamic_library(const String &p_path, void *&p_library_handle, GDExtensionData *p_data) {
	if (p_path.length() == 0) {
		// Static xcframework.
		p_library_handle = RTLD_SELF;

		if (p_data != nullptr && p_data->r_resolved_path != nullptr) {
			*p_data->r_resolved_path = p_path;
		}

		return OK;
	}

	String path = get_framework_executable(p_path);

	if (!FileAccess::exists(path)) {
		// Load .dylib or framework from within the executable path.
		path = get_framework_executable(get_executable_path().get_base_dir().path_join(p_path.get_file()));
	}

	if (!FileAccess::exists(path)) {
		// Load .dylib converted to framework from within the executable path.
		path = get_framework_executable(get_executable_path().get_base_dir().path_join(p_path.get_file().get_basename() + ".framework"));
	}

	if (!FileAccess::exists(path)) {
		// Load .dylib from within the executable path.
		path = get_framework_executable(get_executable_path().get_base_dir().path_join(p_path.get_file().get_basename() + ".dylib"));
	}

	if (!FileAccess::exists(path)) {
		// Load .dylib or framework from a standard iOS location.
		path = get_framework_executable(get_executable_path().get_base_dir().path_join("Frameworks").path_join(p_path.get_file()));
	}

	if (!FileAccess::exists(path)) {
		// Load .dylib converted to framework from a standard iOS location.
		path = get_framework_executable(get_executable_path().get_base_dir().path_join("Frameworks").path_join(p_path.get_file().get_basename() + ".framework"));
	}

	if (!FileAccess::exists(path)) {
		// Load .dylib from a standard iOS location.
		path = get_framework_executable(get_executable_path().get_base_dir().path_join("Frameworks").path_join(p_path.get_file().get_basename() + ".dylib"));
	}

	if (!FileAccess::exists(path) && (p_path.ends_with(".a") || p_path.ends_with(".xcframework"))) {
		// Static library already linked into the binary — use RTLD_SELF.
		p_library_handle = RTLD_SELF;

		if (p_data != nullptr && p_data->r_resolved_path != nullptr) {
			*p_data->r_resolved_path = p_path;
		}

		return OK;
	} else {
		ERR_FAIL_COND_V(!FileAccess::exists(path), ERR_FILE_NOT_FOUND);
	}
	p_library_handle = dlopen(path.utf8().get_data(), RTLD_NOW);
	ERR_FAIL_NULL_V_MSG(p_library_handle, ERR_CANT_OPEN, vformat("Can't open dynamic library: %s. Error: %s.", p_path, dlerror()));

	if (p_data != nullptr && p_data->r_resolved_path != nullptr) {
		*p_data->r_resolved_path = path;
	}

	return OK;
}

Error OS_IOS::close_dynamic_library(void *p_library_handle) {
	if (p_library_handle == RTLD_SELF) {
		return OK;
	}
	return OS_Unix::close_dynamic_library(p_library_handle);
}

Error OS_IOS::get_dynamic_library_symbol_handle(void *p_library_handle, const String &p_name, void *&p_symbol_handle, bool p_optional) {
	if (p_library_handle == RTLD_SELF) {
        // Thread-safe reading to match our thread-safe writing.
        std::lock_guard<std::mutex> lock(dynamic_symbol_mutex);
		void **ptr = OS_IOS::dynamic_symbol_lookup_table.getptr(p_name);
		if (ptr) {
			p_symbol_handle = *ptr;
			return OK;
		}
	}
	return OS_Unix::get_dynamic_library_symbol_handle(p_library_handle, p_name, p_symbol_handle, p_optional);
}

String OS_IOS::get_name() const {
	return "iOS";
}

String OS_IOS::get_distribution_name() const {
	return get_name();
}

String OS_IOS::get_version() const {
	NSOperatingSystemVersion ver = [NSProcessInfo processInfo].operatingSystemVersion;
	return vformat("%d.%d.%d", (int64_t)ver.majorVersion, (int64_t)ver.minorVersion, (int64_t)ver.patchVersion);
}

String OS_IOS::get_model_name() const {
	String model = ios->get_model();
	if (model != "") {
		return model;
	}

	return OS_Unix::get_model_name();
}

Error OS_IOS::shell_open(const String &p_uri) {
	// Guard against null pointers and malformed URI strings
	const char *uri_utf8 = p_uri.utf8().get_data();
	if (!uri_utf8) {
		return ERR_CANT_OPEN;
	}

	NSString *urlPath = [[NSString alloc] initWithUTF8String:uri_utf8];
	if (!urlPath) {
		return ERR_CANT_OPEN;
	}
	
	NSURL *url = [NSURL URLWithString:urlPath];

	if (!url || ![[UIApplication sharedApplication] canOpenURL:url]) {
		return ERR_CANT_OPEN;
	}

	print_verbose(vformat("Opening URL %s", p_uri));

	[[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];

	return OK;
}

String OS_IOS::get_user_data_dir(const String &p_user_dir) const {
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if (paths && [paths count] >= 1) {
            NSString *first_path = [paths firstObject];
            if (first_path) {
                const char *path_utf8 = [first_path UTF8String];
                if (path_utf8) {
                    return String::utf8(path_utf8);
                }
            }
        }
    }
    return "";
}

String OS_IOS::get_cache_path() const {
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        if (paths && [paths count] >= 1) {
            NSString *first_path = [paths firstObject];
            if (first_path) {
                const char *path_utf8 = [first_path UTF8String];
                if (path_utf8) {
                    return String::utf8(path_utf8);
                }
            }
        }
    }
    return "";
}

String OS_IOS::get_temp_path() const {
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
        if (url && url.path) {
            const char *path_utf8 = [url.path UTF8String];
            if (path_utf8) {
                String ret = String::utf8(path_utf8);
                return ret.trim_prefix("file://");
            }
        }
    }
    return "";
}

String OS_IOS::get_locale() const {
    @autoreleasepool {
        NSString *preferredLanguage = [NSLocale preferredLanguages].firstObject;
        if (preferredLanguage) {
            const char *lang_utf8 = [preferredLanguage UTF8String];
            if (lang_utf8) {
                return String::utf8(lang_utf8).replace("-", "_");
            }
        }

        NSString *localeIdentifier = [[NSLocale currentLocale] localeIdentifier];
        if (localeIdentifier) {
            const char *loc_utf8 = [localeIdentifier UTF8String];
            if (loc_utf8) {
                return String::utf8(loc_utf8).replace("-", "_");
            }
        }
    }
	return "en";
}

String OS_IOS::get_unique_id() const {
    @autoreleasepool {
        // identifierForVendor can return nil depending on device lock state and MDM profiles.
        NSUUID *uuid = [UIDevice currentDevice].identifierForVendor;
        if (uuid && uuid.UUIDString) {
            const char *uuid_utf8 = [uuid.UUIDString UTF8String];
            if (uuid_utf8) {
                return String::utf8(uuid_utf8);
            }
        }
    }
	return "";
}

String OS_IOS::get_processor_name() const {
	char buffer[256];
	size_t buffer_len = 256;
	if (sysctlbyname("machdep.cpu.brand_string", &buffer, &buffer_len, nullptr, 0) == 0) {
        // sysctlbyname modifies buffer_len to the exact length including the null terminator.
        // We ensure we don't accidentally parse the null terminator as part of the string payload.
        size_t string_len = (buffer_len > 0 && buffer[buffer_len - 1] == '\0') ? buffer_len - 1 : buffer_len;
		return String::utf8(buffer, string_len);
	}
	ERR_FAIL_V_MSG("", String("Couldn't get the CPU model name. Returning an empty string."));
}

Vector<String> OS_IOS::get_system_fonts() const {
	HashSet<String> font_names;
	CFArrayRef fonts = CTFontManagerCopyAvailableFontFamilyNames();
	if (fonts) {
		for (CFIndex i = 0; i < CFArrayGetCount(fonts); i++) {
			@autoreleasepool {
				CFStringRef cf_name = (CFStringRef)CFArrayGetValueAtIndex(fonts, i);
				if (cf_name && (CFStringGetLength(cf_name) > 0) && (CFStringCompare(cf_name, CFSTR("LastResort"), kCFCompareCaseInsensitive) != kCFCompareEqualTo) && (CFStringGetCharacterAtIndex(cf_name, 0) != '.')) {
					NSString *ns_name = (__bridge NSString *)cf_name;
					const char *utf8_name = [ns_name UTF8String];
					if (utf8_name) {
						font_names.insert(String::utf8(utf8_name));
					}
				}
			}
		}
		CFRelease(fonts);
	}

	Vector<String> ret;
	for (const String &E : font_names) {
		ret.push_back(E);
	}
	return ret;
}

String OS_IOS::_get_default_fontname(const String &p_font_name) const {
	String font_name = p_font_name;
	if (font_name.to_lower() == "sans-serif") {
		font_name = "Helvetica";
	} else if (font_name.to_lower() == "serif") {
		font_name = "Times";
	} else if (font_name.to_lower() == "monospace") {
		font_name = "Courier";
	} else if (font_name.to_lower() == "fantasy") {
		font_name = "Papyrus";
	} else if (font_name.to_lower() == "cursive") {
		font_name = "Apple Chancery";
	};
	return font_name;
}

CGFloat OS_IOS::_weight_to_ct(int p_weight) const {
	if (p_weight < 150) {
		return -0.80;
	} else if (p_weight < 250) {
		return -0.60;
	} else if (p_weight < 350) {
		return -0.40;
	} else if (p_weight < 450) {
		return 0.0;
	} else if (p_weight < 550) {
		return 0.23;
	} else if (p_weight < 650) {
		return 0.30;
	} else if (p_weight < 750) {
		return 0.40;
	} else if (p_weight < 850) {
		return 0.56;
	} else if (p_weight < 925) {
		return 0.62;
	} else {
		return 1.00;
	}
}

CGFloat OS_IOS::_stretch_to_ct(int p_stretch) const {
	if (p_stretch < 56) {
		return -0.5;
	} else if (p_stretch < 69) {
		return -0.37;
	} else if (p_stretch < 81) {
		return -0.25;
	} else if (p_stretch < 93) {
		return -0.13;
	} else if (p_stretch < 106) {
		return 0.0;
	} else if (p_stretch < 137) {
		return 0.13;
	} else if (p_stretch < 144) {
		return 0.25;
	} else if (p_stretch < 162) {
		return 0.37;
	} else {
		return 0.5;
	}
}

Vector<String> OS_IOS::get_system_font_path_for_text(const String &p_font_name, const String &p_text, const String &p_locale, const String &p_script, int p_weight, int p_stretch, bool p_italic) const {
	Vector<String> ret;
	String font_name = _get_default_fontname(p_font_name);

	CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, font_name.utf8().get_data(), kCFStringEncodingUTF8);
	CTFontSymbolicTraits traits = 0;
	if (p_weight >= 700) {
		traits |= kCTFontBoldTrait;
	}
	if (p_italic) {
		traits |= kCTFontItalicTrait;
	}
	if (p_stretch < 100) {
		traits |= kCTFontCondensedTrait; 
	} else if (p_stretch > 100) {
		traits |= kCTFontExpandedTrait;
	}

	CFNumberRef sym_traits = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &traits);
	CFMutableDictionaryRef traits_dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, nullptr, nullptr);
	if (traits_dict && sym_traits) {
		CFDictionaryAddValue(traits_dict, kCTFontSymbolicTrait, sym_traits);
	}

	CGFloat weight = _weight_to_ct(p_weight);
	CFNumberRef font_weight = CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &weight);
	if (traits_dict && font_weight) {
		CFDictionaryAddValue(traits_dict, kCTFontWeightTrait, font_weight);
	}

	CGFloat stretch = _stretch_to_ct(p_stretch);
	CFNumberRef font_stretch = CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &stretch);
	if (traits_dict && font_stretch) {
		CFDictionaryAddValue(traits_dict, kCTFontWidthTrait, font_stretch);
	}

	CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, nullptr, nullptr);
	if (attributes && name) {
		CFDictionaryAddValue(attributes, kCTFontFamilyNameAttribute, name);
	}
	if (attributes && traits_dict) {
		CFDictionaryAddValue(attributes, kCTFontTraitsAttribute, traits_dict);
	}

	CTFontDescriptorRef font = attributes ? CTFontDescriptorCreateWithAttributes(attributes) : nullptr;
	if (font) {
		CTFontRef family = CTFontCreateWithFontDescriptor(font, 0, nullptr);
		if (family) {
			CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, p_text.utf8().get_data(), kCFStringEncodingUTF8);
			if (string) {
				CFRange range = CFRangeMake(0, CFStringGetLength(string));
				CTFontRef fallback_family = CTFontCreateForString(family, string, range);
				if (fallback_family) {
					CTFontDescriptorRef fallback_font = CTFontCopyFontDescriptor(fallback_family);
					if (fallback_font) {
						CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(fallback_font, kCTFontURLAttribute);
						if (url) {
							@autoreleasepool {
								NSString *font_path = [NSString stringWithString:[(__bridge NSURL *)url path]];
								const char *utf8_path = [font_path UTF8String];
								if (utf8_path) {
									ret.push_back(String::utf8(utf8_path));
								}
							}
							CFRelease(url);
						}
						CFRelease(fallback_font);
					}
					CFRelease(fallback_family);
				}
				CFRelease(string);
			}
			CFRelease(family);
		}
		CFRelease(font);
	}

	if (attributes) {
		CFRelease(attributes);
	}
	if (traits_dict) {
		CFRelease(traits_dict);
	}
	if (sym_traits) {
		CFRelease(sym_traits);
	}
	if (font_stretch) {
		CFRelease(font_stretch);
	}
	if (font_weight) {
		CFRelease(font_weight);
	}
	if (name) {
		CFRelease(name);
	}

	return ret;
}

String OS_IOS::get_system_font_path(const String &p_font_name, int p_weight, int p_stretch, bool p_italic) const {
	String ret;
	String font_name = _get_default_fontname(p_font_name);

	CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, font_name.utf8().get_data(), kCFStringEncodingUTF8);

	CTFontSymbolicTraits traits = 0;
	if (p_weight >= 700) {
		traits |= kCTFontBoldTrait;
	}
	if (p_italic) {
		traits |= kCTFontItalicTrait;
	}
	if (p_stretch < 100) {
		traits |= kCTFontCondensedTrait; 
	} else if (p_stretch > 100) {
		traits |= kCTFontExpandedTrait;
	}

	CFNumberRef sym_traits = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &traits);
	CFMutableDictionaryRef traits_dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, nullptr, nullptr);
	if (traits_dict && sym_traits) {
		CFDictionaryAddValue(traits_dict, kCTFontSymbolicTrait, sym_traits);
	}

	CGFloat weight = _weight_to_ct(p_weight);
	CFNumberRef font_weight = CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &weight);
	if (traits_dict && font_weight) {
		CFDictionaryAddValue(traits_dict, kCTFontWeightTrait, font_weight);
	}

	CGFloat stretch = _stretch_to_ct(p_stretch);
	CFNumberRef font_stretch = CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &stretch);
	if (traits_dict && font_stretch) {
		CFDictionaryAddValue(traits_dict, kCTFontWidthTrait, font_stretch);
	}

	CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, nullptr, nullptr);
	if (attributes && name) {
		CFDictionaryAddValue(attributes, kCTFontFamilyNameAttribute, name);
	}
	if (attributes && traits_dict) {
		CFDictionaryAddValue(attributes, kCTFontTraitsAttribute, traits_dict);
	}

	CTFontDescriptorRef font = attributes ? CTFontDescriptorCreateWithAttributes(attributes) : nullptr;
	if (font) {
		CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(font, kCTFontURLAttribute);
		if (url) {
			@autoreleasepool {
				NSString *font_path = [NSString stringWithString:[(__bridge NSURL *)url path]];
				const char *utf8_path = [font_path UTF8String];
				if (utf8_path) {
					ret = String::utf8(utf8_path);
				}
			}
			CFRelease(url);
		}
		CFRelease(font);
	}

	if (attributes) {
		CFRelease(attributes);
	}
	if (traits_dict) {
		CFRelease(traits_dict);
	}
	if (sym_traits) {
		CFRelease(sym_traits);
	}
	if (font_stretch) {
		CFRelease(font_stretch);
	}
	if (font_weight) {
		CFRelease(font_weight);
	}
	if (name) {
		CFRelease(name);
	}

	return ret;
}

void OS_IOS::vibrate_handheld(int p_duration_ms, float p_amplitude) {
	if (ios->supports_haptic_engine()) {
		if (p_amplitude > 0.0) {
			p_amplitude = CLAMP(p_amplitude, 0.0, 1.0);
		}

		ios->vibrate_haptic_engine((float)p_duration_ms / 1000.f, p_amplitude);
	} else {
		// iOS <13 does not support duration for vibration
		AudioServicesPlaySystemSound(kSystemSoundID_Vibrate);
	}
}

bool OS_IOS::_check_internal_feature_support(const String &p_feature) {
	if (p_feature == "system_fonts") {
		return true;
	}
	if (p_feature == "mobile") {
		return true;
	}

	return false;
}

void OS_IOS::on_focus_out() {
	if (is_focused) {
		is_focused = false;

		if (DisplayServerIOS::get_singleton()) {
			DisplayServerIOS::get_singleton()->send_window_event(DisplayServer::WINDOW_EVENT_FOCUS_OUT);
		}

		if (OS::get_singleton()->get_main_loop()) {
			OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
		}

		[AppDelegate.viewController.godotView stopRendering];

		audio_driver.stop();
	}
}

void OS_IOS::on_focus_in() {
	if (!is_focused) {
		is_focused = true;

		if (DisplayServerIOS::get_singleton()) {
			DisplayServerIOS::get_singleton()->send_window_event(DisplayServer::WINDOW_EVENT_FOCUS_IN);
		}

		if (OS::get_singleton()->get_main_loop()) {
			OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
		}

		[AppDelegate.viewController.godotView startRendering];

		audio_driver.start();
	}
}

void OS_IOS::on_enter_background() {
	// Do not check for is_focused, because on_focus_out will always be fired first by applicationWillResignActive.

	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_PAUSED);
	}

	on_focus_out();
}

void OS_IOS::on_exit_background() {
	// Removed the focus in code here.
	// iOS is transitioning the app to the foreground, but it is not active yet.
    // Resuming graphics rendering here violates the iOS lifecycle and will crash.
	if (OS::get_singleton()->get_main_loop()) {
		OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_RESUMED);
	}
}

Rect2 OS_IOS::calculate_boot_screen_rect(const Size2 &p_window_size, const Size2 &p_imgrect_size) const {
	String scalemodestr = GLOBAL_GET("ios/launch_screen_image_mode");

	if (scalemodestr == "scaleAspectFit") {
		return fit_keep_aspect_centered(p_window_size, p_imgrect_size);
	} else if (scalemodestr == "scaleAspectFill") {
		return fit_keep_aspect_covered(p_window_size, p_imgrect_size);
	} else if (scalemodestr == "scaleToFill") {
		return Rect2(Point2(), p_window_size);
	} else if (scalemodestr == "center") {
		return OS_Unix::calculate_boot_screen_rect(p_window_size, p_imgrect_size);
	} else {
		WARN_PRINT(vformat("Boot screen scale mode mismatch between iOS and Godot: %s not supported", scalemodestr));
		return OS_Unix::calculate_boot_screen_rect(p_window_size, p_imgrect_size);
	}
}

#endif // IOS_ENABLED
