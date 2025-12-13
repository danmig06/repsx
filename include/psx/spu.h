#ifndef PSX_SPU_H
#define PSX_SPU_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdbool.h>

struct psx_spu {
	struct {
		struct {
			uint16_t lvolume;
			uint16_t rvolume;
			uint16_t sample_rate;
			uint16_t start_address;
			struct {
				uint8_t r;
				uint8_t s;
				uint8_t d;
				uint8_t a;
			} adsr;
			uint16_t adsr_volume;
			uint16_t adsr_repeat;
		} voice[24];
		uint16_t main_lvolume;
		uint16_t main_rvolume;
		uint16_t reverb_lvolume;
		uint16_t reverb_rvolume;
		uint32_t kon;
		uint32_t koff;
		uint32_t pitch_en;
		uint32_t noise_en;
		uint32_t echo_on;
		uint32_t voice_en;
		uint16_t unk_da0;
		uint16_t mbase;
		uint16_t irq9addr;
		uint16_t ramdta;
		uint16_t ramdtf;
		uint16_t spucnt;
		uint16_t ramdtc;
		uint16_t spustat;
		uint32_t cdaivol;
		uint32_t extivol;
		uint32_t currvol;
		uint32_t unk_dbc;
		uint16_t dapf1;
		uint16_t dapf2;
		int16_t  viir;
		int16_t  vcomb1;
		int16_t  vcomb2;
		int16_t  vcomb3;
		int16_t  vcomb4;
		int16_t  vwall;
		int16_t  vapf1;
		int16_t  vapf2;
		uint16_t mlsame;
		uint16_t mrsame;
		uint16_t mlcomb1;
		uint16_t mrcomb1;
		uint16_t mlcomb2;
		uint16_t mrcomb2;
		uint16_t dlsame;
		uint16_t drsame;
		uint16_t mldiff;
		uint16_t mrdiff;
		uint16_t mlcomb3;
		uint16_t mrcomb3;
		uint16_t mlcomb4;
		uint16_t mrcomb4;
		uint16_t dldiff;
		uint16_t drdiff;
		uint16_t mlapf1;
		uint16_t mrapf1;
		uint16_t mlapf2;
		uint16_t mrapf2;
		int16_t  vlin;
		int16_t  vrin;
		// maybe some other stuff
	} regs;

	struct psx_system* sys;
};

void psx_spu_init(struct psx_spu* spu);
void psx_spu_reset(struct psx_spu* spu);
void psx_spu_set_log_level(int level);

uint32_t psx_spu_read32(struct psx_region* reg, uint32_t addr);
void psx_spu_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_spu_read16(struct psx_region* reg, uint32_t addr);
void psx_spu_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_spu_read8(struct psx_region* reg, uint32_t addr);
void psx_spu_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif // #ifndef PSX_SIO_H
