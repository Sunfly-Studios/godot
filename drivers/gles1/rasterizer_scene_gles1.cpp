/**************************************************************************/
/*  rasterizer_scene_gles1.cpp                                            */
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

#include "rasterizer_scene_gles1.h"

#include "drivers/gles1/effects/copy_effects.h"
#include "drivers/gles1/effects/feed_effects.h"
#include "rasterizer_gles1.h"
#include "storage/config.h"
#include "storage/mesh_storage.h"
#include "storage/particles_storage.h"
#include "storage/texture_storage.h"

#include "core/config/project_settings.h"
#include "core/templates/sort_array.h"
#include "servers/camera/camera_feed.h"
#include "servers/camera_server.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"

#ifdef GLES1_ENABLED

RasterizerSceneGLES1 *RasterizerSceneGLES1::singleton = nullptr;

/* STATIC */

static GLuint _init_radiance_texture_gles1(int p_size, int p_mipmaps, String p_name) {
	GLuint tex;
	glGenTextures(1, &tex);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_init_radiance_texture_gles1: glGenTextures");

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
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_init_radiance_texture_gles1: glTexImage2D");

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, p_mipmaps > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	GLES1::Utilities::get_singleton()->texture_allocated_data(tex, total_size, p_name);

	return tex;
}

// Shared constants across the driver
static constexpr float qv_fallback[12] = {
	-1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	 1.0f,  1.0f, -1.0f
};

static const Vector3 view_normals[6] = {
	Vector3(+1, 0, 0), Vector3(-1, 0, 0), Vector3(0, +1, 0),
	Vector3(0, -1, 0), Vector3(0, 0, +1), Vector3(0, 0, -1)
};
static const Vector3 view_up[6] = {
	Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, +1),
	Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0)
};

constexpr int GRID_SIZE = 16;
constexpr int NUM_VERTICES = GRID_SIZE * GRID_SIZE * 6;

// The 5 GLES1 primitives
static constexpr GLenum prim[5] = { GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP };		

#ifdef TOOLS_ENABLED
// We manually draw the grid X/Y/Z editor lines regardless
// of the world's opinion.
// This is because the shader in Node3DEditorPlugin is a ghost
// shader that exists but cannot be retrieved in anyway.
// And because I don't want the user to just have a
// grid + sky and that's it, we do it ourselves then.
// 
// We don't count the draw calls made by these when
// reporting rendering data.
static constexpr float line_verts[] = {
	0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,   // +X Axis
	0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // -X Axis
	0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,   // +Y Axis
	0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f,  // -Y Axis
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,   // +Z Axis
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f   // -Z Axis
};
static constexpr uint8_t line_colors[] = {
	246, 83, 101, 255, 246, 83, 101, 255, // +X Axis
	246, 83, 101, 255, 246, 83, 101, 255, // -X Axis
	139, 216, 67, 255, 139, 216, 67, 255, // +Y Axis
	139, 216, 67, 255, 139, 216, 67, 255, // -Y Axis
	57, 156, 237, 255, 57, 156, 237, 255, // +Z Axis
	57, 156, 237, 255, 57, 156, 237, 255  // -Z Axis
};

// 3D Gizmos configuration
static constexpr int BORDER_SEGMENTS = 64;
static constexpr float B_WIDTH = 0.028f; // Border width
static constexpr float T_WIDTH = 0.02f;
#endif

void RasterizerSceneGLES1::initialize() {
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();
	GLES1::Config *config = GLES1::Config::get_singleton();

	batch_initialize();

	cull_argument.set_page_pool(&cull_argument_pool);

	{
		String global_defines;
		global_defines += "#define MAX_GLOBAL_SHADER_UNIFORMS 256\n"; // TODO: this is arbitrary for now
		global_defines += "\n#define MAX_LIGHT_DATA_STRUCTS " + itos(config->max_renderable_lights) + "\n";
		global_defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(MAX_DIRECTIONAL_LIGHTS) + "\n";
		global_defines += "\n#define MAX_FORWARD_LIGHTS " + itos(config->max_lights_per_object) + "\n";
		global_defines += "\n#define MAX_ROUGHNESS_LOD " + itos(sky_globals.roughness_layers - 1) + ".0\n";
		global_defines += "\n#define DISABLE_LIGHT_DIRECTIONAL\n";
		global_defines += "\n#define DISABLE_LIGHT_OMNI\n";
		global_defines += "\n#define DISABLE_LIGHT_SPOT\n";
		if (config->force_vertex_shading) {
			global_defines += "\n#define USE_VERTEX_LIGHTING\n";
		}
		if (!config->multi_bounce_occlusion) {
			global_defines += "\n#define MULTI_BOUNCE_OCCLUSION_DISABLED\n";
		}
		material_storage->shaders.scene_shader.initialize(global_defines);
		scene_globals.shader_default_version = material_storage->shaders.scene_shader.version_create();
		material_storage->shaders.scene_shader.version_bind_shader(scene_globals.shader_default_version, SceneShaderGLES1::MODE_COLOR);
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
		sky_globals.max_directional_lights = 4;
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
		sky_globals.screen_triangle = 0;

		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &sky_globals.screen_triangle);
		}

		if (sky_globals.screen_triangle != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, sky_globals.screen_triangle);
			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, qv_fallback, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::initialize: sky screen quad setup");
	}

	{
		// Pre-compute 16x16 geometry grid for Sky Radiance baking
		sky_globals.radiance_verts = memnew_arr(float, 6 * NUM_VERTICES * 2);
		sky_globals.radiance_uvw = memnew_arr(float, 6 * NUM_VERTICES * 3);
		sky_globals.radiance_colors = memnew_arr(uint8_t, 6 * NUM_VERTICES * 4);

		Projection cm;
		cm.set_perspective(90, 1, 0.01, 10.0);
		Projection correction;
		correction.set_depth_correction(true, true, false);
		cm = correction * cm;

		int v_idx = 0;
		for (int i = 0; i < 6; i++) {
			Basis local_view = Basis::looking_at(view_normals[i], view_up[i]);
			for (int y = 0; y < GRID_SIZE; y++) {
				for (int x = 0; x < GRID_SIZE; x++) {
					float px0 = -1.0f + 2.0f * (x / static_cast<float>(GRID_SIZE));
					float py0 = -1.0f + 2.0f * (y / static_cast<float>(GRID_SIZE));
					float px1 = -1.0f + 2.0f * ((x + 1) / static_cast<float>(GRID_SIZE));
					float py1 = -1.0f + 2.0f * ((y + 1) / static_cast<float>(GRID_SIZE));

					Vector2 quad[6] = {
						Vector2(px0, py0), Vector2(px1, py0), Vector2(px0, py1),
						Vector2(px1, py0), Vector2(px1, py1), Vector2(px0, py1)
					};

					for (int v = 0; v < 6; v++) {
						sky_globals.radiance_verts[v_idx * 2 + 0] = quad[v].x;
						sky_globals.radiance_verts[v_idx * 2 + 1] = quad[v].y;

						Vector2 uv_interp = quad[v];
						uv_interp.y *= -1.0f; // sky_spec is 0, USE_INVERTED_Y is false in shader mapping

						Vector3 cube_normal;
						cube_normal.z = -1.0f;
						cube_normal.x = (uv_interp.x - cm.columns[2][0]) / cm.columns[0][0];
						cube_normal.y = (uv_interp.y - cm.columns[2][1]) / cm.columns[1][1];

						cube_normal = local_view.xform(cube_normal).normalized();

						sky_globals.radiance_uvw[v_idx * 3 + 0] = cube_normal.x;
						sky_globals.radiance_uvw[v_idx * 3 + 1] = cube_normal.y;
						sky_globals.radiance_uvw[v_idx * 3 + 2] = cube_normal.z;
						v_idx++;
					}
				}
			}
		}

		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &sky_globals.radiance_verts_vbo);
			glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_verts_vbo);
			glBufferData(GL_ARRAY_BUFFER, 6 * NUM_VERTICES * 2 * sizeof(float), sky_globals.radiance_verts, GL_STATIC_DRAW);

			glGenBuffers(1, &sky_globals.radiance_uvw_vbo);
			glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_uvw_vbo);
			glBufferData(GL_ARRAY_BUFFER, 6 * NUM_VERTICES * 3 * sizeof(float), sky_globals.radiance_uvw, GL_STATIC_DRAW);

			glGenBuffers(1, &sky_globals.radiance_colors_vbo);
			glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_colors_vbo);
			glBufferData(GL_ARRAY_BUFFER, 6 * NUM_VERTICES * 4 * sizeof(uint8_t), nullptr, GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::initialize: radiance VBO cache setup");
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

#ifdef TOOLS_ENABLED
	// Here we prekabe and peform the math required for some
	// of the editor's visuals once and upload to VRAM (when possible)
	// for performance.

	// Precalcuate the trigonometric overhead of the 3D gizmos

	rotate_gizmo_border_verts = memnew_arr(float, (BORDER_SEGMENTS + 1) * 3);
	for (int k = 0; k <= BORDER_SEGMENTS; k++) {
		float angle = (Math_TAU) * (k / static_cast<float>(BORDER_SEGMENTS));
		rotate_gizmo_border_verts[k * 3 + 0] = Math::cos(angle);
		rotate_gizmo_border_verts[k * 3 + 1] = Math::sin(angle);
		rotate_gizmo_border_verts[k * 3 + 2] = 0.0f;
	}

	// For the coloured rings, we only cache P0 and T0.
	rotate_gizmo_ring_verts = memnew_arr(float, (BORDER_SEGMENTS + 1) * 6);
	for (int k = 0; k <= BORDER_SEGMENTS; k++) {
		float angle = -(Math_PI * 0.55f) + (Math_PI * 1.1f) * (k / static_cast<float>(BORDER_SEGMENTS));
		rotate_gizmo_ring_verts[k * 6 + 0] = Math::cos(angle);
		rotate_gizmo_ring_verts[k * 6 + 1] = Math::sin(angle);
		rotate_gizmo_ring_verts[k * 6 + 2] = 0.0f;
		rotate_gizmo_ring_verts[k * 6 + 3] = -Math::sin(angle);
		rotate_gizmo_ring_verts[k * 6 + 4] = Math::cos(angle);
		rotate_gizmo_ring_verts[k * 6 + 5] = 0.0f;
	}

	// Editor origin lines
	if (GLES1::Config::get_singleton()->support_vbo) {
		// Prebake the editor origin lines directly to VRAM.
		glGenBuffers(1, &editor_lines_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, editor_lines_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(line_verts), line_verts, GL_STATIC_DRAW);

		glGenBuffers(1, &editor_lines_color_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, editor_lines_color_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(line_colors), line_colors, GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::initialize: TOOLS_ENABLED editor lines prebake");
	}
#endif

	// MultiMesh may read from color when color is disabled, so make sure that the color defaults to white instead of black.
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	// Prevent affine interpolation artifacts.
	// Varying parameters are often interpolated in
	// screen space rather than perspective space.
	// This will cause issues which is why we set this up.
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::initialize: glHint GL_PERSPECTIVE_CORRECTION_HINT");
}

RenderGeometryInstance *RasterizerSceneGLES1::geometry_instance_create(RID p_base) {
	RS::InstanceType type = RSG::utilities->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RS::INSTANCE_GEOMETRY_MASK), nullptr);

	GeometryInstanceGLES1 *ginstance = geometry_instance_alloc.alloc();
	ginstance->data = memnew(GeometryInstanceGLES1::Data);

	ERR_FAIL_NULL_V(ginstance->data, nullptr);

	ginstance->data->base = p_base;
	ginstance->data->base_type = type;
	ginstance->data->dependency_tracker.userdata = ginstance;
	ginstance->data->dependency_tracker.changed_callback = _geometry_instance_dependency_changed;
	ginstance->data->dependency_tracker.deleted_callback = _geometry_instance_dependency_deleted;

	ginstance->_mark_dirty();

	return ginstance;
}

uint32_t RasterizerSceneGLES1::geometry_instance_get_pair_mask() {
	return ((1 << RS::INSTANCE_LIGHT) | (1 << RS::INSTANCE_REFLECTION_PROBE));
}

void RasterizerSceneGLES1::GeometryInstanceGLES1::pair_light_instances(const RID *p_light_instances, uint32_t p_light_instance_count) {

}

void RasterizerSceneGLES1::GeometryInstanceGLES1::pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) {

}

void RasterizerSceneGLES1::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES1 *ginstance = static_cast<GeometryInstanceGLES1 *>(p_geometry_instance);
	ERR_FAIL_NULL(ginstance);
	GeometryInstanceSurface *surf = ginstance->surface_caches;
	while (surf) {
		GeometryInstanceSurface *next = surf->next;
		_remove_cached_surface(surf);
		geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	memdelete(ginstance->data);
	geometry_instance_alloc.free(ginstance);
}

void RasterizerSceneGLES1::GeometryInstanceGLES1::_mark_dirty() {
	if (dirty_list_element.in_list()) {
		return;
	}

	// Clear surface caches
	GeometryInstanceSurface *surf = surface_caches;
	while (surf) {
		GeometryInstanceSurface *next = surf->next;
		RasterizerSceneGLES1::get_singleton()->_remove_cached_surface(surf);
		RasterizerSceneGLES1::get_singleton()->geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	surface_caches = nullptr;

	RasterizerSceneGLES1::get_singleton()->geometry_instance_dirty_list.add(&dirty_list_element);
}

void RasterizerSceneGLES1::GeometryInstanceGLES1::set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) {

}

void RasterizerSceneGLES1::GeometryInstanceGLES1::set_lightmap_capture(const Color *p_sh9) {

}

void RasterizerSceneGLES1::_update_dirty_geometry_instances() {
	while (geometry_instance_dirty_list.first()) {
		_geometry_instance_update(geometry_instance_dirty_list.first()->self());
	}
}

void RasterizerSceneGLES1::_remove_cached_surface(GeometryInstanceSurface* p_surface) {
	if (p_surface->gizmo_cached) {
		if (p_surface->gizmo_vertex_buffer != 0) {
			glDeleteBuffers(1, &p_surface->gizmo_vertex_buffer);
		}
		if (p_surface->gizmo_color_buffer != 0) {
			glDeleteBuffers(1, &p_surface->gizmo_color_buffer);
		}
		if (p_surface->gizmo_index_buffer != 0) {
			glDeleteBuffers(1, &p_surface->gizmo_index_buffer);
		}
		if (p_surface->gizmo_vertex_array) {
			memdelete_arr(p_surface->gizmo_vertex_array);
		}
		if (p_surface->gizmo_index_array) {
			memdelete_arr(p_surface->gizmo_index_array);
		}
	}
}

