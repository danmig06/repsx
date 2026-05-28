#include <psx/gpu.h>
#include <psx/irq.h>

#include "gpucmds.h"
#include "util.h"
#include "rdef/gpu.h"
#include "log.h"

#include <stdbool.h>
#include <string.h>
#define GPU_VER 1
#define SE11(v) ((int16_t)((v) << 5) >> 5)

#define gpu_render_poly(gpu, sh, ...) gpu->renderer.poly(&gpu->renderer, sh, &(struct psx_render_args){ __VA_ARGS__ })
#define gpu_render_rect(gpu, textured, ...) gpu->renderer.rect(&gpu->renderer, textured, &(struct psx_render_args){ __VA_ARGS__ })
#define gpu_render_line(gpu, gouraud,  ...) gpu->renderer.line(&gpu->renderer, gouraud,  &(struct psx_render_args){ __VA_ARGS__ })

typedef union gp0_color {
	struct __attribute__((packed)) {
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t x;
	};
	uint32_t raw;
} color_t;

typedef union gp0_tex_uv {
	struct {
		uint8_t u;
		uint8_t v;
		uint16_t info;
	};
	uint32_t raw;
} tex_uv_t;

// attributes such as vertices and CLUT attributes do not have a raw representation
// because the x and y components need to be mainpulated
typedef struct gp0_pos {
	int16_t x;
	int16_t y;
} pos_t;

// vec2s are basically the same thing, except they can
// be constructed from the raw representation
typedef union gp0_vec2 {
	struct {
		uint16_t x;
		uint16_t y;
	};
	uint32_t raw;
} vec2_t;

typedef struct gp0_texpage_t {
	pos_t pos;
	uint8_t semi_transparency;
	uint8_t depth;
} texpage_t;

static inline pos_t make_vert(uint32_t cmd) {
	return (pos_t) {
		.x = SE11(cmd & 0xffff),
		.y = SE11(cmd >> 16)
	};
}

static inline pos_t make_clut(uint16_t cmd) {
	return (pos_t) {
		.x = (cmd & 0x3f) * 16,
		.y = (cmd >> 6) & 0x1ff
	};
}

void gp0_cache_clear(struct psx_gpu* gpu) {
	return;
}

