#include <psx/spu.h>

#include <string.h>
#include "util.h"
#include "log.h"

void psx_spu_init(struct psx_spu* spu) {
	psx_spu_reset(spu);
}

void psx_spu_reset(struct psx_spu* spu) {
	memset(spu, 0, sizeof(*spu));
}

uint32_t psx_spu_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read32 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint32_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	log_warn("SPU read32 (0x%08x) (offset <0x%x>)", val, register_offset);
	return val;
}

void psx_spu_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = addr - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU write32 (0x%08x -> offset <0x%x>)", val, register_offset);
		return;
	}

	memcpy(&regs[register_offset], &val, sizeof(val));
	spu->regs.spustat &= 0xffc0;
	spu->regs.spustat |= spu->regs.spucnt & 0x3f;
	log_warn("SPU write32 (0x%08x) (offset <0x%x>)", val, register_offset);
}

uint16_t psx_spu_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read16 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint16_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	log_warn("SPU read16 (0x%04x) (offset <0x%x>)", val, register_offset);
	return val;
}

void psx_spu_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = addr - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU write16 (0x%04x -> offset <0x%x>)", val, register_offset);
		return;
	}

	memcpy(&regs[register_offset], &val, sizeof(val));
	spu->regs.spustat &= 0xffc0;
	spu->regs.spustat |= spu->regs.spucnt & 0x3f;
	log_warn("SPU write16 (0x%04x) (offset <0x%x>)", val, register_offset);
}

uint8_t psx_spu_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read8 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint16_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	log_warn("SPU read8 (0x%02x) (offset <0x%x>)", val, register_offset);
	return val;
}

void psx_spu_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = addr - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read8 (0x%02x -> offset <0x%x>)", val, register_offset);
		return;
	}

	regs[register_offset] = val;
	spu->regs.spustat &= 0xffc0;
	spu->regs.spustat |= spu->regs.spucnt & 0x3f;
	log_warn("SPU write8 (0x%02x) (offset <0x%x>)", val, register_offset);
}

