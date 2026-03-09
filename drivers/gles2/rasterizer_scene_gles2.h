/*************************************************************************/
/*  rasterizer_scene_gles2.h                                             */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#pragma once
// dummy

#ifdef GLES2_ENABLED

#include "core/math/projection.h"
#include "core/templates/rid_owner.h"
#include "core/templates/self_list.h"
#include "scene/resources/mesh.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_scene_render.h"
#include "servers/rendering/renderer_scene_cull.h"
#include "servers/rendering_server.h"
#include "drivers/gles2/common/stubs.h"
#include "drivers/gles2/storage/config.h"
#include "drivers/gles2/storage/texture_storage.h"
#include "drivers/gles2/compiler/shader_gles2.h"
#include "drivers/gles2/storage/light_storage.h"

#include "drivers/gles2/shaders/scene.glsl.gen.h"
#include "drivers/gles2/shaders/cube_to_dp.glsl.gen.h"
#include "drivers/gles2/shaders/effect_blur.glsl.gen.h"
#include "drivers/gles2/shaders/tonemap.glsl.gen.h"

namespace GLES2 {

class RasterizerSceneGLES2 : public RendererSceneRender {
public:
	enum ShadowFilterMode {
		SHADOW_FILTER_NEAREST,
		SHADOW_FILTER_PCF5,
		SHADOW_FILTER_PCF13,
	};

	enum {
		INSTANCE_ATTRIB_BASE = 8,
		INSTANCE_BONE_BASE = 13,
	};

	ShadowFilterMode shadow_filter_mode;

	RID default_material;
	RID default_material_twosided;
	RID default_shader;
	RID default_shader_twosided;

	RID default_worldcoord_material;
	RID default_worldcoord_material_twosided;
	RID default_worldcoord_shader;
	RID default_worldcoord_shader_twosided;

	RID default_overdraw_material;
	RID default_overdraw_shader;

	uint64_t render_pass;
	uint64_t scene_pass;
	uint32_t current_material_index;
	uint32_t current_geometry_index;
	uint32_t current_light_index;
	uint32_t current_refprobe_index;
	uint32_t current_shader_index;

	RasterizerStorageGLES2 *storage;
	struct State {
		bool texscreen_copied;
		int current_blend_mode;
		float current_line_width;
		int current_depth_draw;
		bool current_depth_test;
		GLuint current_main_tex;

		SceneShaderGLES2 scene_shader;
		CubeToDpShaderGLES2 cube_to_dp_shader;
		TonemapShaderGLES2 tonemap_shader;
		EffectBlurShaderGLES2 effect_blur_shader;

		GLuint sky_verts;

		GLuint immediate_buffer;
		Color default_ambient;
		Color default_bg;

		// ResolveShaderGLES3 resolve_shader;
		// ScreenSpaceReflectionShaderGLES3 ssr_shader;
		// EffectBlurShaderGLES3 effect_blur_shader;
		// SubsurfScatteringShaderGLES3 sss_shader;
		// SsaoMinifyShaderGLES3 ssao_minify_shader;
		// SsaoShaderGLES3 ssao_shader;
		// SsaoBlurShaderGLES3 ssao_blur_shader;
		// ExposureShaderGLES3 exposure_shader;

