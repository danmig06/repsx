#include <psx/gpu.h>
#include <psx/irq.h>

#include "gpucmds.h"
#include "util.h"
#include "log.h"

#include <stdbool.h>
#include <string.h>
#define GPU_VER 1

typedef struct gp0_color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t x;
} color_t;

typedef struct gp0_vert {
	int16_t x: 11;
	uint16_t ov_x: 5;
	int16_t y: 11;
	uint16_t ov_y: 5;
} vert_t;

typedef struct gp0_tex_uv {
	uint8_t u;
	uint8_t v;
	uint16_t info;
} tex_uv_t;

typedef struct gp0_pos {
	uint16_t x;
	uint16_t y;
} pos_t;

typedef struct gp0_clut_t {
	uint16_t x: 6;
	uint16_t y: 9;
	uint16_t unused: 1;
} clut_t;

typedef struct gp0_texpage_t {
	uint16_t x: 4;
	uint16_t y: 1;
	uint16_t semi_transparency: 2;
	enum psx_gpu_texdepth depth: 2;
	uint16_t unused: 7;
} texpage_t;

pos_t calc_page_pos(struct psx_gpu* gpu, texpage_t* page) {
	pos_t res;
	if(page) {
		res.x = page->x * 64;
		res.y = page->y * 256;
	} else {
		res.x = gpu->gpustat.tx_base * 64;
		res.y = gpu->gpustat.ty_base1 * 256;
	}
	return res;
}

void gp0_nop(struct psx_gpu* gpu) {
	return;
}

void gp0_cache_clear(struct psx_gpu* gpu) {
	return;
}

