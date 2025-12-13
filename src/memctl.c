#include <psx/memctl.h>
#include <psx/system.h>
#include <psx/bios.h>

#include "util.h"

#include <string.h>

static uint8_t* select_memctl_region(struct psx_memctl* mc, uint32_t addr, uint32_t* off) {
	uint8_t* regs;
	if(addr == PSX_MEMCTRL2_ADDR) {
		regs = (uint8_t*)&mc->memctrl2;
	} else if(addr == PSX_MEMCTRL3_ADDR) {
		regs = (uint8_t*)&mc->memctrl3;
		*off -= 0x130;
	} else {
		regs = (uint8_t*)&mc->memctrl1;
	}
	
	return regs;
}

void psx_memctl_init(struct psx_memctl* mc) {
	mc->memctrl1.exp1_base = 0x1f000000;
	mc->memctrl1.exp2_base = 0x1f802000;
	mc->memctrl1.exp1_size = 0x0013243f;
	mc->memctrl1.exp2_size = 0x00070777;
	mc->memctrl1.bios_size = PSX_BIOS_SIZE;
	mc->memctrl1.com_delay = 0x00000000;
	mc->memctrl1.spu_delay = 0x200931e1;
	mc->memctrl1.cdr_delay = 0x00020843;
	mc->memctrl2.ram_size  = 0x00003022;
	mc->memctrl3.cache_ctl = 0x00000b80;
}

uint32_t psx_memctl_read32(struct psx_region* reg, uint32_t addr) {
	struct psx_memctl* mc = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = select_memctl_region(mc, addr, &register_offset);

	uint32_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	return val;
}

uint16_t psx_memctl_read16(struct psx_region* reg, uint32_t addr) {
	struct psx_memctl* mc = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = select_memctl_region(mc, addr, &register_offset);

	uint32_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	return val;
}

uint8_t psx_memctl_read8(struct psx_region* reg, uint32_t addr) {
	struct psx_memctl* mc = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = select_memctl_region(mc, addr, &register_offset);

	return regs[register_offset];
}

void psx_memctl_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	struct psx_memctl* mc = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = select_memctl_region(mc, addr, &register_offset);

	memcpy(&regs[register_offset], &val, sizeof(val));
}

void psx_memctl_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	struct psx_memctl* mc = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = select_memctl_region(mc, addr, &register_offset);

	memcpy(&regs[register_offset], &val, sizeof(val));
}

void psx_memctl_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	struct psx_memctl* mc = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t* regs = select_memctl_region(mc, addr, &register_offset);

	regs[register_offset] = val;
}

