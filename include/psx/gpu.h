#ifndef PSX_GPU_H
#define PSX_GPU_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdint.h>
#include <stdbool.h>

#define PSX_GPU_VRAM_HEIGHT 512
#define PSX_GPU_VRAM_WIDTH 1024

// CPUClk * 11/7 ~ 44100hz * 0x300 * 11/7
#define PSX_GPU_VIDEO_CLOCK 53222400
#define PSX_GPU_VIDEO_CLOCKS_PER_SEC 53.222400f
#define PSX_GPU_TO_CPU_CLK_RATE 1.5714285f // (11 / 7)
// NTSC timings for a visible/full scanline (full: visible + blanking)
#define PSX_GPU_CLOCKS_PER_HDRAW_NTSC 2560
#define PSX_GPU_CLOCKS_PER_SCAN_NTSC 3413
// PAL timings for a visible/full scanline (full: visible + blanking)
#define PSX_GPU_CLOCKS_PER_HDRAW_PAL 2555
#define PSX_GPU_CLOCKS_PER_SCAN_PAL 3406
// NTSC visible/total scanlines
#define PSX_GPU_VISIBLE_SCANS_NTSC 240
#define PSX_GPU_TOTAL_SCANS_NTSC 263
// PAL visible/total scanlines
#define PSX_GPU_VISIBLE_SCANS_PAL 288
#define PSX_GPU_TOTAL_SCANS_PAL 314

enum psx_gpu_texdepth {
	PSX_GPU_TEXDEPTH_4BIT = 0,
	PSX_GPU_TEXDEPTH_8BIT = 1,
	PSX_GPU_TEXDEPTH_15BIT = 2,
	PSX_GPU_TEXDEPTH_RESERVED = 3
};

enum psx_gpu_dpydepth {
	PSX_GPU_DISPLAYDEPTH_15BIT = 0,
	PSX_GPU_DISPLAYDEPTH_24BIT = 1
};

enum psx_gpu_vmode {
	PSX_GPU_VMODE_NTSC = 0,
	PSX_GPU_VMODE_PAL = 1
};

enum psx_gpu_dmadir {
	PSX_GPU_DMADIR_OFF = 0,
	PSX_GPU_DMADIR_FIFO = 1,
	PSX_GPU_DMADIR_CPUTOGP0 = 2,
	PSX_GPU_DMADIR_VRAMTOCPU = 3
};

enum {
	PSX_RENDERER_SH_FLAT = 0,
	PSX_RENDERER_SH_GOURAUD = 1,
	PSX_RENDERER_SH_TEXTURE = 2
};

typedef struct psx_gpu_vert {
	int16_t x;
	int16_t y;
	uint32_t color;
	uint8_t tx;
	uint8_t ty;
} psx_gpu_vert_t;

typedef struct psx_gpu_color {
	uint16_t r: 5;
	uint16_t g: 5;
	uint16_t b: 5;
	uint16_t x: 1;
} psx_gpu_color_t;

typedef void (*psx_gpucmdfn_t)(struct psx_gpu*);

struct __psx_renderer_args {
	struct {
		union {
			struct {
				psx_gpu_vert_t v0, v1;
			};
			psx_gpu_vert_t vertices[2];
		};
	} line;
	struct {
		psx_gpu_vert_t v;
		uint32_t w, h;
		bool flip_x, flip_y, is_clear;
	} rect;
	struct {
		bool quad;
		union {
			struct {
				psx_gpu_vert_t v0, v1, v2, v3;
			};
			psx_gpu_vert_t vertices[4];
		};
	} poly;
	struct {
		uint16_t page_x, page_y;
		uint16_t clut_x, clut_y;
		uint16_t off_x, off_y;
		uint16_t mask_x, mask_y;
		enum psx_gpu_texdepth depth;
		bool need_dithering;
		bool need_modulation;
	} tex;
};

struct psx_renderer {
	void* host_data;

	void (*poly)(struct psx_renderer*, int shading_mode, bool is_quad, struct __psx_renderer_args*);
	#define gpu_render_poly(gpu, sh, quad, ...) gpu->renderer.poly(&gpu->renderer, sh, quad, &(struct __psx_renderer_args){ { 0 }, __VA_ARGS__ })
	
	void (*rect)(struct psx_renderer*, bool is_textured, struct __psx_renderer_args*);
	#define gpu_render_rect(gpu, textured, ...) gpu->renderer.rect(&gpu->renderer, textured, &(struct __psx_renderer_args){ { 0 }, __VA_ARGS__ }) 

	void (*line)(struct psx_renderer*, int shading_mode, struct __psx_renderer_args*);
	#define gpu_render_line(gpu, sh, ...) gpu->renderer.line(&gpu->renderer, sh, &(struct __psx_renderer_args){ { 0 }, __VA_ARGS__ })

	void (*update)(struct psx_renderer*);
	void (*clip_update)(struct psx_renderer*, unsigned x1, unsigned y1, unsigned x2, unsigned y2);
	void (*blit)(struct psx_renderer*, int sx, int sy, int dx, int dy, int w, int h);
	uint16_t* (*get_vram)(struct psx_renderer*, int x, int y, int w, int h);
	void (*commit_vram)(struct psx_renderer*, uint16_t* vram_rect);
	void (*dispose_vram)(struct psx_renderer*, uint16_t* vram_rect);
};

struct psx_gpu {
	uint32_t gpuread;
	uint32_t gpustat;
	struct {
		union {
			psx_gpucmdfn_t update;
			psx_gpucmdfn_t execute;
		};
		uint32_t buf[12];
		unsigned words_left;
		uint8_t idx;
		bool receiving_data;
	} cmd;
	struct {
		uint16_t x1;
		uint16_t y1;
		uint16_t x2;
		uint16_t y2;
	} draw_area;
	struct {
		uint16_t x;
		uint16_t y;
		uint16_t x1;
		uint16_t y1;
		uint16_t x2;
		uint16_t y2;
	} display_area;
	struct {
		int16_t x;
		int16_t y;
	} draw_off;
	struct {
		uint16_t mask_x;
		uint16_t mask_y;
		uint16_t off_x;
		uint16_t off_y;
	} tex_window;
	struct {
		uint16_t start_x;
		uint16_t start_y;
		uint16_t x;
		uint16_t y;
		uint16_t w;
		uint16_t h;
		bool is_read;
		uint16_t* texels;
	} blit_state;
	int scanline_count;
	struct psx_renderer renderer;
	struct psx_system* sys;
};

void psx_gpu_init(struct psx_gpu* gpu);
void psx_gpu_reset(struct psx_gpu* gpu);

uint32_t psx_gpu_read32(struct psx_region* reg, uint32_t addr);
void psx_gpu_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_gpu_read16(struct psx_region* reg, uint32_t addr);
void psx_gpu_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_gpu_read8(struct psx_region* reg, uint32_t addr);
void psx_gpu_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif // #ifndef PSX_GPU_H