void gp0_poly(struct psx_gpu* gpu) {
	struct {
		uint32_t color0: 24;
		bool is_raw: 1;
		bool is_transparent: 1;
		bool is_textured: 1;
		bool is_quad: 1;
		bool is_gouraud_shaded: 1;
		uint32_t num: 3;
	} poly;
	AS_UINT32(poly) = gpu->cmd.buf[GPU_CMD_IDX];

	log_trace("GP0 %s %s %s%s %s start", 
			(poly.is_raw) ? "solid" : "blended",
			(poly.is_transparent) ? "transparent" : "opaque",
			(poly.is_textured) ? "textured " : "",
			(poly.is_gouraud_shaded) ? "gouraud" : "flat",
			(poly.is_quad) ? "quad" : "triangle");
	unsigned arg_idx = 1;
	color_t c[4] = { 0 };
	vert_t v[4] = { 0 };
	// this array gets optimized out and all its data gets lost for some reason
	volatile tex_uv_t tex_data[4] = { 0 };
	clut_t clut = { 0 };
	texpage_t texpage = { 0 };

	AS_UINT32(c[0]) = poly.color0;
	log_trace("color r=%u, g=%u, b=%u", c[0].r, c[0].g, c[0].b);
	AS_UINT32(v[0]) = gpu->cmd.buf[arg_idx++];
	v[0].x += gpu->draw_off.x;
	v[0].y += gpu->draw_off.y;
	log_trace("vertex x=%hd, y=%hd", v[0].x, v[0].y);
	if(poly.is_textured) {
		AS_UINT32(tex_data[0]) = gpu->cmd.buf[arg_idx++];
		AS_UINT16(clut) = tex_data[0].info;
		log_trace("texdata u=%u, v=%u, clut:(x=%u, y=%u)", tex_data[0].u, tex_data[0].v, clut.x, clut.y);
	}

	int min_x = v[0].x, max_x = v[0].x;
	int min_y = v[0].y, max_y = v[0].y;
	bool need_texpage = true;
	for(int i = 0; i < ((poly.is_quad) ? 3 : 2); i++) {
		if(poly.is_gouraud_shaded) {
			AS_UINT32(c[i + 1]) = gpu->cmd.buf[arg_idx++];
			log_trace("color r=%u, g=%u, b=%u", c[i + 1].r, c[i + 1].g, c[i + 1].b);
		} else {
			c[i + 1] = c[0];
		}
		AS_UINT32(v[i + 1]) = gpu->cmd.buf[arg_idx++];
		v[i + 1].x += gpu->draw_off.x;
		if(v[i + 1].x > max_x) max_x = v[i + 1].x;
		if(v[i + 1].x < min_x) min_x = v[i + 1].x;
		v[i + 1].y += gpu->draw_off.y;
		if(v[i + 1].y > max_y) max_y = v[i + 1].y;
		if(v[i + 1].y < min_y) min_y = v[i + 1].y;
		log_trace("vertex x=%hd, y=%hd", v[i + 1].x, v[i + 1].y);
		if(poly.is_textured) {
			AS_UINT32(tex_data[i + 1]) = gpu->cmd.buf[arg_idx++];
			if(need_texpage) {
				AS_UINT16(texpage) = tex_data[i + 1].info;
				need_texpage = false;
				log_trace("texdata u=%u, v=%u, texpage:(x=%u, y=%u)", tex_data[i + 1].u, tex_data[i + 1].v, texpage.x * 64, texpage.y * 256);
			} else {
				log_trace("texdata u=%u, v=%u, info=0x%04x", tex_data[i + 1].u, tex_data[i + 1].v, tex_data[i + 1].info);
			}
		}
	}
	if((max_x - min_x) > 0x3ff || (max_y - min_y) > 0x1ff) {
		return;
	}

	int shading_mode = PSX_RENDERER_SH_FLAT;
	pos_t texpage_pos = { 0 };
	if(poly.is_textured) {
		shading_mode = PSX_RENDERER_SH_TEXTURE;
		gpu->gpustat.tx_base = texpage.x;
		gpu->gpustat.ty_base1 = texpage.y;
		gpu->gpustat.tex_page_colors = texpage.depth;
		gpu->gpustat.semi_transparency = texpage.semi_transparency;
		texpage_pos = calc_page_pos(gpu, &texpage);
	} else if(poly.is_gouraud_shaded) {
		shading_mode = PSX_RENDERER_SH_GOURAUD;
	}

	if(!gpu->renderer.poly) {
		return;
	}
	psx_gpu_vert_t rv[4] = {
		{ .x = v[0].x, .y = v[0].y, .color = AS_UINT32(c[0]), .tx = tex_data[0].u, .ty = tex_data[0].v },
		{ .x = v[1].x, .y = v[1].y, .color = AS_UINT32(c[1]), .tx = tex_data[1].u, .ty = tex_data[1].v },
		{ .x = v[2].x, .y = v[2].y, .color = AS_UINT32(c[2]), .tx = tex_data[2].u, .ty = tex_data[2].v },
		{ .x = v[3].x, .y = v[3].y, .color = AS_UINT32(c[3]), .tx = tex_data[3].u, .ty = tex_data[3].v }
	}; 
	gpu_render_poly(gpu, shading_mode, poly.is_quad,
			.poly.v0 = rv[0], .poly.v1 = rv[1], .poly.v2 = rv[2], .poly.v3 = rv[3],
			.tex.clut_x = clut.x * 16, .tex.clut_y = clut.y, .tex.page_x = texpage_pos.x, .tex.page_y = texpage_pos.y,
			.tex.off_x = gpu->tex_window.off_x, .tex.off_y = gpu->tex_window.off_y, .tex.mask_x = gpu->tex_window.mask_x, .tex.mask_y = gpu->tex_window.mask_y,
			.tex.depth = texpage.depth, .tex.need_modulation = !poly.is_raw, .tex.need_dithering = (shading_mode == PSX_RENDERER_SH_GOURAUD) || !poly.is_raw);
}