void RasterizerSceneGLES1::_geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker) {
	switch (p_notification) {
		case Dependency::DEPENDENCY_CHANGED_MATERIAL:
		case Dependency::DEPENDENCY_CHANGED_MESH:
		case Dependency::DEPENDENCY_CHANGED_PARTICLES:
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH:
		case Dependency::DEPENDENCY_CHANGED_SKELETON_DATA: {
			static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
			static_cast<GeometryInstanceGLES1 *>(p_tracker->userdata)->data->dirty_dependencies = true;
		} break;
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES: {
			GeometryInstanceGLES1 *ginstance = static_cast<GeometryInstanceGLES1 *>(p_tracker->userdata);
			if (ginstance->data->base_type == RS::INSTANCE_MULTIMESH) {
				ginstance->instance_count = GLES1::MeshStorage::get_singleton()->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		default: {
			//rest of notifications of no interest
		} break;
	}
}

void RasterizerSceneGLES1::_geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker) {
	static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
	static_cast<GeometryInstanceGLES1 *>(p_tracker->userdata)->data->dirty_dependencies = true;
}

void RasterizerSceneGLES1::_geometry_instance_add_surface_with_material(GeometryInstanceGLES1 *ginstance, uint32_t p_surface, GLES1::SceneMaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh) {

}

void RasterizerSceneGLES1::_geometry_instance_add_surface_with_material_chain(GeometryInstanceGLES1 *ginstance, uint32_t p_surface, GLES1::SceneMaterialData *p_material_data, RID p_mat_src, RID p_mesh) {

}

void RasterizerSceneGLES1::_geometry_instance_add_surface(GeometryInstanceGLES1 *ginstance, uint32_t p_surface, RID p_material, RID p_mesh) {
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();
	RID m_src = ginstance->data->material_override.is_valid() ? ginstance->data->material_override : p_material;
	GLES1::SceneMaterialData *material_data = nullptr;

	if (m_src.is_valid()) {
		material_data = static_cast<GLES1::SceneMaterialData *>(material_storage->material_get_data(m_src, RS::SHADER_SPATIAL));
		if (!material_data || !material_data->shader_data->valid) {
			material_data = nullptr;
		}
	}

	if (!material_data) {
		material_data = static_cast<GLES1::SceneMaterialData *>(material_storage->material_get_data(scene_globals.default_material, RS::SHADER_SPATIAL));
		m_src = scene_globals.default_material;
	}

	if (!material_data) {
		return;
	}

	GeometryInstanceSurface *surf = geometry_instance_surface_alloc.alloc();

	bool has_read_screen_alpha = material_data->shader_data->uses_screen_texture || material_data->shader_data->uses_depth_texture || material_data->shader_data->uses_normal_texture;
	bool has_base_alpha = ((material_data->shader_data->uses_alpha && !material_data->shader_data->uses_alpha_clip) || has_read_screen_alpha);
	bool has_blend_alpha = material_data->shader_data->uses_blend_alpha;
	bool has_alpha = has_base_alpha || has_blend_alpha;

	uint32_t flags = 0;
	if (material_data->shader_data->uses_screen_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_SCREEN_TEXTURE;
	}
	if (material_data->shader_data->uses_depth_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_DEPTH_TEXTURE;
	}
	if (material_data->shader_data->uses_normal_texture) {
		flags |= GeometryInstanceSurface::FLAG_USES_NORMAL_TEXTURE;
	}

	if (has_alpha || has_read_screen_alpha || material_data->shader_data->depth_draw == GLES1::SceneShaderData::DEPTH_DRAW_DISABLED || material_data->shader_data->depth_test == GLES1::SceneShaderData::DEPTH_TEST_DISABLED) {
		flags |= GeometryInstanceSurface::FLAG_PASS_ALPHA;
		if (material_data->shader_data->uses_depth_prepass_alpha && !(material_data->shader_data->depth_draw == GLES1::SceneShaderData::DEPTH_DRAW_DISABLED || material_data->shader_data->depth_test == GLES1::SceneShaderData::DEPTH_TEST_DISABLED)) {
			flags |= GeometryInstanceSurface::FLAG_PASS_DEPTH;
		}
	} else {
		flags |= GeometryInstanceSurface::FLAG_PASS_OPAQUE;
		flags |= GeometryInstanceSurface::FLAG_PASS_DEPTH;
	}

	surf->flags = flags;
	surf->shader = material_data->shader_data;
	surf->material = material_data;
	surf->surface = GLES1::MeshStorage::get_singleton()->mesh_get_surface(p_mesh, p_surface);
	surf->primitive = GLES1::MeshStorage::get_singleton()->mesh_surface_get_primitive(surf->surface);
	surf->surface_index = p_surface;

	Array arrays = GLES1::MeshStorage::get_singleton()->mesh_surface_get_arrays(p_mesh, p_surface);
	if (!arrays.is_empty()) {
		surf->vertex_cache = arrays[RS::ARRAY_VERTEX];
		surf->normal_cache = arrays[RS::ARRAY_NORMAL];
		surf->uv_cache = arrays[RS::ARRAY_TEX_UV];
		surf->color_cache = arrays[RS::ARRAY_COLOR];
		surf->index_cache = arrays[RS::ARRAY_INDEX];
		surf->index_count = surf->index_cache.size() > 0 ? surf->index_cache.size() : surf->vertex_cache.size();
	} else {
		surf->index_count = 0;
	}
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_geometry_instance_add_surface: query mesh arrays capacity");

	surf->owner = ginstance;
	surf->next = ginstance->surface_caches;
	ginstance->surface_caches = surf;

	surf->sort.sort_key1 = 0;
	surf->sort.sort_key2 = 0;
	surf->sort.priority = material_data->priority;
}

void RasterizerSceneGLES1::_geometry_instance_update(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceGLES1 *ginstance = static_cast<GeometryInstanceGLES1 *>(p_geometry_instance);
	GLES1::MeshStorage *mesh_storage = GLES1::MeshStorage::get_singleton();
	GLES1::ParticlesStorage *particles_storage = GLES1::ParticlesStorage::get_singleton();

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_begin();
	}

	switch (ginstance->data->base_type) {
		case RS::INSTANCE_MESH: {
			const RID *materials = nullptr;
			uint32_t surface_count;
			RID mesh = ginstance->data->base;

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

void RasterizerSceneGLES1::_free_sky_data(Sky *p_sky) {
	if (p_sky->radiance != 0) {
		GLES1::Utilities::get_singleton()->texture_free_data(p_sky->radiance);
		p_sky->radiance = 0;
		GLES1::Utilities::get_singleton()->texture_free_data(p_sky->raw_radiance);
		p_sky->raw_radiance = 0;
		if (GLES1::Config::get_singleton()->support_fbo) {
			glDeleteFramebuffersOES(1, &p_sky->radiance_framebuffer);
		}
		p_sky->radiance_framebuffer = 0;
	}
}

RID RasterizerSceneGLES1::sky_allocate() {
	return sky_owner.allocate_rid();
}

void RasterizerSceneGLES1::sky_initialize(RID p_rid) {
	sky_owner.initialize_rid(p_rid);
}

void RasterizerSceneGLES1::sky_set_radiance_size(RID p_sky, int p_radiance_size) {
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

void RasterizerSceneGLES1::sky_set_mode(RID p_sky, RS::SkyMode p_mode) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);

	if (sky->mode == p_mode) {
		return;
	}

	sky->mode = p_mode;
	_invalidate_sky(sky);
}

void RasterizerSceneGLES1::sky_set_material(RID p_sky, RID p_material) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);

	if (sky->material == p_material) {
		return;
	}

	sky->material = p_material;
	_invalidate_sky(sky);
}

float RasterizerSceneGLES1::sky_get_baked_exposure(RID p_sky) const {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL_V(sky, 1.0);

	return sky->baked_exposure;
}

void RasterizerSceneGLES1::_invalidate_sky(Sky *p_sky) {
	if (!p_sky->dirty) {
		p_sky->dirty = true;
		p_sky->dirty_list = dirty_sky_list;
		dirty_sky_list = p_sky;
	}
}

void RasterizerSceneGLES1::_update_dirty_skys() {
	Sky *sky = dirty_sky_list;

	while (sky) {
		if (sky->radiance == 0) {
			sky->mipmap_count = Image::get_image_required_mipmaps(sky->radiance_size, sky->radiance_size, Image::FORMAT_RGBA8) - 1;
			// Left uninitialized, will attach a texture at render time
			if (GLES1::Config::get_singleton()->support_fbo) {
				glGenFramebuffers(1, &sky->radiance_framebuffer);
			}
			sky->radiance = _init_radiance_texture_gles1(sky->radiance_size, sky->mipmap_count, "Sky radiance texture");
			sky->raw_radiance = _init_radiance_texture_gles1(sky->radiance_size, sky->mipmap_count, "Sky raw radiance texture");
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

void RasterizerSceneGLES1::_setup_sky(const RenderDataGLES1 *p_render_data, const PagedArray<RID> &p_lights, const Projection &p_projection, const Transform3D &p_transform, const Size2i p_screen_size) {
	GLES1::LightStorage *light_storage = GLES1::LightStorage::get_singleton();
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();
	if (p_render_data->environment.is_null()) {
		return;
	}

	GLES1::SkyMaterialData *material = nullptr;
	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_render_data->environment));

	RID sky_material;
	GLES1::SkyShaderData *shader_data = nullptr;

	if (sky) {
		sky_material = sky->material;

		if (sky_material.is_valid()) {
			material = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material || !material->shader_data->valid) {
				material = nullptr;
			}
		}

		if (!material) {
			sky_material = sky_globals.default_material;
			material = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}

		ERR_FAIL_COND(!material);

		shader_data = material->shader_data;
		ERR_FAIL_COND(!shader_data);

		if (shader_data->uses_time && time - sky->prev_time > 1.0) {
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
		}

		if (!p_transform.origin.is_equal_approx(sky->prev_position) && shader_data->uses_position) {
			sky->prev_position = p_transform.origin;
		}

		if (shader_data->uses_light) {
			sky_globals.directional_light_count = 0;
			for (int i = 0; i < (int)p_lights.size(); i++) {
				GLES1::LightInstance *li = GLES1::LightStorage::get_singleton()->get_light_instance(p_lights[i]);
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

void RasterizerSceneGLES1::_draw_sky(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier, float p_luminance_multiplier, bool p_use_multiview, bool p_flip_y, bool p_apply_color_adjustments_in_post) {
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();

	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_env));
	GLES1::SkyMaterialData *material_data = nullptr;
	RID sky_material;

	RS::EnvironmentBG background = RS::ENV_BG_CLEAR_COLOR;
	if (p_env.is_valid()) {
		background = environment_get_background(p_env);
	}

	if (sky) {
		sky_material = sky->material;
		if (sky_material.is_valid()) {
			material_data = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material_data || !material_data->shader_data->valid) {
				material_data = nullptr;
			}
		}

		if (!material_data) {
			sky_material = sky_globals.default_material;
			material_data = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}
	} else if (background == RS::ENV_BG_CLEAR_COLOR || background == RS::ENV_BG_COLOR || p_env.is_null()) {
		sky_material = sky_globals.fog_material;
		material_data = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
	}

	if (!material_data) {
		return;
	}

	GLES1::SkyShaderData *shader_data = material_data->shader_data;
	if (!shader_data) {
		return;
	}

	// Push the matrices before everything.
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	uint64_t sky_spec = 0;
	if (p_flip_y) {
		sky_spec |= SkyShaderGLES1::USE_INVERTED_Y;
	}
	if (!p_apply_color_adjustments_in_post) {
		sky_spec |= SkyShaderGLES1::APPLY_TONEMAPPING;
	}

	bool success = material_storage->shaders.sky_shader.version_bind_shader(shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	if (!success) {
		// Pop the matrices
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
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
	correction.set_depth_correction(p_flip_y, false, false);
	camera = correction * camera;

	Basis sky_transform;
	if (p_env.is_valid()) {
		sky_transform = environment_get_sky_orientation(p_env);
		sky_transform.invert();
	}
	sky_transform = sky_transform * p_transform.basis;

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::ORIENTATION, sky_transform, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::PROJECTION, camera.columns[2][0], camera.columns[0][0], camera.columns[2][1], camera.columns[1][1], shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::POSITION, p_transform.origin, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::TIME, (float)time, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::LUMINANCE_MULTIPLIER, p_luminance_multiplier, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::EXPOSURE, scene_state.tonemap_ubo.exposure, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::WHITE, scene_state.tonemap_ubo.white, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);

	if (p_env.is_valid()) {
		Color fog_color = environment_get_fog_light_color(p_env).srgb_to_linear() * environment_get_fog_light_energy(p_env);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::FOG_ENABLED, environment_get_fog_enabled(p_env), shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::FOG_AERIAL_PERSPECTIVE, environment_get_fog_aerial_perspective(p_env), shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::FOG_LIGHT_COLOR, fog_color, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::FOG_SUN_SCATTER, environment_get_fog_sun_scatter(p_env), shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::FOG_DENSITY, environment_get_fog_density(p_env), shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	} else {
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::FOG_ENABLED, false, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	}

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHT_COUNT, (int)sky_globals.directional_light_count, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
	for (uint32_t i = 0; i < sky_globals.directional_light_count; i++) {
		const DirectionalLightData &light = sky_globals.directional_lights[i];
		Vector4 dir_energy(light.direction[0], light.direction[1], light.direction[2], light.energy);
		Vector4 col_size(light.color[0], light.color[1], light.color[2], light.size);
		int32_t enabled = light.enabled ? 1 : 0;

		switch (i) {
			case 0:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_0_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_0_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_0_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				break;
			case 1:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_1_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_1_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_1_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				break;
			case 2:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_2_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_2_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_2_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				break;
			case 3:
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_3_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_3_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_3_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);
				break;
		}
	}

	material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::Z_FAR, p_projection.get_z_far(), shader_data->version, SkyShaderGLES1::MODE_BACKGROUND, sky_spec);

	// Clean-up from opaque geometry passes
	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(false);
	glDepthFunc(GL_GEQUAL);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glDepthFunc");

	scene_state.enable_gl_blend(false);
	scene_state.set_gl_cull_mode(RS::CULL_MODE_BACK);

	// Setup identity matrices for full-screen quad
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Disable more client states
	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);
	if (GLES1::Config::get_singleton()->max_texture_units > 1) {
		glActiveTexture(GL_TEXTURE0);
	}
	glDisable(GL_TEXTURE_2D);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glDisable GL_LIGHTING, GL_FOG and GL_TEXTURE_2D");

	glEnableClientState(GL_VERTEX_ARRAY);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glEnableClientState GL_VERTEX_ARRAY");
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);

	if (GLES1::Config::get_singleton()->max_texture_units > 1) {
		glClientActiveTexture(GL_TEXTURE1);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glClientActiveTexture(GL_TEXTURE0);
	}

	// We need UV coordinates to sample the sky texture.
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: Client State hardened");

	float sky_uvw[12] = {};

	// Replicate the `sky.glsl` shader math manually.
	// We do the same in `_update_sky_radiance`
	for (int i = 0; i < 4; i++) {
		Vector2 uv_interp = Vector2(qv_fallback[i * 3], qv_fallback[i * 3 + 1]);
		if (!p_flip_y) {
			uv_interp.y *= -1.0f;
		}

		Vector3 cube_normal;
		cube_normal.z = -1.0f;
		cube_normal.x = (uv_interp.x - camera.columns[2][0]) / camera.columns[0][0];
		cube_normal.y = (uv_interp.y - camera.columns[2][1]) / camera.columns[1][1];

		cube_normal = sky_transform.xform(cube_normal).normalized();

		sky_uvw[i * 3 + 0] = cube_normal.x;
		sky_uvw[i * 3 + 1] = cube_normal.y;
		sky_uvw[i * 3 + 2] = cube_normal.z;
	}

	if (sky && sky->radiance != 0) {
		glEnable(GL_TEXTURE_CUBE_MAP);
		glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
		glColor4f(p_sky_energy_multiplier, p_sky_energy_multiplier, p_sky_energy_multiplier, 1.0f);
	} else {
		// Fallback if the radiance map isn't baked/ready
		Color sky_color = Color(0.3, 0.3, 0.3, 1.0);
		if (sky_material.is_valid()) {
			Variant v = material_storage->material_get_param(sky_material, "clear_color");
			if (v.get_type() == Variant::COLOR) {
				sky_color = v;
			}
		}
		glColor4f(sky_color.r, sky_color.g, sky_color.b, sky_color.a);
	}
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: Sky state setup");

	// Unbind VBOs
	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	// Pass client-side memory for both pointers
	glVertexPointer(3, GL_FLOAT, 0, qv_fallback);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glVertexPointer");

	glTexCoordPointer(3, GL_FLOAT, 0, sky_uvw);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glTexCoordPointer client array");

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glDrawArrays");

	// Unbind state
	if (sky && sky->radiance != 0) {
		glDisable(GL_TEXTURE_CUBE_MAP);
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glDisableClientState cleanup");

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: unbind GL_ARRAY_BUFFER");
	}

	// Restore state
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: matrix pop");

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_sky: glColor4f restore");
}

