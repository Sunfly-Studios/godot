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

#ifdef GLES2_ENABLED

#include "texture_storage.h"

#include "drivers/gles2/effects/copy_effects.h"
#include "drivers/gles2/rasterizer_gles2.h"
#include "config.h"
#include "utilities.h"

#ifdef ANDROID_ENABLED
#define glFramebufferTextureMultiviewOVR GLES2::Config::get_singleton()->eglFramebufferTextureMultiviewOVR
#endif

using namespace GLES2;

TextureStorage *TextureStorage::singleton = nullptr;

TextureStorage *TextureStorage::get_singleton() {
	return singleton;
}

static const GLenum _cube_side_enum[6] = {
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
}

/* Canvas Texture API */

RID TextureStorage::canvas_texture_allocate() {
	return canvas_texture_owner.allocate_rid();
}

void TextureStorage::canvas_texture_initialize(RID p_rid) {

}

void TextureStorage::canvas_texture_free(RID p_rid) {

}

void TextureStorage::canvas_texture_set_channel(RID p_canvas_texture, RS::CanvasTextureChannel p_channel, RID p_texture) {

}

void TextureStorage::canvas_texture_set_shading_parameters(RID p_canvas_texture, const Color &p_specular_color, float p_shininess) {

}

void TextureStorage::canvas_texture_set_texture_filter(RID p_canvas_texture, RS::CanvasItemTextureFilter p_filter) {

}

void TextureStorage::canvas_texture_set_texture_repeat(RID p_canvas_texture, RS::CanvasItemTextureRepeat p_repeat) {

}

/* Texture API */

static inline Error _get_gl_uncompressed_format(const Ref<Image> &p_image, Image::Format p_format, Image::Format &r_real_format, GLenum &r_gl_format, GLenum &r_gl_internal_format, GLenum &r_gl_type) {
    return ERR_UNAVAILABLE;
}

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
			r_gl_internal_format = GL_LUMINANCE;
			r_gl_format = GL_LUMINANCE;
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
			ERR_PRINT_ONCE("RG8 Format is not supported by GLES2, converting to RGB.");
			if (image.is_valid()) {
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
			ERR_PRINT_ONCE("RGF Format is not supported by GLES2, converting to RGB8.");
			if (image.is_valid()) {
				image->convert(Image::FORMAT_RGB8);
			}
			r_real_format = Image::FORMAT_RGB8;
			r_gl_internal_format = GL_RGB;
			r_gl_format = GL_RGB;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RGBF: {
			ERR_PRINT_ONCE("RGB float texture not supported by GLES2, converting to RGB8.");
			if (image.is_valid()) {
				image->convert(Image::FORMAT_RGB8);
			}
			r_real_format = Image::FORMAT_RGB8;
			r_gl_internal_format = GL_RGB;
			r_gl_format = GL_RGB;
			r_gl_type = GL_UNSIGNED_BYTE;
		} break;
		case Image::FORMAT_RGBAF: {
			ERR_PRINT_ONCE("RGBA float texture not supported, converting to RGBA8.");
			if (image.is_valid()) {
				image->convert(Image::FORMAT_RGBA8);
			}
			r_real_format = Image::FORMAT_RGBA8;
			r_gl_internal_format = GL_RGBA;
			r_gl_format = GL_RGBA;
			r_gl_type = GL_UNSIGNED_BYTE;
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
			ERR_FAIL_V_MSG(Ref<Image>(), "The image format " + itos(p_format) + " is not supported by the GLES2 rendering backend.");
		}
	}

	if (need_decompress || p_force_decompress) {
		if (!image.is_null()) {
			image = image->duplicate();
			image->decompress();
			ERR_FAIL_COND_V(image->is_compressed(), image);
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

	glGenTextures(1, &texture.tex_id);
	texture.active = false;
	texture.total_data_size = 0;

	return texture_owner.make_rid(texture);
}

void TextureStorage::texture_free(RID p_texture) {

}

void TextureStorage::texture_2d_initialize(RID p_texture, const Ref<Image> &p_image) {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);
	ERR_FAIL_COND(p_image.is_null());

	texture->type = Texture::TYPE_2D;
	texture->target = GL_TEXTURE_2D;
	texture->width = p_image->get_width();
	texture->height = p_image->get_height();
	texture->alloc_width = texture->width;
	texture->alloc_height = texture->height;
	texture->format = p_image->get_format();
	texture->active = true;
	texture->mipmaps = p_image->has_mipmaps() ? p_image->get_mipmap_count() + 1 : 1;
	texture->resize_to_po2 = false;
	Config *config = Config::get_singleton();

	if (!config->support_npot_repeat_mipmap) {
		int po2_width = next_power_of_2(texture->width);
		int po2_height = next_power_of_2(texture->height);

		bool is_po2 = (texture->width == po2_width) && (texture->height == po2_height);

		if (!is_po2 && p_image->has_mipmaps()) {
			texture->alloc_width = po2_width;
			texture->alloc_height = po2_height;
			texture->resize_to_po2 = true;
		}
	}

	_texture_set_data(p_texture, p_image, 0, true);
}

