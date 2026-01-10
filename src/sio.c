#include <psx/sio.h>
#include <psx/sched.h>
#include <psx/irq.h>

#include "backupunit.h"
#include "pad.h"
#include "log.h"

#include <string.h>
#include <stdbool.h>

#define SIO0_PORT(sio) sio->regs.ctrl.sio0_port_select

void sio_response_event(struct psx_sched* sched, struct psx_sev* self);
struct psx_sev byte_received_irq = {
	.id = PSX_SEV_ID_SIO_RESPONSE,
	.eta = 1088,
	.trigger = sio_response_event
};

void sio_response_event(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_sio* sio = sched->sys->sio;
	sio->regs.stat.irq = true;
	psx_irq_raise(sched->sys->irq, PSX_IRQ_ID_BRI);

	sio->irq_scheduled = false;
	sio->regs.stat.tx_not_full = true;
	sio->regs.stat.tx_idle = true;
	sio->regs.stat.rx_not_empty = true;
	sio->regs.stat.dsr_input_level = false;
	psx_sched_remove_ev(sched, self->id);
}

void psx_sio_init(struct psx_sio* sio) {
	psx_sio_reset(sio);
}

void psx_sio_reset(struct psx_sio* sio) {
	memset(&sio->regs, 0, sizeof(sio->regs));
	sio->tx_address = PSX_SIO_DEV_NONE;
	sio->rx_high_z = false;
	sio->irq_scheduled = false;
	sio->regs.stat.tx_not_full = true;
	sio->regs.stat.tx_idle = true;
}

static bool do_tx_select(struct psx_sio* sio, uint8_t device_id) {
	sio->regs.stat.rx_not_empty = true;
	if(device_id == PSX_SIO_DEV_CONTROLLER) {
		struct psx_pad* pad = sio->dev.pad[SIO0_PORT(sio)];
		if(!pad) {
			// pad[port] is not connected, the system will try to read the device ID 
			// anyway and the response should just be HiZ
			sio->regs.stat.dsr_input_level = false;
			return false;
		}
		log_trace("SIO0: selected device 0x%02x", device_id);
		sio->tx_address = device_id;
		sio->regs.stat.rx_not_empty = false;
		
		if(!sio->irq_scheduled) {
			sio->irq_scheduled = true;
			// TX and RX lines will be busy until HiZ arrives
			sio->regs.stat.tx_not_full = false;
			sio->regs.stat.tx_idle = false;
			psx_sched_add_ev(sio->sys->sched, &byte_received_irq);
		} else {
			log_error("SIO0: cannot schedule selection IRQ");
		}
		return true;
	} else if(device_id >= PSX_SIO_DEV_MEMCARD) {
		struct psx_bu* bu = sio->dev.bu[SIO0_PORT(sio)];
		if(!bu) {
			// bu[port] is not connected, the system will try to read the device ID 
			// anyway and the response should just be HiZ
			sio->regs.stat.dsr_input_level = false;
			return false;
		}
		log_trace("SIO0: selected device 0x%02x", device_id);
		sio->tx_address = device_id;
		sio->regs.stat.rx_not_empty = false;
		
		if(!sio->irq_scheduled) {
			sio->irq_scheduled = true;
			// TX and RX lines will be busy until HiZ arrives
			sio->regs.stat.tx_not_full = false;
			sio->regs.stat.tx_idle = false;
			sio->regs.stat.rx_not_empty = false;
			psx_sched_add_ev(sio->sys->sched, &byte_received_irq);
		} else {
			log_error("SIO0: cannot schedule selection IRQ");
		}
		return true;
	} else {
		log_error("SIO0: unsupported device selected");
		sio->regs.stat.dsr_input_level = false;
	}
	return false;
}

static void unselect_device(struct psx_sio* sio) {
	log_trace("SIO0: unselected device 0x%02x", sio->tx_address);
	if(sio->dev.pad[0]) {
		sio->dev.pad[0]->reset(sio->dev.pad[0]);
	}
	if(sio->dev.pad[1]) {
		sio->dev.pad[1]->reset(sio->dev.pad[1]);
	}
	if(sio->dev.bu[0]) {
		sio->dev.bu[0]->reset(sio->dev.bu[0]);
	}
	if(sio->dev.bu[1]) {
		sio->dev.bu[1]->reset(sio->dev.bu[1]);
	}
	sio->regs.stat.tx_not_full = true;
	sio->regs.stat.tx_idle = true;
	if(sio->irq_scheduled) {
		sio->irq_scheduled = false;
		psx_sched_remove_ev(sio->sys->sched, byte_received_irq.id);
	}

	sio->tx_address = PSX_SIO_DEV_NONE;
}

