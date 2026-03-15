/**************************************************************************/
/*  utilities.cpp                                                         */
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

#ifdef GLES2_ENABLED

#include "utilities.h"

#include "drivers/gles2/rasterizer_gles2.h"
#include "config.h"
#include "light_storage.h"
#include "material_storage.h"
#include "mesh_storage.h"
#include "particles_storage.h"
#include "texture_storage.h"

using namespace GLES2;

Utilities *Utilities::singleton = nullptr;

Utilities::Utilities() {
	singleton = this;
}

Utilities::~Utilities() {
	singleton = nullptr;
}

Vector<uint8_t> Utilities::buffer_get_data(GLenum p_target, GLuint p_buffer, uint32_t p_buffer_size) {
	return Vector<uint8_t>();
}

/* INSTANCES */

RS::InstanceType Utilities::get_base_type(RID p_rid) const {
	return RS::INSTANCE_NONE;
}

bool Utilities::free(RID p_rid) {
    return false;
}

/* DEPENDENCIES */

void Utilities::base_update_dependency(RID p_base, DependencyTracker *p_instance) {

}

/* VISIBILITY NOTIFIER */

RID Utilities::visibility_notifier_allocate() {
    return RID();
}

void Utilities::visibility_notifier_initialize(RID p_notifier) {

}

void Utilities::visibility_notifier_free(RID p_notifier) {

}

void Utilities::visibility_notifier_set_aabb(RID p_notifier, const AABB &p_aabb) {

}

void Utilities::visibility_notifier_set_callbacks(RID p_notifier, const Callable &p_enter_callbable, const Callable &p_exit_callable) {

}

AABB Utilities::visibility_notifier_get_aabb(RID p_notifier) const {
    return AABB();
}

void Utilities::visibility_notifier_call(RID p_notifier, bool p_enter, bool p_deferred) {

}

/* TIMING */

void Utilities::capture_timestamps_begin() {

}

void Utilities::capture_timestamp(const String &p_name) {

}

void Utilities::_capture_timestamps_begin() {

}

void Utilities::capture_timestamps_end() {

}

uint32_t Utilities::get_captured_timestamps_count() const {
    return 0;
}

uint64_t Utilities::get_captured_timestamps_frame() const {
    return 0;
}

uint64_t Utilities::get_captured_timestamp_gpu_time(uint32_t p_index) const {
    return 0;
}

uint64_t Utilities::get_captured_timestamp_cpu_time(uint32_t p_index) const {
    return 0;
}

String Utilities::get_captured_timestamp_name(uint32_t p_index) const {
    return String();
}

/* MISC */

void Utilities::update_dirty_resources() {

}

void Utilities::set_debug_generate_wireframes(bool p_generate) {

}

bool Utilities::has_os_feature(const String &p_feature) const {
	return false;
}

void Utilities::update_memory_info() {
}

uint64_t Utilities::get_rendering_info(RS::RenderingInfo p_info) {
	return 0;
}

String Utilities::get_video_adapter_name() const {
    return String();
}

String Utilities::get_video_adapter_vendor() const {
	return String();
}

RenderingDevice::DeviceType Utilities::get_video_adapter_type() const {
	return RenderingDevice::DeviceType::DEVICE_TYPE_OTHER;
}

String Utilities::get_video_adapter_api_version() const {
	return String();
}

Size2i Utilities::get_maximum_viewport_size() const {
    return Size2i();
}

uint32_t Utilities::get_maximum_shader_varyings() const {
    return 31;
}

uint64_t Utilities::get_maximum_uniform_buffer_size() const {
    return 65536;
}

#endif // GLES2_ENABLED