void RasterizerSceneGLES1::_update_sky_radiance(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier) {
	if (!GLES1::Config::get_singleton()->support_fbo) {
		return;
	}
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();
	if (p_env.is_null()) {
		return;
	}

	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_env));
	if (!sky) {
		return;
	}

	GLES1::SkyMaterialData *material_data = nullptr;
	RID sky_material;

	RS::EnvironmentBG background = environment_get_background(p_env);

	if (sky) {
		sky_material = sky->material;
		if (sky_material.is_valid()) {
			material_data = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
			if (!material_data || !material_data->shader_data->valid) {
				material_data = nullptr;
			}
		}

		if (!material_data) {
			sky_material = sky_globals.default_material;
			material_data = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
		}
	} else if (background == RS::ENV_BG_CLEAR_COLOR || background == RS::ENV_BG_COLOR) {
		sky_material = sky_globals.fog_material;
		material_data = static_cast<GLES1::SkyMaterialData *>(material_storage->material_get_data(sky_material, RS::SHADER_SKY));
	}

	if (!material_data) {
		return;
	}

	GLES1::SkyShaderData *shader_data = material_data->shader_data;
	if (!shader_data) {
		return;
	}

	bool update_single_frame = sky->mode == RS::SKY_MODE_REALTIME || sky->mode == RS::SKY_MODE_QUALITY;
	RS::SkyMode sky_mode = sky->mode;

	if (sky_mode == RS::SKY_MODE_AUTOMATIC) {
		if (shader_data->uses_time || shader_data->uses_position || shader_data->uses_light || shader_data->ubo_size > 0) {
			update_single_frame = false;
			sky_mode = RS::SKY_MODE_INCREMENTAL;
		} else {
			update_single_frame = true;
			sky_mode = RS::SKY_MODE_QUALITY;
		}
	}

	constexpr int max_processing_layer = 6;

	if (sky->reflection_dirty && sky->processing_layer <= max_processing_layer) {
		Projection cm;
		cm.set_perspective(90, 1, 0.01, 10.0);
		Projection correction;
		correction.set_depth_correction(true, true, false);
		cm = correction * cm;

		uint64_t sky_spec = 0;
		bool success = material_storage->shaders.sky_shader.version_bind_shader(shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
		if (!success) {
			return;
		}

		material_data->bind_uniforms();

		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::POSITION, p_transform.origin, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::TIME, (float)time, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::PROJECTION, cm.columns[2][0], cm.columns[0][0], cm.columns[2][1], cm.columns[1][1], shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::LUMINANCE_MULTIPLIER, p_sky_energy_multiplier, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);

		material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHT_COUNT, (int)sky_globals.directional_light_count, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
		for (uint32_t i = 0; i < sky_globals.directional_light_count; i++) {
			const DirectionalLightData &light = sky_globals.directional_lights[i];
			Vector4 dir_energy(light.direction[0], light.direction[1], light.direction[2], light.energy);
			Vector4 col_size(light.color[0], light.color[1], light.color[2], light.size);
			int32_t enabled = light.enabled ? 1 : 0;

			switch (i) {
				case 0:
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_0_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_0_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_0_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					break;
				case 1:
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_1_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_1_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_1_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					break;
				case 2:
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_2_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_2_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_2_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					break;
				case 3:
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_3_DIRECTION_ENERGY, dir_energy, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_3_COLOR_SIZE, col_size, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					material_storage->shaders.sky_shader.version_set_uniform(SkyShaderGLES1::DIRECTIONAL_LIGHTS_DATA_3_ENABLED, enabled, shader_data->version, SkyShaderGLES1::MODE_CUBEMAP, sky_spec);
					break;
			}
		}

		GLint prev_viewport[4] = {};
		glGetIntegerv(GL_VIEWPORT, prev_viewport);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glGetIntegerv GL_VIEWPORT");

		GLint prev_fbo;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &prev_fbo);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glGetIntegerv GL_FRAMEBUFFER_BINDING_OES");

		glViewport(0, 0, sky->radiance_size, sky->radiance_size);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glViewport");

		glBindFramebufferOES(GL_FRAMEBUFFER_OES, sky->radiance_framebuffer);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glBindFramebufferOES");

		// Protect against uniform matrix leak
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glMatrixMode projection push");

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glMatrixMode modelview push");

		// Reset state
		scene_state.reset_gl_state();
		scene_state.set_gl_cull_mode(RS::CULL_MODE_DISABLED);
		scene_state.enable_gl_blend(false);

		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);

		if (GLES1::Config::get_singleton()->max_texture_units > 1) {
			glClientActiveTexture(GL_TEXTURE1);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glClientActiveTexture(GL_TEXTURE0);
		}
		glEnableClientState(GL_TEXTURE_COORD_ARRAY); // Enable for 3D UVWs
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: Client State hardened");

		// Default approximate sky colours if stuff goes wrong.
		Color sky_top_color = Color(0.385, 0.454, 0.55, 1.0);
		Color sky_horizon_color = Color(0.646, 0.656, 0.67, 1.0);
		float sky_curve = 0.15f;
		float sky_energy = 1.0f;

		Color ground_bottom_color = Color(0.2, 0.169, 0.133, 1.0);
		Color ground_horizon_color = Color(0.646, 0.656, 0.67, 1.0);
		float ground_curve = 0.02f;
		float ground_energy = 1.0f;

		float exposure = 1.0f;

		// If we actually have the material, extract the real colours
		// from it.
		if (sky_material.is_valid()) {
			Variant v;

			// TODO(GLES1): This function gets called every frame.
			// We should cache these sky colours so that
			// these only get updated when absolutely necessary
			// (like we already do with the sky paronama).
			//
			// ...also not have to copy paste these manually.

			v = material_storage->material_get_param(sky_material, "sky_top_color");
			if (v.get_type() == Variant::COLOR) {
				sky_top_color = v;
			}
			v = material_storage->material_get_param(sky_material, "sky_horizon_color");
			if (v.get_type() == Variant::COLOR) {
				sky_horizon_color = v;
			}
			v = material_storage->material_get_param(sky_material, "sky_curve");
			if (v.get_type() == Variant::FLOAT) {
				sky_curve = v;
			}
			v = material_storage->material_get_param(sky_material, "sky_energy");
			if (v.get_type() == Variant::FLOAT) {
				sky_energy = v;
			}

			v = material_storage->material_get_param(sky_material, "ground_bottom_color");
			if (v.get_type() == Variant::COLOR) {
				ground_bottom_color = v;
			}
			v = material_storage->material_get_param(sky_material, "ground_horizon_color");
			if (v.get_type() == Variant::COLOR) {
				ground_horizon_color = v;
			}
			v = material_storage->material_get_param(sky_material, "ground_curve");
			if (v.get_type() == Variant::FLOAT) {
				ground_curve = v;
			}
			v = material_storage->material_get_param(sky_material, "ground_energy");
			if (v.get_type() == Variant::FLOAT) {
				ground_energy = v;
			}

			v = material_storage->material_get_param(sky_material, "exposure");
			if (v.get_type() == Variant::FLOAT) {
				exposure = v;
			}
		}
		sky_curve = MAX(sky_curve, 0.0001f);
		ground_curve = MAX(ground_curve, 0.0001f);

		// Only perform CPU math on the very first layer of processing.
		if (sky->processing_layer == 0) {
			for (int i = 0; i < max_processing_layer; i++) {
				for (int v = 0; v < NUM_VERTICES; v++) {
					Vector3 cube_normal;
					cube_normal.x = sky_globals.radiance_uvw[(i * NUM_VERTICES + v) * 3 + 0];
					cube_normal.y = sky_globals.radiance_uvw[(i * NUM_VERTICES + v) * 3 + 1];
					cube_normal.z = sky_globals.radiance_uvw[(i * NUM_VERTICES + v) * 3 + 2];

					Color c;
					float v_angle = Math::acos(CLAMP(cube_normal.y, -1.0f, 1.0f));

					if (cube_normal.y >= 0.0f) {
						float c_val = 1.0f - (v_angle / (Math_PI * 0.5f));
						float blend = CLAMP(1.0f - Math::pow(1.0f - c_val, 1.0f / sky_curve), 0.0f, 1.0f);
						c = sky_horizon_color.lerp(sky_top_color, blend);
						c.r *= sky_energy;
						c.g *= sky_energy;
						c.b *= sky_energy;
					} else {
						float c_val = (v_angle - (Math_PI * 0.5f)) / (Math_PI * 0.5f);
						float blend = CLAMP(1.0f - Math::pow(1.0f - c_val, 1.0f / ground_curve), 0.0f, 1.0f);
						c = ground_horizon_color.lerp(ground_bottom_color, blend);
						c.r *= ground_energy;
						c.g *= ground_energy;
						c.b *= ground_energy;
					}

					c.r *= exposure;
					c.g *= exposure;
					c.b *= exposure;

					int color_offset = (i * NUM_VERTICES + v) * 4;
					sky_globals.radiance_colors[color_offset + 0] = uint8_t(CLAMP(c.r * 255.0f, 0.0f, 255.0f));
					sky_globals.radiance_colors[color_offset + 1] = uint8_t(CLAMP(c.g * 255.0f, 0.0f, 255.0f));
					sky_globals.radiance_colors[color_offset + 2] = uint8_t(CLAMP(c.b * 255.0f, 0.0f, 255.0f));
					sky_globals.radiance_colors[color_offset + 3] = 255;
				}
			}

			if (GLES1::Config::get_singleton()->support_vbo) {
				glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_colors_vbo);
				glBufferSubData(GL_ARRAY_BUFFER, 0, 6 * NUM_VERTICES * 4 * sizeof(uint8_t), sky_globals.radiance_colors);
				glBindBuffer(GL_ARRAY_BUFFER, 0);
			}
		}

		int first_face = 0;
		int last_face = 5;

		if (!update_single_frame) {
			first_face = sky->processing_layer;
			last_face = sky->processing_layer;
		}

		if (sky->processing_layer < max_processing_layer) {
			for (int i = first_face; i <= last_face; i++) {
				if (GLES1::Config::get_singleton()->support_vbo) {
					glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_verts_vbo);
					glVertexPointer(2, GL_FLOAT, 0, (void *)(uintptr_t)(i * NUM_VERTICES * 2 * sizeof(float)));

					glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_uvw_vbo);
					glTexCoordPointer(3, GL_FLOAT, 0, (void *)(uintptr_t)(i * NUM_VERTICES * 3 * sizeof(float)));

					glBindBuffer(GL_ARRAY_BUFFER, sky_globals.radiance_colors_vbo);
					glColorPointer(4, GL_UNSIGNED_BYTE, 0, (void *)(uintptr_t)(i * NUM_VERTICES * 4 * sizeof(uint8_t)));
				} else {
					glVertexPointer(2, GL_FLOAT, 0, sky_globals.radiance_verts + (i * NUM_VERTICES * 2));
					glTexCoordPointer(3, GL_FLOAT, 0, sky_globals.radiance_uvw + (i * NUM_VERTICES * 3));
					glColorPointer(4, GL_UNSIGNED_BYTE, 0, sky_globals.radiance_colors + (i * NUM_VERTICES * 4));
				}

				glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, sky->radiance, 0);
				GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glFramebufferTexture2DOES");
				glDrawArrays(GL_TRIANGLES, 0, NUM_VERTICES);
				GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glDrawArrays");
			}
		}

		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		if (GLES1::Config::get_singleton()->support_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: VBO cleanup");

		if (update_single_frame) {
			sky->processing_layer = max_processing_layer;
		} else {
			sky->processing_layer++;
		}

		if (sky->processing_layer >= max_processing_layer) {
			// Native cubemap LODs
			if (GLES1::Config::get_singleton()->max_texture_units > 1) {
				glActiveTexture(GL_TEXTURE0);
			}
			glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
			glGenerateMipmapOES(GL_TEXTURE_CUBE_MAP_OES);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: glGenerateMipmapOES");

			sky->processing_layer = max_processing_layer + 1;
			sky->baked_exposure = p_sky_energy_multiplier;
			sky->reflection_dirty = false;
		}

		// Restore matrix stack
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: matrix pop");

		glBindFramebufferOES(GL_FRAMEBUFFER_OES, prev_fbo);
		glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_update_sky_radiance: restore GL state");

	} else {
		if (sky_mode == RS::SKY_MODE_INCREMENTAL && sky->processing_layer < max_processing_layer) {
			sky->processing_layer++;
		}
	}
}

