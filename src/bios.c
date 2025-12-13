#include <psx/bios.h>
#include <psx/memory.h>

#include "util.h"
#include "log.h"

#include <stdint.h>
#include <string.h>

void psx_bios_load(struct psx_bios* bios, const char* path, bool cache_to_mem) {
	bios->stream = fopen(path, "rb");
	if(!bios->stream) {
		panic("failed to load BIOS file at '%s'", path);	
	}

	fseek(bios->stream, 0, SEEK_END);

	uint64_t file_size = ftell(bios->stream);
	rewind(bios->stream);

	if(file_size != PSX_BIOS_SIZE) {
		panic("invalid BIOS size");
	}

	if(cache_to_mem) {
		bios->cache = malloc(PSX_BIOS_SIZE);
		fread(bios->cache, PSX_BIOS_SIZE, 1, bios->stream);
		fclose(bios->stream);
		bios->stream = NULL;
	} else {
		bios->cache = NULL;
	}
}

uint32_t psx_bios_read32(struct psx_region* reg, uint32_t addr) {
	if(PSX_MEM_ADDR_MODE(addr) != PSX_MEM_MODE_KSEG1) {
		panic("BIOS memory access mode is not KSEG1");
	}

	struct psx_bios* bios = reg->peripheral;
	uint32_t val = 0;
	uint32_t image_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(bios->cache != NULL) {
		memcpy(&val, &bios->cache[image_offset], sizeof(val));
		return val;
	}

	fseek(bios->stream, image_offset, SEEK_SET);
	fread(&val, sizeof(val), 1, bios->stream);
	return val;
}

uint16_t psx_bios_read16(struct psx_region* reg, uint32_t addr) {
	if(PSX_MEM_ADDR_MODE(addr) != PSX_MEM_MODE_KSEG1) {
		panic("BIOS memory access mode is not KSEG1");
	}

	struct psx_bios* bios = reg->peripheral;
	uint16_t val = 0;
	uint32_t image_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(bios->cache != NULL) {
		memcpy(&val, &bios->cache[image_offset], sizeof(val));
		return val;
	}

	fseek(bios->stream, image_offset, SEEK_SET);
	fread(&val, sizeof(val), 1, bios->stream);
	return val;
}

uint8_t psx_bios_read8(struct psx_region* reg, uint32_t addr) {
	if(PSX_MEM_ADDR_MODE(addr) != PSX_MEM_MODE_KSEG1) {
		panic("BIOS memory access mode is not KSEG1");
	}

	struct psx_bios* bios = reg->peripheral;
	uint8_t val = 0;
	uint32_t image_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	if(bios->cache != NULL) {
		val = bios->cache[image_offset];
		return val;
	}

	fseek(bios->stream, image_offset, SEEK_SET);
	return fgetc(bios->stream);
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
