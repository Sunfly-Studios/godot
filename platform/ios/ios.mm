/**************************************************************************/
/*  ios.mm                                                                */
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

#import "ios.h"

#import "app_delegate.h"
#import "view_controller.h"

#import <CoreHaptics/CoreHaptics.h>
#import <UIKit/UIKit.h>
#include <sys/sysctl.h>

void iOS::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_rate_url", "app_id"), &iOS::get_rate_url);
	ClassDB::bind_method(D_METHOD("supports_haptic_engine"), &iOS::supports_haptic_engine);
	ClassDB::bind_method(D_METHOD("start_haptic_engine"), &iOS::start_haptic_engine);
	ClassDB::bind_method(D_METHOD("stop_haptic_engine"), &iOS::stop_haptic_engine);
}

bool iOS::supports_haptic_engine() {
	if (@available(iOS 13, *)) {
		id<CHHapticDeviceCapability> capabilities = [CHHapticEngine capabilitiesForHardware];
		return capabilities.supportsHaptics;
	}

	return false;
}

CHHapticEngine *iOS::get_haptic_engine_instance() API_AVAILABLE(ios(13)) {
	if (haptic_engine == nullptr) {
		NSError *error = nullptr;
		haptic_engine = [[CHHapticEngine alloc] initAndReturnError:&error];

		if (!error && haptic_engine) {
			[haptic_engine setAutoShutdownEnabled:true];

			// The Haptic Engine is an external OS service. It may get killed/reset by the OS
			// when backgrounding or under memory pressure.
			haptic_engine.resetHandler = ^{
				NSLog(@"Haptic engine reset by OS. Restarting...");
				NSError *restartError = nil;
				[haptic_engine startAndReturnError:&restartError];
				if (restartError) {
					NSLog(@"Failed to restart haptic engine: %@", restartError);
				}
			};

			haptic_engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
				NSLog(@"Haptic engine stopped. Reason: %zd", reason);
				// Engine needs to be recreated or restarted on next use depending on reason.
			};

		} else {
			haptic_engine = nullptr;
			NSLog(@"Could not initialize haptic engine: %@", error);
		}
	}

	return haptic_engine;
}

void iOS::vibrate_haptic_engine(float p_duration_seconds, float p_amplitude) API_AVAILABLE(ios(13)) {
	if (@available(iOS 13, *)) {
		if (supports_haptic_engine()) {
			CHHapticEngine *cur_haptic_engine = get_haptic_engine_instance();
			if (cur_haptic_engine) {
				@autoreleasepool {
					NSDictionary *hapticDict;
					if (p_amplitude < 0) {
						hapticDict = @{
							CHHapticPatternKeyPattern : @[
								@{CHHapticPatternKeyEvent : @{
									CHHapticPatternKeyEventType : CHHapticEventTypeHapticContinuous,
									CHHapticPatternKeyTime : @(CHHapticTimeImmediate),
									CHHapticPatternKeyEventDuration : @(p_duration_seconds),
								}},
							],
						};
					} else {
						hapticDict = @{
							CHHapticPatternKeyPattern : @[
								@{CHHapticPatternKeyEvent : @{
									CHHapticPatternKeyEventType : CHHapticEventTypeHapticContinuous,
									CHHapticPatternKeyTime : @(CHHapticTimeImmediate),
									CHHapticPatternKeyEventDuration : @(p_duration_seconds),
									CHHapticPatternKeyEventParameters : @[
										@{
											CHHapticPatternKeyParameterID : CHHapticEventParameterIDHapticIntensity,
											CHHapticPatternKeyParameterValue : @(p_amplitude)
										},
									],
								}},
							],
						};
					}

					NSError *error = nil;
					CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithDictionary:hapticDict error:&error];

					if (error) {
						NSLog(@"Could not create haptic pattern: %@", error);
						return;
					}

					id<CHHapticPatternPlayer> player = [cur_haptic_engine createPlayerWithPattern:pattern error:&error];
					if (error) {
						NSLog(@"Could not create haptic player: %@", error);
						return;
					}
					
					[player startAtTime:0 error:&error];
					if (error) {
						NSLog(@"Could not start haptic vibration: %@", error);
					}
				}
			}
			return;
		}
	}

	NSLog(@"Haptic engine is not supported in this version of iOS");
}

void iOS::start_haptic_engine() {
	if (@available(iOS 13, *)) {
		if (supports_haptic_engine()) {
			CHHapticEngine *cur_haptic_engine = get_haptic_engine_instance();
			if (cur_haptic_engine) {
				[cur_haptic_engine startWithCompletionHandler:^(NSError *returnedError) {
					if (returnedError) {
						NSLog(@"Could not start haptic engine: %@", returnedError);
					}
				}];
			}

			return;
		}
	}

	NSLog(@"Haptic engine is not supported in this version of iOS");
}

void iOS::stop_haptic_engine() {
	if (@available(iOS 13, *)) {
		if (supports_haptic_engine()) {
			CHHapticEngine *cur_haptic_engine = get_haptic_engine_instance();
			if (cur_haptic_engine) {
				[cur_haptic_engine stopWithCompletionHandler:^(NSError *returnedError) {
					if (returnedError) {
						NSLog(@"Could not stop haptic engine: %@", returnedError);
					}
				}];
			}

			return;
		}
	}

	NSLog(@"Haptic engine is not supported in this version of iOS");
}

void iOS::alert(const char *p_alert, const char *p_title) {
	NSString *title = p_title ? [NSString stringWithUTF8String:p_title] : @"";
	NSString *message = p_alert ? [NSString stringWithUTF8String:p_alert] : @"";

	// Enforce main-thread execution. OS.alert() can be called from background Godot threads,
	// which will cause a fatal Main Thread Checker crash if we touch UIAlertController directly.
	dispatch_async(dispatch_get_main_queue(), ^{
		UIAlertController *alert = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
		UIAlertAction *button = [UIAlertAction actionWithTitle:@"OK"
														 style:UIAlertActionStyleCancel
													   handler:^(id){
													   }];

		[alert addAction:button];

		// Ensure we actually have a view controller before attempting to present.
		if (AppDelegate.viewController) {
			[AppDelegate.viewController presentViewController:alert animated:YES completion:nil];
		} else {
			NSLog(@"Failed to show alert: AppDelegate.viewController is nil.");
		}
	});
}

String iOS::get_model() const {
	// [[UIDevice currentDevice] model] only returns "iPad" or "iPhone".
	size_t size;
	sysctlbyname("hw.machine", nullptr, &size, nullptr, 0);
	
	char *model = (char *)malloc(size);
	if (model == nullptr) {
		return "";
	}
	
	sysctlbyname("hw.machine", model, &size, nullptr, 0);
	
	String result = "";
	@autoreleasepool {
		NSString *platform = [NSString stringWithCString:model encoding:NSUTF8StringEncoding];
		if (platform) {
			const char *str = [platform UTF8String];
			result = String::utf8(str != nullptr ? str : "");
		}
	}
	
	free(model);
	return result;
}

String iOS::get_rate_url(int p_app_id) const {
	String app_url_path = "itms-apps://itunes.apple.com/app/idAPP_ID";

	String ret = app_url_path.replace("APP_ID", String::num_int64(p_app_id));

	print_verbose(vformat("Returning rate url %s", ret));
	return ret;
}

iOS::iOS() {}