Ref<Image> RasterizerSceneGLES1::sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) {
	if (!GLES1::Config::get_singleton()->support_fbo) {
		return Ref<Image>();
	}

	Sky *sky = sky_owner.get_or_null(p_sky);
	if (!sky) {
		return Ref<Image>();
	}

	_update_dirty_skys();

	if (sky->radiance == 0) {
		return Ref<Image>();
	}

	GLES1::CopyEffects *copy_effects = GLES1::CopyEffects::get_singleton();
	if (!copy_effects) {
		return Ref<Image>();
	}

	GLuint rad_tex = 0;
	glGenTextures(1, &rad_tex);
	glBindTexture(GL_TEXTURE_2D, rad_tex);

	bool use_float = false; // Config fallback to unsigned byte as default for GLES1 portability.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_size.width, p_size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	GLES1::Utilities::get_singleton()->texture_allocated_data(rad_tex, p_size.width * p_size.height * 4, "Temp sky panorama");
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: glTexImage2D");

	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &prev_fbo);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: glGetIntegerv GL_FRAMEBUFFER_BINDING_OES");

	GLuint rad_fbo = 0;
	glGenFramebuffersOES(1, &rad_fbo);
	glBindFramebufferOES(GL_FRAMEBUFFER_OES, rad_fbo);
	glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, rad_tex, 0);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: glFramebufferTexture2DOES");

	if (GLES1::Config::get_singleton()->max_texture_units > 1) {
		glActiveTexture(GL_TEXTURE0);
	}
	glBindTexture(GL_TEXTURE_CUBE_MAP, sky->radiance);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: glBindTexture");

	GLint prev_viewport[4];
	glGetIntegerv(GL_VIEWPORT, prev_viewport);
	glViewport(0, 0, p_size.width, p_size.height);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: glViewport");

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: glClear");

	copy_effects->copy_cube_to_panorama(p_bake_irradiance ? float(sky->mipmap_count) : 0.0);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: copy_cube_to_panorama");

	glBindFramebufferOES(GL_FRAMEBUFFER_OES, prev_fbo);
	glDeleteFramebuffersOES(1, &rad_fbo);
	glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::sky_bake_panorama: restore GL state");

	RID tex_rid = GLES1::TextureStorage::get_singleton()->texture_allocate();
	{
		GLES1::Texture texture;
		texture.width = p_size.width;
		texture.height = p_size.height;
		texture.alloc_width = p_size.width;
		texture.alloc_height = p_size.height;
		if (use_float && GLES1::Config::get_singleton()->float_texture_supported) {
			texture.format = Image::FORMAT_RGBF;
			texture.real_format = Image::FORMAT_RGBF;
			texture.gl_type_cache = GL_FLOAT;
		} else {
			texture.format = Image::FORMAT_RGB8;
			texture.real_format = Image::FORMAT_RGB8;
			texture.gl_type_cache = GL_UNSIGNED_BYTE;
		}
		texture.gl_format_cache = GL_RGBA;
		texture.type = GLES1::Texture::TYPE_2D;
		texture.target = GL_TEXTURE_2D;
		texture.active = true;
		texture.tex_id = rad_tex;
		texture.is_render_target = true; // HACK: Prevent TextureStorage from retaining a cached copy of the texture.
		GLES1::TextureStorage::get_singleton()->texture_2d_initialize_from_texture(tex_rid, texture);
	}

	Ref<Image> img = GLES1::TextureStorage::get_singleton()->texture_2d_get(tex_rid);
	GLES1::Utilities::get_singleton()->texture_free_data(rad_tex);

	GLES1::Texture *texture = GLES1::TextureStorage::get_singleton()->get_texture(tex_rid);
	if (texture) {
		texture->is_render_target = false; // HACK: Avoid an error when freeing the texture.
		texture->tex_id = 0;
	}
	GLES1::TextureStorage::get_singleton()->texture_free(tex_rid);

	if (img.is_valid()) {
		for (int i = 0; i < p_size.width; i++) {
			for (int j = 0; j < p_size.height; j++) {
				Color c = img->get_pixel(i, j);
				c.r *= p_energy;
				c.g *= p_energy;
				c.b *= p_energy;
				img->set_pixel(i, j, c);
			}
		}
	}
	return img;
}

/* ENVIRONMENT API */

void RasterizerSceneGLES1::environment_glow_set_use_bicubic_upscale(bool p_enable) {
	glow_bicubic_upscale = p_enable;
}

void RasterizerSceneGLES1::environment_set_ssr_roughness_quality(RS::EnvironmentSSRRoughnessQuality p_quality) {
}

void RasterizerSceneGLES1::environment_set_ssao_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ssao_quality = p_quality;
}

void RasterizerSceneGLES1::environment_set_ssil_quality(RS::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
}

void RasterizerSceneGLES1::environment_set_sdfgi_ray_count(RS::EnvironmentSDFGIRayCount p_ray_count) {
}

void RasterizerSceneGLES1::environment_set_sdfgi_frames_to_converge(RS::EnvironmentSDFGIFramesToConverge p_frames) {
}

void RasterizerSceneGLES1::environment_set_sdfgi_frames_to_update_light(RS::EnvironmentSDFGIFramesToUpdateLight p_update) {
}

void RasterizerSceneGLES1::environment_set_volumetric_fog_volume_size(int p_size, int p_depth) {
}

void RasterizerSceneGLES1::environment_set_volumetric_fog_filter_active(bool p_enable) {
}

Ref<Image> RasterizerSceneGLES1::environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) {
	return Ref<Image>();
}

void RasterizerSceneGLES1::positional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.positional_shadow_quality = p_quality;
}

void RasterizerSceneGLES1::directional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) {
	scene_state.directional_shadow_quality = p_quality;
}

RID RasterizerSceneGLES1::fog_volume_instance_create(RID p_fog_volume) {
	return RID();
}

void RasterizerSceneGLES1::fog_volume_instance_set_transform(RID p_fog_volume_instance, const Transform3D &p_transform) {
}

void RasterizerSceneGLES1::fog_volume_instance_set_active(RID p_fog_volume_instance, bool p_active) {
}

RID RasterizerSceneGLES1::fog_volume_instance_get_volume(RID p_fog_volume_instance) const {
	return RID();
}

Vector3 RasterizerSceneGLES1::fog_volume_instance_get_position(RID p_fog_volume_instance) const {
	return Vector3();
}

RID RasterizerSceneGLES1::voxel_gi_instance_create(RID p_voxel_gi) {
	return RID();
}

void RasterizerSceneGLES1::voxel_gi_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform) {
}

bool RasterizerSceneGLES1::voxel_gi_needs_update(RID p_probe) const {
	return false;
}

void RasterizerSceneGLES1::voxel_gi_update(RID p_probe, bool p_update_light_instances, const Vector<RID> &p_light_instances, const PagedArray<RenderGeometryInstance *> &p_dynamic_objects) {
}

void RasterizerSceneGLES1::voxel_gi_set_quality(RS::VoxelGIQuality) {
}

/* BATCH API */

void RasterizerSceneGLES1::scene_render_items_implementation(GeometryInstanceSurface **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent) {
	for (int i = 0; i < p_count; i++) {
		_render_single_item_immediate(p_surfaces[i]);
	}
}

void RasterizerSceneGLES1::_batch_get_hardware_limits(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchLimits &r_limits) {
	// For GLES1 we do stuff manually via CPU-side transforms, so the matrix palette
	// here is set to infinite.
	r_limits.max_vertices_per_buffer = 65536;
	r_limits.max_indices_per_buffer = 65536 * 2;
}

void RasterizerSceneGLES1::_batch_get_instance_geometry_capacity(const GeometryInstanceSurface *p_surface, uint32_t &r_vertex_count, uint32_t &r_index_count) {
	uint32_t drawn_vertex_count = p_surface->vertex_cache.size();
	uint32_t drawn_index_count = p_surface->index_cache.size();

	// Multiply by instance count so multimeshes automatically
	// bypass the batcher if necessary
	uint32_t instances = p_surface->owner->instance_count > 0 ? p_surface->owner->instance_count : 1;

	r_vertex_count = drawn_vertex_count * instances;
	r_index_count = drawn_index_count * instances;

	// Ensure unindexed geometry does not starve the index buffer requirement.
	if (r_index_count == 0) {
		r_index_count = r_vertex_count;
	}
}

float RasterizerSceneGLES1::_batch_get_item_depth(const GeometryInstanceSurface *p_surface, const Transform3D &p_camera_transform) {
	// Planar depth: Dot product of the camera's look vector
	// and the vector to the object's centre
	Vector3 look_vector = -p_camera_transform.basis.get_column(2);

	Vector3 center = p_surface->owner->transform.origin;
	if (p_surface->owner->use_aabb_center) {
		center = p_surface->owner->transformed_aabb.position + (p_surface->owner->transformed_aabb.size * 0.5f);
	}

	Vector3 to_object = center - p_camera_transform.origin;
	return look_vector.dot(to_object) - p_surface->owner->sorting_offset;
}

uint64_t RasterizerSceneGLES1::_batch_get_state_hash(const GeometryInstanceSurface *p_surface) {
	uint64_t hash = 0;

	uint64_t cull_mode = p_surface->shader ? p_surface->shader->cull_mode : 0;
	hash |= (cull_mode & 0x3) << 62; // Bits 63-62: Cull Mode

	uint64_t depth_test = 0;

	if (p_surface->shader && p_surface->shader->depth_test == GLES1::SceneShaderData::DEPTH_TEST_ENABLED) {
		depth_test = 1;
	}
	hash |= (depth_test & 0x1) << 61; // Bit 61: Depth Test

	// Bits 60-48: Shader version
	uint64_t shader_id = p_surface->shader ? p_surface->shader->version.get_id() : 0;
	hash |= (shader_id & 0x1FFF) << 48;

	// Bits 47-16: Material ID
	uint64_t mat_id = p_surface->material ? static_cast<uint64_t>((uintptr_t)p_surface->material >> 4) : 0;
	hash |= (mat_id & 0xFFFFFFFF) << 16;

	// Bits 15-0: Mesh surface ID / FVF Profile
	uint64_t surface_id = p_surface->surface_index;
	hash |= (surface_id & 0xFFFF);

	return hash;
}

GLES1::SceneMaterialData *RasterizerSceneGLES1::_batch_get_material_data(const GeometryInstanceSurface *p_surface) {
	return p_surface->material;
}

void RasterizerSceneGLES1::_batch_fill_instance_geometry(const GeometryInstanceSurface *p_surface, RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform) {
	const PackedVector3Array &positions = p_surface->vertex_cache;
	if (positions.is_empty()) {
		return;
	}

	const PackedVector3Array &normals = p_surface->normal_cache;
	const PackedVector2Array &uvs = p_surface->uv_cache;
	const PackedColorArray &colors = p_surface->color_cache;
	const PackedInt32Array &indices = p_surface->index_cache;

	Transform3D xform = p_surface->owner->transform;
	Basis normal_basis = xform.basis.inverse().transposed();

	uint32_t v_count = positions.size();
	const Vector3 *pos_ptr = positions.ptr();
	const Vector3 *norm_ptr = normals.size() > 0 ? normals.ptr() : nullptr;
	const Vector2 *uv_ptr = uvs.size() > 0 ? uvs.ptr() : nullptr;
	const Color *col_ptr = colors.size() > 0 ? colors.ptr() : nullptr;

	for (uint32_t i = 0; i < v_count; i++) {
		r_bvs[i].pos.set(xform.xform(pos_ptr[i]));

		if (norm_ptr) {
			r_bvs[i].normal.set(normal_basis.xform(norm_ptr[i]).normalized());
		} else {
			r_bvs[i].normal.set(0, 1, 0);
		}

		if (uv_ptr) {
			r_bvs[i].uv.set(uv_ptr[i]);
		} else {
			r_bvs[i].uv.set(0, 0);
		}

		if (col_ptr) {
			r_bvs[i].color.set(col_ptr[i]);
		} else {
			r_bvs[i].color.set_white();
		}
	}

	if (r_inds && indices.size() > 0) {
		uint32_t i_count = indices.size();
		const int32_t *idx_ptr = indices.ptr();
		for (uint32_t i = 0; i < i_count; i++) {
			r_inds[i] = (uint16_t)(idx_ptr[i] + p_start_vert);
		}
	} else if (r_inds) {
		// Auto-generate sequential indices for unindexed meshes.
		for (uint32_t i = 0; i < v_count; i++) {
			r_inds[i] = (uint16_t)(i + p_start_vert);
		}
	}
}

void RasterizerSceneGLES1::_batch_fill_multimesh_geometry(const GeometryInstanceSurface *p_surface, RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform) {
	if (!p_surface || !p_surface->surface || p_surface->owner->instance_count <= 0) {
		return;
	}

	const PackedVector3Array &positions = p_surface->vertex_cache;
	if (positions.is_empty()) {
		return;
	}

	const PackedVector3Array &normals = p_surface->normal_cache;
	const PackedVector2Array &uvs = p_surface->uv_cache;
	const PackedColorArray &colors = p_surface->color_cache;
	const PackedInt32Array &indices = p_surface->index_cache;

	uint32_t v_count = positions.size();
	const Vector3 *pos_ptr = positions.ptr();
	const Vector3 *norm_ptr = normals.size() > 0 ? normals.ptr() : nullptr;
	const Vector2 *uv_ptr = uvs.size() > 0 ? uvs.ptr() : nullptr;
	const Color *col_ptr = colors.size() > 0 ? colors.ptr() : nullptr;

	int instances = p_surface->owner->instance_count;
	RID base_rid = p_surface->owner->data->base;

	uint32_t bvs_idx = 0;
	Transform3D owner_transform = p_surface->owner->transform;

	for (int inst = 0; inst < instances; inst++) {
		Transform3D xform;
		Color inst_color = Color(1, 1, 1, 1);

		if (p_surface->owner->data->base_type == RS::INSTANCE_MULTIMESH) {
			xform = GLES1::MeshStorage::get_singleton()->multimesh_instance_get_transform(base_rid, inst);
			inst_color = GLES1::MeshStorage::get_singleton()->multimesh_instance_get_color(base_rid, inst);
		}

		Transform3D world_xform = owner_transform * xform;
		Basis normal_basis = world_xform.basis.inverse().transposed();

		for (uint32_t i = 0; i < v_count; i++) {
			r_bvs[bvs_idx].pos.set(world_xform.xform(pos_ptr[i]));

			if (norm_ptr) {
				r_bvs[bvs_idx].normal.set(normal_basis.xform(norm_ptr[i]).normalized());
			} else {
				r_bvs[bvs_idx].normal.set(0, 1, 0);
			}

			if (uv_ptr) {
				r_bvs[bvs_idx].uv.set(uv_ptr[i]);
			} else {
				r_bvs[bvs_idx].uv.set(0, 0);
			}

			if (col_ptr) {
				r_bvs[bvs_idx].color.set(col_ptr[i] * inst_color);
			} else {
				r_bvs[bvs_idx].color.set(inst_color);
			}

			bvs_idx++;
		}
	}

	if (r_inds && indices.size() > 0) {
		uint32_t i_count = indices.size();
		const int32_t *idx_ptr = indices.ptr();

		uint32_t inds_idx = 0;
		for (int inst = 0; inst < instances; inst++) {
			for (uint32_t i = 0; i < i_count; i++) {
				r_inds[inds_idx++] = (uint16_t)(idx_ptr[i] + p_start_vert + (inst * v_count));
			}
		}
	} else if (r_inds) {
		// Auto-generate sequential indices for unindexed meshes.
		uint32_t inds_idx = 0;
		for (int inst = 0; inst < instances; inst++) {
			for (uint32_t i = 0; i < v_count; i++) {
				r_inds[inds_idx++] = (uint16_t)(i + p_start_vert + (inst * v_count));
			}
		}
	}
}

void RasterizerSceneGLES1::_batch_upload_buffers() {
	if (!bdata.gl_vertex_buffer) {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glGenBuffers(1, &bdata.gl_vertex_buffer);
			glGenBuffers(1, &bdata.gl_index_buffer);
		} else {
			bdata.gl_vertex_buffer = 0;
			bdata.gl_index_buffer = 0;
		}
	}

	if (bdata.gl_vertex_buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, bdata.unit_vertices.size() * bdata.unit_vertices.get_unit_size_bytes(), bdata.unit_vertices.get_data(), GL_DYNAMIC_DRAW);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_upload_buffers: glBufferData ARRAY_BUFFER");

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, bdata.indices.size() * sizeof(uint16_t), bdata.indices.get_data(), GL_DYNAMIC_DRAW);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_upload_buffers: glBufferData ELEMENT_ARRAY_BUFFER");
	}
}

