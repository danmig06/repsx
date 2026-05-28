#include <psx/cdrom.h>

#include <string.h>

#include "queue.h"
#include "cmd.h"
#include "../util.h"
#include "../log.h"
#include "../rdef/cdrom.h"

#define XA_MAX_OUTPUT_SAMPLES (((2016 * 2) * 7 * 2) / 6)
#define XA_MAX_OUTPUT_SIZE (XA_MAX_OUTPUT_SAMPLES * sizeof(int16_t))

void psx_cdr_init(struct psx_cdrom* cdr, struct psx_system* sys) {
	cdr->sys = sys;
	cdr->data_queue  = queue_create(PSX_CDROM_DATABUF_SIZE);
	cdr->resp_queue  = queue_create(PSX_CDROM_RESPBUF_SIZE);
	cdr->param_queue = queue_create(PSX_CDROM_PARMBUF_SIZE);
	cdr->out[0].buf = malloc(XA_MAX_OUTPUT_SIZE);
	cdr->out[1].buf = &cdr->out[0].buf[XA_MAX_OUTPUT_SAMPLES / 2];
	cdr->disc = NULL;
	psx_cdr_reset(cdr);
}

void psx_cdr_reset(struct psx_cdrom* cdr) {
	memset(&cdr->regs, 0, sizeof(cdr->regs));

	queue_clear(cdr->data_queue);
	queue_clear(cdr->resp_queue);
	queue_clear(cdr->param_queue);

	cdr->ack = (struct __psx_cdr_event){ .ev.id = PSX_SEV_ID_CDROM_RESP1, .active = false };
	cdr->comp = (struct __psx_cdr_event){ .ev.id = PSX_SEV_ID_CDROM_RESP2, .active = false };
	cdr->async_irq = (struct __psx_cdr_event){ .ev.id = PSX_SEV_ID_CDROM_IRQ, .active = false };
	cdr->drive_event.id = PSX_SEV_ID_CDROM_DRIVE;
	cdr->last_ack_timestamp = 0;

	memset(cdr->out[0].buf, 0, XA_MAX_OUTPUT_SIZE);
	cdr->state = 0x00;
	cdr->disc_mode = 0x00;
	cdr->vol_ll = cdr->vol_rr = 0x80;
	cdr->vol_lr = cdr->vol_rl = 0x00;
	cdr->regs.ctrl = CTRL_PARAM_EMPTY | CTRL_PARAM_READY;
	cdr->loc = 150;
	cdr->seek.is_pending = false;
	cdr->muted = false;
	cdr->shell_open = false;
}

static void cdr_push_param(struct psx_cdrom* cdr, uint8_t pb) {
	queue_push(cdr->param_queue, pb);
	cdr->regs.ctrl &= ~CTRL_PARAM_EMPTY;

	if(queue_full(cdr->param_queue)) {
		cdr->regs.ctrl &= ~CTRL_PARAM_READY;
	}
}

static uint8_t cdr_pop_response(struct psx_cdrom* cdr) {
	uint8_t rb = queue_pop(cdr->resp_queue);

	if(queue_empty(cdr->resp_queue)) {
		cdr->regs.ctrl &= ~CTRL_RESULT_READY;
	}
	return rb;
}

static uint8_t cdr_pop_data(struct psx_cdrom* cdr) {
	uint8_t rb = queue_pop(cdr->data_queue);

	if(queue_empty(cdr->data_queue)) {
		cdr->regs.ctrl &= ~CTRL_DATA_REQUEST;
	}
	return rb;
}

void cdr_bank0_write(struct psx_cdrom* cdr, uint32_t off, uint8_t val) {
	switch(off) {
	case 1:
		cdr->regs.command = val;
		cdr_run_cmd(cdr);
		break;
	case 2:
		log_debug("CDROM: push parameter (%02x)", val);
		cdr_push_param(cdr, val);
		break;
	case 3:
		// log_trace("CDROM: HCHPCTL write (%02x)", val);
		cdr->regs.hchp_ctrl = val;
		break;
	default:
		break;
	}
}

void cdr_bank1_write(struct psx_cdrom* cdr, uint32_t off, uint8_t val) {
	switch(off) {
	case 1:
		cdr->regs.wr_data = val;
		break;
	case 2:
		// log_debug("CDROM: IRQ mask set (%02x)", val);
		cdr->regs.irq_mask = val;
		break;
	case 3:
		// log_debug("CDROM: IRQ acknowledged (%02x)", val);
		cdr->regs.irq_status &= ~(val & (INT_FLAGS | INT_BFEMPT | INT_BFWRDY));
		cdr->last_ack_timestamp = cdr->sys->sched->clocks_elapsed;
		if(cdr->async_irq.active) {
			psx_sched_remove_ev(cdr->sys->sched, cdr->async_irq.ev.id);
			psx_sched_add_ev(cdr->sys->sched, &cdr->async_irq.ev);
		}
		if(cdr->regs.command != 0) {
			cdr_run_cmd(cdr);
		}

		if(val & BIT(6)) {
			queue_clear(cdr->param_queue);
			cdr->regs.ctrl |= CTRL_PARAM_EMPTY | CTRL_PARAM_READY;
		}
		break;
	default:
		break;
	}
}

