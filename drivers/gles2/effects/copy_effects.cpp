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

#ifdef GLES2_ENABLED

#include "copy_effects.h"
#include "drivers/gles2/storage/texture_storage.h"

using namespace GLES2;

CopyEffects *CopyEffects::singleton = nullptr;

CopyEffects *CopyEffects::get_singleton() {
	return singleton;
}

CopyEffects::CopyEffects() {
	singleton = this;

	copy.shader.initialize();

	// ERR_FAIL_COND(copy.shader.is_null());
	copy.shader_version = copy.shader.version_create();
	copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_DEFAULT);

	// Screen Triangle VBO
	glGenBuffers(1, &screen_triangle);
	glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
	const float triangle_verts[6] = {
		-1.0f,
		-1.0f,
		3.0f,
		-1.0f,
		-1.0f,
		3.0f,
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, triangle_verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Screen Quad VBO
	glGenBuffers(1, &quad);
	glBindBuffer(GL_ARRAY_BUFFER, quad);
	const float quad_verts[24] = {
		-1.0f, -1.0f,  0.0f, 0.0f,
		1.0f, -1.0f,  1.0f, 0.0f,
		1.0f,  1.0f,  1.0f, 1.0f,
		-1.0f, -1.0f,  0.0f, 0.0f,
		1.0f,  1.0f,  1.0f, 1.0f,
		-1.0f,  1.0f,  0.0f, 1.0f,
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, quad_verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

CopyEffects::~CopyEffects() {
	singleton = nullptr;

	copy.shader.version_free(copy.shader_version);

	glDeleteBuffers(1, &screen_triangle);
	glDeleteBuffers(1, &quad);
}

void CopyEffects::set_color(const Color &p_color, const Rect2i &p_region) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_SIMPLE_COLOR);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::set_color");

	copy.shader.version_set_uniform(CopyShaderGLES2::COPY_SECTION, p_region.position.x, p_region.position.y, p_region.size.x, p_region.size.y, copy.shader_version, CopyShaderGLES2::MODE_SIMPLE_COLOR);
	copy.shader.version_set_uniform(CopyShaderGLES2::COLOR_IN, p_color, copy.shader_version, CopyShaderGLES2::MODE_SIMPLE_COLOR);

	draw_screen_quad();
}

void CopyEffects::draw_screen_triangle() {
	// GLES2 doesn't have native VAOs in the base spec, so we bind pointers directly
	glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);

	// Position attribute (2 floats).
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
	glEnableVertexAttribArray(RS::ARRAY_VERTEX);

	// UV attribute (2 floats).
	glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)(sizeof(float) * 2));
	glEnableVertexAttribArray(RS::ARRAY_TEX_UV);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Clean up state
	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CopyEffects::draw_screen_quad() {
	glBindBuffer(GL_ARRAY_BUFFER, quad);
	
	// Position attribute
	glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
	glEnableVertexAttribArray(RS::ARRAY_VERTEX);

	// UV attribute (Offset by 2 floats)
	glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)(sizeof(float) * 2));
	glEnableVertexAttribArray(RS::ARRAY_TEX_UV);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	// Clean up state
	glDisableVertexAttribArray(RS::ARRAY_VERTEX);
	glDisableVertexAttribArray(RS::ARRAY_TEX_UV);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CopyEffects::copy_to_rect(const Rect2 &p_rect) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_COPY_SECTION);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_to_rect");

	copy.shader.version_set_uniform(CopyShaderGLES2::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES2::MODE_COPY_SECTION);

	draw_screen_quad();
}

void CopyEffects::copy_to_rect_3d(const Rect2 &p_rect, float p_layer, int p_type, float p_lod) {
	ERR_FAIL_COND(p_type != Texture::TYPE_LAYERED && p_type != Texture::TYPE_3D);

	CopyShaderGLES2::ShaderVariant variant = p_type == Texture::TYPE_LAYERED ? CopyShaderGLES2::MODE_COPY_SECTION_2D_ARRAY : CopyShaderGLES2::MODE_COPY_SECTION_3D;

	bool success = copy.shader.version_bind_shader(copy.shader_version, variant);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_to_rect_3d");

	copy.shader.version_set_uniform(CopyShaderGLES2::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, variant);
	// TODO(GLES2): add layer and lod to CopyShaderGLES2 uniforms.

	draw_screen_quad();
}

void CopyEffects::copy_to_and_from_rect(const Rect2 &p_rect) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_COPY_SECTION_SOURCE);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_to_and_from_rect");

	copy.shader.version_set_uniform(CopyShaderGLES2::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES2::MODE_COPY_SECTION_SOURCE);
	copy.shader.version_set_uniform(CopyShaderGLES2::SOURCE_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES2::MODE_COPY_SECTION_SOURCE);

	draw_screen_quad();
}

void CopyEffects::copy_screen(float p_multiply) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_SCREEN);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_screen");

	copy.shader.version_set_uniform(CopyShaderGLES2::MULTIPLY, p_multiply, copy.shader_version, CopyShaderGLES2::MODE_SCREEN);

	draw_screen_triangle();
}

void CopyEffects::copy_cube_to_rect(const Rect2 &p_rect) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_CUBE_TO_OCTAHEDRAL);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_cube_to_rect");

	copy.shader.version_set_uniform(CopyShaderGLES2::COPY_SECTION, p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y, copy.shader_version, CopyShaderGLES2::MODE_CUBE_TO_OCTAHEDRAL);
	draw_screen_quad();
}

void CopyEffects::copy_cube_to_panorama(float p_mip_level) {
	bool success = copy.shader.version_bind_shader(copy.shader_version, CopyShaderGLES2::MODE_CUBE_TO_PANORAMA);
	ERR_FAIL_COND_MSG(!success, "Failed to bind CopyEffect Shader during CopyEffects::copy_cube_to_panorama");

	copy.shader.version_set_uniform(CopyShaderGLES2::MIP_LEVEL, p_mip_level, copy.shader_version, CopyShaderGLES2::MODE_CUBE_TO_PANORAMA);
	draw_screen_quad();
}

// Stubs for mipmapping/blur effects (GLES2 does not support native framebuffer blit well enough for this)
void CopyEffects::bilinear_blur(GLuint p_source_texture, int p_mipmap_count, const Rect2i &p_region) {

}

void CopyEffects::gaussian_blur(GLuint p_source_texture, int p_mipmap_count, const Rect2i &p_region, const Size2i &p_size) {

}
#endif // GLES2_ENABLED