void RasterizerSceneGLES1::_batch_bind_material(GLES1::SceneMaterialData *p_material_data, const Transform3D &p_world_transform) {
	if (p_material_data) {
		if (p_material_data->shader_data) {
			GLES1::MaterialStorage::get_singleton()->shaders.scene_shader.version_bind_shader(p_material_data->shader_data->version, SceneShaderGLES1::MODE_COLOR, 0);
		}

		p_material_data->bind_uniforms();

		if (p_material_data->shader_data) {
			_bind_scene_camera_uniforms(p_material_data->shader_data->version, SceneShaderGLES1::MODE_COLOR, 0);
			Transform3D identity;
			GLES1::MaterialStorage::get_singleton()->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::WORLD_TRANSFORM, identity, p_material_data->shader_data->version, SceneShaderGLES1::MODE_COLOR, 0);
		}

		if (p_material_data->use_distance_fade) {
			glEnable(GL_FOG);
			glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_LINEAR));
			glFogf(GL_FOG_START, p_material_data->distance_fade_min);
			glFogf(GL_FOG_END, p_material_data->distance_fade_max);

			GLfloat fog_col[4] = { scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2], 1.0f };
			glFogfv(GL_FOG_COLOR, fog_col);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1_batch_bind_material: glFog overrides");
		} else if (scene_state.ubo.fog_enabled) {
			glEnable(GL_FOG);
			glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_EXP));
			glFogf(GL_FOG_DENSITY, scene_state.ubo.fog_density);
			GLfloat fog_col[4] = { scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2], 1.0f };
			glFogfv(GL_FOG_COLOR, fog_col);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1_batch_bind_material: glFog restore");
		} else {
			glDisable(GL_FOG);
		}
	}
}

void RasterizerSceneGLES1::_batch_render_generic(RS::PrimitiveType p_primitive) {
	if (bdata.indices.size() == 0) {
		return;
	}

	uint32_t stride = bdata.unit_vertices.get_unit_size_bytes();

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	if (bdata.gl_vertex_buffer != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, bdata.gl_vertex_buffer);
		if (bdata.fvf == BatcherEnums::FVF_INSTANCED) {
			glVertexPointer(3, GL_FLOAT, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, pos));
			glNormalPointer(GL_FLOAT, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, normal));
			glTexCoordPointer(2, GL_FLOAT, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, uv));
			glColorPointer(4, GL_UNSIGNED_BYTE, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, color));
		} else {
			glVertexPointer(3, GL_FLOAT, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, pos));
			glNormalPointer(GL_FLOAT, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, normal));
			glTexCoordPointer(2, GL_FLOAT, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, uv));
			glColorPointer(4, GL_UNSIGNED_BYTE, stride, (void *)offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, color));
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_render_generic: VBO Array setup");
	} else {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		const uint8_t *data_ptr = (const uint8_t *)bdata.unit_vertices.get_data();
		if (bdata.fvf == BatcherEnums::FVF_INSTANCED) {
			glVertexPointer(3, GL_FLOAT, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, pos));
			glNormalPointer(GL_FLOAT, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, normal));
			glTexCoordPointer(2, GL_FLOAT, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, uv));
			glColorPointer(4, GL_UNSIGNED_BYTE, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3DInstanced, color));
		} else {
			glVertexPointer(3, GL_FLOAT, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, pos));
			glNormalPointer(GL_FLOAT, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, normal));
			glTexCoordPointer(2, GL_FLOAT, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, uv));
			glColorPointer(4, GL_UNSIGNED_BYTE, stride, data_ptr + offsetof(RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>::BatchVertex3D, color));
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_render_generic: Client Array setup");
	}

	if (bdata.gl_index_buffer != 0) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bdata.gl_index_buffer);
		glDrawElements(GL_TRIANGLES, bdata.indices.size(), GL_UNSIGNED_SHORT, nullptr);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_render_generic: glDrawElements VBO");
	} else {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		glDrawElements(GL_TRIANGLES, bdata.indices.size(), GL_UNSIGNED_SHORT, bdata.indices.get_data());
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_render_generic: glDrawElements Client");
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_render_generic: Unbind state");
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_batch_render_generic: Unbind state");
}

void RasterizerSceneGLES1::_render_single_item_immediate(const GeometryInstanceSurface *p_surface) {
	GLES1::MeshStorage *mesh_storage = GLES1::MeshStorage::get_singleton();
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();

	GLES1::SceneShaderData *shader = p_surface->shader;
	if (!shader || !p_surface->surface) {
		return;
	}

	// Bind shader
	bool success = material_storage->shaders.scene_shader.version_bind_shader(shader->version, SceneShaderGLES1::MODE_COLOR, 0);
	if (!success) {
		return;
	}

	if (p_surface->material) {
		p_surface->material->bind_uniforms();
	}

	// Push camera state
	_bind_scene_camera_uniforms(shader->version, SceneShaderGLES1::MODE_COLOR, 0);

	if (p_surface->material && p_surface->material->use_distance_fade) {
		glEnable(GL_FOG);
		glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_LINEAR));
		glFogf(GL_FOG_START, p_surface->material->distance_fade_min);
		glFogf(GL_FOG_END, p_surface->material->distance_fade_max);

		GLfloat fog_col[4] = { scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2], 1.0f };
		glFogfv(GL_FOG_COLOR, fog_col);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glFog overrides");
	}

	// Upload world transform
	Transform3D world_transform = p_surface->owner->transform;
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::WORLD_TRANSFORM, world_transform, shader->version, SceneShaderGLES1::MODE_COLOR, 0);

	// Apply world transform to modelview stack
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	const float world_f[16] = {
		world_transform.basis.rows[0][0], world_transform.basis.rows[1][0], world_transform.basis.rows[2][0], 0.0f,
		world_transform.basis.rows[0][1], world_transform.basis.rows[1][1], world_transform.basis.rows[2][1], 0.0f,
		world_transform.basis.rows[0][2], world_transform.basis.rows[1][2], world_transform.basis.rows[2][2], 0.0f,
		world_transform.origin.x, world_transform.origin.y, world_transform.origin.z, 1.0f
	};
	glMultMatrixf(world_f);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glMultMatrixf MODELVIEW");

	mesh_storage->mesh_surface_bind_arrays_gles1(p_surface->surface, shader->vertex_input_mask);

	GLuint index_array_gl = mesh_storage->mesh_surface_get_index_buffer(p_surface->surface, p_surface->lod_index);
	bool use_index_buffer = index_array_gl != 0;
	if (use_index_buffer) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_array_gl);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glBindBuffer GL_ELEMENT_ARRAY_BUFFER");
	} else if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	GLenum primitive_gl = prim[int(p_surface->primitive)];
	int drawn_count = mesh_storage->mesh_surface_get_vertices_drawn_count(p_surface->surface);

	// Draw
	if (drawn_count > 0) {
		if (use_index_buffer) {
			glDrawElements(primitive_gl, drawn_count, mesh_storage->mesh_surface_get_index_type(p_surface->surface), nullptr);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glDrawElements");
		} else {
			glDrawArrays(primitive_gl, 0, drawn_count);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glDrawArrays");
		}
	}

	// Unbind state
	mesh_storage->mesh_surface_unbind_arrays_gles1(p_surface->surface);
	if (use_index_buffer && GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	// Restore state
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glPopMatrix modelview");

	if (p_surface->material && p_surface->material->use_distance_fade) {
		if (scene_state.ubo.fog_enabled) {
			glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_EXP));
			glFogf(GL_FOG_DENSITY, scene_state.ubo.fog_density);
			GLfloat fog_col[4] = { scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2], 1.0f };
			glFogfv(GL_FOG_COLOR, fog_col);
		} else {
			glDisable(GL_FOG);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_single_item_immediate: glFog teardown");
	}
}

void RasterizerSceneGLES1::_bind_scene_camera_uniforms(RID p_version, SceneShaderGLES1::ShaderVariant p_variant, uint64_t p_spec_constants) {
	GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();

	Projection proj;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			proj.columns[i][j] = scene_state.ubo.projection_matrix[i * 4 + j];
		}
	}

	Projection inv_proj;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			inv_proj.columns[i][j] = scene_state.ubo.inv_projection_matrix[i * 4 + j];
		}
	}

	Transform3D view;
	view.basis.rows[0][0] = scene_state.ubo.view_matrix[0];
	view.basis.rows[1][0] = scene_state.ubo.view_matrix[1];
	view.basis.rows[2][0] = scene_state.ubo.view_matrix[2];
	view.basis.rows[0][1] = scene_state.ubo.view_matrix[4];
	view.basis.rows[1][1] = scene_state.ubo.view_matrix[5];
	view.basis.rows[2][1] = scene_state.ubo.view_matrix[6];
	view.basis.rows[0][2] = scene_state.ubo.view_matrix[8];
	view.basis.rows[1][2] = scene_state.ubo.view_matrix[9];
	view.basis.rows[2][2] = scene_state.ubo.view_matrix[10];
	view.origin.x = scene_state.ubo.view_matrix[12];
	view.origin.y = scene_state.ubo.view_matrix[13];
	view.origin.z = scene_state.ubo.view_matrix[14];

	Transform3D inv_view;
	inv_view.basis.rows[0][0] = scene_state.ubo.inv_view_matrix[0];
	inv_view.basis.rows[1][0] = scene_state.ubo.inv_view_matrix[1];
	inv_view.basis.rows[2][0] = scene_state.ubo.inv_view_matrix[2];
	inv_view.basis.rows[0][1] = scene_state.ubo.inv_view_matrix[4];
	inv_view.basis.rows[1][1] = scene_state.ubo.inv_view_matrix[5];
	inv_view.basis.rows[2][1] = scene_state.ubo.inv_view_matrix[6];
	inv_view.basis.rows[0][2] = scene_state.ubo.inv_view_matrix[8];
	inv_view.basis.rows[1][2] = scene_state.ubo.inv_view_matrix[9];
	inv_view.basis.rows[2][2] = scene_state.ubo.inv_view_matrix[10];
	inv_view.origin.x = scene_state.ubo.inv_view_matrix[12];
	inv_view.origin.y = scene_state.ubo.inv_view_matrix[13];
	inv_view.origin.z = scene_state.ubo.inv_view_matrix[14];

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::PROJECTION_MATRIX, proj, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::INV_PROJECTION_MATRIX, inv_proj, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::VIEW_MATRIX, view, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::INV_VIEW_MATRIX, inv_view, p_version, p_variant, p_spec_constants);

	Vector2 vp_size(scene_state.ubo.viewport_size[0], scene_state.ubo.viewport_size[1]);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::VIEWPORT_SIZE, vp_size, p_version, p_variant, p_spec_constants);

	Vector2 screen_pixel_size(scene_state.ubo.screen_pixel_size[0], scene_state.ubo.screen_pixel_size[1]);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::SCREEN_PIXEL_SIZE, screen_pixel_size, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::TIME, (float)scene_state.ubo.time, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::AMBIENT_LIGHT_COLOR_ENERGY, Color(scene_state.ubo.ambient_light_color_energy[0], scene_state.ubo.ambient_light_color_energy[1], scene_state.ubo.ambient_light_color_energy[2], scene_state.ubo.ambient_light_color_energy[3]), p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::AMBIENT_COLOR_SKY_MIX, scene_state.ubo.ambient_color_sky_mix, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::USE_AMBIENT_LIGHT, (bool)scene_state.ubo.use_ambient_light, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::USE_AMBIENT_CUBEMAP, (bool)scene_state.ubo.use_ambient_cubemap, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::USE_REFLECTION_CUBEMAP, (bool)scene_state.ubo.use_reflection_cubemap, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::DIRECTIONAL_LIGHT_COUNT, (int)scene_state.ubo.directional_light_count, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::Z_FAR, scene_state.ubo.z_far, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::Z_NEAR, scene_state.ubo.z_near, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_ENABLED, (bool)scene_state.ubo.fog_enabled, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_DENSITY, scene_state.ubo.fog_density, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_HEIGHT, scene_state.ubo.fog_height, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_HEIGHT_DENSITY, scene_state.ubo.fog_height_density, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_LIGHT_COLOR, Vector3(scene_state.ubo.fog_light_color[0], scene_state.ubo.fog_light_color[1], scene_state.ubo.fog_light_color[2]), p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_SUN_SCATTER, scene_state.ubo.fog_sun_scatter, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::FOG_AERIAL_PERSPECTIVE, scene_state.ubo.fog_aerial_perspective, p_version, p_variant, p_spec_constants);

	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::EXPOSURE, scene_state.tonemap_ubo.exposure, p_version, p_variant, p_spec_constants);
	material_storage->shaders.scene_shader.version_set_uniform(SceneShaderGLES1::WHITE, scene_state.tonemap_ubo.white, p_version, p_variant, p_spec_constants);
}

void RasterizerSceneGLES1::_fill_render_list(RenderListType p_render_list, const RenderDataGLES1 *p_render_data, PassMode p_pass_mode, bool p_append) {
	if (p_render_list == RENDER_LIST_OPAQUE) {
		scene_state.used_screen_texture = false;
		scene_state.used_normal_texture = false;
		scene_state.used_depth_texture = false;
#ifdef TOOLS_ENABLED
		render_list[RENDER_LIST_GIZMOS].clear();
		render_list[RENDER_LIST_EDITOR_GRID].clear();
#endif
	}

	Plane near_plane;
	if (p_render_data->cam_orthogonal) {
		near_plane = Plane(-p_render_data->cam_transform.basis.get_column(Vector3::AXIS_Z), p_render_data->cam_transform.origin);
		near_plane.d += p_render_data->cam_projection.get_z_near();
	}
	float z_max = p_render_data->cam_projection.get_z_far() - p_render_data->cam_projection.get_z_near();

	RenderList *rl = &render_list[p_render_list];
	_update_dirty_geometry_instances();

	if (!p_append) {
		rl->clear();
		if (p_render_list == RENDER_LIST_OPAQUE) {
			render_list[RENDER_LIST_ALPHA].clear();
		}
	}

	for (int i = 0; i < (int)p_render_data->instances->size(); i++) {
		GeometryInstanceGLES1 *inst = static_cast<GeometryInstanceGLES1 *>((*p_render_data->instances)[i]);

		Vector3 center = inst->transform.origin;
		if (p_render_data->cam_orthogonal) {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.get_support(-near_plane.normal);
			}
			inst->depth = near_plane.distance_to(center) - inst->sorting_offset;
		} else {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.position + (inst->transformed_aabb.size * 0.5);
			}
			inst->depth = p_render_data->cam_transform.origin.distance_to(center) - inst->sorting_offset;
		}

		uint32_t depth_layer = CLAMP(int(inst->depth * 16 / z_max), 0, 15);
		GeometryInstanceSurface *surf = inst->surface_caches;

#ifdef TOOLS_ENABLED
		// Intercept gizmos and bypass the standard batching queues.
		// (Node3DEditorViewport::GIZMO_BASE_LAYER == 27).
		if (inst->layer_mask & ((1 << 27))) {
			while (surf) {
				if (p_pass_mode == PASS_MODE_COLOR) {
					render_list[RENDER_LIST_GIZMOS].add_element(surf);
				}
				surf = surf->next;
			}
			continue; // Discard from normal batching queues
		}

		// Intercept editor grid and origin lines.
		// (Node3DEditorViewport::GIZMO_GRID_LAYER == 25).
		if (inst->layer_mask & (1 << 25)) {
			while (surf) {
				if (p_pass_mode == PASS_MODE_COLOR) {
					render_list[RENDER_LIST_EDITOR_GRID].add_element(surf);
				}
				surf = surf->next;
			}
			continue; // Discard from normal batching queues
		}