void gp0_poly(struct psx_gpu* gpu) {
	struct {
		uint32_t color0;
		bool is_raw;
		bool is_transparent;
		bool is_textured;
		bool is_quad;
		bool is_gouraud_shaded;
	} poly;
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	poly.color0 = cmd & 0xffffff;
	poly.is_raw = (cmd & BIT(24)) != 0;
	poly.is_transparent = (cmd & BIT(25)) != 0;
	poly.is_textured = (cmd & BIT(26)) != 0;
	poly.is_quad = (cmd & BIT(27)) != 0;
	poly.is_gouraud_shaded = (cmd & BIT(28)) != 0;

	unsigned arg_idx = 1;
	color_t c[4] = { [0] = { .raw = poly.color0 } };
	pos_t v[4] = { 0 };
	tex_uv_t tex_data[4] = { 0 };
	pos_t clut = { 0 };
	texpage_t texpage = { 0 };

	v[0] = make_vert(gpu->cmd.buf[arg_idx++]);
	v[0].x += gpu->draw_off.x;
	v[0].y += gpu->draw_off.y;
	if(poly.is_textured) {
		tex_data[0].raw = gpu->cmd.buf[arg_idx++];
		clut = make_clut(tex_data[0].info);
	}

	int min_x = v[0].x, max_x = v[0].x;
	int min_y = v[0].y, max_y = v[0].y;
	int n_vertices = ((poly.is_quad) ? 4 : 3);
	for(int i = 1; i < n_vertices; i++) {
		if(poly.is_gouraud_shaded) {
			c[i].raw = gpu->cmd.buf[arg_idx++];
		} else {
			c[i] = c[0];
		}
		v[i] = make_vert(gpu->cmd.buf[arg_idx++]);
		v[i].x += gpu->draw_off.x;
		if(v[i].x > max_x) max_x = v[i].x;
		if(v[i].x < min_x) min_x = v[i].x;
		v[i].y += gpu->draw_off.y;
		if(v[i].y > max_y) max_y = v[i].y;
		if(v[i].y < min_y) min_y = v[i].y;
		if(poly.is_textured) {
			tex_data[i].raw = gpu->cmd.buf[arg_idx++];
		}
	}
	if((max_x - min_x) > 0x3ff || (max_y - min_y) > 0x1ff) {
		return;
	}

	int shading_mode = PSX_RENDERER_SH_FLAT;
	if(poly.is_textured) {
		shading_mode = PSX_RENDERER_SH_TEXTURE;
		uint16_t raw_texpage = tex_data[1].info;
		texpage.pos.x = (raw_texpage & 0xf) * 64;
		texpage.pos.y = (raw_texpage & BIT(4)) ? 256 : 0;
		texpage.semi_transparency = (raw_texpage >> 5) & 3;
		texpage.depth = (raw_texpage >> 7) & 3;
		// copy the current texpage to GPUSTAT
		GPUSTAT_TPX_SET(gpu->gpustat, raw_texpage & 0xf);
		if(raw_texpage & BIT(4)) {
			gpu->gpustat |= GPUSTAT_TPY;
		} else {
			gpu->gpustat &= ~GPUSTAT_TPY;
		}
		GPUSTAT_TPDEPTH_SET(gpu->gpustat, texpage.depth);
		GPUSTAT_ST_SET(gpu->gpustat, texpage.semi_transparency);
	} else if(poly.is_gouraud_shaded) {
		shading_mode = PSX_RENDERER_SH_GOURAUD;
	}

	psx_gpu_vert_t rv[4] = {
		{ .x = v[0].x, .y = v[0].y, .color = c[0].raw, .tx = tex_data[0].u, .ty = tex_data[0].v },
		{ .x = v[1].x, .y = v[1].y, .color = c[1].raw, .tx = tex_data[1].u, .ty = tex_data[1].v },
		{ .x = v[2].x, .y = v[2].y, .color = c[2].raw, .tx = tex_data[2].u, .ty = tex_data[2].v },
		{ .x = v[3].x, .y = v[3].y, .color = c[3].raw, .tx = tex_data[3].u, .ty = tex_data[3].v }
	}; 
	bool use_mask = (gpu->gpustat & GPUSTAT_USEMSK) != 0;
	bool set_mask = (gpu->gpustat & GPUSTAT_SETMSK) != 0;
	int st_mode = (poly.is_transparent) ? GPUSTAT_ST_GET(gpu->gpustat) : PSX_RENDERER_ST_NONE;
	gpu_render_poly(gpu, shading_mode,
			.poly.v0 = rv[0], .poly.v1 = rv[1], .poly.v2 = rv[2], .poly.v3 = rv[3], .use_mask_bit = use_mask, .set_mask_bit = set_mask,
			.tex.clut = { clut.x, clut.y }, .tex.page = { texpage.pos.x, texpage.pos.y }, .tex.offset = gpu->tex_window.off,
			.tex.mask = gpu->tex_window.mask, .tex.depth_mode = texpage.depth, .tex.need_modulation = !poly.is_raw,
			.need_dithering = (shading_mode == PSX_RENDERER_SH_GOURAUD) || !poly.is_raw, .poly.is_quad = poly.is_quad, .transparency_mode = st_mode);
}