static void gp0_polyline_update(struct psx_gpu* gpu) {
	struct {
		uint32_t color0: 24;
		uint32_t unused0: 1;
		bool is_transparent: 1;
		uint32_t unused1: 1;
		bool polyline: 1;
		bool is_gouraud_shaded: 1;
		uint32_t num: 3;
	} line;
	AS_UINT32(line) = gpu->cmd.buf[GPU_CMD_IDX];

	color_t c[2];
	vert_t v[2];
	uint32_t* state = &gpu->cmd.buf[GPU_CMD_POLYLINE_STATE];
	uint32_t word = gpu->cmd.buf[GPU_CMD_DATA_IDX];
	switch(*state) {
	case GPU_CMD_POLYLINE_C0:
		if((word & 0xf000f000) == 0x50005000) {
			log_trace("gouraud shaded polyline end");
			gpu->cmd.receiving_data = false;
			return;
		}
		gpu->cmd.buf[GPU_CMD_POLYLINE_C0] = word;
		*state = GPU_CMD_POLYLINE_V0;
		break;
	case GPU_CMD_POLYLINE_V0:
		if(!line.is_gouraud_shaded && (word & 0xf000f000) == 0x50005000) {
			log_trace("flat polyline end");
			gpu->cmd.receiving_data = false;
			return;
		}
		gpu->cmd.buf[GPU_CMD_POLYLINE_V0] = word;
		if(line.is_gouraud_shaded) {
			*state = GPU_CMD_POLYLINE_C1;
		} else {
			*state = GPU_CMD_POLYLINE_V1;
		}
		break;
	case GPU_CMD_POLYLINE_C1:
		gpu->cmd.buf[GPU_CMD_POLYLINE_C1] = word;
		*state = GPU_CMD_POLYLINE_V1;
		break;
	case GPU_CMD_POLYLINE_V1:
		if(line.is_gouraud_shaded) {
			AS_UINT32(c[0]) = gpu->cmd.buf[GPU_CMD_POLYLINE_C0];
			log_trace("color r=%u, g=%u, b=%u", c[0].r, c[0].g, c[0].b);
			AS_UINT32(c[1]) = gpu->cmd.buf[GPU_CMD_POLYLINE_C1];
			log_trace("color r=%u, g=%u, b=%u", c[1].r, c[1].g, c[1].b);
			*state = GPU_CMD_POLYLINE_C0;
		} else {
			AS_UINT32(c[0]) = AS_UINT32(c[1]) = line.color0;
			*state = GPU_CMD_POLYLINE_V0;
		}
		AS_UINT32(v[0]) = gpu->cmd.buf[GPU_CMD_POLYLINE_V0];
		log_trace("vertex x=%hd, y=%hd", v[0].x, v[0].y);
		AS_UINT32(v[1]) = word;
		log_trace("vertex x=%hd, y=%hd", v[1].x, v[1].y);
		v[0].x += gpu->draw_off.x;
		v[0].y += gpu->draw_off.y;
		v[1].x += gpu->draw_off.x;
		v[1].y += gpu->draw_off.y;

		if(!gpu->renderer.line) {
			return;
		}
		gpu_render_line(gpu, (line.is_gouraud_shaded) ? PSX_RENDERER_SH_GOURAUD : PSX_RENDERER_SH_FLAT, 
				.line.v0 = { .x = v[0].x, .y = v[0].y, .color = AS_UINT32(c[0]) },
				.line.v1 = { .x = v[1].x, .y = v[1].y, .color = AS_UINT32(c[1]) });
		break;
	default:
		return;
	}

	// line data is not done yet, since we didn't get a terminator, returning early causes the vertex sequence to end
	gpu->cmd.words_left = true;
}

void gp0_line(struct psx_gpu* gpu) {
	struct {
		uint32_t color0: 24;
		uint32_t unused0: 1;
		bool is_transparent: 1;
		uint32_t unused1: 1;
		bool polyline: 1;
		bool is_gouraud_shaded: 1;
		uint32_t num: 3;
	} line;
	AS_UINT32(line) = gpu->cmd.buf[GPU_CMD_IDX];

	log_trace("GP0 %s%s%sline start", 
			(line.is_gouraud_shaded) ? "gouraud " : "flat ",
			(line.is_transparent) ? "transparent " : "opaque ",
			(line.polyline) ? "poly" : "");
	unsigned arg_idx = 1;
	color_t c[2];
	vert_t v[2];

	AS_UINT32(c[0]) = line.color0;
	log_trace("color r=%u, g=%u, b=%u", c[0].r, c[0].g, c[0].b);
	AS_UINT32(v[0]) = gpu->cmd.buf[arg_idx++];
	v[0].x += gpu->draw_off.x;
	v[0].y += gpu->draw_off.y;
	log_trace("vertex x=%hd, y=%hd", v[0].x, v[0].y);
	if(line.is_gouraud_shaded) {
		AS_UINT32(c[1]) = gpu->cmd.buf[arg_idx++];
		log_trace("color r=%u, g=%u, b=%u", c[1].r, c[1].g, c[1].b);
	} else {
		c[1] = c[0];
	}
	AS_UINT32(v[1]) = gpu->cmd.buf[arg_idx++];
	v[1].x += gpu->draw_off.x;
	v[1].y += gpu->draw_off.y;
	log_trace("vertex x=%hd, y=%hd", v[1].x, v[1].y);

	if(line.polyline) {
		gpu->cmd.receiving_data = true;
		gpu->cmd.words_left = true;
		gpu->cmd.update = gp0_polyline_update;
		gpu->cmd.buf[GPU_CMD_POLYLINE_STATE] = (line.is_gouraud_shaded) ? GPU_CMD_POLYLINE_C0 : GPU_CMD_POLYLINE_V0;
	}
	if(!gpu->renderer.line) {
		return;
	}
	gpu_render_line(gpu, (line.is_gouraud_shaded) ? PSX_RENDERER_SH_GOURAUD : PSX_RENDERER_SH_FLAT, 
			.line.v0 = { .x = v[0].x, .y = v[0].y, .color = AS_UINT32(c[0]) },
			.line.v1 = { .x = v[1].x, .y = v[1].y, .color = AS_UINT32(c[1]) });
}