#endif

		while (surf) {
			surf->lod_index = 0; // TODO(GLES1): Simple stub for LOD

			if (p_pass_mode == PASS_MODE_COLOR) {
				if (surf->flags & GeometryInstanceSurface::FLAG_PASS_OPAQUE) {
					rl->add_element(surf);
				}
				if (surf->flags & GeometryInstanceSurface::FLAG_PASS_ALPHA) {
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
			}

			surf->sort.depth_layer = depth_layer;
			surf = surf->next;
		}
	}
}

// Needs to be called after _setup_lights so that directional_light_count is accurate.
void RasterizerSceneGLES1::_setup_environment(const RenderDataGLES1 *p_render_data, bool p_no_fog, const Size2i &p_screen_size, bool p_flip_y, const Color &p_default_bg_color, bool p_pancake_shadows, float p_shadow_bias) {
	Projection correction;
	correction.columns[1][1] = p_flip_y ? -1.0 : 1.0;
	Projection projection = correction * p_render_data->cam_projection;

	GLES1::MaterialStorage::store_camera(projection, scene_state.ubo.projection_matrix);
	GLES1::MaterialStorage::store_camera(projection.inverse(), scene_state.ubo.inv_projection_matrix);
	GLES1::MaterialStorage::store_transform(p_render_data->cam_transform, scene_state.ubo.inv_view_matrix);
	GLES1::MaterialStorage::store_transform(p_render_data->inv_cam_transform, scene_state.ubo.view_matrix);
	scene_state.ubo.camera_visible_layers = p_render_data->camera_visible_layers;

	scene_state.ubo.z_far = p_render_data->z_far;
	scene_state.ubo.z_near = p_render_data->z_near;

	scene_state.ubo.viewport_size[0] = p_screen_size.x;
	scene_state.ubo.viewport_size[1] = p_screen_size.y;

	Size2 screen_pixel_size = Vector2(1.0, 1.0) / Size2(p_screen_size);
	scene_state.ubo.screen_pixel_size[0] = screen_pixel_size.x;
	scene_state.ubo.screen_pixel_size[1] = screen_pixel_size.y;

	scene_state.ubo.time = time;

	scene_state.tonemap_ubo.exposure = 1.0;
	scene_state.tonemap_ubo.white = 1.0;

	if (p_render_data->camera_attributes.is_valid()) {
		scene_state.tonemap_ubo.exposure *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
	}

	if (is_environment(p_render_data->environment)) {
		RS::EnvironmentBG env_bg = environment_get_background(p_render_data->environment);
		RS::EnvironmentAmbientSource ambient_src = environment_get_ambient_source(p_render_data->environment);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(p_render_data->environment);

		scene_state.ubo.ambient_light_color_energy[3] = bg_energy_multiplier;
		scene_state.ubo.ambient_color_sky_mix = environment_get_ambient_sky_contribution(p_render_data->environment);
		scene_state.ubo.fog_enabled = environment_get_fog_enabled(p_render_data->environment);
		scene_state.ubo.fog_density = environment_get_fog_density(p_render_data->environment);
		Color fog_color = environment_get_fog_light_color(p_render_data->environment).srgb_to_linear();
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
		} else {
			float energy = environment_get_ambient_light_energy(p_render_data->environment);
			Color color = environment_get_ambient_light(p_render_data->environment);
			color = color.srgb_to_linear();
			scene_state.ubo.ambient_light_color_energy[0] = color.r * energy;
			scene_state.ubo.ambient_light_color_energy[1] = color.g * energy;
			scene_state.ubo.ambient_light_color_energy[2] = color.b * energy;
			scene_state.ubo.use_ambient_light = (ambient_src == RS::ENV_AMBIENT_SOURCE_COLOR) ? 1 : 0;
		}
	} else {
		scene_state.ubo.use_ambient_light = 0;
		scene_state.ubo.ambient_light_color_energy[0] = p_default_bg_color.r;
		scene_state.ubo.ambient_light_color_energy[1] = p_default_bg_color.g;
		scene_state.ubo.ambient_light_color_energy[2] = p_default_bg_color.b;
		scene_state.ubo.ambient_light_color_energy[3] = 1.0f;
		scene_state.ubo.fog_enabled = false;

		scene_state.ubo.fog_light_color[0] = p_default_bg_color.r;
		scene_state.ubo.fog_light_color[1] = p_default_bg_color.g;
		scene_state.ubo.fog_light_color[2] = p_default_bg_color.b;
	}
}

// Puts lights into Uniform Buffers. Needs to be called before _fill_list as this caches the index of each light in the Uniform Buffer
void RasterizerSceneGLES1::_setup_lights(const RenderDataGLES1 *p_render_data, bool p_using_shadows, uint32_t &r_directional_light_count, uint32_t &r_omni_light_count, uint32_t &r_spot_light_count, uint32_t &r_directional_shadow_count) {

}

// Render shadows
void RasterizerSceneGLES1::_render_shadows(const RenderDataGLES1 *p_render_data, const Size2i &p_viewport_size) {

}

void RasterizerSceneGLES1::_render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, RenderingMethod::RenderInfo *p_render_info, const Size2i &p_viewport_size, const Transform3D &p_main_cam_transform) {

}

void RasterizerSceneGLES1::render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, const RenderSDFGIUpdateData *p_sdfgi_update_data, RenderingMethod::RenderInfo *r_render_info) {
	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();
	RENDER_TIMESTAMP("Setup 3D Scene");

	bool is_reflection_probe = p_reflection_probe.is_valid();

	Ref<RenderSceneBuffersGLES1> rb = p_render_buffers;
	ERR_FAIL_COND(rb.is_null());

	GLES1::RenderTarget *rt = nullptr;
	if (!is_reflection_probe) {
		rt = texture_storage->get_render_target(rb->render_target);
		ERR_FAIL_NULL(rt);
	}

	RenderDataGLES1 render_data;
	{
		render_data.render_buffers = rb;
		if (rt) {
			render_data.transparent_bg = rt->is_transparent;
		}
		render_data.cam_transform = p_camera_data->main_transform;
		render_data.inv_cam_transform = render_data.cam_transform.affine_inverse();
		render_data.cam_projection = p_camera_data->main_projection;
		render_data.cam_orthogonal = p_camera_data->is_orthogonal;
		render_data.camera_visible_layers = p_camera_data->visible_layers;

		render_data.instances = &p_instances;
		render_data.lights = &p_lights;
		render_data.environment = p_environment;
		render_data.camera_attributes = p_camera_attributes;
		render_data.reflection_probe = p_reflection_probe;
		render_data.reflection_probe_pass = p_reflection_probe_pass;
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
	bool draw_sky = false;
	bool draw_sky_fog_only = false;
	bool keep_color = false;
	float sky_energy_multiplier = 1.0;

	bool flip_y = true;
	if (flip_y) {
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
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
				if (environment_get_fog_enabled(render_data.environment)) {
					draw_sky_fog_only = true;
					GLES1::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));
				}
			} break;
			case RS::ENV_BG_COLOR: {
				clear_color = environment_get_bg_color(render_data.environment);
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
				if (environment_get_fog_enabled(render_data.environment)) {
					draw_sky_fog_only = true;
					GLES1::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));
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
				correction.columns[1][1] = -1.0;
				projection = correction * render_data.cam_projection;
			} else if (flip_y) {
				Projection correction;
				correction.set_depth_correction(true, false, false);
				projection = correction * projection;
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
		GLES1::MaterialStorage::get_singleton()->material_set_param(sky_globals.fog_material, "clear_color", Variant(clear_color));

		Projection sky_proj = render_data.cam_projection;
		if (flip_y) {
			Projection correction;
			correction.set_depth_correction(true, false, false);
			sky_proj = correction * sky_proj;
		}
		_setup_sky(&render_data, *render_data.lights, sky_proj, render_data.cam_transform, screen_size);
	}

	_setup_environment(&render_data, false, screen_size, flip_y, clear_color, false);

	_fill_render_list(RENDER_LIST_OPAQUE, &render_data, PASS_MODE_COLOR);
	render_list[RENDER_LIST_OPAQUE].sort_by_key();
	render_list[RENDER_LIST_ALPHA].sort_by_reverse_depth_and_priority();

	scene_state.reset_gl_state();

	if (rb.is_valid()) {
		GLES1::TextureStorage::get_singleton()->bind_framebuffer(rb->get_render_fbo());
		glViewport(0, 0, screen_size.width, screen_size.height);
	}

	scene_state.enable_gl_depth_draw(true);

	if (!keep_color) {
		glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
		// Reverse-Z, far clipping plane is 0.0
		RasterizerGLES1::clear_depth(0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::render_scene: glClear");
	} else {
		RasterizerGLES1::clear_depth(0.0f);
		glClear(GL_DEPTH_BUFFER_BIT);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::render_scene: glClear");
	}

	if (rb.is_valid()) {
		GLES1::TextureStorage::get_singleton()->render_target_disable_clear_request(rb->render_target);
	}

	scene_state.enable_gl_depth_test(true);
	scene_state.enable_gl_depth_draw(true);

	// Near (1.0) is greater than far (0.0)
	glDepthFunc(GL_GEQUAL);

	uint64_t spec_constant_base_flags = 0;

	// Opaque Pass
	scene_state.enable_gl_blend(false);
	RenderListParameters render_list_params(render_list[RENDER_LIST_OPAQUE].elements.ptr(), render_list[RENDER_LIST_OPAQUE].elements.size(), reverse_cull, spec_constant_base_flags, use_wireframe);
	_render_list_template<PASS_MODE_COLOR>(&render_list_params, &render_data, 0, render_list[RENDER_LIST_OPAQUE].elements.size());

	scene_state.enable_gl_depth_draw(false);

	// Sky background pass
	if (draw_sky) {
		scene_state.enable_gl_depth_test(true);
		scene_state.enable_gl_blend(false);
		scene_state.set_gl_cull_mode(RS::CULL_MODE_BACK);
		_draw_sky(render_data.environment, render_data.cam_projection, render_data.cam_transform, sky_energy_multiplier, 1.0, false, flip_y, false);
	}

	// Transparent Pass
	scene_state.enable_gl_blend(true);
	if (GLES1::Config::get_singleton()->support_blend_subtract) {
		glBlendEquationOES(GL_FUNC_ADD_OES);
	}
	
	if (GLES1::Config::get_singleton()->support_blend_func_separate) {
		if (render_data.transparent_bg) {
			glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		} else {
			glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		}
	} else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	RenderListParameters render_list_params_alpha(render_list[RENDER_LIST_ALPHA].elements.ptr(), render_list[RENDER_LIST_ALPHA].elements.size(), reverse_cull, spec_constant_base_flags, use_wireframe);
	_render_list_template<PASS_MODE_COLOR_TRANSPARENT>(&render_list_params_alpha, &render_data, 0, render_list[RENDER_LIST_ALPHA].elements.size(), true);

#ifdef TOOLS_ENABLED
	_draw_editor_lines(&render_data);
	_draw_editor_grid(&render_data);
	_draw_editor_gizmos(&render_data);
#endif

	// Rescue the 3D scene from the internal buffer if it was used.
	_render_post_processing(&render_data);

	// Clean up GL state
	scene_state.reset_gl_state();
	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	GLES1::TextureStorage::get_singleton()->bind_framebuffer_system();
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::render_scene: reset_gl_state cleanup");
}

