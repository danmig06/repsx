#include <psx/dma.h>
#include <psx/irq.h>
#include <psx/mdec.h>

#include <string.h>

#include "util.h"
#include "rdef/dma.h"
#include "log.h"
#define CHCR_MASK 0x71770503
#define OTC_CHCR_MASK 0x50000002
#define DMACHN_IRQ(dmac, id) ((DICR_CHNFLAGS_GET(dmac->regs.dicr) >> (id)) & 1)
#define DMACHN_ENABLED(dmac, id) ((DICR_CHNMASK_GET(dmac->regs.dicr) >> (id)) & 1)
#define DICR_WRITE_MASK 0x00ff807f

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
	dmac->regs.dpcr = 0x07654321;
	DICR_CHNFLAGS_SET(dmac->regs.dicr, 1);
}

uint32_t psx_dmac_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t val = 0;
	uint32_t register_offset = addr - reg->start;

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));

	return val;
}

static void write_dicr(struct psx_dmac* dmac, uint32_t val) {
	uint8_t prev_flags = DICR_CHNFLAGS_GET(dmac->regs.dicr);
	uint8_t updated_flags = prev_flags & ~(val >> 24);
	val &= DICR_WRITE_MASK;
	dmac->regs.dicr = (dmac->regs.dicr & ~DICR_WRITE_MASK) | val;
	DICR_CHNFLAGS_SET(dmac->regs.dicr, updated_flags);
	uint8_t mask = DICR_CHNMASK_GET(dmac->regs.dicr);
	if((dmac->regs.dicr & DICR_IRQ_EN) && (updated_flags & mask) != 0) {
		dmac->regs.dicr |= DICR_IRQ;
	} else {
		dmac->regs.dicr &= ~DICR_IRQ;
	}
}

void psx_dmac_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	
	if(register_offset == 0x74) {
		write_dicr(reg->peripheral, val);
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
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));

	return val;
}

void psx_dmac_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	if(register_offset >= 0x74 && 0x78 > register_offset) {
		int shift = register_offset - 0x74;
		write_dicr(reg->peripheral, ((uint32_t)val) << (shift * 8));
	} else {
		uint8_t* regs = reg->peripheral;
		memcpy(&regs[register_offset], &val, sizeof(val));

		if(((register_offset >> 2) & 3) == 2) {
			psx_dmac_run_transfers(reg->peripheral);
		}
	}
}

uint8_t psx_dmac_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = reg->peripheral;
	return regs[register_offset];
}

void psx_dmac_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(register_offset >= 0x74 && 0x78 > register_offset) {
		int shift = register_offset - 0x74;
		write_dicr(reg->peripheral, ((uint32_t)val) << (shift * 8));
	} else {
		uint8_t* regs = reg->peripheral;
		regs[register_offset] = val;

		if(((register_offset >> 2) & 3) == 2) {
			psx_dmac_run_transfers(reg->peripheral);
		}
	}
}

uint32_t transfer_size(psx_dma_channel_t chn) {
	switch(CHCR_MODE_GET(chn.ctrl)) {
	case PSX_DMA_MODE_MANUAL:
		if(chn.bc.n_words == 0) {
			return 0x10000;
		}
		return chn.bc.n_words;
	case PSX_DMA_MODE_REQUEST:
		return chn.bc.block_size * chn.bc.n_blocks;
	default:
		return 0;
	}
}

bool is_triggered(psx_dma_channel_t* chn) {
	if(CHCR_MODE_GET(chn->ctrl) == PSX_DMA_MODE_MANUAL) {
		return (chn->ctrl & CHCR_START) && (chn->ctrl & CHCR_FORCE);
	}
	return (chn->ctrl & CHCR_START) != 0;
}