void gp0_rect(struct psx_gpu* gpu) {
	struct {
		uint32_t color0: 24;
		bool is_raw: 1; 
		bool is_transparent: 1; 
		bool is_textured: 1; 
		uint32_t geometry: 2;
		uint32_t num: 3;
	} rect;
	AS_UINT32(rect) = gpu->cmd.buf[GPU_CMD_IDX];

	log_trace("GP0 %s%s%srectangle start", 
			(rect.is_raw) ? "solid " : "blended ",
			(rect.is_transparent) ? "transparent " : "opaque ",
			(rect.is_textured) ? "textured " : " ");
	unsigned arg_idx = 1;
	color_t c;
	vert_t tl;
	vert_t size;
	// this gets optimized out and all its data gets lost for some reason
	volatile tex_uv_t tex_data;
	clut_t clut;

	AS_UINT32(c) = rect.color0;
	log_trace("color r=%u, g=%u, b=%u", c.r, c.g, c.b);
	AS_UINT32(tl) = gpu->cmd.buf[arg_idx++];
	tl.x += gpu->draw_off.x;
	tl.y += gpu->draw_off.y;
	log_trace("vertex x=%hd, y=%hd", tl.x, tl.y);
	if(rect.is_textured) {
		AS_UINT32(tex_data) = gpu->cmd.buf[arg_idx++];
		AS_UINT16(clut) = tex_data.info;
		log_trace("texdata u=%u, v=%u, clut:(x=%u, y=%u)", tex_data.u, tex_data.v, clut.x, clut.y);
	}
	switch(rect.geometry) {
	case 1:
		size.x = 1, size.y = 1;
		break;
	case 2:
		size.x = 8, size.y = 8;
		break;
	case 3:
		size.x = 16, size.y = 16;
		break;
	case 0:
	default:
		AS_UINT32(size) = gpu->cmd.buf[arg_idx++];
		break;
	}
	log_trace("size w=%hd, h=%hd", size.x, size.y);
	pos_t texpage_pos = calc_page_pos(gpu, NULL);
	
	if(!gpu->renderer.rect) {
		return;
	}
	gpu_render_rect(gpu, rect.is_textured,
			.rect.v = { .x = tl.x, .y = tl.y, .color = AS_UINT32(c), .tx = tex_data.u, .ty = tex_data.v },
			.rect.w = size.x, .rect.h = size.y, .tex.clut_x = clut.x * 16, .tex.clut_y = clut.y, .tex.need_dithering = false,
			.tex.page_x = texpage_pos.x, .tex.page_y = texpage_pos.y, .tex.depth = gpu->gpustat.tex_page_colors, .tex.need_modulation = !rect.is_raw,
			.tex.off_x = gpu->tex_window.off_x, .tex.off_y = gpu->tex_window.off_y, .tex.mask_x = gpu->tex_window.mask_x, .tex.mask_y = gpu->tex_window.mask_y);
}

