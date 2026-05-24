/**************************************************************************/
/*  texture_storage.cpp                                                   */
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

#include "texture_storage.h"

#include "drivers/gles1/effects/copy_effects.h"
#include "drivers/gles1/rasterizer_gles1.h"
#include "config.h"
#include "utilities.h"

#ifdef ANDROID_ENABLED
#define glFramebufferTextureMultiviewOVR GLES1::Config::get_singleton()->eglFramebufferTextureMultiviewOVR
#endif

using namespace GLES1;

TextureStorage *TextureStorage::singleton = nullptr;

TextureStorage *TextureStorage::get_singleton() {
	return singleton;
}

static constexpr GLenum _cube_side_enum[6] = {
	GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
	GL_TEXTURE_CUBE_MAP_POSITIVE_X,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
};

TextureStorage::TextureStorage() {
	singleton = this;

	// Default texture generation
	Ref<Image> white_img = Image::create_empty(8, 8, false, Image::FORMAT_RGB8);
	white_img->fill(Color(1, 1, 1, 1));
	default_gl_textures[DEFAULT_GL_TEXTURE_WHITE] = texture_allocate();
	texture_2d_initialize(default_gl_textures[DEFAULT_GL_TEXTURE_WHITE], white_img);

	Ref<Image> black_img = Image::create_empty(8, 8, false, Image::FORMAT_RGB8);
	black_img->fill(Color(0, 0, 0, 1));
	default_gl_textures[DEFAULT_GL_TEXTURE_BLACK] = texture_allocate();
	texture_2d_initialize(default_gl_textures[DEFAULT_GL_TEXTURE_BLACK], black_img);

	Ref<Image> normal_img = Image::create_empty(8, 8, false, Image::FORMAT_RGB8);
	normal_img->fill(Color(0.5, 0.5, 1.0, 1));
	default_gl_textures[DEFAULT_GL_TEXTURE_NORMAL] = texture_allocate();
	texture_2d_initialize(default_gl_textures[DEFAULT_GL_TEXTURE_NORMAL], normal_img);

	Ref<Image> aniso_img = Image::create_empty(8, 8, false, Image::FORMAT_RGB8);
	aniso_img->fill(Color(1.0, 0.5, 0.0, 1));
	default_gl_textures[DEFAULT_GL_TEXTURE_ANISO] = texture_allocate();
	texture_2d_initialize(default_gl_textures[DEFAULT_GL_TEXTURE_ANISO], aniso_img);

	Ref<Image> transparent_img = Image::create_empty(8, 8, false, Image::FORMAT_RGBA8);
	transparent_img->fill(Color(0, 0, 0, 0));
	default_gl_textures[DEFAULT_GL_TEXTURE_TRANSPARENT] = texture_allocate();
	texture_2d_initialize(default_gl_textures[DEFAULT_GL_TEXTURE_TRANSPARENT], transparent_img);
}

TextureStorage::~TextureStorage() {
	singleton = nullptr;
	for (int i = 0; i < DEFAULT_GL_TEXTURE_MAX; i++) {
		texture_free(default_gl_textures[i]);
	}
	
	if (texture_atlas.texture != 0) {
		if (GLES1::Utilities::get_singleton()->has_texture_data(texture_atlas.texture)) {
			GLES1::Utilities::get_singleton()->texture_free_data(texture_atlas.texture);
		} else {
			glDeleteTextures(1, &texture_atlas.texture);
		}
	}
	texture_atlas.texture = 0;

	if (Config::get_singleton()->support_fbo && texture_atlas.framebuffer != 0) {
		glDeleteFramebuffersOES(1, &texture_atlas.framebuffer);
	}
	texture_atlas.framebuffer = 0;
}

/* Canvas Texture API */

RID TextureStorage::canvas_texture_allocate() {
	return canvas_texture_owner.allocate_rid();
}

void TextureStorage::canvas_texture_initialize(RID p_rid) {
	canvas_texture_owner.initialize_rid(p_rid);
}

void TextureStorage::canvas_texture_free(RID p_rid) {
	canvas_texture_owner.free(p_rid);
}

void TextureStorage::canvas_texture_set_channel(RID p_canvas_texture, RS::CanvasTextureChannel p_channel, RID p_texture) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_canvas_texture);
	ERR_FAIL_NULL(ct);

	switch (p_channel) {
		case RS::CANVAS_TEXTURE_CHANNEL_DIFFUSE: {
			ct->diffuse = p_texture;
		} break;
		case RS::CANVAS_TEXTURE_CHANNEL_NORMAL: {
			ct->normal_map = p_texture;
		} break;
		case RS::CANVAS_TEXTURE_CHANNEL_SPECULAR: {
			ct->specular = p_texture;
		} break;
	}
}

void TextureStorage::canvas_texture_set_shading_parameters(RID p_canvas_texture, const Color &p_specular_color, float p_shininess) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_canvas_texture);
	ERR_FAIL_NULL(ct);

	ct->specular_color.r = p_specular_color.r;
	ct->specular_color.g = p_specular_color.g;
	ct->specular_color.b = p_specular_color.b;
	ct->specular_color.a = p_shininess;
}

void TextureStorage::canvas_texture_set_texture_filter(RID p_canvas_texture, RS::CanvasItemTextureFilter p_filter) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_canvas_texture);
	ERR_FAIL_NULL(ct);

	ct->texture_filter = p_filter;
}

void TextureStorage::canvas_texture_set_texture_repeat(RID p_canvas_texture, RS::CanvasItemTextureRepeat p_repeat) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_canvas_texture);
	ERR_FAIL_NULL(ct);

	ct->texture_repeat = p_repeat;
}

/* Texture API */