void cdr_bank2_write(struct psx_cdrom* cdr, uint32_t off, uint8_t val) {
	switch(off) {
	case 1:
		log_debug("CDROM: CI write (0x%02x)", val);
		break;
	case 2:
		log_debug("CDROM: ATV0 write (0x%02x)", val);
		cdr->regs.atv0 = val;
		break;
	case 3:
		log_debug("CDROM: ATV1 write (0x%02x)", val);
		cdr->regs.atv1 = val;
		break;
	default:
		break;
	}
}

void cdr_bank3_write(struct psx_cdrom* cdr, uint32_t off, uint8_t val) {
	switch(off) {
	case 1:
		log_debug("CDROM: ATV2 write (0x%02x)", val);
		cdr->regs.atv2 = val;
		break;
	case 2:
		log_debug("CDROM: ATV3 write (0x%02x)", val);
		cdr->regs.atv3 = val;
		break;
	case 3:
		log_debug("CDROM: ADPCTL write (0x%02x)", val);
		if(val & ADPCTL_CHANGE) {
			cdr->vol_ll = cdr->regs.atv0;
			cdr->vol_lr = cdr->regs.atv1;
			cdr->vol_rr = cdr->regs.atv2;
			cdr->vol_rl = cdr->regs.atv3;
		}
		cdr->regs.adpctl = val & ~ADPCTL_CHANGE;
		break;
	default:
		break;
	}
}

psx_cdr_sample_t psx_cdr_pop_sample(struct psx_cdrom* cdr) {
	psx_cdr_sample_t s = { 0 };
	int32_t left = 0, right = 0;
	bool src_muted = false;

	if(cdr->out[0].read_off < cdr->out[0].write_off) {
		if(cdr->disc_mode & MODE_XA) {
			if(cdr->xa.coding_info & XA_CI_SM) {
				left = cdr->out[0].buf[cdr->out[0].read_off++];
				right = cdr->out[1].buf[cdr->out[1].read_off++];
			} else {
				left = right = cdr->out[0].buf[cdr->out[0].read_off++];
			}
			src_muted = (cdr->regs.adpctl & ADPCTL_XA_MUTE) != 0;
		} else {
			left = cdr->out[0].buf[cdr->out[0].read_off++];
			right = cdr->out[1].buf[cdr->out[1].read_off++];
		}

		if(!cdr->muted && !src_muted) {
			s.l = SAT(((left * cdr->vol_ll) >> 7) + ((right * cdr->vol_rl) >> 7), -0x8000, 0x7fff);
			s.r = SAT(((left * cdr->vol_lr) >> 7) + ((right * cdr->vol_rr) >> 7), -0x8000, 0x7fff);
		}
	}

	return s;
}

uint32_t psx_cdr_direct_out(struct psx_cdrom* cdr) {
	uint32_t word = queue_pop(cdr->data_queue) | (queue_pop(cdr->data_queue) << 8) |
		       (queue_pop(cdr->data_queue) << 16) | (queue_pop(cdr->data_queue) << 24);

	if(queue_empty(cdr->data_queue)) {
		cdr->regs.ctrl &= ~CTRL_DATA_REQUEST;
	}
	return word;
}

uint16_t psx_cdr_read16(struct psx_region* reg, uint32_t addr) {
	log_warn("CDROM: read16");
	return (psx_cdr_read8(reg, addr) << 8) | psx_cdr_read8(reg, addr + 1);
}

uint8_t psx_cdr_read8(struct psx_region* reg, uint32_t addr) {
	struct psx_cdrom* cdr = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	switch(register_offset) {
	case 0: {
		uint8_t ctrl = cdr->regs.ctrl;
		if(!(cdr->regs.hchp_ctrl & HCHP_BFRD)) {
			ctrl &= ~CTRL_DATA_REQUEST;
		}

		// log_debug("CDROM: control register read (%02x)", cdr->regs.ctrl);
		return ctrl;
	}
	case 1:
		log_debug("CDROM: response read (%02x)", queue_peek(cdr->resp_queue));
		return cdr_pop_response(cdr);
	case 2:
		// log_debug("CDROM: data read (%02x)", queue_peek(cdr->data_queue));
		cdr->regs.rd_data = cdr_pop_data(cdr);
		return cdr->regs.rd_data;
	case 3:
		if(CTRL_BANK_GET(cdr->regs.ctrl) & 1) {
			// log_debug("CDROM: IRQ status read (%02x)", cdr->regs.irq_status);
			return cdr->regs.irq_status | 0xe0;
		} else {
			// log_debug("CDROM: IRQ mask read (%02x)", AS_UINT8(cdr->regs.irq_mask));
			return cdr->regs.irq_mask;
		}
		break;
	default:
		break;
	}
	return 0;
}

void psx_cdr_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	struct psx_cdrom* cdr = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(register_offset == 0) {
		CTRL_BANK_SET(cdr->regs.ctrl, val);
		return;
	}

	switch(CTRL_BANK_GET(cdr->regs.ctrl)) {
	case 0:
		cdr_bank0_write(cdr, register_offset, val);
		break;
	case 1:
		cdr_bank1_write(cdr, register_offset, val);
		break;
	case 2:
		cdr_bank2_write(cdr, register_offset, val);
		break;
	case 3:
		cdr_bank3_write(cdr, register_offset, val);
		break;
	default:
		break;
	}
}

