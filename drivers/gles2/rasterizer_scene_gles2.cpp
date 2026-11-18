/**************************************************************************/
/*  rasterizer_scene_gles2.cpp                                            */
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

#include "rasterizer_scene_gles2.h"

#include "drivers/gles2/effects/copy_effects.h"
#include "drivers/gles2/effects/feed_effects.h"
#include "rasterizer_gles2.h"
#include "storage/config.h"
#include "storage/mesh_storage.h"
#include "storage/particles_storage.h"
#include "storage/texture_storage.h"
#include "rasterizer_scene_batcher_helpers_gles2.h"

#include "core/config/project_settings.h"
#include "core/templates/sort_array.h"
#include "servers/camera/camera_feed.h"
#include "servers/camera_server.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"

#ifdef GLES2_ENABLED

RasterizerSceneGLES2 *RasterizerSceneGLES2::singleton = nullptr;

/* STATIC */

static GLuint _init_radiance_texture_gles2(int p_size, int p_mipmaps, String p_name) {
	GLuint tex;
	glGenTextures(1, &tex);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_init_radiance_texture_gles2: glGenTextures");

	glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

	int size = p_size;
	int total_size = 0;
	for (int mip = 0; mip <= p_mipmaps; mip++) {
		for (int i = 0; i < 6; i++) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		}
		total_size += size * size * 4 * 6;
		size = MAX(1, size >> 1);
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_init_radiance_texture_gles2: glTexImage2D");

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, p_mipmaps > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	GLES2::Utilities::get_singleton()->texture_allocated_data(tex, total_size, p_name);

	return tex;
}

static constexpr int MAXIMUM_COLOR_ATTACHMENT = 4;
static const Vector3 VIEW_NORMALS[6] = {
	Vector3(+1, 0, 0), Vector3(-1, 0, 0), Vector3(0, +1, 0),
	Vector3(0, -1, 0), Vector3(0, 0, +1), Vector3(0, 0, -1)
};
static const Vector3 VIEW_UP[6] = {
	Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, +1),
	Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0)
};
static GLenum CUBE_MAP_FACES[6] = {
	GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
};
static constexpr uint64_t PASS_SPEC_FLAGS[4] = {
	SceneShaderGLES2::RENDER_MATERIAL_ALBEDO,
	SceneShaderGLES2::RENDER_MATERIAL_NORMAL,
	SceneShaderGLES2::RENDER_MATERIAL_ORM,
	SceneShaderGLES2::RENDER_MATERIAL_EMISSION,
};

/* INITIALISATION */

void RasterizerSceneGLES2::initialize() {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	GLES2::Config *config = GLES2::Config::get_singleton();

	batch_initialize();

	cull_argument.set_page_pool(&cull_argument_pool);

	{
		String global_defines;
		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
		global_defines += "\n#define MAX_LIGHT_DATA_STRUCTS " + itos(config->max_renderable_lights) + "\n";
		global_defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 1\n";
		global_defines += "\n#define MAX_FORWARD_LIGHTS " + itos(config->max_lights_per_object) + "\n";
		global_defines += "\n#define MAX_ROUGHNESS_LOD " + itos(sky_globals.roughness_layers - 1) + ".0\n";
		if (config->force_vertex_shading) {
			global_defines += "\n#define USE_VERTEX_LIGHTING\n";
		}
		if (!config->multi_bounce_occlusion) {
			global_defines += "\n#define MULTI_BOUNCE_OCCLUSION_DISABLED\n";
		}
		material_storage->shaders.scene_shader.initialize(global_defines);
		scene_globals.shader_default_version = material_storage->shaders.scene_shader.version_create();
		material_storage->shaders.scene_shader.version_bind_shader(scene_globals.shader_default_version, SceneShaderGLES2::MODE_COLOR);
	}

	{
		// Setup Lights
		config->max_renderable_lights = MIN(config->max_renderable_lights, 256);
		config->max_lights_per_object = MIN(config->max_lights_per_object, config->max_renderable_lights);

		scene_state.omni_lights = memnew_arr(LightData, config->max_renderable_lights);
		scene_state.omni_light_sort = memnew_arr(InstanceSort<GLES2::LightInstance>, config->max_renderable_lights);
		scene_state.spot_lights = memnew_arr(LightData, config->max_renderable_lights);
		scene_state.spot_light_sort = memnew_arr(InstanceSort<GLES2::LightInstance>, config->max_renderable_lights);
		scene_state.directional_lights = memnew_arr(DirectionalLightData, MAX_DIRECTIONAL_LIGHTS);

		scene_state.positional_shadows = memnew_arr(ShadowData, config->max_renderable_lights * 2);
		scene_state.directional_shadows = memnew_arr(DirectionalShadowData, MAX_DIRECTIONAL_LIGHTS);
	}

	{
		// Initialize Sky stuff
		sky_globals.roughness_layers = GLOBAL_GET("rendering/reflections/sky_reflections/roughness_layers");

		String global_defines;
		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
		global_defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(sky_globals.max_directional_lights) + "\n";
		material_storage->shaders.sky_shader.initialize(global_defines);
		sky_globals.shader_default_version = material_storage->shaders.sky_shader.version_create();
	}

	{
		sky_globals.default_shader = material_storage->shader_allocate();
		sky_globals.max_directional_lights = 1;
		sky_globals.directional_lights = memnew_arr(DirectionalLightData, sky_globals.max_directional_lights);
		ERR_FAIL_NULL(sky_globals.directional_lights);
		sky_globals.last_frame_directional_lights = memnew_arr(DirectionalLightData, sky_globals.max_directional_lights);
		if (unlikely(!sky_globals.last_frame_directional_lights)) {
			memdelete_arr(sky_globals.directional_lights);
			sky_globals.directional_lights = nullptr;
			ERR_FAIL_MSG("Out of memory in initialisation of RasterizerSceneGLES3");
		}
		sky_globals.last_frame_directional_light_count = sky_globals.max_directional_lights + 1;

		material_storage->shader_initialize(sky_globals.default_shader);

		material_storage->shader_set_code(sky_globals.default_shader, R"(
// Default sky shader.

shader_type sky;

void sky() {
	COLOR = vec3(0.0);
}
)");
		sky_globals.default_material = material_storage->material_allocate();
		material_storage->material_initialize(sky_globals.default_material);

		material_storage->material_set_shader(sky_globals.default_material, sky_globals.default_shader);
	}
	{
		sky_globals.fog_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(sky_globals.fog_shader);

		material_storage->shader_set_code(sky_globals.fog_shader, R"(
// Default clear color sky shader.

shader_type sky;

uniform vec4 clear_color;

void sky() {
	COLOR = clear_color.rgb;
}
)");
		sky_globals.fog_material = material_storage->material_allocate();
		material_storage->material_initialize(sky_globals.fog_material);

		material_storage->material_set_shader(sky_globals.fog_material, sky_globals.fog_shader);
	}

	{
		constexpr float qv[6] = {
			-1.0f, -1.0f,
			 3.0f, -1.0f,
			-1.0f,  3.0f,
		};

		glGenBuffers(1, &sky_globals.screen_triangle);
		glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, qv, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// _draw_sky handles fallback internally
		sky_globals.screen_triangle_array = 0;

		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::initialize: sky screen triangle setup");
	}

	{
		// Default 3D material and shader
		scene_globals.default_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(scene_globals.default_shader);
		material_storage->shader_set_code(scene_globals.default_shader, R"(
// Default 3D material shader (Compatibility).

shader_type spatial;

void vertex() {
	ROUGHNESS = 0.8;
}

void fragment() {
	ALBEDO = vec3(0.6);
	ROUGHNESS = 0.8;
	METALLIC = 0.2;
}
)");
		scene_globals.default_material = material_storage->material_allocate();
		material_storage->material_initialize(scene_globals.default_material);
		material_storage->material_set_shader(scene_globals.default_material, scene_globals.default_shader);
	}

	{
		// Overdraw material and shader
		scene_globals.overdraw_shader = material_storage->shader_allocate();
		material_storage->shader_initialize(scene_globals.overdraw_shader);
		material_storage->shader_set_code(scene_globals.overdraw_shader, R"(
// 3D editor Overdraw debug draw mode shader (Compatibility).

shader_type spatial;

render_mode blend_add, unshaded, fog_disabled;

void fragment() {
	ALBEDO = vec3(0.4, 0.8, 0.8);
	ALPHA = 0.2;
}
)");
		scene_globals.overdraw_material = material_storage->material_allocate();
		material_storage->material_initialize(scene_globals.overdraw_material);
		material_storage->material_set_shader(scene_globals.overdraw_material, scene_globals.overdraw_shader);
	}

	// MultiMesh may read from color when color is disabled, so make sure that the color defaults to white instead of black.
	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
}

RenderGeometryInstance *RasterizerSceneGLES2::geometry_instance_create(RID p_base) {
	RS::InstanceType type = RSG::utilities->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RS::INSTANCE_GEOMETRY_MASK), nullptr);

	GeometryInstanceGLES2 *ginstance = geometry_instance_alloc.alloc();
	ginstance->data = memnew(GeometryInstanceGLES2::Data);

	ERR_FAIL_NULL_V(ginstance->data, nullptr);

	ginstance->data->base = p_base;
	ginstance->data->base_type = type;
	ginstance->data->dependency_tracker.userdata = ginstance;
	ginstance->data->dependency_tracker.changed_callback = _geometry_instance_dependency_changed;
	ginstance->data->dependency_tracker.deleted_callback = _geometry_instance_dependency_deleted;

	ginstance->_mark_dirty();

	return ginstance;
}

uint32_t RasterizerSceneGLES2::geometry_instance_get_pair_mask() {
	return ((1 << RS::INSTANCE_LIGHT) | (1 << RS::INSTANCE_REFLECTION_PROBE));
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::pair_light_instances(const RID *p_light_instances, uint32_t p_light_instance_count) {
	GLES2::Config *config = GLES2::Config::get_singleton();

	paired_omni_light_count = 0;
	paired_spot_light_count = 0;
	paired_omni_lights.clear();
	paired_spot_lights.clear();

	for (uint32_t i = 0; i < p_light_instance_count; i++) {
		RS::LightType type = GLES2::LightStorage::get_singleton()->light_instance_get_type(p_light_instances[i]);
		switch (type) {
			case RS::LIGHT_OMNI: {
				if (paired_omni_light_count < (uint32_t)config->max_lights_per_object) {
					paired_omni_lights.push_back(p_light_instances[i]);
					paired_omni_light_count++;
				}
			} break;
			case RS::LIGHT_SPOT: {
				if (paired_spot_light_count < (uint32_t)config->max_lights_per_object) {
					paired_spot_lights.push_back(p_light_instances[i]);
					paired_spot_light_count++;
				}
			} break;
			default:
				break;
		}
	}
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) {
	paired_reflection_probes.clear();

	for (uint32_t i = 0; i < p_reflection_probe_instance_count; i++) {
		paired_reflection_probes.push_back(p_reflection_probe_instances[i]);
	}
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::_clear_surface_caches() {
	RasterizerSceneGLES2 *scene = RasterizerSceneGLES2::get_singleton();
	scene->bdata.invalidate_vbo_cache();

	GeometryInstanceSurface *surf = surface_caches;
	while (surf) {
		GeometryInstanceSurface *next = surf->next;
		surf->baked_world_cache.clear();
		surf->baked_tangent_cache.clear();
		surf->cached_transform_hash = 0;
		scene->geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	surface_caches = nullptr;
}

void RasterizerSceneGLES2::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_geometry_instance);
	ERR_FAIL_NULL(ginstance);
	ginstance->_clear_surface_caches();
	memdelete(ginstance->data);
	geometry_instance_alloc.free(ginstance);
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::_mark_dirty() {
	if (dirty_list_element.in_list()) {
		return;
	}

	_clear_surface_caches();

	RasterizerSceneGLES2::get_singleton()->geometry_instance_dirty_list.add(&dirty_list_element);
}

void RasterizerSceneGLES2::GeometryInstanceGLES2::set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) {

}

void RasterizerSceneGLES2::GeometryInstanceGLES2::set_lightmap_capture(const Color *p_sh9) {

}

RasterizerSceneGLES2::MultiMeshInstanceData RasterizerSceneGLES2::_get_multimesh_data(const GeometryInstanceSurface *p_surface) {
	MultiMeshInstanceData mm;
	if (p_surface->owner && p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH) {
		GLES2::MultiMesh *multimesh = GLES2::MeshStorage::get_singleton()->get_multimesh(p_surface->owner->data->base);
		if (multimesh) {
			mm.data = multimesh->data_cache.ptr();
			mm.stride = multimesh->stride_cache;
			mm.color_offset = multimesh->color_offset_cache;
			mm.uses_colors = multimesh->uses_colors;
			mm.format = multimesh->xform_format;
		}
	}
	return mm;
}

void RasterizerSceneGLES2::_update_dirty_geometry_instances() {
	while (geometry_instance_dirty_list.first()) {
		_geometry_instance_update(geometry_instance_dirty_list.first()->self());
	}
}

void RasterizerSceneGLES2::_geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker) {
	switch (p_notification) {
		case Dependency::DEPENDENCY_CHANGED_MATERIAL:
		case Dependency::DEPENDENCY_CHANGED_MESH:
		case Dependency::DEPENDENCY_CHANGED_PARTICLES:
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH:
		case Dependency::DEPENDENCY_CHANGED_SKELETON_DATA: {
			static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
			static_cast<GeometryInstanceGLES2 *>(p_tracker->userdata)->data->dirty_dependencies = true;
		} break;
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES: {
			GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_tracker->userdata);
			if (ginstance->data->base_type == RS::INSTANCE_MULTIMESH) {
				ginstance->instance_count = GLES2::MeshStorage::get_singleton()->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		default: {
			//rest of notifications of no interest
		} break;
	}
}

void RasterizerSceneGLES2::_geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker) {
	static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
	static_cast<GeometryInstanceGLES2 *>(p_tracker->userdata)->data->dirty_dependencies = true;
}

void RasterizerSceneGLES2::_geometry_instance_add_surface_with_material(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh) {
	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();

	bool has_read_screen_alpha = p_material->shader_data->uses_screen_texture || p_material->shader_data->uses_depth_texture || p_material->shader_data->uses_normal_texture;
	bool has_base_alpha = ((p_material->shader_data->uses_alpha && !p_material->shader_data->uses_alpha_clip) || has_read_screen_alpha);
	bool has_blend_alpha = p_material->shader_data->uses_blend_alpha;
	bool has_alpha = has_base_alpha || has_blend_alpha;

	uint32_t flags = 0;

	if (p_material->shader_data->uses_screen_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_SCREEN_TEXTURE;
	}
	if (p_material->shader_data->uses_depth_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_DEPTH_TEXTURE;
	}
	if (p_material->shader_data->uses_normal_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_NORMAL_TEXTURE;
	}
	if (ginstance->data->cast_double_sided_shadows) {
		flags |= GeometryInstanceSurface::FLAG_USES_DOUBLE_SIDED_SHADOWS;
	}

	if (
		has_alpha ||
		has_read_screen_alpha ||
		p_material->shader_data->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_DISABLED ||
		p_material->shader_data->depth_test == GLES2::SceneShaderData::DEPTH_TEST_DISABLED
	) {
		flags |= GeometryInstanceSurface::FLAG_PASS_ALPHA;

		bool is_depth_disabled = (
			p_material->shader_data->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_DISABLED ||
			p_material->shader_data->depth_test == GLES2::SceneShaderData::DEPTH_TEST_DISABLED
		);

		if (p_material->shader_data->uses_depth_prepass_alpha && !is_depth_disabled) {
			flags |= GeometryInstanceSurface::FLAG_PASS_DEPTH;
			flags |= GeometryInstanceSurface::FLAG_PASS_OPAQUE;
			flags |= GeometryInstanceSurface::FLAG_PASS_SHADOW;
		}
	} else {
		flags |= GeometryInstanceSurface::FLAG_PASS_OPAQUE;
		flags |= GeometryInstanceSurface::FLAG_PASS_DEPTH;
		flags |= GeometryInstanceSurface::FLAG_PASS_SHADOW;
	}

	GLES2::SceneMaterialData *material_shadow = nullptr;
	void *surface_shadow = nullptr;
	if (!p_material->shader_data->uses_particle_trails && !p_material->shader_data->writes_modelview_or_projection && !p_material->shader_data->uses_vertex && !p_material->shader_data->uses_discard && !p_material->shader_data->uses_depth_prepass_alpha && !p_material->shader_data->uses_alpha_clip && !p_material->shader_data->uses_world_coordinates && !p_material->shader_data->wireframe) {
		flags |= GeometryInstanceSurface::FLAG_USES_SHARED_SHADOW_MATERIAL;
		material_shadow = static_cast<GLES2::SceneMaterialData *>(GLES2::MaterialStorage::get_singleton()->material_get_data(scene_globals.default_material, RS::SHADER_SPATIAL));

		RID shadow_mesh = mesh_storage->mesh_get_shadow_mesh(p_mesh);
		if (shadow_mesh.is_valid()) {
			surface_shadow = mesh_storage->mesh_get_surface(shadow_mesh, p_surface);
		}
	} else {
		material_shadow = p_material;
	}

	GeometryInstanceSurface *surf = geometry_instance_surface_alloc.alloc();

	surf->flags = flags;
	surf->shader = p_material->shader_data;
	surf->material = p_material;
	surf->surface = mesh_storage->mesh_get_surface(p_mesh, p_surface);
	surf->primitive = mesh_storage->mesh_surface_get_primitive(surf->surface);
	surf->surface_index = p_surface;

	if (ginstance->data->dirty_dependencies) {
		RSG::utilities->base_update_dependency(p_mesh, &ginstance->data->dependency_tracker);
	}

	surf->shader_shadow = material_shadow->shader_data;
	surf->material_shadow = material_shadow;
	surf->surface_shadow = surface_shadow ? surface_shadow : surf->surface;

	Array arrays = mesh_storage->mesh_surface_get_arrays(p_mesh, p_surface);
	if (!arrays.is_empty()) {
		surf->vertex_cache = arrays[RS::ARRAY_VERTEX];
		surf->normal_cache = arrays[RS::ARRAY_NORMAL];
		surf->tangent_cache = arrays[RS::ARRAY_TANGENT];
		surf->uv_cache = arrays[RS::ARRAY_TEX_UV];
		surf->uv2_cache = arrays[RS::ARRAY_TEX_UV2];
		surf->color_cache = arrays[RS::ARRAY_COLOR];
		surf->index_cache = arrays[RS::ARRAY_INDEX];
		surf->index_count = surf->index_cache.size() > 0 ? surf->index_cache.size() : surf->vertex_cache.size();
	} else {
		surf->index_count = 0;
	}

	surf->baked_world_cache.clear();
	surf->baked_tangent_cache.clear();
	surf->cached_transform_hash = 0;

	surf->owner = ginstance;
	surf->next = ginstance->surface_caches;
	ginstance->surface_caches = surf;

	surf->sort.sort_key1 = 0;
	surf->sort.sort_key2 = 0;
	surf->sort.surface_index = p_surface;
	surf->sort.material_id_low = p_material_id & 0x0000FFFF;
	surf->sort.material_id_hi = p_material_id >> 16;
	surf->sort.shader_id = p_shader_id;
	surf->sort.geometry_id = p_mesh.get_local_index();
	surf->sort.priority = p_material->priority;
}

void RasterizerSceneGLES2::_geometry_instance_add_surface_with_material_chain(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material_data, RID p_mat_src, RID p_mesh) {
	GLES2::SceneMaterialData *material_data = p_material_data;
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	_geometry_instance_add_surface_with_material(ginstance, p_surface, material_data, p_mat_src.get_local_index(), material_storage->material_get_shader_id(p_mat_src), p_mesh);

	while (material_data->next_pass.is_valid()) {
		RID next_pass = material_data->next_pass;
		material_data = static_cast<GLES2::SceneMaterialData *>(material_storage->material_get_data(next_pass, RS::SHADER_SPATIAL));
		if (!material_data || !material_data->shader_data->valid) {
			break;
		}
		if (ginstance->data->dirty_dependencies) {
			material_storage->material_update_dependency(next_pass, &ginstance->data->dependency_tracker);
		}
		_geometry_instance_add_surface_with_material(ginstance, p_surface, material_data, next_pass.get_local_index(), material_storage->material_get_shader_id(next_pass), p_mesh);
	}
}

void RasterizerSceneGLES2::_geometry_instance_add_surface(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, RID p_material, RID p_mesh) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	RID m_src = ginstance->data->material_override.is_valid() ? ginstance->data->material_override : p_material;
	GLES2::SceneMaterialData *material_data = nullptr;

	if (m_src.is_valid()) {
		material_data = static_cast<GLES2::SceneMaterialData *>(material_storage->material_get_data(m_src, RS::SHADER_SPATIAL));
		if (!material_data || !material_data->shader_data->valid) {
			material_data = nullptr;
		}
	}

	if (material_data) {
		if (ginstance->data->dirty_dependencies) {
			material_storage->material_update_dependency(m_src, &ginstance->data->dependency_tracker);
		}
	} else {
		material_data = static_cast<GLES2::SceneMaterialData *>(material_storage->material_get_data(scene_globals.default_material, RS::SHADER_SPATIAL));
		m_src = scene_globals.default_material;
	}

	ERR_FAIL_NULL(material_data);

	_geometry_instance_add_surface_with_material_chain(ginstance, p_surface, material_data, m_src, p_mesh);

	if (ginstance->data->material_overlay.is_valid()) {
		m_src = ginstance->data->material_overlay;

		material_data = static_cast<GLES2::SceneMaterialData *>(material_storage->material_get_data(m_src, RS::SHADER_SPATIAL));
		if (material_data && material_data->shader_data->valid) {
			if (ginstance->data->dirty_dependencies) {
				material_storage->material_update_dependency(m_src, &ginstance->data->dependency_tracker);
			}

			_geometry_instance_add_surface_with_material_chain(ginstance, p_surface, material_data, m_src, p_mesh);
		}
	}
}

void RasterizerSceneGLES2::_geometry_instance_update(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES2 *ginstance = static_cast<GeometryInstanceGLES2 *>(p_geometry_instance);
	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();
	GLES2::ParticlesStorage *particles_storage = GLES2::ParticlesStorage::get_singleton();

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_begin();
	}

	switch (ginstance->data->base_type) {
		case RS::INSTANCE_MESH: {
			const RID *materials = nullptr;
			uint32_t surface_count;
			RID mesh = ginstance->data->base;

			if (ginstance->data->dirty_dependencies) {
				RSG::utilities->base_update_dependency(mesh, &ginstance->data->dependency_tracker);
				if (ginstance->mesh_instance.is_valid()) {
					GLES2::MeshInstance *mi = mesh_storage->get_mesh_instance(ginstance->mesh_instance);
					if (mi && mi->skeleton.is_valid()) {
						mesh_storage->skeleton_update_dependency(mi->skeleton, &ginstance->data->dependency_tracker);
					}
				}
			}

			materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
			if (materials) {
				const RID *inst_materials = ginstance->data->surface_materials.ptr();
				uint32_t surf_mat_count = ginstance->data->surface_materials.size();

				for (uint32_t j = 0; j < surface_count; j++) {
					RID material = (j < surf_mat_count && inst_materials[j].is_valid()) ? inst_materials[j] : materials[j];
					_geometry_instance_add_surface(ginstance, j, material, mesh);
				}
			}
			ginstance->instance_count = -1;
		} break;
		case RS::INSTANCE_MULTIMESH: {
			RID mesh = mesh_storage->multimesh_get_mesh(ginstance->data->base);
			if (mesh.is_valid()) {
				const RID *materials = nullptr;
				uint32_t surface_count;

				materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t j = 0; j < surface_count; j++) {
						_geometry_instance_add_surface(ginstance, j, materials[j], mesh);
					}
				}
				ginstance->instance_count = mesh_storage->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		case RS::INSTANCE_PARTICLES: {
			int draw_passes = particles_storage->particles_get_draw_passes(ginstance->data->base);
			for (int j = 0; j < draw_passes; j++) {
				RID mesh = particles_storage->particles_get_draw_pass_mesh(ginstance->data->base, j);
				if (!mesh.is_valid()) {
					continue;
				}
				const RID *materials = nullptr;
				uint32_t surface_count;
				materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t k = 0; k < surface_count; k++) {
						_geometry_instance_add_surface(ginstance, k, materials[k], mesh);
					}
				}
			}
			ginstance->instance_count = particles_storage->particles_get_amount(ginstance->data->base);
		} break;
		default: {
		}
	}

	ginstance->store_transform_cache = true;

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_end();
		ginstance->data->dirty_dependencies = false;
	}

	ginstance->dirty_list_element.remove_from_list();
}

/* SKY API */

void RasterizerSceneGLES2::_free_sky_data(Sky *p_sky) {
	if (p_sky->radiance != 0) {
		GLES2::Utilities::get_singleton()->texture_free_data(p_sky->radiance);
		p_sky->radiance = 0;
		GLES2::Utilities::get_singleton()->texture_free_data(p_sky->raw_radiance);
		p_sky->raw_radiance = 0;
		glDeleteFramebuffers(1, &p_sky->radiance_framebuffer);
		p_sky->radiance_framebuffer = 0;
	}
}

RID RasterizerSceneGLES2::sky_allocate() {
	return sky_owner.allocate_rid();
}

void RasterizerSceneGLES2::sky_initialize(RID p_rid) {
	sky_owner.initialize_rid(p_rid);
}

void RasterizerSceneGLES2::sky_set_radiance_size(RID p_sky, int p_radiance_size) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);
	ERR_FAIL_COND_MSG(p_radiance_size < 32 || p_radiance_size > 2048, "Sky radiance size must be between 32 and 2048");

	if (sky->radiance_size == p_radiance_size) {
		return; // No need to update
	}

	sky->radiance_size = p_radiance_size;

	_free_sky_data(sky);
	_invalidate_sky(sky);
}

void RasterizerSceneGLES2::sky_set_mode(RID p_sky, RS::SkyMode p_mode) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);

	if (sky->mode == p_mode) {
		return;
	}

	sky->mode = p_mode;
	_invalidate_sky(sky);
}

void RasterizerSceneGLES2::sky_set_material(RID p_sky, RID p_material) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);

	if (sky->material == p_material) {
		return;
	}

	sky->material = p_material;
	_invalidate_sky(sky);
}

float RasterizerSceneGLES2::sky_get_baked_exposure(RID p_sky) const {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL_V(sky, 1.0);

	return sky->baked_exposure;
}

void RasterizerSceneGLES2::_invalidate_sky(Sky *p_sky) {
	if (!p_sky->dirty) {
		p_sky->dirty = true;
		p_sky->dirty_list = dirty_sky_list;
		dirty_sky_list = p_sky;
	}
}

void RasterizerSceneGLES2::_update_dirty_skys() {
	Sky *sky = dirty_sky_list;

	while (sky) {
		if (sky->radiance == 0) {
			sky->mipmap_count = Image::get_image_required_mipmaps(sky->radiance_size, sky->radiance_size, Image::FORMAT_RGBA8) - 1;
			// Left uninitialized, will attach a texture at render time
			glGenFramebuffers(1, &sky->radiance_framebuffer);
			sky->radiance = _init_radiance_texture_gles2(sky->radiance_size, sky->mipmap_count, "Sky radiance texture");
			sky->raw_radiance = _init_radiance_texture_gles2(sky->radiance_size, sky->mipmap_count, "Sky raw radiance texture");
		}

		sky->reflection_dirty = true;
		sky->processing_layer = 0;

		Sky *next = sky->dirty_list;
		sky->dirty_list = nullptr;
		sky->dirty = false;
		sky = next;
	}

	dirty_sky_list = nullptr;
}