void gp0_polyline_update(struct psx_gpu* gpu) {
	struct {
		uint32_t color0;
		bool is_transparent;
		bool is_gouraud_shaded;
	} line;
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	line.color0 = cmd & 0xffffff;
	line.is_transparent = (cmd & BIT(25)) != 0;
	line.is_gouraud_shaded = (cmd & BIT(28)) != 0;

	color_t c[2];
	pos_t v[2];
	uint32_t* state = &gpu->cmd.buf[GPU_CMD_POLYLINE_STATE];
	uint32_t word = gpu->cmd.buf[GPU_CMD_DATA_IDX];
	if((word & 0xf000f000) == 0x50005000) {
		gpu->cmd.receiving_data = false;
		// returning early ends the vertex sequence
		return;
	}
	switch(*state) {
	// for the first pass, intialize the previous Color/Vertex attributes before parsing the next ones
	case GPU_CMD_POLYLINE_CPREV:
		gpu->cmd.buf[GPU_CMD_POLYLINE_CPREV] = word;
		*state = GPU_CMD_POLYLINE_VPREV;
		break;
	case GPU_CMD_POLYLINE_VPREV:
		gpu->cmd.buf[GPU_CMD_POLYLINE_VPREV] = word;
		if(line.is_gouraud_shaded) {
			*state = GPU_CMD_POLYLINE_CCUR;
		} else {
			*state = GPU_CMD_POLYLINE_VCUR;
		}
		break;
	case GPU_CMD_POLYLINE_CCUR:
		gpu->cmd.buf[GPU_CMD_POLYLINE_CCUR] = word;
		*state = GPU_CMD_POLYLINE_VCUR;
		break;
	case GPU_CMD_POLYLINE_VCUR:
		if(line.is_gouraud_shaded) {
			c[0].raw = gpu->cmd.buf[GPU_CMD_POLYLINE_CPREV];
			c[1].raw = gpu->cmd.buf[GPU_CMD_POLYLINE_CCUR];
			*state = GPU_CMD_POLYLINE_CCUR;
		} else {
			c[0].raw = c[1].raw = line.color0;
			*state = GPU_CMD_POLYLINE_VCUR;
		}

		int st_mode = (line.is_transparent) ? GPUSTAT_ST_GET(gpu->gpustat) : PSX_RENDERER_ST_NONE;
		v[0] = make_vert(gpu->cmd.buf[GPU_CMD_POLYLINE_VPREV]);
		v[0].x += gpu->draw_off.x;
		v[0].y += gpu->draw_off.y;
		if(word == gpu->cmd.buf[GPU_CMD_POLYLINE_VPREV]) {
			gpu_render_rect(gpu, false,
					.rect.v = { .x = v[0].x, .y = v[0].y, .color = c[0].raw },
					.rect.w = 1, .rect.h = 1,
					.use_mask_bit = (gpu->gpustat & GPUSTAT_USEMSK) != 0,
					.set_mask_bit = (gpu->gpustat & GPUSTAT_SETMSK) != 0,
					.transparency_mode = st_mode);
			break;
		}
		v[1] = make_vert(word);
		v[1].x += gpu->draw_off.x;
		v[1].y += gpu->draw_off.y;
		gpu->cmd.buf[GPU_CMD_POLYLINE_VPREV] = word;
		gpu->cmd.buf[GPU_CMD_POLYLINE_CPREV] = gpu->cmd.buf[GPU_CMD_POLYLINE_CCUR];
		int32_t dx = v[1].x - v[0].x;
		int32_t dy = v[1].y - v[0].y;
		int32_t adx = ABS(dx);
		int32_t ady = ABS(dy);

		if(adx > 0x3ff || ady > 0x1ff) {
			break;
		} else if(adx > ady) {
			(dx > 0) ? v[1].x++ : v[0].x++;
		} else {
			(dy > 0) ? v[1].y++ : v[0].y++;
		}

		gpu_render_line(gpu, line.is_gouraud_shaded,
				.line.v0 = { .x = v[0].x, .y = v[0].y, .color = c[0].raw },
				.line.v1 = { .x = v[1].x, .y = v[1].y, .color = c[1].raw },
				.use_mask_bit = (gpu->gpustat & GPUSTAT_USEMSK) != 0,
				.set_mask_bit = (gpu->gpustat & GPUSTAT_SETMSK) != 0, 
				.transparency_mode = st_mode);
		break;
	default:
		return;
	}

	// line data is not done yet, since we didn't get a terminator, returning early causes the vertex sequence to end
	gpu->cmd.words_left = true;
}

