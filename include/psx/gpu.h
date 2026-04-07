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

enum {
	PSX_GPU_TEXDEPTH_4BIT = 0,
	PSX_GPU_TEXDEPTH_8BIT = 1,
	PSX_GPU_TEXDEPTH_15BIT = 2,
	PSX_GPU_TEXDEPTH_RESERVED = 3 // Should behave like 15-bit mode
};

// Shading Modes
enum {
	PSX_RENDERER_SH_FLAT = 0,
	PSX_RENDERER_SH_GOURAUD = 1,
	PSX_RENDERER_SH_TEXTURE = 2
};

// Semi-Transparency Modes - B=Background (Old) Pixel, F=Foreground (New) Pixel
enum {
	PSX_RENDERER_ST_HALF = 0, // Half Addition       - (B / 2) + (F / 2)
	PSX_RENDERER_ST_ADD  = 1, // Addition            - B + F
	PSX_RENDERER_ST_RSUB = 2, // Reverse Subtraction - B - F
	PSX_RENDERER_ST_QADD = 3, // Quarter Addition    - B + (F / 4)
	PSX_RENDERER_ST_NONE = 0xff
};

typedef struct psx_gpu_vert {
	int16_t x;
	int16_t y;
	uint32_t color;
	uint8_t tx;
	uint8_t ty;
} psx_gpu_vert_t;

typedef struct psx_gpu_vec2 {
	uint16_t x;
	uint16_t y;
} psx_gpu_vec2_t;

// used for clip and screen update rectangles
typedef struct psx_gpu_rect {
	uint16_t x, y;
	int16_t w, h;
} psx_gpu_rect_t;

typedef struct psx_poly_args {
	bool is_quad;
	union {
		struct {
			psx_gpu_vert_t v0, v1, v2, v3;
		};
		psx_gpu_vert_t vertices[4];
	};
} psx_poly_args_t;

typedef struct psx_rect_args {
	bool is_clear;
	uint16_t w, h;
	psx_gpu_vert_t v;
} psx_rect_args_t;

typedef union psx_line_args {
	struct {
		psx_gpu_vert_t v0, v1;
	};
	psx_gpu_vert_t vertices[2];
} psx_line_args_t;

typedef struct psx_render_args {
	bool set_mask_bit;
	bool use_mask_bit;
	bool need_dithering;
	int transparency_mode;
	struct {
		psx_gpu_vec2_t page;
		psx_gpu_vec2_t clut;
		psx_gpu_vec2_t offset;
		psx_gpu_vec2_t mask;
		int depth_mode;
		bool need_modulation;
	} tex;
	union {
		psx_poly_args_t poly;
		psx_rect_args_t rect;
		psx_line_args_t line;
	};
} psx_render_args_t;

struct psx_renderer {
	void* host_data;

	void (*poly)(struct psx_renderer*, int shading_mode, struct psx_render_args*);
	void (*rect)(struct psx_renderer*, bool is_textured, struct psx_render_args*);
	void (*line)(struct psx_renderer*, bool is_gouraud,  struct psx_render_args*);
	void (*update)(struct psx_renderer*, const psx_gpu_rect_t* rect, bool is_24bit, bool display_enable);
	void (*clip_update)(struct psx_renderer*, psx_gpu_vec2_t start, psx_gpu_vec2_t end);
	void (*blit)(struct psx_renderer*, psx_gpu_vec2_t src, psx_gpu_vec2_t dst, int w, int h);
	uint16_t* (*get_vram)(struct psx_renderer*, const psx_gpu_rect_t* rect, bool is_read);
	void (*commit_vram)(struct psx_renderer*, uint16_t* vram_rect);
	void (*dispose_vram)(struct psx_renderer*, uint16_t* vram_rect);
};

struct psx_gpu {
	uint32_t gpuread;
	uint32_t gpustat;
	struct {
		union {
			void (*update)(struct psx_gpu*);
			void (*execute)(struct psx_gpu*);
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

