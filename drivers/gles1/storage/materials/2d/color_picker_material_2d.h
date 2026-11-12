/**************************************************************************/
/*  color_picker_material_2d.h                                            */
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

#ifndef COLOR_PICKER_MATERIAL_2D_H
#define COLOR_PICKER_MATERIAL_2D_H

#ifdef GLES1_ENABLED

#include "core/templates/hash_map.h"
#include "core/variant/variant.h"
#include "platform_gl.h"

namespace GLES1 {

struct ColorPickerMaterialState {
	enum Mode {
		MODE_NONE = 0,
		MODE_WHEEL,
		MODE_CIRCLE_VHS,
		MODE_CIRCLE_OKHSL
	};

	Mode mode = MODE_NONE;
	float wheel_radius = 0.42f;
	float v = 1.0f;
	float ok_hsl_l = 1.0f;

	GLuint tex_id = 0;
	uint32_t context_generation = 0;
	float baked_param = -1.0f;
	Mode baked_mode = MODE_NONE;
};

class ColorPickerMaterialProcessor {
private:
	static void _generate_texture(ColorPickerMaterialState &r_state);

public:
	static ColorPickerMaterialState::Mode detect_shader_mode(const String &p_code);
	static void update_state(ColorPickerMaterialState &r_state, ColorPickerMaterialState::Mode p_mode, const HashMap<StringName, Variant> &p_parameters);
	static void bind_state(ColorPickerMaterialState &r_state);
	static void free_state(ColorPickerMaterialState &r_state);
};

} // namespace GLES1

#endif // GLES1_ENABLED

#endif // COLOR_PICKER_MATERIAL_2D_H