void gp0_line(struct psx_gpu* gpu) {
	struct {
		uint32_t color0;
		bool is_transparent;
		bool polyline;
		bool is_gouraud_shaded;
	} line;
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	line.color0 = cmd & 0xffffff;
	line.is_transparent = (cmd & BIT(25)) != 0;
	line.polyline = (cmd & BIT(27)) != 0;
	line.is_gouraud_shaded = (cmd & BIT(28)) != 0;

	unsigned arg_idx = 1;
	color_t c[2];
	pos_t v[2];

	c[0].raw = line.color0;
	v[0] = make_vert(gpu->cmd.buf[arg_idx++]);
	v[0].x += gpu->draw_off.x;
	v[0].y += gpu->draw_off.y;
	if(line.is_gouraud_shaded) {
		c[1].raw = gpu->cmd.buf[arg_idx++];
	} else {
		c[1] = c[0];
	}
	v[1] = make_vert(gpu->cmd.buf[arg_idx++]);
	v[1].x += gpu->draw_off.x;
	v[1].y += gpu->draw_off.y;

	int32_t dx = v[1].x - v[0].x;
	int32_t dy = v[1].y - v[0].y;
	int32_t adx = ABS(dx);
	int32_t ady = ABS(dy);

	if(adx > 0x3ff || ady > 0x1ff) {
		return;
	} else if(adx > ady) {
		(dx > 0) ? v[1].x++ : v[0].x++;
	} else {
		(dy > 0) ? v[1].y++ : v[0].y++;
	}

	int st_mode = (line.is_transparent) ? GPUSTAT_ST_GET(gpu->gpustat) : PSX_RENDERER_ST_NONE;
	gpu_render_line(gpu, line.is_gouraud_shaded,
			.line.v0 = { .x = v[0].x, .y = v[0].y, .color = c[0].raw },
			.line.v1 = { .x = v[1].x, .y = v[1].y, .color = c[1].raw },
			.use_mask_bit = (gpu->gpustat & GPUSTAT_USEMSK) != 0,
			.set_mask_bit = (gpu->gpustat & GPUSTAT_SETMSK) != 0,
			.transparency_mode = st_mode);
}

void gp0_rect(struct psx_gpu* gpu) {
	struct {
		uint32_t color0;
		bool is_raw; 
		bool is_transparent; 
		bool is_textured; 
		int geometry;
	} rect;
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	rect.color0 = cmd & 0xffffff;
	rect.is_raw = (cmd & BIT(24)) != 0;
	rect.is_transparent = (cmd & BIT(25)) != 0;
	rect.is_textured = (cmd & BIT(26)) != 0;
	rect.geometry = (cmd >> 27) & 3;

	unsigned arg_idx = 1;
	pos_t tl;
	vec2_t size;
	tex_uv_t tex_data = { 0 };
	pos_t clut;

	color_t c;
	c.raw = rect.color0;
	tl = make_vert(gpu->cmd.buf[arg_idx++]);
	tl.x += gpu->draw_off.x;
	tl.y += gpu->draw_off.y;
	if(rect.is_textured) {
		tex_data.raw = gpu->cmd.buf[arg_idx++];
		clut = make_clut(tex_data.info);
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
		size.raw = gpu->cmd.buf[arg_idx++];
		break;
	}
	pos_t texpage_pos;
	texpage_pos.x = GPUSTAT_TPX_GET(gpu->gpustat) * 64;
	texpage_pos.y = (gpu->gpustat & GPUSTAT_TPY) ? 256 : 0;
	
	bool use_mask = (gpu->gpustat & GPUSTAT_USEMSK) != 0;
	bool set_mask = (gpu->gpustat & GPUSTAT_SETMSK) != 0;
	int st_mode = (rect.is_transparent) ? GPUSTAT_ST_GET(gpu->gpustat) : PSX_RENDERER_ST_NONE;
	gpu_render_rect(gpu, rect.is_textured,
			.rect.v = { .x = tl.x, .y = tl.y, .color = rect.color0, .tx = tex_data.u, .ty = tex_data.v },
			.rect.w = size.x, .rect.h = size.y, .tex.clut = { clut.x, clut.y }, .tex.page = { texpage_pos.x, texpage_pos.y },
			.tex.offset = gpu->tex_window.off, .tex.mask = gpu->tex_window.mask, .tex.depth_mode = GPUSTAT_TPDEPTH_GET(gpu->gpustat),
			.tex.need_modulation = !rect.is_raw, .transparency_mode = st_mode, .use_mask_bit = use_mask, .set_mask_bit = set_mask);
}