		/*
		struct SceneDataUBO {
			//this is a std140 compatible struct. Please read the OpenGL 3.3 Specificaiton spec before doing any changes
			float projection_matrix[16];
			float inv_projection_matrix[16];
			float camera_inverse_matrix[16];
			float camera_matrix[16];
			float ambient_light_color[4];
			float bg_color[4];
			float fog_color_enabled[4];
			float fog_sun_color_amount[4];

			float ambient_energy;
			float bg_energy;
			float z_offset;
			float z_slope_scale;
			float shadow_dual_paraboloid_render_zfar;
			float shadow_dual_paraboloid_render_side;
			float viewport_size[2];
			float screen_pixel_size[2];
			float shadow_atlas_pixel_size[2];
			float shadow_directional_pixel_size[2];

			float time;
			float z_far;
			float reflection_multiplier;
			float subsurface_scatter_width;
			float ambient_occlusion_affect_light;

			uint32_t fog_depth_enabled;
			float fog_depth_begin;
			float fog_depth_curve;
			uint32_t fog_transmit_enabled;
			float fog_transmit_curve;
			uint32_t fog_height_enabled;
			float fog_height_min;
			float fog_height_max;
			float fog_height_curve;
			// make sure this struct is padded to be a multiple of 16 bytes for webgl

		} ubo_data;

		GLuint scene_ubo;

		struct EnvironmentRadianceUBO {
			float transform[16];
			float ambient_contribution;
			uint8_t padding[12];

		} env_radiance_data;

		GLuint env_radiance_ubo;

		GLuint sky_array;

		GLuint directional_ubo;

		GLuint spot_array_ubo;
		GLuint omni_array_ubo;
		GLuint reflection_array_ubo;

		GLuint immediate_buffer;
		GLuint immediate_array;

		uint32_t ubo_light_size;
		uint8_t *spot_array_tmp;
		uint8_t *omni_array_tmp;
		uint8_t *reflection_array_tmp;

		int max_ubo_lights;
		int max_forward_lights_per_object;
		int max_ubo_reflections;
		int max_skeleton_bones;

		bool used_contact_shadows;

		int spot_light_count;
		int omni_light_count;
		int directional_light_count;
		int reflection_probe_count;

		bool used_sss;
		bool using_contact_shadows;

		RS::ViewportDebugDraw debug_draw;
		*/

		bool cull_front;
		bool cull_disabled;

		bool used_screen_texture;
		bool shadow_is_dual_parabolloid;
		float dual_parbolloid_direction;
		float dual_parbolloid_zfar;

		bool render_no_shadows;

		Vector2 viewport_size;

		Vector2 screen_pixel_size;
	} state;

	/* SHADOW ATLAS API */

	uint64_t shadow_atlas_realloc_tolerance_msec;

	struct ShadowAtlas {
		enum {
			QUADRANT_SHIFT = 27,
			SHADOW_INDEX_MASK = (1 << QUADRANT_SHIFT) - 1,
			SHADOW_INVALID = 0xFFFFFFFF,
		};

		struct Quadrant {
			uint32_t subdivision;

			struct Shadow {
				RID owner;
				uint64_t version;
				uint64_t alloc_tick;

				Shadow() {
					version = 0;
					alloc_tick = 0;
				}
			};

			Vector<Shadow> shadows;

			Quadrant() {
				subdivision = 0;
			}
		} quadrants[4];

		int size_order[4];
		uint32_t smallest_subdiv;

		int size;

		GLuint fbo;
		GLuint depth;
		GLuint color;

		HashMap<RID, uint32_t> shadow_owners;
	};

	struct ShadowCubeMap {
		GLuint fbo[6];
		GLuint cubemap;
		uint32_t size;
	};

	Vector<ShadowCubeMap> shadow_cubemaps;

	RID_PtrOwner<ShadowAtlas> shadow_atlas_owner;

	RID shadow_atlas_create();
	void shadow_atlas_set_size(RID p_atlas, int p_size);
	void shadow_atlas_set_quadrant_subdivision(RID p_atlas, int p_quadrant, int p_subdivision);
	bool _shadow_atlas_find_shadow(ShadowAtlas *shadow_atlas, int *p_in_quadrants, int p_quadrant_count, int p_current_subdiv, uint64_t p_tick, int &r_quadrant, int &r_shadow);
	bool shadow_atlas_update_light(RID p_atlas, RID p_light_intance, float p_coverage, uint64_t p_light_version);

	struct DirectionalShadow {
		GLuint fbo;
		GLuint depth;
		GLuint color;

		int light_count;
		int size;
		int current_light;
	} directional_shadow;

