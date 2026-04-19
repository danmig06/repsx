#include <psx/dma.h>
#include <psx/irq.h>
#include <psx/mdec.h>
#include <psx/spu.h>
#include <psx/gpu.h>
#include <psx/cdrom.h>
#include <psx/sched.h>

#include <string.h>

#include "util.h"
#include "rdef/dma.h"
#include "log.h"
#define CHCR_MASK 0x71770503
#define OTC_CHCR_MASK 0x51000002
#define DMACHN_IRQ(dmac, id) ((DICR_CHNFLAGS_GET(dmac->regs.dicr) >> (id)) & 1)
#define DMACHN_ENABLED(dmac, id) ((DICR_CHNMASK_GET(dmac->regs.dicr) >> (id)) & 1)
#define DICR_WRITE_MASK 0x00ff807f

#define READ_WORD(m, a) (*(uint32_t*)&((m)[(a)]))
#define WRITE_WORD(m, a, v) *(uint32_t*)&((m)[(a)]) = (v);

#define LL_HEADER_SETUP_DELAY 8
#define LL_PACKET_SETUP_DELAY 5

// TODO: instant DMA is kept for comparison's sake and in case issues arise, remove this in a fiew commits
// #define INSTANT_DMA

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

#ifndef INSTANT_DMA

static void dma_complete_evcb(struct psx_sched* sched, struct psx_sev* self);

static struct psx_sev dma_comp_ev[7] = {
	{ .id = PSX_SEV_ID_DMAEND + 0x00, .trigger = dma_complete_evcb },
	{ .id = PSX_SEV_ID_DMAEND + 0x10, .trigger = dma_complete_evcb },
	{ .id = PSX_SEV_ID_DMAEND + 0x20, .trigger = dma_complete_evcb },
	{ .id = PSX_SEV_ID_DMAEND + 0x30, .trigger = dma_complete_evcb },
	{ .id = PSX_SEV_ID_DMAEND + 0x40, .trigger = dma_complete_evcb },
	{ .id = PSX_SEV_ID_DMAEND + 0x50, .trigger = dma_complete_evcb },
	{ .id = PSX_SEV_ID_DMAEND + 0x60, .trigger = dma_complete_evcb }
};

static void dma_complete_evcb(struct psx_sched* sched, struct psx_sev* self) {
	enum dmachnidx_t channel = self->id >> 4;
	struct psx_dmac* dmac = sched->sys->dmac;
	psx_dma_channel_t* chn = &dmac->regs.chn[channel];
	if(!(chn->ctrl & CHCR_START) && dmac->chnstate[channel].busy) {
		log_error("DMA: channel %d error", channel);
	}

	dmac->chnstate[channel].busy = false;
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
		// log_error("DMA%d IRQ raised", channel);
		psx_irq_raise(dmac->sys->irq, PSX_IRQ_ID_DMA);
	}
	psx_sched_remove_ev(sched, self->id);
}