void gp0_fillvram(struct psx_gpu* gpu) {
	uint32_t color0 = gpu->cmd.buf[GPU_CMD_IDX] & 0xffffff;
	vec2_t tl, size;
	tl.raw = gpu->cmd.buf[1];
	tl.x &= 0x3f0;
	tl.y &= 0x1ff;
	size.raw = gpu->cmd.buf[2];
	size.x = ((size.x & 0x3ff) + 0xf) & 0xfff0;
	size.y &= 0x1ff;
	gpu_render_rect(gpu, false,
		       .rect.v = { .x = tl.x, .y = tl.y, .color = color0 },
		       .rect.w = size.x, .rect.h = size.y, .rect.is_clear = true, .use_mask_bit = false,
		       .set_mask_bit = (gpu->gpustat & GPUSTAT_SETMSK) != 0);
}

static void gp0_image_load_update(struct psx_gpu* gpu) {
	union {
		uint16_t colors[2];
		uint32_t raw;
	} data;
	uint32_t mask_bits = (gpu->gpustat & GPUSTAT_SETMSK) ? 0x80008000 : 0;
	data.raw = gpu->cmd.buf[GPU_CMD_DATA_IDX] | mask_bits;
	bool check_mask = (gpu->gpustat & GPUSTAT_USEMSK) != 0;

	uint32_t max_x = gpu->blit.box.x + gpu->blit.box.w;
	uint32_t max_y = gpu->blit.box.y + gpu->blit.box.h;
	uint32_t x = gpu->blit.cx - gpu->blit.box.x;
	uint32_t y = gpu->blit.cy - gpu->blit.box.y;

	uint16_t* texel = &gpu->blit.texels[x + (y * gpu->blit.box.w)];
	if(!(check_mask && (*texel & 0x8000))) {
		*texel = data.colors[0];
	}

	gpu->blit.cx++;
	if(gpu->blit.cx == max_x) {
		gpu->blit.cx = gpu->blit.box.x;
		gpu->blit.cy++;
		x = gpu->blit.cx - gpu->blit.box.x;
		y = gpu->blit.cy - gpu->blit.box.y;
		texel = &gpu->blit.texels[x + (y * gpu->blit.box.w)];
		if(gpu->blit.cy == max_y) {
			goto load_end;
		}
	} else {
		texel++;
	}

	if(!(check_mask && (*texel & 0x8000))) {
		*texel = data.colors[1];
	}
	gpu->blit.cx++;
	if(gpu->blit.cx == max_x) {
		gpu->blit.cx = gpu->blit.box.x;
		gpu->blit.cy++;
	}

	if(gpu->cmd.words_left == 0) {
load_end:
		gpu->renderer.commit_vram(&gpu->renderer, gpu->blit.texels);
		gpu->renderer.dispose_vram(&gpu->renderer, gpu->blit.texels);
		gpu->blit.texels = NULL;
	}
}

void gp0_image_load(struct psx_gpu* gpu) {
	// command word has no information
	gpu->blit.box = (psx_gpu_rect_t) {
		.x = gpu->cmd.buf[1] & 0x3ff,
		.y = (gpu->cmd.buf[1] >> 16) & 0x1ff,
		.w = gpu->cmd.buf[2] & 0xffff,
		.h = gpu->cmd.buf[2] >> 16
	};

	gpu->blit.box.w = ((gpu->blit.box.w - 1) & 0x3ff) + 1;
	gpu->blit.box.h = ((gpu->blit.box.h - 1) & 0x1ff) + 1;
	gpu->blit.is_read = false;

	gpu->blit.cx = gpu->blit.box.x;
	gpu->blit.cy = gpu->blit.box.y;
	gpu->blit.texels = gpu->renderer.get_vram(&gpu->renderer, &gpu->blit.box, false);

	gpu->cmd.receiving_data = true;
	gpu->cmd.words_left = (gpu->blit.box.w * gpu->blit.box.h) + 1;
	gpu->cmd.words_left >>= 1;
	gpu->cmd.update = gp0_image_load_update;
}

