#include <psx/spu.h>
#include <psx/memory.h>
#include <psx/sched.h>

#include "util.h"
#include "log.h"
#include "rdef/spu.h"

#include <string.h>

enum {
	ADPCM_CUR,
	ADPCM_OLD,
	ADPCM_OLDER,
	ADPCM_OLDEST
};

static int16_t g_gauss_table[] = {
	-0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
	-0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
	0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
	0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0007, 0x0007,
	0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
	0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018,
	0x0019, 0x001B, 0x001C, 0x001E, 0x0020, 0x0021, 0x0023, 0x0025,
	0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
	0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050,
	0x0054, 0x0057, 0x005B, 0x005F, 0x0063, 0x0067, 0x006B, 0x006F,
	0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
	0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7,
	0x00CD, 0x00D4, 0x00DB, 0x00E3, 0x00EA, 0x00F2, 0x00FA, 0x0101,
	0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
	0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C,
	0x01A8, 0x01B4, 0x01C0, 0x01CC, 0x01D9, 0x01E5, 0x01F2, 0x0200,
	0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
	0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9,
	0x030B, 0x031D, 0x0330, 0x0343, 0x0356, 0x036A, 0x037E, 0x0392,
	0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
	0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506,
	0x0520, 0x053B, 0x0556, 0x0572, 0x058E, 0x05AA, 0x05C7, 0x05E4,
	0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
	0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF,
	0x0813, 0x0838, 0x085D, 0x0883, 0x08A9, 0x08D0, 0x08F7, 0x091E,
	0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
	0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8,
	0x0C07, 0x0C38, 0x0C68, 0x0C99, 0x0CCB, 0x0CFD, 0x0D30, 0x0D63,
	0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
	0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB,
	0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
	0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
	0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
	0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
	0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
	0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
	0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
	0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
	0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
	0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
	0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
	0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
	0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
	0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
	0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
	0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
	0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
	0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
	0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
	0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
	0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
	0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
	0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
	0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
	0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
	0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
	0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
	0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
	0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
	0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
	0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
	0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
	0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3
};

typedef struct {
	int16_t l;
	int16_t r;
} sample_t;

static void spu_update(struct psx_sched*, struct psx_sev*);

static struct psx_sev spu_update_ev = {
	.id = PSX_SEV_ID_SPU_UPDATE,
	.eta = 768,
	.trigger = spu_update
};

void psx_spu_init(struct psx_spu* spu) {
	psx_spu_reset(spu);
	spu->mem = malloc(PSX_SPU_MEM_SIZE);
	spu->out.capacity = PSX_SPU_OUTBUF_SIZE;
	spu->out.buf = malloc(PSX_SPU_OUTBUF_SIZE * sizeof(*spu->out.buf));
	psx_sched_add_ev(spu->sys->sched, &spu_update_ev);
}

void psx_spu_reset(struct psx_spu* spu) {
	memset(&spu->regs, 0, sizeof(spu->regs));
	memset(&spu->voice_state, 0, sizeof(spu->voice_state));
	spu->regs.endx = 0xffffff;
	spu->tfifo.idx = 0;
	spu->out.available = 0;
	spu->out.read_off = 0;
	spu->out.write_off = 0;
}

static int g_adpcm_fc_old[] = { 0, 60, 115, 98, 122 };
static int g_adpcm_fc_older[] = { 0, 0, 52, 55, 60 };

static void adpcm_decode_block(struct psx_spu* spu, int n) {
	uint8_t* src = &spu->mem[spu->voice_state[n].current_addr];
	uint16_t block_header = src[0] | (src[1] << 8);
	src += 2;

	int shift = ADP_SHIFT_GET(block_header);
	if(shift > 12) {
		shift = 9;
	}
	int filter = ADP_FILTER_GET(block_header);
	if(filter > 4) {
		filter = 4;
	}
	int16_t raw = 0;
	int16_t sample = 0;
	int old_coef = g_adpcm_fc_old[filter];
	int older_coef = g_adpcm_fc_older[filter];
	int old_sample;
	int older_sample;
	int8_t cur_byte;
	for(int i = 0; i < 28; i++) {
		cur_byte = (src[i / 2] >> ((i % 2) * 4)) & 0xf;
		raw = ((int8_t)((cur_byte & 0xf) << 4)) >> 4;

		sample = raw << (12 - shift);
		old_sample = spu->voice_state[n].hist[ADPCM_CUR];
		older_sample = spu->voice_state[n].hist[ADPCM_OLD];

		sample += ((old_coef * old_sample) - (older_coef * older_sample) + 32) / 64;

		spu->voice_state[n].hist[ADPCM_OLD] = spu->voice_state[n].hist[ADPCM_CUR];
		spu->voice_state[n].hist[ADPCM_CUR] = SAT(sample, -0x8000, 0x7fff);
		spu->voice_state[n].dec.buf[i] = spu->voice_state[n].hist[ADPCM_CUR];
	}

	if(block_header & ADP_LOOP_START) {
		spu->voice_state[n].repeat_addr = spu->voice_state[n].current_addr;
	}

	if(block_header & ADP_LOOP_END) {
		spu->voice_state[n].current_addr = spu->voice_state[n].repeat_addr;

		if(!(block_header & ADP_LOOP_REPEAT)) {
			// TODO: set envelope phase to Release and volume to 0
			// key off
			spu->regs.endx |= BIT(n);
		}
	} else {
		spu->voice_state[n].current_addr += 16;
	}
}