Ref<Image> TextureStorage::_get_gl_image_and_format(const Ref<Image> &p_image, Image::Format p_format, Image::Format &r_real_format, GLenum &r_gl_format, GLenum &r_gl_internal_format, GLenum &r_gl_type, bool &r_compressed, bool p_force_decompress) const {
	Config *config = Config::get_singleton();
	r_gl_format = 0;
	Ref<Image> image = p_image;
	r_compressed = false;
	r_real_format = p_format;

	bool need_decompress = false;
	bool decompress_ra_to_rg = false;

	switch (p_format) {
		case Image::FORMAT_L8: {
			// Trick GLES1 into treating grayscale fonts as alpha masks
			r_gl_internal_format = GL_ALPHA; 
			r_gl_format = GL_ALPHA;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_LA8: {
			r_gl_internal_format = GL_LUMINANCE_ALPHA;
			r_gl_format = GL_LUMINANCE_ALPHA;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_R8: {
			r_gl_internal_format = GL_ALPHA;
			r_gl_format = GL_ALPHA;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RG8: {
#if DEBUG_ENABLED
			ERR_PRINT_ONCE("RG8 Format is not supported by GLES1, converting to RGB.");
#endif
			if (image.is_valid()) {
				if (image == p_image) {
					image = image->duplicate();
					ERR_FAIL_COND_V_MSG(image.is_null(), p_image, "GLES1: Out of memory during image duplication.");
				}
				image->convert(Image::FORMAT_RGB8);
			}
			r_real_format = Image::FORMAT_RGB8;
			r_gl_internal_format = GL_RGB;
			r_gl_format = GL_RGB;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RGB8: {
			r_gl_internal_format = GL_RGB;
			r_gl_format = GL_RGB;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RGBA8: {
			r_gl_format = GL_RGBA;
			r_gl_internal_format = GL_RGBA;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RGBA4444: {
			r_gl_internal_format = GL_RGBA;
			r_gl_format = GL_RGBA;
			r_gl_type = GL_UNSIGNED_SHORT_4_4_4_4;
		} break;
		case Image::FORMAT_RF: {
			r_gl_internal_format = GL_ALPHA;
			r_gl_format = GL_ALPHA;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RGF: {
			if (config->float_texture_supported) {
				r_real_format = Image::FORMAT_RGBF;
				if (image.is_valid()) {
					if (image == p_image) {
						image = image->duplicate();
						ERR_FAIL_COND_V_MSG(image.is_null(), p_image, "GLES1: Out of memory during image duplication.");
					}
					image->convert(Image::FORMAT_RGBF);
				}
				r_gl_internal_format = GL_RGB;
				r_gl_format = GL_RGB;
				r_gl_type = GL_FLOAT;
			} else {
#ifdef DEBUG_ENABLED
				ERR_PRINT_ONCE("Float textures not supported by GLES1, converting RGF to RGB8.");
#endif
				if (image.is_valid()) {
					if (image == p_image) {
						image = image->duplicate();
						ERR_FAIL_COND_V_MSG(image.is_null(), p_image, "GLES1: Out of memory during image duplication.");
					}
					image->convert(Image::FORMAT_RGB8);
				}
				r_real_format = Image::FORMAT_RGB8;
				r_gl_internal_format = GL_RGB;
				r_gl_format = GL_RGB;
				r_gl_type = GL_UNSIGNED_BYTE;
			}
		} break;
		case Image::FORMAT_RGBF: {
			if (config->float_texture_supported) {
				r_gl_internal_format = GL_RGB;
				r_gl_format = GL_RGB;
				r_gl_type = GL_FLOAT;
			} else {
#ifdef DEBUG_ENABLED
				ERR_PRINT_ONCE("RGB float texture not supported by GLES1, converting to RGB8.");
#endif
				if (image.is_valid()) {
					if (image == p_image) {
						image = image->duplicate();
						ERR_FAIL_COND_V_MSG(image.is_null(), p_image, "GLES1: Out of memory during image duplication.");
					}
					image->convert(Image::FORMAT_RGB8);
				}
				r_real_format = Image::FORMAT_RGB8;
				r_gl_internal_format = GL_RGB;
				r_gl_format = GL_RGB;
				r_gl_type = GL_UNSIGNED_BYTE;
			}
		} break;
		case Image::FORMAT_RGBAF: {
			if (config->float_texture_supported) {
				r_gl_internal_format = GL_RGBA;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_FLOAT;
			} else {
#ifdef DEBUG_ENABLED
				ERR_PRINT_ONCE("RGBA float texture not supported by GLES1, converting to RGBA8.");
#endif
				if (image.is_valid()) {
					if (image == p_image) {
						image = image->duplicate();
						ERR_FAIL_COND_V_MSG(image.is_null(), p_image, "GLES1: Out of memory during image duplication.");
					}
					image->convert(Image::FORMAT_RGBA8);
				}
				r_real_format = Image::FORMAT_RGBA8;
				r_gl_internal_format = GL_RGBA;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
			}
		} break;
		case Image::FORMAT_RH:
		case Image::FORMAT_RGH:
		case Image::FORMAT_RGBH:
		case Image::FORMAT_RGBAH: {
			need_decompress = true;
		} break;
		case Image::FORMAT_RGBE9995: {
			r_gl_internal_format = GL_RGB;
			r_gl_format = GL_RGB;
			r_gl_type = GL_UNSIGNED_BYTE;
			if (image.is_valid()) {
				image = image->rgbe_to_srgb();
			}
			return image;
		} break;
		case Image::FORMAT_DXT1: {
			if (config->s3tc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RGBA_S3TC_DXT1_EXT;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_DXT3: {
			if (config->s3tc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RGBA_S3TC_DXT3_EXT;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_DXT5: {
			if (config->s3tc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_RGTC_R: {
			if (config->rgtc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RED_RGTC1_EXT;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_RGTC_RG: {
			if (config->rgtc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RED_GREEN_RGTC2_EXT;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_BPTC_RGBA: {
			if (config->bptc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RGBA_BPTC_UNORM;
				r_gl_format = GL_RGBA;
				r_gl_type = GL_UNSIGNED_BYTE;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_BPTC_RGBF: {
			if (config->bptc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
				r_gl_format = GL_RGB;
				r_gl_type = GL_FLOAT;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_BPTC_RGBFU: {
			if (config->bptc_supported) {
				r_gl_internal_format = _EXT_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
				r_gl_format = GL_RGB;
				r_gl_type = GL_FLOAT;
				r_compressed = true;
			} else {
				need_decompress = true;
			}
		} break;
		case Image::FORMAT_ETC2_R11:
		case Image::FORMAT_ETC2_R11S:
		case Image::FORMAT_ETC2_RG11:
		case Image::FORMAT_ETC2_RG11S:
		case Image::FORMAT_ETC2_RGB8:
		case Image::FORMAT_ETC2_RGBA8:
		case Image::FORMAT_ETC2_RGB8A1: {
			need_decompress = true;
		} break;
		case Image::FORMAT_ETC2_RA_AS_RG:
		case Image::FORMAT_DXT5_RA_AS_RG: {
			need_decompress = true;
			decompress_ra_to_rg = true;
		} break;
		case Image::FORMAT_ASTC_4x4:
		case Image::FORMAT_ASTC_4x4_HDR:
		case Image::FORMAT_ASTC_8x8:
		case Image::FORMAT_ASTC_8x8_HDR: {
			need_decompress = true;
		} break;
		default: {
			ERR_FAIL_V_MSG(Ref<Image>(), "The image format " + itos(p_format) + " is not supported by the GLES1 rendering backend.");
		}
	}

	if (need_decompress || p_force_decompress) {
		if (!image.is_null()) {
			Ref<Image> dup = image->duplicate();
			ERR_FAIL_COND_V_MSG(dup.is_null(), p_image, "GLES1: Out of memory during image duplication for decompression.");

			image = dup;
			image->decompress();
			ERR_FAIL_COND_V_MSG(image->is_compressed(), image, "GLES1: Image decompression failed.");
			if (decompress_ra_to_rg) {
				image->convert_ra_rgba8_to_rg();
				image->convert(Image::FORMAT_RG8);
			}
			switch (image->get_format()) {
				case Image::FORMAT_RG8:
				case Image::FORMAT_RGB8: {
					r_gl_format = GL_RGB;
					r_gl_internal_format = GL_RGB;
					r_gl_type = GL_UNSIGNED_BYTE;
					r_real_format = Image::FORMAT_RGB8;
					r_compressed = false;
				} break;
				case Image::FORMAT_RGBA8: {
					r_gl_format = GL_RGBA;
					r_gl_internal_format = GL_RGBA;
					r_gl_type = GL_UNSIGNED_BYTE;
					r_real_format = Image::FORMAT_RGBA8;
					r_compressed = false;
				} break;
				default: {
					image->convert(Image::FORMAT_RGBA8);
					r_gl_format = GL_RGBA;
					r_gl_internal_format = GL_RGBA;
					r_gl_type = GL_UNSIGNED_BYTE;
					r_real_format = Image::FORMAT_RGBA8;
					r_compressed = false;
				} break;
			}
		}
		return image;
	}

	return p_image;
}

RID TextureStorage::texture_allocate() {
	Texture texture;
	texture.tex_id = 0;
	glGenTextures(1, &texture.tex_id);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_allocate: glGenTextures");

	ERR_FAIL_COND_V_MSG(texture.tex_id == 0, RID(), "GLES1: Failed to generate texture ID. GL Context might be lost or out of memory.");

	texture.active = false;
	texture.total_data_size = 0;
	texture.target = GL_TEXTURE_2D;

	texture.context_generation = GLES1::Config::get_singleton()->context_generation;

	texture.type = Texture::TYPE_2D;
	texture.width = 0;
	texture.height = 0;
	texture.alloc_width = 0;
	texture.alloc_height = 0;
	texture.is_proxy = false;
	texture.is_render_target = false;
	texture.is_from_native_handle = false;
	texture.render_target = nullptr;
	texture.canvas_texture = nullptr;
	texture.proxy_to = RID();

	return texture_owner.make_rid(texture);
}

void TextureStorage::texture_free(RID p_rid) {
	Texture *tex = texture_owner.get_or_null(p_rid);
	if (!tex) {
		return;
	}

	if (tex->canvas_texture) {
		memdelete(tex->canvas_texture);
		tex->canvas_texture = nullptr;
	}

	bool must_free_data = false;

	// Handle Proxy Unlinking (Base <- Proxy)
	if (tex->is_proxy) {
		if (tex->proxy_to.is_valid()) {
			Texture *base = texture_owner.get_or_null(tex->proxy_to);
			if (base) {
				base->proxies.erase(p_rid);
			}
		}
	} else {
		must_free_data = (tex->tex_id != 0 && !tex->is_from_native_handle);
	}

	// Safely free the tracked data
	if (must_free_data) {
		if (GLES1::Utilities::get_singleton()->has_texture_data(tex->tex_id)) {
			GLES1::Utilities::get_singleton()->texture_free_data(tex->tex_id);
			GL_CHECK_ERROR("GLES1::TextureStorage::texture_free: utilities texture_free_data");
		} else {
			// This was an eagerly allocated empty shell.
			glDeleteTextures(1, &tex->tex_id);
			GL_CHECK_ERROR("GLES1::TextureStorage::texture_free: glDeleteTextures (empty shell)");
		}
		tex->tex_id = 0;
	}

	// Handle Reverse Proxy Unlinking (Proxy -> Base)
	for (int i = 0; i < tex->proxies.size(); i++) {
		Texture *p = texture_owner.get_or_null(tex->proxies[i]);
		if (!p) {
			continue;
		}
		p->proxy_to = RID();
		p->tex_id = 0;
	}

	texture_owner.free(p_rid);
}

void TextureStorage::texture_2d_initialize(RID p_texture, const Ref<Image> &p_image) {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);
	ERR_FAIL_COND(p_image.is_null());

	Config *config = Config::get_singleton();

	int max_size = config->max_texture_size;
	Ref<Image> image = p_image;

	// Texture atlas / max size enforcer
	if (image->get_width() > max_size || image->get_height() > max_size) {
		WARN_PRINT(vformat("GLES1: Texture size (%dx%d) exceeds hardware maximum (%d). Downscaling.", image->get_width(), image->get_height(), max_size));
		
		if (image == p_image) {
			image = p_image->duplicate();
			ERR_FAIL_COND_MSG(image.is_null(), "GLES1: Failed to duplicate image for downscaling. Out of memory.");
		}
		
		int new_w = MIN(image->get_width(), max_size);
		int new_h = MIN(image->get_height(), max_size);
		
		float aspect = (float)image->get_width() / (float)image->get_height();
		if (image->get_width() > image->get_height()) {
			new_w = max_size;
			new_h = MAX(1, (int)(max_size / aspect));
		} else {
			new_h = max_size;
			new_w = MAX(1, (int)(max_size * aspect));
		}
		
		image->resize(new_w, new_h, Image::INTERPOLATE_BILINEAR);
	}

	texture->type = Texture::TYPE_2D;
	texture->target = GL_TEXTURE_2D;
	texture->width = image->get_width();
	texture->height = image->get_height();
	texture->alloc_width = texture->width;
	texture->alloc_height = texture->height;
	texture->format = image->get_format();
	texture->active = true;
	texture->mipmaps = image->has_mipmaps() ? image->get_mipmap_count() + 1 : 1;
	texture->resize_to_po2 = false;

	// Always force NPOT textures to be POT for 1.3,
	// while letting the higher versions handle it
	// gracefully.
	if (!config->support_npot_repeat_mipmap || GLES1::Config::get_singleton()->gl_minor_version < 4) {
		int po2_width = next_power_of_2(texture->width);
		int po2_height = next_power_of_2(texture->height);

		bool is_po2 = (texture->width == po2_width) && (texture->height == po2_height);

		if (!is_po2) {
			texture->alloc_width = po2_width;
			texture->alloc_height = po2_height;
			texture->resize_to_po2 = true;
		}
	}

	_texture_set_data(p_texture, image, 0, true);
	if (!GLES1::Utilities::get_singleton()->has_texture_data(texture->tex_id)) {
		GLES1::Utilities::get_singleton()->texture_allocated_data(texture->tex_id, texture->total_data_size, "Texture 2D");
	}
}

void TextureStorage::texture_external_initialize(RID p_texture, int p_width, int p_height, uint64_t p_external_buffer) {

}

void TextureStorage::texture_2d_layered_initialize(RID p_texture, const Vector<Ref<Image>> &p_layers, RS::TextureLayeredType p_layered_type) {
	ERR_FAIL_COND(p_layers.is_empty());

	ERR_FAIL_COND(p_layered_type == RS::TEXTURE_LAYERED_CUBEMAP && p_layers.size() != 6);
	ERR_FAIL_COND_MSG(p_layered_type == RS::TEXTURE_LAYERED_CUBEMAP_ARRAY, "Cubemap Arrays are not supported in the GLES1 backend.");
	ERR_FAIL_COND_MSG(p_layered_type == RS::TEXTURE_LAYERED_2D_ARRAY, "2D Texture Arrays are not supported in the GLES1 backend.");

	const Ref<Image> &image = p_layers[0];
	{
		int valid_width = 0;
		int valid_height = 0;
		bool valid_mipmaps = false;
		Image::Format valid_format = Image::FORMAT_MAX;

		for (int i = 0; i < p_layers.size(); i++) {
			ERR_FAIL_COND(p_layers[i]->is_empty());

			if (i == 0) {
				valid_width = p_layers[i]->get_width();
				valid_height = p_layers[i]->get_height();
				valid_format = p_layers[i]->get_format();
				valid_mipmaps = p_layers[i]->has_mipmaps();
			} else {
				ERR_FAIL_COND(p_layers[i]->get_width() != valid_width);
				ERR_FAIL_COND(p_layers[i]->get_height() != valid_height);
				ERR_FAIL_COND(p_layers[i]->get_format() != valid_format);
				ERR_FAIL_COND(p_layers[i]->has_mipmaps() != valid_mipmaps);
			}
		}
	}

	GLES1::Texture texture;
	texture.width = image->get_width();
	texture.height = image->get_height();
	texture.alloc_width = texture.width;
	texture.alloc_height = texture.height;
	texture.mipmaps = image->get_mipmap_count() + 1;
	texture.format = image->get_format();
	texture.type = GLES1::Texture::TYPE_LAYERED;
	texture.layered_type = p_layered_type;
	texture.target = GL_TEXTURE_CUBE_MAP;
	texture.layers = p_layers.size();
	_get_gl_image_and_format(Ref<Image>(), texture.format, texture.real_format, texture.gl_format_cache, texture.gl_internal_format_cache, texture.gl_type_cache, texture.compressed, false);
	texture.total_data_size = p_layers[0]->get_image_data_size(texture.width, texture.height, texture.format, texture.mipmaps) * texture.layers;
	texture.active = true;
	glGenTextures(1, &texture.tex_id);

	ERR_FAIL_COND_MSG(texture.tex_id == 0, "GLES1: Failed to generate layered texture ID. GL Context lost.");

	GLES1::Utilities::get_singleton()->texture_allocated_data(texture.tex_id, texture.total_data_size, "Texture Layered");
	texture_owner.initialize_rid(p_texture, texture);
	for (int i = 0; i < p_layers.size(); i++) {
		_texture_set_data(p_texture, p_layers[i], i, i == 0);
	}
}

void TextureStorage::texture_3d_initialize(RID p_texture, Image::Format p_format, int p_width, int p_height, int p_depth, bool p_mipmaps, const Vector<Ref<Image>> &p_data) {
	WARN_PRINT("Trying to initialize a 3D texture with the GLES1 driver, which doesn't support 3D Textures!");
}

// Called internally when texture_proxy_create(p_base) is called.
// Note: p_base is the root and p_texture is the proxy.
void TextureStorage::texture_proxy_initialize(RID p_texture, RID p_base) {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(tex);
	Texture *base = texture_owner.get_or_null(p_base);
	ERR_FAIL_NULL(base);

	ERR_FAIL_COND_MSG(base->is_proxy, "GLES1: Cannot bind a proxy to another proxy. Flat hierarchy enforced.");
	ERR_FAIL_COND_MSG(p_texture == p_base, "GLES1: Texture proxy attempted to bind to itself. Aborting.");

	// Destroy the dummy tex_id created by texture_allocate() so we don't leak it
	if (tex->tex_id != 0) {
		glDeleteTextures(1, &tex->tex_id);
		tex->tex_id = 0;
	}

	// The proxy inherits the exact GL state and ID of its base
	tex->copy_from(*base);
	tex->canvas_texture = nullptr;
	tex->is_proxy = true;
	tex->proxy_to = p_base;

	// Tell the base texture that it has a proxy tracking it
	base->proxies.push_back(p_texture);
}

RID TextureStorage::texture_create_from_native_handle(RS::TextureType p_type, Image::Format p_format, uint64_t p_native_handle, int p_width, int p_height, int p_depth, int p_layers, RS::TextureLayeredType p_layered_type) {
	return RID();
}

void TextureStorage::texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer) {
	texture_set_data(p_texture, p_image, p_layer);

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(tex);
	GLES1::Utilities::get_singleton()->texture_resize_data(tex->tex_id, tex->total_data_size);

#ifdef TOOLS_ENABLED
	tex->image_cache_2d.unref();
#endif

}

void TextureStorage::texture_3d_update(RID p_texture, const Vector<Ref<Image>> &p_data) {
	WARN_PRINT("Trying to update a 3D texture with the GLES1 driver, which doesn't support 3D Textures!");
}

void TextureStorage::texture_external_update(RID p_texture, int p_width, int p_height, uint64_t p_external_buffer) {

}

void TextureStorage::texture_proxy_update(RID p_texture, RID p_proxy_to) {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(tex);
	Texture *base = texture_owner.get_or_null(p_proxy_to);
	ERR_FAIL_NULL(base);

	ERR_FAIL_COND_MSG(base->is_proxy, "GLES1: Cannot bind a proxy to another proxy. Flat hierarchy enforced.");
	ERR_FAIL_COND_MSG(p_texture == p_proxy_to, "GLES1: Texture proxy attempted to bind to itself. Aborting.");

	// Remove from old base's tracker
	if (tex->proxy_to.is_valid()) {
		Texture *old_base = texture_owner.get_or_null(tex->proxy_to);
		if (old_base) {
			old_base->proxies.erase(p_texture);
		}
	}

	tex->copy_from(*base);
	tex->canvas_texture = nullptr;
	tex->is_proxy = true;
	tex->proxy_to = p_proxy_to;

	base->proxies.push_back(p_texture);
}

void TextureStorage::texture_2d_placeholder_initialize(RID p_texture) {
	//this could be better optimized to reuse an existing image , done this way
	//for now to get it working
	Ref<Image> image = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
	image->fill(Color(1, 0, 1, 1));

	texture_2d_initialize(p_texture, image);
}

void TextureStorage::texture_2d_layered_placeholder_initialize(RID p_texture, RenderingServer::TextureLayeredType p_layered_type) {
	//this could be better optimized to reuse an existing image , done this way
	//for now to get it working
	Ref<Image> image = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
	image->fill(Color(1, 0, 1, 1));

	Vector<Ref<Image>> images;
	if (p_layered_type == RS::TEXTURE_LAYERED_2D_ARRAY) {
		images.push_back(image);
	} else {
		//cube
		for (int i = 0; i < 6; i++) {
			images.push_back(image);
		}
	}

	texture_2d_layered_initialize(p_texture, images, p_layered_type);
}

void TextureStorage::texture_3d_placeholder_initialize(RID p_texture) {
	//this could be better optimized to reuse an existing image , done this way
	//for now to get it working
	Ref<Image> image = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
	image->fill(Color(1, 0, 1, 1));

	Vector<Ref<Image>> images;
	//cube
	for (int i = 0; i < 4; i++) {
		images.push_back(image);
	}

	texture_3d_initialize(p_texture, Image::FORMAT_RGBA8, 4, 4, 4, false, images);
}

Ref<Image> TextureStorage::texture_2d_get(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Ref<Image>());

#ifdef TOOLS_ENABLED
	if (tex->image_cache_2d.is_valid() && !tex->is_render_target) {
		return tex->image_cache_2d;
	}
#endif

	Ref<Image> image;

	if (!Config::get_singleton()->support_fbo) {
		WARN_PRINT_ONCE("GLES1: CPU Image cache is empty and FBOs are not supported by the hardware. Cannot read back texture.");
		return Ref<Image>();
	}

	Vector<uint8_t> data;
	int data_size = Image::get_image_data_size(tex->alloc_width, tex->alloc_height, Image::FORMAT_RGBA8, false);

	// Pad the buffer to protect against buggy legacy drivers
	data.resize(data_size * 2);
	uint8_t *w = data.ptrw();

	GLint previous_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previous_fbo);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glGetIntegerv (framebuffer)");

	GLuint temp_framebuffer = 0;
	glGenFramebuffersOES(1, &temp_framebuffer);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glGenFramebuffers");

	GLuint temp_color_texture = 0;
	glGenTextures(1, &temp_color_texture);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glGenTextures");

	if (unlikely(temp_framebuffer == 0 || temp_color_texture == 0)) {
		if (temp_framebuffer != 0) {
			glDeleteFramebuffersOES(1, &temp_framebuffer);
		}
		if (temp_color_texture != 0) {
			glDeleteTextures(1, &temp_color_texture);
		}
		ERR_FAIL_V_MSG(Ref<Image>(), "GLES1: Failed to generate temporary FBO or Texture for readback. Context lost or out of memory.");
	}

	bind_framebuffer(temp_framebuffer);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: bind_framebuffer");

	glBindTexture(GL_TEXTURE_2D, temp_color_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->alloc_width, tex->alloc_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glTexImage2D");

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, temp_color_texture, 0);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glFramebufferTexture2D");

	// Capture states
	GLboolean prev_depth_test = 0, prev_cull_face = 0, prev_blend = 0;
	glGetBooleanv(GL_DEPTH_TEST, &prev_depth_test);
	glGetBooleanv(GL_CULL_FACE, &prev_cull_face);
	glGetBooleanv(GL_BLEND, &prev_blend);

	GLint prev_depth_func = 0;
	glGetIntegerv(GL_DEPTH_FUNC, &prev_depth_func);

	GLboolean prev_depth_mask = 0;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);

	GLboolean prev_color_mask[4] = {};
	glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);

	GLfloat prev_clear_color[4] = {};
	glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear_color);

	GLint prev_active_tex = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
	glActiveTexture(GL_TEXTURE0);
	GLint prev_bound_tex = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_bound_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: state capture");

	GLenum status = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
	if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
		bind_framebuffer(previous_fbo);
		if (temp_color_texture != 0) {
			glDeleteTextures(1, &temp_color_texture);
		}
		if (temp_framebuffer != 0) {
			glDeleteFramebuffersOES(1, &temp_framebuffer);
		}

		// Restore states before early-out
		if (prev_depth_test) {
			glEnable(GL_DEPTH_TEST);
		}
		if (prev_cull_face) {
			glEnable(GL_CULL_FACE);
		}
		if (prev_blend) {
			glEnable(GL_BLEND);
		}
		glDepthFunc(prev_depth_func);
		glDepthMask(prev_depth_mask);
		glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
		glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]);
		glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
		glActiveTexture(prev_active_tex);

		ERR_FAIL_V_MSG(Ref<Image>(), "GLES1: FBO incomplete during texture_2d_get fallback readback.");
	}

	// Setup rendering state
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDepthFunc(GL_LEQUAL);
	glColorMask(1, 1, 1, 1);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: render state setup");

	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex->tex_id);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: texture bind");

	// Backup matrices before we flatten them
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	glViewport(0, 0, tex->alloc_width, tex->alloc_height);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glClear");

	CopyEffects::get_singleton()->copy_to_rect(Rect2(0, 0, 1, 1));
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: copy_to_rect");

	// Clean up GLES1 specific texture state
	glDisable(GL_TEXTURE_2D);

	// Restore matrices
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: matrix restore");

	// Read the pixels back
	glReadPixels(0, 0, tex->alloc_width, tex->alloc_height, GL_RGBA, GL_UNSIGNED_BYTE, &w[0]);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: glReadPixels");

	// Cleanup
	bind_framebuffer(previous_fbo);
	if (temp_color_texture != 0) {
		glDeleteTextures(1, &temp_color_texture);
	}
	if (temp_framebuffer != 0) {
		glDeleteFramebuffersOES(1, &temp_framebuffer);
	}
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: cleanup glDelete");

	// Restore all states
	if (prev_depth_test) {
		glEnable(GL_DEPTH_TEST);
	}
	if (prev_cull_face) {
		glEnable(GL_CULL_FACE);
	}
	if (prev_blend) {
		glEnable(GL_BLEND);
	}

	glDepthFunc(prev_depth_func);
	glDepthMask(prev_depth_mask);
	glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
	glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]);

	glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
	glActiveTexture(prev_active_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::texture_2d_get: state restore");

	data.resize(data_size);

	ERR_FAIL_COND_V(data.is_empty(), Ref<Image>());
	image = Image::create_from_data(tex->alloc_width, tex->alloc_height, false, Image::FORMAT_RGBA8, data);
	ERR_FAIL_COND_V(image.is_null() || image->is_empty(), Ref<Image>());

	if (tex->format != Image::FORMAT_RGBA8) {
		image->convert(tex->format);
	}

	if (tex->mipmaps > 1) {
		image->generate_mipmaps();
	}

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint() && !tex->is_render_target) {
		tex->image_cache_2d = image;
	}
#endif

	return image;
}

Ref<Image> TextureStorage::texture_2d_layer_get(RID p_texture, int p_layer) const {
	ERR_FAIL_V_MSG(Ref<Image>(), "2D layered textures are currently not supported by GLES1");
}

Vector<Ref<Image>> TextureStorage::_texture_3d_read_framebuffer(GLES1::Texture *p_texture) const {
	ERR_FAIL_V_MSG(Vector<Ref<Image>>(), "Trying to get a 3D framebuffer with the GLES1 driver, which doesn't support 3D Textures!");
}

Vector<Ref<Image>> TextureStorage::texture_3d_get(RID p_texture) const {
	ERR_FAIL_V_MSG(Vector<Ref<Image>>(), "Trying to get a 3D texture with the GLES1 driver, which doesn't support 3D Textures!");
}

void TextureStorage::texture_replace(RID p_texture, RID p_by_texture) {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(tex);
	Texture *by_tex = texture_owner.get_or_null(p_by_texture);
	ERR_FAIL_NULL(by_tex);

	// Destroy the old physical GL texture
	if (!tex->is_proxy && tex->tex_id != 0) {
		if (GLES1::Utilities::get_singleton()->has_texture_data(tex->tex_id)) {
			GLES1::Utilities::get_singleton()->texture_free_data(tex->tex_id);
		} else {
			glDeleteTextures(1, &tex->tex_id);
		}
		GL_CHECK_ERROR("GLES1::TextureStorage::texture_replace: utilities texture_free_data (old texture)");
	}

	Vector<RID> old_proxies = tex->proxies;

	// Steal the GL state and ID from the new texture
	tex->copy_from(*by_tex);
	tex->proxies = old_proxies;
	tex->is_proxy = false;

	// Notify all proxy textures (the UI elements) that their base has a new ID/Size
	for (int i = 0; i < tex->proxies.size(); i++) {
		Texture *proxy = texture_owner.get_or_null(tex->proxies[i]);
		if (proxy) {
			proxy->copy_from(*tex);
			proxy->canvas_texture = nullptr;
			proxy->is_proxy = true;
			proxy->proxy_to = p_texture;
		}
	}

	// We consumed by_tex, so clear its GL ID to prevent texture_free from destroying our new texture!
	by_tex->tex_id = 0;
	by_tex->canvas_texture = nullptr;
	texture_free(p_by_texture);
}

void TextureStorage::texture_set_size_override(RID p_texture, int p_width, int p_height) {
	Texture *tex = texture_owner.get_or_null(p_texture);
	if (!tex) {
		return;
	}

	tex->width = p_width;
	tex->height = p_height;
	tex->alloc_width = p_width;
	tex->alloc_height = p_height;
}

void TextureStorage::texture_set_path(RID p_texture, const String &p_path) {

}

String TextureStorage::texture_get_path(RID p_texture) const {
	return String();
}

void TextureStorage::texture_set_detect_3d_callback(RID p_texture, RS::TextureDetectCallback p_callback, void *p_userdata) {
}

void TextureStorage::texture_set_detect_srgb_callback(RID p_texture, RS::TextureDetectCallback p_callback, void *p_userdata) {
}

void TextureStorage::texture_set_detect_normal_callback(RID p_texture, RS::TextureDetectCallback p_callback, void *p_userdata) {
}

void TextureStorage::texture_set_detect_roughness_callback(RID p_texture, RS::TextureDetectRoughnessCallback p_callback, void *p_userdata) {
}

void TextureStorage::texture_debug_usage(List<RS::TextureInfo> *r_info) {
}

void TextureStorage::texture_set_force_redraw_if_visible(RID p_texture, bool p_enable) {
}

Size2 TextureStorage::texture_size_with_proxy(RID p_texture) {
	const Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(texture, Size2());
	if (texture->is_proxy && texture->proxy_to.is_valid()) {
		const Texture *proxy = texture_owner.get_or_null(texture->proxy_to);
		return Size2(proxy->width, proxy->height);
	}
	return Size2(texture->width, texture->height);
}

void TextureStorage::texture_rd_initialize(RID p_texture, const RID &p_rd_texture, const RS::TextureLayeredType p_layer_type) {
}

RID TextureStorage::texture_get_rd_texture(RID p_texture, bool p_srgb) const {
	return RID();
}

uint64_t TextureStorage::texture_get_native_handle(RID p_texture, bool p_srgb) const {
	return 0;
}

void TextureStorage::texture_set_data(RID p_texture, const Ref<Image> &p_image, int p_layer) {
	_texture_set_data(p_texture, p_image, p_layer, false);
}

void TextureStorage::_texture_set_data(RID p_texture, const Ref<Image> &p_image, int p_layer, bool p_initialize) {
	Texture *texture = texture_owner.get_or_null(p_texture);

	ERR_FAIL_NULL(texture);
	ERR_FAIL_COND(!texture->active);
	ERR_FAIL_COND(texture->is_render_target);
	ERR_FAIL_COND(p_image.is_null());
	ERR_FAIL_COND(texture->format != p_image->get_format());
	ERR_FAIL_COND(!p_image->get_width() || !p_image->get_height());

	GLenum type;
	GLenum format;
	GLenum internal_format;
	bool compressed = false;

	Image::Format real_format;
	Ref<Image> img = _get_gl_image_and_format(p_image, p_image->get_format(), real_format, format, internal_format, type, compressed, texture->resize_to_po2);
	ERR_FAIL_COND(img.is_null());

	if (texture->resize_to_po2) {
		if (p_image->is_compressed()) {
			ERR_PRINT("Texture '" + texture->path + "' is required to be a power of 2 because it uses either mipmaps or repeat, so it was decompressed. This will hurt performance and memory usage.");
		}
		if (img == p_image) {
			img = img->duplicate();
		}
		img->resize_to_po2(false);
	}

	// Cache the image so texture_2d_get() can return it later without GPU readback
	texture->image_cache_2d = img;

	GLenum blit_target = (texture->target == GL_TEXTURE_CUBE_MAP) ? _cube_side_enum[p_layer] : texture->target;
	Vector<uint8_t> read = img->get_data();
	int expected_size = img->get_data_size();

	ERR_FAIL_COND_MSG(read.is_empty() || expected_size == 0, "GLES1: Image data is empty. Aborting GL upload to prevent driver crash.");
	ERR_FAIL_COND_MSG(read.size() < expected_size, "GLES1: Image data vector size is smaller than expected image size. Memory corruption risk.");

	const uint8_t *read_ptr = read.ptr();

#ifdef BIG_ENDIAN_ENABLED
	if (compressed) {
		// Call ptrw() to safely detach and allow modification of the byte array
		uint8_t *write_ptr = read.ptrw();
		int total_size = read.size();
		Image::Format img_format = img->get_format();

		if (img_format == Image::FORMAT_DXT1) {
			for (int i = 0; i < total_size; i += 8) {
				uint16_t *c0 = (uint16_t *)&write_ptr[i];
				uint16_t *c1 = (uint16_t *)&write_ptr[i + 2];
				uint32_t *idx = (uint32_t *)&write_ptr[i + 4];
				*c0 = BSWAP16(*c0);
				*c1 = BSWAP16(*c1);
				*idx = BSWAP32(*idx);
			}
		} else if (img_format == Image::FORMAT_DXT3) {
			for (int i = 0; i < total_size; i += 16) {
				// DXT3 Alpha is 4x 16-bit words
				uint16_t *a = (uint16_t *)&write_ptr[i];
				for (int j = 0; j < 4; j++) {
					a[j] = BSWAP16(a[j]);
				}

				// DXT3 Color block
				uint16_t *c0 = (uint16_t *)&write_ptr[i + 8];
				uint16_t *c1 = (uint16_t *)&write_ptr[i + 10];
				uint32_t *idx = (uint32_t *)&write_ptr[i + 12];
				*c0 = BSWAP16(*c0);
				*c1 = BSWAP16(*c1);
				*idx = BSWAP32(*idx);
			}
		} else if (img_format == Image::FORMAT_DXT5) {
			for (int i = 0; i < total_size; i += 16) {
				// The Alpha block (bytes 0-7) consists of 2 single-byte alphas and a 48-bit index.
				// Big Endian GPUs universally expect these 8 bytes
				// to be swapped as four 16-bit words.
				uint16_t *a = (uint16_t *)&write_ptr[i];
				for (int j = 0; j < 4; j++) {
					a[j] = BSWAP16(a[j]);
				}

				// The color block (bytes 8-15) is identical to DXT1
				uint16_t *c0 = (uint16_t *)&write_ptr[i + 8];
				uint16_t *c1 = (uint16_t *)&write_ptr[i + 10];
				uint32_t *idx = (uint32_t *)&write_ptr[i + 12];
				*c0 = BSWAP16(*c0);
				*c1 = BSWAP16(*c1);
				*idx = BSWAP32(*idx);
			}
		}
		// Re-assign read_ptr in case calling ptrw()
		// triggered a memory reallocation
		read_ptr = read.ptr();
	}
#endif

	// Capture previous state
	GLint prev_active_tex = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
	glActiveTexture(GL_TEXTURE0);
	GLint prev_bound_tex = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_bound_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::_texture_set_data: texture state capture");

	glBindTexture(texture->target, texture->tex_id);

	// Default to Clamp-to-Edge to prevent possible NPOT black screens.
	// Default to Linear filtering so mipmap checks don't fail an incomplete texture.
	glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GL_CHECK_ERROR("GLES1::TextureStorage::_texture_set_data: glTexParameteri");

	int mipmaps = img->has_mipmaps() ? img->get_mipmap_count() + 1 : 1;
	int w = img->get_width();
	int h = img->get_height();
	int tsize = 0;

	for (int i = 0; i < mipmaps; i++) {
		int64_t size, ofs;
		img->get_mipmap_offset_and_size(i, ofs, size);

		if (compressed) {
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glCompressedTexImage2D(blit_target, i, internal_format, w, h, 0, size, read_ptr + ofs);
			GL_CHECK_ERROR("GLES1::TextureStorage::_texture_set_data: glCompressedTexImage2D");
		} else {
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(blit_target, i, internal_format, w, h, 0, format, type, read_ptr + ofs);
			GL_CHECK_ERROR("GLES1::TextureStorage::_texture_set_data: glTexImage2D");
		}

		tsize += size;
		w = MAX(1, w >> 1);
		h = MAX(1, h >> 1);
	}

	texture->total_data_size = tsize;
	texture->stored_cube_sides |= (1 << p_layer);
	texture->mipmaps = mipmaps;

	// Restore state
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(texture->target, prev_bound_tex);
	glActiveTexture(prev_active_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::_texture_set_data: texture state capture restore");
}

void TextureStorage::_texture_set_3d_data(RID p_texture, const Vector<Ref<Image>> &p_data, bool p_initialize) {

}

void TextureStorage::_texture_set_swizzle(GLES1::Texture *p_texture, Image::Format p_real_format) {

}

Image::Format TextureStorage::texture_get_format(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Image::FORMAT_L8);
	return tex->format;
}

uint32_t TextureStorage::texture_get_texid(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, 0);
	return tex->tex_id;
}

Vector3i TextureStorage::texture_get_size(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Vector3i(0, 0, 0));
	return Vector3i(tex->width, tex->height, tex->depth);
}

uint32_t TextureStorage::texture_get_width(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, 0);
	return tex->width;
}

uint32_t TextureStorage::texture_get_height(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, 0);
	return tex->height;
}

uint32_t TextureStorage::texture_get_depth(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, 0);
	return tex->depth;
}

void TextureStorage::texture_bind(RID p_texture, uint32_t p_texture_no) {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(tex);
	glActiveTexture(GL_TEXTURE0 + p_texture_no);
	glBindTexture(tex->target, tex->tex_id);
}

GLES1::Texture *TextureStorage::texture_bind_and_validate(RID p_texture, GLenum p_texture_unit, RS::CanvasItemTextureFilter p_filter, RS::CanvasItemTextureRepeat p_repeat) {
	Texture *texture = get_texture(p_texture);

	if (texture && texture->context_generation != GLES1::Config::get_singleton()->context_generation) {
		texture->tex_id = 0; // Handle is dead/stolen by the new context
		texture->context_generation = GLES1::Config::get_singleton()->context_generation;
	}

	if (texture && texture->tex_id == 0 && texture->active) {
		if (texture->image_cache_2d.is_valid()) {
#ifdef DEBUG_ENABLED
			print_verbose("Resurrecting dead texture from CPU cache centrally.");
#endif
			glGenTextures(1, &texture->tex_id);
			if (likely(texture->tex_id != 0)) {
				_texture_set_data(p_texture, texture->image_cache_2d, 0, false);
				glActiveTexture(p_texture_unit);
				glBindTexture(texture->target, texture->tex_id);
				if (!GLES1::Utilities::get_singleton()->has_texture_data(texture->tex_id)) {
					GLES1::Utilities::get_singleton()->texture_allocated_data(texture->tex_id, texture->total_data_size, "Resurrected Texture");
				}
			} else {
				WARN_PRINT_ONCE("GLES1: Failed to resurrect texture. Context lost.");
				texture->active = false;
			}
		} else {
			WARN_PRINT_ONCE("Cannot recover texture centrally, CPU cache is empty.");
			texture->active = false;
		}
	}

	if (!texture || texture->tex_id == 0 || !texture->active || texture->width <= 0 || texture->height <= 0) {
#if defined(DEBUG_ENABLED) || defined(TOOLS_ENABLED)
		WARN_PRINT_ONCE("Texture missing or unrecoverable. Routing to safe fallback.");
#endif
		RID safe_fallback = texture_gl_get_default(DEFAULT_GL_TEXTURE_WHITE);
		texture = get_texture(safe_fallback);

		// Ensure the fallback itself isn't tainted.
		if (
			texture &&
			texture->context_generation != GLES1::Config::get_singleton()->context_generation
		) {
			texture->tex_id = 0;
			texture->context_generation = GLES1::Config::get_singleton()->context_generation;
			glGenTextures(1, &texture->tex_id);
			Ref<Image> white_img = Image::create_empty(8, 8, false, Image::FORMAT_RGB8);
			white_img->fill(Color(1, 1, 1, 1));
			texture_2d_initialize(safe_fallback, white_img);
		}
	}

	if (likely(texture && texture->tex_id != 0)) {
		glActiveTexture(p_texture_unit);

		if (unlikely(texture->target == 0)) {
			texture->target = GL_TEXTURE_2D;
		}

		glEnable(texture->target);
		glBindTexture(texture->target, texture->tex_id);
		GL_CHECK_ERROR("GLES1::TextureStorage::texture_bind_and_validate: glBindTexture");

		if (GLES1::Config::get_singleton()->support_texture_env_combine) {
			if (texture->gl_internal_format_cache == GL_ALPHA || texture->gl_internal_format_cache == GL_LUMINANCE_ALPHA) {
				// Bypass desktop emulator bug where GL_ALPHA RGB is treated as 0.0
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

				// RGB = Vertex Color
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

				// Alpha = Vertex Alpha * Texture Alpha
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
				glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
				GL_CHECK_ERROR("GLES1::TextureStorage::texture_bind_and_validate: emulator bug / support_texture_env_combine glTexEnvi");
			} else {
				// Standard textures behave normally
				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_CHECK_ERROR("GLES1::TextureStorage::texture_bind_and_validate: support_texture_env_combine glTexEnvi");
			}
		}

		// Apply filters and repeats
		texture->gl_set_filter(p_filter);
		texture->gl_set_repeat(p_repeat);

		if (texture->render_target) {
			texture->render_target->used_in_frame = true;
		}
		return texture;
	}

	return nullptr;
}

/* TEXTURE ATLAS API */

void TextureStorage::texture_add_to_texture_atlas(RID p_texture) {

}

void TextureStorage::texture_remove_from_texture_atlas(RID p_texture) {

}

void TextureStorage::texture_atlas_mark_dirty_on_texture(RID p_texture) {

}

void TextureStorage::texture_atlas_remove_texture(RID p_texture) {
	if (texture_atlas.textures.has(p_texture)) {
		texture_atlas.textures.erase(p_texture);
		// There is not much a point of making it dirty, texture can be removed next time the atlas is updated.
	}
}

GLuint TextureStorage::texture_atlas_get_texture() const {
	return 0;
}

void TextureStorage::update_texture_atlas() {

}

/* DECAL API */

RID TextureStorage::decal_allocate() {
	return RID();
}

void TextureStorage::decal_initialize(RID p_rid) {
}

void TextureStorage::decal_set_size(RID p_decal, const Vector3 &p_size) {
}

void TextureStorage::decal_set_texture(RID p_decal, RS::DecalTexture p_type, RID p_texture) {
}

void TextureStorage::decal_set_emission_energy(RID p_decal, float p_energy) {
}

void TextureStorage::decal_set_albedo_mix(RID p_decal, float p_mix) {
}

void TextureStorage::decal_set_modulate(RID p_decal, const Color &p_modulate) {
}

void TextureStorage::decal_set_cull_mask(RID p_decal, uint32_t p_layers) {
}

void TextureStorage::decal_set_distance_fade(RID p_decal, bool p_enabled, float p_begin, float p_length) {
}

void TextureStorage::decal_set_fade(RID p_decal, float p_above, float p_below) {
}

void TextureStorage::decal_set_normal_fade(RID p_decal, float p_fade) {
}

AABB TextureStorage::decal_get_aabb(RID p_decal) const {
	return AABB();
}

/* RENDER TARGET API */

GLuint TextureStorage::system_fbo = 0;

void TextureStorage::_update_render_target(RenderTarget *rt) {
	// Do not allocate a render target with no size
	if (rt->size.x <= 0 || rt->size.y <= 0) {
		return;
	}

	Config *config = Config::get_singleton();

	// Clamp RT size to max viewport dimensions.
	// Old drivers may silently ignore rendering if FBO > Max Viewport.
	if (rt->size.x > config->max_viewport_size[0] || rt->size.y > config->max_viewport_size[1]) {
		WARN_PRINT(vformat("GLES1: RenderTarget size (%dx%d) exceeds hardware maximum viewport size (%dx%d). Clamping.", rt->size.x, rt->size.y, config->max_viewport_size[0], config->max_viewport_size[1]));
		rt->size.x = MIN(rt->size.x, config->max_viewport_size[0]);
		rt->size.y = MIN(rt->size.y, config->max_viewport_size[1]);
	}

	// If drawing directly to the screen, or
	// the GPU flat out doesn't support FBOs
	if (rt->direct_to_screen || !config->support_fbo) {
		rt->fbo = system_fbo;
		rt->direct_to_screen = true;
		return;
	}
	
	// Capture state
	GLint prev_scissor_test = 0;
	glGetIntegerv(GL_SCISSOR_TEST, &prev_scissor_test);

	GLboolean prev_depth_mask = 0;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);

	GLboolean prev_color_mask[4] = {};
	glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);

	GLfloat prev_clear_color[4] = {};
	glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear_color);

	GLint prev_active_tex = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
	glActiveTexture(GL_TEXTURE0);

	GLint prev_bound_tex = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_bound_tex);

	GLint prev_renderbuffer = 0;
	glGetIntegerv(GL_RENDERBUFFER_BINDING_OES, &prev_renderbuffer);
	GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: state capture");

	glDisable(GL_SCISSOR_TEST);
	glColorMask(1, 1, 1, 1);
	glDepthMask(GL_FALSE);

	Texture *texture;
	GLenum texture_target = GL_TEXTURE_2D;

	// FBO Generation
	glGenFramebuffersOES(1, &rt->fbo);
	GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glGenFramebuffersOES");

	if (unlikely(rt->fbo == 0)) {
		// Restore state
		if (prev_scissor_test) {
			glEnable(GL_SCISSOR_TEST);
		}
		glDepthMask(prev_depth_mask);
		glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
		glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]);
		glBindRenderbufferOES(GL_RENDERBUFFER_OES, prev_renderbuffer);
		glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
		glActiveTexture(prev_active_tex);

		ERR_FAIL_MSG("GLES1: Failed to generate Framebuffer Object. Context lost?");
	}

	bind_framebuffer(rt->fbo);

	GLenum target_color_internal_format = rt->is_transparent ? GL_RGBA : GL_RGB;
	GLenum target_color_format = rt->is_transparent ? GL_RGBA : GL_RGB;

	struct FBOFallback {
		GLenum color_type;
		bool use_depth;
		uint32_t color_format_size;
		Image::Format image_format;
	};

	// Fallback chain for strict software rasterizers
	FBOFallback fallbacks[] = {
		{ GL_UNSIGNED_BYTE, true, rt->is_transparent ? 4u : 3u, rt->is_transparent ? Image::FORMAT_RGBA8 : Image::FORMAT_RGB8 },
		{ GL_UNSIGNED_BYTE, false, rt->is_transparent ? 4u : 3u, rt->is_transparent ? Image::FORMAT_RGBA8 : Image::FORMAT_RGB8 },
		{ rt->is_transparent ? (GLenum)GL_UNSIGNED_SHORT_4_4_4_4 : (GLenum)GL_UNSIGNED_SHORT_5_6_5, true, 2u, rt->is_transparent ? Image::FORMAT_RGBA4444 : Image::FORMAT_RGB8 },
		{ rt->is_transparent ? (GLenum)GL_UNSIGNED_SHORT_4_4_4_4 : (GLenum)GL_UNSIGNED_SHORT_5_6_5, false, 2u, rt->is_transparent ? Image::FORMAT_RGBA4444 : Image::FORMAT_RGB8 }
	};

	bool fbo_complete = false;
	GLenum status = GL_FRAMEBUFFER_UNSUPPORTED_OES;

	for (int i = 0; i < 4; i++) {
		FBOFallback fb = fallbacks[i];

		rt->color_internal_format = target_color_internal_format;
		rt->color_format = target_color_format;
		rt->color_type = fb.color_type;
		rt->color_format_size = fb.color_format_size;
		rt->image_format = fb.image_format;

		if (rt->overridden.color.is_valid()) {
			texture = get_texture(rt->overridden.color);
			if (unlikely(!texture)) {
				break;
			}
			rt->color = texture->tex_id;
			rt->size = Size2i(texture->width, texture->height);
		} else {
			texture = get_texture(rt->texture);
			if (unlikely(!texture)) {
				break;
			}

			glGenTextures(1, &rt->color);
			if (unlikely(rt->color == 0)) {
				break;
			}
			glBindTexture(texture_target, rt->color);

			glTexImage2D(texture_target, 0, rt->color_internal_format, rt->size.x, rt->size.y, 0, rt->color_format, rt->color_type, nullptr);
			GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glTexImage2D color allocation");

			// Force the texture parameters immediately after creating the color texture
			glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glTexParameteri color");

			texture->gl_set_filter(RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
			texture->gl_set_repeat(RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		}

		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, texture_target, rt->color, 0);
		GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glFramebufferTexture2DOES color attach");

		if (fb.use_depth) {
			if (rt->overridden.depth.is_valid()) {
				Texture *depth_tex = get_texture(rt->overridden.depth);
				if (unlikely(!depth_tex)) {
					break;
				}
				rt->depth = depth_tex->tex_id;
				glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, texture_target, rt->depth, 0);
				GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glFramebufferTexture2DOES depth override attach");
			} else {
				// Use a Renderbuffer for depth.
				glGenRenderbuffersOES(1, &rt->depth);
				glBindRenderbufferOES(GL_RENDERBUFFER_OES, rt->depth);
				glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, rt->size.x, rt->size.y);
				GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glRenderbufferStorageOES depth allocation");

				glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, rt->depth);
				GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glFramebufferRenderbufferOES depth attach");
			}
		}

		status = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
		if (status == GL_FRAMEBUFFER_COMPLETE_OES) {
			fbo_complete = true;
			if (rt->overridden.color.is_null() && !GLES1::Utilities::get_singleton()->has_texture_data(rt->color)) {
				GLES1::Utilities::get_singleton()->texture_allocated_data(rt->color, rt->size.x * rt->size.y * rt->view_count * rt->color_format_size, "Render target color texture");
			}
			if (fb.use_depth && rt->overridden.depth.is_null() && !GLES1::Utilities::get_singleton()->has_render_buffer_data(rt->depth)) {
				GLES1::Utilities::get_singleton()->render_buffer_allocated_data(rt->depth, rt->size.x * rt->size.y * rt->view_count * 2, "Render target depth texture");
			}
			break;
		}

		// Sterilize attachments for the next fallback iteration
		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, texture_target, 0, 0);
		if (fb.use_depth) {
			if (rt->overridden.depth.is_valid()) {
				glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, texture_target, 0, 0);
			} else {
				glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, 0);
				if (rt->depth != 0) {
					glDeleteRenderbuffersOES(1, &rt->depth);
				}
			}
			rt->depth = 0;
		}

		if (rt->overridden.color.is_null() && rt->color != 0) {
			glDeleteTextures(1, &rt->color);
			rt->color = 0;
		}
	}

	if (!fbo_complete || !texture) {
		if (rt->fbo != 0 && rt->fbo != system_fbo) {
			glDeleteFramebuffersOES(1, &rt->fbo);
			GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: glDeleteFramebuffersOES fallback");
		}

		if (prev_scissor_test) {
			glEnable(GL_SCISSOR_TEST);
		}
		glDepthMask(prev_depth_mask);
		glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
		glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]);
		glBindRenderbufferOES(GL_RENDERBUFFER_OES, prev_renderbuffer);
		glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
		glActiveTexture(prev_active_tex);

		rt->fbo = 0;
		rt->size.x = 0;
		rt->size.y = 0;
		rt->color = 0;
		rt->depth = 0;

		if (rt->overridden.color.is_null() && texture) {
			texture->tex_id = 0;
			texture->active = false;
		}
		WARN_PRINT("Could not create render target FBO, status: " + get_framebuffer_error(status));

		// Route to the system backbuffer instead of leaving a broken 0-handle
		rt->fbo = system_fbo;
		rt->direct_to_screen = true;
		rt->size = DisplayServer::get_singleton()->window_get_size();

		// Attach the system backbuffer
		bind_framebuffer(system_fbo);
		return;
	}

	// Update the backing texture so the engine knows about the FBO's memory
	texture->is_render_target = true;
	texture->render_target = rt;
	if (rt->overridden.color.is_null()) {
		texture->format = rt->image_format;
		texture->real_format = rt->image_format;
		texture->target = texture_target;
		texture->type = Texture::TYPE_2D;
		texture->layers = 1;
		texture->gl_format_cache = rt->color_format;
		texture->gl_type_cache = GL_UNSIGNED_BYTE;
		texture->gl_internal_format_cache = rt->color_internal_format;
		texture->tex_id = rt->color;
		texture->width = rt->size.x;
		texture->alloc_width = rt->size.x;
		texture->height = rt->size.y;
		texture->alloc_height = rt->size.y;
		texture->active = true;
	}

	// Clear out FBO garbage
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	bind_framebuffer(system_fbo);

	if (prev_scissor_test) {
		glEnable(GL_SCISSOR_TEST);
	}
	glDepthMask(prev_depth_mask);
	glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
	glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]);
	glBindRenderbufferOES(GL_RENDERBUFFER_OES, prev_renderbuffer);
	glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
	glActiveTexture(prev_active_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::_update_render_target: state restore");
}

