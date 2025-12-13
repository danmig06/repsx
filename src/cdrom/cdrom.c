#include <psx/cdrom.h>

#include <string.h>

#include "queue.h"
#include "cmd.h"
#include "../util.h"
#include "../log.h"

void psx_cdr_init(struct psx_cdrom* cdr) {
	psx_cdr_reset(cdr);
}

void psx_cdr_reset(struct psx_cdrom* cdr) {
	memset(&cdr->regs, 0, sizeof(cdr->regs));
	AS_UINT8(cdr->state) = 0;
	cdr->data_queue  = queue_create(PSX_CDROM_DATABUF_SIZE);
	cdr->resp_queue  = queue_create(PSX_CDROM_RESPBUF_SIZE);
	cdr->param_queue = queue_create(PSX_CDROM_PARMBUF_SIZE);
	cdr->regs.ctrl.param_empty = true;
	cdr->regs.ctrl.param_wr_ready = true;
	cdr->loc = 0;
}

static void cdr_push_param(struct psx_cdrom* cdr, uint8_t pb) {
	queue_push(cdr->param_queue, pb);
	cdr->regs.ctrl.param_empty = false;

	if(queue_full(cdr->param_queue)) {
		cdr->regs.ctrl.param_wr_ready = false;
	}
}

static uint8_t cdr_pop_response(struct psx_cdrom* cdr) {
	uint8_t rb = queue_pop(cdr->resp_queue);

	if(queue_empty(cdr->resp_queue)) {
		cdr->regs.ctrl.result_ready = false;
	}
	return rb;
}

static uint8_t cdr_pop_data(struct psx_cdrom* cdr) {
	uint8_t rb = queue_pop(cdr->data_queue);

	if(queue_empty(cdr->data_queue)) {
		cdr->regs.ctrl.data_request = false;
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
		// log_trace("CDROM: HPCHCTL write (%02x)", val);
		AS_UINT8(cdr->regs.hphc_ctrl) = val;
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
		// printf("CDROM: IRQ mask set (%02x)", val);
		AS_UINT8(cdr->regs.irq_mask) = val;
		break;
	case 3:
		/*
		struct {
			uint8_t irqsts_flags: 5;
			bool xa_buf_clear: 1;
			bool param_clear: 1;
			bool decoder_reset: 1;
		} value;
		AS_UINT8(value) = val;
		*/
		// printf("CDROM: IRQ acknowledged (%02x)", val);
		AS_UINT8(cdr->regs.irq_status) &= ~(val & 0x1f);
		break;
	default:
		break;
	}
}

uint16_t psx_cdr_read16(struct psx_region* reg, uint32_t addr) {
	log_warn("CDROM: read16");
	return (psx_cdr_read8(reg, addr) << 8) | psx_cdr_read8(reg, addr + 1);
}

uint8_t psx_cdr_read8(struct psx_region* reg, uint32_t addr) {
	struct psx_cdrom* cdr = reg->peripheral;
	uint32_t register_offset = addr - reg->start;
	switch(register_offset) {
	case 0:
		// log_trace("CDROM: control register read (%02x)", AS_UINT8(cdr->regs.ctrl));
		return AS_UINT8(cdr->regs.ctrl);
	case 1:
		log_debug("CDROM: response read (%02x)", queue_peek(cdr->resp_queue));
		return cdr_pop_response(cdr);
	case 2:
		// log_trace("CDROM: data read (%02x)", queue_peek(cdr->data_queue));
		cdr->regs.rd_data = cdr_pop_data(cdr);
		return cdr->regs.rd_data;
	case 3:
		if(cdr->regs.ctrl.address & 1) {
			log_trace("CDROM: IRQ status read (%02x)", AS_UINT8(cdr->regs.irq_status));
			return AS_UINT8(cdr->regs.irq_status) | 0xe0;
		} else {
			// printf("CDROM: IRQ mask read (%02x)", AS_UINT8(cdr->regs.irq_mask));
			return AS_UINT8(cdr->regs.irq_mask);
		}
		break;
	default:
		break;
	}
	return 0;
}

void psx_cdr_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	struct psx_cdrom* cdr = reg->peripheral;
	uint32_t register_offset = addr - reg->start;
	if(register_offset == 0) {
		cdr->regs.ctrl.address = val & 0x3;
		return;
	}

	switch(cdr->regs.ctrl.address) {
	case 0:
		cdr_bank0_write(cdr, register_offset, val);
		break;
	case 1:
		cdr_bank1_write(cdr, register_offset, val);
		break;
	case 2:
		log_error("CDROM: bank2 write (0x%02x) <%s+0x%x>", val, reg->name, register_offset);
		break;
	case 3:
		log_error("CDROM: bank3 write (0x%02x) <%s+0x%x>", val, reg->name, register_offset);
		break;
	default:
		break;
	}
}