	virtual int get_directional_light_shadow_size(RID p_light_intance);
	virtual void set_directional_shadow_count(int p_count);

	/* REFLECTION PROBE ATLAS API */

	virtual RID reflection_atlas_create();
	virtual void reflection_atlas_set_size(RID p_ref_atlas, int p_size);
	virtual void reflection_atlas_set_subdivision(RID p_ref_atlas, int p_subdiv);

	/* REFLECTION CUBEMAPS */

	/* REFLECTION PROBE INSTANCE */

	struct ReflectionProbeInstance {
		ReflectionProbe *probe_ptr;
		RID probe;
		RID self;
		RID atlas;

		int reflection_atlas_index;

		int render_step;
		int reflection_index;

		GLuint fbo[6];
		GLuint color[6];
		GLuint depth;
		GLuint cubemap;

		int current_resolution;
		mutable bool dirty;

		uint64_t last_pass;
		uint32_t index;

		Transform3D transform;
	};

	mutable RID_PtrOwner<ReflectionProbeInstance> reflection_probe_instance_owner;

	ReflectionProbeInstance **reflection_probe_instances;
	int reflection_probe_count;

	virtual RID reflection_probe_instance_create(RID p_probe);
	virtual void reflection_probe_instance_set_transform(RID p_instance, const Transform3D &p_transform);
	virtual void reflection_probe_release_atlas_index(RID p_instance);
	virtual bool reflection_probe_instance_needs_redraw(RID p_instance);
	virtual bool reflection_probe_instance_has_reflection(RID p_instance);
	virtual bool reflection_probe_instance_begin_render(RID p_instance, RID p_reflection_atlas);
	virtual bool reflection_probe_instance_postprocess_step(RID p_instance);

	/* ENVIRONMENT API */

	struct Environment {
		RS::EnvironmentBG bg_mode;

		RID sky;
		float sky_custom_fov;
		Basis sky_orientation;

		Color bg_color;
		float bg_energy;
		float sky_ambient;

		int camera_feed_id;

		Color ambient_color;
		float ambient_energy;
		float ambient_sky_contribution;

		int canvas_max_layer;

		bool glow_enabled;
		int glow_levels;
		float glow_intensity;
		float glow_strength;
		float glow_bloom;
		RS::EnvironmentGlowBlendMode glow_blend_mode;
		float glow_hdr_bleed_threshold;
		float glow_hdr_bleed_scale;
		float glow_hdr_luminance_cap;
		bool glow_bicubic_upscale;

		bool dof_blur_far_enabled;
		float dof_blur_far_distance;
		float dof_blur_far_transition;
		float dof_blur_far_amount;
		RS::DOFBlurQuality dof_blur_far_quality;

		bool dof_blur_near_enabled;
		float dof_blur_near_distance;
		float dof_blur_near_transition;
		float dof_blur_near_amount;
		RS::DOFBlurQuality dof_blur_near_quality;

		bool adjustments_enabled;
		float adjustments_brightness;
		float adjustments_contrast;
		float adjustments_saturation;
		RID color_correction;

		bool fog_enabled;
		Color fog_color;
		Color fog_sun_color;
		float fog_sun_amount;

		bool fog_depth_enabled;
		float fog_depth_begin;
		float fog_depth_end;
		float fog_depth_curve;
		bool fog_transmit_enabled;
		float fog_transmit_curve;
		bool fog_height_enabled;
		float fog_height_min;
		float fog_height_max;
		float fog_height_curve;

