#ifndef PSX_DMA_H
#define PSX_DMA_H

#include <psx/memory.h>

enum {
	PSX_DMA_SYNC_MANUAL     = 0,
	PSX_DMA_SYNC_REQUEST    = 1,
	PSX_DMA_SYNC_LINKEDLIST = 2,
	PSX_DMA_SYNC_RESERVED   = 3,
};

enum {
	PSX_DMA_DIR_TO_RAM   = 0,
	PSX_DMA_DIR_FROM_RAM = 1
};

typedef struct __psx_dma_channel {
	uint32_t start_addr;
	struct {
		union {
			uint16_t n_words;
			uint16_t block_size;
		};
		uint16_t n_blocks;
	} bc;
	struct {
		uint32_t dir: 1;
		uint32_t addr_inc: 1;
		uint32_t unused1: 6;
		uint32_t mode: 1;
		uint32_t trn_mode: 2;
		uint32_t unused2: 5;
		uint32_t dma_chop_size: 3;
		uint32_t unused3: 1;
		uint32_t cpu_chop_size: 3;
		uint32_t unused4: 1;
		uint32_t trn_start: 1;
		uint32_t unused5: 3;
		uint32_t force_trn_start: 1;
		uint32_t force_trn_suspend: 1;
		uint32_t snoop_bus: 1;
		uint32_t unused6: 1;
	} ctrl;
	uint32_t pad;
} psx_dma_channel_t;

struct psx_dmac {
	struct {
		psx_dma_channel_t chn[7];
		struct {
			uint32_t mdecin_pr: 3;
			uint32_t mdecin_en: 1;
			uint32_t mdecout_pr: 3;
			uint32_t mdecout_en: 1;
			uint32_t gpu_pr: 3;
			uint32_t gpu_en: 1;
			uint32_t cdrom_pr: 3;
			uint32_t cdrom_en: 1;
			uint32_t spu_pr: 3;
			uint32_t spu_en: 1;
			uint32_t pio_pr: 3;
			uint32_t pio_en: 1;
			uint32_t otc_pr: 3;
			uint32_t otc_en: 1;
			uint32_t cpu_pr: 3;
			uint32_t cpu_en: 1;
		} dmactl;
		struct {
			uint32_t chn_irq_ctl: 7;
			uint32_t unused: 8;
			uint32_t bus_error: 1;
			uint32_t chn_irq_mask: 7;
			uint32_t master_irq_enable: 1;
			uint32_t chn_irq: 7;
			uint32_t master_irq: 1;
		} dmairq;
		uint32_t unk0;
		uint32_t unk1;
	} regs;
	uint32_t mdec_in_irq_delay;
	uint32_t mdec_out_irq_delay;
	uint32_t cdr_irq_delay;
	uint32_t spu_irq_delay;
	uint32_t gpu_irq_delay;
	uint32_t otc_irq_delay;

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
