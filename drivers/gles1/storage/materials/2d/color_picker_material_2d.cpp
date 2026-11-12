/**************************************************************************/
/*  color_picker_material_2d.cpp                                          */
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

#include "core/math/color.h"
#include "core/math/math_funcs.h"
#include "core/object/object.h"
#include "core/templates/vector.h"

#include "color_picker_material_2d.h"

#include "drivers/gles1/storage/config.h"
#include "drivers/gles_common/error_macros.h"
#include "drivers/gles_common/common_defines.h"

namespace GLES1 {

ColorPickerMaterialState::Mode ColorPickerMaterialProcessor::detect_shader_mode(const String &p_code) {
	if (p_code.find("uniform float wheel_radius") != -1) {
		return ColorPickerMaterialState::MODE_WHEEL;
	} else if (p_code.find("uniform float ok_hsl_l") != -1 || p_code.find("okhsl_to_srgb") != -1) {
		return ColorPickerMaterialState::MODE_CIRCLE_OKHSL;
	} else if (p_code.find("// ColorPicker circle shader") != -1 || p_code.find("uniform float v = 1.0;") != -1) {
		return ColorPickerMaterialState::MODE_CIRCLE_VHS;
	}
	return ColorPickerMaterialState::MODE_NONE;
}

void ColorPickerMaterialProcessor::update_state(ColorPickerMaterialState &r_state, ColorPickerMaterialState::Mode p_mode, const HashMap<StringName, Variant> &p_parameters) {
	r_state.mode = p_mode;

	if (p_parameters.has("wheel_radius")) {
		r_state.wheel_radius = static_cast<float>(p_parameters["wheel_radius"]);
	} else {
		r_state.wheel_radius = 0.42f;
	}

	if (p_parameters.has("v")) {
		r_state.v = static_cast<float>(p_parameters["v"]);
	} else {
		r_state.v = 1.0f;
	}

	if (p_parameters.has("ok_hsl_l")) {
		r_state.ok_hsl_l = static_cast<float>(p_parameters["ok_hsl_l"]);
	} else {
		r_state.ok_hsl_l = 1.0f;
	}
}

void ColorPickerMaterialProcessor::_generate_texture(ColorPickerMaterialState &r_state) {
	constexpr int TEX_SIZE = 256;
	const int size = (GLES1_CONFIG->max_texture_size > 0) ? MIN(TEX_SIZE, (int)GLES1_CONFIG->max_texture_size) : TEX_SIZE;
	ERR_FAIL_COND(size <= 0);

	Vector<uint8_t> pixels;
	pixels.resize(size * size * 4);
	uint8_t *ptr = pixels.ptrw();
	ERR_FAIL_NULL(ptr);

	const float inv_size = 1.0f / static_cast<float>(size);

	// Manually recreate the shader math.
	for (int iy = 0; iy < size; iy++) {
		const float uv_y = (static_cast<float>(iy) + 0.5f) * inv_size;
		for (int ix = 0; ix < size; ix++) {
			const float uv_x = (static_cast<float>(ix) + 0.5f) * inv_size;

			float x = uv_x - 0.5f;
			float y = uv_y - 0.5f;
			const float a = Math::atan2(y, x);

			float r = 1.0f;
			float g = 1.0f;
			float b = 1.0f;
			float alpha = 0.0f;

			if (r_state.mode == ColorPickerMaterialState::MODE_WHEEL) {
				const float wr = r_state.wheel_radius;

				// Smooth the circular edges of the wheel and circles
				// just like the shader.
				x += 0.001f;
				y += 0.001f;
				float d1 = Math::sqrt(x * x + y * y);
				float b1 = (d1 < 0.5f && d1 > wr) ? 1.0f : 0.0f;

				x -= 0.002f;
				float d2 = Math::sqrt(x * x + y * y);
				float b2 = (d2 < 0.5f && d2 > wr) ? 1.0f : 0.0f;

				y -= 0.002f;
				float d3 = Math::sqrt(x * x + y * y);
				float b3 = (d3 < 0.5f && d3 > wr) ? 1.0f : 0.0f;

				x += 0.002f;
				float d4 = Math::sqrt(x * x + y * y);
				float b4 = (d4 < 0.5f && d4 > wr) ? 1.0f : 0.0f;

				alpha = (b1 + b2 + b3 + b4) * 0.25f;

				const float base = (a - static_cast<float>(Math_TAU)) / static_cast<float>(Math_TAU);
				const float fr = Math::fposmod(base + 1.0f, 1.0f);
				const float fg = Math::fposmod(base + (2.0f / 3.0f), 1.0f);
				const float fb = Math::fposmod(base + (1.0f / 3.0f), 1.0f);

				r = CLAMP(Math::abs(fr * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
				g = CLAMP(Math::abs(fg * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
				b = CLAMP(Math::abs(fb * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
			} else if (r_state.mode == ColorPickerMaterialState::MODE_CIRCLE_VHS) {
				x += 0.001f;
				y += 0.001f;
				float b1 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				x -= 0.002f;
				float b2 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				y -= 0.002f;
				float b3 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				x += 0.002f;
				float d4 = Math::sqrt(x * x + y * y);
				float b4 = (d4 < 0.5f) ? 1.0f : 0.0f;

				alpha = (b1 + b2 + b3 + b4) * 0.25f;

				const float base = (a - static_cast<float>(Math_TAU)) / static_cast<float>(Math_TAU);
				const float fr = Math::fposmod(base + 1.0f, 1.0f);
				const float fg = Math::fposmod(base + (2.0f / 3.0f), 1.0f);
				const float fb = Math::fposmod(base + (1.0f / 3.0f), 1.0f);

				const float hr = CLAMP(Math::abs(fr * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
				const float hg = CLAMP(Math::abs(fg * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
				const float hb = CLAMP(Math::abs(fb * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);

				const float sat = d4 * 2.0f;
				r = Math::lerp(1.0f, hr, sat);
				g = Math::lerp(1.0f, hg, sat);
				b = Math::lerp(1.0f, hb, sat);
			} else if (r_state.mode == ColorPickerMaterialState::MODE_CIRCLE_OKHSL) {
				const float h = Math::fposmod(a / static_cast<float>(Math_TAU), 1.0f);
				const float s = CLAMP(Math::sqrt(x * x + y * y) * 2.0f, 0.0f, 1.0f);

				Color c;
				c.set_ok_hsl(h, s, r_state.ok_hsl_l, 1.0f);
				r = CLAMP(c.r, 0.0f, 1.0f);
				g = CLAMP(c.g, 0.0f, 1.0f);
				b = CLAMP(c.b, 0.0f, 1.0f);

				x += 0.001f;
				y += 0.001f;
				float b1 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				x -= 0.002f;
				float b2 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				y -= 0.002f;
				float b3 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				x += 0.002f;
				float b4 = (Math::sqrt(x * x + y * y) < 0.5f) ? 1.0f : 0.0f;

				alpha = (b1 + b2 + b3 + b4) * 0.25f;
			}

			const int idx = (iy * size + ix) * 4;
			ptr[idx + 0] = static_cast<uint8_t>(CLAMP(r * 255.0f + 0.5f, 0.0f, 255.0f));
			ptr[idx + 1] = static_cast<uint8_t>(CLAMP(g * 255.0f + 0.5f, 0.0f, 255.0f));
			ptr[idx + 2] = static_cast<uint8_t>(CLAMP(b * 255.0f + 0.5f, 0.0f, 255.0f));
			ptr[idx + 3] = static_cast<uint8_t>(CLAMP(alpha * 255.0f + 0.5f, 0.0f, 255.0f));
		}
	}

	const uint32_t current_gen = GLES1_CONFIG->context_generation;
	bool is_new = false;

	if (r_state.tex_id != 0 && r_state.context_generation != current_gen) {
		r_state.tex_id = 0;
	}

	if (r_state.tex_id == 0) {
		glGenTextures(1, &r_state.tex_id);
		GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::_generate_texture: glGenTextures");
		r_state.context_generation = current_gen;
		is_new = true;
	}

	ERR_FAIL_COND(r_state.tex_id == 0);

	if (GLES1_CONFIG->max_texture_units > 1) {
		glActiveTexture(GL_TEXTURE0);
	}
	glBindTexture(GL_TEXTURE_2D, r_state.tex_id);
	GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::_generate_texture: glBindTexture");

	// Apply.
	if (is_new) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, ptr);
		GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::_generate_texture: glTexImage2D");
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, ptr);
		GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::_generate_texture: glTexSubImage2D");
	}
}

void ColorPickerMaterialProcessor::bind_state(ColorPickerMaterialState &r_state) {
	if (r_state.mode == ColorPickerMaterialState::MODE_NONE) {
		return;
	}

	float target_param = 1.0f;
	if (r_state.mode == ColorPickerMaterialState::MODE_WHEEL) {
		target_param = r_state.wheel_radius;
	} else if (r_state.mode == ColorPickerMaterialState::MODE_CIRCLE_OKHSL) {
		target_param = r_state.ok_hsl_l;
	}

	const bool needs_bake = (
		r_state.tex_id == 0 ||
		r_state.context_generation != GLES1_CONFIG->context_generation ||
		r_state.baked_mode != r_state.mode ||
		!Math::is_equal_approx(r_state.baked_param, target_param)
	);

	if (needs_bake) {
		_generate_texture(r_state);
		r_state.baked_mode = r_state.mode;
		r_state.baked_param = target_param;
	}

	if (GLES1_CONFIG->max_texture_units > 1) {
		glActiveTexture(GL_TEXTURE0);
	}
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, r_state.tex_id);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::bind_state: glBindTexture");

	if (r_state.mode == ColorPickerMaterialState::MODE_CIRCLE_VHS) {
		const float val = CLAMP(r_state.v, 0.0f, 1.0f);
		glColor4f(val, val, val, 1.0f);
	} else {
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	}
	GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::bind_state: glColor4f");
}

void ColorPickerMaterialProcessor::free_state(ColorPickerMaterialState &r_state) {
	if (r_state.tex_id != 0) {
		if (r_state.context_generation == GLES1_CONFIG->context_generation) {
			glDeleteTextures(1, &r_state.tex_id);
			GL_CHECK_ERROR("GLES1::ColorPickerMaterialProcessor::free_state: glDeleteTextures");
		}
		r_state.tex_id = 0;
	}
}

} // namespace GLES1

#endif // GLES1_ENABLED
