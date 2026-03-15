/**************************************************************************/
/*  material_storage.cpp                                                  */
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

#include "core/config/project_settings.h"

#include "config.h"
#include "material_storage.h"
#include "particles_storage.h"
#include "texture_storage.h"

#include "drivers/gles2/rasterizer_canvas_gles2.h"
#include "drivers/gles2/rasterizer_gles2.h"
#include "servers/rendering/storage/variant_converters.h"

using namespace GLES2;

///////////////////////////////////////////////////////////////////////////
// UBI helper functions

[[maybe_unused]] static void _fill_std140_variant_ubo_value(ShaderLanguage::DataType type, int p_array_size, const Variant &value, uint8_t *data) {

}

_FORCE_INLINE_ static void _fill_std140_ubo_value(ShaderLanguage::DataType type, const Vector<ShaderLanguage::Scalar> &value, uint8_t *data) {

}

_FORCE_INLINE_ static void _fill_std140_ubo_empty(ShaderLanguage::DataType type, int p_array_size, uint8_t *data) {

}

///////////////////////////////////////////////////////////////////////////
// ShaderData

void ShaderData::set_path_hint(const String &p_hint) {
	path = p_hint;
}

void ShaderData::set_default_texture_parameter(const StringName &p_name, RID p_texture, int p_index) {

}

Variant ShaderData::get_default_parameter(const StringName &p_parameter) const {
	return Variant();
}

void ShaderData::get_shader_uniform_list(List<PropertyInfo> *p_param_list) const {

}

void ShaderData::get_instance_param_list(List<RendererMaterialStorage::InstanceShaderParam> *p_param_list) const {

}

bool ShaderData::is_parameter_texture(const StringName &p_param) const {
    return false;
}

///////////////////////////////////////////////////////////////////////////
// MaterialData

// Look up table to translate ShaderLanguage::DataType to GL_TEXTURE_*
static const GLenum target_from_type[ShaderLanguage::TYPE_MAX] = {
	GL_TEXTURE_2D, // TYPE_VOID,
	GL_TEXTURE_2D, // TYPE_BOOL,
	GL_TEXTURE_2D, // TYPE_BVEC2,
	GL_TEXTURE_2D, // TYPE_BVEC3,
	GL_TEXTURE_2D, // TYPE_BVEC4,
	GL_TEXTURE_2D, // TYPE_INT,
	GL_TEXTURE_2D, // TYPE_IVEC2,
	GL_TEXTURE_2D, // TYPE_IVEC3,
	GL_TEXTURE_2D, // TYPE_IVEC4,
	GL_TEXTURE_2D, // TYPE_UINT,
	GL_TEXTURE_2D, // TYPE_UVEC2,
	GL_TEXTURE_2D, // TYPE_UVEC3,
	GL_TEXTURE_2D, // TYPE_UVEC4,
	GL_TEXTURE_2D, // TYPE_FLOAT,
	GL_TEXTURE_2D, // TYPE_VEC2,
	GL_TEXTURE_2D, // TYPE_VEC3,
	GL_TEXTURE_2D, // TYPE_VEC4,
	GL_TEXTURE_2D, // TYPE_MAT2,
	GL_TEXTURE_2D, // TYPE_MAT3,
	GL_TEXTURE_2D, // TYPE_MAT4,
	GL_TEXTURE_2D, // TYPE_SAMPLER2D,
	GL_TEXTURE_2D, // TYPE_ISAMPLER2D,
	GL_TEXTURE_2D, // TYPE_USAMPLER2D,
	GL_TEXTURE_2D_ARRAY, // TYPE_SAMPLER2DARRAY,
	GL_TEXTURE_2D_ARRAY, // TYPE_ISAMPLER2DARRAY,
	GL_TEXTURE_2D_ARRAY, // TYPE_USAMPLER2DARRAY,
	GL_TEXTURE_3D, // TYPE_SAMPLER3D,
	GL_TEXTURE_3D, // TYPE_ISAMPLER3D,
	GL_TEXTURE_3D, // TYPE_USAMPLER3D,
	GL_TEXTURE_CUBE_MAP, // TYPE_SAMPLERCUBE,
	GL_TEXTURE_CUBE_MAP, // TYPE_SAMPLERCUBEARRAY,
	_GL_TEXTURE_EXTERNAL_OES, // TYPE_SAMPLEREXT
	GL_TEXTURE_2D, // TYPE_STRUCT
};

static const RS::CanvasItemTextureRepeat repeat_from_uniform[ShaderLanguage::REPEAT_DEFAULT + 1] = {
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, // ShaderLanguage::TextureRepeat::REPEAT_DISABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED, // ShaderLanguage::TextureRepeat::REPEAT_ENABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED, // ShaderLanguage::TextureRepeat::REPEAT_DEFAULT,
};

