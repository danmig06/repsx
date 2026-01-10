#include <psx/dma.h>
#include <psx/irq.h>

#include <string.h>

#include "util.h"
#include "log.h"
#define DMACTL_CHNPRIORITY(dmactl, c) ((AS_UINT32(dmactl) >> (c * 4)) & 7)
#define CHCR_MASK 0x71770503
#define OTC_CHCR_MASK 0x50000002
#define DMACHN_IRQ(dmac, id) (((dmac->regs.dmairq.chn_irq) >> (id)) & 1)
#define DMACHN_ENABLED(dmac, id) (((dmac->regs.dmairq.chn_irq_mask) >> (id)) & 1)
#define DMAIRQ_WRITE_MASK 0x00ff807f

enum dmachnidx_t {
	DMACHN_MDECIN  = 0,
	DMACHN_MDECOUT = 1,
	DMACHN_GPU     = 2,
	DMACHN_CDROM   = 3,
	DMACHN_SPU     = 4,
	DMACHN_PIO     = 5,
	DMACHN_OTC     = 6,
	DMACHN_NUM     = 7
};

struct copyvec {
	uint32_t addr;
	uint32_t words_left;
	int increment;
};

void psx_dmac_init(struct psx_dmac* dmac) {
	memset(dmac, 0, sizeof(*dmac));
	AS_UINT32(dmac->regs.dmactl) = 0x07654321;
	dmac->regs.dmairq.chn_irq = 1;
}

uint32_t psx_dmac_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t val = 0;
	uint32_t register_offset = addr - reg->start;

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));

	return val;
}

static void write_dmairq(struct psx_dmac* dmac, uint32_t val) {
	uint8_t updated_flags = dmac->regs.dmairq.chn_irq & ~(val >> 24);
	val &= DMAIRQ_WRITE_MASK;
	AS_UINT32(dmac->regs.dmairq) = (AS_UINT32(dmac->regs.dmairq) & ~(DMAIRQ_WRITE_MASK)) | val;
	dmac->regs.dmairq.chn_irq = updated_flags;
	dmac->regs.dmairq.master_irq = dmac->regs.dmairq.master_irq_enable && (dmac->regs.dmairq.chn_irq & dmac->regs.dmairq.chn_irq_mask) != 0;
}

void psx_dmac_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = addr - reg->start;
	
	if(register_offset == 0x74) {
		write_dmairq(reg->peripheral, val);
	} else {
		uint8_t* regs = reg->peripheral;
		memcpy(&regs[register_offset], &val, sizeof(val));

		if(((register_offset >> 2) & 3) == 2) {
			psx_dmac_run_transfers(reg->peripheral);
		}
	}
}

uint16_t psx_dmac_read16(struct psx_region* reg, uint32_t addr) {
	uint16_t val = 0;
	uint32_t register_offset = addr - reg->start;

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));

	return val;
}

void psx_dmac_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = addr - reg->start;

	if(register_offset >= 0x74 && 0x78 > register_offset) {
		int shift = register_offset - 0x74;
		write_dmairq(reg->peripheral, ((uint32_t)val) << (shift * 8));
	} else {
		uint8_t* regs = reg->peripheral;
		memcpy(&regs[register_offset], &val, sizeof(val));

		if(((register_offset >> 2) & 3) == 2) {
			psx_dmac_run_transfers(reg->peripheral);
		}
	}
}

uint8_t psx_dmac_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	uint8_t* regs = reg->peripheral;
	return regs[register_offset];
}

void psx_dmac_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = addr - reg->start;
	if(register_offset >= 0x74 && 0x78 > register_offset) {
		int shift = register_offset - 0x74;
		write_dmairq(reg->peripheral, ((uint32_t)val) << (shift * 8));
	} else {
		uint8_t* regs = reg->peripheral;
		regs[register_offset] = val;

		if(((register_offset >> 2) & 3) == 2) {
			psx_dmac_run_transfers(reg->peripheral);
		}
	}
}

uint32_t transfer_size(psx_dma_channel_t chn) {
	switch(chn.ctrl.trn_mode) {
	case PSX_DMA_SYNC_MANUAL:
		if(chn.bc.n_words == 0) {
			return 0x10000;
		}
		return chn.bc.n_words;
	case PSX_DMA_SYNC_REQUEST:
		return chn.bc.block_size * chn.bc.n_blocks;
	default:
		return 0;
	}
}

