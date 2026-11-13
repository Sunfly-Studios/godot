/**************************************************************************/
/*  rasterizer_scene_gles2.h                                              */
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

#ifndef RASTERIZER_SCENE_GLES2_H
#define RASTERIZER_SCENE_GLES2_H

#ifdef GLES2_ENABLED

#include "core/math/projection.h"
#include "core/templates/paged_allocator.h"
#include "core/templates/rid_owner.h"
#include "core/templates/self_list.h"
#include "drivers/rasterizer_common/batch/rasterizer_scene_batcher_common.h"
#include "drivers/gles_common/common_math.h"
#include "drivers/gles2/shaders/effects/cubemap_filter.glsl.gen.h"
#include "drivers/gles2/shaders/sky.glsl.gen.h"
#include "scene/resources/mesh.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_scene_render.h"
#include "servers/rendering_server.h"
#include "compiler/shader_gles2.h"
#include "storage/light_storage.h"
#include "storage/material_storage.h"
#include "storage/mesh_storage.h"
#include "storage/render_scene_buffers_gles2.h"
#include "storage/utilities.h"

struct RenderDataGLES2 {
	Ref<RenderSceneBuffersGLES2> render_buffers;
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

class RasterizerCanvasGLES2;
class RasterizerSceneGLES2;

struct BatcherAPISceneGLES2 {
	using Scene = RasterizerSceneGLES2;
	using MaterialData = GLES2::SceneMaterialData;
	using MeshStorage = GLES2::MeshStorage;
	using TextureStorage = GLES2::TextureStorage;
	using Texture = GLES2::Texture;
	using CanvasTexture = GLES2::CanvasTexture;
	using Shader = GLES2::Shader;
	using MeshInstance = GLES2::MeshInstance;

	static constexpr bool FORCE_BAKE_MODULATE = false;
};

class RasterizerSceneGLES2 : public RendererSceneRender, public RasterizerSceneBatcherCommon<BatcherAPISceneGLES2> {
	friend class RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>;

public:
	enum RenderListType {
		RENDER_LIST_OPAQUE, //used for opaque objects
		RENDER_LIST_ALPHA, //used for transparent objects
		RENDER_LIST_SECONDARY, //used for shadows and other objects
		RENDER_LIST_MAX
	};

	using PassMode = BatcherEnums::PassMode;
	using BatchType = RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>;

	static constexpr PassMode PASS_MODE_COLOR = BatcherEnums::PASS_MODE_COLOR;
	static constexpr PassMode PASS_MODE_COLOR_TRANSPARENT = BatcherEnums::PASS_MODE_COLOR_TRANSPARENT;
	static constexpr PassMode PASS_MODE_SHADOW = BatcherEnums::PASS_MODE_SHADOW;
	static constexpr PassMode PASS_MODE_DEPTH = BatcherEnums::PASS_MODE_DEPTH;
	static constexpr PassMode PASS_MODE_MATERIAL = BatcherEnums::PASS_MODE_MATERIAL;

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
	static RasterizerSceneGLES2 *singleton;
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

		float world_position[3];
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

	class GeometryInstanceGLES2;

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

		void *surface = nullptr;
		GLES2::SceneShaderData *shader = nullptr;
		GLES2::SceneMaterialData *material = nullptr;

		void *surface_shadow = nullptr;
		GLES2::SceneShaderData *shader_shadow = nullptr;
		GLES2::SceneMaterialData *material_shadow = nullptr;

		GeometryInstanceSurface *next = nullptr;
		GeometryInstanceGLES2 *owner = nullptr;
	};

	struct GeometryInstanceLightmapSH {
		Color sh[9];
	};

	class GeometryInstanceGLES2 : public RenderGeometryInstanceBase {
	public:
		//used during rendering
		bool store_transform_cache = true;

		int32_t instance_count = 0;

		bool can_sdfgi = false;
		bool using_projectors = false;
		bool using_softshadows = false;

		uint32_t paired_omni_light_count = 0;
		uint32_t paired_spot_light_count = 0;
		LocalVector<RID> paired_omni_lights;
		LocalVector<RID> paired_spot_lights;
		LocalVector<uint32_t> omni_light_gl_cache;
		LocalVector<uint32_t> spot_light_gl_cache;

		LocalVector<RID> paired_reflection_probes;

		RID skeleton;
		RID lightmap_instance;
		Rect2 lightmap_uv_scale;
		uint32_t lightmap_slice_index;
		GeometryInstanceLightmapSH *lightmap_sh = nullptr;

		// Used during setup.
		GeometryInstanceSurface *surface_caches = nullptr;
		SelfList<GeometryInstanceGLES2> dirty_list_element;

		GeometryInstanceGLES2() :
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

	/* INLINE GL HELPERS */

	// Forward the structs;
	struct MultiMeshInstanceData;