		Environment() :
				bg_mode(RS::ENV_BG_CLEAR_COLOR),
				sky_custom_fov(0.0),
				bg_energy(1.0),
				sky_ambient(0),
				camera_feed_id(0),
				ambient_energy(1.0),
				ambient_sky_contribution(0.0),
				canvas_max_layer(0),
				glow_enabled(false),
				glow_levels((1 << 2) | (1 << 4)),
				glow_intensity(0.8),
				glow_strength(1.0),
				glow_bloom(0.0),
				glow_blend_mode(RS::ENV_GLOW_BLEND_MODE_SOFTLIGHT),
				glow_hdr_bleed_threshold(1.0),
				glow_hdr_bleed_scale(2.0),
				glow_hdr_luminance_cap(12.0),
				glow_bicubic_upscale(false),
				dof_blur_far_enabled(false),
				dof_blur_far_distance(10),
				dof_blur_far_transition(5),
				dof_blur_far_amount(0.1),
				dof_blur_far_quality(RS::DOF_BLUR_QUALITY_MEDIUM),
				dof_blur_near_enabled(false),
				dof_blur_near_distance(2),
				dof_blur_near_transition(1),
				dof_blur_near_amount(0.1),
				dof_blur_near_quality(RS::DOF_BLUR_QUALITY_MEDIUM),
				adjustments_enabled(false),
				adjustments_brightness(1.0),
				adjustments_contrast(1.0),
				adjustments_saturation(1.0),
				fog_enabled(false),
				fog_color(Color(0.5, 0.5, 0.5)),
				fog_sun_color(Color(0.8, 0.8, 0.0)),
				fog_sun_amount(0),
				fog_depth_enabled(true),
				fog_depth_begin(10),
				fog_depth_end(0),
				fog_depth_curve(1),
				fog_transmit_enabled(true),
				fog_transmit_curve(1),
				fog_height_enabled(false),
				fog_height_min(10),
				fog_height_max(0),
				fog_height_curve(1) {
		}
	};

	mutable RID_PtrOwner<Environment> environment_owner;

	virtual RID environment_create();

	virtual void environment_set_background(RID p_env, RS::EnvironmentBG p_bg);
	virtual void environment_set_sky(RID p_env, RID p_sky);
	virtual void environment_set_sky_custom_fov(RID p_env, float p_scale);
	virtual void environment_set_sky_orientation(RID p_env, const Basis &p_orientation);
	virtual void environment_set_bg_color(RID p_env, const Color &p_color);
	virtual void environment_set_bg_energy(RID p_env, float p_energy);
	virtual void environment_set_canvas_max_layer(RID p_env, int p_max_layer);
	virtual void environment_set_ambient_light(RID p_env, const Color &p_color, float p_energy = 1.0, float p_sky_contribution = 0.0);
	virtual void environment_set_camera_feed_id(RID p_env, int p_camera_feed_id);

	virtual void environment_set_dof_blur_near(RID p_env, bool p_enable, float p_distance, float p_transition, float p_amount, RS::DOFBlurQuality p_quality);
	virtual void environment_set_dof_blur_far(RID p_env, bool p_enable, float p_distance, float p_transition, float p_amount, RS::DOFBlurQuality p_quality);

	virtual void environment_set_glow(RID p_env, bool p_enable, int p_level_flags, float p_intensity, float p_strength, float p_bloom_threshold, RS::EnvironmentGlowBlendMode p_blend_mode, float p_hdr_bleed_threshold, float p_hdr_bleed_scale, float p_hdr_luminance_cap, bool p_bicubic_upscale);
	virtual void environment_set_fog(RID p_env, bool p_enable, float p_begin, float p_end, RID p_gradient_texture);

	virtual void environment_set_ssr(RID p_env, bool p_enable, int p_max_steps, float p_fade_in, float p_fade_out, float p_depth_tolerance, bool p_roughness);
	virtual void environment_set_ssao(RID p_env, bool p_enable, float p_radius, float p_intensity, float p_radius2, float p_intensity2, float p_bias, float p_light_affect, float p_ao_channel_affect, const Color &p_color, RS::EnvironmentSSAOQuality p_quality);

	virtual void environment_set_tonemap(RID p_env, RS::EnvironmentToneMapper p_tone_mapper, float p_exposure, float p_white, bool p_auto_exposure, float p_min_luminance, float p_max_luminance, float p_auto_exp_speed, float p_auto_exp_scale);

