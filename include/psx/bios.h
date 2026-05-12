#ifndef PSX_BIOS_H
#define PSX_BIOS_H

#include <psx/memory.h>
#include <psx/system.h>

#include <stdio.h>
#include <stdint.h>

#define PSX_BIOS_SIZE 512 * 1024

#ifdef __cplusplus
extern "C" {
#endif

struct psx_bios {
	uint8_t* rom;
};

bool psx_bios_load(struct psx_bios* bios, const char* path);

uint32_t psx_bios_read32(struct psx_region* reg, uint32_t addr);
void psx_bios_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_bios_read16(struct psx_region* reg, uint32_t addr);
void psx_bios_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_bios_read8(struct psx_region* reg, uint32_t addr);
void psx_bios_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_BIOS_H

