#include <psx/sio.h>
#include <psx/sched.h>
#include <psx/irq.h>

#include "input/common.h"
#include "log.h"
#include "rdef/sio.h"

#include <string.h>
#include <stdbool.h>

#define SIO0_PORT(sio) ((sio->regs.ctrl & CTRL_SIO0_PORT) != 0)

void sio_response_event(struct psx_sched* sched, struct psx_sev* self);
struct psx_sev byte_received_irq = {
	.id = PSX_SEV_ID_SIO_RESPONSE,
	.eta = 1088,
	.trigger = sio_response_event
};

void sio_response_event(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_sio* sio = sched->sys->sio;
	psx_irq_raise(sched->sys->irq, PSX_IRQ_ID_BRI);

	sio->irq_scheduled = false;
	sio->regs.stat |= (STAT_TXREADY | STAT_TXIDLE | STAT_RXREADY | STAT_IRQ);
	sio->regs.stat &= ~STAT_DSR;
	psx_sched_remove_ev(sched, self->id);
}

void psx_sio_init(struct psx_sio* sio, struct psx_system* sys) {
	sio->sys = sys;
	psx_sio_reset(sio);
}

void psx_sio_reset(struct psx_sio* sio) {
	memset(&sio->regs, 0, sizeof(sio->regs));
	sio->selected_dev = NULL;
	sio->irq_scheduled = false;
	sio->regs.stat = (STAT_TXREADY | STAT_TXIDLE);
}

static bool do_tx_select(struct psx_sio* sio, uint8_t device_id) {
	struct sio_dev* dev = NULL;
	if(device_id >= PSX_SIO_DEV_MEMCARD) {
		dev = sio->dev.bu[SIO0_PORT(sio)];
		if(!dev) {
			sio->regs.stat &= ~STAT_DSR;
			sio->regs.stat |= STAT_RXREADY;
			return false;
		}
	} else {
		dev = sio->dev.in[SIO0_PORT(sio)];
		if(!dev || dev->id != device_id) {
			// the requested device is not connected, the system will try to read the device ID
			// anyway and the response should just be HiZ
			sio->regs.stat &= ~STAT_DSR;
			sio->regs.stat |= STAT_RXREADY;
			return false;
		}
	}

	log_trace("SIO0: selected device 0x%02x", device_id);
	sio->regs.stat &= ~STAT_RXREADY;
	sio->selected_dev = dev;

	if(!sio->irq_scheduled) {
		sio->irq_scheduled = true;
		// TX and RX lines will be busy until HiZ arrives
		sio->regs.stat &= ~(STAT_TXREADY | STAT_TXIDLE);
		psx_sched_add_ev(sio->sys->sched, &byte_received_irq);
	} else {
		log_error("SIO0: cannot schedule selection IRQ");
	}
	return true;
}

static void unselect_device(struct psx_sio* sio) {
	log_trace("SIO0: unselected device 0x%02x", (sio->selected_dev) ? sio->selected_dev->id : 0x00);
	if(sio->dev.in[0]) {
		sio->dev.in[0]->reset(sio->dev.in[0]);
	}
	if(sio->dev.in[1]) {
		sio->dev.in[1]->reset(sio->dev.in[1]);
	}
	if(sio->dev.bu[0]) {
		sio->dev.bu[0]->reset(sio->dev.bu[0]);
	}
	if(sio->dev.bu[1]) {
		sio->dev.bu[1]->reset(sio->dev.bu[1]);
	}
	sio->regs.stat |= (STAT_TXREADY | STAT_TXIDLE);
	if(sio->irq_scheduled) {
		sio->irq_scheduled = false;
		psx_sched_remove_ev(sio->sys->sched, byte_received_irq.id);
	}

	sio->selected_dev = NULL;
}

static void do_transmission(struct psx_sio* sio, uint8_t val) {
	if(!sio->selected_dev && (sio->regs.ctrl & CTRL_DTR)) {
		if(!do_tx_select(sio, val)) {
			// no device selected, the system will just read from floating pins
			return;
		}
	}

	struct sio_dev* dev = sio->selected_dev;
	if(dev->send(dev, val)) {
		sio->regs.stat |= STAT_DSR;
		// if the device acknowledged the transmission then 
		// the TX line will be busy until the response arrives
		sio->regs.stat &= ~STAT_TXIDLE;
		// last byte gets no interrupt
		if(!sio->irq_scheduled && !dev->tx_finished(dev)) {
			sio->irq_scheduled = true;
			psx_sched_add_ev(sio->sys->sched, &byte_received_irq);
		}
	} else {
		sio->regs.stat &= ~STAT_DSR;
	}
}