void RasterizerSceneGLES2::_setup_sky(const RenderDataGLES2 *p_render_data, const PagedArray<RID> &p_lights, const Projection &p_projection, const Transform3D &p_transform, const Size2i p_screen_size) {
	GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	if (!is_environment(p_render_data->environment)) {
		return;
	}

	GLES2::SkyMaterialData *material = nullptr;
	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_render_data->environment));

	RID sky_material;
	GLES2::SkyShaderData *shader_data = nullptr;

	if (sky) {
		sky_material = sky->material;

		if (sky_material.is_valid()) {
			material = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material || !material->shader_data->valid) {
				material = nullptr;
			}
		}

		if (!material) {
			sky_material = sky_globals.default_material;
			material = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}

		ERR_FAIL_COND(!material);

		shader_data = material->shader_data;
		ERR_FAIL_COND(!shader_data);

		if (shader_data->uses_time && time - sky->prev_time > 0.00001) {
			sky->prev_time = time;
			sky->reflection_dirty = true;
			RenderingServerDefault::redraw_request();
		}

		if (material != sky->prev_material) {
			sky->prev_material = material;
			sky->reflection_dirty = true;
		}

		if (material->uniform_set_updated) {
			material->uniform_set_updated = false;
			sky->reflection_dirty = true;
		}

		if (!p_transform.origin.is_equal_approx(sky->prev_position) && shader_data->uses_position) {
			sky->prev_position = p_transform.origin;
			sky->reflection_dirty = true;
		}

		if (shader_data->uses_light) {
			sky_globals.directional_light_count = 0;
			for (int i = 0; i < (int)p_lights.size(); i++) {
				GLES2::LightInstance *li = GLES2::LightStorage::get_singleton()->get_light_instance(p_lights[i]);
				if (!li) {
					continue;
				}
				RID base = li->light;

				ERR_CONTINUE(base.is_null());

				RS::LightType type = light_storage->light_get_type(base);
				if (type == RS::LIGHT_DIRECTIONAL && light_storage->light_directional_get_sky_mode(base) != RS::LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_ONLY) {
					DirectionalLightData &sky_light_data = sky_globals.directional_lights[sky_globals.directional_light_count];
					Transform3D light_transform = li->transform;
					Vector3 world_direction = light_transform.basis.xform(Vector3(0, 0, 1)).normalized();

					sky_light_data.direction[0] = world_direction.x;
					sky_light_data.direction[1] = world_direction.y;
					sky_light_data.direction[2] = world_direction.z;

					float sign = light_storage->light_is_negative(base) ? -1 : 1;
					sky_light_data.energy = sign * light_storage->light_get_param(base, RS::LIGHT_PARAM_ENERGY);

					if (is_using_physical_light_units()) {
						sky_light_data.energy *= light_storage->light_get_param(base, RS::LIGHT_PARAM_INTENSITY);
					} else {
						sky_light_data.energy *= Math_PI;
					}

					if (p_render_data->camera_attributes.is_valid()) {
						sky_light_data.energy *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
					}

					Color linear_col = light_storage->light_get_color(base);
					sky_light_data.color[0] = linear_col.r;
					sky_light_data.color[1] = linear_col.g;
					sky_light_data.color[2] = linear_col.b;

					sky_light_data.enabled = true;

					float angular_diameter = light_storage->light_get_param(base, RS::LIGHT_PARAM_SIZE);
					if (angular_diameter > 0.0) {
						angular_diameter = Math::tan(Math::deg_to_rad(angular_diameter));
					} else {
						angular_diameter = 0.0;
					}
					sky_light_data.size = angular_diameter;
					sky_globals.directional_light_count++;
					if (sky_globals.directional_light_count >= sky_globals.max_directional_lights) {
						break;
					}
				}
			}

			// Check whether the directional_light_buffer changes
			bool light_data_dirty = false;

			// Light buffer is dirty if we have fewer or more lights
			// If we have fewer lights, make sure that old lights are disabled
			if (sky_globals.directional_light_count != sky_globals.last_frame_directional_light_count) {
				light_data_dirty = true;
				for (uint32_t i = sky_globals.directional_light_count; i < sky_globals.max_directional_lights; i++) {
					sky_globals.directional_lights[i].enabled = false;
					sky_globals.last_frame_directional_lights[i].enabled = false;
				}
			}

			if (!light_data_dirty) {
				for (uint32_t i = 0; i < sky_globals.directional_light_count; i++) {
					if (sky_globals.directional_lights[i].direction[0] != sky_globals.last_frame_directional_lights[i].direction[0] ||
							sky_globals.directional_lights[i].direction[1] != sky_globals.last_frame_directional_lights[i].direction[1] ||
							sky_globals.directional_lights[i].direction[2] != sky_globals.last_frame_directional_lights[i].direction[2] ||
							sky_globals.directional_lights[i].energy != sky_globals.last_frame_directional_lights[i].energy ||
							sky_globals.directional_lights[i].color[0] != sky_globals.last_frame_directional_lights[i].color[0] ||
							sky_globals.directional_lights[i].color[1] != sky_globals.last_frame_directional_lights[i].color[1] ||
							sky_globals.directional_lights[i].color[2] != sky_globals.last_frame_directional_lights[i].color[2] ||
							sky_globals.directional_lights[i].enabled != sky_globals.last_frame_directional_lights[i].enabled ||
							sky_globals.directional_lights[i].size != sky_globals.last_frame_directional_lights[i].size) {
						light_data_dirty = true;
						break;
					}
				}
			}

			if (light_data_dirty) {
				for (uint32_t i = 0; i < sky_globals.max_directional_lights; i++) {
					sky_globals.last_frame_directional_lights[i] = sky_globals.directional_lights[i];
				}
				sky_globals.last_frame_directional_light_count = sky_globals.directional_light_count;
				sky->reflection_dirty = true;
			}
		}

		if (!sky->radiance) {
			_invalidate_sky(sky);
			_update_dirty_skys();
		}
	}
}

void RasterizerSceneGLES2::_draw_sky(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier, float p_luminance_multiplier, bool p_use_multiview, bool p_flip_y, bool p_apply_color_adjustments_in_post) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	Sky *sky = is_environment(p_env) ? sky_owner.get_or_null(environment_get_sky(p_env)) : nullptr;
	GLES2::SkyMaterialData *material_data = nullptr;
	RID sky_material;

	RS::EnvironmentBG background = RS::ENV_BG_CLEAR_COLOR;
	if (is_environment(p_env)) {
		background = environment_get_background(p_env);
	}

	if (sky) {
		sky_material = sky->material;
		if (sky_material.is_valid()) {
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material_data || !material_data->shader_data->valid) {
				material_data = nullptr;
			}
		}

		if (!material_data) {
			sky_material = sky_globals.default_material;
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}
	} else if (background == RS::ENV_BG_CLEAR_COLOR || background == RS::ENV_BG_COLOR || p_env.is_null()) {
		sky_material = sky_globals.fog_material;
		material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
	}

	if (!material_data) {
		return;
	}

	GLES2::SkyShaderData *shader_data = material_data->shader_data;
	if (!shader_data) {
		return;
	}

	uint64_t sky_spec = 0;
	if (p_flip_y) {
		sky_spec |= SkyShaderGLES2::USE_INVERTED_Y;
	}
	if (!p_apply_color_adjustments_in_post) {
		sky_spec |= SkyShaderGLES2::APPLY_TONEMAPPING;
	}

	bool success = material_storage->shaders.sky_shader.version_bind_shader(shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	if (!success) {
		return;
	}

	material_data->bind_uniforms();

	Projection camera;
	if (p_env.is_valid() && environment_get_sky_custom_fov(p_env)) {
		float near_plane = p_projection.get_z_near();
		float far_plane = p_projection.get_z_far();
		float aspect = p_projection.get_aspect();
		camera.set_perspective(environment_get_sky_custom_fov(p_env), aspect, near_plane, far_plane);
	} else {
		camera = p_projection;
	}

	Projection correction;
	correction.set_depth_correction(p_flip_y, true, false);
	camera = correction * camera;

	Basis sky_transform;
	if (p_env.is_valid()) {
		sky_transform = environment_get_sky_orientation(p_env);
		sky_transform.invert();
	}
	sky_transform = sky_transform * p_transform.basis;

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::ORIENTATION, sky_transform, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::PROJECTION, camera.columns[2][0], camera.columns[0][0], camera.columns[2][1], camera.columns[1][1], shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::POSITION, p_transform.origin, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::TIME, (float)time, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::LUMINANCE_MULTIPLIER, p_luminance_multiplier, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::EXPOSURE, scene_state.tonemap_ubo.exposure, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::WHITE, scene_state.tonemap_ubo.white, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::TONEMAPPER, scene_state.tonemap_ubo.tonemapper, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);

	if (p_env.is_valid()) {
		Color fog_color = environment_get_fog_light_color(p_env).srgb_to_linear() * environment_get_fog_light_energy(p_env);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_ENABLED, environment_get_fog_enabled(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_AERIAL_PERSPECTIVE, environment_get_fog_aerial_perspective(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_LIGHT_COLOR, fog_color, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_SUN_SCATTER, environment_get_fog_sun_scatter(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_DENSITY, environment_get_fog_density(p_env), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	} else {
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::FOG_ENABLED, false, shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	}

	_bind_sky_directional_lights(shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::Z_FAR, p_projection.get_z_far(), shader_data->version, SkyShaderGLES2::MODE_BACKGROUND, sky_spec);

	// Clean-up from opaque geometry passes
	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(false);
	glDepthFunc(GL_GEQUAL);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glDepthFunc");

	scene_state.enable_gl_blend(false);
	scene_state.set_gl_cull_mode(RS::CULL_MODE_BACK);

	if (sky_globals.screen_triangle_array != 0) {
		glBindVertexArray(sky_globals.screen_triangle_array);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glBindVertexArray");
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glBindBuffer");

		glEnableVertexAttribArray(RS::ARRAY_VERTEX);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glEnableVertexAttribArray");

		glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glVertexAttribPointer");
	}

	glDrawArrays(GL_TRIANGLES, 0, 3);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glDrawArrays");

	if (sky_globals.screen_triangle_array == 0) {
		glDisableVertexAttribArray(RS::ARRAY_VERTEX);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: glDisableVertexAttribArray");

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: unbind GL_ARRAY_BUFFER");
	} else {
		glBindVertexArray(0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_draw_sky: unbind glBindVertexArray");
	}
}

void RasterizerSceneGLES2::_update_sky_radiance(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	if (!is_environment(p_env)) {
		return;
	}

	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_env));
	if (!sky) {
		return;
	}

	GLES2::SkyMaterialData *material_data = nullptr;
	RID sky_material;

	RS::EnvironmentBG background = environment_get_background(p_env);

	if (sky) {
		sky_material = sky->material;
		if (sky_material.is_valid()) {
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material_data || !material_data->shader_data->valid) {
				material_data = nullptr;
			}
		}

		if (!material_data) {
			sky_material = sky_globals.default_material;
			material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}
	} else if (background == RS::ENV_BG_CLEAR_COLOR || background == RS::ENV_BG_COLOR) {
		sky_material = sky_globals.fog_material;
		material_data = static_cast<GLES2::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
	}

	if (!material_data) {
		return;
	}

	GLES2::SkyShaderData *shader_data = material_data->shader_data;
	if (!shader_data) {
		return;
	}

	bool update_single_frame = sky->mode == RS::SKY_MODE_REALTIME || sky->mode == RS::SKY_MODE_QUALITY;
	RS::SkyMode sky_mode = sky->mode;

	if (sky_mode == RS::SKY_MODE_AUTOMATIC) {
		if ((shader_data->uses_time || shader_data->uses_position) && sky->radiance_size == 256) {
			update_single_frame = true;
			sky_mode = RS::SKY_MODE_REALTIME;
		} else if (shader_data->uses_light || shader_data->ubo_size > 0) {
			update_single_frame = false;
			sky_mode = RS::SKY_MODE_INCREMENTAL;
		} else {
			update_single_frame = true;
			sky_mode = RS::SKY_MODE_QUALITY;
		}
	}

	if (sky->processing_layer == 0 && sky_mode == RS::SKY_MODE_INCREMENTAL) {
		// On the first frame after creating sky, rebuild in single frame
		update_single_frame = true;
		sky_mode = RS::SKY_MODE_QUALITY;
	}

	int max_processing_layer = sky->mipmap_count;

	// Update radiance cubemap
	if (sky->reflection_dirty && (sky->processing_layer >= max_processing_layer || update_single_frame)) {
		Projection cm;
		cm.set_perspective(90, 1, 0.01, 10.0);
		Projection correction;
		correction.set_depth_correction(true, true, false);
		cm = correction * cm;

		uint64_t sky_spec = 0;
		bool success = material_storage->shaders.sky_shader.version_bind_shader(shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		if (!success) {
			return;
		}

		material_data->bind_uniforms();

		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::POSITION, p_transform.origin, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::TIME, (float)time, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::PROJECTION, cm.columns[2][0], cm.columns[0][0], cm.columns[2][1], cm.columns[1][1], shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::LUMINANCE_MULTIPLIER, p_sky_energy_multiplier, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);

		_bind_sky_directional_lights(shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);

		GLint prev_viewport[4];
		glGetIntegerv(GL_VIEWPORT, prev_viewport);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glGetIntegerv GL_VIEWPORT");

		GLint prev_fbo;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glGetIntegerv GL_FRAMEBUFFER_BINDING");

		glViewport(0, 0, sky->radiance_size, sky->radiance_size);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glViewport");

		GLES2::TextureStorage::get_singleton()->bind_framebuffer(sky->radiance_framebuffer);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glBindFramebuffer");

		scene_state.reset_gl_state();
		scene_state.set_gl_cull_mode(RS::CULL_MODE_DISABLED);
		scene_state.enable_gl_blend(false);

		if (sky_globals.screen_triangle_array != 0) {
			glBindVertexArray(sky_globals.screen_triangle_array);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glBindVertexArray");
		} else {
			glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glBindBuffer");
			glEnableVertexAttribArray(RS::ARRAY_VERTEX);
			glVertexAttribPointer(RS::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glVertexAttribPointer");
		}

		for (int i = 0; i < 6; i++) {
			Basis local_view = Basis::looking_at(VIEW_NORMALS[i], VIEW_UP[i]);
			material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::ORIENTATION, local_view, shader_data->version, SkyShaderGLES2::MODE_CUBEMAP, sky_spec);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, sky->radiance, 0);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glFramebufferTexture2D");
			glDrawArrays(GL_TRIANGLES, 0, 3);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glDrawArrays");
		}

		if (sky_globals.screen_triangle_array == 0) {
			glDisableVertexAttribArray(RS::ARRAY_VERTEX);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: cleanup VBO");
		} else {
			glBindVertexArray(0);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: cleanup VAO");
		}

		// Fast path generation for cubemap lods natively
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: glGenerateMipmap");

		sky->processing_layer = 1;
		sky->baked_exposure = p_sky_energy_multiplier;
		sky->reflection_dirty = false;

		GLES2::TextureStorage::get_singleton()->bind_framebuffer(prev_fbo);
		glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_update_sky_radiance: restore GL state");

	} else {
		if (sky_mode == RS::SKY_MODE_INCREMENTAL && sky->processing_layer < max_processing_layer) {
			sky->processing_layer++;
		}
	}
}

Ref<Image> RasterizerSceneGLES2::sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	if (!sky) {
		return Ref<Image>();
	}

	_update_dirty_skys();

	if (sky->radiance == 0) {
		return Ref<Image>();
	}

	GLES2::CopyEffects *copy_effects = GLES2::CopyEffects::get_singleton();
	if (!copy_effects) {
		return Ref<Image>();
	}

	GLES2::Config *config = GLES2::Config::get_singleton();

	GLuint rad_tex = 0;
	glGenTextures(1, &rad_tex);
	glBindTexture(GL_TEXTURE_2D, rad_tex);

	bool use_float = config->float_texture_supported;
	if (use_float) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_size.width, p_size.height, 0, GL_RGBA, GL_FLOAT, nullptr);
		GLES2::Utilities::get_singleton()->texture_allocated_data(rad_tex, p_size.width * p_size.height * 16, "Temp sky panorama");
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_size.width, p_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		GLES2::Utilities::get_singleton()->texture_allocated_data(rad_tex, p_size.width * p_size.height * 4, "Temp sky panorama");
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: glTexImage2D");

	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: glGetIntegerv GL_FRAMEBUFFER_BINDING");

	GLuint rad_fbo = 0;
	glGenFramebuffers(1, &rad_fbo);
	GLES2::TextureStorage::get_singleton()->bind_framebuffer(rad_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rad_tex, 0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: glFramebufferTexture2D");

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: glBindTexture");

	GLint prev_viewport[4];
	glGetIntegerv(GL_VIEWPORT, prev_viewport);
	glViewport(0, 0, p_size.width, p_size.height);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: glViewport");

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: glClear");

	copy_effects->copy_cube_to_panorama(p_bake_irradiance ? float(sky->mipmap_count) : 0.0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: copy_cube_to_panorama");

	GLES2::TextureStorage::get_singleton()->bind_framebuffer(prev_fbo);
	glDeleteFramebuffers(1, &rad_fbo);
	glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::sky_bake_panorama: restore GL state");

	RID tex_rid = GLES2::TextureStorage::get_singleton()->texture_allocate();
	{
		GLES2::Texture texture;
		texture.width = p_size.width;
		texture.height = p_size.height;
		texture.alloc_width = p_size.width;
		texture.alloc_height = p_size.height;
		texture.format = use_float ? Image::FORMAT_RGBAF : Image::FORMAT_RGBA8;
		texture.real_format = use_float ? Image::FORMAT_RGBAF : Image::FORMAT_RGBA8;
		texture.gl_format_cache = GL_RGBA;
		texture.gl_type_cache = use_float ? GL_FLOAT : GL_UNSIGNED_BYTE;
		texture.type = GLES2::Texture::TYPE_2D;
		texture.target = GL_TEXTURE_2D;
		texture.active = true;
		texture.tex_id = rad_tex;
		texture.is_render_target = true; // HACK: Prevent TextureStorage from retaining a cached copy of the texture.
		GLES2::TextureStorage::get_singleton()->texture_2d_initialize_from_texture(tex_rid, texture);
	}

	Ref<Image> img = GLES2::TextureStorage::get_singleton()->texture_2d_get(tex_rid);
	GLES2::Utilities::get_singleton()->texture_free_data(rad_tex);

	GLES2::Texture *texture = GLES2::TextureStorage::get_singleton()->get_texture(tex_rid);
	if (texture) {
		texture->is_render_target = false; // HACK: Avoid an error when freeing the texture.
		texture->tex_id = 0;
	}
	GLES2::TextureStorage::get_singleton()->texture_free(tex_rid);

	if (img.is_valid()) {
		for (int i = 0; i < p_size.width; i++) {
			for (int j = 0; j < p_size.height; j++) {
				Color c = img->get_pixel(i, j);
				float a = c.a;
				c *= p_energy;
				c.a = a;
				img->set_pixel(i, j, c);
			}
		}
	}
	return img;
}

/* ENVIRONMENT API */

void RasterizerSceneGLES2::environment_glow_set_use_bicubic_upscale(bool p_enable) {
	glow_bicubic_upscale = p_enable;
}

void RasterizerSceneGLES2::environment_set_ssr_roughness_quality(RS::EnvironmentSSRRoughnessQuality p_quality) {
}

void RasterizerSceneGLES2::environment_set_ssao_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ssao_quality = p_quality;
}

void RasterizerSceneGLES2::environment_set_ssil_quality(RS::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_ray_count(RS::EnvironmentSDFGIRayCount p_ray_count) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_frames_to_converge(RS::EnvironmentSDFGIFramesToConverge p_frames) {
}

void RasterizerSceneGLES2::environment_set_sdfgi_frames_to_update_light(RS::EnvironmentSDFGIFramesToUpdateLight p_update) {
}

void RasterizerSceneGLES2::environment_set_volumetric_fog_volume_size(int p_size, int p_depth) {
}

void RasterizerSceneGLES2::environment_set_volumetric_fog_filter_active(bool p_enable) {
}

Ref<Image> RasterizerSceneGLES2::environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) {
	ERR_FAIL_COND_V(p_env.is_null(), Ref<Image>());

	RS::EnvironmentBG environment_background = environment_get_background(p_env);

	if (environment_background == RS::ENV_BG_CAMERA_FEED || environment_background == RS::ENV_BG_CANVAS || environment_background == RS::ENV_BG_KEEP) {
		return Ref<Image>(); // Nothing to bake.
	}

	RS::EnvironmentAmbientSource ambient_source = environment_get_ambient_source(p_env);

	bool use_ambient_light = false;
	bool use_cube_map = false;
	if (ambient_source == RS::ENV_AMBIENT_SOURCE_BG && (environment_background == RS::ENV_BG_CLEAR_COLOR || environment_background == RS::ENV_BG_COLOR)) {
		use_ambient_light = true;
	} else {
		use_cube_map = (ambient_source == RS::ENV_AMBIENT_SOURCE_BG && environment_background == RS::ENV_BG_SKY) || ambient_source == RS::ENV_AMBIENT_SOURCE_SKY;
		use_ambient_light = use_cube_map || ambient_source == RS::ENV_AMBIENT_SOURCE_COLOR;
	}

	use_cube_map = use_cube_map || (environment_background == RS::ENV_BG_SKY && environment_get_sky(p_env).is_valid());

	Color ambient_color;
	float ambient_color_sky_mix = 0.0;
	if (use_ambient_light) {
		ambient_color_sky_mix = environment_get_ambient_sky_contribution(p_env);
		const float ambient_energy = environment_get_ambient_light_energy(p_env);
		ambient_color = environment_get_ambient_light(p_env);
		ambient_color = ambient_color.srgb_to_linear();
		float a = ambient_color.a;
		ambient_color *= ambient_energy;
		ambient_color.a = a;
	}

	if (use_cube_map) {
		Ref<Image> panorama = sky_bake_panorama(environment_get_sky(p_env), environment_get_bg_energy_multiplier(p_env), p_bake_irradiance, p_size);
		if (use_ambient_light) {
			for (int x = 0; x < p_size.width; x++) {
				for (int y = 0; y < p_size.height; y++) {
					panorama->set_pixel(x, y, ambient_color.lerp(panorama->get_pixel(x, y), ambient_color_sky_mix));
				}
			}
		}
		return panorama;
	} else {
		const float bg_energy_multiplier = environment_get_bg_energy_multiplier(p_env);
		Color panorama_color = ((environment_background == RS::ENV_BG_CLEAR_COLOR) ? RSG::texture_storage->get_default_clear_color() : environment_get_bg_color(p_env));
		panorama_color = panorama_color.srgb_to_linear();
		float p_a = panorama_color.a;
		panorama_color *= bg_energy_multiplier;
		panorama_color.a = p_a;

		if (use_ambient_light) {
			panorama_color = ambient_color.lerp(panorama_color, ambient_color_sky_mix);
		}

		Ref<Image> panorama = Image::create_empty(p_size.width, p_size.height, false, Image::FORMAT_RGBAF);
		panorama->fill(panorama_color);
		return panorama;
	}
}

void RasterizerSceneGLES2::positional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.positional_shadow_quality = p_quality;
}

void RasterizerSceneGLES2::directional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.directional_shadow_quality = p_quality;
}

RID RasterizerSceneGLES2::fog_volume_instance_create(RID p_fog_volume) {
	return RID();
}

void RasterizerSceneGLES2::fog_volume_instance_set_transform(RID p_fog_volume_instance, const Transform3D &p_transform) {
}

void RasterizerSceneGLES2::fog_volume_instance_set_active(RID p_fog_volume_instance, bool p_active) {
}

RID RasterizerSceneGLES2::fog_volume_instance_get_volume(RID p_fog_volume_instance) const {
	return RID();
}

Vector3 RasterizerSceneGLES2::fog_volume_instance_get_position(RID p_fog_volume_instance) const {
	return Vector3();
}

RID RasterizerSceneGLES2::voxel_gi_instance_create(RID p_voxel_gi) {
	return RID();
}

void RasterizerSceneGLES2::voxel_gi_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform) {
}

bool RasterizerSceneGLES2::voxel_gi_needs_update(RID p_probe) const {
	return false;
}

void RasterizerSceneGLES2::voxel_gi_update(RID p_probe, bool p_update_light_instances, const Vector<RID> &p_light_instances, const PagedArray<RenderGeometryInstance *> &p_dynamic_objects) {
}

void RasterizerSceneGLES2::voxel_gi_set_quality(RS::VoxelGIQuality) {
}

/* BATCH API */

void RasterizerSceneGLES2::scene_render_items_implementation(GeometryInstanceSurface **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	for (int i = 0; i < p_count; i++) {
		// Skip depth-only pre-pass surfaces during the opaque color pass.
		if (
			bdata.pass_mode == PASS_MODE_COLOR &&
			!(p_surfaces[i]->flags & GeometryInstanceSurface::FLAG_PASS_OPAQUE)
		) {
			continue;
		}

		// Unshaded surfaces do not receive lighting; skip them during the pass-level
		// directional additive replay to avoid wasted shader binds and discarded draws.
		if (
			scene_state.is_directional_additive_replay &&
			p_surfaces[i]->shader &&
			p_surfaces[i]->shader->unshaded
		) {
			continue;
		}
		_render_single_item_immediate(p_surfaces[i]);
	}
}

void RasterizerSceneGLES2::_batch_get_hardware_limits(BatchType::BatchLimits &r_limits) {
	// In GLES2, we use vertex attributes for instancing.
	// So we set this to a high value so we don't prematurely flush.
	r_limits.max_matrix_palette_vectors = 0xFFFFFF;
	r_limits.max_vertices_per_buffer = 65536;
	r_limits.max_indices_per_buffer = 65536 * 2;
}

void RasterizerSceneGLES2::_batch_get_instance_geometry_capacity(const GeometryInstanceSurface *p_surface, uint32_t &r_vertex_count, uint32_t &r_index_count) {
	r_vertex_count = p_surface->vertex_cache.size();
	r_index_count = p_surface->index_cache.size();

	// Ensure unindexed geometry does not starve the index buffer requirement.
	if (r_index_count == 0) {
		r_index_count = r_vertex_count;
	}
}

float RasterizerSceneGLES2::_batch_get_item_depth(const GeometryInstanceSurface *p_surface, const Transform3D &p_camera_transform) {
	// Use the pre-validated per-instance depth computed once during the render list.
	return (p_surface && p_surface->owner) ? p_surface->owner->depth : 0.0f;
}

void RasterizerSceneGLES2::_batch_resolve_pass_surface_state(const GeometryInstanceSurface *p_surface, GLES2::SceneMaterialData *&r_material, GLES2::SceneShaderData *&r_shader, RS::CullMode &r_cull_mode) const {
	if (!p_surface) {
		r_material = nullptr;
		r_shader = nullptr;
		r_cull_mode = RS::CULL_MODE_BACK;
		return;
	}

	const bool use_shadow_override = (bdata.pass_mode == PASS_MODE_SHADOW);
	r_material = (use_shadow_override && p_surface->material_shadow) ? p_surface->material_shadow : p_surface->material;
	r_shader = (use_shadow_override && p_surface->shader_shadow) ? p_surface->shader_shadow : p_surface->shader;

	if (use_shadow_override && (p_surface->flags & GeometryInstanceSurface::FLAG_USES_DOUBLE_SIDED_SHADOWS)) {
		r_cull_mode = RS::CULL_MODE_DISABLED;
	} else if (p_surface->shader) {
		r_cull_mode = p_surface->shader->cull_mode;
	} else if (r_shader) {
		r_cull_mode = r_shader->cull_mode;
	} else {
		r_cull_mode = RS::CULL_MODE_BACK;
	}
}

uint64_t RasterizerSceneGLES2::_batch_get_state_hash(const GeometryInstanceSurface *p_surface) {
	GLES2::SceneMaterialData *active_mat = nullptr;
	GLES2::SceneShaderData *active_shader = nullptr;
	RS::CullMode active_cull = RS::CULL_MODE_BACK;

	// First resolve surface pass.
	_batch_resolve_pass_surface_state(p_surface, active_mat, active_shader, active_cull);

	uint64_t hash = 0;
	// Bits 63-62: Cull mode (2 bits)
	// Bits 61-52: Shader version (10 bits)
	uint64_t cull_bits = static_cast<uint64_t>(active_cull) & 0x3;
	uint64_t shader_id = active_shader ? active_shader->version.get_id() : 0;
	hash |= (cull_bits << 62);
	hash |= (shader_id & 0x3FF) << 52;

	// Bits 51-32: Material ID (texture bindings, uniform blocks, 20 bits)
	uint64_t mat_id = active_mat ? static_cast<uint64_t>((uintptr_t)active_mat >> 4) : 0;
	hash |= (mat_id & 0xFFFFF) << 32;

	// Bit 31: Has texture (1 bit)
	// Not used for state toggles, but keeps hashes the same between GLES1 and GLES2
	uint64_t has_texture = 0;
	hash |= (has_texture & 0x1) << 31;

	// Bits 30-16: Base Type (15 bits)
	uint64_t base_type = p_surface->owner->data->base_type;
	hash |= (base_type & 0x7FFF) << 16;

	// Bits 15-0: Primitive topology (16 bits)
	uint64_t primitive = p_surface->primitive;
	hash |= (primitive & 0xFFFF);

	return hash;
}

uint32_t RasterizerSceneGLES2::_batch_get_surface_flags(const GeometryInstanceSurface *p_surface) const {
	// Inspects the surface's mesh caches, shader input mask, and MultiMesh configuration
	// to build a bitmask of BatchFlags3D. Used during Stage 1 joining to determine the
	// minimal FVF tier needed for the batch.
	uint32_t flags = BatcherEnums::BTF_3D_NONE;

	GLES2::SceneMaterialData *active_mat = nullptr;
	GLES2::SceneShaderData *active_shader = nullptr;
	RS::CullMode active_cull = RS::CULL_MODE_BACK;
	_batch_resolve_pass_surface_state(p_surface, active_mat, active_shader, active_cull);

	const bool is_depth_pass = _batch_util_is_depth_pass();

	if (p_surface->owner && p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH) {
		flags |= BatcherEnums::BTF_3D_USES_INSTANCING;
		if (!is_depth_pass) {
			GLES2::MultiMesh *mm = GLES2::MeshStorage::get_singleton()->get_multimesh(p_surface->owner->data->base);
			if (mm && mm->uses_colors) {
				flags |= BatcherEnums::BTF_3D_USES_COLORS;
			}
		}
	}

	if (!is_depth_pass) {
		if (!p_surface->color_cache.is_empty()) {
			if (!active_shader || (active_shader->vertex_input_mask & (1ULL << RS::ARRAY_COLOR))) {
				flags |= BatcherEnums::BTF_3D_USES_COLORS;
			}
		}

		if (!p_surface->uv2_cache.is_empty() || bdata.pass_mode == PASS_MODE_MATERIAL) {
			flags |= BatcherEnums::BTF_3D_USES_UV2;
		}

		if (!p_surface->tangent_cache.is_empty()) {
			if (!active_shader || (active_shader->vertex_input_mask & (1ULL << RS::ARRAY_TANGENT)) || active_shader->uses_normal) {
				flags |= BatcherEnums::BTF_3D_USES_TANGENTS;
			}
		}
	}

	if (
		active_shader &&
		(active_shader->uses_alpha_clip ||
		 active_shader->uses_depth_prepass_alpha ||
		 active_shader->uses_discard)
	) {
		flags |= BatcherEnums::BTF_3D_USES_ALPHA_CLIP;
		if (is_depth_pass && !p_surface->color_cache.is_empty()) {
			if (active_shader->vertex_input_mask & (1ULL << RS::ARRAY_COLOR)) {
				flags |= BatcherEnums::BTF_3D_USES_COLORS;
			}
		}
	}

	// Prevent CPU vertex baking and multi-item joining if the shader reads/writes VERTEX, POSITION,
	// uses world coordinates, or writes projection/modelview matrices in GLSL, so its hardware
	// WORLD_TRANSFORM uniform stays intact and is not applied twice.
	if (_shader_prevents_vertex_baking(active_shader)) {
		flags |= BatcherEnums::BTF_3D_PREVENT_VERTEX_BAKING | BatcherEnums::BTF_3D_PREVENT_ITEM_JOINING;
		if (flags & BatcherEnums::BTF_3D_USES_INSTANCING) {
			// Promote MultiMesh with custom vertex shader to Tier 4 instancing (32 instances/draw).
			flags |= BatcherEnums::BTF_3D_USES_SKINNING;
		}
	}

	return flags;
}

