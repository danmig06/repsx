#include <psx/memory.h>
#include <psx/memctl.h>
#include <psx/system.h>
#include <psx/cpu.h>
#include <psx/irq.h>

#include "util.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// #define HOOK_POST_CODES

void psx_mem_init(struct psx_mem* memory) {
	memory->phys = malloc(PSX_MEM_PHYS_SIZE);
	if(!memory->phys) {
		perror("Failed to allocate physical system memory: ");
	}
	memset(memory->phys, 0, PSX_MEM_PHYS_SIZE);

	memory->scratch = malloc(PSX_MEM_SCRATCH_SIZE);
	if(!memory->scratch) {
		perror("Failed to allocate scratchpad memory: ");
	}
	memset(memory->scratch, 0, PSX_MEM_SCRATCH_SIZE);
}

static struct psx_region* get_mapped_region(struct psx_mem* memory, uint32_t addr) {
	struct psx_region* selected_region = NULL;
	uint32_t real_addr = PSX_MEM_REAL_ADDR(addr);
	for(int i = 1; i < PSX_MEM_NUM_REGIONS; i++) {
		if(memory->map[i].start <= real_addr && real_addr < (memory->map[i].start + memory->map[i].size)) {
			selected_region = &memory->map[i];
			break;
		}
	}

	if(!selected_region) {
		struct psx_cpu* cpu = memory->sys->cpu;
		panic("0x%08x: Out-of-bounds access at address 0x%08x\n\
				ra=0x%08x, a0=0x%08x, a1=0x%08x, a2=0x%08x, a3=0x%08x, v0=0x%08x, v1=0x%08x", 
				cpu->regs.pc, addr, cpu->regs.ra, cpu->regs.a0, cpu->regs.a1, 
				cpu->regs.a2, cpu->regs.a3, cpu->regs.v0, cpu->regs.v1);
	}

	return selected_region;
}

uint32_t psx_mem_read32(struct psx_mem* memory, uint32_t addr) {
	if(PSX_MEM_REAL_ADDR(addr) < (PSX_MEM_PHYS_SIZE * 4)) {
		uint32_t val;
		memcpy(&val, &memory->phys[addr & 0x1fffff], sizeof(val));
		return val;
	}
	struct psx_region* selected_region = get_mapped_region(memory, addr);
	return selected_region->read32(selected_region, addr);
}

void psx_mem_write32(struct psx_mem* memory, uint32_t addr, uint32_t val) {
	if(PSX_MEM_REAL_ADDR(addr) < (PSX_MEM_PHYS_SIZE * 4)) {
		memcpy(&memory->phys[addr & 0x1fffff], &val, sizeof(val));
		return;
	}
	struct psx_region* selected_region = get_mapped_region(memory, addr);
	selected_region->write32(selected_region, addr, val);
}

uint16_t psx_mem_read16(struct psx_mem* memory, uint32_t addr) {
	if(PSX_MEM_REAL_ADDR(addr) < (PSX_MEM_PHYS_SIZE * 4)) {
		uint16_t val;
		memcpy(&val, &memory->phys[addr & 0x1fffff], sizeof(val));
		return val;
	}
	struct psx_region* selected_region = get_mapped_region(memory, addr);
	return selected_region->read16(selected_region, addr);
}

void psx_mem_write16(struct psx_mem* memory, uint32_t addr, uint16_t val) {
	if(PSX_MEM_REAL_ADDR(addr) < (PSX_MEM_PHYS_SIZE * 4)) {
		memcpy(&memory->phys[addr & 0x1fffff], &val, sizeof(val));
		return;
	}
	struct psx_region* selected_region = get_mapped_region(memory, addr);
	selected_region->write16(selected_region, addr, val);
}

uint8_t psx_mem_read8(struct psx_mem* memory, uint32_t addr) {
	if(PSX_MEM_REAL_ADDR(addr) < (PSX_MEM_PHYS_SIZE * 4)) {
		return memory->phys[addr & 0x1fffff];
	}
	struct psx_region* selected_region = get_mapped_region(memory, addr);
	return selected_region->read8(selected_region, addr);
}