void gp0_image_store(struct psx_gpu* gpu) {
	// command word has no information
	gpu->blit.box = (psx_gpu_rect_t) {
		.x = gpu->cmd.buf[1] & 0xffff,
		.y = gpu->cmd.buf[1] >> 16,
		.w = gpu->cmd.buf[2] & 0xffff,
		.h = gpu->cmd.buf[2] >> 16
	};

	gpu->blit.cx = gpu->blit.box.x;
	gpu->blit.cy = gpu->blit.box.y;
	gpu->blit.is_read = true;
	gpu->blit.texels = gpu->renderer.get_vram(&gpu->renderer, &gpu->blit.box, true);

	gpu->gpustat |= GPUSTAT_VRAM_READY;
}

void gp0_blit(struct psx_gpu* gpu) {
	// command word has no information
	psx_gpu_vec2_t src = {
		.x = gpu->cmd.buf[1] & 0xffff,
		.y = gpu->cmd.buf[1] >> 16
	};
	psx_gpu_vec2_t dst = {
		.x = gpu->cmd.buf[2] & 0xffff,
		.y = gpu->cmd.buf[2] >> 16
	};
	int w, h;
	w = gpu->cmd.buf[3] & 0xffff;
	h = gpu->cmd.buf[3] >> 16;

	gpu->renderer.blit(&gpu->renderer, src, dst, w, h);
}

void gp0_draw_mode(struct psx_gpu* gpu) {
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	GPUSTAT_TPX_SET(gpu->gpustat, cmd & 0xf);
	if(cmd & BIT(4)) {
		gpu->gpustat |= GPUSTAT_TPY;
	} else {
		gpu->gpustat &= ~GPUSTAT_TPY;
	}
	GPUSTAT_ST_SET(gpu->gpustat, (cmd >> 5) & 3);
	GPUSTAT_TPDEPTH_SET(gpu->gpustat, (cmd >> 7) & 3);
	if(cmd & BIT(9)) {
		gpu->gpustat |= GPUSTAT_DITHER;
	} else {
		gpu->gpustat &= ~GPUSTAT_DITHER;
	}
	if(cmd & BIT(10)) {
		gpu->gpustat |= GPUSTAT_DRAWEN;
	} else {
		gpu->gpustat &= ~GPUSTAT_DRAWEN;
	}
}

void gp0_set_texture_window(struct psx_gpu* gpu) {
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	gpu->tex_window.mask = (psx_gpu_vec2_t) {
		.x = cmd & 0x1f,
		.y = (cmd >> 5) & 0x1f
	};
	gpu->tex_window.off = (psx_gpu_vec2_t) {
		.x = (cmd >> 10) & 0x1f,
		.y = (cmd >> 15) & 0x1f
	};
}

void gp0_draw_area_tl(struct psx_gpu* gpu) {
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	gpu->draw_area.start = (psx_gpu_vec2_t) {
		.x = cmd & 0x3ff,        // 10 bits
		.y = (cmd >> 10) & 0x3ff // 10 bits
	};

	gpu->renderer.clip_update(&gpu->renderer, gpu->draw_area.start, gpu->draw_area.end);
}

void gp0_draw_area_br(struct psx_gpu* gpu) {
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	gpu->draw_area.end = (psx_gpu_vec2_t) {
		.x = cmd & 0x3ff,        // 10 bits
		.y = (cmd >> 10) & 0x3ff // 10 bits
	};

	gpu->renderer.clip_update(&gpu->renderer, gpu->draw_area.start, gpu->draw_area.end);
}

void gp0_set_draw_offset(struct psx_gpu* gpu) {
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	gpu->draw_off.x = SE11(cmd & 0x7ff); // 11 bits, sign-extended
	gpu->draw_off.y = SE11((cmd >> 11) & 0x7ff); // 11 bits, sign-extended
	log_trace("draw offset set to x=%hd, y=%hd", gpu->draw_off.x, gpu->draw_off.y);
}

void gp0_set_mask_bit(struct psx_gpu* gpu) {
	uint32_t cmd = gpu->cmd.buf[GPU_CMD_IDX];
	if(cmd & BIT(0)) {
		gpu->gpustat |= GPUSTAT_SETMSK;
	} else {
		gpu->gpustat &= ~GPUSTAT_SETMSK;
	}
	if(cmd & BIT(1)) {
		gpu->gpustat |= GPUSTAT_USEMSK;
	} else {
		gpu->gpustat &= ~GPUSTAT_USEMSK;
	}
}