	_FORCE_INLINE_ void _gl_setup_bake_uv2_tex_params() {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

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

	_FORCE_INLINE_ void _gl_reset_instancing_attributes() {
		if (!scene_state.instancing_attribs_dirty) {
			return;
		}
		glDisableVertexAttribArray(12);
		glDisableVertexAttribArray(13);
		glDisableVertexAttribArray(14);
		glDisableVertexAttribArray(15);
		glVertexAttrib4f(12, 1.0f, 0.0f, 0.0f, 0.0f);
		glVertexAttrib4f(13, 0.0f, 1.0f, 0.0f, 0.0f);
		glVertexAttrib4f(14, 0.0f, 0.0f, 1.0f, 0.0f);
		glVertexAttrib4f(15, 1.0f, 1.0f, 1.0f, 1.0f);
		scene_state.instancing_attribs_dirty = false;
	}

	/* UTILITIES */

	struct MultiMeshInstanceData {
		const float *data = nullptr;
		uint32_t stride = 0;
		uint32_t color_offset = 0;
		bool uses_colors = false;
		RS::MultimeshTransformFormat format = RS::MULTIMESH_TRANSFORM_3D;
	};

	enum AdditiveLightType {
		ADDITIVE_LIGHT_DIRECTIONAL,
		ADDITIVE_LIGHT_OMNI,
		ADDITIVE_LIGHT_SPOT
	};

	MultiMeshInstanceData _get_multimesh_data(const GeometryInstanceSurface *p_surface);

	_FORCE_INLINE_ AABB _compute_positional_light_aabb(const LightData &p_light) const {
		const float radius = (p_light.inv_radius > 0.0f) ? (1.0f / p_light.inv_radius) : 0.0f;
		const Vector3 pos(p_light.world_position[0], p_light.world_position[1], p_light.world_position[2]);
		const Vector3 ext(radius, radius, radius);
		return AABB(pos - ext, ext * 2.0f);
	}

	_FORCE_INLINE_ bool _is_aabb_culled_by_positional_light(const AABB &p_aabb, const LightData &p_light) const {
		return !_compute_positional_light_aabb(p_light).intersects(p_aabb);
	}

	void _record_draw_call_info(RenderingMethod::RenderInfo *r_rendering_info) {
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

	_FORCE_INLINE_ static bool _shader_prevents_vertex_baking(const GLES2::SceneShaderData *p_shader) {
		return p_shader && (
			p_shader->uses_vertex ||
			p_shader->uses_position ||
			p_shader->uses_world_coordinates ||
			p_shader->writes_modelview_or_projection
		);
	}

	_FORCE_INLINE_ uint64_t _resolve_effective_vertex_input_mask(const GLES2::SceneShaderData *p_shader, bool p_is_depth_only = false, bool p_is_depth_alpha = false) const {
		uint64_t input_mask = p_shader ? p_shader->vertex_input_mask : 0xFFFFFFFFULL;
		if (bdata.pass_mode == PASS_MODE_MATERIAL) {
			input_mask |= (1ULL << RS::ARRAY_TEX_UV2);
		}
		if (p_is_depth_only) {
			input_mask &= (1ULL << RS::ARRAY_VERTEX);
		} else if (p_is_depth_alpha) {
			input_mask &= ((1ULL << RS::ARRAY_VERTEX) | (1ULL << RS::ARRAY_TEX_UV));
		}
		return input_mask;
	}

	_FORCE_INLINE_ SceneShaderGLES2::ShaderVariant _get_shader_variant(bool p_promote_to_instancing) const {
		if (_batch_util_is_depth_pass()) {
			return p_promote_to_instancing ? SceneShaderGLES2::MODE_DEPTH_INSTANCING : SceneShaderGLES2::MODE_DEPTH;
		} else if (scene_state.is_directional_additive_replay) {
			return p_promote_to_instancing ? SceneShaderGLES2::MODE_ADDITIVE_DIRECTIONAL_INSTANCING : SceneShaderGLES2::MODE_ADDITIVE_DIRECTIONAL;
		}
		return p_promote_to_instancing ? SceneShaderGLES2::MODE_COLOR_INSTANCING : SceneShaderGLES2::MODE_COLOR;
	}

	_FORCE_INLINE_ SceneShaderGLES2::ShaderVariant _get_batch_shader_variant() const {
		const uint32_t batch_flags = _render_item_state.curr_batch ? _render_item_state.curr_batch->flags : 0U;
		return _get_shader_variant(_batch_util_promote_to_instancing(bdata.fvf, batch_flags));
	}

	_FORCE_INLINE_ uint64_t _get_pass_spec_constants() const {
		uint64_t spec_constants = scene_state.current_spec_constants;
		if (bdata.pass_mode == PASS_MODE_MATERIAL) {
			spec_constants |= SceneShaderGLES2::RENDER_MATERIAL;
		} else if (scene_state.is_directional_additive_replay) {
			spec_constants = ((spec_constants & ~SceneShaderGLES2::BASE_PASS) & ~SceneShaderGLES2::DISABLE_LIGHT_DIRECTIONAL) | SceneShaderGLES2::USE_ADDITIVE_LIGHTING;
		}
		return spec_constants;
	}

	// Invalidates the state hash cache if it has been mutated
	// so any subsequent single-mesh direct draw fallback or bypassed immediate item
	// rebinds its shader program, uniforms, and render states cleanly.
	_FORCE_INLINE_ void _invalidate_state_if_additive_mutated() {
		if (scene_state.additive_pass_mutated_state) {
			_render_item_state.current_state_hash = 0;
			_render_item_state.current_material_data = nullptr;
			scene_state.additive_pass_mutated_state = false;
		}
	}

	/* BATCH UTILITIES  */

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

	_FORCE_INLINE_ static bool _batch_util_has_vertex_colors(BatcherEnums::FVF p_fvf, uint32_t p_batch_flags) {
		return (
			(p_fvf == BatcherEnums::FVF_UNBATCHED && (p_batch_flags & BatcherEnums::BTF_3D_USES_COLORS) != 0) ||
			p_fvf == BatcherEnums::FVF_3D_TIER2_COLOR ||
			p_fvf == BatcherEnums::FVF_3D_TIER3_TANGENT ||
			p_fvf == BatcherEnums::FVF_3D_TIER4_PALETTE ||
			p_fvf == BatcherEnums::FVF_REGULAR ||
			p_fvf == BatcherEnums::FVF_INSTANCED
		);
	}

	_FORCE_INLINE_ bool _batch_util_promote_to_instancing(BatcherEnums::FVF p_fvf, uint32_t p_flags) const {
		if (p_fvf == BatcherEnums::FVF_STATIC_MESH_LOOP) {
			return (p_flags & BatcherEnums::BTF_3D_USES_INSTANCING) != 0;
		}
		return p_fvf == BatcherEnums::FVF_INSTANCED;
	}

	_FORCE_INLINE_ SceneShaderGLES2::ShaderVariant _batch_util_get_batch_shader_variant() {
		const bool promote_to_instancing = _batch_util_promote_to_instancing(bdata.fvf, _render_item_state.curr_batch->flags);
		SceneShaderGLES2::ShaderVariant variant = SceneShaderGLES2::MODE_COLOR;

		if (bdata.pass_mode == PASS_MODE_SHADOW || bdata.pass_mode == PASS_MODE_DEPTH) {
			if (promote_to_instancing) {
				variant = SceneShaderGLES2::MODE_DEPTH_INSTANCING;
			} else {
				variant = SceneShaderGLES2::MODE_DEPTH;
			}
		} else if (scene_state.is_directional_additive_replay) {
			if (promote_to_instancing) {
				variant = SceneShaderGLES2::MODE_ADDITIVE_DIRECTIONAL_INSTANCING;
			} else {
				variant = SceneShaderGLES2::MODE_ADDITIVE_DIRECTIONAL;
			}
		} else {
			if (promote_to_instancing) {
				variant = SceneShaderGLES2::MODE_COLOR_INSTANCING;
			} else {
				variant = SceneShaderGLES2::MODE_COLOR;
			}
		}
		return variant;
	}

	_FORCE_INLINE_ void _batch_util_begin_additive_light_gl_state(bool &r_was_blend, GLES2::SceneShaderData::BlendMode &r_was_blend_mode, bool &r_was_depth_draw) {
		r_was_blend = scene_state.current_blend_enabled;
		r_was_blend_mode = scene_state.current_blend_mode;
		r_was_depth_draw = scene_state.current_depth_draw_enabled;

		scene_state.enable_gl_blend(true);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		GL_CHECK_ERROR("GLES2::RasterizerSceneGLES2::_begin_additive_light_gl_state: glBlendFuncSeparate additive");
		scene_state.enable_gl_depth_draw(false);
		glDepthFunc(GL_GEQUAL);
	}

	_FORCE_INLINE_ void _batch_util_end_additive_light_gl_state(bool p_was_blend, GLES2::SceneShaderData::BlendMode p_was_blend_mode, bool p_was_depth_draw) {
		scene_state.enable_gl_blend(p_was_blend);
		if (p_was_blend) {
			scene_state.current_blend_mode = GLES2::SceneShaderData::BLEND_MODE_ADD;
			scene_state.set_gl_blend_mode(p_was_blend_mode);
		}
		scene_state.enable_gl_depth_draw(p_was_depth_draw);
	}

	template <AdditiveLightType p_additive_light_type>
	_FORCE_INLINE_ void _batch_util_collect_surface_unique_lights(const GeometryInstanceGLES2 *p_owner, const AABB &p_cull_aabb, LocalVector<uint32_t> &r_unique_lights) const {
		const LocalVector<uint32_t> &cache = (p_additive_light_type == ADDITIVE_LIGHT_OMNI) ? p_owner->omni_light_gl_cache : p_owner->spot_light_gl_cache;
		const LightData *lights = (p_additive_light_type == ADDITIVE_LIGHT_OMNI) ? scene_state.omni_lights : scene_state.spot_lights;
		const uint32_t max_count = (p_additive_light_type == ADDITIVE_LIGHT_OMNI) ? scene_state.omni_light_count : scene_state.spot_light_count;

		const uint32_t count = cache.size();
		for (uint32_t l = 0; l < count; l++) {
			const uint32_t gl_id = cache[l];
			if (
				gl_id < max_count &&
				!r_unique_lights.has(gl_id) &&
				!_is_aabb_culled_by_positional_light(p_cull_aabb, lights[gl_id])
			) {
				r_unique_lights.push_back(gl_id);
			}
		}
	}

	template <AdditiveLightType p_additive_light_type, bool p_is_batch>
	_FORCE_INLINE_ void _batch_util_get_unique_light_vector(
		LocalVector<uint32_t> &r_unique_lights,
		const GeometryInstanceSurface *p_surf = nullptr
	) {
		r_unique_lights.clear();

		if constexpr (p_is_batch) {
			if (_render_item_state.curr_batch) {
				const BatchType::Batch3D &batch = *_render_item_state.curr_batch;
				for (uint32_t r = 0; r < batch.num_item_refs; r++) {
					const BatchType::BItemRef3D &ref = bdata.item_refs[batch.first_item_ref + r];
					const GeometryInstanceSurface *surf = static_cast<const GeometryInstanceSurface *>(ref.surface);
					if (!surf || !surf->owner || ref.instance_count == 0) {
						continue;
					}
					_batch_util_collect_surface_unique_lights<p_additive_light_type>(surf->owner, batch.bounding_aabb, r_unique_lights);
				}
			}
		} else if (p_surf && p_surf->owner) {
			_batch_util_collect_surface_unique_lights<p_additive_light_type>(p_surf->owner, p_surf->owner->transformed_aabb, r_unique_lights);
		}
	}

	/* REST OF GEOMETRY FUNCTIONS */

	static void _geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker);
	static void _geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker);