void TextureStorage::_create_render_target_backbuffer(RenderTarget *rt) {

}

void GLES1::TextureStorage::check_backbuffer(RenderTarget *rt, const bool uses_screen_texture, const bool uses_depth_texture) {
}

void TextureStorage::_clear_render_target(RenderTarget *rt) {
	if (!rt) {
		return;
	}

	if (Config::get_singleton()->support_fbo) {
		if (rt->fbo != 0 && rt->fbo != system_fbo) {
			glDeleteFramebuffersOES(1, &rt->fbo);
			GL_CHECK_ERROR("GLES1::TextureStorage::_clear_render_target: glDeleteFramebuffersOES");
			rt->fbo = 0;
		}

		if (rt->depth != 0 && !rt->overridden.depth.is_valid()) {
			if (GLES1::Utilities::get_singleton()->has_render_buffer_data(rt->depth)) {
				GLES1::Utilities::get_singleton()->render_buffer_free_data(rt->depth);
			} else {
				glDeleteRenderbuffersOES(1, &rt->depth);
			}
			GL_CHECK_ERROR("GLES1::TextureStorage::_clear_render_target: glDeleteRenderbuffersOES");
			rt->depth = 0;
		}
	}

	if (rt->color != 0 && !rt->overridden.color.is_valid()) {
		if (GLES1::Utilities::get_singleton()->has_texture_data(rt->color)) {
			GLES1::Utilities::get_singleton()->texture_free_data(rt->color);
		} else {
			glDeleteTextures(1, &rt->color);
		}
		GL_CHECK_ERROR("GLES1::TextureStorage::_clear_render_target: glDeleteTextures (color)");
		rt->color = 0;

		Texture *texture = texture_owner.get_or_null(rt->texture);
		if (texture) {
			texture->tex_id = 0;
			texture->active = false;
		}
	}

	if (rt->backbuffer != 0) {
		if (GLES1::Utilities::get_singleton()->has_texture_data(rt->backbuffer)) {
			GLES1::Utilities::get_singleton()->texture_free_data(rt->backbuffer);
		} else {
			glDeleteTextures(1, &rt->backbuffer);
		}
		GL_CHECK_ERROR("GLES1::TextureStorage::_clear_render_target: glDeleteTextures (backbuffer)");
		rt->backbuffer = 0;
	}
}