	virtual void environment_set_adjustment(RID p_env, bool p_enable, float p_brightness, float p_contrast, float p_saturation, RID p_ramp);

	virtual void environment_set_fog(RID p_env, bool p_enable, const Color &p_color, const Color &p_sun_color, float p_sun_amount);
	virtual void environment_set_fog_depth(RID p_env, bool p_enable, float p_depth_begin, float p_depth_end, float p_depth_curve, bool p_transmit, float p_transmit_curve);
	virtual void environment_set_fog_height(RID p_env, bool p_enable, float p_min_height, float p_max_height, float p_height_curve);

	virtual bool is_environment(RID p_env);

	virtual RS::EnvironmentBG environment_get_background(RID p_env);
	virtual int environment_get_canvas_max_layer(RID p_env);



	/* LIGHT INSTANCE */

	struct LightInstance {
		struct ShadowTransform {
			Projection camera;
			Transform3D transform;
			float farplane;
			float split;
			float bias_scale;
		};

		ShadowTransform shadow_transform[4];

		RID self;
		RID light;

		Light *light_ptr;
		Transform3D transform;

		Vector3 light_vector;
		Vector3 spot_vector;
		float linear_att;

		// TODO passes and all that stuff ?
		uint64_t last_scene_pass;
		uint64_t last_scene_shadow_pass;

		uint16_t light_index;
		uint16_t light_directional_index;

		Rect2 directional_rect;

		RBSet<RID> shadow_atlases; // atlases where this light is registered
	};

	mutable RID_PtrOwner<LightInstance> light_instance_owner;

	virtual RID light_instance_create(RID p_light);
	virtual void light_instance_set_transform(RID p_light_instance, const Transform3D &p_transform);
	virtual void light_instance_set_shadow_transform(RID p_light_instance, const Projection &p_projection, const Transform3D &p_transform, float p_far, float p_split, int p_pass, float p_bias_scale = 1.0);
	virtual void light_instance_mark_visible(RID p_light_instance);
	virtual bool light_instances_can_render_shadow_cube() const {
		// return storage->config.support_shadow_cubemaps;
		return false;
	}

	LightInstance **render_light_instances;
	int render_directional_lights;
	int render_light_instance_count;

	/* REFLECTION INSTANCE */

	virtual RID gi_probe_instance_create();
	virtual void gi_probe_instance_set_light_data(RID p_probe, RID p_base, RID p_data);
	virtual void gi_probe_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform);
	virtual void gi_probe_instance_set_bounds(RID p_probe, const Vector3 &p_bounds);

	/* RENDER LIST */

	enum LightMode {
		LIGHTMODE_NORMAL,
		LIGHTMODE_UNSHADED,
		LIGHTMODE_LIGHTMAP,
		LIGHTMODE_LIGHTMAP_CAPTURE,
	};

	struct RenderList {
		enum {
			MAX_LIGHTS = 255,
			MAX_REFLECTION_PROBES = 255,
			DEFAULT_MAX_ELEMENTS = 65536
		};

		int max_elements;

		struct Element {
			RendererSceneCull::Instance *instance;

			void *geometry;
			Material *material;
			void *owner;

			bool use_accum; //is this an add pass for multipass
			bool *use_accum_ptr;
			bool front_facing;

			union {
				//TODO: should be endian swapped on big endian
				struct {
					int32_t depth_layer : 16;
					int32_t priority : 16;
				};

				uint32_t depth_key;
			};

			union {
				struct {
					//from least significant to most significant in sort, TODO: should be endian swapped on big endian

