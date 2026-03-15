/**************************************************************************/
/*  cubemap_filter.cpp                                                    */
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

#include "cubemap_filter.h"

#include "drivers/gles1/storage/texture_storage.h"
#include "core/config/project_settings.h"

using namespace GLES1;

CubemapFilter *CubemapFilter::singleton = nullptr;

CubemapFilter::CubemapFilter() {
	singleton = this;
}

CubemapFilter::~CubemapFilter() {
	singleton = nullptr;
}

// Helper functions for IBL filtering

[[maybe_unused]] static Vector3 importance_sample_GGX(Vector2 xi, float roughness4) {
    return Vector3();
}

[[maybe_unused]] static float distribution_GGX(float NdotH, float roughness4) {
    return 0.0f;
}

[[maybe_unused]] static float radical_inverse_vdC(uint32_t bits) {
    return 0.0f;
}

[[maybe_unused]] static Vector2 hammersley(uint32_t i, uint32_t N) {
    return Vector2();
}

void CubemapFilter::filter_radiance(GLuint p_source_cubemap, GLuint p_dest_cubemap, GLuint p_dest_framebuffer, int p_source_size, int p_mipmap_count, int p_layer) {
    
}

#endif // GLES1_ENABLED