#endif

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
	if((dmac->regs.dicr & DICR_BUSERROR) || ((dmac->regs.dicr & DICR_IRQ_EN) && (updated_flags & mask) != 0)) {
		if(!(dmac->regs.dicr & DICR_IRQ)) {
			dmac->regs.dicr |= DICR_IRQ;
			// log_error("DMA: DICR write IRQ triggered");
			psx_irq_raise(dmac->sys->irq, PSX_IRQ_ID_DMA);
		}
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
			struct psx_dmac* dmac = reg->peripheral;
#ifndef INSTANT_DMA
			if(dmac->chnstate[register_offset >> 4].busy) {
				// log_error("DMA: transfer canceled");
				psx_sched_remove_ev(dmac->sys->sched, dma_comp_ev[register_offset >> 4].id);
			}
#endif
			dmac->chnstate[register_offset >> 4].busy = false;
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

static uint32_t transfer_size(psx_dma_channel_t chn) {
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

static bool is_triggered(psx_dma_channel_t* chn) {
	if(CHCR_MODE_GET(chn->ctrl) == PSX_DMA_MODE_MANUAL) {
		return (chn->ctrl & CHCR_START) && (chn->ctrl & CHCR_FORCE);
	}
	return (chn->ctrl & CHCR_START) != 0;
}

static uint32_t fetch_word_dev(struct psx_system* sys, struct copyvec* copy_state, enum dmachnidx_t channel) {
	switch(channel) {
	case DMACHN_MDECIN:
		break;
	case DMACHN_MDECOUT:
		return psx_mdec_direct_out(sys->mdec);
	case DMACHN_GPU:
		return psx_gpu_direct_out(sys->gpu);
	case DMACHN_CDROM:
		return psx_cdr_direct_out(sys->cdrom);
	case DMACHN_SPU:
		return psx_spu_direct_out(sys->spu);
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

static void write_word_dev(struct psx_system* sys, enum dmachnidx_t channel, uint32_t data) {
	switch(channel) {
	case DMACHN_MDECIN:
		psx_mdec_direct_in(sys->mdec, data);
		break;
	case DMACHN_MDECOUT:
		break;
	case DMACHN_GPU:
		psx_gpu_direct_in(sys->gpu, data);
		break;
	case DMACHN_CDROM:
		break;
	case DMACHN_SPU:
		psx_spu_direct_in(sys->spu, data);
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

static void do_dev_blkcopy(struct psx_dmac* dmac, enum dmachnidx_t channel) {
	psx_dma_channel_t* chn = &dmac->regs.chn[channel];

	uint32_t start_addr = chn->start_addr & 0x1ffffc;

	struct copyvec copy_state;
	copy_state.words_left = transfer_size(*chn);
	copy_state.addr = start_addr;
	copy_state.increment = (chn->ctrl & CHCR_INC) ? -4 : 4;

	uint32_t src;
	const uint8_t* mem = dmac->sys->memory->phys;
	if((chn->ctrl & CHCR_DIR) == PSX_DMA_DIR_TO_RAM) {
		while(copy_state.words_left > 0) {
			src = fetch_word_dev(dmac->sys, &copy_state, channel);
			WRITE_WORD(mem, copy_state.addr, src);

			copy_state.addr = (copy_state.addr + copy_state.increment) & 0x1fffff;
			copy_state.words_left--;
		}
	} else {
		while(copy_state.words_left > 0) {
			src = READ_WORD(mem, copy_state.addr);
			write_word_dev(dmac->sys, channel, src);

			copy_state.addr = (copy_state.addr + copy_state.increment) & 0x1fffff;
			copy_state.words_left--;
		}
	}

	if(CHCR_MODE_GET(chn->ctrl) != PSX_DMA_MODE_MANUAL) {
		chn->start_addr = copy_state.addr;
	}
}

static void do_dev_linked_list(struct psx_dmac* dmac, enum dmachnidx_t channel, uint32_t* current_delay) {
	psx_dma_channel_t* chn = &dmac->regs.chn[channel];

	if(chn->start_addr & 0x800000) {
		log_warn("DMA: GPU received empty linked list");
		return;
	}

	const uint8_t* mem = dmac->sys->memory->phys;
	struct psx_gpu* gpu = dmac->sys->gpu;
	uint32_t addr = chn->start_addr & 0x1ffffc;
	uint32_t list_header;
	uint8_t items_left;
	uint32_t limit = 65536;

	while(limit--) {
		list_header = READ_WORD(mem, addr);

		items_left = list_header >> 24;
		*current_delay += (items_left + ((items_left + 15) / 16)) + LL_HEADER_SETUP_DELAY;
		if(items_left > 0) {
			*current_delay += LL_PACKET_SETUP_DELAY;
		}

		while(items_left > 0) {
			addr = (addr + 4) & 0x1ffffc;
			psx_gpu_direct_in(gpu, READ_WORD(mem, addr));
			items_left--;
		}

		if(list_header & 0x800000) {
			chn->start_addr = list_header & 0xffffff;
			break;
		}

		addr = list_header & 0x1ffffc;
	}
}

static void dma_do_transfer(struct psx_dmac* dmac, enum dmachnidx_t channel) {
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

	uint32_t delay = 0;
#ifndef INSTANT_DMA
	uint32_t word_count = transfer_size(*chn);
#endif
	switch(channel) {
	case DMACHN_MDECIN:
		chn->ctrl &= ~CHCR_FORCE;
		do_dev_blkcopy(dmac, channel);
		break;
	case DMACHN_MDECOUT:
		chn->ctrl &= ~CHCR_FORCE;
		do_dev_blkcopy(dmac, channel);
		break;
	case DMACHN_GPU:
		if(CHCR_MODE_GET(chn->ctrl) != PSX_DMA_MODE_LINKEDLIST) {
			chn->ctrl &= ~CHCR_FORCE;
			do_dev_blkcopy(dmac, channel);
		} else {
			do_dev_linked_list(dmac, channel, &delay);
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
		chn->ctrl &= ~CHCR_FORCE;
		do_dev_blkcopy(dmac, channel);
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

#ifdef INSTANT_DMA
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
#else
	delay += word_count + ((word_count + 15) / 16);
	dma_comp_ev[channel].eta = delay;
	dmac->chnstate[channel].busy = true;
	psx_sched_add_ev(dmac->sys->sched, &dma_comp_ev[channel]);
#endif
}

void psx_dmac_run_transfers(struct psx_dmac* dmac) {
	for(int priority = 0; priority <= 7; priority++) {
		for(int channel = DMACHN_OTC; channel >= DMACHN_MDECIN; channel--) {
			if(!dmac->chnstate[channel].busy && priority == DPCR_PR_GET(dmac->regs.dpcr, channel)) {
				dma_do_transfer(dmac, channel);
			}
		}
	}

#ifdef INSTANT_DMA
	for(int i = 0; i < 7; i++) {
		dmac->chnstate[i].busy = false;
	}
#endif
}