void gp0_fillvram(struct psx_gpu* gpu) {
	struct {
		uint32_t color: 24;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	uint32_t arg_idx = 1;
	vert_t tl, size;
	AS_UINT32(tl) = gpu->cmd.buf[arg_idx++];
	tl.x &= 0x3f0;
	tl.y &= 0x1ff;
	AS_UINT32(size) = gpu->cmd.buf[arg_idx++];
	size.x = ((size.x & 0x3ff) + 0xf) & 0xfff0;
	size.y &= 0x1ff;
	log_trace("GP0 FillVram: x=%u, y=%u, w=%u, h=%u", tl.x, tl.y, size.x, size.y);
	gpu_render_rect(gpu, false,
		       .rect.v = { .x = tl.x, .y = tl.y, .color = command.color },
		       .rect.w = size.x, .rect.h = size.y, .rect.is_clear = true);
}

static void gp0_image_load_update(struct psx_gpu* gpu) {
	if(gpu->cmd.words_left == 0 && ((gpu->blit_state.w * gpu->blit_state.h) & 1)) {
		gpu->cmd.buf[GPU_CMD_DATA_IDX] &= 0xffff;
	}

	struct {
		psx_gpu_color_t colors[2];
	} data;
	AS_UINT32(data) = gpu->cmd.buf[GPU_CMD_DATA_IDX];

	uint32_t max_x = gpu->blit_state.start_x + gpu->blit_state.w;
	uint32_t x = gpu->blit_state.x - gpu->blit_state.start_x;
	uint32_t y = gpu->blit_state.y - gpu->blit_state.start_y;
	// gpu->renderer.vram_write(&gpu->renderer, gpu->blit_state.x, gpu->blit_state.y, data.colors[0]);
	gpu->blit_state.texels[x + (y * gpu->blit_state.w)] = AS_UINT16(data.colors[0]);
	gpu->blit_state.x++;
	if(gpu->blit_state.x == max_x) {
		gpu->blit_state.x = gpu->blit_state.start_x;
		gpu->blit_state.y++;
	}
	// gpu->renderer.vram_write(&gpu->renderer, gpu->blit_state.x, gpu->blit_state.y, data.colors[1]);
	x = gpu->blit_state.x - gpu->blit_state.start_x;
	y = gpu->blit_state.y - gpu->blit_state.start_y;
	gpu->blit_state.texels[x + (y * gpu->blit_state.w)] = AS_UINT16(data.colors[1]);
	gpu->blit_state.x++;
	if(gpu->blit_state.x == max_x) {
		gpu->blit_state.x = gpu->blit_state.start_x;
		gpu->blit_state.y++;
	}

	if(gpu->cmd.words_left == 0) {
		gpu->renderer.commit_vram(&gpu->renderer, gpu->blit_state.texels);
		gpu->renderer.dispose_vram(&gpu->renderer, gpu->blit_state.texels);
		gpu->blit_state.texels = NULL;
	}
}

void gp0_image_load(struct psx_gpu* gpu) {
	struct {
		uint32_t unused: 29;
		uint32_t num: 3;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	gpu->blit_state.x = gpu->cmd.buf[1] & 0xffff;
	gpu->blit_state.y = gpu->cmd.buf[1] >> 16;
	gpu->blit_state.w = gpu->cmd.buf[2] & 0xffff;
	gpu->blit_state.h = gpu->cmd.buf[2] >> 16;
	gpu->blit_state.is_read = false;

	gpu->blit_state.start_x = gpu->blit_state.x;
	gpu->blit_state.start_y = gpu->blit_state.y;
	gpu->blit_state.texels = gpu->renderer.get_vram(&gpu->renderer, gpu->blit_state.x, gpu->blit_state.y, gpu->blit_state.w, gpu->blit_state.h);

	gpu->cmd.receiving_data = true;
	gpu->cmd.words_left = (gpu->blit_state.w * gpu->blit_state.h) + 1;
	gpu->cmd.words_left >>= 1;
	gpu->cmd.update = gp0_image_load_update;
	log_trace("GP0 image load at x=%hu y=%hu w=%hu h=%hu (%u words)", 
			gpu->blit_state.x, gpu->blit_state.y, gpu->blit_state.w, gpu->blit_state.h, gpu->cmd.words_left);
}

void gp0_image_store(struct psx_gpu* gpu) {
	struct {
		uint32_t unused: 29;
		uint32_t num: 3;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	gpu->blit_state.x = gpu->cmd.buf[1] & 0xffff;
	gpu->blit_state.y = gpu->cmd.buf[1] >> 16;
	gpu->blit_state.w = gpu->cmd.buf[2] & 0xffff;
	gpu->blit_state.h = gpu->cmd.buf[2] >> 16;
	gpu->blit_state.start_x = gpu->blit_state.x;
	gpu->blit_state.start_y = gpu->blit_state.y;
	gpu->blit_state.is_read = true;
	gpu->blit_state.texels = gpu->renderer.get_vram(&gpu->renderer, gpu->blit_state.x, gpu->blit_state.y, gpu->blit_state.w, gpu->blit_state.h);

	gpu->gpustat.vram_ready = true;

	log_trace("GP0 image store at x=%hu y=%hu w=%hu h=%hu (%u words)", 
			gpu->blit_state.x, gpu->blit_state.y, gpu->blit_state.w, gpu->blit_state.h, ((gpu->blit_state.w * gpu->blit_state.h) + 1) >> 1);
}

void gp0_blit(struct psx_gpu* gpu) {
	struct {
		uint32_t unused: 29;
		uint32_t num: 3;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	vert_t src, dst;
	int w, h;
	AS_UINT32(src) = gpu->cmd.buf[1];
	AS_UINT32(dst) = gpu->cmd.buf[2];
	w = gpu->cmd.buf[3] & 0xffff;
	h = gpu->cmd.buf[3] >> 16;

	log_trace("GP0 blit at sx=%hu sy=%hu -> dx=%hu, dy=%hu w=%d h=%d", 
			src.x, src.y, dst.x, dst.y, w, h);

	gpu->renderer.blit(&gpu->renderer, src.x, src.y, dst.x, dst.y, w, h);
}

void gp0_draw_mode(struct psx_gpu* gpu) {
	struct {
		uint32_t tex_x_base: 4;
		uint32_t tex_y_base1: 1;
		uint32_t semi_transparency: 2;
		enum psx_gpu_texdepth tex_page_colors: 2;
		uint32_t dither_mode: 1;
		uint32_t display_draw: 1;
		uint32_t tex_y_base2: 1;
		uint32_t rect_x_flip: 1;
		uint32_t rect_y_flip: 1;
		uint32_t unused: 10;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	gpu->gpustat.tx_base = command.tex_x_base;
	gpu->gpustat.ty_base1 = command.tex_y_base1;
	gpu->gpustat.semi_transparency = command.semi_transparency;
	gpu->gpustat.tex_page_colors = command.tex_page_colors;
	gpu->gpustat.dither_mode = command.dither_mode;
	gpu->gpustat.display_draw = command.display_draw;
	gpu->rect_x_flip = command.rect_x_flip;
	gpu->rect_y_flip = command.rect_y_flip;
}

void gp0_set_texture_window(struct psx_gpu* gpu) {
	struct {
		uint32_t mask_x: 5;
		uint32_t mask_y: 5;
		uint32_t off_x: 5;
		uint32_t off_y: 5;
		uint32_t unused: 4;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	gpu->tex_window.mask_x = command.mask_x;
	gpu->tex_window.mask_y = command.mask_y;
	gpu->tex_window.off_x = command.off_x;
	gpu->tex_window.off_y = command.off_y;
}

void gp0_draw_area_tl(struct psx_gpu* gpu) {
	struct {
		uint32_t draw_area_left: 10;
		uint32_t draw_area_top: 10;
		uint32_t unused: 4;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	gpu->draw_area.x1 = command.draw_area_left;
	gpu->draw_area.y1 = command.draw_area_top;
	gpu->renderer.clip_update(&gpu->renderer, gpu->draw_area.x1, gpu->draw_area.y1, gpu->draw_area.x2, gpu->draw_area.y2);
}

void gp0_draw_area_br(struct psx_gpu* gpu) {
	struct {
		uint32_t draw_area_right: 10;
		uint32_t draw_area_bottom: 10;
		uint32_t unused: 4;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];

	gpu->draw_area.x2 = command.draw_area_right;
	gpu->draw_area.y2 = command.draw_area_bottom;
	gpu->renderer.clip_update(&gpu->renderer, gpu->draw_area.x1, gpu->draw_area.y1, gpu->draw_area.x2, gpu->draw_area.y2);
}

void gp0_set_draw_offset(struct psx_gpu* gpu) {
	/*
	// now this bugs out for some reason?
	struct {
		int16_t off_x: 11;
		int16_t off_y: 11;
		uint8_t unused: 2;
		uint8_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];
	*/

	gpu->draw_off.x = (int16_t)(gpu->cmd.buf[GPU_CMD_IDX] & 0x7ff);
	gpu->draw_off.y = (int16_t)((gpu->cmd.buf[GPU_CMD_IDX] >> 11) & 0x7ff);
	log_trace("draw offset set to x=%hd, y=%hd", gpu->draw_off.x, gpu->draw_off.y);
}

void gp0_set_mask_bit(struct psx_gpu* gpu) {
	struct {
		bool set_mask: 1;
		bool use_mask: 1;
		uint32_t unused: 22;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = gpu->cmd.buf[GPU_CMD_IDX];
	
	gpu->gpustat.set_mask = command.set_mask;
	gpu->gpustat.use_mask = command.use_mask;
}

void gp1_reset(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		uint32_t unused: 24;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	psx_gpu_reset(gpu);
}

void gp1_buffer_clear(struct psx_gpu* gpu, uint32_t cmd) {
	memset(&gpu->cmd, 0, sizeof(gpu->cmd));
}

void gp1_irq_ack(struct psx_gpu* gpu, uint32_t cmd) {
	gpu->gpustat.irq = 0;
}

void gp1_display_mode(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		uint32_t hres1: 2;
		uint32_t vres: 1;
		uint32_t video_mode: 1;
		uint32_t display_color_depth: 1;
		uint32_t vinterlace: 1;
		uint32_t hres2: 1;
		uint32_t screen_hflip: 1;
		uint32_t unused: 16;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	gpu->gpustat.hres1 = command.hres1;
	gpu->gpustat.hres2 = command.hres2;
	gpu->gpustat.vres = command.vres;
	gpu->gpustat.video_mode = command.video_mode;
	gpu->gpustat.screen_hflip = command.screen_hflip;
	gpu->gpustat.vinterlace = command.vinterlace;
}

void gp1_dma_direction(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		enum psx_gpu_dmadir dma_direction: 2;
		uint32_t unused: 22;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	gpu->gpustat.dma_direction = command.dma_direction;
}

void gp1_set_display_area(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		uint32_t display_x: 10;
		uint32_t display_y: 9;
		uint32_t unused: 5;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	gpu->display_area.x = command.display_x;
	gpu->display_area.y = command.display_y;
}

void gp1_set_display_hrange(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		uint32_t display_x1: 12;
		uint32_t display_x2: 12;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	gpu->display_area.x1 = command.display_x1;
	gpu->display_area.x2 = command.display_x2;
}

void gp1_set_display_vrange(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		uint32_t display_y1: 10;
		uint32_t display_y2: 10;
		uint32_t unused: 4;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	gpu->display_area.y1 = command.display_y1;
	gpu->display_area.y2 = command.display_y2;
}

void gp1_set_display_disable(struct psx_gpu* gpu, uint32_t cmd) {
	struct {
		bool display_disable: 1;
		uint32_t unused: 23;
		uint32_t num: 8;
	} command;
	AS_UINT32(command) = cmd;

	gpu->gpustat.display_disable = command.display_disable;
}

void gp1_read_register(struct psx_gpu* gpu, uint32_t cmd) {
	uint32_t index = cmd & 7;

	switch(index) {
	case 2:
		gpu->gpuread = gpu->tex_window.mask_x | (gpu->tex_window.mask_y << 5);
		gpu->gpuread |= (gpu->tex_window.off_x << 10) | (gpu->tex_window.off_y << 15);
		break;
	case 3:
		gpu->gpuread = gpu->draw_area.x1 | (gpu->draw_area.y1 << 10);
		break;
	case 4:
		gpu->gpuread = gpu->draw_area.x2 | (gpu->draw_area.y2 << 10);
		break;
	case 5:
		gpu->gpuread = gpu->draw_off.x | (gpu->draw_off.y << 11);
		break;
	case 7:
		gpu->gpuread = GPU_VER;
		break;
	default: // handles 0, 1, 6
		break;
	}
}