static int16_t vol_mult(int32_t sample, int32_t vol) {
	return (sample * vol) >> 15;
}

static int16_t spu_process_voice(struct psx_spu* spu, int n) {
	uint32_t pitch_step = spu->regs.voice[n].sample_rate;
	// the sample rate value is basically the fixed point ratio of the current rate over 44100Hz (the max is 4x speed)
	// with a 12 bit fractional part, so we can treat the pitch counter as such and extract the integer part to compute the current step 
	if(pitch_step > 0x4000) {
		pitch_step = 0x4000;
	}

	spu->voice_state[n].pitch_counter += pitch_step;
	// TODO: pitch modulation
	spu->voice_state[n].dec.off += spu->voice_state[n].pitch_counter >> 12;
	spu->voice_state[n].pitch_counter &= 0xfff;
	if(spu->voice_state[n].dec.off >= 28) {
		adpcm_decode_block(spu, n);
		spu->voice_state[n].dec.off -= 28;
	}

	spu->voice_state[n].sample[ADPCM_OLDEST] = spu->voice_state[n].sample[ADPCM_OLDER];
	spu->voice_state[n].sample[ADPCM_OLDER]  = spu->voice_state[n].sample[ADPCM_OLD];
	spu->voice_state[n].sample[ADPCM_OLD]    = spu->voice_state[n].sample[ADPCM_CUR];
	spu->voice_state[n].sample[ADPCM_CUR]    = spu->voice_state[n].dec.buf[spu->voice_state[n].dec.off];
	uint32_t interp_idx = (spu->voice_state[n].pitch_counter >> 4) & 0xff;
	int32_t new_sample;
	new_sample  = (g_gauss_table[0x0ff - interp_idx] * spu->voice_state[n].sample[ADPCM_OLDEST]) >> 15;
	new_sample += (g_gauss_table[0x1ff - interp_idx] * spu->voice_state[n].sample[ADPCM_OLDER] ) >> 15;
	new_sample += (g_gauss_table[0x100 + interp_idx] * spu->voice_state[n].sample[ADPCM_OLD]   ) >> 15;
	new_sample += (g_gauss_table[0x000 + interp_idx] * spu->voice_state[n].sample[ADPCM_CUR]   ) >> 15;
	// TODO: apply volume modifiers
	return new_sample;
}

static void spu_update(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_spu* spu = sched->sys->spu;

	int32_t mixed_sample = 0; 
	for(int i = 0; i < 24; i++) {
		// TODO: actually, playback ends when in Release with volume (L and R) = 0, not just in a Key-OFF state
		if(VOICE_KEY_OFF(spu->regs.endx, i)) {
			continue;
		}
		// log_error("SPU: updating voice %d", i);
		mixed_sample += spu_process_voice(spu, i) / 8;
	}
	mixed_sample = SAT(mixed_sample, -0x8000, 0x7fff);
	// TODO: output in the circular output buffer
	spu->out.buf[spu->out.write_off++] = mixed_sample;
	spu->out.buf[spu->out.write_off++] = mixed_sample;
	if(spu->out.write_off == spu->out.capacity) {
		spu->out.write_off = 0;
	}
	if(spu->out.available >= spu->out.capacity) {
		log_error("SPU: buffer overflow");
		spu->out.read_off += 2;
		if(spu->out.read_off >= spu->out.capacity) {
			spu->out.read_off -= spu->out.capacity;
		}
	} else {
		spu->out.available += 2;
	}
	
	psx_sched_remove_ev(sched, self->id);
	psx_sched_add_ev(sched, self);
}

uint32_t psx_spu_available_samples(struct psx_spu* spu) {
	return spu->out.available;
}

void psx_spu_read_samples(struct psx_spu* spu, void* buf, uint32_t count) {
	uint16_t* dst = buf;

	for(uint32_t i = 0; i < count; i++) {
		dst[i] = spu->out.buf[spu->out.read_off++];
		spu->out.available--;
		if(spu->out.read_off == spu->out.capacity) {
			spu->out.read_off = 0;
		}
	}
}