void TextureStorage::texture_external_initialize(RID p_texture, int p_width, int p_height, uint64_t p_external_buffer) {

}

void TextureStorage::texture_2d_layered_initialize(RID p_texture, const Vector<Ref<Image>> &p_layers, RS::TextureLayeredType p_layered_type) {

}

void TextureStorage::texture_3d_initialize(RID p_texture, Image::Format p_format, int p_width, int p_height, int p_depth, bool p_mipmaps, const Vector<Ref<Image>> &p_data) {

}

// Called internally when texture_proxy_create(p_base) is called.
// Note: p_base is the root and p_texture is the proxy.
void TextureStorage::texture_proxy_initialize(RID p_texture, RID p_base) {

}

RID TextureStorage::texture_create_from_native_handle(RS::TextureType p_type, Image::Format p_format, uint64_t p_native_handle, int p_width, int p_height, int p_depth, int p_layers, RS::TextureLayeredType p_layered_type) {
    return RID();
}

void TextureStorage::texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer) {
	texture_set_data(p_texture, p_image, p_layer);

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(tex);
	GLES2::Utilities::get_singleton()->texture_resize_data(tex->tex_id, tex->total_data_size);

#ifdef TOOLS_ENABLED
	tex->image_cache_2d.unref();
#endif

}

void TextureStorage::texture_3d_update(RID p_texture, const Vector<Ref<Image>> &p_data) {

}

void TextureStorage::texture_external_update(RID p_texture, int p_width, int p_height, uint64_t p_external_buffer) {

}

void TextureStorage::texture_proxy_update(RID p_texture, RID p_proxy_to) {

}

void TextureStorage::texture_2d_placeholder_initialize(RID p_texture) {

}

void TextureStorage::texture_2d_layered_placeholder_initialize(RID p_texture, RS::TextureLayeredType p_layered_type) {

}

void TextureStorage::texture_3d_placeholder_initialize(RID p_texture) {

}

Ref<Image> TextureStorage::texture_2d_get(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Ref<Image>());

	// Return the cached image since GLES2 can't read it back natively
	if (tex->image_cache_2d.is_valid()) {
		return tex->image_cache_2d;
	}

	ERR_PRINT_ONCE("CPU Image cache is empty for this texture, and GLES2 cannot read back directly.");
	return Ref<Image>();
}

Ref<Image> TextureStorage::texture_2d_layer_get(RID p_texture, int p_layer) const {
    return Ref<Image>();
}

Vector<Ref<Image>> TextureStorage::_texture_3d_read_framebuffer(GLES2::Texture *p_texture) const {
	return Vector<Ref<Image>>();
}

Vector<Ref<Image>> TextureStorage::texture_3d_get(RID p_texture) const {
    return Vector<Ref<Image>>();
}

