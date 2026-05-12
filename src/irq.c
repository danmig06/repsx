#include <psx/cpu.h>
#include <psx/irq.h>

#include "util.h"

#include <string.h>

void psx_irq_init(struct psx_irq* irq, struct psx_system* sys) {
	psx_irq_reset(irq);
	irq->sys = sys;
}

void psx_irq_reset(struct psx_irq* irq) {
	irq->stat = 0;
	irq->mask = 0;
}

static void psx_irq_ack(struct psx_irq* irq) {
	if(irq->stat & irq->mask) {
		psx_cpu_register_irq(irq->sys->cpu);
	} else {
		psx_cpu_clear_irq(irq->sys->cpu);
	}
}

uint32_t psx_irq_read32(struct psx_region* reg, uint32_t addr) {
	struct psx_irq* irq = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	if(register_offset == 0) {
		return irq->stat;
	} else if(register_offset == 4) {
		return irq->mask;
	}

	return 0;
}

void psx_irq_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	struct psx_irq* irq = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	if(register_offset == 0) {
		irq->stat &= val;
	} else if(register_offset == 4) {
		irq->mask = val;
	}

	psx_irq_ack(irq);
}

uint16_t psx_irq_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint16_t val = 0;
	uint8_t* regs = reg->peripheral;

	if(register_offset < 8) {
		memcpy(&val, &regs[register_offset], sizeof(val));
	}

	return val;
}

void psx_irq_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = reg->peripheral;

	if(register_offset < 4) {
		struct psx_irq* irq = reg->peripheral;
		irq->stat &= val << (register_offset * 8);
	} else if(register_offset < 8) {
		memcpy(&regs[register_offset], &val, sizeof(val));
	}

	psx_irq_ack(reg->peripheral);
}

uint8_t psx_irq_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = reg->peripheral;

	if(register_offset < 8) {
		return regs[register_offset];
	}

	return 0;
}

void psx_irq_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = reg->peripheral;

	if(register_offset < 4) {
		struct psx_irq* irq = reg->peripheral;
		irq->stat &= val << (register_offset * 8);
	} else if(register_offset < 8) {
		regs[register_offset] = val;
	}

	psx_irq_ack(reg->peripheral);
}

void psx_irq_raise(struct psx_irq* irq, enum psx_irq_id id) {
	irq->stat |= id;

	if(irq->stat & irq->mask) {
		psx_cpu_register_irq(irq->sys->cpu);
	} else {
		psx_cpu_clear_irq(irq->sys->cpu);
	}
}