RID TextureStorage::render_target_create() {
	RenderTarget rt;
	rt.texture = texture_allocate();

	RID rid = render_target_owner.make_rid(rt);

	// Get the real pointer to link them
	RenderTarget *real_rt = render_target_owner.get_or_null(rid);
	Texture *tex = texture_owner.get_or_null(real_rt->texture);

	if (tex) {
		tex->is_render_target = true;
		tex->render_target = real_rt;
	}

	return rid;
}

void TextureStorage::render_target_free(RID p_rid) {
	RenderTarget *rt = render_target_owner.get_or_null(p_rid);
	if (rt) {
		_clear_render_target(rt);

		for (const KeyValue<uint32_t, RenderTarget::RTOverridden::FBOCacheEntry> &E : rt->overridden.fbo_cache) {
			GLuint fbo_id = E.value.fbo;
			if (fbo_id != 0 && fbo_id != system_fbo) {
				glDeleteFramebuffersOES(1, &fbo_id);
				GL_CHECK_ERROR("GLES1::TextureStorage::render_target_free: glDeleteFramebuffersOES cache");
			}
			for (int i = 0; i < E.value.allocated_textures.size(); i++) {
				GLuint tex_id = E.value.allocated_textures[i];
				if (GLES1::Utilities::get_singleton()->has_texture_data(tex_id)) {
					GLES1::Utilities::get_singleton()->texture_free_data(tex_id);
					GL_CHECK_ERROR("GLES1::TextureStorage::render_target_free: texture_free_data cache");
				} else if (GLES1::Utilities::get_singleton()->has_render_buffer_data(tex_id)) {
					GLES1::Utilities::get_singleton()->render_buffer_free_data(tex_id);
					GL_CHECK_ERROR("GLES1::TextureStorage::render_target_free: render_buffer_free_data cache");
				} else {
					glDeleteTextures(1, &tex_id);
					GL_CHECK_ERROR("GLES1::TextureStorage::render_target_free: glDeleteTextures cache");
				}
			}
		}
		rt->overridden.fbo_cache.clear();

		// If the RenderTarget owns a proxy texture, mark it for deletion too
		if (rt->texture.is_valid()) {
			texture_free(rt->texture);
		}

		render_target_owner.free(p_rid);
	}
}

