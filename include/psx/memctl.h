#ifndef PSX_MEMCTL_H
#define PSX_MEMCTL_H

#include <psx/memory.h>
#include <stdint.h>

#define PSX_MEMCTRL2_ADDR 0x1f801060
#define PSX_MEMCTRL3_ADDR 0xfffe0130

#ifdef __cplusplus
extern "C" {
#endif

struct psx_memctl {
	struct {
		uint32_t exp1_base;
		uint32_t exp2_base;
		uint32_t exp1_size;
		uint32_t exp3_size;
		uint32_t bios_size;
		uint32_t spu_delay;
		uint32_t cdr_delay;
		uint32_t exp2_size;
		uint32_t com_delay;
	} memctrl1;
	struct {
		uint32_t ram_size;
	} memctrl2;
	struct {
		uint32_t cache_ctl;
	} memctrl3;
};

void psx_memctl_init(struct psx_memctl* mc);

uint32_t psx_memctl_read32(struct psx_region* reg, uint32_t addr);
void psx_memctl_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_memctl_read16(struct psx_region* reg, uint32_t addr);
void psx_memctl_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_memctl_read8(struct psx_region* reg, uint32_t addr);
void psx_memctl_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_MEMCTL_H