int16_t psx_spu_pop_sample(struct psx_spu* spu) {
	int16_t sample = spu->out.buf[spu->out.read_off++];
	spu->out.available--;
	if(spu->out.read_off == spu->out.capacity) {
		spu->out.read_off = 0;
	}
	return sample;
}

void psx_spu_direct_in(struct psx_spu* spu, uint32_t word) {
	*(uint32_t*)(&spu->mem[spu->transfer_addr]) = word;
	spu->transfer_addr = (spu->transfer_addr + 4) & 0x7ffff;
	spu->regs.trn_addr = spu->transfer_addr / 8;
}

uint32_t psx_spu_direct_out(struct psx_spu* spu) {
	uint32_t word = *(uint32_t*)(&spu->mem[spu->transfer_addr]);
	spu->transfer_addr = (spu->transfer_addr + 4) & 0x7ffff;
	spu->regs.trn_addr = spu->transfer_addr / 8;
	return word;
}

static void do_key_on(struct psx_spu* spu, uint32_t new) {
	for(int i = 0; i < 24; i++) {
		if(new & BIT(i)) {
			spu->voice_state[i].current_addr = spu->regs.voice[i].start_address * 8;
			spu->voice_state[i].pitch_counter = 0;
			spu->voice_state[i].dec.off = 0;
			adpcm_decode_block(spu, i);
		}
	}
}

static void do_key_off(struct psx_spu* spu, uint32_t new) {
	// set ADSR phase to Release
}

static bool spu_handle_write(struct psx_spu* spu, uint32_t off, uint32_t val) {
	switch(off) {
	// we deny writes to Read-Only registers
	// ENDX
	case 0x198:
	case 0x19c:
	// SPUSTAT
	case 0x1ae:
		return true;
	// KON
	case 0x188:
	case 0x18a: {
		int shift = ((off & 2) != 0) ? 16 : 0;
		uint32_t on = (val << shift) & 0xffffff;
		do_key_on(spu, on);
		spu->regs.endx &= ~on;
		break;
	}
	// KOFF
	case 0x18c:
	case 0x18e: {
		int shift = ((off & 2) != 0) ? 16 : 0;
		uint32_t off = (val << shift) & 0xffffff;
		// do_key_off(spu, off);
		spu->regs.endx |= off;
		break;
	}
	// TADDR
	case 0x1a6:
		spu->transfer_addr = val * 8;
		break;
	// TFIFO
	case 0x1a8:
		spu->tfifo.buf[spu->tfifo.idx] = val;
		if(spu->tfifo.idx < 32) {
			spu->tfifo.idx++;
		}
		break;
	// SPUCNT
	case 0x1aa:
		spu->regs.spustat &= 0xffc0;
		spu->regs.spustat |= val & 0x3f;

		int mode = CNT_TRN_MODE_GET(val);
		if(mode == 1) {
			uint16_t* dst = (uint16_t*)(&spu->mem[spu->transfer_addr]);
			for(int i = 0; i < spu->tfifo.idx; i++) {
				dst[i] = spu->tfifo.buf[i];
				spu->transfer_addr += 2;
			}
			spu->regs.trn_addr = spu->transfer_addr / 8;
			spu->tfifo.idx = 0;
		} else if(mode > 1) {
			// ensure no stale data is left after the transfer,
			// since we avoid using the transfer fifo during DMA
			spu->tfifo.idx = 0;
		}
		break;
	default:
		break;
	}

	return false;
}

uint32_t psx_spu_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read32 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint32_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	log_trace("SPU read32 (0x%08x) (offset <0x%x>)", val, register_offset);
	return val;
}

void psx_spu_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU write32 (0x%08x -> offset <0x%x>)", val, register_offset);
		return;
	}

	log_trace("SPU write32 (0x%08x) (offset <0x%x>)", val, register_offset);
	if(spu_handle_write(spu, register_offset, val)) {
		return;
	}
	memcpy(&regs[register_offset], &val, sizeof(val));
}

uint16_t psx_spu_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read16 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint16_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	log_trace("SPU read16 (0x%04x) (offset <0x%x>)", val, register_offset);
	return val;
}

void psx_spu_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU write16 (0x%04x -> offset <0x%x>)", val, register_offset);
		return;
	}

	log_trace("SPU write16 (0x%04x) (offset <0x%x>)", val, register_offset);
	if(spu_handle_write(spu, register_offset, val)) {
		return;
	}
	memcpy(&regs[register_offset], &val, sizeof(val));
}

uint8_t psx_spu_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read8 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint16_t val;
	memcpy(&val, &regs[register_offset], sizeof(val));
	log_trace("SPU read8 (0x%02x) (offset <0x%x>)", val, register_offset);
	return val;
}

void psx_spu_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	log_error("Unhandled SPU read8 (0x%02x -> offset <0x%x>)", val, register_offset);
}