void TextureStorage::texture_replace(RID p_texture, RID p_by_texture) {

}

void TextureStorage::texture_set_size_override(RID p_texture, int p_width, int p_height) {

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
	return Size2();
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

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->tex_id);

	// Default to Clamp-to-Edge to prevent possible NPOT black screens.
	// Default to Linear filtering so mipmap checks don't fail an incomplete texture.
	glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	int mipmaps = img->has_mipmaps() ? img->get_mipmap_count() + 1 : 1;
	int w = img->get_width();
	int h = img->get_height();
	int tsize = 0;

	for (int i = 0; i < mipmaps; i++) {
		int64_t size, ofs;
		img->get_mipmap_offset_and_size(i, ofs, size);

		if (compressed) {
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glCompressedTexImage2D(blit_target, i, internal_format, w, h, 0, size, read.ptr() + ofs);
		} else {
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(blit_target, i, internal_format, w, h, 0, format, type, read.ptr() + ofs);
		}

		tsize += size;
		w = MAX(1, w >> 1);
		h = MAX(1, h >> 1);
	}

	texture->total_data_size = tsize;
	texture->stored_cube_sides |= (1 << p_layer);
	texture->mipmaps = mipmaps;
}

void TextureStorage::_texture_set_3d_data(RID p_texture, const Vector<Ref<Image>> &p_data, bool p_initialize) {

}

void TextureStorage::_texture_set_swizzle(GLES2::Texture *p_texture, Image::Format p_real_format) {

}

Image::Format TextureStorage::texture_get_format(RID p_texture) const {
	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Image::FORMAT_L8);
	return tex->format;
}

uint32_t TextureStorage::texture_get_texid(RID p_texture) const {
    return 0;
}

Vector3i TextureStorage::texture_get_size(RID p_texture) const {
	return Vector3i(0, 0, 0);
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
	return 0;
}

void TextureStorage::texture_bind(RID p_texture, uint32_t p_texture_no) {
}

/* TEXTURE ATLAS API */

void TextureStorage::texture_add_to_texture_atlas(RID p_texture) {

}

void TextureStorage::texture_remove_from_texture_atlas(RID p_texture) {

}

void TextureStorage::texture_atlas_mark_dirty_on_texture(RID p_texture) {

}

void TextureStorage::texture_atlas_remove_texture(RID p_texture) {

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

	// If drawing directly to the screen, we just use
	// the default system Framebuffer (0)
	if (rt->direct_to_screen) {
		rt->fbo = system_fbo;
		return;
	}

	// Set up the color formats for the FBO
	if (rt->is_transparent) {
		rt->color_internal_format = GL_RGBA;
		rt->color_format = GL_RGBA;
		rt->color_type = GL_UNSIGNED_BYTE;
		rt->color_format_size = 4;
		rt->image_format = Image::FORMAT_RGBA8;
	} else {
		rt->color_internal_format = GL_RGB;
		rt->color_format = GL_RGB;
		rt->color_type = GL_UNSIGNED_BYTE;
		rt->color_format_size = 3;
		rt->image_format = Image::FORMAT_RGB8;
	}

	glDisable(GL_SCISSOR_TEST);
	glColorMask(1, 1, 1, 1);
	glDepthMask(GL_FALSE);

	Texture *texture;
	GLenum texture_target = GL_TEXTURE_2D;

	// FBO Generation
	glGenFramebuffers(1, &rt->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);

	if (rt->overridden.color.is_valid()) {
		texture = get_texture(rt->overridden.color);
		ERR_FAIL_NULL(texture);
		rt->color = texture->tex_id;
		rt->size = Size2i(texture->width, texture->height);
	} else {
		texture = get_texture(rt->texture);
		ERR_FAIL_NULL(texture);

		glGenTextures(1, &rt->color);
		glBindTexture(texture_target, rt->color);

		glTexImage2D(texture_target, 0, rt->color_internal_format, rt->size.x, rt->size.y, 0, rt->color_format, rt->color_type, nullptr);

		// Force the texture parameters immediately after creating the color texture,
		// otherwise shenanigans will happen (a.k.a, a very useful black screen).
		glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		texture->gl_set_filter(RS::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
		texture->gl_set_repeat(RS::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	}

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_target, rt->color, 0);

	if (rt->overridden.depth.is_valid()) {
		texture = get_texture(rt->overridden.depth);
		ERR_FAIL_NULL(texture);
		rt->depth = texture->tex_id;
	} else {
		glGenTextures(1, &rt->depth);
		glBindTexture(texture_target, rt->depth);

		glTexImage2D(texture_target, 0, GL_DEPTH_COMPONENT, rt->size.x, rt->size.y, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

		glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture_target, rt->depth, 0);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		glDeleteFramebuffers(1, &rt->fbo);
		rt->fbo = 0;
		WARN_PRINT("Could not create render target FBO, status: " + get_framebuffer_error(status));
		return;
	}

	// Update the backing texture so the engine
	// knows about the FBO's memory
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

	// Clear out FBO rubbish/garbage
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, system_fbo);
}