static const RS::CanvasItemTextureRepeat repeat_from_uniform_canvas[ShaderLanguage::REPEAT_DEFAULT + 1] = {
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, // ShaderLanguage::TextureRepeat::REPEAT_DISABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED, // ShaderLanguage::TextureRepeat::REPEAT_ENABLE,
	RS::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED, // ShaderLanguage::TextureRepeat::REPEAT_DEFAULT,
};

static const RS::CanvasItemTextureFilter filter_from_uniform[ShaderLanguage::FILTER_DEFAULT + 1] = {
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, // ShaderLanguage::TextureFilter::FILTER_NEAREST,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, // ShaderLanguage::TextureFilter::FILTER_LINEAR,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_DEFAULT,
};

static const RS::CanvasItemTextureFilter filter_from_uniform_canvas[ShaderLanguage::FILTER_DEFAULT + 1] = {
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, // ShaderLanguage::TextureFilter::FILTER_NEAREST,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, // ShaderLanguage::TextureFilter::FILTER_LINEAR,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_NEAREST_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, // ShaderLanguage::TextureFilter::FILTER_LINEAR_MIPMAP_ANISOTROPIC,
	RS::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, // ShaderLanguage::TextureFilter::FILTER_DEFAULT,
};

void MaterialData::update_uniform_buffer(const HashMap<StringName, ShaderLanguage::ShaderNode::Uniform> &p_uniforms, const uint32_t *p_uniform_offsets, const HashMap<StringName, Variant> &p_parameters, uint8_t *p_buffer, uint32_t p_buffer_size) {

}

MaterialData::~MaterialData() {

}

void MaterialData::update_textures(const HashMap<StringName, Variant> &p_parameters, const HashMap<StringName, HashMap<int, RID>> &p_default_textures, const Vector<ShaderCompiler::GeneratedCode::Texture> &p_texture_uniforms, RID *p_textures, bool p_is_3d_shader_type) {

}

void MaterialData::update_parameters_internal(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty, const HashMap<StringName, ShaderLanguage::ShaderNode::Uniform> &p_uniforms, const uint32_t *p_uniform_offsets, const Vector<ShaderCompiler::GeneratedCode::Texture> &p_texture_uniforms, const HashMap<StringName, HashMap<int, RID>> &p_default_texture_params, uint32_t p_ubo_size, bool p_is_3d_shader_type) {

}

///////////////////////////////////////////////////////////////////////////
// Material Storage

MaterialStorage *MaterialStorage::singleton = nullptr;

MaterialStorage *MaterialStorage::get_singleton() {
	return singleton;
}

MaterialStorage::MaterialStorage() {
	singleton = this;
}

MaterialStorage::~MaterialStorage() {
	//shaders.copy.version_free(shaders.copy_version);
	singleton = nullptr;
}

/* GLOBAL SHADER UNIFORM API */

int32_t MaterialStorage::_global_shader_uniform_allocate(uint32_t p_elements) {
	return -1;
}

void MaterialStorage::_global_shader_uniform_store_in_buffer(int32_t p_index, RS::GlobalShaderParameterType p_type, const Variant &p_value) {

}

void MaterialStorage::_global_shader_uniform_mark_buffer_dirty(int32_t p_index, int32_t p_elements) {

}

void MaterialStorage::global_shader_parameter_add(const StringName &p_name, RS::GlobalShaderParameterType p_type, const Variant &p_value) {

}

void MaterialStorage::global_shader_parameter_remove(const StringName &p_name) {

}

Vector<StringName> MaterialStorage::global_shader_parameter_get_list() const {
    return Vector<StringName>();
}

void MaterialStorage::global_shader_parameter_set(const StringName &p_name, const Variant &p_value) {

}

void MaterialStorage::global_shader_parameter_set_override(const StringName &p_name, const Variant &p_value) {

}

Variant MaterialStorage::global_shader_parameter_get(const StringName &p_name) const {
    return Variant();
}

RS::GlobalShaderParameterType MaterialStorage::global_shader_parameter_get_type_internal(const StringName &p_name) const {
    return RS::GLOBAL_VAR_TYPE_MAX;
}

RS::GlobalShaderParameterType MaterialStorage::global_shader_parameter_get_type(const StringName &p_name) const {
    return RS::GLOBAL_VAR_TYPE_MAX;
}

void MaterialStorage::global_shader_parameters_load_settings(bool p_load_textures) {

}

void MaterialStorage::global_shader_parameters_clear() {
	global_shader_uniforms.variables.clear();
}

GLuint MaterialStorage::global_shader_parameters_get_uniform_buffer() const {
	return 0;
}