void TextureStorage::render_target_set_position(RID p_render_target, int p_x, int p_y) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	rt->position = Point2i(p_x, p_y);
}

Point2i TextureStorage::render_target_get_position(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Point2i());

	return rt->position;
}

void TextureStorage::render_target_set_size(RID p_render_target, int p_width, int p_height, uint32_t p_view_count) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	// If the size hasn't actually changed, do nothing to save performance
	if (rt->size.width == p_width && rt->size.height == p_height && rt->view_count == p_view_count) {
		return;
	}

	_clear_render_target(rt);

	rt->size.width = p_width;
	rt->size.height = p_height;
	rt->view_count = p_view_count;

	_update_render_target(rt);
}

Size2i TextureStorage::render_target_get_size(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Size2i());

	return rt->size;
}

void TextureStorage::render_target_set_override(RID p_render_target, RID p_color_texture, RID p_depth_texture, RID p_velocity_texture, RID p_velocity_depth_texture) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	ERR_FAIL_COND(rt->direct_to_screen);

	rt->overridden.velocity = p_velocity_texture;

	if (rt->overridden.color == p_color_texture && rt->overridden.depth == p_depth_texture) {
		return;
	}

	if (p_color_texture.is_null() && p_depth_texture.is_null()) {
		_clear_render_target(rt);
		_update_render_target(rt);
		return;
	}

	if (!rt->overridden.is_overridden) {
		_clear_render_target(rt);
	}

	rt->overridden.color = p_color_texture;
	rt->overridden.depth = p_depth_texture;
	rt->overridden.is_overridden = true;

	uint32_t hash_key = hash_murmur3_one_64(p_color_texture.get_id());
	hash_key = hash_murmur3_one_64(p_depth_texture.get_id(), hash_key);
	hash_key = hash_fmix32(hash_key);

	RBMap<uint32_t, RenderTarget::RTOverridden::FBOCacheEntry>::Element *cache;
	if ((cache = rt->overridden.fbo_cache.find(hash_key)) != nullptr) {
		rt->fbo = cache->get().fbo;
		rt->color = cache->get().color;
		rt->depth = cache->get().depth;
		rt->size = cache->get().size;
		rt->texture = p_color_texture;
		return;
	}

	_update_render_target(rt);

	RenderTarget::RTOverridden::FBOCacheEntry new_entry;
	new_entry.fbo = rt->fbo;
	new_entry.color = rt->color;
	new_entry.depth = rt->depth;
	new_entry.size = rt->size;
	// Keep track of any textures we had to allocate because they weren't overridden.
	if (p_color_texture.is_null()) {
		new_entry.allocated_textures.push_back(rt->color);
	}
	if (p_depth_texture.is_null()) {
		new_entry.allocated_textures.push_back(rt->depth);
	}
	rt->overridden.fbo_cache.insert(hash_key, new_entry);
}

