/**************************************************************************/
/*  copy_effects.cpp                                                      */
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

#include "copy_effects.h"
#include "drivers/gles1/storage/utilities.h"
#include "drivers/gles1/storage/texture_storage.h"
#include "drivers/gles_common/error_macros.h"

using namespace GLES1;

CopyEffects *CopyEffects::singleton = nullptr;

CopyEffects *CopyEffects::get_singleton() {
	return singleton;
}

static constexpr float screen_triangle_verts[12] = {
	// Pos            // UV
	-1.0f,  -1.0f,    0.0f, 0.0f,
	 3.0f,  -1.0f,    2.0f, 0.0f,
	-1.0f,   3.0f,    0.0f, 2.0f,
};

static constexpr float screen_quad_verts[24] = {
	-1.0f, -1.0f,  0.0f, 0.0f,
	 1.0f, -1.0f,  1.0f, 0.0f,
	 1.0f,  1.0f,  1.0f, 1.0f,
	-1.0f, -1.0f,  0.0f, 0.0f,
	 1.0f,  1.0f,  1.0f, 1.0f,
	-1.0f,  1.0f,  0.0f, 1.0f,
};

CopyEffects::CopyEffects() {
	singleton = this;

	copy.shader.initialize();

	copy.shader_version = copy.shader.version_create();
	copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_DEFAULT);

	// Screen Triangle VBO
	glGenBuffers(1, &screen_triangle);
	if (screen_triangle != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
		GL_CHECK_ERROR("GLES1::CopyEffects setup: bind screen_triangle");

		GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, screen_triangle, sizeof(float) * 12, screen_triangle_verts, GL_STATIC_DRAW, "CopyEffects Screen Triangle");
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES1::CopyEffects setup: buffer data screen_triangle");
	} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
		WARN_PRINT("GLES1: Failed to generate screen_triangle VBO. Using client memory fallback.");
#endif
	}

	// Screen Quad VBO
	glGenBuffers(1, &quad);
	if (quad != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, quad);
		GL_CHECK_ERROR("GLES1::CopyEffects setup: bind screen_quad");
		
		GLES1::Utilities::get_singleton()->buffer_allocate_data(GL_ARRAY_BUFFER, quad, sizeof(float) * 24, screen_quad_verts, GL_STATIC_DRAW, "CopyEffects Screen Quad");
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES1::CopyEffects setup: buffer data screen_quad");
	} else {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
		WARN_PRINT("GLES1: Failed to generate screen_quad VBO. Using client memory fallback.");
#endif
	}
}

CopyEffects::~CopyEffects() {
	singleton = nullptr;

	copy.shader.version_free(copy.shader_version);

	if (screen_triangle != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(screen_triangle);
	}
	if (quad != 0) {
		GLES1::Utilities::get_singleton()->buffer_free_data(quad);
	}
}

void CopyEffects::set_color(const Color &p_color, const Rect2i &p_region) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_SIMPLE_COLOR);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::set_color");

	copy.shader.version_set_uniform(CopyShaderGLES1::COPY_SECTION, p_region.position.x, p_region.position.y, p_region.size.x, p_region.size.y, copy.shader_version, CopyShaderGLES1::MODE_SIMPLE_COLOR);
	copy.shader.version_set_uniform(CopyShaderGLES1::COLOR_IN, p_color, copy.shader_version, CopyShaderGLES1::MODE_SIMPLE_COLOR);

	draw_screen_quad();
}

void CopyEffects::draw_screen_triangle() {
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	if (likely(screen_triangle != 0)) {
		// Set pointers of 4 floats (2 pos, 2 uv)
		glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
		glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, nullptr);
		glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, (const void *)(uintptr_t)(sizeof(float) * 2));
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, screen_triangle_verts);
		glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, screen_triangle_verts + 2);
	}
	GL_CHECK_ERROR("CopyEffects::draw_screen_triangle: state setup");
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	// Draw the triangle
	glDrawArrays(GL_TRIANGLES, 0, 3);
	GL_CHECK_ERROR("CopyEffects::draw_screen_triangle: glDrawArrays");

	// Clean up state
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	// Stop matrix bleed from source_section
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	GL_CHECK_ERROR("CopyEffects::draw_screen_triangle: cleanup");
}

