#ifndef PSX_SPU_H
#define PSX_SPU_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdbool.h>

#define PSX_SPU_MEM_SIZE (512 * 1024)
#define PSX_SPU_OUTBUF_SIZE 32768

struct psx_spu {
	struct __attribute__((packed)) {
		struct {
			 int16_t lvolume;
			 int16_t rvolume;
			uint16_t sample_rate;
			uint16_t start_address;
			uint32_t adsr;
			 int16_t adsr_volume;
			uint16_t repeat_address;
		} voice[24];
		 int16_t main_lvolume;
		 int16_t main_rvolume;
		 int16_t revb_lvolume;
		 int16_t revb_rvolume;
		uint32_t kon;
		uint32_t koff;
		uint32_t pmon;
		uint32_t noise_en;
		uint32_t eon;
		uint32_t endx;
		uint16_t unk_da0;
		uint16_t mbase;
		uint16_t irq_addr;
		uint16_t trn_addr;
		uint16_t trn_fifo;
		uint16_t spucnt;
		uint16_t trn_ctrl;
		uint16_t spustat;
		uint32_t cdin_vol;
		uint32_t extin_vol;
		uint32_t current_vol;
		uint32_t unk_dbc;
		uint16_t dapf1;
		uint16_t dapf2;
		 int16_t viir;
		 int16_t vcomb1;
		 int16_t vcomb2;
		 int16_t vcomb3;
		 int16_t vcomb4;
		 int16_t vwall;
		 int16_t vapf1;
		 int16_t vapf2;
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
		 int16_t vlin;
		 int16_t vrin;
	} regs;

	uint32_t transfer_addr;
	uint8_t* mem;
	struct {
		uint32_t current_addr;
		uint16_t pitch_counter;
		struct {
			int16_t buf[28];
			uint32_t off;
		} dec;
		int16_t hist[2];
		int16_t sample[4];
		struct {
			uint8_t phase;
			int16_t level;
			int32_t counter;
		} env;
	} voice_state[24];
	struct {
		bool signal;
		int32_t timer;
		int16_t level;
	} noise;
	uint32_t loop_ignore;
	int16_t prev_output;
	uint16_t capture_offset;
	struct {
		uint16_t buf[32];
		int idx;
	} tfifo;
	struct {
		int32_t in_l[39];
		int32_t in_r[39];
		int16_t out_l[39];
		int16_t out_r[39];
		uint8_t off;
	} fir_buf;
	bool revb_signal;
	uint32_t revb_addr;
	struct {
		uint32_t write_off;
		uint32_t read_off;
		uint32_t capacity;
		int16_t* buf;
	} out;

	struct psx_system* sys;
};

void psx_spu_init(struct psx_spu* spu);
void psx_spu_reset(struct psx_spu* spu);
void psx_spu_direct_in(struct psx_spu* spu, uint32_t word);
uint32_t psx_spu_direct_out(struct psx_spu* spu);
uint32_t psx_spu_available_samples(struct psx_spu* spu);
void psx_spu_read_samples(struct psx_spu* spu, void* buf, uint32_t count);
int16_t psx_spu_pop_sample(struct psx_spu* spu);

uint32_t psx_spu_read32(struct psx_region* reg, uint32_t addr);
void psx_spu_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_spu_read16(struct psx_region* reg, uint32_t addr);
void psx_spu_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_spu_read8(struct psx_region* reg, uint32_t addr);
void psx_spu_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif // #ifndef PSX_SIO_H