					uint64_t geometry_index : 14;
					uint64_t instancing : 1;
					uint64_t skeleton : 1;
					uint64_t shader_index : 10;
					uint64_t material_index : 10;
					uint64_t light_index : 8;
					uint64_t light_type2 : 1; // if 1==0 : nolight/directional, else omni/spot
					uint64_t refprobe_1_index : 8;
					uint64_t refprobe_0_index : 8;
					uint64_t light_type1 : 1; //no light, directional is 0, omni spot is 1
					uint64_t light_mode : 2; // LightMode enum
				};

				uint64_t sort_key;
			};
		};

		Element *base_elements;
		Element **elements;

		int element_count;
		int alpha_element_count;

		void clear() {
			element_count = 0;
			alpha_element_count = 0;
		}

		// sorts

		struct SortByKey {
			_FORCE_INLINE_ bool operator()(const Element *A, const Element *B) const {
				if (A->depth_key == B->depth_key) {
					return A->sort_key < B->sort_key;
				} else {
					return A->depth_key < B->depth_key;
				}
			}
		};

		void sort_by_key(bool p_alpha) {
			SortArray<Element *, SortByKey> sorter;

			if (p_alpha) {
				sorter.sort(&elements[max_elements - alpha_element_count], alpha_element_count);
			} else {
				sorter.sort(elements, element_count);
			}
		}

		struct SortByDepth {
			_FORCE_INLINE_ bool operator()(const Element *A, const Element *B) const {
				return A->depth_layer < B->depth_layer;
			}
		};

		void sort_by_depth(bool p_alpha) { //used for shadows

			SortArray<Element *, SortByDepth> sorter;
			if (p_alpha) {
				sorter.sort(&elements[max_elements - alpha_element_count], alpha_element_count);
			} else {
				sorter.sort(elements, element_count);
			}
		}

		struct SortByReverseDepthAndPriority {
			_FORCE_INLINE_ bool operator()(const Element *A, const Element *B) const {
				if (A->priority == B->priority) {
					return A->depth_layer > B->depth_layer;
				} else {
					return A->priority < B->priority;
				}
			}
		};

		void sort_by_reverse_depth_and_priority(bool p_alpha) { //used for alpha

			SortArray<Element *, SortByReverseDepthAndPriority> sorter;
			if (p_alpha) {
				sorter.sort(&elements[max_elements - alpha_element_count], alpha_element_count);
			} else {
				sorter.sort(elements, element_count);
			}
		}

		// element adding and stuff

		_FORCE_INLINE_ Element *add_element() {
			if (element_count + alpha_element_count >= max_elements) {
				return NULL;
			}

			elements[element_count] = &base_elements[element_count];
			return elements[element_count++];
		}

		_FORCE_INLINE_ Element *add_alpha_element() {
			if (element_count + alpha_element_count >= max_elements) {
				return NULL;
			}

			int idx = max_elements - alpha_element_count - 1;
			elements[idx] = &base_elements[idx];
			alpha_element_count++;
			return elements[idx];
		}

		void init() {
			element_count = 0;
			alpha_element_count = 0;

			elements = memnew_arr(Element *, max_elements);
			base_elements = memnew_arr(Element, max_elements);

			for (int i = 0; i < max_elements; i++) {
				elements[i] = &base_elements[i];
			}
		}

		RenderList() {
			max_elements = DEFAULT_MAX_ELEMENTS;
		}

		~RenderList() {
			memdelete_arr(elements);
			memdelete_arr(base_elements);
		}
	};

	RenderList render_list;

	void _add_geometry(void *p_geometry, RendererSceneCull::Instance *p_instance, void *p_owner, int p_material, bool p_depth_pass, bool p_shadow_pass);
	void _add_geometry_with_material(void *p_geometry, RendererSceneCull::Instance *p_instance, void *p_owner, Material *p_material, bool p_depth_pass, bool p_shadow_pass);

