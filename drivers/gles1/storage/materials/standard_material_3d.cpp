/**************************************************************************/
/*  standard_material_3d.cpp                                              */
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

#include "standard_material_3d.h"
#include "drivers/gles_common/error_macros.h"

namespace GLES1 {

void StandardMaterialProcessor::update_state(StandardMaterialState &r_state, const HashMap<StringName, Variant> &p_parameters) {
	if (p_parameters.has("albedo")) {
		Color albedo = p_parameters["albedo"];
		r_state.albedo[0] = albedo.r;
		r_state.albedo[1] = albedo.g;
		r_state.albedo[2] = albedo.b;
		r_state.albedo[3] = albedo.a;
	} else {
		r_state.albedo[0] = 1.0f;
		r_state.albedo[1] = 1.0f;
		r_state.albedo[2] = 1.0f;
		r_state.albedo[3] = 1.0f;
	}

	if (p_parameters.has("emission")) {
		Color emission = p_parameters["emission"];
		float energy = 1.0f;
		if (p_parameters.has("emission_energy_multiplier")) {
			energy = static_cast<float>(p_parameters["emission_energy_multiplier"]);
		} else if (p_parameters.has("emission_energy")) {
			energy = static_cast<float>(p_parameters["emission_energy"]);
		}
		r_state.emission[0] = emission.r * energy;
		r_state.emission[1] = emission.g * energy;
		r_state.emission[2] = emission.b * energy;
		r_state.emission[3] = 1.0f;
	} else {
		r_state.emission[0] = 0.0f;
		r_state.emission[1] = 0.0f;
		r_state.emission[2] = 0.0f;
		r_state.emission[3] = 1.0f;
	}

	float roughness = p_parameters.has("roughness") ? (float)p_parameters["roughness"] : 1.0f;
	float metallic = p_parameters.has("metallic") ? (float)p_parameters["metallic"] : 0.0f;

	// Approximate specular and shininess
	float spec_intensity = metallic * (1.0f - roughness);
	r_state.specular[0] = r_state.albedo[0] * spec_intensity;
	r_state.specular[1] = r_state.albedo[1] * spec_intensity;
	r_state.specular[2] = r_state.albedo[2] * spec_intensity;
	r_state.specular[3] = 1.0f;

	// Invert roughness for GL_SHININESS (0.0 to 128.0)
	r_state.shininess = CLAMP((1.0f - roughness) * 128.0f, 0.0f, 128.0f);
}

void StandardMaterialProcessor::bind_state(const StandardMaterialState &p_state) {
	glColor4f(p_state.albedo[0], p_state.albedo[1], p_state.albedo[2], p_state.albedo[3]);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, p_state.albedo);
	GL_CHECK_ERROR("GLES1::StandardMaterialProcessor::bind_state: glMaterialfv ALBEDO");

	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, p_state.emission);
	GL_CHECK_ERROR("GLES1::StandardMaterialProcessor::bind_state: glMaterialfv EMISSION");

	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, p_state.specular);
	GL_CHECK_ERROR("GLES1::StandardMaterialProcessor::bind_state: glMaterialfv SPECULAR");

	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, p_state.shininess);
	GL_CHECK_ERROR("GLES1::StandardMaterialProcessor::bind_state: glMaterialf SHININESS");
}

} // namespace GLES1

#endif // GLES1_ENABLED