bool is_triggered(psx_dma_channel_t* chn) {
	if(chn->ctrl.trn_mode == PSX_DMA_SYNC_MANUAL) {
		return chn->ctrl.trn_start && chn->ctrl.force_trn_start;
	}
	return chn->ctrl.trn_start;
}

uint32_t fetch_word_dev(struct psx_dmac* dmac, struct copyvec* copy_state, enum dmachnidx_t channel) {
	uint32_t word = 0;
	switch(channel) {
	case DMACHN_MDECIN:
		break;
	case DMACHN_MDECOUT:
		break;
	case DMACHN_GPU:
		return psx_mem_read32(dmac->sys->memory, 0x1f801810);
	case DMACHN_CDROM:
		word = psx_mem_read8(dmac->sys->memory, 0x1f801802);
		word |= psx_mem_read8(dmac->sys->memory, 0x1f801802) << 8;
		word |= psx_mem_read8(dmac->sys->memory, 0x1f801802) << 16;
		word |= psx_mem_read8(dmac->sys->memory, 0x1f801802) << 24;
		return word;
	case DMACHN_SPU:
		break;
	case DMACHN_PIO:
		break;
	case DMACHN_OTC:
		if(copy_state->words_left == 1) {
			return 0xffffff;
		} else {
			return (copy_state->addr - 4) & 0x1fffff;
		}
	default:
		panic("invalid channel");
		break;
	}

	return 0;
}

void write_word_dev(struct psx_dmac* dmac, struct copyvec* copy_state, enum dmachnidx_t channel, uint32_t data) {
	switch(channel) {
	case DMACHN_MDECIN:
		break;
	case DMACHN_MDECOUT:
		break;
	case DMACHN_GPU:
		psx_mem_write32(dmac->sys->memory, 0x1f801810, data);
		break;
	case DMACHN_CDROM:
		break;
	case DMACHN_SPU:
		break;
	case DMACHN_PIO:
		break;
	case DMACHN_OTC:
		break;
	default:
		panic("invalid channel");
		break;
	}
}

void do_dev_blkcopy(struct psx_dmac* dmac, enum dmachnidx_t channel) {
	psx_dma_channel_t* chn = &dmac->regs.chn[channel];

	uint32_t start_addr = chn->start_addr & 0x1ffffc;

	struct copyvec copy_state;
	copy_state.words_left = transfer_size(*chn);
	copy_state.addr = start_addr;
	copy_state.increment = (chn->ctrl.addr_inc) ? -4 : 4;

	uint32_t src;
	if(chn->ctrl.dir == PSX_DMA_DIR_TO_RAM) {
		while(copy_state.words_left > 0) {
			src = fetch_word_dev(dmac, &copy_state, channel);
			psx_mem_write32(dmac->sys->memory, copy_state.addr, src);

			copy_state.addr = (copy_state.addr + copy_state.increment) & 0x1fffff;
			chn->start_addr = copy_state.addr;
			copy_state.words_left--;
		}
	} else {
		while(copy_state.words_left > 0) {
			src = psx_mem_read32(dmac->sys->memory, copy_state.addr);
			write_word_dev(dmac, &copy_state, channel, src);

			copy_state.addr = (copy_state.addr + copy_state.increment) & 0x1fffff;
			chn->start_addr = copy_state.addr;
			copy_state.words_left--;
		}
	}
}

void do_dev_linked_list(struct psx_dmac* dmac, enum dmachnidx_t channel) {
	if(channel != DMACHN_GPU) {
		panic("linked list mode is not implemented for devices other than GPU");
	}

	psx_dma_channel_t chn = dmac->regs.chn[channel];
	if(chn.ctrl.dir != PSX_DMA_DIR_FROM_RAM) {
		panic("invalid linked list transfer");
	}

	uint32_t addr = chn.start_addr & 0x1ffffc;
	uint32_t list_header;
	uint32_t item;
	uint8_t items_left;
	uint32_t limit = 16384;

	while(limit--) {
		list_header = psx_mem_read32(dmac->sys->memory, addr);
		items_left = list_header >> 24;

		while(items_left > 0) {
			addr = (addr + 4) & 0x1ffffc;
			item = psx_mem_read32(dmac->sys->memory, addr);
			psx_mem_write32(dmac->sys->memory, 0x1f801810, item);
			items_left--;
		}
		
		if(list_header & 0x800000) {
			break;
		}

		addr = list_header & 0x1ffffc;
		dmac->gpu_irq_delay++;
	}
}