	void _copy_texture_to_buffer(GLuint p_texture, GLuint p_buffer);
	void _fill_render_list(RendererSceneCull::Instance **p_cull_result, int p_cull_count, bool p_depth_pass, bool p_shadow_pass);
	void _render_render_list(RenderList::Element **p_elements, int p_element_count,
			const Transform3D &p_view_transform,
			const Projection &p_projection,
			RID p_shadow_atlas,
			Environment *p_env,
			GLuint p_base_env,
			float p_shadow_bias,
			float p_shadow_normal_bias,
			bool p_reverse_cull,
			bool p_alpha_pass,
			bool p_shadow);

	void _draw_sky(RasterizerStorageGLES2::Sky *p_sky, const Projection &p_projection, const Transform3D &p_transform, bool p_vflip, float p_custom_fov, float p_energy, const Basis &p_sky_orientation);

	_FORCE_INLINE_ void _set_cull(bool p_front, bool p_disabled, bool p_reverse_cull);
	_FORCE_INLINE_ bool _setup_material(Material *p_material, bool p_alpha_pass, Size2i p_skeleton_tex_size = Size2i(0, 0));
	_FORCE_INLINE_ void _setup_geometry(RenderList::Element *p_element, Skeleton *p_skeleton);
	_FORCE_INLINE_ void _setup_light_type(LightInstance *p_light, ShadowAtlas *shadow_atlas);
	_FORCE_INLINE_ void _setup_light(LightInstance *p_light, ShadowAtlas *shadow_atlas, const Transform3D &p_view_transform, bool accum_pass);
	_FORCE_INLINE_ void _setup_refprobes(ReflectionProbeInstance *p_refprobe1, ReflectionProbeInstance *p_refprobe2, const Transform3D &p_view_transform, Environment *p_env);
	_FORCE_INLINE_ void _render_geometry(RenderList::Element *p_element);

	void _post_process(Environment *env, const Projection &p_cam_projection);

	virtual void render_scene(
			const Ref<RenderSceneBuffers> &p_render_buffers,
			const RendererSceneRender::CameraData *p_camera_data,
			const RendererSceneRender::CameraData *p_prev_camera_data,
			const PagedArray<RenderGeometryInstance *> &p_instances,
			const PagedArray<RID> &p_lights,
			const PagedArray<RID> &p_reflection_probes,
			const PagedArray<RID> &p_vfx_nodes,
			const PagedArray<RID> &p_decals,
			const PagedArray<RID> &p_lightmaps,
			const PagedArray<RID> &p_fog_volumes,
			RID p_environment,
			RID p_camera_effects,
			RID p_shadow_atlas,
			RID p_occluder_debug_tex,
			RID p_reflection_atlas,
			RID p_reflection_probes_buffer,
			RID p_lightmap_buffer,
			int p_viewport_usage,
			float p_screen_mesh_lod_threshold,
			const RendererSceneRender::RenderShadowData *p_render_shadow_data,
			int p_render_shadow_count,
			const RendererSceneRender::RenderSDFGIData *p_render_sdfgi_data,
			int p_render_sdfgi_count,
			const RendererSceneRender::RenderSDFGIUpdateData *p_render_sdfgi_update_data,
			RenderingMethod::RenderInfo *r_render_info) override;

	virtual void render_shadow(RID p_light, RID p_shadow_atlas, int p_pass, RendererSceneCull::Instance **p_cull_result, int p_cull_count);
	virtual bool free(RID p_rid);

	virtual void set_scene_pass(uint64_t p_pass);
	virtual void set_debug_draw_mode(RS::ViewportDebugDraw p_debug_draw);

	void iteration();
	void initialize();
	void finalize();

	// Godot 4 stubs
	virtual RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	virtual void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;
	virtual uint32_t geometry_instance_get_pair_mask() override;
	virtual void mesh_generate_pipelines(RID p_mesh, bool p_current_formats) override;
	virtual uint32_t get_pipeline_compilations(RenderingServer::PipelineSource p_source) override;