	SelfList<GeometryInstanceGLES2>::List geometry_instance_dirty_list;

	// Use PagedAllocator instead of RID to maximize performance
	PagedAllocator<GeometryInstanceGLES2> geometry_instance_alloc;
	PagedAllocator<GeometryInstanceSurface> geometry_instance_surface_alloc;

	void _geometry_instance_add_surface_with_material(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh);
	void _geometry_instance_add_surface_with_material_chain(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, GLES2::SceneMaterialData *p_material, RID p_mat_src, RID p_mesh);
	void _geometry_instance_add_surface(GeometryInstanceGLES2 *ginstance, uint32_t p_surface, RID p_material, RID p_mesh);
	void _geometry_instance_update(RenderGeometryInstance *p_geometry_instance);
	void _update_dirty_geometry_instances();

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

		struct ProgramCameraStateKey {
			uint64_t version_id = 0;
			uint64_t spec_constants = 0;
			uint32_t variant = 0;
			uint32_t generation = 0;
		};

		UBO ubo;
		GLuint ubo_buffer = 0;
		MultiviewUBO multiview_ubo;
		GLuint multiview_buffer = 0;
		TonemapUBO tonemap_ubo;
		GLuint tonemap_buffer = 0;

		Projection cached_projection;
		Projection cached_inv_projection;
		Transform3D cached_view_matrix;
		Transform3D cached_inv_view_matrix;
		Transform3D cached_main_cam_inv_view_matrix;
		Basis cached_radiance_inverse_xform;
		Projection cached_directional_shadow_matrices[4];