int32_t MaterialStorage::global_shader_parameters_instance_allocate(RID p_instance) {
	return -1;
}

void MaterialStorage::global_shader_parameters_instance_free(RID p_instance) {

}

void MaterialStorage::global_shader_parameters_instance_update(RID p_instance, int p_index, const Variant &p_value, int p_flags_count) {

}

void MaterialStorage::_update_global_shader_uniforms() {

}

/* SHADER API */

RID MaterialStorage::shader_allocate() {
    return RID();
}

void MaterialStorage::shader_initialize(RID p_rid) {

}

void MaterialStorage::shader_free(RID p_rid) {

}

void MaterialStorage::shader_set_code(RID p_shader, const String &p_code) {

}

void MaterialStorage::shader_set_path_hint(RID p_shader, const String &p_path) {

}

String MaterialStorage::shader_get_code(RID p_shader) const {
	return String();
}

void MaterialStorage::get_shader_parameter_list(RID p_shader, List<PropertyInfo> *p_param_list) const {

}

void MaterialStorage::shader_set_default_texture_parameter(RID p_shader, const StringName &p_name, RID p_texture, int p_index) {

}

RID MaterialStorage::shader_get_default_texture_parameter(RID p_shader, const StringName &p_name, int p_index) const {
	return RID();
}

Variant MaterialStorage::shader_get_parameter_default(RID p_shader, const StringName &p_param) const {
	return Variant();
}

RS::ShaderNativeSourceCode MaterialStorage::shader_get_native_source_code(RID p_shader) const {
	return RS::ShaderNativeSourceCode();
}

/* MATERIAL API */

void MaterialStorage::_material_queue_update(GLES2::Material *material, bool p_uniform, bool p_texture) {

}

void MaterialStorage::_update_queued_materials() {
}

RID MaterialStorage::material_allocate() {
    return RID();
}

void MaterialStorage::material_initialize(RID p_rid) {

}

void MaterialStorage::material_free(RID p_rid) {

}

void MaterialStorage::material_set_shader(RID p_material, RID p_shader) {

}

void MaterialStorage::material_set_param(RID p_material, const StringName &p_param, const Variant &p_value) {

}

Variant MaterialStorage::material_get_param(RID p_material, const StringName &p_param) const {
    return Variant();
}

void MaterialStorage::material_set_next_pass(RID p_material, RID p_next_material) {

}

void MaterialStorage::material_set_render_priority(RID p_material, int priority) {

}

bool MaterialStorage::material_is_animated(RID p_material) {
	return false; //by default nothing is animated
}

bool MaterialStorage::material_casts_shadows(RID p_material) {
	return true; //by default everything casts shadows
}

RS::CullMode MaterialStorage::material_get_cull_mode(RID p_material) const {
	return RS::CULL_MODE_DISABLED;
}

void MaterialStorage::material_get_instance_shader_parameters(RID p_material, List<InstanceShaderParam> *r_parameters) {

}

void MaterialStorage::material_update_dependency(RID p_material, DependencyTracker *p_instance) {

}

LocalVector<ShaderGLES2::TextureUniformData> get_texture_uniform_data(const Vector<ShaderCompiler::GeneratedCode::Texture> &texture_uniforms) {
	return LocalVector<ShaderGLES2::TextureUniformData>();
}

/* Canvas Shader Data */

void CanvasShaderData::set_code(const String &p_code) {

}

bool CanvasShaderData::is_animated() const {
	return false;
}

bool CanvasShaderData::casts_shadows() const {
	return false;
}

RS::ShaderNativeSourceCode CanvasShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.canvas_shader.version_get_native_source_code(version);
}

CanvasShaderData::CanvasShaderData() {
	valid = false;
	uses_screen_texture = false;
	uses_sdf = false;
}

CanvasShaderData::~CanvasShaderData() {
	if (version.is_valid()) {
		MaterialStorage::get_singleton()->shaders.canvas_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_canvas_shader_func() {
	CanvasShaderData *shader_data = memnew(CanvasShaderData);
	return shader_data;
}

void CanvasMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, false);
}

[[maybe_unused]] static void bind_uniforms_generic(const Vector<RID> &p_textures, const Vector<ShaderCompiler::GeneratedCode::Texture> &p_texture_uniforms, int texture_offset = 0, const RS::CanvasItemTextureFilter *filter_mapping = filter_from_uniform, const RS::CanvasItemTextureRepeat *repeat_mapping = repeat_from_uniform) {
}

void CanvasMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

CanvasMaterialData::~CanvasMaterialData() {
}