#ifdef TOOLS_ENABLED
void RasterizerSceneGLES1::_draw_editor_gizmos(const RenderDataGLES1 *p_render_data) {
	if (render_list[RENDER_LIST_GIZMOS].elements.is_empty()) {
		return;
	}

	scene_state.reset_gl_state();

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	Projection projection = p_render_data->cam_projection;
	Projection correction;
	correction.columns[1][1] = -1.0f; // flip_y
	projection = correction * projection;

	const float proj_m[16] = {
		projection.columns[0][0], projection.columns[0][1], projection.columns[0][2], projection.columns[0][3],
		projection.columns[1][0], projection.columns[1][1], projection.columns[1][2], projection.columns[1][3],
		projection.columns[2][0], projection.columns[2][1], projection.columns[2][2], projection.columns[2][3],
		projection.columns[3][0], projection.columns[3][1], projection.columns[3][2], projection.columns[3][3]
	};
	glLoadMatrixf(proj_m);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	Transform3D view = p_render_data->inv_cam_transform;
	const float view_m[16] = {
		view.basis.rows[0][0], view.basis.rows[1][0], view.basis.rows[2][0], 0.0f,
		view.basis.rows[0][1], view.basis.rows[1][1], view.basis.rows[2][1], 0.0f,
		view.basis.rows[0][2], view.basis.rows[1][2], view.basis.rows[2][2], 0.0f,
		view.origin.x, view.origin.y, view.origin.z, 1.0f
	};
	glLoadMatrixf(view_m);

	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_FOG);
	glClear(GL_DEPTH_BUFFER_BIT);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	for (uint32_t i = 0; i < render_list[RENDER_LIST_GIZMOS].elements.size(); i++) {
		GeometryInstanceSurface *surf = render_list[RENDER_LIST_GIZMOS].elements[i];
		if (surf->vertex_cache.is_empty()) {
			continue;
		}

		// Correctly cull
		if (surf->shader) {
			if (surf->shader->cull_mode == RS::CULL_MODE_DISABLED) {
				glDisable(GL_CULL_FACE);
			} else {
				glEnable(GL_CULL_FACE);
				glCullFace(surf->shader->cull_mode == RS::CULL_MODE_FRONT ? GL_FRONT : GL_BACK);
			}

			if (surf->shader->depth_test == GLES1::SceneShaderData::DEPTH_TEST_DISABLED) {
				glDisable(GL_DEPTH_TEST);
			} else {
				glEnable(GL_DEPTH_TEST);
			}

			if (surf->shader->depth_draw == GLES1::SceneShaderData::DEPTH_DRAW_DISABLED || (surf->flags & GeometryInstanceSurface::FLAG_PASS_ALPHA)) {
				glDepthMask(GL_FALSE);
			} else {
				glDepthMask(GL_TRUE);
			}
		} else {
			glDisable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_gizmos: surface state setup");

		bool is_multimesh = surf->owner->data->base_type == RS::INSTANCE_MULTIMESH;
		uint32_t instances = is_multimesh ? surf->owner->instance_count : 1;
		RID base_rid = surf->owner->data->base;

		Color base_albedo = Color(1.0f, 1.0f, 1.0f, 1.0f);
		RID mat_rid = RID();

		if (surf->owner->data->surface_materials.size() > surf->surface_index) {
			mat_rid = surf->owner->data->surface_materials[surf->surface_index];
		}

		// Fallback
		if (!mat_rid.is_valid()) {
			mat_rid = GLES1::MeshStorage::get_singleton()->mesh_surface_get_material(surf->owner->data->base, surf->surface_index);
		}

		if (mat_rid.is_valid()) {
			Variant albedo_var = GLES1::MaterialStorage::get_singleton()->material_get_param(mat_rid, "albedo");
			if (albedo_var.get_type() == Variant::COLOR) {
				base_albedo = albedo_var;
			}

			if (surf->primitive == RS::PRIMITIVE_POINTS) {
				Variant pt_size = GLES1::MaterialStorage::get_singleton()->material_get_param(mat_rid, "point_size");
				if (pt_size.get_type() == Variant::FLOAT) {
					glPointSize(pt_size);
				} else {
					glPointSize(1.0f);
				}
			}
		}

		GLenum primitive_gl = prim[int(surf->primitive)];

		if (primitive_gl == GL_LINES || primitive_gl == GL_LINE_STRIP) {
			glLineWidth(3.0f);
		} else {
			glLineWidth(1.0f);
		}

		// The rotate gizmo has precisely 384 vertices (128 segments * 3 thickness layers)
		bool is_rotate_gizmo = (
			surf->vertex_cache.size() == 384 &&
			surf->normal_cache.size() == 384 &&
			surf->primitive == RS::PRIMITIVE_TRIANGLES
		);
		bool is_border = is_rotate_gizmo && (
			Math::is_equal_approx(base_albedo.r, 0.75f) &&
			Math::is_equal_approx(base_albedo.g, 0.75f) &&
			Math::is_equal_approx(base_albedo.b, 0.75f)
		);

		// Cache standard gizmo vertices if not already cached
		if (!is_rotate_gizmo && !surf->gizmo_cached) {
			int v_count = surf->vertex_cache.size();
			surf->gizmo_vertex_array = memnew_arr(float, v_count * 3);
			for (int v = 0; v < v_count; v++) {
				Vector3 vertex = surf->vertex_cache[v];
				if (vertex.length_squared() > 16777216.0f) {
					vertex = vertex.normalized() * 4096.0f;
				}
				surf->gizmo_vertex_array[v * 3 + 0] = vertex.x;
				surf->gizmo_vertex_array[v * 3 + 1] = vertex.y;
				surf->gizmo_vertex_array[v * 3 + 2] = vertex.z;
			}

			if (!surf->index_cache.is_empty()) {
				surf->gizmo_index_array = memnew_arr(uint16_t, surf->index_cache.size());
				for (uint32_t ii = 0; ii < surf->index_cache.size(); ii++) {
					surf->gizmo_index_array[ii] = static_cast<uint16_t>(surf->index_cache[ii]);
				}
			}

			if (GLES1::Config::get_singleton()->support_vbo) {
				glGenBuffers(1, &surf->gizmo_vertex_buffer);
				glBindBuffer(GL_ARRAY_BUFFER, surf->gizmo_vertex_buffer);
				glBufferData(GL_ARRAY_BUFFER, v_count * 3 * sizeof(float), surf->gizmo_vertex_array, GL_STATIC_DRAW);

				if (!surf->color_cache.is_empty()) {
					glGenBuffers(1, &surf->gizmo_color_buffer);
					glBindBuffer(GL_ARRAY_BUFFER, surf->gizmo_color_buffer);
					glBufferData(GL_ARRAY_BUFFER, surf->color_cache.size() * sizeof(Color), surf->color_cache.ptr(), GL_STATIC_DRAW);
				}

				if (surf->gizmo_index_array) {
					glGenBuffers(1, &surf->gizmo_index_buffer);
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surf->gizmo_index_buffer);
					glBufferData(GL_ELEMENT_ARRAY_BUFFER, surf->index_cache.size() * sizeof(uint16_t), surf->gizmo_index_array, GL_STATIC_DRAW);
				}
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_gizmos: gizmo VBO gen");
			}

			surf->gizmo_cached = true;
		}

		for (uint32_t inst = 0; inst < instances; inst++) {
			glPushMatrix();

			Transform3D xform;
			Color final_color = base_albedo;

			if (is_multimesh) {
				xform = GLES1::MeshStorage::get_singleton()->multimesh_instance_get_transform(base_rid, inst);
				final_color *= GLES1::MeshStorage::get_singleton()->multimesh_instance_get_color(base_rid, inst);
			}

			Transform3D final_xform = surf->owner->transform * xform;
			const float world_f[16] = {
				final_xform.basis.rows[0][0], final_xform.basis.rows[1][0], final_xform.basis.rows[2][0], 0.0f,
				final_xform.basis.rows[0][1], final_xform.basis.rows[1][1], final_xform.basis.rows[2][1], 0.0f,
				final_xform.basis.rows[0][2], final_xform.basis.rows[1][2], final_xform.basis.rows[2][2], 0.0f,
				final_xform.origin.x, final_xform.origin.y, final_xform.origin.z, 1.0f
			};
			glMultMatrixf(world_f);

			// Rotation rings
			if (is_rotate_gizmo) {
				float radius = surf->vertex_cache[0].length();

				// View direction in gizmo's local space
				Vector3 local_view_dir = final_xform.basis.inverse().xform(
					p_render_data->cam_transform.basis.get_column(2)
				).normalized();

				glColor4f(final_color.r, final_color.g, final_color.b, final_color.a);
				glDisableClientState(GL_COLOR_ARRAY);

				if (is_border) {
					// White outline: A full 360-degree circle rotated
					// to billboard the camera

					Vector3 cam_dir = local_view_dir;
					Vector3 cam_up = final_xform.basis.inverse().xform(p_render_data->cam_transform.basis.get_column(1)).normalized();
					Vector3 cam_right = cam_up.cross(cam_dir).normalized();
					cam_up = cam_dir.cross(cam_right).normalized();

					Basis rot_mat;
					rot_mat.set_column(0, cam_right);
					rot_mat.set_column(1, cam_up);
					rot_mat.set_column(2, cam_dir);

					Vector3 temp_verts[(BORDER_SEGMENTS + 1) * 2];

					for (int k = 0; k <= BORDER_SEGMENTS; k++) {
						Vector3 P0(rotate_gizmo_border_verts[k * 3 + 0], rotate_gizmo_border_verts[k * 3 + 1], rotate_gizmo_border_verts[k * 3 + 2]);

						Vector3 P = P0 * radius;
						Vector3 v1 = P + P0 * (B_WIDTH * radius);
						Vector3 v2 = P - P0 * (B_WIDTH * radius);

						temp_verts[k * 2 + 0] = rot_mat.xform(v1);
						temp_verts[k * 2 + 1] = rot_mat.xform(v2);
					}

					if (GLES1::Config::get_singleton()->support_vbo) {
						glBindBuffer(GL_ARRAY_BUFFER, 0);
					}
					glVertexPointer(3, GL_FLOAT, 0, temp_verts);
					glDrawArrays(GL_TRIANGLE_STRIP, 0, (BORDER_SEGMENTS + 1) * 2);
				} else {
					// Coloured rings:
					// We create a dynamic 198-degree half-circle that follows the camera

					// Project the view direction down onto the gizmo's local 2D plane (Z=0)
					Vector2 front_2d = Vector2(local_view_dir.x, local_view_dir.y);
					if (front_2d.length_squared() < 0.0001f) {
						front_2d = Vector2(1, 0);
					}
					front_2d.normalize();
					float base_angle = front_2d.angle();

					// Transform the local view into the ring's
					// unrotated frame to calculate twist.
					Vector3 view0 = Basis(Vector3(0, 0, 1), -base_angle).xform(local_view_dir);

					Vector3 temp_verts[(BORDER_SEGMENTS + 1) * 2];

					for (int k = 0; k <= BORDER_SEGMENTS; k++) {
						Vector3 P0(rotate_gizmo_ring_verts[k * 6 + 0], rotate_gizmo_ring_verts[k * 6 + 1], rotate_gizmo_ring_verts[k * 6 + 2]);
						Vector3 T0(rotate_gizmo_ring_verts[k * 6 + 3], rotate_gizmo_ring_verts[k * 6 + 4], rotate_gizmo_ring_verts[k * 6 + 5]);

						Vector3 P = P0 * radius;
						Vector3 W0 = T0.cross(view0).normalized() * (T_WIDTH * radius);

						temp_verts[k * 2 + 0] = P + W0;
						temp_verts[k * 2 + 1] = P - W0;
					}

					glRotatef(Math::rad_to_deg(base_angle), 0.0f, 0.0f, 1.0f);
					if (GLES1::Config::get_singleton()->support_vbo) {
						glBindBuffer(GL_ARRAY_BUFFER, 0);
					}
					glVertexPointer(3, GL_FLOAT, 0, temp_verts);
					glDrawArrays(GL_TRIANGLE_STRIP, 0, (BORDER_SEGMENTS + 1) * 2);
				}
			} else {
				// Standard drawing
				if (GLES1::Config::get_singleton()->support_vbo && surf->gizmo_vertex_buffer != 0) {
					glBindBuffer(GL_ARRAY_BUFFER, surf->gizmo_vertex_buffer);
					glVertexPointer(3, GL_FLOAT, 0, nullptr);

					if (surf->gizmo_color_buffer != 0) {
						glEnableClientState(GL_COLOR_ARRAY);
						glBindBuffer(GL_ARRAY_BUFFER, surf->gizmo_color_buffer);
						glColorPointer(4, GL_FLOAT, 0, nullptr);
					} else {
						glDisableClientState(GL_COLOR_ARRAY);
						glColor4f(final_color.r, final_color.g, final_color.b, final_color.a);
					}

					if (surf->gizmo_index_buffer != 0) {
						glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surf->gizmo_index_buffer);
						glDrawElements(primitive_gl, surf->index_cache.size(), GL_UNSIGNED_SHORT, nullptr);
					} else {
						glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						glDrawArrays(primitive_gl, 0, surf->vertex_cache.size());
					}
				} else {
					if (GLES1::Config::get_singleton()->support_vbo) {
						glBindBuffer(GL_ARRAY_BUFFER, 0);
					}
					glVertexPointer(3, GL_FLOAT, 0, surf->gizmo_vertex_array);

					if (!surf->color_cache.is_empty()) {
						glEnableClientState(GL_COLOR_ARRAY);
						glColorPointer(4, GL_FLOAT, 0, surf->color_cache.ptr());
					} else {
						glDisableClientState(GL_COLOR_ARRAY);
						glColor4f(final_color.r, final_color.g, final_color.b, final_color.a);
					}

					if (surf->gizmo_index_array) {
						if (GLES1::Config::get_singleton()->support_vbo) {
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						}
						glDrawElements(primitive_gl, surf->index_cache.size(), GL_UNSIGNED_SHORT, surf->gizmo_index_array);
					} else {
						if (GLES1::Config::get_singleton()->support_vbo) {
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
						}
						glDrawArrays(primitive_gl, 0, surf->vertex_cache.size());
					}
				}
			}

			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_gizmos: glDraw elements dispatch");
			glPopMatrix();
		}
	}

	// Clean up
	glLineWidth(1.0f);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_gizmos: cleanup");
}

void RasterizerSceneGLES1::_draw_editor_lines(const RenderDataGLES1 *p_render_data) {
	// Restrict to viewports requesting the gizmo grid layer
	// (Node3DEditorViewport::GIZMO_BASE_LAYER == 27).
	if (!(p_render_data->camera_visible_layers & (1 << 27))) {
		return;
	}

	scene_state.reset_gl_state();

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	Projection projection = p_render_data->cam_projection;
	Projection correction;
	correction.columns[1][1] = -1.0f; // flip_y
	projection = correction * projection;

	const float proj_m[16] = {
		projection.columns[0][0], projection.columns[0][1], projection.columns[0][2], projection.columns[0][3],
		projection.columns[1][0], projection.columns[1][1], projection.columns[1][2], projection.columns[1][3],
		projection.columns[2][0], projection.columns[2][1], projection.columns[2][2], projection.columns[2][3],
		projection.columns[3][0], projection.columns[3][1], projection.columns[3][2], projection.columns[3][3]
	};
	glMultMatrixf(proj_m);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	Transform3D view = p_render_data->inv_cam_transform;
	const float view_m[16] = {
		view.basis.rows[0][0], view.basis.rows[1][0], view.basis.rows[2][0], 0.0f,
		view.basis.rows[0][1], view.basis.rows[1][1], view.basis.rows[2][1], 0.0f,
		view.basis.rows[0][2], view.basis.rows[1][2], view.basis.rows[2][2], 0.0f,
		view.origin.x, view.origin.y, view.origin.z, 1.0f
	};
	glMultMatrixf(view_m);

	float max_dist = p_render_data->cam_transform.origin.length() + p_render_data->z_far;

	// GLES1 doesn't like long distances (it starts
	// to jitter when drawing these long lines).
	// 2^12 stretches it far enough into "infinity"
	max_dist = MAX(max_dist, 4096.0f);

	glScalef(max_dist, max_dist, max_dist);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_lines: glScalef");

	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_FOG);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Width of the line.
	// 3.0 seems to be the magic number that draws it
	// similarly to the GLES2/GLES3 drivers.
	glLineWidth(3.0f);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_lines: setup");

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	if (GLES1::Config::get_singleton()->support_vbo && editor_lines_vbo != 0) {
		glBindBuffer(GL_ARRAY_BUFFER, editor_lines_vbo);
		glVertexPointer(3, GL_FLOAT, 0, nullptr);

		glBindBuffer(GL_ARRAY_BUFFER, editor_lines_color_vbo);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, nullptr);
	} else {
		if (GLES1::Config::get_singleton()->support_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glVertexPointer(3, GL_FLOAT, 0, line_verts);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, line_colors);
	}

	// Draw each of the axis lines individually.
	// This allows for customising each line
	// individually if needed.
	
	// X axis
	glDrawArrays(GL_LINES, 0, 4);

	// Y axis
	// Fade the alpha to 0 as the camera looks directly down the Y-axis.
	Vector3 view_dir = p_render_data->cam_transform.basis.get_column(2).normalized();
	constexpr float MAX_FADE = 0.5f;
	constexpr float FADE_RANGE = 0.15f;
	float y_dot = Math::abs(view_dir.y);
	float alpha_factor = 1.0f;

	if (y_dot > MAX_FADE) {
		alpha_factor = CLAMP(1.0f - ((y_dot - MAX_FADE) / FADE_RANGE), 0.0f, 1.0f);
	}

	glDisableClientState(GL_COLOR_ARRAY);
	glColor4f(line_colors[16] / 255.0f, line_colors[17] / 255.0f, line_colors[18] / 255.0f, alpha_factor);

	glPushMatrix();
	glDrawArrays(GL_LINES, 4, 4);
	glPopMatrix();

	glEnableClientState(GL_COLOR_ARRAY);

	// Z axis
	glDrawArrays(GL_LINES, 8, 4);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_lines: glDrawArrays");

	// Reset states
	glLineWidth(1.0f); // Default value

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_BLEND);

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_lines: cleanup");
}

