#include <psx/bios.h>
#include <psx/memory.h>

#include "util.h"
#include "log.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

bool psx_bios_load(struct psx_bios* bios, const char* path) {
	FILE* stream = fopen(path, "rb");
	if(!stream) {
		log_fatal("failed to load BIOS file at '%s'", path);
		return false;
	}

	fseek(stream, 0, SEEK_END);

	size_t file_size = ftell(stream);
	rewind(stream);

	if(file_size != PSX_BIOS_SIZE) {
		log_fatal("invalid BIOS size");
		return false;
	}

	bios->rom = malloc(PSX_BIOS_SIZE);
	fread(bios->rom, PSX_BIOS_SIZE, 1, stream);
	fclose(stream);
	return true;
}

uint32_t psx_bios_read32(struct psx_region* reg, uint32_t addr) {
	struct psx_bios* bios = reg->peripheral;
	uint32_t val = 0;
	uint32_t image_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	memcpy(&val, &bios->rom[image_offset], sizeof(val));
	return val;
}

uint16_t psx_bios_read16(struct psx_region* reg, uint32_t addr) {
	struct psx_bios* bios = reg->peripheral;
	uint16_t val = 0;
	uint32_t image_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	memcpy(&val, &bios->rom[image_offset], sizeof(val));
	return val;
}

uint8_t psx_bios_read8(struct psx_region* reg, uint32_t addr) {
	struct psx_bios* bios = reg->peripheral;
	uint8_t val = 0;
	uint32_t image_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	val = bios->rom[image_offset];
	return val;
}

void psx_bios_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	panic("attempt to write (0x%x) to BIOS ROM <%s+0x%x>", val, reg->name, addr - reg->start);
}

void psx_bios_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	panic("attempt to write (0x%x) to BIOS ROM <%s+0x%x>", val, reg->name, addr - reg->start);
}

void psx_bios_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	panic("attempt to write (0x%x) to BIOS ROM <%s+0x%x>", val, reg->name, addr - reg->start);
}
