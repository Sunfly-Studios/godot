/**************************************************************************/
/*  rasterizer_scene_gles1.h                                              */
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

#ifndef RASTERIZER_SCENE_GLES1_H
#define RASTERIZER_SCENE_GLES1_H

#ifdef GLES1_ENABLED

#include "core/math/projection.h"
#include "core/templates/paged_allocator.h"
#include "core/templates/rid_owner.h"
#include "core/templates/self_list.h"
#include "drivers/rasterizer_common/batch/rasterizer_scene_batcher_common.h"
#include "drivers/gles_common/common_math.h"
#include "drivers/gles_common/common_functions.h"
#include "drivers/gles1/shaders/effects/cubemap_filter.glsl.gen.h"
#include "drivers/gles1/shaders/sky.glsl.gen.h"
#include "scene/resources/mesh.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_scene_render.h"
#include "servers/rendering_server.h"
#include "transpiler/shader_gles1.h"
#include "storage/light_storage.h"
#include "storage/material_storage.h"
#include "storage/render_scene_buffers_gles1.h"
#include "storage/utilities.h"
#include "storage/mesh_storage.h"
#include "function_router_gles1.h"

struct RenderDataGLES1 {
	Ref<RenderSceneBuffersGLES1> render_buffers;
	bool transparent_bg = false;
	Rect2i render_region;

	Transform3D cam_transform;
	Transform3D inv_cam_transform;
	Projection cam_projection;
	bool cam_orthogonal = false;
	bool cam_frustum = false;
	uint32_t camera_visible_layers = 0xFFFFFFFF;

	// For billboards to cast correct shadows.
	Transform3D main_cam_transform;

	// For stereo rendering
	uint32_t view_count = 1;
	Vector3 view_eye_offset[RendererSceneRender::MAX_RENDER_VIEWS];
	Projection view_projection[RendererSceneRender::MAX_RENDER_VIEWS];

	float z_near = 0.0;
	float z_far = 0.0;

	const PagedArray<RenderGeometryInstance *> *instances = nullptr;
	const PagedArray<RID> *lights = nullptr;
	const PagedArray<RID> *reflection_probes = nullptr;
	RID environment;
	RID camera_attributes;
	RID shadow_atlas;
	RID reflection_probe;
	int reflection_probe_pass = 0;

	float lod_distance_multiplier = 0.0;
	float screen_mesh_lod_threshold = 0.0;

	uint32_t directional_light_count = 0;
	uint32_t directional_shadow_count = 0;

	uint32_t spot_light_count = 0;
	uint32_t omni_light_count = 0;

	float luminance_multiplier = 1.0;

	RenderingMethod::RenderInfo *render_info = nullptr;

	/* Shadow data */
	const RendererSceneRender::RenderShadowData *render_shadows = nullptr;
	int render_shadow_count = 0;
};

class RasterizerCanvasGLES1;
class RasterizerSceneGLES1;

struct BatcherAPISceneGLES1 {
	using Scene = RasterizerSceneGLES1;
	using MaterialData = GLES1::SceneMaterialData;
	using MeshStorage = GLES1::MeshStorage;
	using TextureStorage = GLES1::TextureStorage;
	using Texture = GLES1::Texture;
	using CanvasTexture = GLES1::CanvasTexture;
	using Shader = GLES1::Shader;
	using MeshInstance = GLES1::MeshInstance;
	using MultiMesh = GLES1::MultiMesh;

	static constexpr bool FORCE_BAKE_MODULATE = true;
};

class RasterizerSceneGLES1 : public RendererSceneRender, public RasterizerSceneBatcherCommon<BatcherAPISceneGLES1> {
	friend class RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>;

public:
	enum RenderListType {
		RENDER_LIST_OPAQUE, //used for opaque objects
		RENDER_LIST_ALPHA, //used for transparent objects
		RENDER_LIST_SECONDARY, //used for shadows and other objects
#ifdef TOOLS_ENABLED
		RENDER_LIST_GIZMOS, //used for intercepting editor gizmos
		RENDER_LIST_EDITOR_GRID, //used for 3D grid rendering
#endif
		RENDER_LIST_MAX
	};

	using PassMode = BatcherEnums::PassMode;
	using BatchType = RasterizerSceneBatcherCommon<BatcherAPISceneGLES1>;

	static constexpr PassMode PASS_MODE_COLOR = BatcherEnums::PASS_MODE_COLOR;
	static constexpr PassMode PASS_MODE_COLOR_TRANSPARENT = BatcherEnums::PASS_MODE_COLOR_TRANSPARENT;
	static constexpr PassMode PASS_MODE_SHADOW = BatcherEnums::PASS_MODE_SHADOW;
	static constexpr PassMode PASS_MODE_DEPTH = BatcherEnums::PASS_MODE_DEPTH;
	static constexpr PassMode PASS_MODE_MATERIAL = BatcherEnums::PASS_MODE_MATERIAL;
	static constexpr PassMode PASS_MODE_SHADOW_PROJECTION = BatcherEnums::PASS_MODE_SHADOW_PROJECTION;

	// These should share as much as possible with SkyUniform Location
	enum SceneUniformLocation {
		SCENE_TONEMAP_UNIFORM_LOCATION,
		SCENE_GLOBALS_UNIFORM_LOCATION,
		SCENE_DATA_UNIFORM_LOCATION,
		SCENE_MATERIAL_UNIFORM_LOCATION,
		SCENE_EMPTY1, // Unused, put here to avoid conflicts with SKY_DIRECTIONAL_LIGHT_UNIFORM_LOCATION.
		SCENE_OMNILIGHT_UNIFORM_LOCATION,
		SCENE_SPOTLIGHT_UNIFORM_LOCATION,
		SCENE_DIRECTIONAL_LIGHT_UNIFORM_LOCATION,
		SCENE_MULTIVIEW_UNIFORM_LOCATION,
		SCENE_POSITIONAL_SHADOW_UNIFORM_LOCATION,
		SCENE_DIRECTIONAL_SHADOW_UNIFORM_LOCATION,
		SCENE_EMPTY2, // Unused, put here to avoid conflicts with SKY_MULTIVIEW_UNIFORM_LOCATION.
	};

	enum SkyUniformLocation {
		SKY_TONEMAP_UNIFORM_LOCATION,
		SKY_GLOBALS_UNIFORM_LOCATION,
		SKY_EMPTY1, // Unused, put here to avoid conflicts with SCENE_DATA_UNIFORM_LOCATION.
		SKY_MATERIAL_UNIFORM_LOCATION,
		SKY_DIRECTIONAL_LIGHT_UNIFORM_LOCATION,
		SKY_EMPTY2, // Unused, put here to avoid conflicts with SCENE_OMNILIGHT_UNIFORM_LOCATION.
		SKY_EMPTY3, // Unused, put here to avoid conflicts with SCENE_SPOTLIGHT_UNIFORM_LOCATION.
		SKY_EMPTY4, // Unused, put here to avoid conflicts with SCENE_DIRECTIONAL_LIGHT_UNIFORM_LOCATION.
		SKY_EMPTY5, // Unused, put here to avoid conflicts with SCENE_MULTIVIEW_UNIFORM_LOCATION.
		SKY_EMPTY6, // Unused, put here to avoid conflicts with SCENE_POSITIONAL_SHADOW_UNIFORM_LOCATION.
		SKY_EMPTY7, // Unused, put here to avoid conflicts with SCENE_DIRECTIONAL_SHADOW_UNIFORM_LOCATION.
		SKY_MULTIVIEW_UNIFORM_LOCATION,
	};

	/* BATCH WRAPPERS */

	_FORCE_INLINE_ static BatchType::TransformMode3D _batch_find_transform_mode_3d(const Transform3D &p_tr, float *r_inv_scale = nullptr) {
		return BatchType::_find_transform_mode_3d(p_tr, r_inv_scale);
	}

private:
	static RasterizerSceneGLES1 *singleton;
	RS::ViewportDebugDraw debug_draw = RS::VIEWPORT_DEBUG_DRAW_DISABLED;
	uint64_t scene_pass = 0;

	template <typename T>
	struct InstanceSort {
		float depth;
		T *instance = nullptr;
		bool operator<(const InstanceSort &p_sort) const {
			return depth < p_sort.depth;
		}
	};

	struct SceneGlobals {
		RID shader_default_version;
		RID default_material;
		RID default_shader;
		RID overdraw_material;
		RID overdraw_shader;
	} scene_globals;

	/* LIGHT INSTANCE */

	struct LightData {
		float position[3];
		float inv_radius;

		float direction[3]; // Only used by SpotLight
		float size;

		float color[3];
		float attenuation;

		float inv_spot_attenuation;
		float cos_spot_angle;
		float specular_amount;
		float shadow_opacity;

		float pad[3];
		uint32_t bake_mode;
	};
	static_assert(sizeof(LightData) % 16 == 0, "LightData size must be a multiple of 16 bytes");

	struct DirectionalLightData {
		float direction[3];
		float energy;

		float color[3];
		float size;

		uint32_t enabled; // For use by SkyShaders
		uint32_t bake_mode;
		float shadow_opacity;
		float specular;
	};
	static_assert(sizeof(DirectionalLightData) % 16 == 0, "DirectionalLightData size must be a multiple of 16 bytes");

	struct ShadowData {
		float shadow_matrix[16];

		float light_position[3];
		float shadow_normal_bias;

		float pad[3];
		float shadow_atlas_pixel_size;
	};
	static_assert(sizeof(ShadowData) % 16 == 0, "ShadowData size must be a multiple of 16 bytes");

	struct DirectionalShadowData {
		float direction[3];
		float shadow_atlas_pixel_size;
		float shadow_normal_bias[4];
		float shadow_split_offsets[4];
		float shadow_matrices[4][16];
		float fade_from;
		float fade_to;
		uint32_t blend_splits; // Not exposed to the shader.
		uint32_t pad;
	};
	static_assert(sizeof(DirectionalShadowData) % 16 == 0, "DirectionalShadowData size must be a multiple of 16 bytes");

	class GeometryInstanceGLES1;

	// Cached data for drawing surfaces
	struct GeometryInstanceSurface {
		enum {
			FLAG_PASS_DEPTH = 1,
			FLAG_PASS_OPAQUE = 2,
			FLAG_PASS_ALPHA = 4,
			FLAG_PASS_SHADOW = 8,
			FLAG_USES_SHARED_SHADOW_MATERIAL = 128,
			FLAG_USES_SCREEN_TEXTURE = 2048,
			FLAG_USES_DEPTH_TEXTURE = 4096,
			FLAG_USES_NORMAL_TEXTURE = 8192,
			FLAG_USES_DOUBLE_SIDED_SHADOWS = 16384,
		};

		union {
			struct {
				uint64_t sort_key1;
				uint64_t sort_key2;
			};
			struct {
				uint64_t lod_index : 8;
				uint64_t surface_index : 8;
				uint64_t geometry_id : 32;
				uint64_t material_id_low : 16;