RasterizerSceneGLES2::BatchType::BatchColor RasterizerSceneGLES2::_batch_get_surface_modulate(const GeometryInstanceSurface *p_surface) const {
	// This is not needed for the GLES2 driver, as everything is done
	// via shader uniforms instead of FFP vertex colour baking.

	// Return default white colour.
	BatchType::BatchColor col{};
	col.set_white();
	return col;
}

GLES2::SceneMaterialData *RasterizerSceneGLES2::_batch_get_material_data(const GeometryInstanceSurface *p_surface) {
	GLES2::SceneMaterialData *active_mat = nullptr;
	GLES2::SceneShaderData *active_shader = nullptr;
	RS::CullMode active_cull = RS::CULL_MODE_BACK;
	_batch_resolve_pass_surface_state(p_surface, active_mat, active_shader, active_cull);
	return active_mat;
}

bool RasterizerSceneGLES2::_batch_can_use_single_mesh_fallback(const GeometryInstanceSurface *p_surface) const {
	if (!p_surface || !p_surface->surface || !p_surface->owner) {
		return false;
	}

	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();
	if (mesh_storage->mesh_surface_get_vertex_buffer(p_surface->surface) == 0) {
		return false;
	}

	// Ensure indexed surfaces have a valid static IBO and supported index type.
	const uint32_t index_count = mesh_storage->mesh_surface_get_index_count(p_surface->surface, p_surface->lod_index);
	if (index_count > 0) {
		if (mesh_storage->mesh_surface_get_index_buffer(p_surface->surface, p_surface->lod_index) == 0) {
			return false;
		}
		const GLenum index_type = mesh_storage->mesh_surface_get_index_type(p_surface->surface);
		if (index_type == GL_UNSIGNED_INT && !GLES2_CONFIG->support_32_bits_indices) {
			// Cannot perform if we don't support 32-bit indices
			return false;
		}
	}

	return true;
}

void RasterizerSceneGLES2::_batch_prepare_surface_world_cache(const GeometryInstanceSurface *p_surface, const BatchType::BItemRef3D &p_ref, BatchType::TransformMode3D p_effective_mode, bool p_need_tangents) {
	const uint32_t v_count = p_surface->vertex_cache.size();
	const Vector3 *pos_ptr = p_surface->vertex_cache.ptr();
	const Vector3 *norm_ptr = p_surface->normal_cache.size() > 0 ? p_surface->normal_cache.ptr() : nullptr;
	const float *tan_ptr = p_surface->tangent_cache.size() > 0 ? p_surface->tangent_cache.ptr() : nullptr;
	const Vector2 *uv_ptr = p_surface->uv_cache.size() > 0 ? p_surface->uv_cache.ptr() : nullptr;

	const uint32_t xform_hash = BatchType::_hash_transform_3d(p_ref.transform);
	const bool cache_valid = (
		p_surface->cached_transform_hash == xform_hash &&
		p_surface->baked_world_cache.size() == v_count &&
		p_surface->cached_transform == p_ref.transform
	);

	if (!cache_valid) {
		p_surface->baked_world_cache.resize(v_count);
		BatchType::BatchVertex3DTier1 *RESTRICT cache_base = p_surface->baked_world_cache.ptr();
		for (uint32_t i = 0; i < v_count; i++) {
			_batch_transform_pos_norm(cache_base[i].pos, cache_base[i].normal, pos_ptr[i], norm_ptr, i, p_effective_mode, p_ref.transform, p_ref.normal_cofactor);
			if (uv_ptr) {
				cache_base[i].uv.set(uv_ptr[i]);
			} else {
				cache_base[i].uv.set(0.0f, 0.0f);
			}
		}
		p_surface->baked_tangent_cache.clear();
		p_surface->cached_transform = p_ref.transform;
		p_surface->cached_transform_hash = xform_hash;
	}

	if (p_need_tangents && p_surface->baked_tangent_cache.size() != v_count) {
		p_surface->baked_tangent_cache.resize(v_count);
		BatchType::BatchTangent *RESTRICT cache_tan = p_surface->baked_tangent_cache.ptr();
		for (uint32_t i = 0; i < v_count; i++) {
			_batch_transform_tangent(cache_tan[i], tan_ptr, i, p_effective_mode, p_ref.transform, p_ref.normal_cofactor);
		}
	}
}

void RasterizerSceneGLES2::_batch_fill_instance_tiered(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index) {
	// Writes transformed vertices directly into the interleaved target FVF tier.
	const GeometryInstanceSurface *p_surface = static_cast<const GeometryInstanceSurface *>(p_ref.surface);
	if (!p_surface || !p_surface->surface) {
		return;
	}

	const PackedVector3Array &positions = p_surface->vertex_cache;
	if (positions.is_empty()) {
		return;
	}

	const uint32_t v_count = positions.size();
	const Vector3 *pos_ptr = positions.ptr();
	const Vector3 *norm_ptr = p_surface->normal_cache.size() > 0 ? p_surface->normal_cache.ptr() : nullptr;
	const float *tan_ptr = p_surface->tangent_cache.size() > 0 ? p_surface->tangent_cache.ptr() : nullptr;
	const Vector2 *uv_ptr = p_surface->uv_cache.size() > 0 ? p_surface->uv_cache.ptr() : nullptr;
	const Vector2 *uv2_ptr = p_surface->uv2_cache.size() > 0 ? p_surface->uv2_cache.ptr() : nullptr;
	const Color *col_ptr = p_surface->color_cache.size() > 0 ? p_surface->color_cache.ptr() : nullptr;
	const PackedInt32Array &indices = p_surface->index_cache;

	const BatchType::TransformMode3D effective_mode = p_use_hardware_transform ? BatchType::TM_3D_NONE : p_ref.transform_mode;
	const uint8_t m_idx = static_cast<uint8_t>(p_item_index & 0xFF);

	const BatchType::BatchVertex3DTier1 *RESTRICT cache_base = nullptr;
	const BatchType::BatchTangent *RESTRICT cache_tan = nullptr;
	if (!p_use_hardware_transform) {
		_batch_prepare_surface_world_cache(p_surface, p_ref, effective_mode, bdata.fvf >= BatcherEnums::FVF_3D_TIER3_TANGENT);
		cache_base = p_surface->baked_world_cache.ptr();
		if (bdata.fvf >= BatcherEnums::FVF_3D_TIER3_TANGENT) {
			cache_tan = p_surface->baked_tangent_cache.ptr();
		}
		if (bdata.fvf == BatcherEnums::FVF_3D_TIER1_BASE) {
			memcpy(r_bvs, cache_base, v_count * sizeof(BatchType::BatchVertex3DTier1));
			_batch_fill_indices_single(r_inds, indices, v_count, p_start_vert);
			return;
		}
	}

	if (bdata.fvf == BatcherEnums::FVF_3D_TIER1_BASE) {
		_batch_fill_tier(reinterpret_cast<BatchType::BatchVertex3DTier1 *>(r_bvs), v_count, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, cache_base, cache_tan, p_ref, m_idx);
	} else if (bdata.fvf == BatcherEnums::FVF_3D_TIER2_COLOR) {
		_batch_fill_tier(reinterpret_cast<BatchType::BatchVertex3DTier2 *>(r_bvs), v_count, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, cache_base, cache_tan, p_ref, m_idx);
	} else if (bdata.fvf == BatcherEnums::FVF_3D_TIER3_TANGENT) {
		_batch_fill_tier(reinterpret_cast<BatchType::BatchVertex3DTier3 *>(r_bvs), v_count, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, cache_base, cache_tan, p_ref, m_idx);
	} else {
		_batch_fill_tier(reinterpret_cast<BatchType::BatchVertex3DTier4 *>(r_bvs), v_count, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, cache_base, cache_tan, p_ref, m_idx);
	}

	_batch_fill_indices_single(r_inds, indices, v_count, p_start_vert);
}

void RasterizerSceneGLES2::_batch_fill_multimesh_tiered(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index) {
	// Two-pass SoA MultiMesh expansion directly into interleaved unit_vertices.
	const GeometryInstanceSurface *p_surface = static_cast<const GeometryInstanceSurface *>(p_ref.surface);
	if (!p_surface || !p_surface->surface || p_ref.instance_count == 0) {
		return;
	}

	const PackedVector3Array &positions = p_surface->vertex_cache;
	if (positions.is_empty()) {
		return;
	}

	const uint32_t v_count = positions.size();
	const Vector3 *pos_ptr = positions.ptr();
	const Vector3 *norm_ptr = p_surface->normal_cache.size() > 0 ? p_surface->normal_cache.ptr() : nullptr;
	const float *tan_ptr = p_surface->tangent_cache.size() > 0 ? p_surface->tangent_cache.ptr() : nullptr;
	const Vector2 *uv_ptr = p_surface->uv_cache.size() > 0 ? p_surface->uv_cache.ptr() : nullptr;
	const Vector2 *uv2_ptr = p_surface->uv2_cache.size() > 0 ? p_surface->uv2_cache.ptr() : nullptr;
	const Color *col_ptr = p_surface->color_cache.size() > 0 ? p_surface->color_cache.ptr() : nullptr;
	const PackedInt32Array &indices = p_surface->index_cache;

	const int instances = p_ref.instance_count;
	MultiMeshInstanceData mm = _get_multimesh_data(p_surface);
	const bool is_multimesh = (p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH && mm.data != nullptr);
	const float *mm_chunk_data = is_multimesh ? (mm.data + (p_ref.instance_offset * mm.stride)) : nullptr;
	const bool need_normals = (norm_ptr != nullptr || tan_ptr != nullptr);

	// Sequential Pass 1: Pre-compute contiguous SoA streams of instance transforms,
	// inverse-transpose (cofactor) normal bases, and quantized colors before vertex expansion.
	bdata.multimesh_xform_soa.clear();
	bdata.multimesh_xform_soa.reserve(instances);

	LocalVector<Transform3D> &xform_stream = bdata.multimesh_xform_soa.get_stream<0>();
	LocalVector<Basis> &cofactor_stream = bdata.multimesh_xform_soa.get_stream<1>();
	LocalVector<BatchType::BatchColor> &color_stream = bdata.multimesh_xform_soa.get_stream<2>();
	LocalVector<BatchType::TransformMode3D> &mode_stream = bdata.multimesh_xform_soa.get_stream<3>();

	xform_stream.resize(instances);
	if (need_normals) {
		cofactor_stream.resize(instances);
	}
	color_stream.resize(instances);
	mode_stream.resize(instances);

	Transform3D *RESTRICT xform_dst = xform_stream.ptr();
	Basis *RESTRICT cofactor_dst = need_normals ? cofactor_stream.ptr() : nullptr;
	BatchType::BatchColor *RESTRICT color_dst = color_stream.ptr();
	BatchType::TransformMode3D *RESTRICT mode_dst = mode_stream.ptr();

	const bool prevent_vertex_baking = _shader_prevents_vertex_baking(p_surface->shader);

	for (int inst = 0; inst < instances; inst++) {
		Transform3D xform;
		Color raw_color(1.0f, 1.0f, 1.0f, 1.0f);
		if (is_multimesh) {
			_gl_batch_decode_multimesh_instance(mm_chunk_data + (inst * mm.stride), mm.format, mm.uses_colors, mm.color_offset, xform, raw_color);
		}

		const Transform3D world_xform = prevent_vertex_baking ? Transform3D() : (p_use_hardware_transform ? xform : (p_ref.transform * xform));
		float inv_scale = 1.0f;
		const BatchType::TransformMode3D mode = prevent_vertex_baking ? BatchType::TM_3D_NONE : BatchType::_find_transform_mode_3d(world_xform, &inv_scale);
		xform_dst[inst] = world_xform;
		mode_dst[inst] = mode;

		if (cofactor_dst) {
			if (mode == BatchType::TM_3D_UNIFORM_SCALE) {
				cofactor_dst[inst] = world_xform.basis * inv_scale;
			} else if (mode == BatchType::TM_3D_ALL) {
				cofactor_dst[inst] = BatchType::_compute_normal_cofactor(world_xform.basis);
			} else {
				cofactor_dst[inst] = Basis();
			}
		}

		color_dst[inst].set(raw_color);
	}

	const uint8_t m_idx = static_cast<uint8_t>(p_item_index & 0xFF);

	// Sequential Pass 2: Populate aligned FVF staging buffer from contiguous SoA streams.
	if (bdata.fvf == BatcherEnums::FVF_3D_TIER1_BASE) {
		_batch_fill_multimesh_tier(reinterpret_cast<BatchType::BatchVertex3DTier1 *>(r_bvs), instances, v_count, is_multimesh, mm.uses_colors, mm_chunk_data, mm.stride, mm.color_offset, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, xform_dst, mode_dst, cofactor_dst, color_dst, m_idx);
	} else if (bdata.fvf == BatcherEnums::FVF_3D_TIER2_COLOR) {
		_batch_fill_multimesh_tier(reinterpret_cast<BatchType::BatchVertex3DTier2 *>(r_bvs), instances, v_count, is_multimesh, mm.uses_colors, mm_chunk_data, mm.stride, mm.color_offset, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, xform_dst, mode_dst, cofactor_dst, color_dst, m_idx);
	} else if (bdata.fvf == BatcherEnums::FVF_3D_TIER3_TANGENT) {
		_batch_fill_multimesh_tier(reinterpret_cast<BatchType::BatchVertex3DTier3 *>(r_bvs), instances, v_count, is_multimesh, mm.uses_colors, mm_chunk_data, mm.stride, mm.color_offset, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, xform_dst, mode_dst, cofactor_dst, color_dst, m_idx);
	} else {
		_batch_fill_multimesh_tier(reinterpret_cast<BatchType::BatchVertex3DTier4 *>(r_bvs), instances, v_count, is_multimesh, mm.uses_colors, mm_chunk_data, mm.stride, mm.color_offset, pos_ptr, norm_ptr, tan_ptr, uv_ptr, uv2_ptr, col_ptr, xform_dst, mode_dst, cofactor_dst, color_dst, m_idx);
	}

	_batch_fill_indices_multimesh(r_inds, indices, v_count, instances, p_start_vert, p_surface->primitive);
}

void RasterizerSceneGLES2::_batch_fill_instance_depth(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index) {
	// Packs minimal depth-only (16-byte) or alpha-tested depth (32-byte) vertices
	// directly into unit_vertices for shadow and depth pre-passes, bypassing normal/color/tangent streams.
	const GeometryInstanceSurface *p_surface = static_cast<const GeometryInstanceSurface *>(p_ref.surface);
	const PackedVector3Array &positions = p_surface->vertex_cache;
	if (positions.is_empty()) {
		return;
	}

	const uint32_t v_count = positions.size();
	const Vector3 *pos_ptr = positions.ptr();
	const Vector2 *uv_ptr = p_surface->uv_cache.size() > 0 ? p_surface->uv_cache.ptr() : nullptr;
	const PackedInt32Array &indices = p_surface->index_cache;
	const BatchType::TransformMode3D effective_mode = p_use_hardware_transform ? BatchType::TM_3D_NONE : p_ref.transform_mode;
	const uint8_t m_idx = static_cast<uint8_t>(p_item_index);

	const uint32_t xform_hash = p_use_hardware_transform ? 0U : BatchType::_hash_transform_3d(p_ref.transform);
	const bool can_reuse_world_cache = (
		!p_use_hardware_transform &&
		p_surface->cached_transform_hash == xform_hash &&
		p_surface->baked_world_cache.size() == v_count &&
		p_surface->cached_transform == p_ref.transform
	);
	const BatchType::BatchVertex3DTier1 *RESTRICT world_cache = can_reuse_world_cache ? p_surface->baked_world_cache.ptr() : nullptr;

	const Color *col_ptr = p_surface->color_cache.size() > 0 ? p_surface->color_cache.ptr() : nullptr;

	if (_batch_util_is_depth_alpha_fvf(bdata.fvf)) {
		_batch_fill_depth(reinterpret_cast<BatchType::BatchVertex3DDepthAlphaMinimal *>(r_bvs), v_count, pos_ptr, uv_ptr, col_ptr, p_ref.modulate, world_cache, effective_mode, p_ref.transform, m_idx);
	} else {
		_batch_fill_depth(reinterpret_cast<BatchType::BatchVertex3DDepthMinimal *>(r_bvs), v_count, pos_ptr, uv_ptr, col_ptr, p_ref.modulate, world_cache, effective_mode, p_ref.transform, m_idx);
	}

	_batch_fill_indices_single(r_inds, indices, v_count, p_start_vert);
}

void RasterizerSceneGLES2::_batch_fill_multimesh_depth(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index) {
	// Expands MultiMesh instances directly into minimal depth-only (16-byte) or
	// alpha-tested depth (32-byte) vertices using per-instance TransformMode3D fast-paths.
	const GeometryInstanceSurface *p_surface = static_cast<const GeometryInstanceSurface *>(p_ref.surface);
	if (!p_surface || !p_surface->surface || p_ref.instance_count == 0) {
		return;
	}

	const PackedVector3Array &positions = p_surface->vertex_cache;
	if (positions.is_empty()) {
		return;
	}

	const uint32_t v_count = positions.size();
	const Vector3 *pos_ptr = positions.ptr();
	const Vector2 *uv_ptr = p_surface->uv_cache.size() > 0 ? p_surface->uv_cache.ptr() : nullptr;
	const Color *col_ptr = p_surface->color_cache.size() > 0 ? p_surface->color_cache.ptr() : nullptr;
	const PackedInt32Array &indices = p_surface->index_cache;
	const int instances = p_ref.instance_count;
	MultiMeshInstanceData mm = _get_multimesh_data(p_surface);
	const bool is_multimesh = (p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH && mm.data != nullptr);
	const float *mm_chunk_data = is_multimesh ? (mm.data + (p_ref.instance_offset * mm.stride)) : nullptr;
	const uint8_t m_idx = static_cast<uint8_t>(p_item_index);

	const bool prevent_vertex_baking = _shader_prevents_vertex_baking(p_surface->shader);
	const bool use_local_xform = p_use_hardware_transform || prevent_vertex_baking;
	const bool keep_unbaked = is_multimesh && prevent_vertex_baking;

	if (_batch_util_is_depth_alpha_fvf(bdata.fvf)) {
		_batch_fill_multimesh_depth_vertices(reinterpret_cast<BatchType::BatchVertex3DDepthAlphaMinimal *>(r_bvs), instances, v_count, is_multimesh && !keep_unbaked, mm_chunk_data, mm.stride, mm.format, use_local_xform, p_ref.transform, pos_ptr, uv_ptr, col_ptr, p_ref.modulate, m_idx);
	} else {
		_batch_fill_multimesh_depth_vertices(reinterpret_cast<BatchType::BatchVertex3DDepthMinimal *>(r_bvs), instances, v_count, is_multimesh && !keep_unbaked, mm_chunk_data, mm.stride, mm.format, use_local_xform, p_ref.transform, pos_ptr, uv_ptr, col_ptr, p_ref.modulate, m_idx);
	}

	_batch_fill_indices_multimesh(r_inds, indices, v_count, instances, p_start_vert, p_surface->primitive);
}

bool RasterizerSceneGLES2::_batch_try_reuse_cached_vbo_slot_3d(BatchType::Batch3D &r_batch, uint64_t p_content_hash) {
	// Don't reuse for Android emulators since they may not like
	// or speediness.
	if (GLES2_CONFIG->is_android_emulator || p_content_hash == 0) {
		return false;
	}

	// Fast-path: check current sequential slot cursor first.
	if (bdata.current_vbo_index < bdata.vbo_pool.size()) {
		const BatchType::VBOPool &pool = bdata.vbo_pool[bdata.current_vbo_index];
		if (pool.gl_vertex_buffer != 0 && pool.gl_index_buffer != 0) {
			if (bdata.current_vbo_slot_index < pool.cached_slots.size()) {
				const BatchType::CachedBatchSlot3D &slot = pool.cached_slots[bdata.current_vbo_slot_index];
				if (
					slot.content_hash == p_content_hash &&
					slot.vbo_byte_offset == bdata.current_vbo_byte_offset &&
					slot.ibo_byte_offset == bdata.current_ibo_byte_offset
				) {
					r_batch.vbo_index = bdata.current_vbo_index;
					r_batch.vbo_byte_offset = slot.vbo_byte_offset;
					r_batch.ibo_byte_offset = slot.ibo_byte_offset;
					r_batch.num_verts = slot.num_verts;
					r_batch.num_indices = slot.num_indices;
					r_batch.fvf = slot.fvf;
					r_batch.flags = slot.flags;

					bdata.current_vbo_byte_offset += slot.aligned_vbo_bytes;
					bdata.current_ibo_byte_offset += slot.aligned_ibo_bytes;
					bdata.current_vbo_slot_index++;
					return true;
				}
			}
		}
	}

	// Search all existing valid VBO pools for any cached slot with matching content_hash.
	// This allows 32-instance Tier 4 MultiMesh chunks and depth-reordered static batches
	// to share or reuse already-uploaded VBO data without re-uploading or invalidating the pool.
	for (uint32_t p = 0; p < bdata.vbo_pool.size(); p++) {
		const BatchType::VBOPool &search_pool = bdata.vbo_pool[p];
		if (search_pool.gl_vertex_buffer == 0 || search_pool.gl_index_buffer == 0) {
			continue;
		}
		const uint32_t slot_count = search_pool.cached_slots.size();
		for (uint32_t s = 0; s < slot_count; s++) {
			const BatchType::CachedBatchSlot3D &slot = search_pool.cached_slots[s];
			if (slot.content_hash == p_content_hash) {
				r_batch.vbo_index = p;
				r_batch.vbo_byte_offset = slot.vbo_byte_offset;
				r_batch.ibo_byte_offset = slot.ibo_byte_offset;
				r_batch.num_verts = slot.num_verts;
				r_batch.num_indices = slot.num_indices;
				r_batch.fvf = slot.fvf;
				r_batch.flags = slot.flags;

				// Advance the write cursor past this slot if it resides in the active pool
				// so subsequent new uploads append cleanly after it.
				if (p == bdata.current_vbo_index && s >= bdata.current_vbo_slot_index) {
					bdata.current_vbo_byte_offset = slot.vbo_byte_offset + slot.aligned_vbo_bytes;
					bdata.current_ibo_byte_offset = slot.ibo_byte_offset + slot.aligned_ibo_bytes;
					bdata.current_vbo_slot_index = s + 1;
				} else if (p > bdata.current_vbo_index) {
					bdata.current_vbo_index = p;
					bdata.current_vbo_byte_offset = slot.vbo_byte_offset + slot.aligned_vbo_bytes;
					bdata.current_ibo_byte_offset = slot.ibo_byte_offset + slot.aligned_ibo_bytes;
					bdata.current_vbo_slot_index = s + 1;
				}
				return true;
			}
		}
	}

	return false;
}

void RasterizerSceneGLES2::_batch_upload_buffers_3d(BatchType::Batch3D &r_batch, uint64_t p_content_hash) {
	const uint32_t max_vbo_bytes = bdata.unit_vertices.max_size() * sizeof(BatchType::BatchVertex3DTier4);
	const uint32_t max_ibo_bytes = bdata.indices.max_size() * sizeof(uint16_t);
	const uint32_t bytes_to_upload = bdata.total_verts * bdata.unit_vertices.get_unit_size_bytes();
	const uint32_t indices_bytes_to_upload = bdata.total_indices * sizeof(uint16_t);
	const uint32_t aligned_vbo_bytes = (bytes_to_upload + 15U) & ~15U;
	const uint32_t aligned_ibo_bytes = (indices_bytes_to_upload + 3U) & ~3U;

	if (GLES2_CONFIG->is_android_emulator) {
		bdata.current_vbo_byte_offset = 0;
		bdata.current_ibo_byte_offset = 0;
		bdata.current_vbo_slot_index = 0;
	} else {
		// If the current pool still has valid cached slots from earlier this frame or last frame,
		// append after the last cached slot.
		if (
			bdata.current_vbo_index < bdata.vbo_pool.size() &&
			bdata.current_vbo_slot_index < bdata.vbo_pool[bdata.current_vbo_index].cached_slots.size()
		) {
			const BatchType::VBOPool &cur_pool = bdata.vbo_pool[bdata.current_vbo_index];
			const BatchType::CachedBatchSlot3D &last_slot = cur_pool.cached_slots[cur_pool.cached_slots.size() - 1];
			bdata.current_vbo_byte_offset = last_slot.vbo_byte_offset + last_slot.aligned_vbo_bytes;
			bdata.current_ibo_byte_offset = last_slot.ibo_byte_offset + last_slot.aligned_ibo_bytes;
			bdata.current_vbo_slot_index = cur_pool.cached_slots.size();
		}

		while (
			bdata.current_vbo_index < bdata.vbo_pool.size() &&
			(bdata.current_vbo_byte_offset > 0 || bdata.current_ibo_byte_offset > 0) &&
			((bdata.current_vbo_byte_offset + aligned_vbo_bytes > max_vbo_bytes) ||
				(bdata.current_ibo_byte_offset + aligned_ibo_bytes > max_ibo_bytes))
		) {
			bdata.current_vbo_index++;
			if (bdata.current_vbo_index < bdata.vbo_pool.size() && bdata.vbo_pool[bdata.current_vbo_index].cached_slots.size() > 0) {
				const BatchType::VBOPool &next_pool = bdata.vbo_pool[bdata.current_vbo_index];
				const BatchType::CachedBatchSlot3D &last_slot = next_pool.cached_slots[next_pool.cached_slots.size() - 1];
				bdata.current_vbo_byte_offset = last_slot.vbo_byte_offset + last_slot.aligned_vbo_bytes;
				bdata.current_ibo_byte_offset = last_slot.ibo_byte_offset + last_slot.aligned_ibo_bytes;
				bdata.current_vbo_slot_index = next_pool.cached_slots.size();
			} else {
				bdata.current_vbo_byte_offset = 0;
				bdata.current_ibo_byte_offset = 0;
				bdata.current_vbo_slot_index = 0;
			}
		}
	}

	// Grow the VBO pool on demand so multi-pass replays (such as _render_uv2)
	// retain every batch's VBO/IBO intact across the pass.
	bool pool_created = false;
	if (bdata.current_vbo_index >= bdata.vbo_pool.size()) {
		BatchType::VBOPool new_pool{};
		glGenBuffers(1, &new_pool.gl_vertex_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, new_pool.gl_vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, max_vbo_bytes, nullptr, GL_DYNAMIC_DRAW);

		glGenBuffers(1, &new_pool.gl_index_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, new_pool.gl_index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, max_ibo_bytes, nullptr, GL_DYNAMIC_DRAW);

		bdata.vbo_pool.push_back(new_pool);
		pool_created = true;

		scene_state.current_array_buffer = new_pool.gl_vertex_buffer;
		scene_state.current_element_array_buffer = new_pool.gl_index_buffer;
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_upload_buffers: pool initialization");
	}

	r_batch.vbo_index = bdata.current_vbo_index;
	r_batch.vbo_byte_offset = bdata.current_vbo_byte_offset;
	r_batch.ibo_byte_offset = bdata.current_ibo_byte_offset;
	BatchType::VBOPool &pool = bdata.vbo_pool[bdata.current_vbo_index];

	if (pool.gl_vertex_buffer != 0) {
		const bool is_sub_alloc = (
			!GLES2_CONFIG->is_android_emulator &&
			(pool_created || bdata.current_vbo_byte_offset > 0 || bdata.current_ibo_byte_offset > 0)
		);

		// If we orphan at offset 0, all subsequent cached slots in this VBO are invalidated.
		if (!is_sub_alloc) {
			pool.cached_slots.clear();
		} else if (bdata.current_vbo_slot_index < pool.cached_slots.size()) {
			// A new upload at this offset invalidates any downstream cached slots that might overlap.
			pool.cached_slots.resize(bdata.current_vbo_slot_index);
		}

		// Orphan the vertex buffer
		scene_state.bind_gl_array_buffer(pool.gl_vertex_buffer);
		uint32_t vbo_alloc = GLES2_CONFIG->is_android_emulator ? bytes_to_upload : max_vbo_bytes;
		_batch_buffer_orphan_and_upload_3d(vbo_alloc, r_batch.vbo_byte_offset, bytes_to_upload, bdata.unit_vertices.get_data(), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, is_sub_alloc);

		// Orphan the index buffer
		scene_state.bind_gl_element_array_buffer(pool.gl_index_buffer);
		uint32_t ibo_alloc = GLES2_CONFIG->is_android_emulator ? indices_bytes_to_upload : max_ibo_bytes;
		_batch_buffer_orphan_and_upload_3d(ibo_alloc, r_batch.ibo_byte_offset, indices_bytes_to_upload, bdata.indices.get_data(), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW, is_sub_alloc);
	}

	if (GLES2_CONFIG->is_android_emulator) {
		bdata.current_vbo_index++;
	} else {
		BatchType::CachedBatchSlot3D slot{};
		slot.content_hash = p_content_hash;
		slot.vbo_byte_offset = r_batch.vbo_byte_offset;
		slot.ibo_byte_offset = r_batch.ibo_byte_offset;
		slot.aligned_vbo_bytes = aligned_vbo_bytes;
		slot.aligned_ibo_bytes = aligned_ibo_bytes;
		slot.num_verts = r_batch.num_verts;
		slot.num_indices = r_batch.num_indices;
		slot.fvf = r_batch.fvf;
		slot.flags = r_batch.flags;

		if (bdata.current_vbo_slot_index < pool.cached_slots.size()) {
			pool.cached_slots[bdata.current_vbo_slot_index] = slot;
		} else {
			pool.cached_slots.push_back(slot);
		}
		bdata.current_vbo_slot_index++;

		bdata.current_vbo_byte_offset += aligned_vbo_bytes;
		bdata.current_ibo_byte_offset += aligned_ibo_bytes;
	}
}

