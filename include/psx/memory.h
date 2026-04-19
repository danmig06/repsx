#ifndef PSX_MEMORY_H
#define PSX_MEMORY_H

#include <psx/system.h>

#include <stdbool.h>
#include <stdint.h>

#define PSX_MEM_NUM_REGIONS 16
#define PSX_MEM_PHYS_SIZE (2 * 1024 * 1024) // 2 MB
#define PSX_MEM_SCRATCH_SIZE (1024) // 1 KB

#define PSX_MEM_MODE_KUSEG 0x00000000
#define PSX_MEM_MODE_KSEG0 0x80000000
#define PSX_MEM_MODE_KSEG1 0xa0000000
#define PSX_MEM_MODE_KSEG2 0xe0000000
#define PSX_MEM_MODE_MASK  0xe0000000
#define PSX_MEM_ADDR_MASK  0x1fffffff

#define PSX_MEM_ADDR_MODE(addr) (((addr) & PSX_MEM_MODE_MASK))
#define PSX_MEM_REAL_ADDR(addr) ((uint32_t)((addr) & PSX_MEM_ADDR_MASK))

#ifdef __cplusplus
extern "C" {
#endif

struct psx_region {
	char* name;
	uint32_t start;
	uint32_t size;
	void* peripheral;

	uint32_t (*read32)(struct psx_region* reg, uint32_t addr);
	void (*write32)(struct psx_region* reg, uint32_t addr, uint32_t val);
	uint16_t (*read16)(struct psx_region* reg, uint32_t addr);
	void (*write16)(struct psx_region* reg, uint32_t addr, uint16_t val);
	uint8_t (*read8)(struct psx_region* reg, uint32_t addr);
	void (*write8)(struct psx_region* reg, uint32_t addr, uint8_t val);
};

struct psx_mem {
	uint8_t* phys;
	struct psx_region map[PSX_MEM_NUM_REGIONS];
	uint8_t scratch[PSX_MEM_SCRATCH_SIZE];
	struct psx_system* sys;
};

void psx_mem_init(struct psx_mem* memory);

char* psx_mem_addr_region_name(uint32_t addr);

uint32_t psx_mem_read32(struct psx_mem* memory, uint32_t addr);
void psx_mem_write32(struct psx_mem* memory, uint32_t addr, uint32_t val);
uint16_t psx_mem_read16(struct psx_mem* memory, uint32_t addr);
void psx_mem_write16(struct psx_mem* memory, uint32_t addr, uint16_t val);
uint8_t psx_mem_read8(struct psx_mem* memory, uint32_t addr);
void psx_mem_write8(struct psx_mem* memory, uint32_t addr, uint8_t val);

uint32_t psx_phys_read32(struct psx_region* reg, uint32_t addr);
void psx_phys_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_phys_read16(struct psx_region* reg, uint32_t addr);
void psx_phys_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_phys_read8(struct psx_region* reg, uint32_t addr);
void psx_phys_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

uint32_t psx_null_read32(struct psx_region* reg, uint32_t addr);
void psx_null_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_null_read16(struct psx_region* reg, uint32_t addr);
void psx_null_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_null_read8(struct psx_region* reg, uint32_t addr);
void psx_null_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_MEMORY_H