GLES2::MaterialData *GLES2::_create_canvas_material_func(ShaderData *p_shader) {
	CanvasMaterialData *material_data = memnew(CanvasMaterialData);
	material_data->shader_data = static_cast<CanvasShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

////////////////////////////////////////////////////////////////////////////////
// SKY SHADER

void SkyShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader.
	valid = true;
}

bool SkyShaderData::is_animated() const {
	return false;
}

bool SkyShaderData::casts_shadows() const {
	return false;
}

RS::ShaderNativeSourceCode SkyShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.sky_shader.version_get_native_source_code(version);
}

SkyShaderData::SkyShaderData() {
	valid = false;
}

SkyShaderData::~SkyShaderData() {
	if (version.is_valid()) {
		MaterialStorage::get_singleton()->shaders.sky_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_sky_shader_func() {
	SkyShaderData *shader_data = memnew(SkyShaderData);
	return shader_data;
}

////////////////////////////////////////////////////////////////////////////////
// Sky material

void SkyMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	uniform_set_updated = true;
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, true);
}

SkyMaterialData::~SkyMaterialData() {
}

GLES2::MaterialData *GLES2::_create_sky_material_func(ShaderData *p_shader) {
	SkyMaterialData *material_data = memnew(SkyMaterialData);
	material_data->shader_data = static_cast<SkyShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

void SkyMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

////////////////////////////////////////////////////////////////////////////////
// Scene SHADER

void SceneShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader.
}

bool SceneShaderData::is_animated() const {
	return (uses_fragment_time && uses_discard) || (uses_vertex_time && uses_vertex);
}

bool SceneShaderData::casts_shadows() const {
	bool has_read_screen_alpha = uses_screen_texture || uses_depth_texture || uses_normal_texture;
	bool has_base_alpha = (uses_alpha && !uses_alpha_clip) || has_read_screen_alpha;
	bool has_alpha = has_base_alpha || uses_blend_alpha;

	return !has_alpha || (uses_depth_prepass_alpha && !(depth_draw == DEPTH_DRAW_DISABLED || depth_test == DEPTH_TEST_DISABLED));
}

RS::ShaderNativeSourceCode SceneShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.scene_shader.version_get_native_source_code(version);
}

SceneShaderData::SceneShaderData() {
	valid = false;
	uses_screen_texture = false;
}

SceneShaderData::~SceneShaderData() {
	if (version.is_valid()) {
		MaterialStorage::get_singleton()->shaders.scene_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_scene_shader_func() {
	SceneShaderData *shader_data = memnew(SceneShaderData);
	return shader_data;
}

void SceneMaterialData::set_render_priority(int p_priority) {
//	priority = p_priority - RS::MATERIAL_RENDER_PRIORITY_MIN; //8 bits
}

void SceneMaterialData::set_next_pass(RID p_pass) {
	next_pass = p_pass;
}

void SceneMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, true);
}

SceneMaterialData::~SceneMaterialData() {
}

GLES2::MaterialData *GLES2::_create_scene_material_func(ShaderData *p_shader) {
	SceneMaterialData *material_data = memnew(SceneMaterialData);
	material_data->shader_data = static_cast<SceneShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

void SceneMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

/* Particles SHADER */

void ParticlesShaderData::set_code(const String &p_code) {
	// Initialize and compile the shader.
}

bool ParticlesShaderData::is_animated() const {
	return false;
}

bool ParticlesShaderData::casts_shadows() const {
	return false;
}

RS::ShaderNativeSourceCode ParticlesShaderData::get_native_source_code() const {
	return MaterialStorage::get_singleton()->shaders.particles_process_shader.version_get_native_source_code(version);
}

ParticlesShaderData::~ParticlesShaderData() {
	if (version.is_valid()) {
		MaterialStorage::get_singleton()->shaders.particles_process_shader.version_free(version);
	}
}

GLES2::ShaderData *GLES2::_create_particles_shader_func() {
	ParticlesShaderData *shader_data = memnew(ParticlesShaderData);
	return shader_data;
}

void ParticleProcessMaterialData::update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) {
	update_parameters_internal(p_parameters, p_uniform_dirty, p_textures_dirty, shader_data->uniforms, shader_data->ubo_offsets.ptr(), shader_data->texture_uniforms, shader_data->default_texture_params, shader_data->ubo_size, true);
}

ParticleProcessMaterialData::~ParticleProcessMaterialData() {
}

GLES2::MaterialData *GLES2::_create_particles_material_func(ShaderData *p_shader) {
	ParticleProcessMaterialData *material_data = memnew(ParticleProcessMaterialData);
	material_data->shader_data = static_cast<ParticlesShaderData *>(p_shader);
	//update will happen later anyway so do nothing.
	return material_data;
}

void ParticleProcessMaterialData::bind_uniforms() {
	// Bind Material Uniforms
}

#endif // !GLES2_ENABLED