void RasterizerSceneGLES1::_draw_editor_grid(const RenderDataGLES1 *p_render_data) {
	if (render_list[RENDER_LIST_EDITOR_GRID].elements.is_empty()) {
		return;
	}

	float grid_size = 100.0f;
	Color grid_color = Color(0.5f, 0.5f, 0.5f, 0.5f);
	bool found_grid = false;

	for (uint32_t i = 0; i < render_list[RENDER_LIST_EDITOR_GRID].elements.size(); i++) {
		GeometryInstanceSurface *surf = render_list[RENDER_LIST_EDITOR_GRID].elements[i];
		if (!surf->material) {
			continue;
		}

		RID mat_rid = RID();
		if (surf->owner->data->surface_materials.size() > surf->surface_index) {
			mat_rid = surf->owner->data->surface_materials[surf->surface_index];
		}
		if (!mat_rid.is_valid()) {
			mat_rid = GLES1::MeshStorage::get_singleton()->mesh_surface_get_material(surf->owner->data->base, surf->surface_index);
		}

		Variant gs = GLES1::MaterialStorage::get_singleton()->material_get_param(mat_rid, "grid_size");
		if (gs.get_type() == Variant::FLOAT) {
			grid_size = gs;
			found_grid = true;
			if (surf->color_cache.size() > 0) {
				grid_color = surf->color_cache[0];
			}
			break; // We only need the parameters, not to draw the actual mesh.
		}
	}

	if (!found_grid) {
		return;
	}

	Vector3 cam_pos = p_render_data->cam_transform.origin;
	Vector2 cam_plane_pos = Vector2(cam_pos.x, cam_pos.z);

	int min_x = Math::floor(cam_plane_pos.x - grid_size);
	int max_x = Math::ceil(cam_plane_pos.x + grid_size);
	int min_z = Math::floor(cam_plane_pos.y - grid_size);
	int max_z = Math::ceil(cam_plane_pos.y + grid_size);

	// Tessellate to bake distance fade.
	float step = MAX(1.0f, grid_size / 20.0f);

	// Calculate maximum possible vertices
	// For each axis, we calculate the total lines and segments per line.
	int num_lines_x = (max_x - min_x + 1);
	int num_segs_z = Math::ceil((max_z - min_z) / step) + 1; // +1 to ensure safety bounds

	int num_lines_z = (max_z - min_z + 1);
	int num_segs_x = Math::ceil((max_x - min_x) / step) + 1;

	// Each segment has 2 vertices
	// resulting in (lines * segments * 2) vertices per axis.
	int max_verts = (num_lines_x * num_segs_z * 2) + (num_lines_z * num_segs_x * 2);

	float *grid_verts = SAFE_ALLOCA_ARRAY(float, max_verts * 3);
	uint8_t *grid_colors = SAFE_ALLOCA_ARRAY(uint8_t, max_verts * 4);

	if (!grid_verts || !grid_colors) {
		return;
	}

	int v_idx = 0;
	int c_idx = 0;

	uint8_t r = static_cast<uint8_t>(CLAMP(grid_color.r * 255.0f, 0.0f, 255.0f));
	uint8_t g = static_cast<uint8_t>(CLAMP(grid_color.g * 255.0f, 0.0f, 255.0f));
	uint8_t b = static_cast<uint8_t>(CLAMP(grid_color.b * 255.0f, 0.0f, 255.0f));
	float base_a = grid_color.a * 255.0f;

	for (int x = min_x; x <= max_x; x++) {
		if (x == 0) {
			continue; // Skip origin line
		}

		for (float z = min_z; z < max_z; z += step) {
			float z1 = z;
			float z2 = MIN(z + step, max_z);

			// Calculate radial fade-out distance from the camera projection
			float d1 = Vector2(x, z1).distance_to(cam_plane_pos);
			float fade1 = 1.0f - (d1 / grid_size);
			fade1 = Math::smoothstep(0.02f, 0.3f, fade1);

			float d2 = Vector2(x, z2).distance_to(cam_plane_pos);
			float fade2 = 1.0f - (d2 / grid_size);
			fade2 = Math::smoothstep(0.02f, 0.3f, fade2);

			if (!p_render_data->cam_orthogonal) {
				// Apply fade
				Vector3 dir1 = (cam_pos - Vector3(x, 0, z1)).normalized();
				fade1 *= Math::smoothstep(0.05f, 0.2f, Math::abs(dir1.y));
				Vector3 dir2 = (cam_pos - Vector3(x, 0, z2)).normalized();
				fade2 *= Math::smoothstep(0.05f, 0.2f, Math::abs(dir2.y));
			}

			// Push only visible geometry to the stack buffers
			if (fade1 > 0.0f || fade2 > 0.0f) {
				grid_verts[v_idx++] = x;
				grid_verts[v_idx++] = 0;
				grid_verts[v_idx++] = z1;
				grid_verts[v_idx++] = x;
				grid_verts[v_idx++] = 0;
				grid_verts[v_idx++] = z2;

				grid_colors[c_idx++] = r;
				grid_colors[c_idx++] = g;
				grid_colors[c_idx++] = b;
				grid_colors[c_idx++] = static_cast<uint8_t>(CLAMP(base_a * fade1, 0.0f, 255.0f));
				grid_colors[c_idx++] = r;
				grid_colors[c_idx++] = g;
				grid_colors[c_idx++] = b;
				grid_colors[c_idx++] = static_cast<uint8_t>(CLAMP(base_a * fade2, 0.0f, 255.0f));
			}
		}
	}

	// Same loop for the far (Z) axis
	for (int z = min_z; z <= max_z; z++) {
		if (z == 0) {
			continue;
		}

		for (float x = min_x; x < max_x; x += step) {
			float x1 = x;
			float x2 = MIN(x + step, max_x);

			// Calculate radial fade-out distance from the camera projection
			float d1 = Vector2(x1, z).distance_to(cam_plane_pos);
			float fade1 = 1.0f - (d1 / grid_size);
			fade1 = Math::smoothstep(0.02f, 0.3f, fade1);

			float d2 = Vector2(x2, z).distance_to(cam_plane_pos);
			float fade2 = 1.0f - (d2 / grid_size);
			fade2 = Math::smoothstep(0.02f, 0.3f, fade2);

			if (!p_render_data->cam_orthogonal) {
				// Apply fade
				Vector3 dir1 = (cam_pos - Vector3(x1, 0, z)).normalized();
				fade1 *= Math::smoothstep(0.05f, 0.2f, Math::abs(dir1.y));
				Vector3 dir2 = (cam_pos - Vector3(x2, 0, z)).normalized();
				fade2 *= Math::smoothstep(0.05f, 0.2f, Math::abs(dir2.y));
			}

			// Only push visible geometry to the stack buffers
			if (fade1 > 0.0f || fade2 > 0.0f) {
				grid_verts[v_idx++] = x1;
				grid_verts[v_idx++] = 0;
				grid_verts[v_idx++] = z;
				grid_verts[v_idx++] = x2;
				grid_verts[v_idx++] = 0;
				grid_verts[v_idx++] = z;

				grid_colors[c_idx++] = r;
				grid_colors[c_idx++] = g;
				grid_colors[c_idx++] = b;
				grid_colors[c_idx++] = static_cast<uint8_t>(CLAMP(base_a * fade1, 0.0f, 255.0f));
				grid_colors[c_idx++] = r;
				grid_colors[c_idx++] = g;
				grid_colors[c_idx++] = b;
				grid_colors[c_idx++] = static_cast<uint8_t>(CLAMP(base_a * fade2, 0.0f, 255.0f));
			}
		}
	}

	scene_state.reset_gl_state();

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	Projection projection = p_render_data->cam_projection;
	Projection correction;
	correction.columns[1][1] = -1.0f; // flip_y
	projection = correction * projection;

	const float proj_m[16] = {
		projection.columns[0][0], projection.columns[0][1], projection.columns[0][2], projection.columns[0][3],
		projection.columns[1][0], projection.columns[1][1], projection.columns[1][2], projection.columns[1][3],
		projection.columns[2][0], projection.columns[2][1], projection.columns[2][2], projection.columns[2][3],
		projection.columns[3][0], projection.columns[3][1], projection.columns[3][2], projection.columns[3][3]
	};
	glLoadMatrixf(proj_m);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	Transform3D view = p_render_data->inv_cam_transform;
	const float view_m[16] = {
		view.basis.rows[0][0], view.basis.rows[1][0], view.basis.rows[2][0], 0.0f,
		view.basis.rows[0][1], view.basis.rows[1][1], view.basis.rows[2][1], 0.0f,
		view.basis.rows[0][2], view.basis.rows[1][2], view.basis.rows[2][2], 0.0f,
		view.origin.x, view.origin.y, view.origin.z, 1.0f
	};
	glLoadMatrixf(view_m);

	// Clean-up
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(1.0f);

	if (GLES1::Config::get_singleton()->support_vbo) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	if (v_idx > 0) {
		glVertexPointer(3, GL_FLOAT, 0, grid_verts);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, grid_colors);

		glDrawArrays(GL_LINES, 0, v_idx / 3);
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_BLEND);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_draw_editor_grid: cleanup");
}

#endif // TOOLS_ENABLED

void RasterizerSceneGLES1::_render_post_processing(const RenderDataGLES1 *p_render_data) {
	Ref<RenderSceneBuffersGLES1> rb = p_render_data->render_buffers;
	if (rb.is_null() || rb->internal3d.color == 0) {
		return; // We rendered directly to the RenderTarget; no blit required.
	}

	GLES1::TextureStorage *texture_storage = GLES1::TextureStorage::get_singleton();

	// Bind the final render target FBO
	GLuint rt_fbo = texture_storage->render_target_get_fbo(rb->render_target);
	texture_storage->bind_framebuffer(rt_fbo);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_post_processing: bind_framebuffer");

	// Set up pure blitting state
	glViewport(0, 0, rb->target_size.width, rb->target_size.height);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	if (GLES1::Config::get_singleton()->max_texture_units > 1) {
		glActiveTexture(GL_TEXTURE0);
	}
	glBindTexture(GL_TEXTURE_2D, rb->internal3d.color);

	if (rb->scaling_3d_mode == RS::VIEWPORT_SCALING_3D_MODE_BILINEAR) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	GLES1::CopyEffects::get_singleton()->copy_to_rect(Rect2(0, 0, 1, 1));

	glBindTexture(GL_TEXTURE_2D, 0);
	GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_render_post_processing: copy_to_rect");
}

template <RasterizerSceneGLES1::PassMode p_pass_mode>
void RasterizerSceneGLES1::_render_list_template(RenderListParameters *p_params, const RenderDataGLES1 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass) {
	if (p_from_element >= p_to_element) {
		return;
	}

	int count = p_to_element - p_from_element;
	GeometryInstanceSurface **surfaces = &p_params->elements[p_from_element];

	// Kick off the batched draw pipeline
	batch_scene_render_items(surfaces, count, p_render_data->cam_transform, p_alpha_pass);
}

void RasterizerSceneGLES1::render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) {
}

void RasterizerSceneGLES1::render_particle_collider_heightfield(RID p_collider, const Transform3D &p_transform, const PagedArray<RenderGeometryInstance *> &p_instances) {

}

void RasterizerSceneGLES1::_render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, GLuint p_framebuffer, const Rect2i &p_region) {

}

void RasterizerSceneGLES1::set_time(double p_time, double p_step) {
	time = p_time;
	time_step = p_step;
}

void RasterizerSceneGLES1::set_debug_draw_mode(RS::ViewportDebugDraw p_debug_draw) {
	debug_draw = p_debug_draw;
}

Ref<RenderSceneBuffers> RasterizerSceneGLES1::render_buffers_create() {
	Ref<RenderSceneBuffersGLES1> rb;
	rb.instantiate();
	ERR_FAIL_COND_V(rb.is_null(), Ref<RenderSceneBuffers>());
	return rb;
}

void RasterizerSceneGLES1::_render_buffers_debug_draw(Ref<RenderSceneBuffersGLES1> p_render_buffers, RID p_shadow_atlas, GLuint p_fbo) {
}

void RasterizerSceneGLES1::gi_set_use_half_resolution(bool p_enable) {
}

void RasterizerSceneGLES1::screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_curve) {
}

bool RasterizerSceneGLES1::screen_space_roughness_limiter_is_active() const {
	return false;
}

void RasterizerSceneGLES1::sub_surface_scattering_set_quality(RS::SubSurfaceScatteringQuality p_quality) {
}

void RasterizerSceneGLES1::sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) {
}

TypedArray<Image> RasterizerSceneGLES1::bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) {
	return TypedArray<Image>();
}

bool RasterizerSceneGLES1::free(RID p_rid) {
	if (is_environment(p_rid)) {
		environment_free(p_rid);
	} else if (sky_owner.owns(p_rid)) {
		Sky *sky = sky_owner.get_or_null(p_rid);
		ERR_FAIL_NULL_V(sky, false);
		_free_sky_data(sky);
		sky_owner.free(p_rid);
	} else if (GLES1::LightStorage::get_singleton()->owns_light_instance(p_rid)) {
		GLES1::LightStorage::get_singleton()->light_instance_free(p_rid);
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

void RasterizerSceneGLES1::update() {
	_update_dirty_skys();
}

void RasterizerSceneGLES1::sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) {
}

void RasterizerSceneGLES1::decals_set_filter(RS::DecalFilter p_filter) {
}

void RasterizerSceneGLES1::light_projectors_set_filter(RS::LightProjectorFilter p_filter) {
}

void RasterizerSceneGLES1::lightmaps_set_bicubic_filter(bool p_enable) {
	lightmap_bicubic_upscale = p_enable;
}

RasterizerSceneGLES1::RasterizerSceneGLES1() {
	singleton = this;

	initialize();

	batch_constructor();
}

RasterizerSceneGLES1::~RasterizerSceneGLES1() {
	singleton = nullptr;

	// Scene Shader
	GLES1::MaterialStorage::get_singleton()->shaders.scene_shader.version_free(scene_globals.shader_default_version);
	RSG::material_storage->material_free(scene_globals.default_material);
	RSG::material_storage->shader_free(scene_globals.default_shader);

	// Overdraw Shader
	RSG::material_storage->material_free(scene_globals.overdraw_material);
	RSG::material_storage->shader_free(scene_globals.overdraw_shader);

	// Sky Shader
	GLES1::MaterialStorage::get_singleton()->shaders.sky_shader.version_free(sky_globals.shader_default_version);
	RSG::material_storage->material_free(sky_globals.default_material);
	RSG::material_storage->shader_free(sky_globals.default_shader);
	RSG::material_storage->material_free(sky_globals.fog_material);
	RSG::material_storage->shader_free(sky_globals.fog_shader);
	memdelete_arr(sky_globals.directional_lights);
	memdelete_arr(sky_globals.last_frame_directional_lights);

	if (sky_globals.screen_triangle != 0) {
		glDeleteBuffers(1, &sky_globals.screen_triangle);
	}

	if (sky_globals.radiance_verts) {
		memdelete_arr(sky_globals.radiance_verts);
	}
	if (sky_globals.radiance_uvw) {
		memdelete_arr(sky_globals.radiance_uvw);
	}
	if (sky_globals.radiance_colors) {
		memdelete_arr(sky_globals.radiance_colors);
	}
	if (sky_globals.radiance_verts_vbo != 0) {
		glDeleteBuffers(1, &sky_globals.radiance_verts_vbo);
	}
	if (sky_globals.radiance_uvw_vbo != 0) {
		glDeleteBuffers(1, &sky_globals.radiance_uvw_vbo);
	}
	if (sky_globals.radiance_colors_vbo != 0) {
		glDeleteBuffers(1, &sky_globals.radiance_colors_vbo);
	}

#ifdef TOOLS_ENABLED
	if (editor_lines_vbo != 0) {
		glDeleteBuffers(1, &editor_lines_vbo);
	}
	if (editor_lines_color_vbo != 0) {
		glDeleteBuffers(1, &editor_lines_color_vbo);
	}
	if (rotate_gizmo_border_vbo != 0) {
		glDeleteBuffers(1, &rotate_gizmo_border_vbo);
	}
	if (rotate_gizmo_border_verts) {
		memdelete_arr(rotate_gizmo_border_verts);
	}
	if (rotate_gizmo_ring_verts) {
		memdelete_arr(rotate_gizmo_ring_verts);
	}
#endif
}

#endif // GLES1_ENABLED