void TextureStorage::_create_render_target_backbuffer(RenderTarget *rt) {

}

void GLES2::TextureStorage::check_backbuffer(RenderTarget *rt, const bool uses_screen_texture, const bool uses_depth_texture) {
}

void TextureStorage::_clear_render_target(RenderTarget *rt) {

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

}

void TextureStorage::render_target_set_position(RID p_render_target, int p_x, int p_y) {

}

Point2i TextureStorage::render_target_get_position(RID p_render_target) const {
	return Point2i();
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

}

RID TextureStorage::render_target_get_override_color(RID p_render_target) const {
    return RID();
}

RID TextureStorage::render_target_get_override_depth(RID p_render_target) const {
    return RID();
}

RID TextureStorage::render_target_get_override_velocity(RID p_render_target) const {
    return RID();
}

void TextureStorage::render_target_set_render_region(RID p_render_target, const Rect2i &p_render_region) {
}

Rect2i TextureStorage::render_target_get_render_region(RID p_render_target) const {
    return Rect2i();
}

RID TextureStorage::render_target_get_texture(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());
	return rt->texture;
}

void TextureStorage::render_target_set_transparent(RID p_render_target, bool p_transparent) {

}

bool TextureStorage::render_target_get_transparent(RID p_render_target) const {
    return false;
}

void TextureStorage::render_target_set_direct_to_screen(RID p_render_target, bool p_direct_to_screen) {

}

bool TextureStorage::render_target_get_direct_to_screen(RID p_render_target) const {
    return false;
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
    return GL_RGBA8;
}

GLuint TextureStorage::render_target_get_color_format(RID p_render_target) const {
    return GL_RGBA;
}

GLuint TextureStorage::render_target_get_color_type(RID p_render_target) const {
    return GL_UNSIGNED_BYTE;
}

uint32_t TextureStorage::render_target_get_color_format_size(RID p_render_target) const {
    return 4;
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
	glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);

	// Paint the color.
	glClearColor(rt->clear_color.r, rt->clear_color.g, rt->clear_color.b, rt->clear_color.a);
	glClear(GL_COLOR_BUFFER_BIT);

	rt->clear_requested = false;

	// Reset back to the main window
	glBindFramebuffer(GL_FRAMEBUFFER, system_fbo);
}

GLuint TextureStorage::render_target_get_fbo(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, 0);
	return rt->fbo;
}

GLuint TextureStorage::render_target_get_color(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, 0);
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

}

void TextureStorage::render_target_clear_back_buffer(RID p_render_target, const Rect2i &p_region, const Color &p_color) {

}

void TextureStorage::render_target_gen_back_buffer_mipmaps(RID p_render_target, const Rect2i &p_region) {

}

#endif // GLES2_ENABLED