uint32_t fetch_word_dev(struct psx_dmac* dmac, struct copyvec* copy_state, enum dmachnidx_t channel) {
	uint32_t word = 0;
	switch(channel) {
	case DMACHN_MDECIN:
		break;
	case DMACHN_MDECOUT:
		return psx_mdec_direct_out(dmac->sys->mdec);
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
		psx_mdec_direct_in(dmac->sys->mdec, data);
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
	copy_state.increment = (chn->ctrl & CHCR_INC) ? -4 : 4;

	uint32_t src;
	if((chn->ctrl & CHCR_DIR) == PSX_DMA_DIR_TO_RAM) {
		while(copy_state.words_left > 0) {
			src = fetch_word_dev(dmac, &copy_state, channel);
			psx_mem_write32(dmac->sys->memory, copy_state.addr, src);

			copy_state.addr = (copy_state.addr + copy_state.increment) & 0x1fffff;
			copy_state.words_left--;
		}
	} else {
		while(copy_state.words_left > 0) {
			src = psx_mem_read32(dmac->sys->memory, copy_state.addr);
			write_word_dev(dmac, &copy_state, channel, src);

			copy_state.addr = (copy_state.addr + copy_state.increment) & 0x1fffff;
			copy_state.words_left--;
		}
	}

	if(CHCR_MODE_GET(chn->ctrl) != PSX_DMA_MODE_MANUAL) {
		chn->start_addr = copy_state.addr;
	}
}

void do_dev_linked_list(struct psx_dmac* dmac, enum dmachnidx_t channel) {
	if(channel != DMACHN_GPU) {
		panic("linked list mode is not implemented for devices other than GPU");
	}

	psx_dma_channel_t* chn = &dmac->regs.chn[channel];
	if((chn->ctrl & CHCR_DIR) != PSX_DMA_DIR_FROM_RAM) {
		panic("invalid linked list transfer");
	}

	uint32_t addr = chn->start_addr & 0xfffffc;
	uint32_t list_header;
	uint32_t item;
	uint8_t items_left;
	uint32_t limit = 65536;

	while(limit--) {
		list_header = psx_mem_read32(dmac->sys->memory, addr);

		items_left = list_header >> 24;

		while(items_left > 0) {
			addr = (addr + 4) & 0xfffffc;
			item = psx_mem_read32(dmac->sys->memory, addr);
			psx_mem_write32(dmac->sys->memory, 0x1f801810, item);
			items_left--;
		}

		if(list_header & 0x800000) {
			chn->start_addr = list_header & 0xffffff;
			break;
		}

		addr = list_header & 0xfffffc;
	}
}

void do_transfer(struct psx_dmac* dmac, enum dmachnidx_t channel) {
	psx_dma_channel_t* chn = &dmac->regs.chn[channel];
	if(channel == DMACHN_OTC) {
		// OTC is hardwired differently, it needs special handling
		// the increment bit is hardwired to 1 (negative increment)
		chn->ctrl |= CHCR_INC;
		// transfer mode is not factored in so we treat it as manual
		CHCR_MODE_SET(chn->ctrl, PSX_DMA_MODE_MANUAL);
		chn->ctrl &= ~CHCR_DIR;
		if(!(DPCR_EN_GET(dmac->regs.dpcr, DMACHN_OTC) && is_triggered(chn))) {
			chn->ctrl &= OTC_CHCR_MASK;
			return;
		}
		chn->ctrl &= OTC_CHCR_MASK;
	} else if(!(DPCR_EN_GET(dmac->regs.dpcr, channel) && is_triggered(chn))) {
		return;
	}

	switch(channel) {
	case DMACHN_MDECIN:
		// log_error("DMA: MDECIN transfer (%d blocks, 0x%x bytes)", chn->bc.n_blocks, chn->bc.block_size);
		chn->ctrl &= ~CHCR_FORCE;
		do_dev_blkcopy(dmac, channel);
		break;
	case DMACHN_MDECOUT:
		// log_error("DMA: MDECOUT transfer (%d blocks, 0x%x bytes)", chn->bc.n_blocks, chn->bc.block_size);
		chn->ctrl &= ~CHCR_FORCE;
		do_dev_blkcopy(dmac, channel);
		break;
	case DMACHN_GPU:
		if(CHCR_MODE_GET(chn->ctrl) != PSX_DMA_MODE_LINKEDLIST) {
			chn->ctrl &= ~CHCR_FORCE;
			do_dev_blkcopy(dmac, channel);
		} else {
			do_dev_linked_list(dmac, channel);
		}
		break;
	case DMACHN_CDROM:
		if((chn->ctrl & CHCR_DIR) == PSX_DMA_DIR_TO_RAM) {
			chn->ctrl &= ~CHCR_FORCE;
			do_dev_blkcopy(dmac, channel);
		} else {
			log_error("Unhandled DMACHN_CDROM transfer");
		}
		break;
	case DMACHN_SPU:
		// many games won't start if they get no feedback
		log_error("Unhandled DMACHN_SPU (write=%d) transfer (IRQ triggered)", (chn->ctrl & CHCR_DIR) != 0);
		chn->ctrl &= ~CHCR_FORCE;
		// CAUTION: Dead or Alive uses sound RAM to store pointers and
		// other sensitive stuff, and will crash after some amount of time
		// during a battle
		chn->start_addr += transfer_size(*chn) * ((chn->ctrl & CHCR_INC) ? -1 : 1);
		break;
	case DMACHN_PIO:
		log_error("Unhandled DMACHN_PIO transfer");
		break;
	case DMACHN_OTC:
		chn->ctrl &= ~CHCR_FORCE;
		do_dev_blkcopy(dmac, channel);
		break;
	default:
		panic("invalid channel");
		break;
	}

	// cleared on completion
	chn->ctrl &= ~CHCR_START;
	
	if(dmac->regs.dicr & DICR_BUSERROR) {
		dmac->regs.dicr |= DICR_IRQ;
	}

	if((dmac->regs.dicr & DICR_IRQ_EN) && DMACHN_ENABLED(dmac, channel) && !DMACHN_IRQ(dmac, channel)) {
		dmac->regs.dicr |= DICR_IRQ;
		int flags = DICR_CHNFLAGS_GET(dmac->regs.dicr);
		flags |= 1 << channel;
		DICR_CHNFLAGS_SET(dmac->regs.dicr, flags);
		// log_error("DMA IRQ raised");
		psx_irq_raise(dmac->sys->irq, PSX_IRQ_ID_DMA);
	}
}

void psx_dmac_run_transfers(struct psx_dmac* dmac) {
	enum dmachnidx_t channel;
	for(int priority = 7; priority >= 0; priority--) {
		for(channel = DMACHN_MDECIN; channel < DMACHN_NUM; channel++) {
			if(priority == DPCR_PR_GET(dmac->regs.dpcr, channel)) {
				do_transfer(dmac, channel);
			}
		}
	}
}