void gp1_reset(struct psx_gpu* gpu, uint32_t cmd) {
	psx_gpu_reset(gpu);
}

void gp1_buffer_clear(struct psx_gpu* gpu, uint32_t cmd) {
	memset(&gpu->cmd, 0, sizeof(gpu->cmd));
}

void gp1_irq_ack(struct psx_gpu* gpu, uint32_t cmd) {
	gpu->gpustat &= ~GPUSTAT_IRQ;
}

void gp1_display_mode(struct psx_gpu* gpu, uint32_t cmd) {
	GPUSTAT_HRES1_SET(gpu->gpustat, cmd & 3);
	if(cmd & BIT(6)) {
		gpu->gpustat |= GPUSTAT_HRES2;
	} else {
		gpu->gpustat &= ~GPUSTAT_HRES2;
	}
	if(cmd & BIT(2)) {
		gpu->gpustat |= GPUSTAT_VRES;
	} else {
		gpu->gpustat &= ~GPUSTAT_VRES;
	}
	if(cmd & BIT(3)) {
		gpu->gpustat |= GPUSTAT_VMODE;
	} else {
		gpu->gpustat &= ~GPUSTAT_VMODE;
	}
	if(cmd & BIT(4)) {
		gpu->gpustat |= GPUSTAT_RGB24EN;
	} else {
		gpu->gpustat &= ~GPUSTAT_RGB24EN;
	}
	if(cmd & BIT(7)) {
		gpu->gpustat |= GPUSTAT_HFLIP;
	} else {
		gpu->gpustat &= ~GPUSTAT_HFLIP;
	}
	if(cmd & BIT(5)) {
		gpu->gpustat |= GPUSTAT_VINTERLACE;
	} else {
		gpu->gpustat &= ~GPUSTAT_VINTERLACE;
	}
}

void gp1_dma_direction(struct psx_gpu* gpu, uint32_t cmd) {
	int dir = cmd & 3;
	GPUSTAT_DMADIR_SET(gpu->gpustat, dir);
	if(dir == 2) {
		gpu->gpustat |= GPUSTAT_DMAREQ;
	} else {
		gpu->gpustat &= ~GPUSTAT_DMAREQ;
	}
}

void gp1_set_display_area(struct psx_gpu* gpu, uint32_t cmd) {
	gpu->display_area.x = cmd & 0x3fe; // 10 bits
	gpu->display_area.y = (cmd >> 10) & 0x1ff; // 9 bits
}

void gp1_set_display_hrange(struct psx_gpu* gpu, uint32_t cmd) {
	gpu->display_area.x1 = cmd & 0xfff; // 12 bits
	gpu->display_area.x2 = (cmd >> 12) & 0xfff; // 12 bits
}

void gp1_set_display_vrange(struct psx_gpu* gpu, uint32_t cmd) {
	gpu->display_area.y1 = cmd & 0x3ff; // 10 bits
	gpu->display_area.y2 = (cmd >> 10) & 0x3ff; // 10 bits
}

void gp1_set_display_disable(struct psx_gpu* gpu, uint32_t cmd) {
	if(cmd & 1) {
		gpu->gpustat |= GPUSTAT_DPY_DISABLE;
	} else {
		gpu->gpustat &= ~GPUSTAT_DPY_DISABLE;
	}
}

void gp1_read_register(struct psx_gpu* gpu, uint32_t cmd) {
	uint32_t index = cmd & 7;

	switch(index) {
	case 2:
		gpu->gpuread = gpu->tex_window.mask.x | (gpu->tex_window.mask.y << 5);
		gpu->gpuread |= (gpu->tex_window.off.x << 10) | (gpu->tex_window.off.y << 15);
		break;
	case 3:
		gpu->gpuread = gpu->draw_area.start.x | (gpu->draw_area.start.y << 10);
		break;
	case 4:
		gpu->gpuread = gpu->draw_area.end.x | (gpu->draw_area.end.y << 10);
		break;
	case 5:
		gpu->gpuread = (gpu->draw_off.x & 0x7ff) | ((gpu->draw_off.y & 0x7ff) << 11);
		break;
	case 7:
		gpu->gpuread = GPU_VER;
		break;
	default: // handles 0, 1, 6
		break;
	}
}

