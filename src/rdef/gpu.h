#ifndef RDEF_GPU
#define RDEF_GPU

#include "../util.h"

// GPUSTAT
enum {
	GPUSTAT_TPX = BIT_RANGE(0, 4), // Texture Page X
#define GPUSTAT_TPX_SET(o, v) SET_BITS(o, v, GPUSTAT_TPX, 0)
#define GPUSTAT_TPX_GET(o) GET_BITS(o, GPUSTAT_TPX, 0)

	GPUSTAT_TPY = BIT(4), // Texture Page Y
	GPUSTAT_ST = BIT_RANGE(5, 2), // Semi-Transparency
#define GPUSTAT_ST_SET(o, v) SET_BITS(o, v, GPUSTAT_ST, 5)
#define GPUSTAT_ST_GET(o) GET_BITS(o, GPUSTAT_ST, 5)
	
	GPUSTAT_TPDEPTH = BIT_RANGE(7, 2), // Texpage Depth
#define GPUSTAT_TPDEPTH_SET(o, v) SET_BITS(o, v, GPUSTAT_TPDEPTH, 7)
#define GPUSTAT_TPDEPTH_GET(o) GET_BITS(o, GPUSTAT_TPDEPTH, 7)
	
	GPUSTAT_DITHER = BIT(9), // Dither 24-bit to 15-bit
	GPUSTAT_DRAWEN = BIT(10), // Display Draw Enable
	GPUSTAT_SETMSK = BIT(11), // Set Mask Bit when drawing
	GPUSTAT_USEMSK = BIT(12), // Check Mask Bit when drawing
	GPUSTAT_INTERLACE_FIELD = BIT(13), // Interlace Field
	GPUSTAT_HFLIP = BIT(14), // Display Horizontal Flip
	GPUSTAT_HRES2 = BIT(16), // Horizontal Resolution 2 (0=256/320/512/640, 1=368)
	GPUSTAT_HRES1 = BIT_RANGE(17, 2), // Horizontal Resolution 1 (0=256, 1=320, 2=512, 3=640)
#define GPUSTAT_HRES1_SET(o, v) SET_BITS(o, v, GPUSTAT_HRES1, 17)
#define GPUSTAT_HRES1_GET(o) GET_BITS(o, GPUSTAT_HRES1, 17)
	
	GPUSTAT_VRES = BIT(19), // Vertical Resolution
	GPUSTAT_VMODE = BIT(20), // Video Mode (NTSC/PAL)
	GPUSTAT_RGB24EN = BIT(21), // Depth (24-bit) enable
	GPUSTAT_VINTERLACE = BIT(22), // Vertical Interlace
	GPUSTAT_DPY_DISABLE = BIT(23), // Display Disable
	GPUSTAT_IRQ = BIT(24), // Interrupt (unused)
	GPUSTAT_DMAREQ = BIT(25), // DMA request
	GPUSTAT_CMD_READY = BIT(26), // Command Ready
	GPUSTAT_VRAM_READY = BIT(27), // VRAM ready
	GPUSTAT_DMA_READY = BIT(28), // DMA ready
	GPUSTAT_DMADIR = BIT_RANGE(29, 2), // DMA direction
#define GPUSTAT_DMADIR_SET(o, v) SET_BITS(o, v, GPUSTAT_DMADIR, 29)
#define GPUSTAT_DMADIR_GET(o) GET_BITS(o, GPUSTAT_DMADIR, 29)

	GPUSTAT_SCNODD = BIT(31) // Scanline/Frame Odd
};

#endif