				uint64_t material_id_hi : 16;
				uint64_t shader_id : 32;
				uint64_t uses_softshadow : 1;
				uint64_t uses_projector : 1;
				uint64_t uses_forward_gi : 1;
				uint64_t uses_lightmap : 1;
				uint64_t depth_layer : 4;
				uint64_t priority : 8;
			};
		} sort;

		RS::PrimitiveType primitive = RS::PRIMITIVE_MAX;
		uint32_t flags = 0;
		uint32_t surface_index = 0;
		uint32_t lod_index = 0;
		uint32_t index_count = 0;
		int32_t light_pass_index = -1;
		bool finished_base_pass = false;

		PackedVector3Array vertex_cache;
		PackedVector3Array normal_cache;
		PackedFloat32Array tangent_cache;
		PackedVector2Array uv_cache;
		PackedVector2Array uv2_cache;
		PackedColorArray color_cache;
		PackedInt32Array index_cache;

		mutable LocalVector<BatchVertex3DTier1> baked_world_cache;
		mutable LocalVector<BatchTangent> baked_tangent_cache;
		mutable Transform3D cached_transform;
		mutable uint32_t cached_transform_hash = 0;

		// Independent per-frame transform tracking for MPI dynamic detection.
		mutable uint32_t prev_frame_transform_hash = 0;
		mutable uint64_t last_transform_frame = 0;
		mutable bool is_dynamic_frame = false;

		void *surface = nullptr;
		GLES1::SceneShaderData *shader = nullptr;
		GLES1::SceneMaterialData *material = nullptr;

		void *surface_shadow = nullptr;
		GLES1::SceneShaderData *shader_shadow = nullptr;
		GLES1::SceneMaterialData *material_shadow = nullptr;

		bool gizmo_cached = false;
		GLuint gizmo_vertex_buffer = 0;
		GLuint gizmo_color_buffer = 0;
		GLuint gizmo_uv_buffer = 0;
		GLuint gizmo_index_buffer = 0;
		float *gizmo_vertex_array = nullptr;
		uint16_t *gizmo_index_array = nullptr;

		GeometryInstanceSurface *next = nullptr;
		GeometryInstanceGLES1 *owner = nullptr;
	};

	struct GeometryInstanceLightmapSH {
		Color sh[9];
	};

	class GeometryInstanceGLES1 : public RenderGeometryInstanceBase {
	public:
		//used during rendering
		bool store_transform_cache = true;

		int32_t instance_count = 0;

		bool can_sdfgi = false;
		bool using_projectors = false;
		bool using_softshadows = false;

		struct LightPass {
			int32_t light_id = -1; // Position in the light uniform buffer.
			int32_t shadow_id = -1; // Position in the shadow uniform buffer.
			RID light_instance_rid;
			bool is_omni = false;
		};

		LocalVector<LightPass> light_passes;

		uint32_t paired_omni_light_count = 0;
		uint32_t paired_spot_light_count = 0;
		LocalVector<RID> paired_omni_lights;
		LocalVector<RID> paired_spot_lights;
		LocalVector<uint32_t> omni_light_gl_cache;
		LocalVector<uint32_t> spot_light_gl_cache;

		LocalVector<RID> paired_reflection_probes;
		LocalVector<RID> reflection_probe_rid_cache;
		LocalVector<Transform3D> reflection_probes_local_transform_cache;

		RID skeleton;
		RID lightmap_instance;
		Rect2 lightmap_uv_scale;
		uint32_t lightmap_slice_index;
		GeometryInstanceLightmapSH *lightmap_sh = nullptr;

		// Used during setup.
		GeometryInstanceSurface *surface_caches = nullptr;
		SelfList<GeometryInstanceGLES1> dirty_list_element;

		GeometryInstanceGLES1() :
			lightmap_slice_index(0),
			dirty_list_element(this) {}

		void _clear_surface_caches();
		virtual void _mark_dirty() override;
		virtual void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) override;
		virtual void set_lightmap_capture(const Color *p_sh9) override;

		virtual void pair_light_instances(const RID *p_light_instances, uint32_t p_light_instance_count) override;
		virtual void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) override;
		virtual void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) override {}
		virtual void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) override {}

		virtual void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) override {}
	};

	enum {
		INSTANCE_DATA_FLAGS_DYNAMIC = 1 << 3,
		INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE = 1 << 4,
		INSTANCE_DATA_FLAG_USE_GI_BUFFERS = 1 << 5,
		INSTANCE_DATA_FLAG_USE_LIGHTMAP_CAPTURE = 1 << 7,
		INSTANCE_DATA_FLAG_USE_LIGHTMAP = 1 << 8,
		INSTANCE_DATA_FLAG_USE_SH_LIGHTMAP = 1 << 9,
		INSTANCE_DATA_FLAG_USE_VOXEL_GI = 1 << 10,
		INSTANCE_DATA_FLAG_PARTICLES = 1 << 11,
		INSTANCE_DATA_FLAG_MULTIMESH = 1 << 12,
		INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D = 1 << 13,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR = 1 << 14,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA = 1 << 15,
	};

#ifdef TOOLS_ENABLED
	// 3D Gizmos
	GLuint rotate_gizmo_border_vbo = 0;
	float *rotate_gizmo_border_verts = nullptr;
	float *rotate_gizmo_ring_verts = nullptr;

	// Draw functions
	void _draw_editor_lines(const RenderDataGLES1 *p_render_data, bool p_flip_y);
	void _draw_editor_grid(const RenderDataGLES1 *p_render_data, bool p_flip_y);
	void _draw_editor_gizmos(const RenderDataGLES1 *p_render_data, bool p_flip_y);
	void _setup_gizmo_surface_material(const GeometryInstanceSurface *p_surf, Color &r_base_albedo, bool &r_has_texture);

	_FORCE_INLINE_ RID _editor_resolve_surface_material(const GeometryInstanceSurface *p_surf) const {
		RID mat_rid = RID();
		if (!p_surf || !p_surf->owner || !p_surf->owner->data) {
			return mat_rid;
		}
		if ((uint32_t)p_surf->owner->data->surface_materials.size() > p_surf->surface_index) {
			mat_rid = p_surf->owner->data->surface_materials[p_surf->surface_index];
		}
		if (!mat_rid.is_valid()) {
			if (p_surf->owner->data->base_type == RS::INSTANCE_MESH) {
				mat_rid = GLES1::MeshStorage::get_singleton()->mesh_surface_get_material(p_surf->owner->data->base, p_surf->surface_index);
			} else if (p_surf->owner->data->base_type == RS::INSTANCE_MULTIMESH) {
				RID mesh = GLES1::MeshStorage::get_singleton()->multimesh_get_mesh(p_surf->owner->data->base);
				if (mesh.is_valid()) {
					mat_rid = GLES1::MeshStorage::get_singleton()->mesh_surface_get_material(mesh, p_surf->surface_index);
				}
			}
		}
		return mat_rid;
	}

	// Setup functions
	_FORCE_INLINE_ void _gl_setup_editor_state(const RenderDataGLES1 *p_render_data, bool p_flip_y) {
		scene_state.reset_gl_state();

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();

		Projection correction;
		correction.set_depth_correction(p_flip_y, true, false);
		Projection projection = correction * p_render_data->cam_projection;

		glLoadMatrixf(_gl_load_projection(projection));

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();

		Transform3D view = p_render_data->inv_cam_transform;
		glLoadMatrixf(_gl_load_transform(view));

		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_FOG);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_GEQUAL);
	}

	_FORCE_INLINE_ void _gl_teardown_editor_state() {
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		scene_state.reset_gl_state();
	}
