/**************************************************************************/
/*  post_effects.cpp                                                      */
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

#ifdef GLES1_ENABLED

#include "post_effects.h"
#include "drivers/gles1/storage/texture_storage.h"

using namespace GLES1;

PostEffects *PostEffects::singleton = nullptr;

PostEffects *PostEffects::get_singleton() {
	return singleton;
}

PostEffects::PostEffects() {
	singleton = this;
}

PostEffects::~PostEffects() {
	singleton = nullptr;
}

void PostEffects::_draw_screen_triangle() {

}

void PostEffects::post_copy(
		GLuint p_dest_framebuffer, Size2i p_dest_size, GLuint p_source_color,
		GLuint p_source_depth, bool p_ssao_enabled, int p_ssao_quality_level, float p_ssao_strength, float p_ssao_radius,
		Size2i p_source_size, float p_luminance_multiplier, const Glow::GLOWLEVEL *p_glow_buffers, float p_glow_intensity,
		float p_srgb_white, uint32_t p_view, bool p_use_multiview, uint64_t p_spec_constants) {

}

#endif // GLES1_ENABLED