void psx_mem_write8(struct psx_mem* memory, uint32_t addr, uint8_t val) {
	if(PSX_MEM_REAL_ADDR(addr) < (PSX_MEM_PHYS_SIZE * 4)) {
		memcpy(&memory->phys[addr & 0x1fffff], &val, sizeof(val));
		return;
	}
	struct psx_region* selected_region = get_mapped_region(memory, addr);
	selected_region->write8(selected_region, addr, val);
}

static char* region_names[] = {
	"<undefined>",
	"KUSEG",
	"KSEG0",
	"KSEG1",
	"KSEG2",
};

char* psx_mem_addr_region_name(uint32_t addr) {
	uint32_t addr_region = PSX_MEM_ADDR_MODE(addr);

	switch(addr_region) {
	case PSX_MEM_MODE_KUSEG:
		return region_names[1];
	case PSX_MEM_MODE_KSEG0:
		return region_names[2];
	case PSX_MEM_MODE_KSEG1:
		return region_names[3];
	case PSX_MEM_MODE_KSEG2:
		return region_names[4];
	default:
		return region_names[0];
	}
}

uint32_t psx_phys_read32(struct psx_region* reg, uint32_t addr) {
	addr &= PSX_MEM_PHYS_SIZE - 1;
	uint8_t* memory = reg->peripheral;

	uint32_t val = 0;
	memcpy(&val, &memory[addr], sizeof(val));

	return val;
}

void psx_phys_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	addr &= PSX_MEM_PHYS_SIZE - 1;
	uint8_t* memory = reg->peripheral;

	memcpy(&memory[addr], &val, sizeof(val));
}

uint16_t psx_phys_read16(struct psx_region* reg, uint32_t addr) {
	addr &= PSX_MEM_PHYS_SIZE - 1;
	uint8_t* memory = reg->peripheral;

	uint16_t val = 0;
	memcpy(&val, &memory[addr], sizeof(val));

	return val;
}

void psx_phys_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	addr &= PSX_MEM_PHYS_SIZE - 1;
	uint8_t* memory = reg->peripheral;

	memcpy(&memory[addr], &val, sizeof(val));
}

uint8_t psx_phys_read8(struct psx_region* reg, uint32_t addr) {
	addr &= PSX_MEM_PHYS_SIZE - 1;
	uint8_t* memory = reg->peripheral;

	return memory[addr];
}

void psx_phys_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	addr &= PSX_MEM_PHYS_SIZE - 1;
	uint8_t* memory = reg->peripheral;

	memory[addr] = val;
}

uint32_t psx_null_read32(struct psx_region* reg, uint32_t addr) {
	log_warn("Unhandled read32 at address 0x%08x <%s+0x%x>", addr, reg->name, PSX_MEM_REAL_ADDR(addr) - reg->start);
	return 0;
}

void psx_null_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	log_warn("Unhandled write32 (0x%x) at address 0x%08x <%s+0x%x>", val, addr, reg->name, PSX_MEM_REAL_ADDR(addr) - reg->start);
}

uint16_t psx_null_read16(struct psx_region* reg, uint32_t addr) {
	log_warn("Unhandled read16 at address 0x%08x <%s+0x%x>", addr, reg->name, PSX_MEM_REAL_ADDR(addr) - reg->start);
	return 0;
}

void psx_null_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	log_warn("Unhandled write16 (0x%x) at address 0x%08x <%s+0x%x>", val, addr, reg->name, PSX_MEM_REAL_ADDR(addr) - reg->start);
}

uint8_t psx_null_read8(struct psx_region* reg, uint32_t addr) {
	log_warn("Unhandled read8 at address 0x%08x <%s+0x%x>", addr, reg->name, PSX_MEM_REAL_ADDR(addr) - reg->start);
	return 0;
}

void psx_null_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
#ifdef HOOK_POST_CODES
	if(addr == 0x1f802041) {
		fprintf(stderr, "PSX: POST %u\n", val);
	}
#endif
	log_warn("Unhandled write8 (0x%x) at address 0x%08x <%s+0x%x>", val, addr, reg->name, PSX_MEM_REAL_ADDR(addr) - reg->start);
}