static void do_transmission(struct psx_sio* sio, uint8_t val) {
	if(sio->tx_address == PSX_SIO_DEV_NONE && sio->regs.ctrl.dtr_out_level) {
		if(!do_tx_select(sio, val)) {
			// no device selected, the system will just read from floating pins
			return;
		}
	}

	/* TODO: generalize this interface, could live inside an internal header
	 *	struct sio_dev {
	 *		bool (*send)(struct sio_dev*, uint8_t);
	 *		uint8_t (*recv)(struct sio_dev*);
	 *		void (*reset)(struct sio_dev*);
	 *		bool (*tx_finished)(struct sio_dev*);
	 *	};
	 * then just bind the current device and call its functions
	 * directly (by taking &xxx->dev).
	 */
	struct psx_pad* pad;
	struct psx_bu* bu;
	switch(sio->tx_address) {
	case PSX_SIO_DEV_CONTROLLER:
		pad = sio->dev.pad[SIO0_PORT(sio)];
		sio->regs.stat.dsr_input_level = pad->send(pad, val);
		if(sio->regs.stat.dsr_input_level) {
			// if the device acknowledged the transmission then 
			// the TX line will be busy until the response arrives
			sio->regs.stat.tx_idle = false;
			// last byte gets no interrupt
			if(!sio->irq_scheduled && !pad->tx_finished(pad)) {
				sio->irq_scheduled = true;
				psx_sched_add_ev(sio->sys->sched, &byte_received_irq);
			}
		}
		break;
	case PSX_SIO_DEV_MEMCARD:
		bu = sio->dev.bu[SIO0_PORT(sio)];
		sio->regs.stat.dsr_input_level = bu->send(bu, val);
		log_warn("SIO0: bu sent 0x%02x", val);
		if(sio->regs.stat.dsr_input_level) {
			// if the device acknowledged the transmission then 
			// the TX line will be busy until the response arrives
			sio->regs.stat.tx_idle = false;
			// last byte gets no interrupt
			if(!sio->irq_scheduled && !bu->tx_finished(bu)) {
				log_warn("SIO0: bu IRQ scheduled");
				sio->irq_scheduled = true;
				psx_sched_add_ev(sio->sys->sched, &byte_received_irq);
			} else {
				log_warn("SIO0: bu transmission ended");
			}
		}
		break;
	case PSX_SIO_DEV_NONE:
	default:
		log_error("SIO0: write to no device");
		break;
	}
}

static uint8_t rx_update(struct psx_sio* sio) {
	uint8_t val = 0xff;

	struct psx_pad* pad;
	struct psx_bu* bu;
	switch(sio->tx_address) {
	case PSX_SIO_DEV_CONTROLLER:
		pad = sio->dev.pad[SIO0_PORT(sio)];
		log_trace("SIO0: pad read %s", (sio->regs.stat.rx_not_empty) ? "(data ready)" : "(not ready)");
		val = pad->recv(pad);
		sio->regs.stat.rx_not_empty = true; // !pad->tx_finished(pad);
		break;
	case PSX_SIO_DEV_MEMCARD:
		bu = sio->dev.bu[SIO0_PORT(sio)];
		log_trace("SIO0: bu read %s", (sio->regs.stat.rx_not_empty) ? "(data ready)" : "(not ready)");
		val = bu->recv(bu);
		log_warn("SIO0: bu got 0x%02x", val);
		sio->regs.stat.rx_not_empty = true; // !bu->tx_finished(bu);
		break;
	case PSX_SIO_DEV_NONE:
	default:
		// log_error("SIO0: read from no device");
		break;
	}

	return val;
}

uint32_t psx_sio_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	struct psx_sio* sio = reg->peripheral;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 read at <%s+0x%x>", reg->name, addr);
		return 0xffffffff;
	}

	uint32_t val = 0xffffffff;
	if(!reg->peripheral) return val;
	if(register_offset < 4) {
		sio->regs.rx_data = rx_update(reg->peripheral);	
		sio->regs.stat.rx_not_empty = false;
	}

	uint8_t* regs = reg->peripheral;
	memcpy(&val, &regs[register_offset], sizeof(val));
	if(register_offset == 4) {
		sio->regs.stat.dsr_input_level = false;
	}

	return val;
}

static void handle_write(struct psx_sio* sio, uint32_t off) {
	if(sio->regs.ctrl.acknowledge) {
		sio->regs.stat.rx_pe = false;
		sio->regs.stat.rx1_bsb = false;
		sio->regs.stat.irq = false;
		sio->regs.ctrl.acknowledge = false;
	}

	if(sio->regs.ctrl.reset) {
		// log_error("SIO0: reset");
		// resets "most of the registers", which?
		unselect_device(sio);
		sio->regs.ctrl.reset = false;
	}

	if(!sio->regs.ctrl.dtr_out_level && sio->tx_address != PSX_SIO_DEV_NONE) {
		unselect_device(sio);
	}
}

void psx_sio_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = addr - reg->start;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 write (0x%08x) at <%s+0x%x>", val, reg->name, addr);
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
	uint32_t register_offset = addr - reg->start;
	struct psx_sio* sio = reg->peripheral;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 read at <%s+0x%x>", reg->name, addr);
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
		sio->regs.stat.dsr_input_level = false;
	}

	return val;
}

void psx_sio_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = addr - reg->start;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 write (0x%04x) at <%s+0x%x>", val, reg->name, addr);
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
	uint32_t register_offset = addr - reg->start;
	struct psx_sio* sio = reg->peripheral;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 read at <%s+0x%x>", reg->name, addr);
		return 0xff;
	}

	uint8_t val = 0xff;
	if(register_offset < 4) {
		sio->regs.rx_data = rx_update(reg->peripheral);	
	}

	uint8_t* regs = reg->peripheral;
	val = regs[register_offset];
	if(register_offset == 4) {
		sio->regs.stat.dsr_input_level = false;
	}

	return val;
}

void psx_sio_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = addr - reg->start;
	if(register_offset > 0x10) {
		log_error("unhandled SIO1 write (0x%02x) at <%s+0x%x>", val, reg->name, addr);
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