_FORCE_INLINE_ bool RasterizerSceneGLES2::_batch_buffer_orphan_and_upload_3d(unsigned int p_buffer_size_bytes, unsigned int p_offset_bytes, unsigned int p_data_size_bytes, const void *p_data, GLenum p_target, GLenum p_usage, bool p_optional_orphan) const {
	ERR_FAIL_COND_V((p_offset_bytes + p_data_size_bytes) > p_buffer_size_bytes, false);

	if (!p_optional_orphan) {
		if (GLES2_CONFIG->is_android_emulator && p_offset_bytes == 0 && p_buffer_size_bytes == p_data_size_bytes) {
			// Workaround: Buggy emulators crash or race on standard orphaning.
			// Passing the exact size and data directly to glBufferData forces a safe internal reallocation.
			glBufferData(p_target, p_buffer_size_bytes, p_data, p_usage);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_buffer_orphan_and_upload_3d: glBufferData emulator");
			return true;
		}
		glBufferData(p_target, p_buffer_size_bytes, nullptr, p_usage);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_buffer_orphan_and_upload_3d: glBufferData orphan");
	}

	if (p_data_size_bytes > 0 && p_data != nullptr) {
		glBufferSubData(p_target, p_offset_bytes, p_data_size_bytes, p_data);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_buffer_orphan_and_upload_3d: glBufferSubData");
	}
	return true;
}

void RasterizerSceneGLES2::_batch_upload_multimesh_palette_3d(const BatchType::Batch3D &p_batch, RID p_shader_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
	if (p_batch.first_item_ref >= (uint32_t)bdata.item_refs.size()) {
		return;
	}
	const BatchType::BItemRef3D &ref = bdata.item_refs[p_batch.first_item_ref];
	const GeometryInstanceSurface *surf = static_cast<const GeometryInstanceSurface *>(ref.surface);
	if (!surf || !surf->owner || surf->owner->data->base_type != RS::INSTANCE_MULTIMESH) {
		return;
	}

	MultiMeshInstanceData mm = _get_multimesh_data(surf);
	if (!mm.data || ref.instance_count == 0) {
		return;
	}

	const GLint palette_loc = GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_get_uniform(SceneShaderGLES2::MULTIMESH_PALETTE, p_shader_version, p_variant, p_spec_constants);
	if (palette_loc == -1) {
		return;
	}

	const uint32_t count = MIN(ref.instance_count, 32U);
	const float *src_base = mm.data + (ref.instance_offset * mm.stride);

	if (mm.format == RS::MULTIMESH_TRANSFORM_3D && mm.stride == 12) {
		glUniform4fv(palette_loc, count * 3, src_base);
	} else {
		float palette_buf[96 * 4] = {};
		for (uint32_t i = 0; i < count; i++) {
			const float *s = src_base + (i * mm.stride);
			float *d = palette_buf + (i * 12);
			if (mm.format == RS::MULTIMESH_TRANSFORM_3D) {
				memcpy(d, s, sizeof(float) * 12);
			} else {
				d[0] = s[0];
				d[1] = s[1];
				d[2] = 0.0f;
				d[3] = s[3];
				d[4] = s[4];
				d[5] = s[5];
				d[6] = 0.0f;
				d[7] = s[7];
				d[8] = 0.0f;
				d[9] = 0.0f;
				d[10] = 1.0f;
				d[11] = 0.0f;
			}
		}
		glUniform4fv(palette_loc, count * 3, palette_buf);
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_upload_batch_multimesh_palette: glUniform4fv");
}

void RasterizerSceneGLES2::_batch_threshold_flush() {
	glFlush();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_batch_threshold_flush: glFlush");
}

bool RasterizerSceneGLES2::_batch_bind_pass_material_and_state(GLES2::SceneMaterialData *p_material_data, GLES2::SceneShaderData *p_shader, RS::CullMode p_cull_mode, uint64_t p_state_hash, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, bool p_transparent) {
	if (!p_shader) {
		return false;
	}

	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	const bool same_material_state = (
		p_state_hash != 0 &&
		p_state_hash == _render_item_state.current_state_hash &&
		p_material_data == _render_item_state.current_material_data &&
		p_variant == scene_state.current_shader_variant
	);

	if (!same_material_state) {
		if (!material_storage->shaders.scene_shader.version_bind_shader(p_shader->version, p_variant, p_spec_constants)) {
			return false;
		}

		if (p_material_data) {
			p_material_data->bind_uniforms();
		}
		_bind_scene_camera_uniforms(p_shader->version, p_variant, p_spec_constants);

		if (p_shader->uses_depth_prepass_alpha) {
			const float prepass_threshold = (bdata.pass_mode == PASS_MODE_SHADOW)
					? 0.5f
					: ((bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT) ? 0.1f : 0.99f);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::OPAQUE_PREPASS_THRESHOLD, prepass_threshold, p_shader->version, p_variant, p_spec_constants);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_pass_material_and_state: version_set_uniform OPAQUE_PREPASS_THRESHOLD");
		}

		if (
			!scene_state.is_directional_additive_replay &&
			(bdata.pass_mode == PASS_MODE_COLOR || bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT)
		) {
			// Base pass ensures omni and spot counts are disabled/zeroed
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::OMNI_LIGHT_COUNT, 0, p_shader->version, p_variant, p_spec_constants);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SPOT_LIGHT_COUNT, 0, p_shader->version, p_variant, p_spec_constants);
		}
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_bind_pass_material_and_state: light data upload");

		_render_item_state.current_state_hash = p_state_hash;
		_render_item_state.current_material_data = p_material_data;
		scene_state.current_shader_variant = p_variant;
	}

	if (bdata.pass_mode == PASS_MODE_MATERIAL) {
		GLint uv_offset_loc = material_storage->shaders.scene_shader.version_get_uniform(SceneShaderGLES2::UV_OFFSET, p_shader->version, p_variant, p_spec_constants);
		if (uv_offset_loc != -1) {
			glUniform2f(uv_offset_loc, scene_state.current_uv_offset.x, scene_state.current_uv_offset.y);
			GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_bind_pass_material_and_state: glUniform2f uv_offset");
		}
	}

	scene_state.set_gl_cull_mode(p_cull_mode);
	scene_state.enable_gl_depth_test(p_shader->depth_test == GLES2::SceneShaderData::DEPTH_TEST_ENABLED);

	if (scene_state.is_directional_additive_replay) {
		scene_state.enable_gl_blend(true);
		scene_state.enable_gl_depth_draw(false);
	} else {
		if (p_transparent) {
			scene_state.enable_gl_blend(true);
			scene_state.set_gl_blend_mode(p_shader->blend_mode);
		}

		if (p_shader->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_OPAQUE) {
			scene_state.enable_gl_depth_draw(!p_transparent);
		} else {
			scene_state.enable_gl_depth_draw(p_shader->depth_draw == GLES2::SceneShaderData::DEPTH_DRAW_ALWAYS);
		}
	}

	return true;
}

void RasterizerSceneGLES2::_batch_bind_material(GLES2::SceneMaterialData *p_material_data, const Transform3D &p_world_transform, bool p_transparent) {
	if (!p_material_data || !p_material_data->shader_data) {
		return;
	}

	const SceneShaderGLES2::ShaderVariant variant = _get_batch_shader_variant();
	const uint64_t spec_constants = _get_pass_spec_constants();

	RS::CullMode cull_mode = p_material_data->shader_data->cull_mode;
	uint64_t batch_state_hash = 0;

	if (_render_item_state.curr_batch && _render_item_state.curr_batch->first_item_ref < (uint32_t)bdata.item_refs.size()) {
		const GeometryInstanceSurface *first_surf = static_cast<const GeometryInstanceSurface *>(bdata.item_refs[_render_item_state.curr_batch->first_item_ref].surface);
		GLES2::SceneMaterialData *dummy_mat = nullptr;
		GLES2::SceneShaderData *dummy_shader = nullptr;
		_batch_resolve_pass_surface_state(first_surf, dummy_mat, dummy_shader, cull_mode);
		if (first_surf) {
			batch_state_hash = _batch_get_state_hash(first_surf) & BatchType::MASK_ID;
		}
	}

	if (!_batch_bind_pass_material_and_state(p_material_data, p_material_data->shader_data, cull_mode, batch_state_hash, variant, spec_constants, p_transparent)) {
		return;
	}

	GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WORLD_TRANSFORM, p_world_transform, p_material_data->shader_data->version, variant, spec_constants);

	if (_render_item_state.curr_batch && _batch_util_promote_to_instancing(bdata.fvf, _render_item_state.curr_batch->flags)) {
		_batch_upload_multimesh_palette_3d(*_render_item_state.curr_batch, p_material_data->shader_data->version, variant, spec_constants);
	}
}

