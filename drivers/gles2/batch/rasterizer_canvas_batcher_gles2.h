/**************************************************************************/
/*  rasterizer_canvas_batcher_gles2.h                                     */
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

#ifndef RASTERIZER_CANVAS_BATCHER_GLES2_H
#define RASTERIZER_CANVAS_BATCHER_GLES2_H

#ifdef GLES2_ENABLED

#include "drivers/gles_common/batch/rasterizer_canvas_batcher_common.h"
#include "drivers/gles2/storage/material_storage.h"
#include "drivers/gles2/storage/texture_storage.h"

class RasterizerCanvasGLES2;

struct BatcherAPIGLES2 {
	using Canvas = RasterizerCanvasGLES2;
	using MaterialData = GLES2::CanvasMaterialData;
	using TextureStorage = GLES2::TextureStorage;
	using Texture = GLES2::Texture;
	using CanvasTexture = GLES2::CanvasTexture;
	using Shader = GLES2::Shader;
};

class RasterizerCanvasBatcherGLES2 : public RasterizerCanvasBatcherCommon<BatcherAPIGLES2> {
	// All logic is inherited in `rasterizer_canvas_batcher_common.inc`
};

#endif // GLES2_ENABLED
#endif // RASTERIZER_CANVAS_BATCHER_GLES2_H