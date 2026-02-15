#ifndef PSX_DMA_H
#define PSX_DMA_H

#include <psx/memory.h>

enum {
	PSX_DMA_MODE_MANUAL     = 0,
	PSX_DMA_MODE_REQUEST    = 1,
	PSX_DMA_MODE_LINKEDLIST = 2,
	PSX_DMA_MODE_RESERVED   = 3,
};

enum {
	PSX_DMA_DIR_TO_RAM   = 0,
	PSX_DMA_DIR_FROM_RAM = 1
};

typedef struct __attribute__((packed)) __psx_dma_channel {
	uint32_t start_addr;
	struct {
		union {
			uint16_t n_words;
			uint16_t block_size;
		};
		uint16_t n_blocks;
	} bc;
	uint32_t ctrl;
	uint32_t pad;
} psx_dma_channel_t;

struct psx_dmac {
	struct __attribute__((packed)) {
		psx_dma_channel_t chn[7];
		uint32_t dpcr;
		uint32_t dicr;
		uint32_t unk0;
		uint32_t unk1;
	} regs;

	struct psx_system* sys;
};

void psx_dmac_init(struct psx_dmac* dmac);
void psx_dmac_run_transfers(struct psx_dmac* dmac);

uint32_t psx_dmac_read32(struct psx_region* reg, uint32_t addr);
void psx_dmac_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_dmac_read16(struct psx_region* reg, uint32_t addr);
void psx_dmac_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_dmac_read8(struct psx_region* reg, uint32_t addr);
void psx_dmac_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif // #ifndef PSX_DMA_H
