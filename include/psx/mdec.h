#ifndef PSX_MDEC_H
#define PSX_MDEC_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdint.h>
#include <stdbool.h>

#define PSX_MDEC_BLOCK_SIZE 64
#define PSX_MDEC_EOB 0xfe00
#define PSX_MDEC_MB_BUFFER_SIZE (20)
#define PSX_MDEC_MB_SIZE (16 * 16 * 3)

#ifdef __cplusplus
extern "C" {
#endif

struct psx_mdec {
	struct {
		uint32_t command;
		uint32_t stat;
		uint32_t ctrl;
	} regs;

	union {
		struct {
			// Luminance Quantization Table
			uint8_t lqtab[64];
			// Color Quantization Table
			uint8_t cqtab[64];
		};
		uint8_t iqtab[128];
	};
	// Zig-Zag Scale Table
	int16_t scale_table[64];
	uint16_t* mb_data;
	struct {
		bool is_data;
		uint32_t offset;
		uint32_t size;
		uint32_t* dst;
	} input;
	struct {
		uint32_t offset;
		// uint8_t buf[16 * 16 * 3]; // PSX_MDEC_MB_SIZE * PSX_MDEC_MB_BUFFER_SIZE
		uint8_t buf[PSX_MDEC_MB_SIZE * PSX_MDEC_MB_BUFFER_SIZE];
	} dec;
	struct {
		bool is_monochrome;
		uint32_t index;
		uint32_t n_available;
		uint32_t offset;
		uint32_t size;
		int16_t cr[8 * 8];
		int16_t cb[8 * 8];
		int16_t y[8 * 8];
	} block;
	bool receiving_data;

	struct psx_system* sys;
};

void psx_mdec_init(struct psx_mdec* mdec);
void psx_mdec_reset(struct psx_mdec* mdec);
void psx_mdec_direct_in(struct psx_mdec* mdec, uint32_t word);
uint32_t psx_mdec_direct_out(struct psx_mdec* mdec);

uint32_t psx_mdec_read32(struct psx_region* reg, uint32_t addr);
void psx_mdec_write32(struct psx_region* reg, uint32_t addr, uint32_t val);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_MDEC_H