		bool used_depth_prepass = false;

		GLES2::SceneShaderData::BlendMode current_blend_mode = GLES2::SceneShaderData::BLEND_MODE_MIX;
		RS::CullMode cull_mode = RS::CULL_MODE_BACK;

		bool current_blend_enabled = false;
		bool current_depth_draw_enabled = false;
		bool current_depth_test_enabled = false;
		bool current_scissor_test_enabled = false;

		// VBO, IBO, attribute mask and FVF layout tracker(s).
		GLuint current_array_buffer = 0;
		GLuint current_element_array_buffer = 0;
		uint32_t enabled_vertex_attrib_mask = 0;
		bool instancing_attribs_dirty = true;

		void bind_gl_array_buffer(GLuint p_buffer) {
			if (current_array_buffer != p_buffer) {
				glBindBuffer(GL_ARRAY_BUFFER, p_buffer);
				current_array_buffer = p_buffer;
			}
		}

		void bind_gl_element_array_buffer(GLuint p_buffer) {
			if (current_element_array_buffer != p_buffer) {
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_buffer);
				current_element_array_buffer = p_buffer;
			}
		}

		void set_gl_vertex_attrib_mask(uint32_t p_mask) {
			uint32_t diff = enabled_vertex_attrib_mask ^ p_mask;
			if (diff == 0) {
				return;
			}
			for (uint32_t i = 0; i < RS::ARRAY_INDEX; i++) {
				const uint32_t bit = (1U << i);
				if (diff & bit) {
					if (p_mask & bit) {
						glEnableVertexAttribArray(i);
					} else {
						glDisableVertexAttribArray(i);
					}
				}
			}
			enabled_vertex_attrib_mask = p_mask;
		}