RID TextureStorage::render_target_get_override_color(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());

	return rt->overridden.color;
}

RID TextureStorage::render_target_get_override_depth(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());

	return rt->overridden.depth;
}

RID TextureStorage::render_target_get_override_velocity(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());

	return rt->overridden.velocity;
}

void TextureStorage::render_target_set_render_region(RID p_render_target, const Rect2i &p_render_region) {
}

Rect2i TextureStorage::render_target_get_render_region(RID p_render_target) const {
	return Rect2i();
}

RID TextureStorage::render_target_get_texture(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());

	if (rt->overridden.color.is_valid()) {
		return rt->overridden.color;
	}
	return rt->texture;
}

void TextureStorage::render_target_set_transparent(RID p_render_target, bool p_transparent) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	rt->is_transparent = p_transparent;

	if (rt->overridden.color.is_null()) {
		_clear_render_target(rt);
		_update_render_target(rt);
	}
}

bool TextureStorage::render_target_get_transparent(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);

	return rt->is_transparent;
}

void TextureStorage::render_target_set_direct_to_screen(RID p_render_target, bool p_direct_to_screen) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	if (p_direct_to_screen == rt->direct_to_screen) {
		return;
	}
	_clear_render_target(rt);
	rt->direct_to_screen = p_direct_to_screen;
	if (rt->direct_to_screen) {
		rt->overridden.color = RID();
		rt->overridden.depth = RID();
		rt->overridden.velocity = RID();
	}
	_update_render_target(rt);
}