static uint8_t rx_update(struct psx_sio* sio) {
	struct sio_dev* dev = sio->selected_dev;
	if(dev) {
		log_trace("SIO0: device read %s", (sio->regs.stat & STAT_RXREADY) ? "(data ready)" : "(not ready)");
		sio->regs.stat |= STAT_RXREADY;
		return dev->recv(dev);
	}

	sio->regs.stat &= ~STAT_RXREADY;
	return 0xff;
}

uint32_t psx_sio_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_sio* sio = reg->peripheral;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 read at <%s+0x%x>", reg->name, register_offset);
		return 0xffffffff;
	}

	uint32_t val = 0xffffffff;
	if(!reg->peripheral) return val;
	if(register_offset < 4) {
		sio->regs.rx_data = rx_update(reg->peripheral);	
	}

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));
	if(register_offset == 4) {
		sio->regs.stat &= ~STAT_DSR;
	}

	return val;
}

static void handle_write(struct psx_sio* sio, uint32_t off) {
	if(sio->regs.ctrl & CTRL_ACK) {
		sio->regs.stat &= ~(STAT_IRQ | STAT_RX_PE | STAT_RX_BSB);
		sio->regs.ctrl &= ~CTRL_ACK;
	}

	if(sio->regs.ctrl & CTRL_RESET) {
		// log_error("SIO0: reset");
		// resets "most of the registers", which?
		unselect_device(sio);
		sio->regs.ctrl &= ~CTRL_RESET;
	}

	if(!(sio->regs.ctrl & CTRL_DTR) && sio->selected_dev) {
		unselect_device(sio);
	}
}

void psx_sio_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 write (0x%08x) at <%s+0x%x>", val, reg->name, register_offset);
		return;
	}
	struct psx_sio* sio = reg->peripheral;

	if(register_offset < 4) {
		do_transmission(sio, val & 0xff);
	} else {
		uint8_t* regs = reg->peripheral;
		memcpy(&regs[register_offset], &val, sizeof(val));
		handle_write(reg->peripheral, register_offset);
	}
}

uint16_t psx_sio_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_sio* sio = reg->peripheral;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 read at <%s+0x%x>", reg->name, register_offset);
		return 0xffff;
	}

	uint16_t val = 0xffff;
	if(!reg->peripheral) return val;
	if(register_offset < 4) {
		sio->regs.rx_data = rx_update(reg->peripheral);	
	}

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));
	if(register_offset == 4) {
		sio->regs.stat &= ~STAT_DSR;
	}

	return val;
}

void psx_sio_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 write (0x%04x) at <%s+0x%x>", val, reg->name, register_offset);
		return;
	}
	struct psx_sio* sio = reg->peripheral;

	if(register_offset < 4) {
		do_transmission(sio, val & 0xff);
	} else {
		uint8_t* regs = reg->peripheral;
		memcpy(&regs[register_offset], &val, sizeof(val));
		handle_write(reg->peripheral, register_offset);
	}
}

uint8_t psx_sio_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_sio* sio = reg->peripheral;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 read at <%s+0x%x>", reg->name, register_offset);
		return 0xff;
	}

	uint8_t val = 0xff;
	if(register_offset < 4) {
		sio->regs.rx_data = rx_update(reg->peripheral);	
	}

	uint8_t* regs = reg->peripheral;
	val = regs[register_offset];
	if(register_offset == 4) {
		sio->regs.stat &= ~STAT_DSR;
	}

	return val;
}

void psx_sio_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 write (0x%02x) at <%s+0x%x>", val, reg->name, register_offset);
		return;
	}
	struct psx_sio* sio = reg->peripheral;

	if(register_offset < 4) {
		do_transmission(sio, val);
	} else {
		uint8_t* regs = reg->peripheral;
		regs[register_offset] = val;
		handle_write(reg->peripheral, register_offset);
	}
}