#endif

	/* INLINE GL HELPERS */

	_FORCE_INLINE_ void _gl_batch_decode_multimesh_instance(const float *p_data, RS::MultimeshTransformFormat p_format, bool p_uses_colors, uint32_t p_color_offset, Transform3D &r_xform, Color &r_color) {
		if (p_format == RS::MULTIMESH_TRANSFORM_3D) {
			r_xform.basis.rows[0] = Vector3(p_data[0], p_data[1], p_data[2]);
			r_xform.origin.x = p_data[3];
			r_xform.basis.rows[1] = Vector3(p_data[4], p_data[5], p_data[6]);
			r_xform.origin.y = p_data[7];
			r_xform.basis.rows[2] = Vector3(p_data[8], p_data[9], p_data[10]);
			r_xform.origin.z = p_data[11];
		} else {
			r_xform.basis.rows[0] = Vector3(p_data[0], p_data[1], 0.0f);
			r_xform.origin.x = p_data[3];
			r_xform.basis.rows[1] = Vector3(p_data[4], p_data[5], 0.0f);
			r_xform.origin.y = p_data[7];
		}

		if (p_uses_colors) {
			const float *cdata = p_data + p_color_offset;
			r_color = Color(cdata[0], cdata[1], cdata[2], cdata[3]);
		} else {
			r_color = Color(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	_FORCE_INLINE_ void _gl_setup_bake_uv2_tex_params() {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	_FORCE_INLINE_ void _gl_setup_uv2_projection(const Vector2 &p_uv_offset) {
		// gl_Position = vec4((uv2_attrib.xy + uv_offset) * 2.0 - 1.0, 0.0, 1.0)
		// Since in PASS_MODE_MATERIAL the vertex array is fed UV2 (z = 0),
		// an orthographic projection with scale (2, 2, 1) and translation
		// (2 * offset.x - 1, 2 * offset.y - 1, 0) maps [0, 1] UVs to [-1, 1] NDC.
		const float m[16] = {
			2.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			(p_uv_offset.x * 2.0f) - 1.0f, (p_uv_offset.y * 2.0f) - 1.0f, 0.0f, 1.0f
		};
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(m);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	_FORCE_INLINE_ void _gl_setup_uv2_material_pass(GLES1::SceneMaterialData *p_material_data, uint64_t p_spec_constants) {
		glDisable(GL_LIGHTING);
		glDisable(GL_FOG);
		glDisable(GL_COLOR_MATERIAL);

		if (GLES1_CONFIG->max_texture_units > 1) {
			glActiveTexture(GL_TEXTURE1);
			glDisable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			glActiveTexture(GL_TEXTURE0);
		}

		if (p_spec_constants & SceneShaderGLES1::RENDER_MATERIAL_ALBEDO) {
			if (p_material_data && p_material_data->texture_cache.size() > 0) {
				glEnable(GL_TEXTURE_2D);
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			} else {
				glDisable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		} else if (p_spec_constants & SceneShaderGLES1::RENDER_MATERIAL_NORMAL) {
			glDisable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			// Flat tangent-space normal (0.5, 0.5, 1.0, 1.0)
			glColor4f(0.5f, 0.5f, 1.0f, 1.0f);
		} else if (p_spec_constants & SceneShaderGLES1::RENDER_MATERIAL_ORM) {
			glDisable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			// Default ORM (AO = 1.0, Roughness = 0.8, Metallic = 0.2)
			glColor4f(1.0f, 0.8f, 0.2f, 1.0f);
		} else if (p_spec_constants & SceneShaderGLES1::RENDER_MATERIAL_EMISSION) {
			glDisable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
		}
	}

	// RAII like data structures to manage these matrix
	// stacks a bit easier.
	struct GLMatrixScope {
		bool active = false;
		GLenum mode = GL_INVALID_ENUM;
		_FORCE_INLINE_ GLMatrixScope(GLenum p_mode, bool p_active = true) {
			active = p_active;
			if (!active) {
				return;
			}
			mode = p_mode;
			glMatrixMode(mode);
			glPushMatrix();
		}
		_FORCE_INLINE_ ~GLMatrixScope() {
			if (!active) {
				return;
			}
			glMatrixMode(mode);
			glPopMatrix();
		}
	};

	struct GLProjectiveTextureScope {
		bool active = false;
		GLenum texture_unit = GL_INVALID_ENUM;
		_FORCE_INLINE_ GLProjectiveTextureScope(bool p_active, GLenum p_texture_unit) {
			active = p_active;
			if (!active) {
				return;
			}
			texture_unit = p_texture_unit;
			glActiveTexture(texture_unit);
			glClientActiveTexture(texture_unit);
			glEnable(GL_TEXTURE_2D);

			glMatrixMode(GL_TEXTURE);
			glPushMatrix();
		}
		_FORCE_INLINE_ ~GLProjectiveTextureScope() {
			if (!active) {
				return;
			}
			glActiveTexture(texture_unit);
			glClientActiveTexture(texture_unit);

			if (GLES1_CONFIG->support_shadow) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
			}

			glMatrixMode(GL_TEXTURE);
			glPopMatrix();

			glBindTexture(GL_TEXTURE_2D, 0);
			glDisable(GL_TEXTURE_2D);

			glMatrixMode(GL_MODELVIEW);
			glActiveTexture(GL_TEXTURE0);
			glClientActiveTexture(GL_TEXTURE0);
		}
	};

	struct GLClipPlaneScope {
		bool active = false;
		_FORCE_INLINE_ GLClipPlaneScope(bool p_active, const GLfloat *p_near, const GLfloat *p_far) {
			if (p_active) {
				enable(p_near, p_far);
			}
		}
		_FORCE_INLINE_ void enable(const GLfloat *p_near, const GLfloat *p_far) {
			active = true;
			glEnable(GL_CLIP_PLANE0);
			glEnable(GL_CLIP_PLANE1);

			GLES1::Router::clip_plane(GL_CLIP_PLANE0, p_near);
			GLES1::Router::clip_plane(GL_CLIP_PLANE1, p_far);
		}
		_FORCE_INLINE_ ~GLClipPlaneScope() {
			if (active) {
				glDisable(GL_CLIP_PLANE0);
				glDisable(GL_CLIP_PLANE1);
			}
		}
	};

	enum GLClientStateFlags {
		GL_CLIENT_STATE_VERTEX = 1 << 0,
		GL_CLIENT_STATE_COLOR = 1 << 1,
		GL_CLIENT_STATE_NORMAL = 1 << 2,
		GL_CLIENT_STATE_TEXCOORD = 1 << 3,
	};

	_FORCE_INLINE_ void _gl_set_client_states(uint32_t p_flags) {
		if (p_flags & GL_CLIENT_STATE_VERTEX) {
			glEnableClientState(GL_VERTEX_ARRAY);
		} else {
			glDisableClientState(GL_VERTEX_ARRAY);
		}
		
		if (p_flags & GL_CLIENT_STATE_COLOR) {
			glEnableClientState(GL_COLOR_ARRAY);
		} else {
			glDisableClientState(GL_COLOR_ARRAY);
		}

		if (p_flags & GL_CLIENT_STATE_NORMAL) {
			glEnableClientState(GL_NORMAL_ARRAY);
		} else {
			glDisableClientState(GL_NORMAL_ARRAY);
		}

		if (p_flags & GL_CLIENT_STATE_TEXCOORD) {
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		} else {
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
	}

	_FORCE_INLINE_ void _gl_setup_alpha_scissor(const GLES1::SceneMaterialData *p_material_data) {
		bool use_scissor = false;
		float threshold = 0.5f;

		if (p_material_data && p_material_data->shader_data) {
			if (p_material_data->shader_data->uses_alpha_clip) {
				use_scissor = true;

				if (
					p_material_data->approx_type != GLES1::SceneMaterialData::APPROX_NONE &&
					p_material_data->approx_data
				) {
					GLES1::StandardMaterialState *state = static_cast<GLES1::StandardMaterialState *>(p_material_data->approx_data);
					if (state->use_alpha_scissor) {
						threshold = state->alpha_scissor_threshold;
					}
				}
			} else if (p_material_data->shader_data->uses_depth_prepass_alpha) {
				use_scissor = true;
				if (bdata.pass_mode == PASS_MODE_SHADOW) {
					threshold = 0.5f;
				} else {
					threshold = ((bdata.pass_mode == PASS_MODE_COLOR_TRANSPARENT) ? 0.1f : 0.99f);
				}
			}
		}

		if (use_scissor) {
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GEQUAL, threshold);
		} else {
			glDisable(GL_ALPHA_TEST);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_setup_alpha_scissor");
	}

	_FORCE_INLINE_ void _gl_setup_material_pass(const GLES1::SceneMaterialData *p_material_data, bool p_is_additive) {
		constexpr GLfloat mat_default_albedo[] = { 0.6f, 0.6f, 0.6f, 1.0f };
		constexpr GLfloat mat_specular[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		constexpr GLfloat mat_zero[] = { 0.0f, 0.0f, 0.0f, 1.0f };

		bool unshaded = (
			p_material_data &&
			p_material_data->shader_data &&
			p_material_data->shader_data->unshaded
		);

		if (unshaded) {
			glDisable(GL_LIGHTING);
		} else {
			glEnable(GL_LIGHTING);
		}

		if (p_material_data && p_material_data->texture_cache.size() > 0) {
			glEnable(GL_TEXTURE_2D);
		} else {
			glDisable(GL_TEXTURE_2D);
		}

		bool has_approx = p_material_data && (
			p_material_data->approx_type != GLES1::SceneMaterialData::APPROX_NONE
		) && p_material_data->approx_data;

		glDisable(GL_COLOR_MATERIAL);

		_gl_setup_alpha_scissor(p_material_data);

		if (!has_approx) {
			glColor4f(mat_default_albedo[0], mat_default_albedo[1], mat_default_albedo[2], mat_default_albedo[3]);
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_default_albedo);
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
			glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0.0f);

			if (p_is_additive) {
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_zero);
				glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_zero);
			} else {
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_default_albedo);
				glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_zero);
			}
		} else {
			const GLES1::StandardMaterialState *state = static_cast<const GLES1::StandardMaterialState *>(p_material_data->approx_data);
			glColor4f(state->albedo[0], state->albedo[1], state->albedo[2], state->albedo[3]);
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, state->albedo);
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, state->specular);
			glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, state->shininess);

			if (p_is_additive) {
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_zero);
				glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_zero);
			} else {
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, state->albedo);
				glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, state->emission);
			}
		}

		// For FVF_UNBATCHED and SMDDF, only enable GL_COLOR_MATERIAL if the batch actually
		// uses vertex colors AND the shader requests them. Otherwise, material albedo is
		// supplied directly via glColor4f and glMaterialfv below without a colour array bound.
		const bool is_immediate_draw = (_render_item_state.curr_batch == nullptr);
		const uint32_t batch_flags = _render_item_state.curr_batch ? _render_item_state.curr_batch->flags : 0;
		const bool unbatched_uses_color = (
			!is_immediate_draw &&
			(bdata.fvf == BatcherEnums::FVF_UNBATCHED || bdata.fvf == BatcherEnums::FVF_SAME_MESH_LOOP) &&
			(batch_flags & BatcherEnums::BTF_3D_USES_COLORS) != 0 &&
			(
				(p_material_data && p_material_data->shader_data && p_material_data->shader_data->uses_color) ||
				(batch_flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0
			)
		);
		const bool immediate_uses_color = (
			is_immediate_draw &&
			scene_state.immediate_surface_has_colors &&
			p_material_data &&
			p_material_data->shader_data &&
			p_material_data->shader_data->uses_color
		);

		bool use_color_material = (
			immediate_uses_color ||
			unbatched_uses_color ||
			(!is_immediate_draw && _batch_util_is_colored_tier_fvf(bdata.fvf))
		);

		if (use_color_material) {
			// Reset glColor4f and diffuse/ambient material factors to white so the pipeline
			// does not double multiply the material albedo.
			constexpr GLfloat mat_white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_white);
			if (!p_is_additive) {
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_white);
			}
			glEnable(GL_COLOR_MATERIAL);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_setup_material_pass: material pass state");
	}

	_FORCE_INLINE_ void _gl_setup_light(bool p_two_sided = false) {
		glEnable(GL_LIGHTING);
		glEnable(GL_NORMALIZE);
		glShadeModel(GL_SMOOTH);

		const GLfloat two_side[] = { p_two_sided ? 1.0f : 0.0f };
		glLightModelfv(GL_LIGHT_MODEL_TWO_SIDE, two_side);
	}

	_FORCE_INLINE_ void _gl_setup_sky_fill_light(const Color &p_sky_top, const Color &p_ground_bottom) {
		const GLfloat sky_fill_col[] = {
			MAX(0.0f, p_sky_top.r - p_ground_bottom.r),
			MAX(0.0f, p_sky_top.g - p_ground_bottom.g),
			MAX(0.0f, p_sky_top.b - p_ground_bottom.b),
			1.0f
		};
		constexpr GLfloat zero_col[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		constexpr GLfloat top_pos[] = { 0.0f, 1.0f, 0.0f, 0.0f };

		int max_lights = GLES1_CONFIG->max_lights;
		int sky_light_idx = GL_LIGHT0 + (max_lights - 1);

		glEnable(sky_light_idx);
		glLightfv(sky_light_idx, GL_AMBIENT, zero_col);
		glLightfv(sky_light_idx, GL_DIFFUSE, sky_fill_col);
		glLightfv(sky_light_idx, GL_SPECULAR, zero_col);
		glLightfv(sky_light_idx, GL_POSITION, top_pos);
		glLightf(sky_light_idx, GL_CONSTANT_ATTENUATION, 1.0f);
		glLightf(sky_light_idx, GL_LINEAR_ATTENUATION, 0.0f);
		glLightf(sky_light_idx, GL_QUADRATIC_ATTENUATION, 0.0f);
		glLightf(sky_light_idx, GL_SPOT_CUTOFF, 180.0f);
		glLightf(sky_light_idx, GL_SPOT_EXPONENT, 0.0f);

		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_setup_sky_fill_light");
	}

	_FORCE_INLINE_ void _gl_setup_ambient_model(const Color &p_color) {
		const GLfloat ambient_col[] = { p_color.r, p_color.g, p_color.b, p_color.a };
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient_col);
	}

	_FORCE_INLINE_ void _gl_set_light_chunk_state(int p_active_count) {
		int max_lights = GLES1_CONFIG->max_lights;
		for (int i = 0; i < max_lights - 1; i++) {
			if (i < p_active_count) {
				glEnable(GL_LIGHT0 + i);
			} else {
				glDisable(GL_LIGHT0 + i);
			}
		}
	}

	_FORCE_INLINE_ void _gl_setup_common_light_params(int p_gl_light, const float p_pos_or_dir[4], const float p_color[3], float p_linear_attenuation, float p_spot_cutoff = 180.0f, float p_spot_exponent = 0.0f) {
		constexpr float light_amb[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		glLightfv(p_gl_light, GL_AMBIENT, light_amb);
		glLightfv(p_gl_light, GL_POSITION, p_pos_or_dir);

		const float light_col[4] = { p_color[0], p_color[1], p_color[2], 1.0f };
		glLightfv(p_gl_light, GL_DIFFUSE, light_col);
		glLightfv(p_gl_light, GL_SPECULAR, light_col);

		glLightf(p_gl_light, GL_CONSTANT_ATTENUATION, 1.0f);
		glLightf(p_gl_light, GL_LINEAR_ATTENUATION, p_linear_attenuation);
		glLightf(p_gl_light, GL_QUADRATIC_ATTENUATION, 0.0f);
		glLightf(p_gl_light, GL_SPOT_CUTOFF, p_spot_cutoff);
		glLightf(p_gl_light, GL_SPOT_EXPONENT, p_spot_exponent);
	}

	_FORCE_INLINE_ void _gl_setup_directional_light(const DirectionalLightData &p_light, int p_light_idx) {
		const int gl_light = GL_LIGHT0 + p_light_idx;
		const float light_dir[4] = { p_light.direction[0], p_light.direction[1], p_light.direction[2], 0.0f };
		_gl_setup_common_light_params(gl_light, light_dir, p_light.color, 0.0f);

		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_setup_directional_light: setup directional light");
	}

	_FORCE_INLINE_ void _gl_setup_omni_light(const LightData &p_light, int p_light_idx) {
		const int gl_light = GL_LIGHT0 + p_light_idx;
		const float light_pos[4] = { p_light.position[0], p_light.position[1], p_light.position[2], 1.0f };
		_gl_setup_common_light_params(gl_light, light_pos, p_light.color, p_light.attenuation * p_light.inv_radius);

		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_setup_omni_light: setup omni light");
	}

	_FORCE_INLINE_ void _gl_setup_spot_light(const LightData &p_light, int p_light_idx) {
		const int gl_light = GL_LIGHT0 + p_light_idx;
		const float light_pos[4] = { p_light.position[0], p_light.position[1], p_light.position[2], 1.0f };
		const float spot_dir[3] = { -p_light.direction[0], -p_light.direction[1], -p_light.direction[2] };
		glLightfv(gl_light, GL_SPOT_DIRECTION, spot_dir);

		const float cutoff = Math::rad_to_deg(Math::acos(p_light.cos_spot_angle));
		_gl_setup_common_light_params(gl_light, light_pos, p_light.color, p_light.attenuation * p_light.inv_radius, cutoff, p_light.inv_spot_attenuation * 128.0f);

		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_setup_spot_light: setup spot light");
	}

	_FORCE_INLINE_ void _gl_setup_fog(const GLES1::SceneMaterialData *p_material_data) {
		const GLfloat fog_col[4] = {
			scene_state.is_additive_pass ? 0.0f : scene_state.ubo.fog_light_color[0],
			scene_state.is_additive_pass ? 0.0f : scene_state.ubo.fog_light_color[1],
			scene_state.is_additive_pass ? 0.0f : scene_state.ubo.fog_light_color[2],
			1.0f
		};
		if (p_material_data && p_material_data->use_distance_fade) {
			glEnable(GL_FOG);
			glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_LINEAR));
			glFogf(GL_FOG_START, p_material_data->distance_fade_min);
			glFogf(GL_FOG_END, p_material_data->distance_fade_max);
			glFogfv(GL_FOG_COLOR, fog_col);
		} else if (scene_state.ubo.fog_enabled && !(scene_state.current_spec_constants & SceneShaderGLES1::DISABLE_FOG)) {
			glEnable(GL_FOG);
			if (scene_state.ubo.fog_mode == uint32_t(RS::ENV_FOG_MODE_DEPTH)) {
				glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_LINEAR));
				glFogf(GL_FOG_START, scene_state.ubo.fog_depth_begin);
				glFogf(GL_FOG_END, scene_state.ubo.fog_depth_end);
			} else {
				glFogf(GL_FOG_MODE, static_cast<GLfloat>(GL_EXP));
				glFogf(GL_FOG_DENSITY, scene_state.ubo.fog_density);
			}
			glFogfv(GL_FOG_COLOR, fog_col);
		} else {
			glDisable(GL_FOG);
		}
	}

	_FORCE_INLINE_ void _gl_batch_material_manage_texture_shadows() {
		// TU1
		glActiveTexture(GL_TEXTURE1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, scene_state.current_shadow_texture);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_batch_material_manage_texture_shadows: GL_TEXTURE1 projective texture restore");

		if (GLES1_CONFIG->max_texture_units > 2) {
			// TU2
			glActiveTexture(GL_TEXTURE2);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, scene_state.z_bound_texture);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_batch_material_manage_texture_shadows: GL_TEXTURE2 projective texture restore");
		}

		glActiveTexture(GL_TEXTURE0);
	}

	_FORCE_INLINE_ void _gl_push_shadow_projection_matrices(const Transform3D &p_world_xform) {
		if (bdata.pass_mode != PASS_MODE_SHADOW_PROJECTION) {
			return;
		}
		if (GLES1_CONFIG->max_texture_units > 1) {
			glActiveTexture(GL_TEXTURE1);
			glMatrixMode(GL_TEXTURE);
			glPushMatrix();
			glMultMatrixf(_gl_mult_transform(p_world_xform));
		}
		if (GLES1_CONFIG->max_texture_units > 2) {
			glActiveTexture(GL_TEXTURE2);
			glMatrixMode(GL_TEXTURE);
			glPushMatrix();
			glMultMatrixf(_gl_mult_transform(p_world_xform));
		}
		if (GLES1_CONFIG->max_texture_units > 1) {
			glActiveTexture(GL_TEXTURE0);
			glMatrixMode(GL_MODELVIEW);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_push_shadow_projection_matrices: push texture matrices");
		}
	}

	_FORCE_INLINE_ void _gl_pop_shadow_projection_matrices() {
		if (bdata.pass_mode != PASS_MODE_SHADOW_PROJECTION) {
			return;
		}
		if (GLES1_CONFIG->max_texture_units > 1) {
			glActiveTexture(GL_TEXTURE1);
			glMatrixMode(GL_TEXTURE);
			glPopMatrix();
		}
		if (GLES1_CONFIG->max_texture_units > 2) {
			glActiveTexture(GL_TEXTURE2);
			glMatrixMode(GL_TEXTURE);
			glPopMatrix();
		}
		if (GLES1_CONFIG->max_texture_units > 1) {
			glActiveTexture(GL_TEXTURE0);
			glMatrixMode(GL_MODELVIEW);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_pop_shadow_projection_matrices: pop texture matrices");
		}
	}

	_FORCE_INLINE_ void _gl_rsii_setup_shadow_projection_texcoords(const GeometryInstanceSurface *p_surface, uint64_t p_input_mask, bool p_use_deformed_instance) {
		if (GLES1_CONFIG->max_texture_units <= 1) {
			return;
		}

		GLES1::MeshStorage *mesh_storage = GLES1::MeshStorage::get_singleton();
		GLuint dummy_vao = 0;
		GLES1::Mesh::Surface::Version *ver = nullptr;
		GLuint proj_vbo = 0;
		const uint8_t *proj_ptr = nullptr;

		// Skinned or blend-shaped meshes store their deformed vertex positions in the
		// MeshInstance's surface buffer rather than the static base Mesh surface.
		// Resolve the deformed stream first so projected shadows match animated poses.
		if (p_use_deformed_instance) {
			GLES1::MeshInstance *mi = mesh_storage->get_mesh_instance(p_surface->owner->mesh_instance);
			if (mi && p_surface->surface_index < mi->surfaces.size()) {
				const GLES1::MeshInstance::Surface &mis = mi->surfaces[p_surface->surface_index];
				if (mis.vertex_buffer != 0 || !mis.vertex_buffer_fallback.is_empty()) {
					ver = mesh_storage->mesh_instance_surface_get_vertex_arrays_and_format(p_surface->owner->mesh_instance, p_surface->surface_index, p_input_mask, dummy_vao);
					proj_vbo = GLES1_CONFIG->support_vbo ? mis.vertex_buffer : 0;
					proj_ptr = (proj_vbo == 0) ? mis.vertex_buffer_fallback.ptr() : nullptr;
				}
			}
		}

		// Fallback to the static mesh surface if no instance deformation buffer is active.
		if (!ver) {
			ver = mesh_storage->mesh_surface_get_vertex_arrays_and_format(p_surface->surface, p_input_mask, dummy_vao);
			proj_vbo = GLES1_CONFIG->support_vbo ? mesh_storage->mesh_surface_get_vertex_buffer(p_surface->surface) : 0;
			proj_ptr = (proj_vbo == 0) ? mesh_storage->mesh_surface_get_vertex_array(p_surface->surface) : nullptr;
		}

		if (!ver || (proj_vbo == 0 && proj_ptr == nullptr)) {
			// Nothing to do if we find nothing...
			return;
		}

		if (proj_vbo != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, proj_vbo);
		} else if (GLES1_CONFIG->support_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		const GLES1::Mesh::Surface::Attrib &v_attrib = ver->attribs[RS::ARRAY_VERTEX];
		const void *v_ofs_ptr = nullptr;
		if (proj_vbo != 0) {
			v_ofs_ptr = reinterpret_cast<void *>(static_cast<uintptr_t>(v_attrib.offset));
		} else {
			v_ofs_ptr = reinterpret_cast<const void *>(proj_ptr + v_attrib.offset);
		}

		// Route the 3D vertex positions into TU1 (shadow map silhouette) and TU2 (Z-bound gradient)
		// texture coordinates so the GL_TEXTURE matrix projects local vertices into light clip-space.
		glClientActiveTexture(GL_TEXTURE1);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(v_attrib.size, v_attrib.type, v_attrib.stride, v_ofs_ptr);

		if (GLES1_CONFIG->max_texture_units > 2) {
			glClientActiveTexture(GL_TEXTURE2);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(v_attrib.size, v_attrib.type, v_attrib.stride, v_ofs_ptr);
		}

		glClientActiveTexture(GL_TEXTURE0);

		if (proj_vbo != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_rsii_setup_shadow_projection_texcoords: bind projective texcoords");
	}

	_FORCE_INLINE_ void _gl_cleanup_batch_draw_state(bool p_use_palette = false) {
		_gl_set_client_states(0);

		if (GLES1_CONFIG->max_texture_units > 1) {
			if (GLES1_CONFIG->max_texture_units > 2) {
				glClientActiveTexture(GL_TEXTURE2);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			glClientActiveTexture(GL_TEXTURE1);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glClientActiveTexture(GL_TEXTURE0);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_cleanup_batch_draw_state: Texture unit state cleanup");

		if (p_use_palette) {
			glDisableClientState(GL_MATRIX_INDEX_ARRAY_OES);
			glDisableClientState(GL_WEIGHT_ARRAY_OES);
		}

		if (GLES1_CONFIG->support_vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}

		if (bdata.pass_mode != PASS_MODE_SHADOW && bdata.pass_mode != PASS_MODE_SHADOW_PROJECTION) {
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		}
		GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::_gl_cleanup_batch_draw_state: Unbind state");
	}

	// Tracks the chuncked lights
	struct GlActiveLight {
		RS::LightType type = RS::LIGHT_DIRECTIONAL;
		uint32_t index = 0;
	};

	/* UTILITIES */

	struct MultiMeshInstanceData {
		const float *data = nullptr;
		uint32_t stride = 0;
		uint32_t color_offset = 0;
		bool uses_colors = false;
		RS::MultimeshTransformFormat format = RS::MULTIMESH_TRANSFORM_3D;
	};

	struct BatchFVFOffsetsGLES1 {
		size_t pos = 0;
		size_t norm = 0;
		size_t uv = 0;
		size_t col = 0;
		size_t m_idx = 0;
		size_t weight = 0;
	};

	MultiMeshInstanceData _get_multimesh_data(const GeometryInstanceSurface *p_surface);

	_FORCE_INLINE_ void _sky_fetch_colour(const RID &sky_material, const StringName &p_name, Color &r_prop) {
		GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();

		Variant v = material_storage->material_get_param(sky_material, p_name);
		if (v.get_type() == Variant::COLOR) {
			r_prop = v;
		}
	}

	_FORCE_INLINE_ void _sky_fetch_float(const RID &sky_material, const StringName &p_name, const StringName &p_fallback, float &r_prop) {
		GLES1::MaterialStorage *material_storage = GLES1::MaterialStorage::get_singleton();

		Variant v = material_storage->material_get_param(sky_material, p_name);
		if (v.get_type() == Variant::NIL && p_fallback != StringName()) {
			v = material_storage->material_get_param(sky_material, p_fallback);
		}
		if (v.get_type() == Variant::FLOAT) {
			r_prop = v;
		}
	}

	template <bool p_is_batch>
	_FORCE_INLINE_ uint64_t _set_rendering_input_mask(
		GLES1::SceneMaterialData *p_material_data,
		const GeometryInstanceSurface *p_surface,
		BatchType::Batch3D *p_batch = nullptr
	) {
		if (unlikely(!p_material_data || !p_surface)) {
			return 0;
		}
		GLES1::SceneShaderData *shader = p_surface->shader;
		uint64_t input_mask = (shader ? shader->vertex_input_mask : 0ULL) | (1ULL << RS::ARRAY_VERTEX);
		const uint32_t eff_flags = p_is_batch ? p_batch->flags : _batch_get_surface_flags(p_surface);

		if (bdata.pass_mode == PASS_MODE_SHADOW || bdata.pass_mode == PASS_MODE_SHADOW_PROJECTION) {
			input_mask &= ~(1ULL << RS::ARRAY_COLOR);
			if (
				(shader && (shader->uses_alpha_clip || shader->uses_depth_prepass_alpha || shader->uses_discard)) ||
				(eff_flags & BatcherEnums::BTF_3D_USES_ALPHA_CLIP)
			) {
				input_mask |= (1ULL << RS::ARRAY_TEX_UV);
				if (bdata.pass_mode == PASS_MODE_SHADOW && (eff_flags & BatcherEnums::BTF_3D_USES_COLORS)) {
					input_mask |= (1ULL << RS::ARRAY_COLOR);
				}
			}
		} else {
			input_mask |= (1ULL << RS::ARRAY_NORMAL);

			if constexpr (p_is_batch) {
				if (
					(eff_flags & BatcherEnums::BTF_3D_USES_COLORS) &&
					shader &&
					shader->uses_color &&
					!p_surface->color_cache.is_empty()
				) {
					input_mask |= (1ULL << RS::ARRAY_COLOR);
				} else {
					input_mask &= ~(1ULL << RS::ARRAY_COLOR);
				}
			} else {
				if (eff_flags & BatcherEnums::BTF_3D_USES_COLORS) {
					input_mask |= (1ULL << RS::ARRAY_COLOR);
				}
			}

			if (eff_flags & BatcherEnums::BTF_3D_USES_TANGENTS) {
				input_mask |= (1ULL << RS::ARRAY_TANGENT);
			}
			if (eff_flags & BatcherEnums::BTF_3D_USES_UV2) {
				input_mask |= (1ULL << RS::ARRAY_TEX_UV2);
			}

			const GLES1::SceneMaterialData *tex_mat = p_is_batch ? p_material_data : p_surface->material;
			if (
				(tex_mat && tex_mat->texture_cache.size() > 0) ||
				!p_surface->uv_cache.is_empty() ||
				(shader && shader->uses_alpha_clip) ||
				(eff_flags & BatcherEnums::BTF_3D_USES_ALPHA_CLIP)
			) {
				input_mask |= (1ULL << RS::ARRAY_TEX_UV);
			}
		}

		return input_mask;
	}

	_FORCE_INLINE_ AABB _compute_positional_light_aabb(const LightData &p_light) const {
		const float radius = (p_light.inv_radius > 0.0f) ? (1.0f / p_light.inv_radius) : 0.0f;
		const Vector3 pos(p_light.position[0], p_light.position[1], p_light.position[2]);
		const Vector3 ext(radius, radius, radius);
		return AABB(pos - ext, ext * 2.0f);
	}

	_FORCE_INLINE_ void _gl_setup_active_light_and_bounds(const GlActiveLight &p_light, int p_slot_idx, bool &r_has_directional, bool &r_bounds_init, AABB &r_bounds) {
		if (p_light.type == RS::LIGHT_DIRECTIONAL) {
			_gl_setup_directional_light(scene_state.directional_lights[p_light.index], p_slot_idx);
			r_has_directional = true;
		} else {
			const LightData &ld = (p_light.type == RS::LIGHT_OMNI) ? scene_state.omni_lights[p_light.index] : scene_state.spot_lights[p_light.index];
			if (p_light.type == RS::LIGHT_OMNI) {
				_gl_setup_omni_light(ld, p_slot_idx);
			} else {
				_gl_setup_spot_light(ld, p_slot_idx);
			}
			const AABB l_aabb = _compute_positional_light_aabb(ld);
			if (!r_bounds_init) {
				r_bounds = l_aabb;
				r_bounds_init = true;
			} else {
				r_bounds.merge_with(l_aabb);
			}
		}
	}

	_FORCE_INLINE_ bool _is_aabb_culled_by_active_light(const AABB &p_aabb) const {
		if (!scene_state.is_additive_pass || !scene_state.has_active_light_bounds) {
			return false;
		}
		return !scene_state.active_light_bounds.intersects(p_aabb);
	}

	_FORCE_INLINE_ void _record_draw_call_info(RenderingMethod::RenderInfo *r_rendering_info) {
		if (!r_rendering_info) {
			return;
		}

		// Count the draw call
		if (bdata.pass_mode != PASS_MODE_DEPTH) {
			RS::ViewportRenderInfoType info_type;
			if (bdata.pass_mode == PASS_MODE_SHADOW) {
				info_type = RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW;
			} else {
				info_type = RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE;
			}
			r_rendering_info->info[info_type][RS::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
		}
	}

	/* BATCH INLINE UTILS  */

	_FORCE_INLINE_ bool _batch_util_is_depth_pass() const {
		return bdata.pass_mode == PASS_MODE_SHADOW || bdata.pass_mode == PASS_MODE_DEPTH;
	}

	_FORCE_INLINE_ bool _batch_util_is_depth_only_flags(uint32_t p_flags) const {
		return _batch_util_is_depth_pass() && !(p_flags & BatcherEnums::BTF_3D_USES_ALPHA_CLIP);
	}

	_FORCE_INLINE_ bool _batch_util_is_depth_alpha_flags(uint32_t p_flags) const {
		return _batch_util_is_depth_pass() && (p_flags & BatcherEnums::BTF_3D_USES_ALPHA_CLIP) != 0;
	}

	_FORCE_INLINE_ static bool _batch_util_is_depth_only_fvf(BatcherEnums::FVF p_fvf) {
		return p_fvf == BatcherEnums::FVF_3D_DEPTH_MINIMAL || p_fvf == BatcherEnums::FVF_DEPTH_ONLY;
	}

	_FORCE_INLINE_ static bool _batch_util_is_depth_alpha_fvf(BatcherEnums::FVF p_fvf) {
		return p_fvf == BatcherEnums::FVF_3D_DEPTH_ALPHA_MINIMAL || p_fvf == BatcherEnums::FVF_DEPTH_ALPHA;
	}

	_FORCE_INLINE_ static bool _batch_util_is_colored_tier_fvf(BatcherEnums::FVF p_fvf) {
		return (
			p_fvf == BatcherEnums::FVF_3D_TIER2_COLOR ||
			p_fvf == BatcherEnums::FVF_3D_TIER3_TANGENT ||
			p_fvf == BatcherEnums::FVF_3D_TIER4_PALETTE ||
			p_fvf == BatcherEnums::FVF_3D_PALETTE_INSTANCED ||
			p_fvf == BatcherEnums::FVF_REGULAR ||
			p_fvf == BatcherEnums::FVF_INSTANCED
		);
	}

	_FORCE_INLINE_ static BatchFVFOffsetsGLES1 _batch_util_get_fvf_offsets(BatcherEnums::FVF p_fvf) {
		BatchFVFOffsetsGLES1 ofs{};
		if (_batch_util_is_depth_only_fvf(p_fvf)) {
			ofs.pos = offsetof(BatchType::BatchVertex3DDepthMinimal, pos);
			ofs.m_idx = offsetof(BatchType::BatchVertex3DDepthMinimal, matrix_index);
		} else if (_batch_util_is_depth_alpha_fvf(p_fvf)) {
			ofs.pos = offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, pos);
			ofs.uv = offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, uv);
			ofs.col = offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, color);
			ofs.m_idx = offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, matrix_index);
			ofs.weight = offsetof(BatchType::BatchVertex3DDepthAlphaMinimal, weight);
		} else if (p_fvf == BatcherEnums::FVF_3D_TIER1_BASE) {
			ofs.pos = offsetof(BatchType::BatchVertex3DTier1, pos);
			ofs.norm = offsetof(BatchType::BatchVertex3DTier1, normal);
			ofs.uv = offsetof(BatchType::BatchVertex3DTier1, uv);
		} else if (p_fvf == BatcherEnums::FVF_3D_TIER2_COLOR) {
			ofs.pos = offsetof(BatchType::BatchVertex3DTier2, pos);
			ofs.col = offsetof(BatchType::BatchVertex3DTier2, color);
			ofs.norm = offsetof(BatchType::BatchVertex3DTier2, normal);
			ofs.uv = offsetof(BatchType::BatchVertex3DTier2, uv);
		} else if (p_fvf == BatcherEnums::FVF_3D_TIER3_TANGENT) {
			ofs.pos = offsetof(BatchType::BatchVertex3DTier3, pos);
			ofs.col = offsetof(BatchType::BatchVertex3DTier3, color);
			ofs.norm = offsetof(BatchType::BatchVertex3DTier3, normal);
			ofs.uv = offsetof(BatchType::BatchVertex3DTier3, uv);
		} else {
			ofs.pos = offsetof(BatchType::BatchVertex3DTier4, pos);
			ofs.col = offsetof(BatchType::BatchVertex3DTier4, color);
			ofs.norm = offsetof(BatchType::BatchVertex3DTier4, normal);
			ofs.m_idx = offsetof(BatchType::BatchVertex3DTier4, matrix_index);
			ofs.uv = offsetof(BatchType::BatchVertex3DTier4, uv);
			ofs.weight = offsetof(BatchType::BatchVertex3DTier4, weights);
		}
		return ofs;
	}

	/* REST OF GEOMETRY FUNCTIONS */

	static void _geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker);
	static void _geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker);

	SelfList<GeometryInstanceGLES1>::List geometry_instance_dirty_list;

	// Use PagedAllocator instead of RID to maximize performance
	PagedAllocator<GeometryInstanceGLES1> geometry_instance_alloc;
	PagedAllocator<GeometryInstanceSurface> geometry_instance_surface_alloc;

	void _geometry_instance_add_surface_with_material(GeometryInstanceGLES1 *ginstance, uint32_t p_surface, GLES1::SceneMaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh);
	void _geometry_instance_add_surface_with_material_chain(GeometryInstanceGLES1 *ginstance, uint32_t p_surface, GLES1::SceneMaterialData *p_material, RID p_mat_src, RID p_mesh);
	void _geometry_instance_add_surface(GeometryInstanceGLES1 *ginstance, uint32_t p_surface, RID p_material, RID p_mesh);
	void _geometry_instance_update(RenderGeometryInstance *p_geometry_instance);
	void _update_dirty_geometry_instances();
	void _remove_cached_surface(GeometryInstanceSurface *p_surface);

	struct SceneState {
		struct UBO {
			float projection_matrix[16];
			float inv_projection_matrix[16];
			float inv_view_matrix[16];
			float view_matrix[16];

			float main_cam_inv_view_matrix[16];

			float viewport_size[2];
			float screen_pixel_size[2];

			float ambient_light_color_energy[4];

			float ambient_color_sky_mix;
			uint32_t pad2;
			float emissive_exposure_normalization;
			uint32_t use_ambient_light = 0;

			uint32_t use_ambient_cubemap = 0;
			uint32_t use_reflection_cubemap = 0;
			float fog_aerial_perspective;
			float time;

			float radiance_inverse_xform[12];

			uint32_t directional_light_count;
			float z_far;
			float z_near;
			float IBL_exposure_normalization;

			uint32_t fog_enabled;
			uint32_t fog_mode;
			float fog_density;
			float fog_height;

			float fog_height_density;
			float fog_depth_curve;
			float fog_sun_scatter;
			float fog_depth_begin;

			float fog_light_color[3];
			float fog_depth_end;

			float shadow_bias;
			float luminance_multiplier;
			uint32_t camera_visible_layers;
			bool pancake_shadows;
		};
		static_assert(sizeof(UBO) % 16 == 0, "Scene UBO size must be a multiple of 16 bytes");
		static_assert(sizeof(UBO) < 16384, "Scene UBO size must be 16384 bytes or smaller");

		struct MultiviewUBO {
			float projection_matrix_view[RendererSceneRender::MAX_RENDER_VIEWS][16];
			float inv_projection_matrix_view[RendererSceneRender::MAX_RENDER_VIEWS][16];
			float eye_offset[RendererSceneRender::MAX_RENDER_VIEWS][4];
		};
		static_assert(sizeof(MultiviewUBO) % 16 == 0, "Multiview UBO size must be a multiple of 16 bytes");
		static_assert(sizeof(MultiviewUBO) < 16384, "MultiviewUBO size must be 16384 bytes or smaller");

		struct TonemapUBO {
			float exposure = 1.0;
			float white = 1.0;
			int32_t tonemapper = 0;
			int32_t pad = 0;

			int32_t pad2 = 0;
			float brightness = 1.0;
			float contrast = 1.0;
			float saturation = 1.0;
		};
		static_assert(sizeof(TonemapUBO) % 16 == 0, "Tonemap UBO size must be a multiple of 16 bytes");

		UBO ubo;
		MultiviewUBO multiview_ubo;
		TonemapUBO tonemap_ubo;

		bool used_depth_prepass = false;

		GLES1::SceneShaderData::BlendMode current_blend_mode = GLES1::SceneShaderData::BLEND_MODE_MIX;
		RS::CullMode cull_mode = RS::CULL_MODE_BACK;

		bool current_blend_enabled = false;
		bool current_depth_draw_enabled = false;
		bool current_depth_test_enabled = false;
		bool current_scissor_test_enabled = false;

		void reset_gl_state() {
			glDisable(GL_BLEND);
			if (GLES1_CONFIG->support_blend_subtract) {
				glBlendEquationOES(GL_FUNC_ADD_OES);
			}
			if (GLES1_CONFIG->support_blend_func_separate) {
				glBlendFuncSeparateOES(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
			} else {
				glBlendFunc(GL_ONE, GL_ZERO);
			}
			current_blend_enabled = false;

			glDisable(GL_SCISSOR_TEST);
			current_scissor_test_enabled = false;

			glDisable(GL_ALPHA_TEST);
			glAlphaFunc(GL_ALWAYS, 0.0f);

			glCullFace(GL_BACK);
			glEnable(GL_CULL_FACE);
			cull_mode = RS::CULL_MODE_BACK;

			glDepthMask(GL_FALSE);
			current_depth_draw_enabled = false;
			glDisable(GL_DEPTH_TEST);
			glDepthFunc(GL_GEQUAL);
			current_depth_test_enabled = false;

			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glPolygonOffset(0.0f, 0.0f);

			glDisable(GL_FOG);

			const int max_tu = GLES1_CONFIG->max_texture_units;
			for (int i = 0; i < max_tu; i++) {
				if (max_tu > 1) {
					glActiveTexture(GL_TEXTURE0 + i);
					glClientActiveTexture(GL_TEXTURE0 + i);
				}
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glDisable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
				if (GLES1_CONFIG->support_cubemap) {
					glDisable(GL_TEXTURE_CUBE_MAP);
					glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
				}
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			}
			if (max_tu > 1) {
				glActiveTexture(GL_TEXTURE0);
				glClientActiveTexture(GL_TEXTURE0);
			}
			if (GLES1_CONFIG->support_matrix_palette) {
				glDisable(GL_MATRIX_PALETTE_OES);
				glDisableClientState(GL_MATRIX_INDEX_ARRAY_OES);
				glDisableClientState(GL_WEIGHT_ARRAY_OES);
			}
			glDisable(GL_COLOR_MATERIAL);
			glDisable(GL_LIGHTING);
			GL_CHECK_ERROR("GLES1::RasterizerSceneGLES1::reset_gl_state: texture and lighting scrub");
		}

		void set_gl_cull_mode(RS::CullMode p_mode) {
			if (cull_mode != p_mode) {
				if (p_mode == RS::CULL_MODE_DISABLED) {
					glDisable(GL_CULL_FACE);
				} else {
					if (cull_mode == RS::CULL_MODE_DISABLED) {
						// Last time was disabled, so enable and set proper face.
						glEnable(GL_CULL_FACE);
					}
					glCullFace(p_mode == RS::CULL_MODE_FRONT ? GL_FRONT : GL_BACK);
				}
				cull_mode = p_mode;
			}
		}

		void enable_gl_blend(bool p_enabled) {
			if (current_blend_enabled != p_enabled) {
				if (p_enabled) {
					glEnable(GL_BLEND);
				} else {
					glDisable(GL_BLEND);
				}
				current_blend_enabled = p_enabled;
			}
		}

		void enable_gl_scissor_test(bool p_enabled) {
			if (current_scissor_test_enabled != p_enabled) {
				if (p_enabled) {
					glEnable(GL_SCISSOR_TEST);
				} else {
					glDisable(GL_SCISSOR_TEST);
				}
				current_scissor_test_enabled = p_enabled;
			}
		}

		void enable_gl_depth_draw(bool p_enabled) {
			if (current_depth_draw_enabled != p_enabled) {
				glDepthMask(p_enabled ? GL_TRUE : GL_FALSE);
				current_depth_draw_enabled = p_enabled;
			}
		}

		void enable_gl_depth_test(bool p_enabled) {
			if (current_depth_test_enabled != p_enabled) {
				if (p_enabled) {
					glEnable(GL_DEPTH_TEST);
				} else {
					glDisable(GL_DEPTH_TEST);
				}
				current_depth_test_enabled = p_enabled;
			}
		}

		bool texscreen_copied = false;
		bool used_screen_texture = false;
		bool used_normal_texture = false;
		bool used_depth_texture = false;
		bool is_additive_pass = false;
		bool has_active_light_bounds = false;
		AABB active_light_bounds;
		bool immediate_surface_has_colors = false;

		GLuint current_shadow_texture = 0;
		float current_shadow_matrix[16] = {};
		GLuint z_bound_texture = 0;

		uint64_t current_spec_constants = 0;
		Vector2 current_uv_offset = Vector2(0, 0);

		// Default fallback colours
		Color sky_top_color = Color(0.385, 0.454, 0.55, 1.0);
		Color ground_bottom_color = Color(0.2, 0.169, 0.133, 1.0);

		RenderingMethod::RenderInfo *current_render_info = nullptr;

		LightData *omni_lights = nullptr;
		LightData *spot_lights = nullptr;
		ShadowData *positional_shadows = nullptr;

		InstanceSort<GLES1::LightInstance> *omni_light_sort;
		InstanceSort<GLES1::LightInstance> *spot_light_sort;
		uint32_t omni_light_count = 0;
		uint32_t spot_light_count = 0;
		RS::ShadowQuality positional_shadow_quality = RS::ShadowQuality::SHADOW_QUALITY_SOFT_LOW;

		DirectionalLightData *directional_lights = nullptr;
		DirectionalShadowData *directional_shadows = nullptr;
		RS::ShadowQuality directional_shadow_quality = RS::ShadowQuality::SHADOW_QUALITY_SOFT_LOW;
	} scene_state;

	struct RenderListParameters {
		GeometryInstanceSurface **elements = nullptr;
		int element_count = 0;
		bool reverse_cull = false;
		uint64_t spec_constant_base_flags = 0;
		bool force_wireframe = false;
		Vector2 uv_offset = Vector2(0, 0);

		RenderListParameters(GeometryInstanceSurface **p_elements, int p_element_count, bool p_reverse_cull, uint64_t p_spec_constant_base_flags, bool p_force_wireframe = false, Vector2 p_uv_offset = Vector2()) {
			elements = p_elements;
			element_count = p_element_count;
			reverse_cull = p_reverse_cull;
			spec_constant_base_flags = p_spec_constant_base_flags;
			force_wireframe = p_force_wireframe;
			uv_offset = p_uv_offset;
		}
	};

	struct RenderList {
		LocalVector<GeometryInstanceSurface *> elements;

		void clear() {
			elements.clear();
		}

		//should eventually be replaced by radix

		struct SortByKey {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurface *A, const GeometryInstanceSurface *B) const {
				return (A->sort.sort_key2 == B->sort.sort_key2) ? (A->sort.sort_key1 < B->sort.sort_key1) : (A->sort.sort_key2 < B->sort.sort_key2);
			}
		};

		void sort_by_key() {
			SortArray<GeometryInstanceSurface *, SortByKey> sorter{};
			sorter.sort(elements.ptr(), elements.size());
		}

		void sort_by_key_range(uint32_t p_from, uint32_t p_size) {
			SortArray<GeometryInstanceSurface *, SortByKey> sorter{};
			sorter.sort(elements.ptr() + p_from, p_size);
		}

		struct SortByDepth {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurface *A, const GeometryInstanceSurface *B) const {
				return (A->owner->depth < B->owner->depth);
			}
		};

		void sort_by_depth() { //used for shadows

			SortArray<GeometryInstanceSurface *, SortByDepth> sorter{};
			sorter.sort(elements.ptr(), elements.size());
		}

		struct SortByReverseDepthAndPriority {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurface *A, const GeometryInstanceSurface *B) const {
				return (A->sort.priority == B->sort.priority) ? (A->owner->depth > B->owner->depth) : (A->sort.priority < B->sort.priority);
			}
		};

		void sort_by_reverse_depth_and_priority() { //used for alpha

			SortArray<GeometryInstanceSurface *, SortByReverseDepthAndPriority> sorter{};
			sorter.sort(elements.ptr(), elements.size());
		}

		_FORCE_INLINE_ void add_element(GeometryInstanceSurface *p_element) {
			elements.push_back(p_element);
		}
	};

	RenderList render_list[RENDER_LIST_MAX];

	void _setup_lights(const RenderDataGLES1 *p_render_data, bool p_using_shadows, uint32_t &r_directional_light_count, uint32_t &r_omni_light_count, uint32_t &r_spot_light_count, uint32_t &r_directional_shadow_count);
	void _setup_environment(const RenderDataGLES1 *p_render_data, bool p_no_fog, const Size2i &p_screen_size, bool p_flip_y, const Color &p_default_bg_color, bool p_pancake_shadows, float p_shadow_bias = 0.0);

	template <RenderListType p_render_list, PassMode p_pass_mode>
	void _fill_render_list(const RenderDataGLES1 *p_render_data, bool p_append = false);
	void _render_shadows(const RenderDataGLES1 *p_render_data, const Size2i &p_viewport_size = Size2i(1, 1));
	void _render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier = 0, float p_screen_mesh_lod_threshold = 0.0, RenderingMethod::RenderInfo *p_render_info = nullptr, const Size2i &p_viewport_size = Size2i(1, 1), const Transform3D &p_main_cam_transform = Transform3D());
	void _render_post_processing(const RenderDataGLES1 *p_render_data);

	void _setup_base_lighting_pass(const RenderDataGLES1 *p_render_data);

	template <PassMode p_pass_mode>
	void _execute_scene_additive_light_pass(RenderListParameters *p_params, const RenderDataGLES1 *p_render_data, uint32_t p_element_count, bool p_alpha_pass);

	template <PassMode p_pass_mode>
	_FORCE_INLINE_ void _render_list_template(RenderListParameters *p_params, const RenderDataGLES1 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass = false, bool p_replay = false);

	template <PassMode p_pass_mode>
	void _render_additive_light_passes(RenderListParameters *p_params, const RenderDataGLES1 *p_render_data, uint32_t p_element_count, bool p_alpha_pass = false);

	template <PassMode p_pass_mode>
	void _render_light_shadows(RenderListParameters *p_params, const RenderDataGLES1 *p_render_data, uint32_t p_element_count, bool p_alpha_pass, const GlActiveLight &p_light);

	/* BATCH API */

	void scene_render_items_implementation(GeometryInstanceSurface **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent);
	
	void _batch_get_hardware_limits(BatchType::BatchLimits &r_limits);
	void _batch_get_instance_geometry_capacity(const GeometryInstanceSurface *p_surface, uint32_t &r_vertex_count, uint32_t &r_index_count);
	void _batch_resolve_pass_surface_state(const GeometryInstanceSurface *p_surface, GLES1::SceneMaterialData *&r_material, GLES1::SceneShaderData *&r_shader, RS::CullMode &r_cull_mode) const;
	float _batch_get_item_depth(const GeometryInstanceSurface *p_surface, const Transform3D &p_camera_transform);
	uint64_t _batch_compute_canonical_material_id(const GLES1::SceneMaterialData *p_material, const GLES1::SceneShaderData *p_shader) const;
	uint64_t _batch_get_state_hash(const GeometryInstanceSurface *p_surface);
	uint32_t _batch_get_surface_flags(const GeometryInstanceSurface *p_surface) const;
	BatchType::BatchColor _batch_get_surface_modulate(const GeometryInstanceSurface *p_surface) const;
	_FORCE_INLINE_ bool _batch_requires_palette_fvf() const {
		return GLES1_CONFIG->support_matrix_palette && glIsEnabled(GL_MATRIX_PALETTE_OES);
	}
	_FORCE_INLINE_ uint32_t _batch_get_mpi_palette_capacity() const {
		if (GLES1_CONFIG->support_matrix_palette && GLES1_CONFIG->max_palette_matrices > 0) {
			return MIN(static_cast<uint32_t>(GLES1_CONFIG->max_palette_matrices), 32U);
		}
		return 0U;
	}
	bool _batch_can_use_single_mesh_fallback(const GeometryInstanceSurface *p_surface) const;
	_FORCE_INLINE_ bool _batch_buffer_orphan_and_upload_3d(unsigned int p_buffer_size_bytes, unsigned int p_offset_bytes, unsigned int p_data_size_bytes, const void *p_data, GLenum p_target, GLenum p_usage, bool p_optional_orphan) const;

	GLES1::SceneMaterialData *_batch_get_material_data(const GeometryInstanceSurface *p_surface);

	void _batch_prepare_surface_world_cache(const GeometryInstanceSurface *p_surface, const BatchType::BItemRef3D &p_ref, BatchType::TransformMode3D p_effective_mode, bool p_need_tangents);
	void _batch_fill_instance_tiered(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_multimesh_tiered(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_instance_depth(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_multimesh_depth(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_mpi_template(const BatchType::BItemRef3D &p_ref, uint32_t p_palette_count, uint8_t *r_bvs, uint16_t *r_inds);
	void _batch_fill_mpi_depth_template(const BatchType::BItemRef3D &p_ref, uint32_t p_palette_count, uint8_t *r_bvs, uint16_t *r_inds);

	bool _batch_try_reuse_cached_vbo_slot_3d(BatchType::Batch3D &r_batch, uint64_t p_content_hash);
	void _batch_upload_buffers_3d(BatchType::Batch3D &r_batch, uint64_t p_content_hash = 0);
	void _batch_bind_material(GLES1::SceneMaterialData *p_material_data, const Transform3D &p_world_transform, bool p_transparent);
	void _batch_threshold_flush();

	/* BATCH LANES */

	// Lane 1: BRI
	void _batch_render_generic(RS::PrimitiveType p_primitive, uint32_t p_offset = 0, uint32_t p_count = 0, bool p_has_color = true);
	void _batch_render_items(GLES1::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent);

	// Lane 2: SMDD
	void _batch_render_single_mesh_fallback(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, bool p_has_color);

	// Lane 4: SMSG
	void _batch_render_same_mesh_group(GLES1::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent);

	// Lane 5: MPI
	void _batch_render_mpi(GLES1::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent);
	void _batch_util_mpi_flush_chunk(uint32_t &r_chunk_count, GLenum p_primitive_gl, uint32_t p_base_indices, bool p_is_strip, GLuint p_gl_ibo, uint32_t p_ibo_byte_offset);
	void _batch_util_mpi_push_transform(const Transform3D &p_world_xform, const Transform3D &p_view_matrix, const GLES1::SceneShaderData *p_active_shader, uint32_t &r_chunk_count, uint32_t p_cap, GLenum p_primitive_gl, uint32_t p_base_indices, bool p_is_strip, GLuint p_gl_ibo, uint32_t p_ibo_byte_offset);

	// Lane 3: RSII
	void _render_single_item_immediate(const GeometryInstanceSurface *p_surface);

	/* GENERIC BATCH UTILS */
	void _batch_util_bind_surface_static_arrays_and_ibo(const GeometryInstanceSurface *p_surface, uint64_t p_input_mask, bool p_allow_deformed_instance, bool &r_has_indices, bool &r_use_index_buffer, int &r_drawn_count, GLenum &r_index_type, const uint8_t *&r_index_client_ptr);
	void _batch_util_unbind_surface_static_arrays_and_ibo(const GeometryInstanceSurface *p_surface, bool p_use_index_buffer, RS::CullMode p_prev_cull_mode, bool p_prev_depth_draw, bool p_prev_depth_test);
	void _batch_util_resolve_surface_transforms(const Transform3D &p_model_matrix, const Transform3D &p_view_matrix, const GLES1::SceneShaderData *p_shader, Transform3D &r_model_view, Transform3D &r_world_matrix) const;
	Transform3D _batch_util_compute_billboard_modelview(const Transform3D &p_model_matrix, const Transform3D &p_view_matrix, const Transform3D &p_main_cam_inv_view_matrix, const Projection &p_projection, const GLES1::SceneShaderData *p_shader) const;
	void _batch_util_draw_surface_instances(const GeometryInstanceSurface *p_surface, const Transform3D &p_base_transform, const Transform3D &p_view_matrix, int p_instances, const MultiMeshInstanceData *p_mm, bool p_has_indices, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, const uint8_t *p_index_client_ptr);
	void _batch_util_draw_item_ref_instances(const BatchType::BItemRef3D &p_ref, const Transform3D &p_view_matrix, bool p_has_indices, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, const uint8_t *p_index_client_ptr);


	/* BINDING */

	void _bind_scene_camera_uniforms(RID p_version, SceneShaderGLES1::ShaderVariant p_variant, uint64_t p_spec_constants);
	void _bind_sky_directional_lights(RID p_version, SkyShaderGLES1::ShaderVariant p_variant, uint64_t p_spec_constants);

protected:
	double time;
	double time_step = 0;

	bool screen_space_roughness_limiter = false;
	float screen_space_roughness_limiter_amount = 0.25;
	float screen_space_roughness_limiter_limit = 0.18;

	void _render_buffers_debug_draw(Ref<RenderSceneBuffersGLES1> p_render_buffers, RID p_shadow_atlas, GLuint p_fbo);

	/* Camera Attributes */

	struct CameraAttributes {
		float exposure_multiplier = 1.0;
		float exposure_normalization = 1.0;
	};

	bool use_physical_light_units = false;
	mutable RID_Owner<CameraAttributes, true> camera_attributes_owner;

	/* Environment */

	RS::EnvironmentSSAOQuality ssao_quality = RS::ENV_SSAO_QUALITY_MEDIUM;
	bool ssao_half_size = false;
	float ssao_adaptive_target = 0.5;
	int ssao_blur_passes = 2;
	float ssao_fadeout_from = 50.0;
	float ssao_fadeout_to = 300.0;

	bool glow_bicubic_upscale = false;
	RS::EnvironmentSSRRoughnessQuality ssr_roughness_quality = RS::ENV_SSR_ROUGHNESS_QUALITY_LOW;

	bool lightmap_bicubic_upscale = false;

	/* Sky */

	struct SkyGlobals {
		float fog_aerial_perspective = 0.0;
		Color fog_light_color;
		float fog_sun_scatter = 0.0;
		bool fog_enabled = false;
		float fog_density = 0.0;
		float z_far = 0.0;
		uint32_t directional_light_count = 0;

		DirectionalLightData *directional_lights = nullptr;
		DirectionalLightData *last_frame_directional_lights = nullptr;
		uint32_t last_frame_directional_light_count = 0;

		RID shader_default_version;
		RID default_material;
		RID default_shader;
		RID fog_material;
		RID fog_shader;
		GLuint screen_triangle = 0;
		uint32_t max_directional_lights = 4;
		uint32_t roughness_layers = 8;

		float *radiance_verts = nullptr;
		float *radiance_uvw = nullptr;
		uint8_t *radiance_colors = nullptr;
		GLuint radiance_verts_vbo = 0;
		GLuint radiance_uvw_vbo = 0;
		GLuint radiance_colors_vbo = 0;

		float fallback_sky_uvw_cache[12] = {};
		GLuint fallback_sky_uvw_vbo = 0;
	} sky_globals;

	struct Sky {
		// Screen Buffers
		GLuint half_res_pass = 0;
		GLuint half_res_framebuffer = 0;
		GLuint quarter_res_pass = 0;
		GLuint quarter_res_framebuffer = 0;
		Size2i screen_size = Size2i(0, 0);

		// Radiance Cubemap
		GLuint radiance = 0;
		GLuint radiance_framebuffer = 0;
		GLuint raw_radiance = 0;

		RID material;

		int radiance_size = 256;
		int mipmap_count = 1;

		RS::SkyMode mode = RS::SKY_MODE_AUTOMATIC;

		//ReflectionData reflection;
		bool reflection_dirty = false;
		bool dirty = false;
		int processing_layer = 0;
		Sky *dirty_list = nullptr;
		float baked_exposure = 1.0;
		Color ambient_fallback = Color(0.469, 0.483, 0.505, 1.0);

		//State to track when radiance cubemap needs updating
		GLES1::SkyMaterialData *prev_material = nullptr;
		Vector3 prev_position = Vector3(0.0, 0.0, 0.0);
		float prev_time = 0.0f;

		//Uniform parameters
		bool material_cache_dirty = true;
		Color sky_top_color = Color(0.385, 0.454, 0.55, 1.0);
		Color sky_horizon_color = Color(0.646, 0.656, 0.67, 1.0);
		float sky_curve = 0.15f;
		float sky_energy = 1.0f;

		Color ground_bottom_color = Color(0.2, 0.169, 0.133, 1.0);
		Color ground_horizon_color = Color(0.646, 0.656, 0.67, 1.0);
		float ground_curve = 0.02f;
		float ground_energy = 1.0f;

		float exposure = 1.0f;
		float sky_uvw_cache[12];
		GLuint sky_uvw_vbo = 0;
	};

	Sky *dirty_sky_list = nullptr;
	mutable RID_Owner<Sky, true> sky_owner;

	void _setup_sky(const RenderDataGLES1 *p_render_data, const PagedArray<RID> &p_lights, const Projection &p_projection, const Transform3D &p_transform, const Size2i p_screen_size);
	void _invalidate_sky(Sky *p_sky);
	void _update_dirty_skys();
	void _update_sky_radiance(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier);
	void _draw_sky(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier, float p_luminance_multiplier, bool p_use_multiview, bool p_flip_y, bool p_apply_color_adjustments_in_post);
	void _free_sky_data(Sky *p_sky);

	// Needed for a single argument calls (material and uv2).
	PagedArrayPool<RenderGeometryInstance *> cull_argument_pool;
	PagedArray<RenderGeometryInstance *> cull_argument;

public:
	static RasterizerSceneGLES1 *get_singleton() { return singleton; }

	void initialize();

	RasterizerCanvasGLES1 *canvas = nullptr;

	RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;

	uint32_t geometry_instance_get_pair_mask() override;

	/* PIPELINES */

	virtual void mesh_generate_pipelines(RID p_mesh, bool p_background_compilation) override {}
	virtual uint32_t get_pipeline_compilations(RS::PipelineSource p_source) override { return 0; }

	/* SDFGI UPDATE */

	void sdfgi_update(const Ref<RenderSceneBuffers> &p_render_buffers, RID p_environment, const Vector3 &p_world_position) override {}
	int sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_render_buffers) const override {
		return 0;
	}
	AABB sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override {
		return AABB();
	}
	uint32_t sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override {
		return 0;
	}

	/* SKY API */

	RID sky_allocate() override;
	void sky_initialize(RID p_rid) override;
	void sky_set_radiance_size(RID p_sky, int p_radiance_size) override;
	void sky_set_mode(RID p_sky, RS::SkyMode p_mode) override;
	void sky_set_material(RID p_sky, RID p_material) override;
	Ref<Image> sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) override;
	float sky_get_baked_exposure(RID p_sky) const;

	/* ENVIRONMENT API */

	void environment_glow_set_use_bicubic_upscale(bool p_enable) override;

	void environment_set_ssr_roughness_quality(RS::EnvironmentSSRRoughnessQuality p_quality) override;

	void environment_set_ssao_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;

	void environment_set_ssil_quality(RS::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;

	void environment_set_sdfgi_ray_count(RS::EnvironmentSDFGIRayCount p_ray_count) override;
	void environment_set_sdfgi_frames_to_converge(RS::EnvironmentSDFGIFramesToConverge p_frames) override;
	void environment_set_sdfgi_frames_to_update_light(RS::EnvironmentSDFGIFramesToUpdateLight p_update) override;

	void environment_set_volumetric_fog_volume_size(int p_size, int p_depth) override;
	void environment_set_volumetric_fog_filter_active(bool p_enable) override;

	Ref<Image> environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) override;

	_FORCE_INLINE_ bool is_using_physical_light_units() {
		return use_physical_light_units;
	}

	void positional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) override;
	void directional_soft_shadow_filter_set_quality(RS::ShadowQuality p_quality) override;

	RID fog_volume_instance_create(RID p_fog_volume) override;
	void fog_volume_instance_set_transform(RID p_fog_volume_instance, const Transform3D &p_transform) override;
	void fog_volume_instance_set_active(RID p_fog_volume_instance, bool p_active) override;
	RID fog_volume_instance_get_volume(RID p_fog_volume_instance) const override;
	Vector3 fog_volume_instance_get_position(RID p_fog_volume_instance) const override;

	RID voxel_gi_instance_create(RID p_voxel_gi) override;
	void voxel_gi_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform) override;
	bool voxel_gi_needs_update(RID p_probe) const override;
	void voxel_gi_update(RID p_probe, bool p_update_light_instances, const Vector<RID> &p_light_instances, const PagedArray<RenderGeometryInstance *> &p_dynamic_objects) override;

	void voxel_gi_set_quality(RS::VoxelGIQuality) override;

	void render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, const RenderSDFGIUpdateData *p_sdfgi_update_data = nullptr, RenderingMethod::RenderInfo *r_render_info = nullptr) override;
	void render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) override;
	void render_particle_collider_heightfield(RID p_collider, const Transform3D &p_transform, const PagedArray<RenderGeometryInstance *> &p_instances) override;

	void set_scene_pass(uint64_t p_pass) override {
		scene_pass = p_pass;
	}

	_FORCE_INLINE_ uint64_t get_scene_pass() {
		return scene_pass;
	}

	void set_time(double p_time, double p_step) override;
	void set_debug_draw_mode(RS::ViewportDebugDraw p_debug_draw) override;
	_FORCE_INLINE_ RS::ViewportDebugDraw get_debug_draw_mode() const {
		return debug_draw;
	}

	Ref<RenderSceneBuffers> render_buffers_create() override;
	void gi_set_use_half_resolution(bool p_enable) override;

	void screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_curve) override;
	bool screen_space_roughness_limiter_is_active() const override;

	void sub_surface_scattering_set_quality(RS::SubSurfaceScatteringQuality p_quality) override;
	void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) override;

	TypedArray<Image> bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) override;
	void _render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, GLuint p_framebuffer, const Rect2i &p_region, const GLuint p_color_textures[4], const RID p_color_tex_rids[4] = nullptr);

	bool free(RID p_rid) override;
	void update() override;
	void sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) override;

	void decals_set_filter(RS::DecalFilter p_filter) override;
	void light_projectors_set_filter(RS::LightProjectorFilter p_filter) override;
	virtual void lightmaps_set_bicubic_filter(bool p_enable) override;

	RasterizerSceneGLES1();
	~RasterizerSceneGLES1();
};

#endif // GLES1_ENABLED

#endif // RASTERIZER_SCENE_GLES1_H