	virtual void sdfgi_update(const Ref<RenderSceneBuffers> &p_buffers, RID p_environment, const Vector3 &p_cam_transform) override;
	virtual int sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_buffers) const override;
	virtual AABB sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_buffers, int p_region) const override;
	virtual uint32_t sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_buffers, int p_region) const override;
	virtual void sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) override;

	virtual RID sky_allocate() override;
	virtual void sky_initialize(RID p_rid) override;
	virtual void sky_set_radiance_size(RID p_sky, int p_radiance_size) override;
	virtual void sky_set_mode(RID p_sky, RenderingServer::SkyMode p_mode) override;
	virtual void sky_set_material(RID p_sky, RID p_material) override;
	virtual Ref<Image> sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) override;

	virtual void environment_set_volumetric_fog_volume_size(int p_size, int p_depth) override;
	virtual void environment_set_volumetric_fog_filter_active(bool p_active) override;
	virtual void environment_glow_set_use_bicubic_upscale(bool p_enable) override;
	virtual void environment_set_ssr_roughness_quality(RenderingServer::EnvironmentSSRRoughnessQuality p_quality) override;
	virtual void environment_set_ssao_quality(RenderingServer::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	virtual void environment_set_ssil_quality(RenderingServer::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	virtual void environment_set_sdfgi_ray_count(RenderingServer::EnvironmentSDFGIRayCount p_ray_count) override;
	virtual void environment_set_sdfgi_frames_to_converge(RenderingServer::EnvironmentSDFGIFramesToConverge p_frames) override;
	virtual void environment_set_sdfgi_frames_to_update_light(RenderingServer::EnvironmentSDFGIFramesToUpdateLight p_update) override;
	virtual Ref<Image> environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) override;

	virtual void positional_soft_shadow_filter_set_quality(RenderingServer::ShadowQuality p_quality) override;
	virtual void directional_soft_shadow_filter_set_quality(RenderingServer::ShadowQuality p_quality) override;

	virtual RID fog_volume_instance_create(RID p_base) override;
	virtual void fog_volume_instance_set_transform(RID p_fog_volume, const Transform3D &p_transform) override;
	virtual void fog_volume_instance_set_active(RID p_fog_volume, bool p_active) override;
	virtual RID fog_volume_instance_get_volume(RID p_fog_volume) const override;
	virtual Vector3 fog_volume_instance_get_position(RID p_fog_volume) const override;

	virtual RID voxel_gi_instance_create(RID p_base) override;
	virtual void voxel_gi_instance_set_transform_to_data(RID p_probe, const Transform3D &p_xform) override;
	virtual bool voxel_gi_needs_update(RID p_probe) const override;
	virtual void voxel_gi_update(RID p_probe, bool p_update_light_instances, const Vector<RID> &p_light_instances, const PagedArray<RenderGeometryInstance *> &p_dynamic_objects) override;
	virtual void voxel_gi_set_quality(RenderingServer::VoxelGIQuality) override;

	virtual void render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) override;
	virtual void render_particle_collider_heightfield(RID p_collider, const Transform3D &p_transform, const PagedArray<RenderGeometryInstance *> &p_instances) override;
	virtual void set_time(double p_time, double p_step) override;
	virtual Ref<RenderSceneBuffers> render_buffers_create() override;
	virtual void gi_set_use_half_resolution(bool p_enable) override;
	virtual void screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_limit) override;
	virtual bool screen_space_roughness_limiter_is_active() const override;
	virtual void sub_surface_scattering_set_quality(RenderingServer::SubSurfaceScatteringQuality p_quality) override;
	virtual void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) override;
	virtual TypedArray<Image> bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) override;
	virtual void decals_set_filter(RenderingServer::DecalFilter p_filter) override;
	virtual void light_projectors_set_filter(RenderingServer::LightProjectorFilter p_filter) override;
	virtual void lightmaps_set_bicubic_filter(bool p_enable) override;
	virtual void update() override;

public:
	RasterizerSceneGLES2();
	~RasterizerSceneGLES2() = default;
};
} //namespace GLES2
#endif // GLES2_ENABLED