		void reset_gl_vertex_buffer_state() {
			if (GLES2_CONFIG->support_vao) {
				glBindVertexArray(0);
			}
			for (uint32_t i = 0; i < RS::ARRAY_INDEX; i++) {
				glDisableVertexAttribArray(i);
			}
			enabled_vertex_attrib_mask = 0;
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			current_array_buffer = 0;
			current_element_array_buffer = 0;
			instancing_attribs_dirty = true;
			glVertexAttrib4f(RS::ARRAY_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
		}

		void reset_gl_state() {
			glDisable(GL_BLEND);
			current_blend_enabled = false;
			current_blend_mode = GLES2::SceneShaderData::BLEND_MODE_MIX;

			glBlendEquation(GL_FUNC_ADD);
			if (transparent_bg) {
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			} else {
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
			}

			glDisable(GL_SCISSOR_TEST);
			current_scissor_test_enabled = false;

			glCullFace(GL_BACK);
			glEnable(GL_CULL_FACE);
			cull_mode = RS::CULL_MODE_BACK;

			glDepthMask(GL_FALSE);
			current_depth_draw_enabled = false;
			glDisable(GL_DEPTH_TEST);
			current_depth_test_enabled = false;

			current_array_buffer = 0;
			current_element_array_buffer = 0;
			enabled_vertex_attrib_mask = 0;
			instancing_attribs_dirty = true;
		}

		void set_gl_blend_mode(GLES2::SceneShaderData::BlendMode p_mode) {
			if (current_blend_mode == p_mode) {
				return;
			}
			switch (p_mode) {
				case GLES2::SceneShaderData::BLEND_MODE_MIX:
				case GLES2::SceneShaderData::BLEND_MODE_ALPHA_TO_COVERAGE: {
					glBlendEquation(GL_FUNC_ADD);
					if (transparent_bg) {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					}
				} break;
				case GLES2::SceneShaderData::BLEND_MODE_ADD: {
					glBlendEquation(GL_FUNC_ADD);
					glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
				} break;
				case GLES2::SceneShaderData::BLEND_MODE_SUB: {
					glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
					glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
				} break;
				case GLES2::SceneShaderData::BLEND_MODE_MUL: {
					glBlendEquation(GL_FUNC_ADD);
					glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
				} break;
				case GLES2::SceneShaderData::BLEND_MODE_PREMULT_ALPHA: {
					glBlendEquation(GL_FUNC_ADD);
					if (transparent_bg) {
						glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					} else {
						glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
					}
				} break;
			}
			current_blend_mode = p_mode;
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

		bool check_and_mark_program_camera_bound(RID p_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants) {
			// Verify that this pass actually requires uniforms to be
			// binded.
			// 
			// Improves performance by not rebinding all of the 30+ uniforms
			// in every single pass.
			const uint64_t ver_id = p_version.get_id();
			const uint32_t var_id = static_cast<uint32_t>(p_variant);
			ProgramCameraStateKey *keys = program_camera_state_cache.ptr();
			const uint32_t count = program_camera_state_cache.size();

			for (uint32_t i = 0; i < count; i++) {
				if (
					keys[i].version_id == ver_id &&
					keys[i].variant == var_id &&
					keys[i].spec_constants == p_spec_constants
				) {
					if (keys[i].generation == camera_pass_generation) {
						return true;
					}
					keys[i].generation = camera_pass_generation;
					return false;
				}
			}

			ProgramCameraStateKey new_key;
			new_key.version_id = ver_id;
			new_key.spec_constants = p_spec_constants;
			new_key.variant = var_id;
			new_key.generation = camera_pass_generation;
			program_camera_state_cache.push_back(new_key);
			return false;
		}

		bool texscreen_copied = false;
		bool used_screen_texture = false;
		bool used_normal_texture = false;
		bool used_depth_texture = false;
		uint64_t current_spec_constants = 0;
		SceneShaderGLES2::ShaderVariant current_shader_variant = SceneShaderGLES2::MODE_COLOR;
		bool additive_pass_mutated_state = false;
		bool is_directional_additive_replay = false;
		Vector2 current_uv_offset = Vector2(0, 0);
		bool transparent_bg = false;
		RenderingMethod::RenderInfo *current_render_info = nullptr;

		uint32_t camera_pass_generation = 0;
		LocalVector<ProgramCameraStateKey> program_camera_state_cache;

		LightData *omni_lights = nullptr;
		LightData *spot_lights = nullptr;
		ShadowData *positional_shadows = nullptr;

		LocalVector<uint32_t> group_omni_lights_scratch;
		LocalVector<uint32_t> group_spot_lights_scratch;

		InstanceSort<GLES2::LightInstance> *omni_light_sort;
		InstanceSort<GLES2::LightInstance> *spot_light_sort;
		GLuint omni_light_buffer = 0;
		GLuint spot_light_buffer = 0;
		GLuint positional_shadow_buffer = 0;
		uint32_t omni_light_count = 0;
		uint32_t spot_light_count = 0;
		RS::ShadowQuality positional_shadow_quality = RS::ShadowQuality::SHADOW_QUALITY_SOFT_LOW;

		DirectionalLightData *directional_lights = nullptr;
		GLuint directional_light_buffer = 0;
		DirectionalShadowData *directional_shadows = nullptr;
		GLuint directional_shadow_buffer = 0;
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
				if (A->sort.priority == B->sort.priority) {
					if (A->owner->depth == B->owner->depth) {
						return (A->sort.sort_key2 == B->sort.sort_key2) ? (A->sort.sort_key1 < B->sort.sort_key1) : (A->sort.sort_key2 < B->sort.sort_key2);
					}
					return A->owner->depth > B->owner->depth;
				}
				return A->sort.priority < B->sort.priority;
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

	void _setup_lights(const RenderDataGLES2 *p_render_data, bool p_using_shadows, uint32_t &r_directional_light_count, uint32_t &r_omni_light_count, uint32_t &r_spot_light_count, uint32_t &r_directional_shadow_count);
	void _setup_environment(const RenderDataGLES2 *p_render_data, bool p_no_fog, const Size2i &p_screen_size, bool p_flip_y, const Color &p_default_bg_color, bool p_pancake_shadows, float p_shadow_bias = 0.0);

	template <RenderListType p_render_list, PassMode p_pass_mode>
	void _fill_render_list(const RenderDataGLES2 *p_render_data, bool p_append = false);
	void _render_shadows(const RenderDataGLES2 *p_render_data, const Size2i &p_viewport_size = Size2i(1, 1));
	void _render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier = 0, float p_screen_mesh_lod_threshold = 0.0, RenderingMethod::RenderInfo *p_render_info = nullptr, const Size2i &p_viewport_size = Size2i(1, 1), const Transform3D &p_main_cam_transform = Transform3D());
	void _render_post_processing(const RenderDataGLES2 *p_render_data);

	template <PassMode p_pass_mode>
	_FORCE_INLINE_ void _render_list_template(RenderListParameters *p_params, const RenderDataGLES2 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass = false, bool p_replay = false);

	template <PassMode p_pass_mode>
	_FORCE_INLINE_ void _render_directional_light_replay_pass(RenderListParameters *p_params, const RenderDataGLES2 *p_render_data, uint32_t p_from_element, uint32_t p_to_element, bool p_alpha_pass = false);

	// TODO: This is ugly. To prettify later.
	struct BatchBoundGeometryStateGLES2;

	template <AdditiveLightType p_light_type, bool p_is_batch>
	_FORCE_INLINE_ void _execute_additive_light_draw(uint32_t p_light_gl_id, GLES2::SceneMaterialData *p_material, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, const Transform3D &p_world_xform, RS::PrimitiveType p_primitive, int p_instances, const MultiMeshInstanceData *p_mm, const Transform3D *p_owner_transform, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, RS::InstanceType p_base_type, const BatchBoundGeometryStateGLES2 *p_bound_state = nullptr);

	template <bool p_is_batch>
	_FORCE_INLINE_ void _render_additive_light_passes(const GeometryInstanceSurface *p_surf, GLES2::SceneMaterialData *p_material, uint64_t p_spec_constants, bool p_instancing, const Transform3D &p_world_xform, RS::PrimitiveType p_primitive, int p_instances = 1, const MultiMeshInstanceData *p_mm = nullptr, const Transform3D *p_owner_transform = nullptr, bool p_use_index_buffer = false, GLenum p_primitive_gl = 0, int p_drawn_count = 0, GLenum p_index_type = 0, const BatchBoundGeometryStateGLES2 *p_bound_state = nullptr);

	template <AdditiveLightType p_light_type>
	_FORCE_INLINE_ void _upload_additive_light_uniforms(uint32_t p_light_gl_id, RID p_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants);

	template <AdditiveLightType p_light_type>
	_FORCE_INLINE_ void _render_static_mesh_group_light_type_pass(const LocalVector<uint32_t> &p_unique_lights, BatchType::Batch3D &p_batch, GLES2::SceneMaterialData *p_material_data, GLES2::SceneShaderData *p_shader, SceneShaderGLES2::ShaderVariant p_additive_variant, uint64_t p_additive_base_spec, bool p_promote_to_instancing, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type);

	template <AdditiveLightType p_light_type, bool p_is_batch>
	_FORCE_INLINE_ void _render_additive_light_type_pass(const LocalVector<uint32_t> &p_unique_lights, GLES2::SceneMaterialData *p_material, SceneShaderGLES2::ShaderVariant p_additive_variant, uint64_t p_additive_base_spec, const Transform3D &p_world_xform, RS::PrimitiveType p_primitive, int p_instances, const MultiMeshInstanceData *p_mm, const Transform3D *p_owner_transform, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type, RS::InstanceType p_base_type, const BatchBoundGeometryStateGLES2 *p_bound_state);

	/* Batch API */

	struct BatchBoundGeometryStateGLES2 {
		bool valid = false;
		bool is_single_mesh_fallback = false;
		bool use_index_buffer = false;
		GLenum index_type = GL_UNSIGNED_SHORT;
		uint32_t index_size = sizeof(uint16_t);
		uint32_t default_draw_count = 0;
		size_t ibo_byte_ofs = 0;
	};

	void scene_render_items_implementation(GeometryInstanceSurface **p_surfaces, int p_count, const Transform3D &p_camera_transform, bool p_transparent);

	void _batch_get_hardware_limits(BatchType::BatchLimits &r_limits);
	void _batch_get_instance_geometry_capacity(const GeometryInstanceSurface *p_surface, uint32_t &r_vertex_count, uint32_t &r_index_count);
	void _batch_resolve_pass_surface_state(const GeometryInstanceSurface *p_surface, GLES2::SceneMaterialData *&r_material, GLES2::SceneShaderData *&r_shader, RS::CullMode &r_cull_mode) const;
	float _batch_get_item_depth(const GeometryInstanceSurface *p_surface, const Transform3D &p_camera_transform);
	uint64_t _batch_get_state_hash(const GeometryInstanceSurface *p_surface);
	uint32_t _batch_get_surface_flags(const GeometryInstanceSurface *p_surface) const;
	BatchType::BatchColor _batch_get_surface_modulate(const GeometryInstanceSurface *p_surface) const;
	_FORCE_INLINE_ bool _batch_requires_palette_fvf() const {
		// Not required for GLES2
		return false;
	}
	bool _batch_can_use_single_mesh_fallback(const GeometryInstanceSurface *p_surface) const;
	_FORCE_INLINE_ bool _batch_buffer_orphan_and_upload_3d(unsigned int p_buffer_size_bytes, unsigned int p_offset_bytes, unsigned int p_data_size_bytes, const void *p_data, GLenum p_target, GLenum p_usage, bool p_optional_orphan) const;

	GLES2::SceneMaterialData *_batch_get_material_data(const GeometryInstanceSurface *p_surface);

	void _batch_prepare_surface_world_cache(const GeometryInstanceSurface *p_surface, const BatchType::BItemRef3D &p_ref, BatchType::TransformMode3D p_effective_mode, bool p_need_tangents);
	void _batch_fill_instance_tiered(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_multimesh_tiered(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_instance_depth(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);
	void _batch_fill_multimesh_depth(const BatchType::BItemRef3D &p_ref, uint8_t *r_bvs, uint16_t *r_inds, uint32_t p_start_vert, bool p_use_hardware_transform, uint32_t p_item_index);

	bool _batch_bind_generic_buffers(bool p_has_color, BatchBoundGeometryStateGLES2 &r_state);
	bool _batch_bind_single_mesh_fallback_buffers(bool p_has_color, BatchBoundGeometryStateGLES2 &r_state);
	void _batch_draw_bound_buffers(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, const BatchBoundGeometryStateGLES2 &p_state);

	void _batch_upload_buffers_3d(BatchType::Batch3D &r_batch, uint64_t p_content_hash);
	bool _batch_try_reuse_cached_vbo_slot_3d(RasterizerSceneBatcherCommon<BatcherAPISceneGLES2>::Batch3D &r_batch, uint64_t p_content_hash);
	void _batch_bind_material(GLES2::SceneMaterialData *p_material_data, const Transform3D &p_world_transform, bool p_transparent);
	bool _batch_bind_pass_material_and_state(GLES2::SceneMaterialData *p_material_data, GLES2::SceneShaderData *p_shader, RS::CullMode p_cull_mode, uint64_t p_state_hash, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, bool p_transparent);
	void _batch_render_generic(RS::PrimitiveType p_primitive, uint32_t p_offset = 0, uint32_t p_count = 0, bool p_has_color = true);
	void _batch_render_items(GLES2::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent);
	void _batch_render_single_mesh_fallback(RS::PrimitiveType p_primitive, uint32_t p_offset, uint32_t p_count, bool p_has_color);
	void _batch_render_static_mesh_group(GLES2::SceneMaterialData *p_material_data, RS::PrimitiveType p_primitive, BatchType::Batch3D &p_batch, bool p_transparent);
	void _batch_threshold_flush();

	void _render_static_mesh_group_additive_light_passes(BatchType::Batch3D &p_batch, GLES2::SceneMaterialData *p_material_data, GLES2::SceneShaderData *p_shader, uint64_t p_spec_constants, bool p_promote_to_instancing, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type);
	void _render_single_item_immediate(const GeometryInstanceSurface *p_surface);

	void _bind_surface_static_arrays_and_ibo(const GeometryInstanceSurface *p_surface, uint64_t p_input_mask, bool p_allow_deformed_instance, bool &r_use_index_buffer, int &r_drawn_count, GLenum &r_index_type);
	void _draw_surface_instances(const GeometryInstanceSurface *p_surface, const Transform3D &p_base_transform, int p_instances, const MultiMeshInstanceData *p_mm, RID p_shader_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, bool p_set_instancing_color, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type);
	void _draw_item_ref_instances(const BatchType::BItemRef3D &p_ref, RID p_shader_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants, bool p_promote_to_instancing, bool p_use_index_buffer, GLenum p_primitive_gl, int p_drawn_count, GLenum p_index_type);
	void _bind_scene_camera_uniforms(RID p_version, SceneShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants);
	void _bind_sky_directional_lights(RID p_version, SkyShaderGLES2::ShaderVariant p_variant, uint64_t p_spec_constants);

protected:
	double time;
	double time_step = 0;

	bool screen_space_roughness_limiter = false;
	float screen_space_roughness_limiter_amount = 0.25;
	float screen_space_roughness_limiter_limit = 0.18;

	void _render_buffers_debug_draw(Ref<RenderSceneBuffersGLES2> p_render_buffers, RID p_shadow_atlas, GLuint p_fbo);

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
		GLuint directional_light_buffer = 0;

		RID shader_default_version;
		RID default_material;
		RID default_shader;
		RID fog_material;
		RID fog_shader;
		GLuint screen_triangle = 0;
		GLuint screen_triangle_array = 0;
		uint32_t max_directional_lights = 4;
		uint32_t roughness_layers = 8;
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
		GLuint uniform_buffer;

		int radiance_size = 256;
		int mipmap_count = 1;

		RS::SkyMode mode = RS::SKY_MODE_AUTOMATIC;

		//ReflectionData reflection;
		bool reflection_dirty = false;
		bool dirty = false;
		int processing_layer = 0;
		Sky *dirty_list = nullptr;
		float baked_exposure = 1.0;

		//State to track when radiance cubemap needs updating
		GLES2::SkyMaterialData *prev_material = nullptr;
		Vector3 prev_position = Vector3(0.0, 0.0, 0.0);
		float prev_time = 0.0f;
	};

	Sky *dirty_sky_list = nullptr;
	mutable RID_Owner<Sky, true> sky_owner;

	void _setup_sky(const RenderDataGLES2 *p_render_data, const PagedArray<RID> &p_lights, const Projection &p_projection, const Transform3D &p_transform, const Size2i p_screen_size);
	void _invalidate_sky(Sky *p_sky);
	void _update_dirty_skys();
	void _update_sky_radiance(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier);
	void _draw_sky(RID p_env, const Projection &p_projection, const Transform3D &p_transform, float p_sky_energy_multiplier, float p_luminance_multiplier, bool p_use_multiview, bool p_flip_y, bool p_apply_color_adjustments_in_post);
	void _free_sky_data(Sky *p_sky);

	// Needed for a single argument calls (material and uv2).
	PagedArrayPool<RenderGeometryInstance *> cull_argument_pool;
	PagedArray<RenderGeometryInstance *> cull_argument;

public:
	static RasterizerSceneGLES2 *get_singleton() { return singleton; }

	void initialize();

	RasterizerCanvasGLES2 *canvas = nullptr;

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
	void _render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, GLuint p_framebuffer, const Rect2i &p_region, const GLuint p_color_textures[4]);

	bool free(RID p_rid) override;
	void update() override;
	void sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) override;

	void decals_set_filter(RS::DecalFilter p_filter) override;
	void light_projectors_set_filter(RS::LightProjectorFilter p_filter) override;
	virtual void lightmaps_set_bicubic_filter(bool p_enable) override;

public:
	RasterizerSceneGLES2();
	~RasterizerSceneGLES2();
};

#endif // GLES2_ENABLED

#endif // RASTERIZER_SCENE_GLES2_H