void CopyEffects::draw_screen_quad() {
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	if (likely(quad != 0)) {
		glBindBuffer(GL_ARRAY_BUFFER, quad);
		// Position (2 floats, stride of 4 floats)
		glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, nullptr);
		// UV (2 floats, stride of 4 floats, offset by 2 floats)
		glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, (const void *)(uintptr_t)(sizeof(float) * 2));
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, screen_quad_verts);
		glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, screen_quad_verts + 2);
	}
	GL_CHECK_ERROR("CopyEffects::draw_screen_quad: state setup");
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glDrawArrays(GL_TRIANGLES, 0, 6);
	GL_CHECK_ERROR("CopyEffects::draw_screen_quad: glDrawArrays");

	// Clean up state
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	// Stop matrix bleed from source_section
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	GL_CHECK_ERROR("CopyEffects::draw_screen_quad: cleanup");
}

void CopyEffects::copy_to_rect(const Rect2 &p_rect) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_COPY_SECTION);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_to_rect");

	copy.shader.version_set_uniform(CopyShaderGLES1::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES1::MODE_COPY_SECTION);

	draw_screen_quad();
	GL_CHECK_ERROR("CopyEffects::copy_to_rect");
}

void CopyEffects::copy_to_rect_3d(const Rect2 &p_rect, float p_layer, int p_type, float p_lod) {
	ERR_FAIL_COND(p_type != Texture::TYPE_LAYERED && p_type != Texture::TYPE_3D);

	if (unlikely(!GLES1::Config::get_singleton()->support_3d_textures)) {
		ERR_PRINT_ONCE("GLES1: 3D and Layered texture copying is not supported on this hardware. Aborting copy_to_rect_3d.");
		return;
	}

	CopyShaderGLES1::ShaderVariant variant = p_type == Texture::TYPE_LAYERED ? CopyShaderGLES1::MODE_COPY_SECTION_2D_ARRAY : CopyShaderGLES1::MODE_COPY_SECTION_3D;

	bool success = copy.shader.version_bind_shader(copy.shader_version, variant);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_to_rect_3d");

	copy.shader.version_set_uniform(CopyShaderGLES1::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, variant);
	copy.shader.version_set_uniform(CopyShaderGLES1::LAYER, p_layer, copy.shader_version, variant);
	copy.shader.version_set_uniform(CopyShaderGLES1::LOD, p_lod, copy.shader_version, variant);

	draw_screen_quad();
	GL_CHECK_ERROR("CopyEffects::copy_to_rect_3d");
}

void CopyEffects::copy_to_and_from_rect(const Rect2 &p_rect) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_COPY_SECTION_SOURCE);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_to_and_from_rect");

	copy.shader.version_set_uniform(CopyShaderGLES1::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES1::MODE_COPY_SECTION_SOURCE);
	copy.shader.version_set_uniform(CopyShaderGLES1::SOURCE_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES1::MODE_COPY_SECTION_SOURCE);

	draw_screen_quad();
	GL_CHECK_ERROR("CopyEffects::copy_to_and_from_rect");
}

void CopyEffects::copy_screen(float p_multiply) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_SCREEN);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_screen");

	copy.shader.version_set_uniform(CopyShaderGLES1::MULTIPLY, p_multiply, copy.shader_version, CopyShaderGLES1::MODE_SCREEN);

	draw_screen_triangle();
	GL_CHECK_ERROR("CopyEffects::copy_screen");
}

void CopyEffects::copy_cube_to_rect(const Rect2 &p_rect) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_CUBE_TO_OCTAHEDRAL);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_cube_to_rect");

	copy.shader.version_set_uniform(CopyShaderGLES1::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES1::MODE_CUBE_TO_OCTAHEDRAL);
	draw_screen_quad();
	GL_CHECK_ERROR("CopyEffects::copy_cube_to_rect");
}

void CopyEffects::copy_cube_to_panorama(float p_mip_level) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES1::MODE_CUBE_TO_PANORAMA);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_cube_to_panorama");

	copy.shader.version_set_uniform(CopyShaderGLES1::MIP_LEVEL, p_mip_level, copy.shader_version, CopyShaderGLES1::MODE_CUBE_TO_PANORAMA);
	draw_screen_quad();
	GL_CHECK_ERROR("CopyEffects::copy_cube_to_panorama");
}

// Stubs for mipmapping/blur effects (GLES1 has no support for native framebuffer blit)
void CopyEffects::bilinear_blur(GLuint p_source_texture, int p_mipmap_count, const Rect2i &p_region) {

}

void CopyEffects::gaussian_blur(GLuint p_source_texture, int p_mipmap_count, const Rect2i &p_region, const Size2i &p_size) {

}
#endif // GLES1_ENABLED
