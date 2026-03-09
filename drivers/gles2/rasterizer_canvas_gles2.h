/*************************************************************************/
/*  rasterizer_canvas_gles2.h                                            */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
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

#ifdef GLES2_ENABLED

#include "drivers/gles2/base/rasterizer_canvas_base_gles2.h"
#include "drivers/gles2/batch/rasterizer_canvas_batcher.h"
#include "servers/rendering/renderer_canvas_render.h"

namespace GLES2 {

class RasterizerStorageGLES2; // Forward declare for the batcher template

// Properly inherit from the Base canvas and the Batcher template!
class RasterizerCanvasGLES2 : public RasterizerCanvasBaseGLES2, public RasterizerCanvasBatcher<RasterizerCanvasGLES2, RasterizerStorageGLES2> {
	friend class RasterizerCanvasBatcher<RasterizerCanvasGLES2, RasterizerStorageGLES2>;

public:

	LocalVector<uint32_t> indices_ptr;

	void _batch_upload_buffers();
	void _batch_render_lines(const Batch &p_batch, Material *p_material, bool p_anti_alias);
	void _batch_render_generic(const Batch &p_batch, Material *p_material);
	void render_batches(Item::Command *const *p_commands, Item *p_current_clip, bool &r_reclip, Material *p_material);

	virtual void canvas_begin() override;
	virtual void canvas_end() override;

	void canvas_render_items_begin(const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform);
	void canvas_render_items_end();
	void canvas_render_items_internal(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform);
	void canvas_render_items_implementation(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light, const Transform2D &p_base_transform);

	void _legacy_canvas_render_item(Item *p_ci, RenderItemState &r_ris);
	void gl_enable_scissor(int p_x, int p_y, int p_width, int p_height) const;
	void gl_disable_scissor() const;

	void initialize();
	RasterizerCanvasGLES2();

	virtual void set_debug_redraw(bool p_enabled, double p_time, const Color &p_color) override {}
	virtual uint32_t get_pipeline_compilations(RS::PipelineSource p_source) override { return 0; }
};

} // namespace GLES2
#endif // GLES2_ENABLED