bool RasterizerSceneGLES2::_batch_bind_generic_buffers(bool p_has_color, BatchBoundGeometryStateGLES2 &r_state) {
	r_state = BatchBoundGeometryStateGLES2();

	uint32_t vbo_idx = _render_item_state.curr_batch ? _render_item_state.curr_batch->vbo_index : 0;
	uint32_t gl_vbo = bdata.vbo_pool.size() > vbo_idx ? bdata.vbo_pool[vbo_idx].gl_vertex_buffer : 0;
	uint32_t gl_ibo = bdata.vbo_pool.size() > vbo_idx ? bdata.vbo_pool[vbo_idx].gl_index_buffer : 0;

	if (gl_vbo == 0 || (gl_ibo == 0 && bdata.indices.size() == 0)) {
		return false;
	}

	const bool is_depth_only = _batch_util_is_depth_only_fvf(bdata.fvf);
	const bool is_depth_alpha = _batch_util_is_depth_alpha_fvf(bdata.fvf);
	const bool is_tier1 = (bdata.fvf == BatcherEnums::FVF_3D_TIER1_BASE);
	const bool is_tier2 = (bdata.fvf == BatcherEnums::FVF_3D_TIER2_COLOR);
	const bool is_tier3 = (bdata.fvf == BatcherEnums::FVF_3D_TIER3_TANGENT);

	uint32_t stride = bdata.unit_vertices.get_unit_size_bytes();

	scene_state.bind_gl_array_buffer(gl_vbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_generic_buffers: glBindBuffer ARRAY_BUFFER");

	const size_t vbo_byte_ofs = _render_item_state.curr_batch ? _render_item_state.curr_batch->vbo_byte_offset : 0;
	const size_t ibo_byte_ofs = _render_item_state.curr_batch ? _render_item_state.curr_batch->ibo_byte_offset : 0;

	size_t pos_ofs = vbo_byte_ofs;
	size_t norm_ofs = vbo_byte_ofs;
	size_t uv_ofs = vbo_byte_ofs;
	size_t uv2_ofs = vbo_byte_ofs;
	size_t col_ofs = vbo_byte_ofs;
	size_t tan_ofs = vbo_byte_ofs;
	size_t m_idx_ofs = vbo_byte_ofs;

	if (is_depth_only) {
		pos_ofs += offsetof(BatchType::BatchVertex3DDepthMinimal, pos);
		m_idx_ofs += offsetof(BatchType::BatchVertex3DDepthMinimal, matrix_index);
	} else if (is_depth_alpha) {
		pos_ofs += offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, pos);
		uv_ofs += offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, uv);
		col_ofs += offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, color);
		m_idx_ofs += offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, matrix_index);
	} else if (is_tier1) {
		pos_ofs += offsetof(BatchType::BatchVertex3DTier1, pos);
		norm_ofs += offsetof(BatchType::BatchVertex3DTier1, normal);
		uv_ofs += offsetof(BatchType::BatchVertex3DTier1, uv);
	} else if (is_tier2) {
		pos_ofs += offsetof(BatchType::BatchVertex3DTier2, pos);
		col_ofs += offsetof(BatchType::BatchVertex3DTier2, color);
		norm_ofs += offsetof(BatchType::BatchVertex3DTier2, normal);
		uv_ofs += offsetof(BatchType::BatchVertex3DTier2, uv);
		uv2_ofs += offsetof(BatchType::BatchVertex3DTier2, uv2);
	} else if (is_tier3) {
		pos_ofs += offsetof(BatchType::BatchVertex3DTier3, pos);
		col_ofs += offsetof(BatchType::BatchVertex3DTier3, color);
		norm_ofs += offsetof(BatchType::BatchVertex3DTier3, normal);
		uv_ofs += offsetof(BatchType::BatchVertex3DTier3, uv);
		uv2_ofs += offsetof(BatchType::BatchVertex3DTier3, uv2);
		tan_ofs += offsetof(BatchType::BatchVertex3DTier3, tangent);
	} else {
		pos_ofs += offsetof(BatchType::BatchVertex3DTier4, pos);
		col_ofs += offsetof(BatchType::BatchVertex3DTier4, color);
		norm_ofs += offsetof(BatchType::BatchVertex3DTier4, normal);
		uv_ofs += offsetof(BatchType::BatchVertex3DTier4, uv);
		uv2_ofs += offsetof(BatchType::BatchVertex3DTier4, uv2);
		tan_ofs += offsetof(BatchType::BatchVertex3DTier4, tangent);
		m_idx_ofs += offsetof(BatchType::BatchVertex3DTier4, matrix_index);
	}

	uint32_t desired_mask = (1U << RS::ARRAY_VERTEX);
	glVertexAttribPointer(RS::ARRAY_VERTEX, 3, GL_FLOAT, GL_FALSE, stride, (const void *)pos_ofs);

	if (is_depth_alpha) {
		desired_mask |= (1U << RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, stride, (const void *)uv_ofs);
		if (p_has_color) {
			desired_mask |= (1U << RS::ARRAY_COLOR);
			glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)col_ofs);
		} else {
			glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
		}
	} else if (!is_depth_only) {
		desired_mask |= (1U << RS::ARRAY_NORMAL) | (1U << RS::ARRAY_TEX_UV);
		glVertexAttribPointer(RS::ARRAY_NORMAL, 3, GL_BYTE, GL_TRUE, stride, (const void *)norm_ofs);
		glVertexAttribPointer(RS::ARRAY_TEX_UV, 2, GL_FLOAT, GL_FALSE, stride, (const void *)uv_ofs);

		if (!is_tier1 && p_has_color) {
			desired_mask |= (1U << RS::ARRAY_COLOR);
			glVertexAttribPointer(RS::ARRAY_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)col_ofs);
		} else {
			glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
		}

		if (!is_tier1) {
			desired_mask |= (1U << RS::ARRAY_TEX_UV2);
			glVertexAttribPointer(RS::ARRAY_TEX_UV2, 2, GL_FLOAT, GL_FALSE, stride, (const void *)uv2_ofs);
		} else {
			glVertexAttrib2f(RS::ARRAY_TEX_UV2, 0.0f, 0.0f);
		}

		if (!is_tier1 && !is_tier2) {
			desired_mask |= (1U << RS::ARRAY_TANGENT);
			glVertexAttribPointer(RS::ARRAY_TANGENT, 4, GL_BYTE, GL_TRUE, stride, (const void *)tan_ofs);
		} else {
			glVertexAttrib4f(RS::ARRAY_TANGENT, 1.0f, 0.0f, 0.0f, 1.0f);
		}
	}

	scene_state.set_gl_vertex_attrib_mask(desired_mask);

	// Ensure instancing attribute slots (12-15) are disabled and set to identity defaults
	// so no state leaks occur from immediate MultiMesh draws.
	_gl_reset_instancing_attributes();

	const uint32_t batch_flags = _render_item_state.curr_batch ? _render_item_state.curr_batch->flags : 0U;
	if (_batch_util_promote_to_instancing(bdata.fvf, batch_flags)) {
		glEnableVertexAttribArray(12);
		glVertexAttribPointer(12, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, (const void *)m_idx_ofs);
		glVertexAttrib4f(15, 1.0f, 1.0f, 1.0f, 1.0f);
		scene_state.instancing_attribs_dirty = true;
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_generic_buffers: glVertexAttribPointers setup");

	scene_state.bind_gl_element_array_buffer(gl_ibo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_generic_buffers: glBindBuffer ELEMENT_ARRAY_BUFFER");

	r_state.valid = true;
	r_state.is_single_mesh_fallback = false;
	r_state.use_index_buffer = true;
	r_state.index_type = GL_UNSIGNED_SHORT;
	r_state.index_size = sizeof(uint16_t);
	r_state.ibo_byte_ofs = ibo_byte_ofs;
	r_state.default_draw_count = (
		gl_ibo != 0 && _render_item_state.curr_batch
	) ? _render_item_state.curr_batch->num_indices : static_cast<uint32_t>(bdata.indices.size());
	return true;
}

bool RasterizerSceneGLES2::_batch_bind_single_mesh_fallback_buffers(bool p_has_color, BatchBoundGeometryStateGLES2 &r_state) {
	r_state = BatchBoundGeometryStateGLES2();
	if (!_render_item_state.curr_batch) {
		return false;
	}

	const BatchType::Batch3D &batch = *_render_item_state.curr_batch;
	const BatchType::BItemRef3D &first_ref = bdata.item_refs[batch.first_item_ref];
	const GeometryInstanceSurface *surf = static_cast<const GeometryInstanceSurface *>(first_ref.surface);
	if (!surf || !surf->surface || !surf->owner) {
		return false;
	}

	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();

	const GLuint gl_vbo = mesh_storage->mesh_surface_get_vertex_buffer(surf->surface);
	if (gl_vbo == 0) {
		return false;
	}

	const bool is_depth_only = _batch_util_is_depth_only_flags(batch.flags);
	const bool is_depth_alpha = _batch_util_is_depth_alpha_flags(batch.flags);

	GLES2::SceneMaterialData *active_mat = nullptr;
	GLES2::SceneShaderData *active_shader = nullptr;
	RS::CullMode active_cull = RS::CULL_MODE_BACK;
	_batch_resolve_pass_surface_state(surf, active_mat, active_shader, active_cull);

	const uint64_t input_mask = _resolve_effective_vertex_input_mask(active_shader, is_depth_only, is_depth_alpha);

	GLuint dummy_vao = 0;
	GLES2::Mesh::Surface::Version *version = mesh_storage->mesh_surface_get_vertex_arrays_and_format(surf->surface, input_mask, dummy_vao);
	ERR_FAIL_NULL_V(version, false);

	uint32_t desired_mask = 0;

	// Bind interleaved vertex buffer (position, normal, tangent)
	scene_state.bind_gl_array_buffer(gl_vbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_single_mesh_fallback_buffers: glBindBuffer vertex");

	if (version->attribs[RS::ARRAY_VERTEX].enabled) {
		desired_mask |= (1U << RS::ARRAY_VERTEX);
		glVertexAttribPointer(
			RS::ARRAY_VERTEX,
			version->attribs[RS::ARRAY_VERTEX].size,
			version->attribs[RS::ARRAY_VERTEX].type,
			version->attribs[RS::ARRAY_VERTEX].normalized,
			version->attribs[RS::ARRAY_VERTEX].stride,
			(const void *)(uintptr_t)version->attribs[RS::ARRAY_VERTEX].offset
		);
	}

	if (!is_depth_only && !is_depth_alpha && version->attribs[RS::ARRAY_NORMAL].enabled) {
		desired_mask |= (1U << RS::ARRAY_NORMAL);
		glVertexAttribPointer(
			RS::ARRAY_NORMAL,
			version->attribs[RS::ARRAY_NORMAL].size,
			version->attribs[RS::ARRAY_NORMAL].type,
			version->attribs[RS::ARRAY_NORMAL].normalized,
			version->attribs[RS::ARRAY_NORMAL].stride,
			(const void *)(uintptr_t)version->attribs[RS::ARRAY_NORMAL].offset
		);
	} else {
		glVertexAttrib3f(RS::ARRAY_NORMAL, 0.0f, 1.0f, 0.0f);
	}

	if (!is_depth_only && !is_depth_alpha && version->attribs[RS::ARRAY_TANGENT].enabled) {
		desired_mask |= (1U << RS::ARRAY_TANGENT);
		glVertexAttribPointer(
			RS::ARRAY_TANGENT,
			version->attribs[RS::ARRAY_TANGENT].size,
			version->attribs[RS::ARRAY_TANGENT].type,
			version->attribs[RS::ARRAY_TANGENT].normalized,
			version->attribs[RS::ARRAY_TANGENT].stride,
			(const void *)(uintptr_t)version->attribs[RS::ARRAY_TANGENT].offset
		);
	} else {
		glVertexAttrib4f(RS::ARRAY_TANGENT, 1.0f, 0.0f, 0.0f, 1.0f);
	}

	// Bind attribute buffer (color, uv, uv2, custom0..3)
	const GLuint gl_abo = mesh_storage->mesh_surface_get_attribute_buffer(surf->surface);
	if (gl_abo != 0) {
		scene_state.bind_gl_array_buffer(gl_abo);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_single_mesh_fallback_buffers: glBindBuffer attribute");
	}

	if (
		!is_depth_only &&
		p_has_color &&
		gl_abo != 0 &&
		version->attribs[RS::ARRAY_COLOR].enabled
	) {
		desired_mask |= (1U << RS::ARRAY_COLOR);
		glVertexAttribPointer(
			RS::ARRAY_COLOR,
			version->attribs[RS::ARRAY_COLOR].size,
			version->attribs[RS::ARRAY_COLOR].type,
			version->attribs[RS::ARRAY_COLOR].normalized,
			version->attribs[RS::ARRAY_COLOR].stride,
			(const void *)(uintptr_t)version->attribs[RS::ARRAY_COLOR].offset
		);
	} else {
		glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (!is_depth_only && gl_abo != 0 && version->attribs[RS::ARRAY_TEX_UV].enabled) {
		desired_mask |= (1U << RS::ARRAY_TEX_UV);
		glVertexAttribPointer(
			RS::ARRAY_TEX_UV,
			version->attribs[RS::ARRAY_TEX_UV].size,
			version->attribs[RS::ARRAY_TEX_UV].type,
			version->attribs[RS::ARRAY_TEX_UV].normalized,
			version->attribs[RS::ARRAY_TEX_UV].stride,
			(const void *)(uintptr_t)version->attribs[RS::ARRAY_TEX_UV].offset
		);
	} else {
		glVertexAttrib2f(RS::ARRAY_TEX_UV, 0.0f, 0.0f);
	}

	if (!is_depth_only && !is_depth_alpha && gl_abo != 0 && version->attribs[RS::ARRAY_TEX_UV2].enabled) {
		desired_mask |= (1U << RS::ARRAY_TEX_UV2);
		glVertexAttribPointer(
			RS::ARRAY_TEX_UV2,
			version->attribs[RS::ARRAY_TEX_UV2].size,
			version->attribs[RS::ARRAY_TEX_UV2].type,
			version->attribs[RS::ARRAY_TEX_UV2].normalized,
			version->attribs[RS::ARRAY_TEX_UV2].stride,
			(const void *)(uintptr_t)version->attribs[RS::ARRAY_TEX_UV2].offset
		);
	} else {
		glVertexAttrib2f(RS::ARRAY_TEX_UV2, 0.0f, 0.0f);
	}

	for (int i = RS::ARRAY_CUSTOM0; i <= RS::ARRAY_CUSTOM3; i++) {
		if (!is_depth_only && !is_depth_alpha && gl_abo != 0 && version->attribs[i].enabled) {
			desired_mask |= (1U << i);
			glVertexAttribPointer(
				i,
				version->attribs[i].size,
				version->attribs[i].type,
				version->attribs[i].normalized,
				version->attribs[i].stride,
				(const void *)(uintptr_t)version->attribs[i].offset
			);
		}
	}

	scene_state.set_gl_vertex_attrib_mask(desired_mask);

	// Sterilise instancing slots (12-15)
	_gl_reset_instancing_attributes();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_single_mesh_fallback_buffers: glVertexAttribPointers setup");

	const uint32_t index_count = mesh_storage->mesh_surface_get_index_count(surf->surface, surf->lod_index);
	r_state.valid = true;
	r_state.is_single_mesh_fallback = true;
	r_state.ibo_byte_ofs = 0;

	if (index_count > 0) {
		const GLuint gl_ibo = mesh_storage->mesh_surface_get_index_buffer(surf->surface, surf->lod_index);
		const GLenum index_type = mesh_storage->mesh_surface_get_index_type(surf->surface);
		scene_state.bind_gl_element_array_buffer(gl_ibo);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_bind_single_mesh_fallback_buffers: glBindBuffer ELEMENT_ARRAY_BUFFER");

		r_state.use_index_buffer = true;
		r_state.index_type = index_type;
		r_state.index_size = (index_type == GL_UNSIGNED_INT) ? sizeof(uint32_t) : sizeof(uint16_t);
		r_state.default_draw_count = index_count;
	} else {
		scene_state.bind_gl_element_array_buffer(0);
		r_state.use_index_buffer = false;
		r_state.index_type = 0;
		r_state.index_size = 0;
		r_state.default_draw_count = mesh_storage->mesh_surface_get_vertices_drawn_count(surf->surface);
	}

	return true;
}

void RasterizerSceneGLES2::_batch_draw_bound_buffers(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, const BatchBoundGeometryStateGLES2 &p_state) {
	if (!p_state.valid) {
		return;
	}

	const uint32_t draw_count = (p_count > 0) ? p_count : p_state.default_draw_count;
	if (draw_count == 0) {
		return;
	}

	const GLenum primitive_gl = _gl_get_primitive_type(p_primitive);
	if (p_state.use_index_buffer) {
		const size_t byte_ofs = p_state.ibo_byte_ofs + (static_cast<size_t>(p_offset) * p_state.index_size);
		glDrawElements(primitive_gl, draw_count, p_state.index_type, (const void *)(uintptr_t)byte_ofs);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_draw_bound_buffers: glDrawElements");
	} else {
		glDrawArrays(primitive_gl, p_offset, draw_count);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_draw_bound_buffers: glDrawArrays");
	}

	_record_draw_call_info(scene_state.current_render_info);
}

void RasterizerSceneGLES2::_batch_render_items(GLES2::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent) {
	if (
		scene_state.is_directional_additive_replay &&
		p_material_data &&
		p_material_data->shader_data &&
		p_material_data->shader_data->unshaded
	) {
		return;
	}

	if (p_batch.fvf == BatcherEnums::FVF_SAME_MESH_LOOP) {
		_batch_render_same_mesh_group(p_material_data, p_primitive, p_batch, p_transparent);
		return;
	}

	const BatchType::BItemRef3D &first_ref = bdata.item_refs[p_batch.first_item_ref];
	GeometryInstanceSurface *first_surf = static_cast<GeometryInstanceSurface *>(first_ref.surface);
	if (!first_surf || !first_surf->owner) {
		return;
	}

	const bool use_hardware_transform = (p_batch.num_item_refs == 1);
	// Include FVF_UNBATCHED batches that flagged BTF_3D_USES_COLORS so their static
	// color attribute stream is enabled during the SMDDF call.
	const bool has_vertex_colors = _batch_util_has_vertex_colors(bdata.fvf, p_batch.flags);

	Transform3D world_xform;
	if (use_hardware_transform) {
		world_xform = first_ref.transform;
	}

	BatchBoundGeometryStateGLES2 bound_state{};
	const bool bound_ok = (bdata.fvf == BatcherEnums::FVF_UNBATCHED)
			? _batch_bind_single_mesh_fallback_buffers(has_vertex_colors, bound_state)
			: _batch_bind_generic_buffers(has_vertex_colors, bound_state);

	if (!bound_ok) {
		// Invalidate if something went wrong in the bind
		_render_item_state.current_state_hash = 0;
		return;
	}

	// =====================================
	// Base pass (directional, ambient, IBL)
	// =====================================
	_batch_bind_material(p_material_data, world_xform, p_transparent);
	_batch_draw_bound_buffers(p_primitive, 0, p_batch.num_indices, bound_state);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_items: glDrawElements BASE_PASS");

	// =====================================
	// Additive passes (omni & spot lights)
	// =====================================
	if (
		!scene_state.is_directional_additive_replay &&
		(bdata.pass_mode == PASS_MODE_COLOR || bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT)
	) {
		bool promote_to_instancing = _batch_util_promote_to_instancing(bdata.fvf, p_batch.flags);

		_render_additive_light_passes<true>(
			first_surf,
			p_material_data,
			scene_state.current_spec_constants,
			promote_to_instancing,
			world_xform,
			p_primitive,
			1,
			nullptr,
			nullptr,
			false,
			0,
			0,
			0,
			&bound_state
		);
	}
	_invalidate_state_if_additive_mutated();
}

void RasterizerSceneGLES2::_batch_render_generic(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, bool p_has_color) {
	if (bdata.fvf == BatcherEnums::FVF_UNBATCHED) {
		_batch_render_single_mesh_fallback(p_primitive, p_offset, p_count, p_has_color);
		return;
	}

	BatchBoundGeometryStateGLES2 state{};
	if (_batch_bind_generic_buffers(p_has_color, state)) {
		_batch_draw_bound_buffers(p_primitive, p_offset, p_count, state);
	}
}

void RasterizerSceneGLES2::_batch_render_single_mesh_fallback(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, bool p_has_color) {
	// Executes a single-mesh direct draw fallback (SMDDF, FVF_UNBATCHED) directly from the mesh's
	// native static VBO, ABO, and IBO while maintaining identical
	// default vertex attribute constants and state sterilisation.
	BatchBoundGeometryStateGLES2 state{};
	if (_batch_bind_single_mesh_fallback_buffers(p_has_color, state)) {
		_batch_draw_bound_buffers(p_primitive, p_offset, p_count, state);
	}
}

void RasterizerSceneGLES2::_batch_render_same_mesh_group(GLES2::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent) {
	// Executes the static-mesh state grouping lane (FVF_SAME_MESH_LOOP) for repeated
	// heavy meshes (> 1024 vertices) and MultiMesh instances. Binds the shader, material
	// uniforms, and the mesh's native static VBO, ABO, and IBO once for the entire
	// contiguous run, then loops over each reference/instance updating only the
	// WORLD_TRANSFORM uniform and instancing color attribute (slot 15) before drawing.
	const BatchType::BItemRef3D &first_ref = bdata.item_refs[p_batch.first_item_ref];
	const GeometryInstanceSurface *first_surf = static_cast<const GeometryInstanceSurface *>(first_ref.surface);
	if (!first_surf || !first_surf->surface || !first_surf->owner || !first_surf->shader) {
		return;
	}

	GLES2::SceneMaterialData *active_mat = nullptr;
	GLES2::SceneShaderData *shader = nullptr;
	RS::CullMode active_cull = RS::CULL_MODE_BACK;
	_batch_resolve_pass_surface_state(first_surf, active_mat, shader, active_cull);
	if (!shader) {
		return;
	}

	const bool promote_to_instancing = _batch_util_promote_to_instancing(p_batch.fvf, p_batch.flags);
	const SceneShaderGLES2::ShaderVariant variant = _get_shader_variant(promote_to_instancing);
	const uint64_t spec_constants = _get_pass_spec_constants();

	// Bind material and shader once for the entire group
	_batch_bind_material(p_material_data, first_ref.transform, p_transparent);

	const bool is_depth_only = _batch_util_is_depth_only_flags(p_batch.flags);
	const bool is_depth_alpha = _batch_util_is_depth_alpha_flags(p_batch.flags);
	const uint64_t input_mask = _resolve_effective_vertex_input_mask(shader, is_depth_only, is_depth_alpha);

	// Bind static mesh VBO/ABO and IBO once for the entire run
	bool use_index_buffer = false;
	int drawn_count = 0;
	GLenum index_type = 0;
	_bind_surface_static_arrays_and_ibo(first_surf, input_mask, false, use_index_buffer, drawn_count, index_type);

	// Sterilise instancing attribute slots (12-15)
	_gl_reset_instancing_attributes();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_same_mesh_group: setup instancing defaults");

	const GLenum primitive_gl = _gl_get_primitive_type(p_primitive);

	// =====================================
	// Base pass (directional, ambient, IBL)
	// =====================================
	if (drawn_count > 0) {
		for (uint32_t r = 0; r < p_batch.num_item_refs; r++) {
			const BatchType::BItemRef3D &ref = bdata.item_refs[p_batch.first_item_ref + r];
			_draw_item_ref_instances(ref, shader->version, variant, spec_constants, promote_to_instancing, use_index_buffer, primitive_gl, drawn_count, index_type);
		}
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_same_mesh_group: glDrawElements BASE_PASS");

	// =====================================
	// Additive passes (omni & spot lights)
	// =====================================
	if (
		!scene_state.is_directional_additive_replay &&
		(bdata.pass_mode == PASS_MODE_COLOR || bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT) &&
		drawn_count > 0
	) {
		_render_static_mesh_group_additive_light_passes(
			p_batch,
			p_material_data,
			shader,
			spec_constants,
			promote_to_instancing,
			use_index_buffer,
			primitive_gl,
			drawn_count,
			index_type
		);
	}

	// Unbind static VBO/IBO and sterilise attribute state once at the end of the run
	if (promote_to_instancing) {
		glVertexAttrib4f(15, 1.0f, 1.0f, 1.0f, 1.0f);
	}
	glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_batch_render_same_mesh_group: state cleanup");

	_invalidate_state_if_additive_mutated();
}

void RasterizerSceneGLES2::_render_single_item_immediate(const GeometryInstanceSurface *p_surface) {
	GLES2::SceneMaterialData *active_mat = nullptr;
	GLES2::SceneShaderData *shader = nullptr;

	RS::CullMode active_cull = RS::CULL_MODE_BACK;
	_batch_resolve_pass_surface_state(p_surface, active_mat, shader, active_cull);

	if (!shader || !p_surface->surface || !p_surface->owner) {
		return;
	}
	if (scene_state.is_directional_additive_replay && shader->unshaded) {
		return;
	}

	const int instances = p_surface->owner->instance_count >= 0 ? p_surface->owner->instance_count : 1;
	if (instances == 0) {
		return;
	}

	const uint64_t item_hash = _batch_get_state_hash(p_surface);
	const bool promote_to_instancing = _batch_util_promote_to_instancing(bdata.fvf, p_surface->flags);
	const SceneShaderGLES2::ShaderVariant variant = _get_shader_variant(promote_to_instancing);
	const uint64_t spec_constants = _get_pass_spec_constants();
	const bool is_transparent = (
		bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT ||
		(p_surface->flags & GeometryInstanceSurface::FLAG_PASS_ALPHA) != 0
	);

	if (!_batch_bind_pass_material_and_state(active_mat, shader, active_cull, item_hash, variant, spec_constants, is_transparent)) {
		return;
	}

	const uint64_t input_mask = _resolve_effective_vertex_input_mask(shader);

	bool use_index_buffer = false;
	int drawn_count = 0;
	GLenum index_type = 0;
	_bind_surface_static_arrays_and_ibo(p_surface, input_mask, true, use_index_buffer, drawn_count, index_type);

	const GLenum primitive_gl = _gl_get_primitive_type(p_surface->primitive);
	const MultiMeshInstanceData mm = _get_multimesh_data(p_surface);
	const Transform3D owner_transform = p_surface->owner->transform;

	if (promote_to_instancing) {
		// Provide default values when we promote
		// to instancing.
		scene_state.instancing_attribs_dirty = true;
		_gl_reset_instancing_attributes();
		scene_state.instancing_attribs_dirty = true;
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_single_item_immediate: glVertexAttrib4f instancing defaults");
	}

	// =====================================
	// Base pass (directional, ambien, IBL)
	// =====================================
	_draw_surface_instances(p_surface, owner_transform, instances, &mm, shader->version, variant, spec_constants, promote_to_instancing, use_index_buffer, primitive_gl, drawn_count, index_type);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_single_item_immediate: glDrawElements BASE_PASS");

	// =====================================
	// Additive passes (omni & spot lights)
	// =====================================
	if (
		!scene_state.is_directional_additive_replay &&
		(bdata.pass_mode == PASS_MODE_COLOR || bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT)
	) {
		_render_additive_light_passes<false>(
			p_surface,
			p_surface->material,
			spec_constants,
			promote_to_instancing,
			Transform3D(),
			RS::PRIMITIVE_MAX,
			instances,
			&mm,
			&owner_transform,
			use_index_buffer,
			primitive_gl,
			drawn_count,
			index_type
		);
		_invalidate_state_if_additive_mutated();
	}

	// Unbind state
	if (promote_to_instancing) {
		glVertexAttrib4f(15, 1.0f, 1.0f, 1.0f, 1.0f);
		scene_state.instancing_attribs_dirty = false;
	}
}

template <RasterizerSceneGLES2::AdditiveLightType p_light_type>
void RasterizerSceneGLES2::_render_static_mesh_group_light_type_pass(const LocalVector<uint32_t> &p_unique_lights, BatchType::Batch3D &p_batch, GLES2::SceneMaterialData *p_material_data, GLES2::SceneShaderData *p_shader, SceneShaderGLES2::ShaderVariant p_additive_variant, uint64_t p_additive_base_spec, bool p_promote_to_instancing, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type) {
	if (p_unique_lights.is_empty()) {
		return;
	}

	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	constexpr uint64_t light_spec_flag = (p_light_type == ADDITIVE_LIGHT_OMNI) ? SceneShaderGLES2::ADDITIVE_OMNI : SceneShaderGLES2::ADDITIVE_SPOT;
	const uint64_t pass_spec = p_additive_base_spec | SceneShaderGLES2::USE_ADDITIVE_LIGHTING | light_spec_flag;

	if (!material_storage->shaders.scene_shader.version_bind_shader(p_shader->version, p_additive_variant, pass_spec)) {
		return;
	}

	p_material_data->bind_uniforms();
	_bind_scene_camera_uniforms(p_shader->version, p_additive_variant, pass_spec);

	const LightData *lights = (p_light_type == ADDITIVE_LIGHT_OMNI) ? scene_state.omni_lights : scene_state.spot_lights;

	for (uint32_t u = 0; u < p_unique_lights.size(); u++) {
		const uint32_t gl_id = p_unique_lights[u];
		_upload_additive_light_uniforms<p_light_type>(gl_id, p_shader->version, p_additive_variant, pass_spec);

		for (uint32_t r = 0; r < p_batch.num_item_refs; r++) {
			const BatchType::BItemRef3D &ref = bdata.item_refs[p_batch.first_item_ref + r];
			const GeometryInstanceSurface *surf = static_cast<const GeometryInstanceSurface *>(ref.surface);
			if (!surf || !surf->owner || ref.instance_count == 0) {
				continue;
			}
			const LocalVector<uint32_t> &light_cache = (p_light_type == ADDITIVE_LIGHT_OMNI) ? surf->owner->omni_light_gl_cache : surf->owner->spot_light_gl_cache;
			if (!light_cache.has(gl_id) || _is_aabb_culled_by_positional_light(surf->owner->transformed_aabb, lights[gl_id])) {
				continue;
			}

			_draw_item_ref_instances(ref, p_shader->version, p_additive_variant, pass_spec, p_promote_to_instancing, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type);
		}
	}

	if constexpr (p_light_type == ADDITIVE_LIGHT_OMNI) {
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_static_mesh_group_additive_light_passes: OMNI");
	} else {
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_static_mesh_group_additive_light_passes: SPOT");
	}
}

void RasterizerSceneGLES2::_render_static_mesh_group_additive_light_passes(BatchType::Batch3D &p_batch, GLES2::SceneMaterialData *p_material_data, GLES2::SceneShaderData *p_shader, uint64_t p_spec_constants, bool p_promote_to_instancing, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type) {
	if (!p_material_data || !p_shader || p_shader->unshaded) {
		return;
	}

	LocalVector<uint32_t> &unique_omni = scene_state.group_omni_lights_scratch;
	LocalVector<uint32_t> &unique_spot = scene_state.group_spot_lights_scratch;
	_batch_util_get_unique_light_vector<ADDITIVE_LIGHT_OMNI, true>(unique_omni);
	_batch_util_get_unique_light_vector<ADDITIVE_LIGHT_SPOT, true>(unique_spot);

	if (unique_omni.is_empty() && unique_spot.is_empty()) {
		return;
	}

	scene_state.additive_pass_mutated_state = true;
	bool was_blend = false;
	GLES2::SceneShaderData::BlendMode was_blend_mode = GLES2::SceneShaderData::BLEND_MODE_MIX;
	bool was_depth_draw = false;
	_batch_util_begin_additive_light_gl_state(was_blend, was_blend_mode, was_depth_draw);

	const SceneShaderGLES2::ShaderVariant additive_variant = p_promote_to_instancing ? SceneShaderGLES2::MODE_ADDITIVE_INSTANCING : SceneShaderGLES2::MODE_ADDITIVE;
	const uint64_t additive_base_spec = p_spec_constants & ~SceneShaderGLES2::BASE_PASS;

	_render_static_mesh_group_light_type_pass<ADDITIVE_LIGHT_OMNI>(unique_omni, p_batch, p_material_data, p_shader, additive_variant, additive_base_spec, p_promote_to_instancing, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type);
	_render_static_mesh_group_light_type_pass<ADDITIVE_LIGHT_SPOT>(unique_spot, p_batch, p_material_data, p_shader, additive_variant, additive_base_spec, p_promote_to_instancing, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type);

	_batch_util_end_additive_light_gl_state(was_blend, was_blend_mode, was_depth_draw);
}

void RasterizerSceneGLES2::_bind_surface_static_arrays_and_ibo(const GeometryInstanceSurface *p_surface, uint64_t p_input_mask, bool p_allow_deformed_instance, bool &r_use_index_buffer, int &r_drawn_count, GLenum &r_index_type) {
	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();

	bool bind_deformed = false;
	if (p_allow_deformed_instance && p_surface->owner->data->base_type == RS::INSTANCE_MESH && p_surface->owner->mesh_instance.is_valid()) {
		GLES2::MeshInstance *mi = mesh_storage->get_mesh_instance(p_surface->owner->mesh_instance);
		const bool has_skel = mi && mi->skeleton.is_valid();
		const int blend_count = mesh_storage->mesh_get_blend_shape_count(p_surface->owner->data->base);
		bind_deformed = has_skel || blend_count > 0;
	}

	if (bind_deformed) {
		mesh_storage->mesh_instance_surface_bind_arrays_gles2(p_surface->owner->mesh_instance, p_surface->surface_index, p_input_mask);
	} else {
		mesh_storage->mesh_surface_bind_arrays_gles2(p_surface->surface, p_input_mask);
	}

	scene_state.current_array_buffer = 0;
	scene_state.enabled_vertex_attrib_mask = static_cast<uint32_t>(
		(p_input_mask & mesh_storage->mesh_surface_get_format(p_surface->surface)) &
		((1ULL << RS::ARRAY_INDEX) - 1ULL)
	);

	const GLuint index_array_gl = mesh_storage->mesh_surface_get_index_buffer(p_surface->surface, p_surface->lod_index);
	r_use_index_buffer = index_array_gl != 0;
	scene_state.bind_gl_element_array_buffer(r_use_index_buffer ? index_array_gl : 0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_bind_surface_static_arrays_and_ibo: glBindBuffer GL_ELEMENT_ARRAY_BUFFER");

	r_drawn_count = mesh_storage->mesh_surface_get_vertices_drawn_count(p_surface->surface);
	r_index_type = r_use_index_buffer ? mesh_storage->mesh_surface_get_index_type(p_surface->surface) : 0;
}

void RasterizerSceneGLES2::_draw_surface_instances(const GeometryInstanceSurface *p_surface, const Transform3D &p_base_transform, int p_instances, const MultiMeshInstanceData *p_mm, RID p_shader_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, bool p_set_instancing_color, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type) {
	if (!p_surface || !p_surface->owner || p_instances <= 0 || p_drawn_count <= 0) {
		return;
	}

	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	const GLint world_xform_loc = material_storage->shaders.scene_shader.version_get_uniform(SceneShaderGLES2::WORLD_TRANSFORM, p_shader_version, p_variant, p_spec_constants);
	const RS::InstanceType base_type = p_surface->owner->data->base_type;
	const bool is_multimesh = (base_type == RS::INSTANCE_MULTIMESH);

	if (!is_multimesh && p_instances == 1) {
		if (world_xform_loc != -1) {
			float mat_buf[16] = {};
			GLES2::MaterialStorage::store_transform(p_base_transform, mat_buf);
			glUniformMatrix4fv(world_xform_loc, 1, GL_FALSE, mat_buf);
		}
		if (p_set_instancing_color) {
			glVertexAttrib4f(15, 1.0f, 1.0f, 1.0f, 1.0f);
		}
		if (p_use_index_buffer) {
			glDrawElements(p_primitive_gl, p_drawn_count, p_index_type, 0);
		} else {
			glDrawArrays(p_primitive_gl, 0, p_drawn_count);
		}
		_record_draw_call_info(scene_state.current_render_info);
		return;
	}

	MultiMeshInstanceData local_mm;
	const MultiMeshInstanceData *mm = p_mm;
	if (is_multimesh && !mm) {
		local_mm = _get_multimesh_data(p_surface);
		mm = &local_mm;
	}

	const bool has_mesh_color_array = !p_surface->color_cache.is_empty();
	const bool apply_inst_color = is_multimesh && mm && mm->uses_colors && (p_set_instancing_color || !has_mesh_color_array);
	const GLuint color_attrib_slot = p_set_instancing_color ? 15U : static_cast<GLuint>(RS::ARRAY_COLOR);

	Color inst_color(1.0f, 1.0f, 1.0f, 1.0f);
	float mat_buf[16] = {};
	for (int inst = 0; inst < p_instances; inst++) {
		Transform3D xform;
		if (is_multimesh && mm && mm->data) {
			_gl_batch_decode_multimesh_instance(mm->data + (inst * mm->stride), mm->format, mm->uses_colors, mm->color_offset, xform, inst_color);
		}

		if (world_xform_loc != -1) {
			const Transform3D write_xform = p_base_transform * xform;
			GLES2::MaterialStorage::store_transform(write_xform, mat_buf);
			glUniformMatrix4fv(world_xform_loc, 1, GL_FALSE, mat_buf);
		}

		if (apply_inst_color) {
			glVertexAttrib4f(color_attrib_slot, inst_color.r, inst_color.g, inst_color.b, inst_color.a);
		}

		if (p_use_index_buffer) {
			glDrawElements(p_primitive_gl, p_drawn_count, p_index_type, 0);
		} else {
			glDrawArrays(p_primitive_gl, 0, p_drawn_count);
		}
		_record_draw_call_info(scene_state.current_render_info);
	}
}

void RasterizerSceneGLES2::_draw_item_ref_instances(const BatchType::BItemRef3D &p_ref, RID p_shader_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, bool p_promote_to_instancing, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type) {
	const GeometryInstanceSurface *surf = static_cast<const GeometryInstanceSurface *>(p_ref.surface);
	if (!surf || !surf->owner || p_ref.instance_count == 0) {
		return;
	}
	const bool is_multimesh = (surf->owner->data->base_type == RS::INSTANCE_MULTIMESH);
	const int instances = is_multimesh ? static_cast<int>(p_ref.instance_count) : 1;
	MultiMeshInstanceData mm;
	if (is_multimesh) {
		mm = _get_multimesh_data(surf);
		if (mm.data) {
			mm.data += (p_ref.instance_offset * mm.stride);
		}
	}
	_draw_surface_instances(surf, p_ref.transform, instances, is_multimesh ? &mm : nullptr, p_shader_version, p_variant, p_spec_constants, is_multimesh && p_promote_to_instancing, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type);
}

/* REST OF RENDERING METHODS */

template <RasterizerSceneGLES2::RenderListType p_render_list, RasterizerSceneGLES2::PassMode p_pass_mode>
void RasterizerSceneGLES2::_fill_render_list(const RenderDataGLES2 *p_render_data, bool p_append) {
	GLES2::MeshStorage *mesh_storage = GLES2::MeshStorage::get_singleton();

	if constexpr (p_render_list == RENDER_LIST_OPAQUE) {
		scene_state.used_screen_texture = false;
		scene_state.used_normal_texture = false;
		scene_state.used_depth_texture = false;
	}

	const Vector3 cam_forward = -p_render_data->cam_transform.basis.get_column(Vector3::AXIS_Z);
	Plane near_plane;
	if (p_render_data->cam_orthogonal) {
		near_plane = Plane(cam_forward, p_render_data->cam_transform.origin);
		near_plane.d += p_render_data->cam_projection.get_z_near();
	}
	float z_max = p_render_data->cam_projection.get_z_far() - p_render_data->cam_projection.get_z_near();

	RenderList *rl = &render_list[p_render_list];
	_update_dirty_geometry_instances();

	if (!p_append) {
		rl->clear();
		if constexpr (p_render_list == RENDER_LIST_OPAQUE) {
			render_list[RENDER_LIST_ALPHA].clear();
		}
	}

	for (int i = 0; i < (int)p_render_data->instances->size(); i++) {
		GeometryInstanceGLES2 *inst = static_cast<GeometryInstanceGLES2 *>((*p_render_data->instances)[i]);

		// Guard against extreme AABB dimensions,
		// which breaks SortByReverseDepthAndPriority and LOD distance calculations.
		const AABB &inst_aabb = inst->transformed_aabb;
		const bool valid_aabb_center = (
			inst->use_aabb_center &&
			Math::abs(inst_aabb.size.x) < 1e6f &&
			Math::abs(inst_aabb.size.y) < 1e6f &&
			Math::abs(inst_aabb.size.z) < 1e6f
		);

		Vector3 center = inst->transform.origin;
		if (p_render_data->cam_orthogonal) {
			if (valid_aabb_center) {
				center = inst_aabb.get_support(-near_plane.normal);
			}
			inst->depth = near_plane.distance_to(center) - inst->sorting_offset;
		} else {
			if (valid_aabb_center) {
				center = inst_aabb.position + (inst_aabb.size * 0.5f);
			}
			inst->depth = cam_forward.dot(center - p_render_data->cam_transform.origin) - inst->sorting_offset;
		}
		if (unlikely(!Math::is_finite(inst->depth))) {
			inst->depth = 0.0f;
		}
		uint32_t depth_layer = z_max > 0.0f ? CLAMP(int(inst->depth * 16.0f / z_max), 0, 15) : 0;

		uint32_t flags = inst->base_flags; //fill flags if appropriate

		if (inst->non_uniform_scale) {
			flags |= INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE;
		}

		if constexpr (p_pass_mode == PASS_MODE_COLOR) {
			inst->omni_light_gl_cache.clear();
			inst->spot_light_gl_cache.clear();

			const uint64_t current_frame = RSG::rasterizer->get_frame_number();
			GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();

			for (int light_kind = 0; light_kind < 2; light_kind++) {
				const uint32_t paired_count = (light_kind == 0) ? inst->paired_omni_light_count : inst->paired_spot_light_count;
				const LocalVector<RID> &paired_lights = (light_kind == 0) ? inst->paired_omni_lights : inst->paired_spot_lights;
				LocalVector<uint32_t> &gl_cache = (light_kind == 0) ? inst->omni_light_gl_cache : inst->spot_light_gl_cache;

				for (uint32_t j = 0; j < paired_count; j++) {
					RID light_instance = paired_lights[j];
					if (light_storage->light_instance_get_render_pass(light_instance) != current_frame) {
						continue;
					}
					RID light = light_storage->light_instance_get_base_light(light_instance);
					int32_t shadow_id = light_storage->light_instance_get_shadow_id(light_instance);
					if (light_storage->light_has_shadow(light) && shadow_id >= 0) {
						continue;
					}
					gl_cache.push_back((uint32_t)light_storage->light_instance_get_gl_id(light_instance));
				}
			}
		}

		GeometryInstanceSurface *surf = inst->surface_caches;

		float lod_distance = 0.0;

		if (p_render_data->cam_orthogonal || !valid_aabb_center) {
			lod_distance = 1.0f;
		} else {
			Vector3 aabb_min = inst->transformed_aabb.position;
			Vector3 aabb_max = inst->transformed_aabb.position + inst->transformed_aabb.size;
			Vector3 camera_position = p_render_data->main_cam_transform.origin;
			Vector3 surface_distance = Vector3(0.0, 0.0, 0.0).max(aabb_min - camera_position).max(camera_position - aabb_max);

			lod_distance = surface_distance.length();
		}

		while (surf) {
			// LOD

			if (p_render_data->screen_mesh_lod_threshold > 0.0 && mesh_storage->mesh_surface_has_lod(surf->surface)) {
				uint32_t indices = 0;
				surf->lod_index = mesh_storage->mesh_surface_get_lod(surf->surface, inst->lod_model_scale * inst->lod_bias, lod_distance * p_render_data->lod_distance_multiplier, p_render_data->screen_mesh_lod_threshold, indices);
				surf->index_count = indices;

				if (p_render_data->render_info) {
					indices = _gl_indices_to_primitives_3d(surf->primitive, indices);
					indices *= inst->instance_count > 0 ? inst->instance_count : 1;
					if constexpr (p_render_list == RENDER_LIST_OPAQUE) { //opaque
						p_render_data->render_info->info[RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += indices;
					} else if constexpr (p_render_list == RENDER_LIST_SECONDARY) { //shadow
						p_render_data->render_info->info[RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += indices;
					}
				}

			} else {
				surf->lod_index = 0;

				if (p_render_data->render_info) {
					uint32_t to_draw = mesh_storage->mesh_surface_get_vertices_drawn_count(surf->surface);
					to_draw = _gl_indices_to_primitives_3d(surf->primitive, to_draw);
					to_draw *= inst->instance_count > 0 ? inst->instance_count : 1;
					if constexpr (p_render_list == RENDER_LIST_OPAQUE) { //opaque
						p_render_data->render_info->info[RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += to_draw;
					} else if constexpr (p_render_list == RENDER_LIST_SECONDARY) { //shadow
						p_render_data->render_info->info[RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += to_draw;
					}
				}
			}

			if constexpr (p_pass_mode == PASS_MODE_COLOR) {
#ifdef DEBUG_ENABLED
				bool force_alpha = unlikely(get_debug_draw_mode() == RS::VIEWPORT_DEBUG_DRAW_OVERDRAW);
#else
				bool force_alpha = false;
#endif
				if (!force_alpha && (surf->flags & (GeometryInstanceSurface::FLAG_PASS_DEPTH | GeometryInstanceSurface::FLAG_PASS_OPAQUE))) {
					rl->add_element(surf);
				}
				if (force_alpha || (surf->flags & GeometryInstanceSurface::FLAG_PASS_ALPHA)) {
					render_list[RENDER_LIST_ALPHA].add_element(surf);
				}

				if (surf->flags & GeometryInstanceSurface::FLAG_USES_SCREEN_TEXTURE) {
					scene_state.used_screen_texture = true;
				}
				if (surf->flags & GeometryInstanceSurface::FLAG_USES_NORMAL_TEXTURE) {
					scene_state.used_normal_texture = true;
				}
				if (surf->flags & GeometryInstanceSurface::FLAG_USES_DEPTH_TEXTURE) {
					scene_state.used_depth_texture = true;
				}
			} else if constexpr (p_pass_mode == PASS_MODE_SHADOW) {
				if (surf->flags & GeometryInstanceSurface::FLAG_PASS_SHADOW) {
					rl->add_element(surf);
				}
			} else if constexpr (p_pass_mode == PASS_MODE_MATERIAL) {
				if (surf->flags & (GeometryInstanceSurface::FLAG_PASS_DEPTH | GeometryInstanceSurface::FLAG_PASS_OPAQUE | GeometryInstanceSurface::FLAG_PASS_ALPHA)) {
					rl->add_element(surf);
				}
			} else {
				if (surf->flags & (GeometryInstanceSurface::FLAG_PASS_DEPTH | GeometryInstanceSurface::FLAG_PASS_OPAQUE)) {
					rl->add_element(surf);
				}
			}

			surf->sort.depth_layer = depth_layer;
			surf = surf->next;
		}
	}
}

// Needs to be called after _setup_lights so that directional_light_count is accurate.
void RasterizerSceneGLES2::_setup_environment(const RenderDataGLES2 *p_render_data, bool p_no_fog, const Size2i &p_screen_size, bool p_flip_y, const Color &p_default_bg_color, bool p_pancake_shadows, float p_shadow_bias) {
	// Store the bound generation for this pass.
	scene_state.camera_pass_generation++;
	if (unlikely(scene_state.camera_pass_generation == 0)) {
		scene_state.program_camera_state_cache.clear();
		scene_state.camera_pass_generation = 1;
	}

	// Zero-out the ubo state.
	::new (&scene_state.ubo) SceneState::UBO{};

	scene_state.ubo.directional_light_count = p_render_data->directional_light_count;

	Projection correction;
	correction.set_depth_correction(p_flip_y, true, false);
	Projection projection = correction * p_render_data->cam_projection;

	GLES2::MaterialStorage::store_camera(projection, scene_state.ubo.projection_matrix);
	GLES2::MaterialStorage::store_camera(projection.inverse(), scene_state.ubo.inv_projection_matrix);
	GLES2::MaterialStorage::store_transform(p_render_data->cam_transform, scene_state.ubo.inv_view_matrix);
	GLES2::MaterialStorage::store_transform(p_render_data->inv_cam_transform, scene_state.ubo.view_matrix);

	scene_state.ubo.camera_visible_layers = p_render_data->camera_visible_layers;

	scene_state.ubo.z_far = p_render_data->z_far;
	scene_state.ubo.z_near = p_render_data->z_near;

	scene_state.ubo.viewport_size[0] = p_screen_size.x;
	scene_state.ubo.viewport_size[1] = p_screen_size.y;

	GLES2::MaterialStorage::store_transform(p_render_data->main_cam_transform, scene_state.ubo.main_cam_inv_view_matrix);

	// Project and reconstruct the view matrix to have up-to-date
	// to the cache values.
	scene_state.cached_projection = _gl_array_to_projection(scene_state.ubo.projection_matrix);
	scene_state.cached_inv_projection = _gl_array_to_projection(scene_state.ubo.inv_projection_matrix);
	_gl_reconstruct_view_matrix(scene_state.cached_view_matrix, scene_state.ubo.view_matrix);
	_gl_reconstruct_view_matrix(scene_state.cached_inv_view_matrix, scene_state.ubo.inv_view_matrix);
	_gl_reconstruct_view_matrix(scene_state.cached_main_cam_inv_view_matrix, scene_state.ubo.main_cam_inv_view_matrix);

	scene_state.ubo.shadow_bias = p_shadow_bias;
	scene_state.ubo.pancake_shadows = p_pancake_shadows;

	Size2 screen_pixel_size = Vector2(1.0, 1.0) / Size2(MAX(1, p_screen_size.x), MAX(1, p_screen_size.y));
	scene_state.ubo.screen_pixel_size[0] = screen_pixel_size.x;
	scene_state.ubo.screen_pixel_size[1] = screen_pixel_size.y;

	scene_state.ubo.time = time;
	scene_state.ubo.emissive_exposure_normalization = 1.0f;

	scene_state.tonemap_ubo.exposure = 1.0;
	scene_state.tonemap_ubo.white = 1.0;

	if (is_environment(p_render_data->environment)) {
		scene_state.tonemap_ubo.exposure = environment_get_exposure(p_render_data->environment);
		scene_state.tonemap_ubo.white = environment_get_white(p_render_data->environment);
	}

	if (p_render_data->camera_attributes.is_valid()) {
		scene_state.tonemap_ubo.exposure *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
	}

	if (is_environment(p_render_data->environment)) {
		Sky *sky = sky_owner.get_or_null(environment_get_sky(p_render_data->environment));

		glActiveTexture(GL_TEXTURE0 + GLES2_CONFIG->max_texture_image_units - 2);
		if (sky && sky->radiance != 0) {
			glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
		} else {
			glBindTexture(GL_TEXTURE_CUBE_MAP, GLES2::TextureStorage::get_singleton()->texture_get_texid(GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_CUBEMAP_BLACK)));
		}
		glActiveTexture(GL_TEXTURE0);

		RS::EnvironmentBG env_bg = environment_get_background(p_render_data->environment);
		RS::EnvironmentAmbientSource ambient_src = environment_get_ambient_source(p_render_data->environment);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(p_render_data->environment);

		scene_state.ubo.fog_enabled = !p_no_fog && environment_get_fog_enabled(p_render_data->environment);
		scene_state.ubo.fog_mode = uint32_t(environment_get_fog_mode(p_render_data->environment));
		scene_state.ubo.fog_density = environment_get_fog_density(p_render_data->environment);
		scene_state.ubo.fog_height = environment_get_fog_height(p_render_data->environment);
		scene_state.ubo.fog_height_density = environment_get_fog_height_density(p_render_data->environment);
		scene_state.ubo.fog_depth_curve = environment_get_fog_depth_curve(p_render_data->environment);
		scene_state.ubo.fog_depth_begin = environment_get_fog_depth_begin(p_render_data->environment);
		float fog_depth_end = environment_get_fog_depth_end(p_render_data->environment);
		if (fog_depth_end <= 0.0f) {
			fog_depth_end = scene_state.ubo.z_far;
		}
		scene_state.ubo.fog_depth_end = fog_depth_end;
		scene_state.ubo.fog_sun_scatter = environment_get_fog_sun_scatter(p_render_data->environment);
		scene_state.ubo.fog_aerial_perspective = environment_get_fog_aerial_perspective(p_render_data->environment);
		Color fog_color = environment_get_fog_light_color(p_render_data->environment).srgb_to_linear() * environment_get_fog_light_energy(p_render_data->environment);
		scene_state.ubo.fog_light_color[0] = fog_color.r;
		scene_state.ubo.fog_light_color[1] = fog_color.g;
		scene_state.ubo.fog_light_color[2] = fog_color.b;

		if (ambient_src == RS::ENV_AMBIENT_SOURCE_BG && (env_bg == RS::ENV_BG_CLEAR_COLOR || env_bg == RS::ENV_BG_COLOR)) {
			Color color = env_bg == RS::ENV_BG_CLEAR_COLOR ? p_default_bg_color : environment_get_bg_color(p_render_data->environment);
			color = color.srgb_to_linear();

			scene_state.ubo.ambient_light_color_energy[0] = color.r * bg_energy_multiplier;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * bg_energy_multiplier;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * bg_energy_multiplier;
			scene_state.ubo.use_ambient_light = 1;
			scene_state.ubo.use_ambient_cubemap = 0;
		} else {
			float energy = environment_get_ambient_light_energy(p_render_data->environment);
			Color color = environment_get_ambient_light(p_render_data->environment);
			color = color.srgb_to_linear();
			scene_state.ubo.ambient_light_color_energy[0] = color.r * energy;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * energy;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * energy;

			Basis sky_transform = environment_get_sky_orientation(p_render_data->environment);
			sky_transform = sky_transform.inverse() * p_render_data->cam_transform.basis;
			_gl_basis_to_array(sky_transform, scene_state.ubo.radiance_inverse_xform);

			scene_state.ubo.use_ambient_cubemap = ((ambient_src == RS::ENV_AMBIENT_SOURCE_BG && env_bg == RS::ENV_BG_SKY) || ambient_src == RS::ENV_AMBIENT_SOURCE_SKY) ? 1 : 0;
			scene_state.ubo.use_ambient_light = (scene_state.ubo.use_ambient_cubemap || ambient_src == RS::ENV_AMBIENT_SOURCE_COLOR) ? 1 : 0;
		}

		//specular
		RS::EnvironmentReflectionSource ref_src = environment_get_reflection_source(p_render_data->environment);
		if ((ref_src == RS::ENV_REFLECTION_SOURCE_BG && env_bg == RS::ENV_BG_SKY) || ref_src == RS::ENV_REFLECTION_SOURCE_SKY) {
			scene_state.ubo.use_reflection_cubemap = 1;
		} else {
			scene_state.ubo.use_reflection_cubemap = 0;
		}
	} else {
		glActiveTexture(GL_TEXTURE0 + GLES2_CONFIG->max_texture_image_units - 2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, GLES2::TextureStorage::get_singleton()->texture_get_texid(GLES2::TextureStorage::get_singleton()->texture_gl_get_default(GLES2::DEFAULT_GL_TEXTURE_CUBEMAP_BLACK)));
		glActiveTexture(GL_TEXTURE0);

		scene_state.ubo.use_ambient_light = 1;
		scene_state.ubo.use_ambient_cubemap = 0;
		Color linear_bg = p_default_bg_color.srgb_to_linear();
		scene_state.ubo.ambient_light_color_energy[0] = linear_bg.r;
		scene_state.ubo.ambient_light_color_energy[1] = linear_bg.g;
		scene_state.ubo.ambient_light_color_energy[2] = linear_bg.b;
		scene_state.ubo.ambient_light_color_energy[3] = 1.0f;
		scene_state.ubo.fog_enabled = false;
	}

	scene_state.cached_radiance_inverse_xform = _gl_array_to_basis(scene_state.ubo.radiance_inverse_xform);
}

// Puts lights into Uniform Buffers. Needs to be called before _fill_list as this caches the index of each light in the Uniform Buffer
void RasterizerSceneGLES2::_setup_lights(const RenderDataGLES2 *p_render_data, bool p_using_shadows, uint32_t &r_directional_light_count, uint32_t &r_omni_light_count, uint32_t &r_spot_light_count, uint32_t &r_directional_shadow_count) {
	GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();
	GLES2::Config *config = GLES2::Config::get_singleton();

	const Transform3D inverse_transform = p_render_data->inv_cam_transform;
	const PagedArray<RID> &lights = *p_render_data->lights;

	r_directional_light_count = 0;
	r_omni_light_count = 0;
	r_spot_light_count = 0;
	r_directional_shadow_count = 0;

	int num_lights = lights.size();

	for (int i = 0; i < num_lights; i++) {
		GLES2::LightInstance *li = GLES2::LightStorage::get_singleton()->get_light_instance(lights[i]);
		if (!li) {
			continue;
		}
		RID base = li->light;

		ERR_CONTINUE(base.is_null());

		RS::LightType type = light_storage->light_get_type(base);
		switch (type) {
			case RS::LIGHT_DIRECTIONAL: {
				if (r_directional_light_count >= RendererSceneRender::MAX_DIRECTIONAL_LIGHTS || light_storage->light_directional_get_sky_mode(base) == RS::LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY) {
					continue;
				}

				DirectionalLightData &light_data = scene_state.directional_lights[r_directional_light_count];
				Transform3D light_transform = li->transform;
				Vector3 direction = inverse_transform.basis.xform(light_transform.basis.xform(Vector3(0, 0, 1))).normalized();

				light_data.direction[0] = direction.x;
				light_data.direction[1] = direction.y;
				light_data.direction[2] = direction.z;
				light_data.bake_mode = light_storage->light_get_bake_mode(base);

				float sign = light_storage->light_is_negative(base) ? -1 : 1;
				light_data.energy = sign * light_storage->light_get_param(base, RS::LIGHT_PARAM_ENERGY);

				if (is_using_physical_light_units()) {
					light_data.energy *= light_storage->light_get_param(base, RS::LIGHT_PARAM_INTENSITY);
				} else {
					light_data.energy *= Math_PI;
				}

				if (p_render_data->camera_attributes.is_valid()) {
					light_data.energy *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
				}

				Color linear_col = light_storage->light_get_color(base).srgb_to_linear();
				light_data.color[0] = linear_col.r;
				light_data.color[1] = linear_col.g;
				light_data.color[2] = linear_col.b;

				float size = light_storage->light_get_param(base, RS::LIGHT_PARAM_SIZE);
				light_data.size = 1.0f - Math::cos(Math::deg_to_rad(size));

				light_data.specular = light_storage->light_get_param(base, RS::LIGHT_PARAM_SPECULAR);

				bool has_shadow = p_using_shadows && light_storage->light_has_shadow(base);
				light_data.shadow_opacity = has_shadow ? light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_OPACITY) : 0.0f;

				if (has_shadow && r_directional_shadow_count < MAX_DIRECTIONAL_LIGHTS) {
					DirectionalShadowData &shadow_data = scene_state.directional_shadows[r_directional_shadow_count];
					RS::LightDirectionalShadowMode shadow_mode = light_storage->light_directional_get_shadow_mode(base);
					int limit = shadow_mode == RS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL ? 0 : (shadow_mode == RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS ? 1 : 3);

					shadow_data.shadow_atlas_pixel_size = 1.0f / MAX(1.0f, light_storage->directional_shadow_get_size());
					shadow_data.blend_splits = uint32_t((shadow_mode != RS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL) && light_storage->light_directional_get_blend_splits(base));

					for (int j = 0; j < 4; j++) {
						Rect2 atlas_rect = li->shadow_transform[j].atlas_rect;
						Projection correction;
						correction.set_depth_correction(false, true, false);
						Projection matrix = correction * li->shadow_transform[j].camera;
						float split = li->shadow_transform[MIN(limit, j)].split;

						Projection bias;
						bias.set_light_bias();
						Projection rectm;
						rectm.set_light_atlas_rect(atlas_rect);

						Transform3D modelview = (inverse_transform * li->shadow_transform[j].transform).inverse();

						shadow_data.direction[0] = light_data.direction[0];
						shadow_data.direction[1] = light_data.direction[1];
						shadow_data.direction[2] = light_data.direction[2];

						Projection shadow_mtx = rectm * bias * matrix * modelview;
						shadow_data.shadow_split_offsets[j] = split;
						shadow_data.shadow_normal_bias[j] = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_NORMAL_BIAS) * li->shadow_transform[j].shadow_texel_size;
						GLES2::MaterialStorage::store_camera(shadow_mtx, shadow_data.shadow_matrices[j]);
						scene_state.cached_directional_shadow_matrices[r_directional_shadow_count][j] = _gl_array_to_projection(shadow_data.shadow_matrices[j]);
					}
					float fade_start = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_FADE_START);
					shadow_data.fade_from = -shadow_data.shadow_split_offsets[3] * MIN(fade_start, 0.999f);
					shadow_data.fade_to = -shadow_data.shadow_split_offsets[3];

					r_directional_shadow_count++;
				}

				r_directional_light_count++;
			} break;
			case RS::LIGHT_OMNI:
			case RS::LIGHT_SPOT: {
				const bool is_omni = (type == RS::LIGHT_OMNI);
				uint32_t &light_count = is_omni ? r_omni_light_count : r_spot_light_count;
				if (light_count >= (uint32_t)config->max_renderable_lights) {
					continue;
				}
				const real_t distance = p_render_data->cam_transform.origin.distance_to(li->transform.origin);

				if (light_storage->light_is_distance_fade_enabled(li->light)) {
					const float fade_begin = light_storage->light_get_distance_fade_begin(li->light);
					const float fade_length = light_storage->light_get_distance_fade_length(li->light);

					if (distance > fade_begin + fade_length) {
						continue;
					}
				}

				InstanceSort<GLES2::LightInstance> *sort_arr = is_omni ? scene_state.omni_light_sort : scene_state.spot_light_sort;
				sort_arr[light_count].instance = li;
				sort_arr[light_count].depth = distance;
				light_count++;
			} break;
		}
		li->last_pass = RSG::rasterizer->get_frame_number();
	}

	if (r_omni_light_count) {
		SortArray<InstanceSort<GLES2::LightInstance>> sorter{};
		sorter.sort(scene_state.omni_light_sort, r_omni_light_count);
	}

	if (r_spot_light_count) {
		SortArray<InstanceSort<GLES2::LightInstance>> sorter{};
		sorter.sort(scene_state.spot_light_sort, r_spot_light_count);
	}

	for (uint32_t i = 0; i < (r_omni_light_count + r_spot_light_count); i++) {
		uint32_t index = (i < r_omni_light_count) ? i : i - r_omni_light_count;
		LightData &light_data = (i < r_omni_light_count) ? scene_state.omni_lights[index] : scene_state.spot_lights[index];
		RS::LightType type = (i < r_omni_light_count) ? RS::LIGHT_OMNI : RS::LIGHT_SPOT;
		GLES2::LightInstance *li = (i < r_omni_light_count) ? scene_state.omni_light_sort[index].instance : scene_state.spot_light_sort[index].instance;
		RID base = li->light;

		li->gl_id = index;

		Transform3D light_transform = li->transform;
		Vector3 pos = inverse_transform.xform(light_transform.origin);

		light_data.position[0] = pos.x;
		light_data.position[1] = pos.y;
		light_data.position[2] = pos.z;
		light_data.world_position[0] = light_transform.origin.x;
		light_data.world_position[1] = light_transform.origin.y;
		light_data.world_position[2] = light_transform.origin.z;
		light_data.bake_mode = light_storage->light_get_bake_mode(base);

		float radius = MAX(0.001f, light_storage->light_get_param(base, RS::LIGHT_PARAM_RANGE));
		light_data.inv_radius = 1.0f / radius;

		Vector3 direction = inverse_transform.basis.xform(light_transform.basis.xform(Vector3(0, 0, -1))).normalized();

		light_data.direction[0] = direction.x;
		light_data.direction[1] = direction.y;
		light_data.direction[2] = direction.z;
		light_data.size = light_storage->light_get_param(base, RS::LIGHT_PARAM_SIZE);

		float sign = light_storage->light_is_negative(base) ? -1 : 1;
		Color linear_col = light_storage->light_get_color(base).srgb_to_linear();
		
		real_t distance = (i < r_omni_light_count) ? scene_state.omni_light_sort[index].depth : scene_state.spot_light_sort[index].depth;
		float fade = 1.0;
		float shadow_opacity_fade = 1.0;

		if (light_storage->light_is_distance_fade_enabled(base)) {
			float fade_begin = light_storage->light_get_distance_fade_begin(base);
			float fade_shadow = light_storage->light_get_distance_fade_shadow(base);
			float fade_length = light_storage->light_get_distance_fade_length(base);

			if (distance > fade_begin) {
				fade = Math::smoothstep(0.0f, 1.0f, 1.0f - float(distance - fade_begin) / fade_length);
			}
			if (distance > fade_shadow) {
				shadow_opacity_fade = Math::smoothstep(0.0f, 1.0f, 1.0f - float(distance - fade_shadow) / fade_length);
			}
		}

		float energy = sign * light_storage->light_get_param(base, RS::LIGHT_PARAM_ENERGY) * fade;

		if (is_using_physical_light_units()) {
			energy *= light_storage->light_get_param(base, RS::LIGHT_PARAM_INTENSITY);
			if (type == RS::LIGHT_OMNI) {
				energy *= 1.0f / (Math_PI * 4.0f);
			} else {
				energy *= 1.0f / Math_PI;
			}
		} else {
			energy *= Math_PI;
		}

		if (p_render_data->camera_attributes.is_valid()) {
			energy *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
		}

		light_data.color[0] = linear_col.r * energy;
		light_data.color[1] = linear_col.g * energy;
		light_data.color[2] = linear_col.b * energy;

		light_data.attenuation = light_storage->light_get_param(base, RS::LIGHT_PARAM_ATTENUATION);
		light_data.inv_spot_attenuation = 1.0f / light_storage->light_get_param(base, RS::LIGHT_PARAM_SPOT_ATTENUATION);
		float spot_angle = light_storage->light_get_param(base, RS::LIGHT_PARAM_SPOT_ANGLE);
		light_data.cos_spot_angle = Math::cos(Math::deg_to_rad(spot_angle));
		light_data.specular_amount = light_storage->light_get_param(base, RS::LIGHT_PARAM_SPECULAR) * 2.0f;

		bool needs_shadow = p_using_shadows && light_storage->owns_shadow_atlas(p_render_data->shadow_atlas) && light_storage->shadow_atlas_owns_light_instance(p_render_data->shadow_atlas, li->self) && light_storage->light_has_shadow(base);
		bool in_shadow_range = true;

		if (needs_shadow && light_storage->light_is_distance_fade_enabled(base)) {
			float fade_shadow = light_storage->light_get_distance_fade_shadow(base);
			float fade_length = light_storage->light_get_distance_fade_length(base);
			if (distance > fade_shadow + fade_length) {
				in_shadow_range = false;
			}
		}

		light_data.shadow_opacity = (needs_shadow && in_shadow_range) ? light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_OPACITY) * shadow_opacity_fade : 0.0f;

		if (needs_shadow && in_shadow_range) {
			uint32_t shadow_idx = (type == RS::LIGHT_OMNI) ? i : r_omni_light_count + (i - r_omni_light_count);
			if (shadow_idx < config->max_renderable_lights * 2) {
				ShadowData &shadow_data = scene_state.positional_shadows[shadow_idx];
				li->shadow_id = shadow_idx;

				float shadow_texel_size = light_storage->light_instance_get_shadow_texel_size(li->self, p_render_data->shadow_atlas);
				shadow_data.shadow_atlas_pixel_size = shadow_texel_size;
				shadow_data.shadow_normal_bias = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_NORMAL_BIAS) * shadow_texel_size * 10.0f;

				shadow_data.light_position[0] = light_data.position[0];
				shadow_data.light_position[1] = light_data.position[1];
				shadow_data.light_position[2] = light_data.position[2];

				if (type == RS::LIGHT_OMNI) {
					Transform3D proj = (inverse_transform * light_transform).inverse();
					GLES2::MaterialStorage::store_transform(proj, shadow_data.shadow_matrix);
				} else if (type == RS::LIGHT_SPOT) {
					Transform3D modelview = (inverse_transform * light_transform).inverse();
					Projection bias;
					bias.set_light_bias();

					Projection correction;
					correction.set_depth_correction(false, true, false);
					Projection cm = correction * li->shadow_transform[0].camera;
					Projection shadow_mtx = bias * cm * modelview;
					GLES2::MaterialStorage::store_camera(shadow_mtx, shadow_data.shadow_matrix);
				}
			}
		} else {
			li->shadow_id = -1;
		}
	}

	scene_state.omni_light_count = r_omni_light_count;
	scene_state.spot_light_count = r_spot_light_count;
	scene_state.ubo.directional_light_count = r_directional_light_count;
}

// Render shadows
void RasterizerSceneGLES2::_render_shadows(const RenderDataGLES2 *p_render_data, const Size2i &p_viewport_size) {
	GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();

	LocalVector<int> cube_shadows;
	LocalVector<int> shadows;
	LocalVector<int> directional_shadows;

	float lod_distance_multiplier = p_render_data->cam_projection.get_lod_multiplier();

	for (int i = 0; i < p_render_data->render_shadow_count; i++) {
		RID li = p_render_data->render_shadows[i].light;
		RID base = light_storage->light_instance_get_base_light(li);

		if (light_storage->light_get_type(base) == RS::LIGHT_DIRECTIONAL) {
			directional_shadows.push_back(i);
		} else if (light_storage->light_get_type(base) == RS::LIGHT_OMNI && light_storage->light_omni_get_shadow_mode(base) == RS::LIGHT_OMNI_SHADOW_CUBE) {
			cube_shadows.push_back(i);
		} else {
			shadows.push_back(i);
		}
	}

	if (directional_shadows.size()) {
		light_storage->update_directional_shadow_atlas();
	}

	bool render_shadows = directional_shadows.size() || shadows.size() || cube_shadows.size();

	if (render_shadows) {
		RENDER_TIMESTAMP("Render Shadows");

		for (const int &index : cube_shadows) {
			_render_shadow_pass(p_render_data->render_shadows[index].light, p_render_data->shadow_atlas, p_render_data->render_shadows[index].pass, p_render_data->render_shadows[index].instances, lod_distance_multiplier, p_render_data->screen_mesh_lod_threshold, p_render_data->render_info, p_viewport_size, p_render_data->cam_transform);
		}
		for (uint32_t i = 0; i < directional_shadows.size(); i++) {
			_render_shadow_pass(p_render_data->render_shadows[directional_shadows[i]].light, p_render_data->shadow_atlas, p_render_data->render_shadows[directional_shadows[i]].pass, p_render_data->render_shadows[directional_shadows[i]].instances, lod_distance_multiplier, p_render_data->screen_mesh_lod_threshold, p_render_data->render_info, p_viewport_size, p_render_data->cam_transform);
		}
		for (uint32_t i = 0; i < shadows.size(); i++) {
			_render_shadow_pass(p_render_data->render_shadows[shadows[i]].light, p_render_data->shadow_atlas, p_render_data->render_shadows[shadows[i]].pass, p_render_data->render_shadows[shadows[i]].instances, lod_distance_multiplier, p_render_data->screen_mesh_lod_threshold, p_render_data->render_info, p_viewport_size, p_render_data->cam_transform);
		}
	}
}

void RasterizerSceneGLES2::_render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, RenderingMethod::RenderInfo *p_render_info, const Size2i &p_viewport_size, const Transform3D &p_main_cam_transform) {
	GLES2::LightStorage *light_storage = GLES2::LightStorage::get_singleton();

	ERR_FAIL_COND(!light_storage->owns_light_instance(p_light));

	RID base = light_storage->light_instance_get_base_light(p_light);

	float zfar = 0.0f;
	bool use_pancake = false;
	float shadow_bias = 0.0f;
	bool reverse_cull = false;

	Projection light_projection;
	Transform3D light_transform;
	GLuint shadow_fb = 0;
	Rect2i atlas_rect;

	if (light_storage->light_get_type(base) == RS::LIGHT_DIRECTIONAL) {
		uint64_t last_scene_shadow_pass = light_storage->light_instance_get_shadow_pass(p_light);
		if (last_scene_shadow_pass != get_scene_pass()) {
			light_storage->light_instance_set_directional_rect(p_light, light_storage->get_directional_shadow_rect());
			light_storage->directional_shadow_increase_current_light();
			light_storage->light_instance_set_shadow_pass(p_light, get_scene_pass());
		}

		atlas_rect = light_storage->light_instance_get_directional_rect(p_light);

		if (light_storage->light_directional_get_shadow_mode(base) == RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_4_SPLITS) {
			atlas_rect.size.width /= 2;
			atlas_rect.size.height /= 2;

			if (p_pass == 1) {
				atlas_rect.position.x += atlas_rect.size.width;
			} else if (p_pass == 2) {
				atlas_rect.position.y += atlas_rect.size.height;
			} else if (p_pass == 3) {
				atlas_rect.position += atlas_rect.size;
			}
		} else if (light_storage->light_directional_get_shadow_mode(base) == RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS) {
			atlas_rect.size.height /= 2;
			if (p_pass == 1) {
				atlas_rect.position.y += atlas_rect.size.height;
			}
		}

		use_pancake = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_PANCAKE_SIZE) > 0.0f;
		light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
		light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);

		float directional_shadow_size = light_storage->directional_shadow_get_size();
		Rect2 atlas_rect_norm = atlas_rect;
		atlas_rect_norm.position /= directional_shadow_size;
		atlas_rect_norm.size /= directional_shadow_size;
		light_storage->light_instance_set_directional_shadow_atlas_rect(p_light, p_pass, atlas_rect_norm);

		zfar = RSG::light_storage->light_get_param(base, RS::LIGHT_PARAM_RANGE);
		shadow_fb = light_storage->direction_shadow_get_fb();
		reverse_cull = !light_storage->light_get_reverse_cull_face_mode(base);

		float bias_scale = light_storage->light_instance_get_shadow_bias_scale(p_light, p_pass);
		shadow_bias = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_BIAS) / 100.0f * bias_scale;

	} else {
		ERR_FAIL_COND(!light_storage->owns_shadow_atlas(p_shadow_atlas));
		ERR_FAIL_COND(!light_storage->shadow_atlas_owns_light_instance(p_shadow_atlas, p_light));

		uint32_t key = light_storage->shadow_atlas_get_light_instance_key(p_shadow_atlas, p_light);
		uint32_t quadrant = (key >> GLES2::LightStorage::QUADRANT_SHIFT) & 0x3;
		uint32_t shadow = key & GLES2::LightStorage::SHADOW_INDEX_MASK;

		ERR_FAIL_INDEX((int)shadow, light_storage->shadow_atlas_get_quadrant_shadows_length(p_shadow_atlas, quadrant));

		int shadow_size = light_storage->shadow_atlas_get_quadrant_shadow_size(p_shadow_atlas, quadrant);
		shadow_fb = light_storage->shadow_atlas_get_quadrant_shadow_fb(p_shadow_atlas, quadrant, shadow);

		zfar = light_storage->light_get_param(base, RS::LIGHT_PARAM_RANGE);
		reverse_cull = !light_storage->light_get_reverse_cull_face_mode(base);

		if (light_storage->light_get_type(base) == RS::LIGHT_OMNI) {
			if (light_storage->light_omni_get_shadow_mode(base) == RS::LIGHT_OMNI_SHADOW_CUBE) {
				GLuint shadow_texture = light_storage->shadow_atlas_get_quadrant_shadow_texture(p_shadow_atlas, quadrant, shadow);

				GLES2::TextureStorage::get_singleton()->bind_framebuffer(shadow_fb);
				GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: glBindFramebuffer (omni)");

				if (GLES2_CONFIG->support_depth_texture) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, CUBE_MAP_FACES[p_pass], shadow_texture, 0);
				} else {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CUBE_MAP_FACES[p_pass], shadow_texture, 0);
				}
				GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: glFramebufferTexture2D (omni)");

				light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
				light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);
				shadow_size = shadow_size / 2;
			}
			shadow_bias = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_BIAS);
		} else if (light_storage->light_get_type(base) == RS::LIGHT_SPOT) {
			light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);
			light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

			shadow_bias = light_storage->light_get_param(base, RS::LIGHT_PARAM_SHADOW_BIAS) / 10.0f;
			shadow_bias *= light_storage->light_get_param(base, RS::LIGHT_PARAM_RANGE);
		}
		atlas_rect.position.x = 0;
		atlas_rect.position.y = 0;
		atlas_rect.size.x = shadow_size;
		atlas_rect.size.y = shadow_size;
	}

	RenderDataGLES2 render_data;
	render_data.cam_projection = light_projection;
	render_data.cam_transform = light_transform;
	render_data.inv_cam_transform = light_transform.affine_inverse();
	render_data.z_far = zfar;
	render_data.z_near = 0.0f;
	render_data.lod_distance_multiplier = p_lod_distance_multiplier;
	render_data.main_cam_transform = p_main_cam_transform;
	render_data.instances = &p_instances;
	render_data.render_info = p_render_info;

	_setup_environment(&render_data, true, p_viewport_size, false, Color(), use_pancake, shadow_bias);

	if (get_debug_draw_mode() == RS::VIEWPORT_DEBUG_DRAW_DISABLE_LOD) {
		render_data.screen_mesh_lod_threshold = 0.0f;
	} else {
		render_data.screen_mesh_lod_threshold = p_screen_mesh_lod_threshold;
	}

	_fill_render_list<RENDER_LIST_SECONDARY, PASS_MODE_SHADOW>(&render_data);
	if (!bdata.settings_use_batching) {
		// When batching is enabled, the batcher already sorts these
		// naturally.
		render_list[RENDER_LIST_SECONDARY].sort_by_key();
	}

	GLES2::TextureStorage::get_singleton()->bind_framebuffer(shadow_fb);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: glBindFramebuffer");
	glViewport(atlas_rect.position.x, atlas_rect.position.y, atlas_rect.size.x, atlas_rect.size.y);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: glViewport");

	scene_state.reset_gl_state();
	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(true);
	glDepthFunc(GL_GREATER);

	if (GLES2_CONFIG->support_depth_texture) {
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	} else {
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	RasterizerGLES2::clear_depth(0.0f);

	scene_state.enable_gl_scissor_test(true);
	glScissor(atlas_rect.position.x, atlas_rect.position.y, atlas_rect.size.x, atlas_rect.size.y);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: glScissor");

	if (GLES2_CONFIG->support_depth_texture) {
		glClear(GL_DEPTH_BUFFER_BIT);
	} else {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: glClear");

	scene_state.enable_gl_scissor_test(false);

	uint64_t spec_constant_base_flags = (
		SceneShaderGLES2::DISABLE_LIGHTMAP |
		SceneShaderGLES2::DISABLE_LIGHT_DIRECTIONAL |
		SceneShaderGLES2::DISABLE_LIGHT_OMNI |
		SceneShaderGLES2::DISABLE_LIGHT_SPOT |
		SceneShaderGLES2::DISABLE_FOG |
		SceneShaderGLES2::RENDER_SHADOWS
	);

	RenderListParameters render_list_params(render_list[RENDER_LIST_SECONDARY].elements.ptr(), render_list[RENDER_LIST_SECONDARY].elements.size(), reverse_cull, spec_constant_base_flags, false);

	_render_list_template<PASS_MODE_SHADOW>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_SECONDARY].elements.size());

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	scene_state.enable_gl_depth_test(false);
	glDisable(GL_CULL_FACE);
	scene_state.cull_mode = RS::CULL_MODE_DISABLED;
	GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_shadow_pass: cleanup");
}