bool TextureStorage::render_target_get_direct_to_screen(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);

	return rt->direct_to_screen;
}

bool TextureStorage::render_target_was_used(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->was_used; 
}

void TextureStorage::render_target_clear_used(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->was_used = false;
}

void TextureStorage::render_target_set_msaa(RID p_render_target, RS::ViewportMSAA p_msaa) {

}

RS::ViewportMSAA TextureStorage::render_target_get_msaa(RID p_render_target) const {
	return RS::VIEWPORT_MSAA_DISABLED;
}

void TextureStorage::render_target_set_use_hdr(RID p_render_target, bool p_use_hdr_2d) {

}

bool TextureStorage::render_target_is_using_hdr(RID p_render_target) const {
	return false;
}

GLuint TextureStorage::render_target_get_color_internal_format(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, GL_RGBA);

	return rt->color_internal_format;
}

GLuint TextureStorage::render_target_get_color_format(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, GL_RGBA);

	return rt->color_format;
}

GLuint TextureStorage::render_target_get_color_type(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, GL_UNSIGNED_BYTE);

	return rt->color_type;
}

uint32_t TextureStorage::render_target_get_color_format_size(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, 4);

	return rt->color_format_size;
}

void TextureStorage::render_target_request_clear(RID p_render_target, const Color &p_clear_color) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	rt->clear_requested = true;
	rt->clear_color = p_clear_color;
}