void do_transfer(struct psx_dmac* dmac, enum dmachnidx_t channel) {
	psx_dma_channel_t* chn = &dmac->regs.chn[channel];
	switch(channel) {
	case DMACHN_MDECIN:
		if(!(dmac->regs.dmactl.mdecin_en && is_triggered(chn))) {
			return;
		}

		log_error("Unhandled DMACHN_MDECIN transfer (IRQ triggered)");
		// dmac->regs.dmactl.mdecin_en = false;
		chn->ctrl.force_trn_start = false;
		break;
	case DMACHN_MDECOUT:
		if(!(dmac->regs.dmactl.mdecout_en && is_triggered(chn))) {
			return;
		}

		log_error("Unhandled DMACHN_MDECOUT transfer (IRQ triggered)");
		// dmac->regs.dmactl.mdecout_en = false;
		chn->ctrl.force_trn_start = false;
		break;
	case DMACHN_GPU:
		if(!(dmac->regs.dmactl.gpu_en && is_triggered(chn))) {
			return;
		}
		
		if(chn->ctrl.trn_mode != PSX_DMA_SYNC_LINKEDLIST) {
			chn->ctrl.force_trn_start = false;
			dmac->gpu_irq_delay = transfer_size(*chn);
			do_dev_blkcopy(dmac, channel);
		} else {
			do_dev_linked_list(dmac, channel);
		}
		break;
	case DMACHN_CDROM:
		if(!(dmac->regs.dmactl.cdrom_en && is_triggered(chn))) {
			return;
		}

		if(chn->ctrl.dir == PSX_DMA_DIR_TO_RAM) {
			chn->ctrl.force_trn_start = false;
			do_dev_blkcopy(dmac, channel);
		} else {
			log_error("Unhandled DMACHN_CDROM transfer");
		}
		break;
	case DMACHN_SPU:
		if(!(dmac->regs.dmactl.spu_en && is_triggered(chn))) {
			return;
		}

		// many games won't start if they get no feedback
		log_error("Unhandled DMACHN_SPU transfer (IRQ triggered)");
		chn->ctrl.force_trn_start = false;
		break;
	case DMACHN_PIO:
		if(!(dmac->regs.dmactl.pio_en && is_triggered(chn))) {
			return;
		}

		log_error("Unhandled DMACHN_PIO transfer");
		dmac->regs.dmactl.pio_en = false;
		break;
	case DMACHN_OTC:
		// OTC is hardwired differently, though the masks overlap
		// chn[7].bit2 is hardwired to 1
		chn->ctrl.addr_inc = true;
		// transfer mode is not factored in so we treat it as manual,
		// since it actually is a garbage value it will be cleared later
		chn->ctrl.trn_mode = PSX_DMA_SYNC_MANUAL;
		chn->ctrl.dir = PSX_DMA_DIR_TO_RAM;
		if(!(dmac->regs.dmactl.otc_en && is_triggered(chn))) {
			AS_UINT32(chn->ctrl) &= OTC_CHCR_MASK;
			return;
		}

		AS_UINT32(chn->ctrl) &= OTC_CHCR_MASK;

		// cleared on start
		chn->ctrl.force_trn_start = false;
		dmac->otc_irq_delay = transfer_size(*chn);
		do_dev_blkcopy(dmac, channel);
		break;
	default:
		panic("invalid channel");
		break;
	}

	// cleared on completion
	chn->ctrl.trn_start = false;
	
	if(dmac->regs.dmairq.bus_error) {
		dmac->regs.dmairq.master_irq = true;
	}

	if(dmac->regs.dmairq.master_irq_enable && DMACHN_ENABLED(dmac, channel) && !DMACHN_IRQ(dmac, channel)) {
		dmac->regs.dmairq.master_irq = true;
		dmac->regs.dmairq.chn_irq |= 1 << channel;
		// log_trace(stderr, "DMA IRQ raised");
		psx_irq_raise(dmac->sys->irq, PSX_IRQ_ID_DMA);
	}
}

void psx_dmac_run_transfers(struct psx_dmac* dmac) {
	enum dmachnidx_t channel;
	for(int priority = 7; priority >= 0; priority--) {
		for(channel = DMACHN_MDECIN; channel < DMACHN_NUM; channel++) {
			if(priority == DMACTL_CHNPRIORITY(dmac->regs.dmactl, channel)) {
				do_transfer(dmac, channel);
			}
		}
	}
}