/* BINDING UTILITIES */

void RasterizerSceneGLES2::_bind_sky_directional_lights(RID p_version, SkyShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHT_COUNT, (int)sky_globals.directional_light_count, p_version, p_variant, p_spec_constants);
	if (sky_globals.directional_light_count > 0) {
		const DirectionalLightData &light = sky_globals.directional_lights[0];
		Vector4 dir_energy(light.direction[0], light.direction[1], light.direction[2], light.energy);
		Vector4 col_size(light.color[0], light.color[1], light.color[2], light.size);
		int32_t enabled = light.enabled ? 1 : 0;

		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_DIRECTION_ENERGY, dir_energy, p_version, p_variant, p_spec_constants);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_COLOR_SIZE, col_size, p_version, p_variant, p_spec_constants);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_ENABLED, enabled, p_version, p_variant, p_spec_constants);
	}
}

void RasterizerSceneGLES2::_bind_scene_camera_uniforms(RID p_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
	// Only bind the uniforms if the program actually changes between pasess.
	// Doing this once per unique pass improves performance by a lot.
	if (scene_state.check_and_mark_program_camera_bound(p_version, p_variant, p_spec_constants)) {
		return;
	}

	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::PROJECTION_MATRIX, scene_state.cached_projection, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::INV_PROJECTION_MATRIX, scene_state.cached_inv_projection, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::VIEW_MATRIX, scene_state.cached_view_matrix, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::INV_VIEW_MATRIX, scene_state.cached_inv_view_matrix, p_version, p_variant, p_spec_constants);

	Vector2 vp_size(scene_state.ubo.viewport_size[0], scene_state.ubo.viewport_size[1]);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::VIEWPORT_SIZE, vp_size, p_version, p_variant, p_spec_constants);

	Vector2 screen_pixel_size(scene_state.ubo.screen_pixel_size[0], scene_state.ubo.screen_pixel_size[1]);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SCREEN_PIXEL_SIZE, screen_pixel_size, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::TIME, (float)scene_state.ubo.time, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Z_FAR, scene_state.ubo.z_far, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Z_NEAR, scene_state.ubo.z_near, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::MAIN_CAM_INV_VIEW_MATRIX, scene_state.cached_main_cam_inv_view_matrix, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::SHADOW_BIAS, scene_state.ubo.shadow_bias, p_version, p_variant, p_spec_constants);

	const bool is_depth_variant = (
		p_variant == SceneShaderGLES2::MODE_DEPTH ||
		p_variant == SceneShaderGLES2::MODE_DEPTH_INSTANCING ||
		_batch_util_is_depth_pass()
	);
	if (is_depth_variant) {
		// We run parallel tracks for depth variant passes
		// that we can safely skip re-binding the uniforms
		// for performance.
		return;
	}

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::EMISSIVE_EXPOSURE_NORMALIZATION, scene_state.ubo.emissive_exposure_normalization, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::AMBIENT_LIGHT_COLOR_ENERGY, Color(scene_state.ubo.ambient_light_color_energy[0], scene_state.ubo.ambient_light_color_energy[1], scene_state.ubo.ambient_light_color_energy[2], scene_state.ubo.ambient_light_color_energy[3]), p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::AMBIENT_COLOR_SKY_MIX, scene_state.ubo.ambient_color_sky_mix, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_AMBIENT_LIGHT, (bool)scene_state.ubo.use_ambient_light, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_AMBIENT_CUBEMAP, (bool)scene_state.ubo.use_ambient_cubemap, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::USE_REFLECTION_CUBEMAP, (bool)scene_state.ubo.use_reflection_cubemap, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::RADIANCE_INVERSE_XFORM, scene_state.cached_radiance_inverse_xform, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_LIGHT_COUNT, (int)scene_state.ubo.directional_light_count, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_ENABLED, (bool)scene_state.ubo.fog_enabled, p_version, p_variant, p_spec_constants);
	if (scene_state.ubo.fog_enabled && !(p_spec_constants & SceneShaderGLES2::DISABLE_FOG)) {
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_MODE, (int)scene_state.ubo.fog_mode, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_DENSITY, scene_state.ubo.fog_density, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_HEIGHT, scene_state.ubo.fog_height, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_HEIGHT_DENSITY, scene_state.ubo.fog_height_density, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_DEPTH_CURVE, scene_state.ubo.fog_depth_curve, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_DEPTH_BEGIN, scene_state.ubo.fog_depth_begin, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_DEPTH_END, scene_state.ubo.fog_depth_end, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_LIGHT_COLOR, Vector3(scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2]), p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_SUN_SCATTER, scene_state.ubo.fog_sun_scatter, p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::FOG_AERIAL_PERSPECTIVE, scene_state.ubo.fog_aerial_perspective, p_version, p_variant, p_spec_constants);
	}

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::EXPOSURE, scene_state.tonemap_ubo.exposure, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WHITE, scene_state.tonemap_ubo.white, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::TONEMAPPER, (int)scene_state.tonemap_ubo.tonemapper, p_version, p_variant, p_spec_constants);

	if (!(p_spec_constants & SceneShaderGLES2::DISABLE_LIGHT_DIRECTIONAL)) {
		const uint32_t d_idx = scene_state.is_directional_additive_replay ? scene_state.current_directional_light_index : 0U;
		if (d_idx < scene_state.ubo.directional_light_count) {
			const DirectionalLightData &dir_light = scene_state.directional_lights[d_idx];
			int base_idx = SceneShaderGLES2::DIRECTIONAL_LIGHTS_DATA_0_DIRECTION_ENERGY;
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(dir_light.direction[0], dir_light.direction[1], dir_light.direction[2], dir_light.energy), p_version, p_variant, p_spec_constants);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(dir_light.color[0], dir_light.color[1], dir_light.color[2], dir_light.size), p_version, p_variant, p_spec_constants);

			Vector4 extended_data(1.0f, (float)dir_light.bake_mode, dir_light.shadow_opacity, dir_light.specular);
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), extended_data, p_version, p_variant, p_spec_constants);

			if (dir_light.shadow_opacity > 0.0f && scene_state.directional_shadows != nullptr) {
				uint32_t shadow_idx = 0;
				for (uint32_t s = 0; s < MAX_DIRECTIONAL_LIGHTS; s++) {
					if (
						scene_state.directional_shadows[s].direction[0] == dir_light.direction[0] &&
						scene_state.directional_shadows[s].direction[1] == dir_light.direction[1] &&
						scene_state.directional_shadows[s].direction[2] == dir_light.direction[2]
					) {
						shadow_idx = s;
						break;
					}
				}
				DirectionalShadowData &shadow_data = scene_state.directional_shadows[shadow_idx];
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_DIRECTION, Vector3(shadow_data.direction[0], shadow_data.direction[1], shadow_data.direction[2]), p_version, p_variant, p_spec_constants);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_ATLAS_PIXEL_SIZE, shadow_data.shadow_atlas_pixel_size, p_version, p_variant, p_spec_constants);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_NORMAL_BIAS, Vector4(shadow_data.shadow_normal_bias[0], shadow_data.shadow_normal_bias[1], shadow_data.shadow_normal_bias[2], shadow_data.shadow_normal_bias[3]), p_version, p_variant, p_spec_constants);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_SPLIT_OFFSETS, Vector4(shadow_data.shadow_split_offsets[0], shadow_data.shadow_split_offsets[1], shadow_data.shadow_split_offsets[2], shadow_data.shadow_split_offsets[3]), p_version, p_variant, p_spec_constants);

				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_MATRIX1, scene_state.cached_directional_shadow_matrices[shadow_idx][0], p_version, p_variant, p_spec_constants);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_MATRIX2, scene_state.cached_directional_shadow_matrices[shadow_idx][1], p_version, p_variant, p_spec_constants);

				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_FADE_FROM, shadow_data.fade_from, p_version, p_variant, p_spec_constants);
				material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::DIRECTIONAL_SHADOW_FADE_TO, shadow_data.fade_to, p_version, p_variant, p_spec_constants);
			}
		}
	}
}