bool TextureStorage::render_target_is_clear_requested(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->clear_requested;
}

Color TextureStorage::render_target_get_clear_request_color(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Color());
	return rt->clear_color;
}

void TextureStorage::render_target_disable_clear_request(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->clear_requested = false;
}

void TextureStorage::render_target_do_clear_request(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	if (!rt->clear_requested) {
		return;
	}

	// Steer the GPU to the correct framebuffer
	bind_framebuffer(rt->fbo);

	// Capture and neutralize masks that restrict glClear
	GLint prev_scissor_test = 0;
	glGetIntegerv(GL_SCISSOR_TEST, &prev_scissor_test);
	GLboolean prev_color_mask[4] = {};
	glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
	GLfloat prev_clear_color[4] = {};
	glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear_color);
	GL_CHECK_ERROR("GLES1::TextureStorage::render_target_do_clear_request: state capture");

	glDisable(GL_SCISSOR_TEST);
	glColorMask(1, 1, 1, 1);

	// Paint the color.
	glClearColor(rt->clear_color.r, rt->clear_color.g, rt->clear_color.b, rt->clear_color.a);
	glClear(GL_COLOR_BUFFER_BIT);

	rt->clear_requested = false;

	// Restore states to exact previous condition
	if (prev_scissor_test) {
		glEnable(GL_SCISSOR_TEST);
	}
	glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
	glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]);
	GL_CHECK_ERROR("GLES1::TextureStorage::render_target_do_clear_request: state restore");

	// Reset back to the main window
	bind_framebuffer(system_fbo);
}

GLuint TextureStorage::render_target_get_fbo(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, 0);
	return rt->fbo;
}

GLuint TextureStorage::render_target_get_color(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, 0);

	if (rt->overridden.color.is_valid()) {
		Texture *texture = get_texture(rt->overridden.color);
		ERR_FAIL_NULL_V(texture, 0);

		return texture->tex_id;
	}
	return rt->color;
}

GLuint TextureStorage::render_target_get_depth(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, 0);
	return rt->depth;
}

bool TextureStorage::render_target_get_depth_has_stencil(RID p_render_target) const {
	return false;
}

void TextureStorage::render_target_set_reattach_textures(RID p_render_target, bool p_reattach_textures) const {

}

bool TextureStorage::render_target_is_reattach_textures(RID p_render_target) const {
	return false;
}

void TextureStorage::render_target_set_sdf_size_and_scale(RID p_render_target, RS::ViewportSDFOversize p_size, RS::ViewportSDFScale p_scale) {

}

Rect2i TextureStorage::_render_target_get_sdf_rect(const RenderTarget *rt) const {
	return Rect2i();
}

Rect2i TextureStorage::render_target_get_sdf_rect(RID p_render_target) const {
	return Rect2i();
}

void TextureStorage::render_target_mark_sdf_enabled(RID p_render_target, bool p_enabled) {

}

bool TextureStorage::render_target_is_sdf_enabled(RID p_render_target) const {
	return false;
}

GLuint TextureStorage::render_target_get_sdf_texture(RID p_render_target) {
	return 0;
}

void TextureStorage::_render_target_allocate_sdf(RenderTarget *rt) {

}

void TextureStorage::_render_target_clear_sdf(RenderTarget *rt) {

}

GLuint TextureStorage::render_target_get_sdf_framebuffer(RID p_render_target) {
	return 0;
}

void TextureStorage::render_target_sdf_process(RID p_render_target) {

}

void TextureStorage::render_target_copy_to_back_buffer(RID p_render_target, const Rect2i &p_region, bool p_gen_mipmaps) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);

	GLint prev_active_tex = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
	glActiveTexture(GL_TEXTURE0);
	GLint prev_bound_tex = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_bound_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::render_target_copy_to_back_buffer: glGetIntegerv GL_TEXTURE0 texture state hijack");

	// Generate the backbuffer texture
	if (rt->backbuffer == 0) {
		glGenTextures(1, &rt->backbuffer);

		if (unlikely(rt->backbuffer == 0)) {
			// Restore hijacked active texture
			glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
			glActiveTexture(prev_active_tex);
			ERR_FAIL_MSG("GLES1: Failed to allocate backbuffer texture.");
		}

		glBindTexture(GL_TEXTURE_2D, rt->backbuffer);

		// NPOT enforcer
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GL_CHECK_ERROR("GLES1::TextureStorage::render_target_copy_to_back_buffer: glTexParameteri");

		// Allocate the VRAM for the texture,
		// matching the FBO's format and size
		glTexImage2D(GL_TEXTURE_2D, 0, rt->color_internal_format, rt->size.width, rt->size.height, 0, rt->color_format, GL_UNSIGNED_BYTE, nullptr);
		GL_CHECK_ERROR("GLES1::TextureStorage::render_target_copy_to_back_buffer: glTexImage2D backbuffer allocation");

		if (!GLES1::Utilities::get_singleton()->has_texture_data(rt->backbuffer)) {
			GLES1::Utilities::get_singleton()->texture_allocated_data(rt->backbuffer, rt->size.width * rt->size.height * rt->color_format_size, "Render target backbuffer");
		}
	} else {
		glBindTexture(GL_TEXTURE_2D, rt->backbuffer);
	}

	// The FBO we want to copy from is already bound by the engine at this exact moment.
	// We simply command OpenGL to rip the pixels from the FBO and write them into our bound texture.
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0,
		// Destination x, y in the texture
		p_region.position.x, p_region.position.y,

		// Source x, y from the FBO
		p_region.position.x, p_region.position.y,

		// Width and height to copy
		p_region.size.width, p_region.size.height
	);
	GL_CHECK_ERROR("GLES1::TextureStorage::render_target_copy_to_back_buffer: glCopyTexSubImage2D");

	// Clean up
	glBindTexture(GL_TEXTURE_2D, prev_bound_tex);
	glActiveTexture(prev_active_tex);
	GL_CHECK_ERROR("GLES1::TextureStorage::render_target_copy_to_back_buffer: glGetIntegerv GL_TEXTURE0 texture state restore");
}

void TextureStorage::render_target_clear_back_buffer(RID p_render_target, const Rect2i &p_region, const Color &p_color) {

}

void TextureStorage::render_target_gen_back_buffer_mipmaps(RID p_render_target, const Rect2i &p_region) {

}

#endif // GLES1_ENABLED