void RasterizerSceneGLES2::render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, const RenderSDFGIUpdateData *p_sdfgi_update_data, RenderingMethod::RenderInfo *r_render_info) {
	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();
	RENDER_TIMESTAMP("Setup 3D Scene");

	bool apply_color_adjustments_in_post = false;
	bool is_reflection_probe = p_reflection_probe.is_valid();

	Ref<RenderSceneBuffersGLES2> rb = p_render_buffers;
	ERR_FAIL_COND(rb.is_null());

	if (rb->get_scaling_3d_mode() != RS::VIEWPORT_SCALING_3D_MODE_OFF) {
		// If we're scaling, we apply tonemapping etc. in post, so disable it during rendering
		apply_color_adjustments_in_post = true;
	}

	GLES2::RenderTarget *rt = nullptr;
	if (!is_reflection_probe) {
		rt = texture_storage->get_render_target(rb->render_target);
		ERR_FAIL_NULL(rt);
	}

	/* TODO: Add GLOW for this renderer (maybe M3.6?)
	bool glow_enabled = false;
	if (p_environment.is_valid()) {
		glow_enabled = environment_get_glow_enabled(p_environment);
		if (glow_enabled) {
			// If glow is enabled, we apply tonemapping etc. in post, so disable it during rendering
			apply_color_adjustments_in_post = true;
		}
	}
	*/

	// Assign render data
	// Use the format from rendererRD
	RenderDataGLES2 render_data;
	{
		render_data.render_buffers = rb;
		if (rt) {
			render_data.transparent_bg = rt->is_transparent;
		}
		scene_state.transparent_bg = render_data.transparent_bg;

		render_data.cam_transform = p_camera_data->main_transform;
		render_data.inv_cam_transform = render_data.cam_transform.affine_inverse();
		render_data.cam_projection = p_camera_data->main_projection;
		render_data.cam_orthogonal = p_camera_data->is_orthogonal;
		render_data.camera_visible_layers = p_camera_data->visible_layers;
		render_data.main_cam_transform = p_camera_data->main_transform;
		render_data.z_near = p_camera_data->main_projection.get_z_near();
		render_data.z_far = p_camera_data->main_projection.get_z_far();

		render_data.instances = &p_instances;
		render_data.lights = &p_lights;
		render_data.environment = p_environment;
		render_data.camera_attributes = p_camera_attributes;
		render_data.shadow_atlas = p_shadow_atlas;
		render_data.reflection_probe = p_reflection_probe;
		render_data.reflection_probe_pass = p_reflection_probe_pass;
		render_data.render_info = r_render_info;
		render_data.render_shadows = p_render_shadows;
		render_data.render_shadow_count = p_render_shadow_count;
	}

	Color clear_color = texture_storage->get_default_clear_color();
	if (rb.is_valid()) {
		clear_color = texture_storage->render_target_get_clear_request_color(rb->render_target);
	}
	Size2i screen_size(1, 1);

	if (rb.is_valid()) {
		screen_size.width = rb->internal_size.width;
		screen_size.height = rb->internal_size.height;
	}

	bool reverse_cull = render_data.cam_transform.basis.determinant() < 0;
	bool use_wireframe = get_debug_draw_mode() == RS::VIEWPORT_DEBUG_DRAW_WIREFRAME;

	// Tonemapping
	SceneState::TonemapUBO tonemap_ubo;
	if (render_data.environment.is_valid()) {
		bool use_bcs = environment_get_adjustments_enabled(render_data.environment);
		if (use_bcs) {
			apply_color_adjustments_in_post = true;
		}

		tonemap_ubo.exposure = environment_get_exposure(render_data.environment);
		tonemap_ubo.white = environment_get_white(render_data.environment);
		tonemap_ubo.tonemapper = int32_t(environment_get_tone_mapper(render_data.environment));

		tonemap_ubo.brightness = environment_get_adjustments_brightness(render_data.environment);
		tonemap_ubo.contrast = environment_get_adjustments_contrast(render_data.environment);
		tonemap_ubo.saturation = environment_get_adjustments_saturation(render_data.environment);
	}
	scene_state.tonemap_ubo = tonemap_ubo;

	bool draw_sky = false;
	bool draw_sky_fog_only = false;
	bool keep_color = false;
	float sky_energy_multiplier = 1.0;
	bool flip_y = !is_reflection_probe;

	if (rt && rt->overridden.color.is_valid()) {
		// If we've overridden the render target's color texture, then don't render upside down.
		// We're probably rendering directly to an XR device.
		flip_y = false;
	}
	if (!flip_y) {
		reverse_cull = !reverse_cull;
	}

	if (get_debug_draw_mode() == RS::VIEWPORT_DEBUG_DRAW_OVERDRAW) {
		clear_color = Color(0, 0, 0, 1);
	} else if (render_data.environment.is_valid()) {
		RS::EnvironmentBG bg_mode = environment_get_background(render_data.environment);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(render_data.environment);
		bg_energy_multiplier *= environment_get_bg_intensity(render_data.environment);

		if (render_data.camera_attributes.is_valid()) {
			bg_energy_multiplier *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(render_data.camera_attributes);
		}

		switch (bg_mode) {
			case RS::ENV_BG_CLEAR_COLOR: {
				float c_a = clear_color.a;
				clear_color *= bg_energy_multiplier;
				clear_color.a = c_a;
				if (environment_get_fog_enabled(render_data.environment)) {
					draw_sky_fog_only = true;
					GLES2::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));
				}
			} break;
			case RS::ENV_BG_COLOR: {
				clear_color = environment_get_bg_color(render_data.environment);
				float c_a = clear_color.a;
				clear_color *= bg_energy_multiplier;
				clear_color.a = c_a;
				if (environment_get_fog_enabled(render_data.environment)) {
					draw_sky_fog_only = true;
					GLES2::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));
				}
			} break;
			case RS::ENV_BG_SKY: {
				draw_sky = true;
			} break;
			case RS::ENV_BG_CANVAS:
			case RS::ENV_BG_KEEP: {
				keep_color = true;
			} break;
			default: {
			}
		}

		if (draw_sky || draw_sky_fog_only || environment_get_reflection_source(render_data.environment) == RS::ENV_REFLECTION_SOURCE_SKY || environment_get_ambient_source(render_data.environment) == RS::ENV_AMBIENT_SOURCE_SKY) {
			RENDER_TIMESTAMP("Setup Sky");
			Projection projection = render_data.cam_projection;
			if (render_data.reflection_probe.is_valid()) {
				Projection correction;
				correction.set_depth_correction(true, true, false);
				projection = correction * render_data.cam_projection;
			}

			sky_energy_multiplier *= bg_energy_multiplier;

			_setup_sky(&render_data, *render_data.lights, projection, render_data.cam_transform, screen_size);

			if (environment_get_sky(render_data.environment).is_valid()) {
				if (environment_get_reflection_source(render_data.environment) == RS::ENV_REFLECTION_SOURCE_SKY || environment_get_ambient_source(render_data.environment) == RS::ENV_AMBIENT_SOURCE_SKY || (environment_get_reflection_source(render_data.environment) == RS::ENV_REFLECTION_SOURCE_BG && environment_get_background(render_data.environment) == RS::ENV_BG_SKY)) {
					_update_sky_radiance(render_data.environment, projection, render_data.cam_transform, sky_energy_multiplier);
				}
			} else {
				draw_sky = false;
			}
		}
	} else {
		draw_sky = true;
		GLES2::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));

		Projection sky_proj = render_data.cam_projection;
		_setup_sky(&render_data, *render_data.lights, sky_proj, render_data.cam_transform, screen_size);
	}

	_render_shadows(&render_data, screen_size);
	_setup_lights(&render_data, true, render_data.directional_light_count, render_data.omni_light_count, render_data.spot_light_count, render_data.directional_shadow_count);
	_setup_environment(&render_data, false, screen_size, flip_y, clear_color, false);

	_fill_render_list<RENDER_LIST_OPAQUE, PASS_MODE_COLOR>(&render_data);
	if (!bdata.settings_use_batching) {
		// When batching is enabled, the batcher already sorts these
		// naturally.
		render_list[RENDER_LIST_OPAQUE].sort_by_key();
		render_list[RENDER_LIST_ALPHA].sort_by_reverse_depth_and_priority();
	}

	scene_state.reset_gl_state();

	if (rb.is_valid()) {
		GLES2::TextureStorage::get_singleton()->bind_framebuffer(rb->get_render_fbo());
		rb->set_apply_color_adjustments_in_post(apply_color_adjustments_in_post);
		glViewport(0, 0, screen_size.width, screen_size.height);
	}

	scene_state.enable_gl_depth_draw(true);

	if (!keep_color) {
		glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
		// Reverse-Z, far clipping plane is 0.0
		RasterizerGLES2::clear_depth(0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::render_scene: glClear");
	} else {
		RasterizerGLES2::clear_depth(0.0f);
		glClear(GL_DEPTH_BUFFER_BIT);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::render_scene: glClear");
	}

	if (rb.is_valid()) {
		GLES2::TextureStorage::get_singleton()->render_target_disable_clear_request(rb->render_target);
	}

	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(true);

	// Near (1.0) is less than far (0.0)
	glDepthFunc(GL_GEQUAL);

	if (render_data.directional_shadow_count > 0) {
		glActiveTexture(GL_TEXTURE0 + GLES2_CONFIG->max_texture_image_units - 3);
		glBindTexture(GL_TEXTURE_2D, GLES2::LightStorage::get_singleton()->directional_shadow_get_texture());
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::render_scene: glBindTexture directional shadow");
		glActiveTexture(GL_TEXTURE0);
	}

	uint64_t spec_constant_base_flags = 0;

	if (render_data.directional_light_count == 0) {
		spec_constant_base_flags |= SceneShaderGLES2::DISABLE_LIGHT_DIRECTIONAL;
	} else if (render_data.directional_shadow_count > 0) {
		DirectionalShadowData &shadow_data = scene_state.directional_shadows[0];
		if (shadow_data.shadow_split_offsets[0] == shadow_data.shadow_split_offsets[1]) {
			// Orthogonal
		} else {
			spec_constant_base_flags |= SceneShaderGLES2::LIGHT_USE_PSSM2;
		}

		if (scene_state.directional_shadow_quality >= RS::SHADOW_QUALITY_SOFT_HIGH) {
#if defined(TOOLS_ENABLED) || defined(DEBUG_ENABLED)
			WARN_PRINT_ONCE("Shadow filter PCF 13 is not available on the Legacy renderer.");
#endif
		} else if (scene_state.directional_shadow_quality >= RS::SHADOW_QUALITY_SOFT_LOW) {
			spec_constant_base_flags |= SceneShaderGLES2::SHADOW_MODE_PCF_5;
		}
	}

	if (render_data.omni_light_count == 0) {
		spec_constant_base_flags |= SceneShaderGLES2::DISABLE_LIGHT_OMNI;
	}

	if (render_data.spot_light_count == 0) {
		spec_constant_base_flags |= SceneShaderGLES2::DISABLE_LIGHT_SPOT;
	}

	if (
		render_data.environment.is_null() ||
		(render_data.environment.is_valid() && !environment_get_fog_enabled(render_data.environment))
	) {
		spec_constant_base_flags |= SceneShaderGLES2::DISABLE_FOG;
	}

	if (!apply_color_adjustments_in_post) {
		spec_constant_base_flags |= SceneShaderGLES2::APPLY_TONEMAPPING;
	}

	// ==================================
	// Opaque pass
	// ==================================
	RENDER_TIMESTAMP("Render Opaque Pass");
	scene_state.enable_gl_blend(false);
	RenderListParameters render_list_params(render_list[RENDER_LIST_OPAQUE].elements.ptr(), render_list[RENDER_LIST_OPAQUE].elements.size(), reverse_cull, spec_constant_base_flags, use_wireframe);
	_render_list_template<PASS_MODE_COLOR>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_OPAQUE].elements.size());
	_render_directional_light_replay_pass<PASS_MODE_COLOR>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_OPAQUE].elements.size(), false);

	scene_state.enable_gl_depth_draw(false);

	// ==================================
	// Sky background pass
	// ==================================
	if (draw_sky) {
		RENDER_TIMESTAMP("Render Sky");
		scene_state.enable_gl_depth_test(true);
		scene_state.enable_gl_blend(false);
		scene_state.set_gl_cull_mode(RS::CULL_MODE_BACK);
		_draw_sky(render_data.environment, render_data.cam_projection, render_data.cam_transform, sky_energy_multiplier, 1.0, false, flip_y, false);
	}

	// ==================================
	// Transparent pass
	// ==================================
	RENDER_TIMESTAMP("Render 3D Transparent Pass");
	scene_state.enable_gl_blend(true);
	glBlendEquation(GL_FUNC_ADD);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::render_scene: glBlendEquation");

	if (render_data.transparent_bg) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	}
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::render_scene: glBlendFuncSeparate");

	RenderListParameters render_list_params_alpha(render_list[RENDER_LIST_ALPHA].elements.ptr(), render_list[RENDER_LIST_ALPHA].elements.size(), reverse_cull, spec_constant_base_flags, use_wireframe);
	_render_list_template<PASS_MODE_COLOR_TRANSPARENT>(&render_list_params_alpha, &render_data, 0, render_list[RENDER_LIST_ALPHA].elements.size(), true);
	_render_directional_light_replay_pass<PASS_MODE_COLOR_TRANSPARENT>(&render_list_params_alpha, &render_data, 0, render_list[RENDER_LIST_ALPHA].elements.size(), true);

	// Rescue the 3D scene from the internal buffer if it was used.
	_render_post_processing(&render_data);

	// Clean up GL state
	scene_state.reset_gl_state();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (GLES2_CONFIG->support_vao) {
		glBindVertexArray(0);
	}
	GLES2::TextureStorage::get_singleton()->bind_framebuffer_system();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::render_scene: reset_gl_state cleanup");

	// Reset VBO pool cursor and byte offsets at the end of the frame
	// after all passes have completed while retaining cached_slots for frame N+1.
	bdata.current_vbo_index = 0;
	bdata.current_vbo_byte_offset = 0;
	bdata.current_ibo_byte_offset = 0;
	bdata.current_vbo_slot_index = 0;
}

void RasterizerSceneGLES2::_render_post_processing(const RenderDataGLES2 *p_render_data) {
	Ref<RenderSceneBuffersGLES2> rb = p_render_data->render_buffers;
	if (rb.is_null() || rb->internal3d.color == 0) {
		return; // We rendered directly to the RenderTarget; no blit required.
	}

	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();

	// Bind the actual final render target FBO
	GLuint rt_fbo = texture_storage->render_target_get_fbo(rb->render_target);
	texture_storage->bind_framebuffer(rt_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_post_processing: bind_framebuffer");

	// Set up pure blitting state
	glViewport(0, 0, rb->target_size.width, rb->target_size.height);
	scene_state.enable_gl_blend(false);
	scene_state.enable_gl_depth_test(false);
	scene_state.set_gl_cull_mode(RS::CULL_MODE_DISABLED);
	scene_state.enable_gl_scissor_test(false);
	scene_state.enable_gl_depth_draw(false);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_post_processing: glColorMask");

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, rb->internal3d.color);

	if (rb->scaling_3d_mode == RS::VIEWPORT_SCALING_3D_MODE_BILINEAR) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	GLES2::CopyEffects::get_singleton()->copy_to_rect(Rect2(0, 0, 1, 1));

	glBindTexture(GL_TEXTURE_2D, 0);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_post_processing: copy_to_rect");
}

template <RasterizerSceneGLES2::PassMode p_pass_mode>
void RasterizerSceneGLES2::_render_list_template(RenderListParameters *p_params, const RenderDataGLES2 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass, bool p_replay) {
	if (p_from_element >= p_to_element) {
		return;
	}

	int count = p_to_element - p_from_element;
	GeometryInstanceSurface **surfaces = &p_params->elements[p_from_element];

	glFrontFace(p_params->reverse_cull ? GL_CW : GL_CCW);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_list_template: glFrontFace");

	// Save the spec constant, uv_offset, and render_info
	scene_state.current_spec_constants = p_params->spec_constant_base_flags;
	scene_state.current_uv_offset = p_params->uv_offset;
	scene_state.current_render_info = p_render_data->render_info;

	// Record objects and check redraw requirements before batching
	bool should_request_redraw = false;
	for (int i = 0; i < count; i++) {
		if (surfaces[i]->shader && surfaces[i]->shader->uses_time) {
			should_request_redraw = true;
			break;
		}
	}

	if constexpr (p_pass_mode != PASS_MODE_DEPTH) {
		// Don't count elements during depth pre-pass to match the RD renderers.
		// (or additive replays)
		if (p_render_data->render_info && !p_replay) {
			RS::ViewportRenderInfoType info_type;
			if constexpr (p_pass_mode == PASS_MODE_SHADOW) {
				info_type = RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW;
			} else {
				info_type = RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE;
			}
			p_render_data->render_info->info[info_type][RS::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += count;
		}
	}

	// Dispatch batch processor or immediate drawer
	batch_scene_render_items<p_pass_mode>(surfaces, count, p_render_data->cam_transform, p_alpha_pass, p_replay);

	scene_state.current_render_info = nullptr;

	if (should_request_redraw) {
		RenderingServerDefault::redraw_request();
	}

	// Clean-up
	scene_state.reset_gl_vertex_buffer_state();
	glFrontFace(GL_CCW);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_render_list_template: glFrontFace restore");
}

template <RasterizerSceneGLES2::AdditiveLightType p_light_type>
void RasterizerSceneGLES2::_upload_additive_light_uniforms(uint32_t p_light_gl_id, RID p_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
	if constexpr (p_light_type == ADDITIVE_LIGHT_OMNI || p_light_type == ADDITIVE_LIGHT_SPOT) {
		GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
		constexpr int base_idx = (p_light_type == ADDITIVE_LIGHT_OMNI)
				? SceneShaderGLES2::OMNI_LIGHTS_DATA_0_POSITION_INV_RADIUS
				: SceneShaderGLES2::SPOT_LIGHTS_DATA_0_POSITION_INV_RADIUS;
		const LightData &light = (p_light_type == ADDITIVE_LIGHT_OMNI)
				? scene_state.omni_lights[p_light_gl_id]
				: scene_state.spot_lights[p_light_gl_id];

		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 0), Vector4(light.position[0], light.position[1], light.position[2], light.inv_radius), p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 1), Vector4(light.direction[0], light.direction[1], light.direction[2], light.size), p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 2), Vector4(light.color[0], light.color[1], light.color[2], light.attenuation), p_version, p_variant, p_spec_constants);
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::Uniforms(base_idx + 3), Vector4(light.inv_spot_attenuation, light.cos_spot_angle, light.specular_amount, light.shadow_opacity), p_version, p_variant, p_spec_constants);
	}
}

template <RasterizerSceneGLES2::PassMode p_pass_mode>
void RasterizerSceneGLES2::_render_directional_light_replay_pass(RenderListParameters *p_params, const RenderDataGLES2 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass) {
	// Directional Light 0 is computed directly in BASE_PASS.
	// Only replay when there are 2 or more directional lights.
	if (p_from_element >= p_to_element || scene_state.ubo.directional_light_count <= 1) {
		return;
	}

	bool was_blend = false;
	GLES2::SceneShaderData::BlendMode was_blend_mode = GLES2::SceneShaderData::BLEND_MODE_MIX;
	bool was_depth_draw = false;

	scene_state.is_directional_additive_replay = true;
	_batch_util_begin_additive_light_gl_state(was_blend, was_blend_mode, was_depth_draw);

	const uint64_t saved_base_flags = p_params->spec_constant_base_flags;

	for (uint32_t d_idx = 1; d_idx < scene_state.ubo.directional_light_count; d_idx++) {
		scene_state.current_directional_light_index = d_idx;

		// Bump camera_pass_generation so scene binds uploads this directional light's data.
		scene_state.camera_pass_generation++;
		if (unlikely(scene_state.camera_pass_generation == 0)) {
			scene_state.program_camera_state_cache.clear();
			scene_state.camera_pass_generation = 1;
		}

		uint64_t light_spec = saved_base_flags & ~SceneShaderGLES2::LIGHT_USE_PSSM2;
		const DirectionalLightData &dir_light = scene_state.directional_lights[d_idx];
		if (dir_light.shadow_opacity > 0.0f && scene_state.directional_shadows != nullptr) {
			for (uint32_t s = 0; s < MAX_DIRECTIONAL_LIGHTS; s++) {
				if (
					scene_state.directional_shadows[s].direction[0] == dir_light.direction[0] &&
					scene_state.directional_shadows[s].direction[1] == dir_light.direction[1] &&
					scene_state.directional_shadows[s].direction[2] == dir_light.direction[2]
				) {
					if (scene_state.directional_shadows[s].shadow_split_offsets[0] != scene_state.directional_shadows[s].shadow_split_offsets[1]) {
						light_spec |= SceneShaderGLES2::LIGHT_USE_PSSM2;
					}
					break;
				}
			}
		}
		p_params->spec_constant_base_flags = light_spec;

		// Flag as a replay pass (p_replay = true) so the batcher bypasses geometry repacking
		// and reuses the VBOs and batch list established during the base pass.
		_render_list_template<p_pass_mode>(p_params, p_render_data, p_from_element, p_to_element, p_alpha_pass, true);
	}

	p_params->spec_constant_base_flags = saved_base_flags;
	scene_state.current_directional_light_index = 0;
	scene_state.is_directional_additive_replay = false;
	_batch_util_end_additive_light_gl_state(was_blend, was_blend_mode, was_depth_draw);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_directional_light_replay_pass: state restore");
}

template <RasterizerSceneGLES2::AdditiveLightType p_light_type, bool p_is_batch>
void RasterizerSceneGLES2::_execute_additive_light_draw(uint32_t p_light_gl_id, GLES2::SceneMaterialData *p_material, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, const Transform3D &p_world_xform, RS::PrimitiveType p_primitive, int p_instances, const MultiMeshInstanceData *p_mm, const Transform3D *p_owner_transform, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, RS::InstanceType p_base_type, const BatchBoundGeometryStateGLES2 *p_bound_state) {
	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	GLES2::SceneShaderData *shader = p_material->shader_data;

	_upload_additive_light_uniforms<p_light_type>(p_light_gl_id, shader->version, p_variant, p_spec_constants);

	if constexpr (p_is_batch) {
		if (p_bound_state && p_bound_state->valid) {
			_batch_draw_bound_buffers(p_primitive, 0, 0, *p_bound_state);
		} else {
			const uint32_t batch_flags = _render_item_state.curr_batch ? _render_item_state.curr_batch->flags : 0U;
			const bool has_vertex_colors = _batch_util_has_vertex_colors(bdata.fvf, batch_flags);
			_batch_render_generic(p_primitive, 0, 0, has_vertex_colors);
		}
	} else {
		const bool is_multimesh = (p_base_type == RS::INSTANCE_MULTIMESH);
		const bool is_particles = (p_base_type == RS::INSTANCE_PARTICLES);
		Color inst_color(1.0f, 1.0f, 1.0f, 1.0f);

		for (int inst = 0; inst < p_instances; inst++) {
			Transform3D xform;
			if (is_multimesh && p_mm && p_mm->data) {
				_gl_batch_decode_multimesh_instance(p_mm->data + (inst * p_mm->stride), p_mm->format, p_mm->uses_colors, p_mm->color_offset, xform, inst_color);
			}

			const Transform3D write_xform = (*p_owner_transform) * xform;
			material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WORLD_TRANSFORM, write_xform, shader->version, p_variant, p_spec_constants);

			if (is_multimesh || is_particles) {
				glVertexAttrib4f(15, inst_color.r, inst_color.g, inst_color.b, inst_color.a);
			}

			if (p_use_index_buffer) {
				glDrawElements(p_primitive_gl, p_drawn_count, p_index_type, 0);
			} else {
				glDrawArrays(p_primitive_gl, 0, p_drawn_count);
			}
			_record_draw_call_info(scene_state.current_render_info);
		}
	}

	if constexpr (p_light_type == ADDITIVE_LIGHT_DIRECTIONAL) {
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_execute_additive_light_draw: DIRECTIONAL");
	} else if constexpr (p_light_type == ADDITIVE_LIGHT_OMNI) {
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_execute_additive_light_draw: OMNI");
	} else if constexpr (p_light_type == ADDITIVE_LIGHT_SPOT) {
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::RasterizerSceneGLES2::_execute_additive_light_draw: SPOT");
	}
}

template <RasterizerSceneGLES2::AdditiveLightType p_light_type, bool p_is_batch>
void RasterizerSceneGLES2::_render_additive_light_type_pass(const LocalVector<uint32_t> &p_unique_lights, GLES2::SceneMaterialData *p_material, SceneShaderGLES2::ShaderVariant p_additive_variant, uint64_t p_additive_base_spec, const Transform3D &p_world_xform, RS::PrimitiveType p_primitive, int p_instances, const MultiMeshInstanceData *p_mm, const Transform3D *p_owner_transform, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, RS::InstanceType p_base_type, const BatchBoundGeometryStateGLES2 *p_bound_state) {
	const uint32_t light_count = p_unique_lights.size();
	if (light_count == 0) {
		return;
	}

	GLES2::MaterialStorage *material_storage = GLES2::MaterialStorage::get_singleton();
	GLES2::SceneShaderData *shader = p_material->shader_data;
	constexpr uint64_t light_spec_flag = (p_light_type == ADDITIVE_LIGHT_OMNI) ? SceneShaderGLES2::ADDITIVE_OMNI : SceneShaderGLES2::ADDITIVE_SPOT;
	const uint64_t pass_spec = p_additive_base_spec | SceneShaderGLES2::USE_ADDITIVE_LIGHTING | light_spec_flag;

	if (!material_storage->shaders.scene_shader.version_bind_shader(shader->version, p_additive_variant, pass_spec)) {
		return;
	}

	p_material->bind_uniforms();
	_bind_scene_camera_uniforms(shader->version, p_additive_variant, pass_spec);
	if constexpr (p_is_batch) {
		material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES2::WORLD_TRANSFORM, p_world_xform, shader->version, p_additive_variant, pass_spec);
		if (_render_item_state.curr_batch && _batch_util_promote_to_instancing(bdata.fvf, _render_item_state.curr_batch->flags)) {
			_batch_upload_multimesh_palette_3d(*_render_item_state.curr_batch, shader->version, p_additive_variant, pass_spec);
		}
	}

	for (uint32_t i = 0; i < light_count; i++) {
		_execute_additive_light_draw<p_light_type, p_is_batch>(p_unique_lights[i], p_material, p_additive_variant, pass_spec, p_world_xform, p_primitive, p_instances, p_mm, p_owner_transform, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type, p_base_type, p_bound_state);
	}
}

template <bool p_is_batch>
void RasterizerSceneGLES2::_render_additive_light_passes(const GeometryInstanceSurface *p_surf, GLES2::SceneMaterialData *p_material, uint64_t p_spec_constants, bool p_instancing, const Transform3D &p_world_xform, RS::PrimitiveType p_primitive, int p_instances, const MultiMeshInstanceData *p_mm, const Transform3D *p_owner_transform, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, const BatchBoundGeometryStateGLES2 *p_bound_state) {
	if (
		!p_surf ||
		!p_surf->owner ||
		!p_material ||
		!p_material->shader_data ||
		p_material->shader_data->unshaded
	) {
		return;
	}

	LocalVector<uint32_t> &unique_omni = scene_state.group_omni_lights_scratch;
	LocalVector<uint32_t> &unique_spot = scene_state.group_spot_lights_scratch;
	_batch_util_get_unique_light_vector<ADDITIVE_LIGHT_OMNI, p_is_batch>(unique_omni, p_surf);
	_batch_util_get_unique_light_vector<ADDITIVE_LIGHT_SPOT, p_is_batch>(unique_spot, p_surf);

	if (unique_omni.is_empty() && unique_spot.is_empty()) {
		return;
	}

	scene_state.additive_pass_mutated_state = true;
	bool was_blend = false;
	GLES2::SceneShaderData::BlendMode was_blend_mode = GLES2::SceneShaderData::BLEND_MODE_MIX;
	bool was_depth_draw = false;
	_batch_util_begin_additive_light_gl_state(was_blend, was_blend_mode, was_depth_draw);

	const SceneShaderGLES2::ShaderVariant additive_variant = p_instancing ? SceneShaderGLES2::MODE_ADDITIVE_INSTANCING : SceneShaderGLES2::MODE_ADDITIVE;
	const RS::InstanceType base_type = p_surf->owner->data->base_type;
	const uint64_t additive_base_spec = p_spec_constants & ~SceneShaderGLES2::BASE_PASS;

	_render_additive_light_type_pass<ADDITIVE_LIGHT_OMNI, p_is_batch>(unique_omni, p_material, additive_variant, additive_base_spec, p_world_xform, p_primitive, p_instances, p_mm, p_owner_transform, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type, base_type, p_bound_state);
	_render_additive_light_type_pass<ADDITIVE_LIGHT_SPOT, p_is_batch>(unique_spot, p_material, additive_variant, additive_base_spec, p_world_xform, p_primitive, p_instances, p_mm, p_owner_transform, p_use_index_buffer, p_primitive_gl, p_drawn_count, p_index_type, base_type, p_bound_state);

	_batch_util_end_additive_light_gl_state(was_blend, was_blend_mode, was_depth_draw);
}

void RasterizerSceneGLES2::render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) {
}

void RasterizerSceneGLES2::render_particle_collider_heightfield(RID p_collider, const Transform3D &p_transform, const PagedArray<RenderGeometryInstance *> &p_instances) {

}

void RasterizerSceneGLES2::_render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, GLuint p_framebuffer, const Rect2i &p_region, const GLuint p_color_textures[4]) {
	RENDER_TIMESTAMP("Setup Rendering UV2");

	RenderDataGLES2 render_data;
	render_data.instances = &p_instances;

	scene_state.ubo.emissive_exposure_normalization = -1.0;

	_setup_environment(&render_data, true, Size2i(1, 1), true, Color(), false);

	_fill_render_list<RENDER_LIST_SECONDARY, PASS_MODE_MATERIAL>(&render_data);
	if (!bdata.settings_use_batching) {
		// When batching is enabled, the batcher already sorts these
		// naturally.
		render_list[RENDER_LIST_SECONDARY].sort_by_key();
	}

	RENDER_TIMESTAMP("Render 3D Material");

	// Save state
	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glGetIntegerv GL_FRAMEBUFFER_BINDING");

	GLint prev_viewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, prev_viewport);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glGetIntegerv GL_VIEWPORT");

	GLboolean prev_color_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
	glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glGetBooleanv GL_COLOR_WRITEMASK");

	GLint prev_depth_func = GL_LESS;
	glGetIntegerv(GL_DEPTH_FUNC, &prev_depth_func);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glGetIntegerv GL_DEPTH_FUNC");

	GLES2::TextureStorage::get_singleton()->bind_framebuffer(p_framebuffer);
	glViewport(p_region.position.x, p_region.position.y, p_region.size.x, p_region.size.y);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glViewport");

	const uint64_t base_spec_constant = (
		SceneShaderGLES2::RENDER_MATERIAL |
		SceneShaderGLES2::DISABLE_FOG |
		SceneShaderGLES2::DISABLE_LIGHT_DIRECTIONAL |
		SceneShaderGLES2::DISABLE_LIGHT_OMNI |
		SceneShaderGLES2::DISABLE_LIGHT_SPOT |
		SceneShaderGLES2::DISABLE_LIGHTMAP
	);

	constexpr int UV_OFFSET_COUNT = 9;
	const Vector2 uv_offsets[UV_OFFSET_COUNT] = {
		Vector2(-1, 1),
		Vector2(1, 1),
		Vector2(1, -1),
		Vector2(-1, -1),
		Vector2(-1, 0),
		Vector2(1, 0),
		Vector2(0, -1),
		Vector2(0, 1),
		Vector2(0, 0),
	};

	// Run the single-attachment pass since we
	// cannot "just" use GL_COLOR_ATTACHMENT1..3
	for (int pass = 0; pass < MAXIMUM_COLOR_ATTACHMENT; pass++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p_color_textures[pass], 0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glFramebufferTexture2D");

		scene_state.reset_gl_state();
		scene_state.enable_gl_depth_test(true);
		scene_state.enable_gl_depth_draw(true);
		glDepthFunc(GL_GREATER);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glDepthFunc");

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		RasterizerGLES2::clear_depth(0.0f);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: glClear");

		const uint64_t spec_constant = base_spec_constant | PASS_SPEC_FLAGS[pass];
		RenderListParameters render_list_params(render_list[RENDER_LIST_SECONDARY].elements.ptr(), render_list[RENDER_LIST_SECONDARY].elements.size(), false, spec_constant, true, Vector2(0, 0));

		for (int i = 0; i < UV_OFFSET_COUNT; i++) {
			Vector2 ofs = uv_offsets[i];
			ofs.x /= p_region.size.width;
			ofs.y /= p_region.size.height;
			render_list_params.uv_offset = ofs;
			_render_list_template<PASS_MODE_MATERIAL>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_SECONDARY].elements.size(), false, i > 0);
		}

		render_list_params.uv_offset = Vector2(0, 0);
		render_list_params.force_wireframe = false;
		_render_list_template<PASS_MODE_MATERIAL>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_SECONDARY].elements.size(), false, true);
	}

	// Restore state
	scene_state.reset_gl_state();
	glDepthFunc(prev_depth_func);
	glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
	glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
	GLES2::TextureStorage::get_singleton()->bind_framebuffer(prev_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_render_uv2: restore GL state");
	bdata.invalidate_vbo_cache();
}

void RasterizerSceneGLES2::set_time(double p_time, double p_step) {
	time = p_time;
	time_step = p_step;
}

void RasterizerSceneGLES2::set_debug_draw_mode(RS::ViewportDebugDraw p_debug_draw) {
	debug_draw = p_debug_draw;
}

Ref<RenderSceneBuffers> RasterizerSceneGLES2::render_buffers_create() {
	Ref<RenderSceneBuffersGLES2> rb;
	rb.instantiate();
	ERR_FAIL_COND_V(rb.is_null(), Ref<RenderSceneBuffers>());
	return rb;
}

void RasterizerSceneGLES2::_render_buffers_debug_draw(Ref<RenderSceneBuffersGLES2> p_render_buffers, RID p_shadow_atlas, GLuint p_fbo) {
}

void RasterizerSceneGLES2::gi_set_use_half_resolution(bool p_enable) {
}

void RasterizerSceneGLES2::screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_curve) {
}

bool RasterizerSceneGLES2::screen_space_roughness_limiter_is_active() const {
	return false;
}

void RasterizerSceneGLES2::sub_surface_scattering_set_quality(RS::SubSurfaceScatteringQuality p_quality) {
}

void RasterizerSceneGLES2::sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) {
}

TypedArray<Image> RasterizerSceneGLES2::bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) {
	ERR_FAIL_COND_V_MSG(p_image_size.width <= 0, TypedArray<Image>(), "Image width must be greater than 0.");
	ERR_FAIL_COND_V_MSG(p_image_size.height <= 0, TypedArray<Image>(), "Image height must be greater than 0.");

	GLES2::Config *config = GLES2::Config::get_singleton();
	GLES2::TextureStorage *texture_storage = GLES2::TextureStorage::get_singleton();
	GLES2::Utilities *utilities = GLES2::Utilities::get_singleton();

	// Safe states
	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glGetIntegerv GL_FRAMEBUFFER_BINDING");

	GLint prev_active_tex = GL_TEXTURE0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glGetIntegerv GL_ACTIVE_TEXTURE");

	glActiveTexture(GL_TEXTURE0);
	GLint prev_tex_2d = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex_2d);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glGetIntegerv GL_TEXTURE_BINDING_2D");

	GLint prev_rbo = 0;
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glGetIntegerv GL_RENDERBUFFER_BINDING");

	GLuint albedo_alpha_tex = 0;
	GLuint normal_tex = 0;
	GLuint orm_tex = 0;
	GLuint emission_tex = 0;
	GLuint depth_tex = 0;
	GLuint depth_rbo = 0;

	// Generate textures
	glGenTextures(1, &albedo_alpha_tex);
	glGenTextures(1, &normal_tex);
	glGenTextures(1, &orm_tex);
	glGenTextures(1, &emission_tex);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glGenTextures");

	// Bind and setup textures
	glBindTexture(GL_TEXTURE_2D, albedo_alpha_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_image_size.width, p_image_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	_gl_setup_bake_uv2_tex_params();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D albedo_alpha_tex");
	utilities->texture_allocated_data(albedo_alpha_tex, p_image_size.width * p_image_size.height * 4, "Lightmap albedo texture");

	glBindTexture(GL_TEXTURE_2D, normal_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_image_size.width, p_image_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	_gl_setup_bake_uv2_tex_params();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D normal_tex");
	utilities->texture_allocated_data(normal_tex, p_image_size.width * p_image_size.height * 4, "Lightmap normal texture");

	glBindTexture(GL_TEXTURE_2D, orm_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_image_size.width, p_image_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	_gl_setup_bake_uv2_tex_params();
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D orm_tex");
	utilities->texture_allocated_data(orm_tex, p_image_size.width * p_image_size.height * 4, "Lightmap ORM texture");

	glBindTexture(GL_TEXTURE_2D, emission_tex);
	if (config->float_texture_supported) {
#ifdef GL_API_ENABLED
		GLenum internal_format = RasterizerGLES2::is_gles_over_gl() ? GL_RGBA32F : GL_RGBA;
#else
		GLenum internal_format = GL_RGBA;
#endif
		glTexImage2D(GL_TEXTURE_2D, 0, internal_format, p_image_size.width, p_image_size.height, 0, GL_RGBA, GL_FLOAT, nullptr);
		_gl_setup_bake_uv2_tex_params();
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D emission_tex float");
		utilities->texture_allocated_data(emission_tex, p_image_size.width * p_image_size.height * 16, "Lightmap emission texture");
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_image_size.width, p_image_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		_gl_setup_bake_uv2_tex_params();
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D emission_tex rgba8");
		utilities->texture_allocated_data(emission_tex, p_image_size.width * p_image_size.height * 4, "Lightmap emission texture");
	}

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glGenFramebuffers");
	texture_storage->bind_framebuffer(fbo);

	if (config->support_depth_texture) {
		glGenTextures(1, &depth_tex);
		glBindTexture(GL_TEXTURE_2D, depth_tex);
#ifdef GL_API_ENABLED
		GLenum depth_internal = RasterizerGLES2::is_gles_over_gl() ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT;
#else
		GLenum depth_internal = GL_DEPTH_COMPONENT;
#endif
		glTexImage2D(GL_TEXTURE_2D, 0, depth_internal, p_image_size.width, p_image_size.height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		_gl_setup_bake_uv2_tex_params();
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D depth_tex");
		utilities->texture_allocated_data(depth_tex, p_image_size.width * p_image_size.height * 4, "Lightmap depth texture");

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex, 0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glFramebufferTexture2D depth_tex");
	} else {
		glGenRenderbuffers(1, &depth_rbo);
		glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, p_image_size.width, p_image_size.height);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glRenderbufferStorage depth_rbo");
		utilities->render_buffer_allocated_data(depth_rbo, p_image_size.width * p_image_size.height * 2, "Lightmap depth renderbuffer");

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glFramebufferRenderbuffer depth_rbo");
	}

	const GLuint color_texs[4] = { albedo_alpha_tex, normal_tex, orm_tex, emission_tex };

#define CLEANUP_RESOURCES()                             \
	texture_storage->bind_framebuffer(prev_fbo);        \
	glBindRenderbuffer(GL_RENDERBUFFER, prev_rbo);      \
	glActiveTexture(GL_TEXTURE0);                       \
	glBindTexture(GL_TEXTURE_2D, prev_tex_2d);          \
	glActiveTexture(prev_active_tex);                   \
	if (fbo != 0) {                                     \
		glDeleteFramebuffers(1, &fbo);                  \
	}                                                   \
	if (albedo_alpha_tex != 0) {                        \
		utilities->texture_free_data(albedo_alpha_tex); \
	}                                                   \
	if (normal_tex != 0) {                              \
		utilities->texture_free_data(normal_tex);       \
	}                                                   \
	if (orm_tex != 0) {                                 \
		utilities->texture_free_data(orm_tex);          \
	}                                                   \
	if (emission_tex != 0) {                            \
		utilities->texture_free_data(emission_tex);     \
	}                                                   \
	if (depth_tex != 0) {                               \
		utilities->texture_free_data(depth_tex);        \
	}                                                   \
	if (depth_rbo != 0) {                               \
		utilities->render_buffer_free_data(depth_rbo);  \
	}                                                   \
	GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: cleanup_resources");

	// Validate completeness across all color attachments (including float emission_tex)
	for (int i = 0; i < MAXIMUM_COLOR_ATTACHMENT; i++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texs[i], 0);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glFramebufferTexture2D completeness check");
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			if (i == 3 && config->float_texture_supported) {
				// Hardware supports sampling float textures but not rendering to them.
				// Fallback to RGBA8
				glBindTexture(GL_TEXTURE_2D, emission_tex);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_image_size.width, p_image_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
				GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glTexImage2D emission_tex fallback");
				utilities->texture_resize_data(emission_tex, p_image_size.width * p_image_size.height * 4);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, emission_tex, 0);
				status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			}
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				CLEANUP_RESOURCES();
				WARN_PRINT("Could not create render target, status: " + texture_storage->get_framebuffer_error(status));
				return TypedArray<Image>();
			}
		}
	}

	RenderGeometryInstance *gi_inst = geometry_instance_create(p_base);
	if (unlikely(!gi_inst)) {
		CLEANUP_RESOURCES();
		ERR_FAIL_V(TypedArray<Image>());
	}

	uint32_t sc = RSG::mesh_storage->mesh_get_surface_count(p_base);
	Vector<RID> materials;
	materials.resize(sc);

	for (uint32_t i = 0; i < sc; i++) {
		if (i < (uint32_t)p_material_overrides.size()) {
			materials.write[i] = p_material_overrides[i];
		}
	}

	gi_inst->set_surface_materials(materials);

	if (cull_argument.size() == 0) {
		cull_argument.push_back(nullptr);
	}
	cull_argument[0] = gi_inst;
	_render_uv2(cull_argument, fbo, Rect2i(0, 0, p_image_size.width, p_image_size.height), color_texs);

	geometry_instance_free(gi_inst);

	TypedArray<Image> ret;

	RID tex_rid = texture_storage->texture_allocate();
	GLES2::Texture *existing_tex = texture_storage->get_texture(tex_rid);
	if (existing_tex && existing_tex->tex_id != 0) {
		glDeleteTextures(1, &existing_tex->tex_id);
		existing_tex->tex_id = 0;
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::bake_render_uv2: glDeleteTextures dummy shell");
	}

	GLES2::Texture texture;
	texture.width = p_image_size.width;
	texture.height = p_image_size.height;
	texture.alloc_width = p_image_size.width;
	texture.alloc_height = p_image_size.height;
	texture.format = Image::FORMAT_RGBA8;
	texture.real_format = Image::FORMAT_RGBA8;
	texture.gl_format_cache = GL_RGBA;
	texture.gl_type_cache = GL_FLOAT;
	texture.type = GLES2::Texture::TYPE_2D;
	texture.target = GL_TEXTURE_2D;
	texture.active = true;
	texture.is_render_target = true;

	texture_storage->texture_2d_initialize_from_texture(tex_rid, texture);
	GLES2::Texture *tex = texture_storage->get_texture(tex_rid);

	{
		tex->tex_id = albedo_alpha_tex;
		Ref<Image> img = texture_storage->texture_2d_get(tex_rid);
		utilities->texture_free_data(albedo_alpha_tex);
		albedo_alpha_tex = 0;
		ret.push_back(img);
	}

	{
		tex->tex_id = normal_tex;
		Ref<Image> img = texture_storage->texture_2d_get(tex_rid);
		utilities->texture_free_data(normal_tex);
		normal_tex = 0;
		ret.push_back(img);
	}

	{
		tex->tex_id = orm_tex;
		Ref<Image> img = texture_storage->texture_2d_get(tex_rid);
		utilities->texture_free_data(orm_tex);
		orm_tex = 0;
		ret.push_back(img);
	}

	{
		tex->tex_id = emission_tex;
		if (config->float_texture_supported) {
			tex->format = Image::FORMAT_RGBAH;
			tex->real_format = Image::FORMAT_RGBAH;
			tex->gl_type_cache = GL_FLOAT;
		}
		Ref<Image> img = texture_storage->texture_2d_get(tex_rid);
		utilities->texture_free_data(emission_tex);
		emission_tex = 0;
		ret.push_back(img);
	}

	tex->is_render_target = false;
	tex->tex_id = 0;
	texture_storage->texture_free(tex_rid);

	CLEANUP_RESOURCES();

#undef CLEANUP_RESOURCES

	return ret;
}

bool RasterizerSceneGLES2::free(RID p_rid) {
	if (is_environment(p_rid)) {
		environment_free(p_rid);
	} else if (sky_owner.owns(p_rid)) {
		Sky *sky = sky_owner.get_or_null(p_rid);
		ERR_FAIL_NULL_V(sky, false);
		_free_sky_data(sky);
		sky_owner.free(p_rid);
	} else if (GLES2::LightStorage::get_singleton()->owns_light_instance(p_rid)) {
		GLES2::LightStorage::get_singleton()->light_instance_free(p_rid);
	} else if (RSG::camera_attributes->owns_camera_attributes(p_rid)) {
		//not much to delete, just free it
		RSG::camera_attributes->camera_attributes_free(p_rid);
	} else if (is_compositor(p_rid)) {
		compositor_free(p_rid);
	} else if (is_compositor_effect(p_rid)) {
		compositor_effect_free(p_rid);
	} else {
		return false;
	}
	return true;
}

void RasterizerSceneGLES2::update() {
	_update_dirty_skys();
}

void RasterizerSceneGLES2::sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) {
}

void RasterizerSceneGLES2::decals_set_filter(RS::DecalFilter p_filter) {
}

void RasterizerSceneGLES2::light_projectors_set_filter(RS::LightProjectorFilter p_filter) {
}

void RasterizerSceneGLES2::lightmaps_set_bicubic_filter(bool p_enable) {
	lightmap_bicubic_upscale = p_enable;
}

RasterizerSceneGLES2::RasterizerSceneGLES2() {
	singleton = this;

	initialize();

	batch_constructor();
}

RasterizerSceneGLES2::~RasterizerSceneGLES2() {
	singleton = nullptr;

	// 3D batcher VBO pools
	for (uint32_t i = 0; i < bdata.vbo_pool.size(); i++) {
		if (bdata.vbo_pool[i].gl_vertex_buffer != 0) {
			glDeleteBuffers(1, &bdata.vbo_pool[i].gl_vertex_buffer);
			glDeleteBuffers(1, &bdata.vbo_pool[i].gl_index_buffer);
		}
	}

	// Scene Shader
	GLES2::MaterialStorage::get_singleton()->shaders.scene_shader.version_free(scene_globals.shader_default_version);
	RSG::material_storage->material_free(scene_globals.default_material);
	RSG::material_storage->shader_free(scene_globals.default_shader);

	// Sky Shader
	memdelete_arr(scene_state.directional_lights);
	memdelete_arr(scene_state.omni_lights);
	memdelete_arr(scene_state.spot_lights);
	memdelete_arr(scene_state.omni_light_sort);
	memdelete_arr(scene_state.spot_light_sort);
	memdelete_arr(scene_state.positional_shadows);
	memdelete_arr(scene_state.directional_shadows);

	// Overdraw Shader
	RSG::material_storage->material_free(scene_globals.overdraw_material);
	RSG::material_storage->shader_free(scene_globals.overdraw_shader);

	// Sky Shader
	GLES2::MaterialStorage::get_singleton()->shaders.sky_shader.version_free(sky_globals.shader_default_version);
	RSG::material_storage->material_free(sky_globals.default_material);
	RSG::material_storage->shader_free(sky_globals.default_shader);
	RSG::material_storage->material_free(sky_globals.fog_material);
	RSG::material_storage->shader_free(sky_globals.fog_shader);
	memdelete_arr(sky_globals.directional_lights);
	memdelete_arr(sky_globals.last_frame_directional_lights);

	// Buffers
	if (scene_state.ubo_buffer != 0) {
		GLES2::Utilities::get_singleton()->buffer_free_data(scene_state.ubo_buffer);
	}

	if (sky_globals.screen_triangle != 0) {
		glDeleteBuffers(1, &sky_globals.screen_triangle);
		glDeleteVertexArrays(1, &sky_globals.screen_triangle_array);
	}
}

#endif // GLES2_ENABLED
