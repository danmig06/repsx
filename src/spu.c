#include <psx/spu.h>
#include <psx/memory.h>
#include <psx/sched.h>
#include <psx/irq.h>
#include <psx/cdrom.h>

#include "util.h"
#include "log.h"
#include "rdef/spu.h"

#include <string.h>

#define ENV_COUNTER_MAX (1 << 21)
#define SAT16(n) SAT(n, -0x8000, 0x7fff)

#define USE_HERMITE 1

enum {
	CAP_CD_LEFT,
	CAP_CD_RIGHT,
	CAP_VOICE1,
	CAP_VOICE3,
	CAP_BUF_COUNT
};

enum {
	ADPCM_CUR,
	ADPCM_OLD,
	ADPCM_OLDER,
	ADPCM_OLDEST
};

enum {
	DIR_INCREASING = 0,
	DIR_DECREASING = 1
};

enum {
	MODE_LINEAR = 0,
	MODE_EXPONENTIAL = 1	
};

typedef uint32_t spu_addr_t;

static int16_t g_fir_filter[39] = {
	-0x0001,  0x0000,  0x0002,  0x0000, -0x000A,  0x0000,  0x0023,  0x0000,
	-0x0067,  0x0000,  0x010A,  0x0000, -0x0268,  0x0000,  0x0534,  0x0000,
	-0x0B90,  0x0000,  0x2806,  0x4000,  0x2806,  0x0000, -0x0B90,  0x0000,
	 0x0534,  0x0000, -0x0268,  0x0000,  0x010A,  0x0000, -0x0067,  0x0000,
	 0x0023,  0x0000, -0x000A,  0x0000,  0x0002,  0x0000, -0x0001
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

static inline int32_t spu_gauss_interp(struct psx_spu* spu, int n) {
	int32_t new_sample;
	uint32_t interp_idx = (spu->voice_state[n].pitch_counter >> 4) & 0xff;
	new_sample  = (g_gauss_table[0x0ff - interp_idx] * spu->voice_state[n].sample[ADPCM_OLDEST]) >> 15;
	new_sample += (g_gauss_table[0x1ff - interp_idx] * spu->voice_state[n].sample[ADPCM_OLDER] ) >> 15;
	new_sample += (g_gauss_table[0x100 + interp_idx] * spu->voice_state[n].sample[ADPCM_OLD]   ) >> 15;
	new_sample += (g_gauss_table[0x000 + interp_idx] * spu->voice_state[n].sample[ADPCM_CUR]   ) >> 15;
	return new_sample;
}

#define HERMITE_FP_BITS 16
#define mulh(a, b) (((a) * (b)) >> HERMITE_FP_BITS)

static inline int32_t spu_hermite_interp(struct psx_spu* spu, int n) {
	int64_t y0, y1, y2, y3;
	y0 = spu->voice_state[n].sample[ADPCM_OLDEST] << HERMITE_FP_BITS;
	y1 = spu->voice_state[n].sample[ADPCM_OLDER]  << HERMITE_FP_BITS;
	y2 = spu->voice_state[n].sample[ADPCM_OLD]    << HERMITE_FP_BITS;
	y3 = spu->voice_state[n].sample[ADPCM_CUR]    << HERMITE_FP_BITS;

	int64_t x = (spu->voice_state[n].pitch_counter & 0xfff) << (HERMITE_FP_BITS - 12);

	int64_t c0 = y1;
	int64_t c1 = (y2 - y0) / 2;
	int64_t c2 = y0 - ((y1 * 5) / 2) + (y2 * 2) - (y3 / 2);
	int64_t c3 = ((y3 - y0) / 2) + (((y1 - y2) * 3) / 2);
	int64_t res = mulh(mulh(mulh(c3, x) + c2, x) + c1, x) + c0;
	return res >> HERMITE_FP_BITS;
}

#undef mulh

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

void psx_spu_init(struct psx_spu* spu, struct psx_system* sys) {
	psx_spu_reset(spu);
	spu->sys = sys;
	spu->mem = malloc(PSX_SPU_MEM_SIZE);
	spu->out.capacity = PSX_SPU_OUTBUF_SIZE;
	spu->out.buf = malloc(PSX_SPU_OUTBUF_SIZE * sizeof(*spu->out.buf));
	memset(&spu->fir_buf, 0, sizeof(spu->fir_buf));
	psx_sched_add_ev(spu->sys->sched, &spu_update_ev);
}

void psx_spu_reset(struct psx_spu* spu) {
	memset(&spu->regs, 0, sizeof(spu->regs));
	memset(&spu->voice_state, 0, sizeof(spu->voice_state));
	memset(&spu->noise, 0, sizeof(spu->noise));
	spu->capture_offset = 0;
	spu->regs.endx = 0xffffff;
	spu->tfifo.idx = 0;
	spu->out.read_off = 0;
	spu->out.write_off = 0;
}

static void* spu_get_ptr(struct psx_spu* spu, spu_addr_t addr) {
	if((spu->regs.spucnt & CNT_IRQ_EN) && addr == (spu->regs.irq_addr * 8)) {
		// log_error("SPU: IRQ triggered");
		spu->regs.spustat |= STAT_IRQ;
		psx_irq_raise(spu->sys->irq, PSX_IRQ_ID_SPU);
	}

	return &spu->mem[addr];
}

static int g_adpcm_fc_old[] = { 0, 60, 115, 98, 122 };
static int g_adpcm_fc_older[] = { 0, 0, 52, 55, 60 };

static void spu_adpcm_decode_block(struct psx_spu* spu, int n) {
	uint8_t* src = spu_get_ptr(spu, spu->voice_state[n].current_addr);
	// touch the mid-block address, games such Valkyrie Profile will hang at random points otherwise
	(void) spu_get_ptr(spu, spu->voice_state[n].current_addr + 8);
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
	int32_t sample = 0;
	int old_coef = g_adpcm_fc_old[filter];
	int older_coef = g_adpcm_fc_older[filter];
	int old_sample;
	int older_sample;
	int8_t cur_byte;
	for(int i = 0; i < 28; i++) {
		cur_byte = (src[i / 2] >> ((i % 2) * 4)) & 0xf;
		raw = ((int8_t)(cur_byte << 4)) >> 4;

		sample = raw << (12 - shift);
		old_sample = spu->voice_state[n].hist[ADPCM_CUR];
		older_sample = spu->voice_state[n].hist[ADPCM_OLD];

		sample += ((old_coef * old_sample) - (older_coef * older_sample) + 32) / 64;

		spu->voice_state[n].hist[ADPCM_OLD] = spu->voice_state[n].hist[ADPCM_CUR];
		spu->voice_state[n].hist[ADPCM_CUR] = SAT16(sample);
		spu->voice_state[n].dec.buf[i] = spu->voice_state[n].hist[ADPCM_CUR];
	}

	if(!(spu->loop_ignore & BIT(n)) && (block_header & ADP_LOOP_START)) {
		spu->regs.voice[n].repeat_address = spu->voice_state[n].current_addr / 8;
	}

	if(block_header & ADP_LOOP_END) {
		spu->voice_state[n].current_addr = spu->regs.voice[n].repeat_address * 8;

		if(!(block_header & ADP_LOOP_REPEAT)) {
			spu->voice_state[n].env.level = 0;
			// key off
			spu->voice_state[n].env.phase = PHASE_RELEASE;
			spu->regs.endx |= BIT(n);
		}
	} else {
		spu->voice_state[n].current_addr += 16;
	}
}

static void spu_key_on(struct psx_spu* spu, uint32_t new) {
	for(int i = 0; i < 24; i++) {
		if(new & BIT(i)) {
			spu->voice_state[i].current_addr = spu->regs.voice[i].start_address * 8;
			spu->voice_state[i].pitch_counter = 0;
			spu->voice_state[i].dec.off = 0;
			spu->voice_state[i].env.level = 0;
			spu->voice_state[i].env.phase = PHASE_ATTACK;
			spu->loop_ignore &= ~BIT(i);
			spu_adpcm_decode_block(spu, i);
		}
	}
	spu->regs.endx &= ~new;
}

static void spu_key_off(struct psx_spu* spu, uint32_t new) {
	for(int i = 0; i < 24; i++) {
		if(new & BIT(i)) {
			spu->voice_state[i].env.phase = PHASE_RELEASE;
		}
	}
	spu->regs.endx |= new;
}

static int16_t* spu_get_capture_ptr(struct psx_spu* spu, int capture) {
	switch(capture) {
		default:
		case CAP_CD_LEFT:  return spu_get_ptr(spu, 0x000 + spu->capture_offset);
		case CAP_CD_RIGHT: return spu_get_ptr(spu, 0x400 + spu->capture_offset);
		case CAP_VOICE1:   return spu_get_ptr(spu, 0x800 + spu->capture_offset);
		case CAP_VOICE3:   return spu_get_ptr(spu, 0xc00 + spu->capture_offset);
	}
}

static inline int16_t vmult(int32_t sample, int16_t vol) {
	return SAT16((sample * vol) >> 15);
}

static void spu_update_phase(struct psx_spu* spu, int n) {
	int phase = spu->voice_state[n].env.phase;
	uint16_t level = spu->voice_state[n].env.level;
	int sustain_level = (ADSR_STN_LVL_GET(spu->regs.voice[n].adsr) + 1) * 0x800;

	if(phase == PHASE_ATTACK && level == 0x7fff) {
		spu->voice_state[n].env.phase = PHASE_DECAY;
	}

	if(phase == PHASE_DECAY && level <= sustain_level) {
		spu->voice_state[n].env.phase = PHASE_SUSTAIN;
	}
}

static int16_t spu_env_tick(int32_t lvl, int dir, int mode, int shift, int step) {
	step = 7 - step;
	if(dir == DIR_DECREASING) {
		step = ~step;
	}

	step <<= MAX(0, 11 - shift);

	if(dir == DIR_DECREASING && mode == MODE_EXPONENTIAL) {
		step = (step * lvl) >> 15;
	}

	lvl = SAT(lvl + step, 0, 0x7fff);
	return lvl;
}

static int16_t spu_process_noise(struct psx_spu* spu) {
	spu->noise.signal ^= true;
	if(!spu->noise.signal) {
		return spu->noise.level;
	}

	int step = 4 | CNT_NOISE_STEP_GET(spu->regs.spucnt);
	int shift = CNT_NOISE_SH_GET(spu->regs.spucnt);
	spu->noise.timer -= step;
	if(spu->noise.timer < 0) {
		int lvl = spu->noise.level;
		int parity_bit = ((lvl >> 15) & 1) ^ ((lvl >> 12) & 1) ^ ((lvl >> 11) & 1) ^ ((lvl >> 10) & 1) ^ 1;
		spu->noise.level = (spu->noise.level << 1) | parity_bit;

		spu->noise.timer += 0x20000 >> shift;
		if(spu->noise.timer < 0) {
			spu->noise.timer += 0x20000 >> shift;
		}
	}

	return spu->noise.level;
}

static int16_t spu_revb_read(struct psx_spu* spu, spu_addr_t addr) {
	spu_addr_t base = spu->regs.mbase * 8;

	spu_addr_t off = ((spu->revb_addr + addr) - base) % (0x80000 - base);
	int16_t* src = spu_get_ptr(spu, (base + off) & 0x7fffe);
	return *src;
}

static void spu_revb_write(struct psx_spu* spu, spu_addr_t addr, int16_t val) {
	if(spu->regs.spucnt & CNT_REVB_EN) {
		spu_addr_t base = spu->regs.mbase * 8;

		spu_addr_t off = ((spu->revb_addr + addr) - base) % (0x80000 - base);
		int16_t* dst = spu_get_ptr(spu, (base + off) & 0x7fffe);
		*dst = val;
	}
}

#define R(a) spu_revb_read(spu, a)
#define W(a, v) spu_revb_write(spu, a, v)

static void spu_process_reverb(struct psx_spu* spu, int32_t* left_out, int32_t* right_out) {
	int32_t lv_in = 0, rv_in = 0;
	// downsample the input
	int read_off = spu->fir_buf.off;
	for(int i = 0; i < 39; i++) {
		lv_in += (spu->fir_buf.in_l[read_off] * g_fir_filter[i]) >> 15;
		rv_in += (spu->fir_buf.in_r[read_off] * g_fir_filter[i]) >> 15;
		if(read_off == 0) {
			read_off = 38;
		} else {
			read_off--;
		}
	}

	int16_t Lin = vmult(lv_in, spu->regs.vlin);
	int16_t Rin = vmult(rv_in, spu->regs.vrin);

	if(spu->regs.spucnt & CNT_REVB_EN) {
		bool viir_sign_flip = spu->regs.viir == -0x8000;
		spu_addr_t mLSAME = spu->regs.mlsame * 8;
		spu_addr_t mRSAME = spu->regs.mrsame * 8;
		spu_addr_t dLSAME = spu->regs.dlsame * 8;
		spu_addr_t dRSAME = spu->regs.drsame * 8;
		spu_addr_t mLDIFF = spu->regs.mldiff * 8;
		spu_addr_t mRDIFF = spu->regs.mrdiff * 8;
		spu_addr_t dLDIFF = spu->regs.dldiff * 8;
		spu_addr_t dRDIFF = spu->regs.drdiff * 8;
		// Same Side Reflection (L-to-L and R-to-R)
		int16_t same_ll = vmult(Lin + vmult(R(dLSAME), spu->regs.vwall) - R(mLSAME - 2), spu->regs.viir);
		int16_t same_rr = vmult(Rin + vmult(R(dRSAME), spu->regs.vwall) - R(mRSAME - 2), spu->regs.viir);
		// Different Side Reflection (R-to-L and L-to-R)
		int16_t diff_rl = vmult(Lin + vmult(R(dLDIFF), spu->regs.vwall) - R(mLDIFF - 2), spu->regs.viir);
		int16_t diff_lr = vmult(Rin + vmult(R(dRDIFF), spu->regs.vwall) - R(mRDIFF - 2), spu->regs.viir);
		W(mLSAME, SAT16(R(mLSAME - 2) + ((viir_sign_flip) ? -same_ll : same_ll)));
		W(mRSAME, SAT16(R(mRSAME - 2) + ((viir_sign_flip) ? -same_rr : same_rr)));
		W(mLDIFF, SAT16(R(mLDIFF - 2) + ((viir_sign_flip) ? -diff_rl : diff_rl)));
		W(mRDIFF, SAT16(R(mRDIFF - 2) + ((viir_sign_flip) ? -diff_lr : diff_lr)));
	}

	int16_t vCOMB1 = spu->regs.vcomb1;
	int16_t vCOMB2 = spu->regs.vcomb2;
	int16_t vCOMB3 = spu->regs.vcomb3;
	int16_t vCOMB4 = spu->regs.vcomb4;
	spu_addr_t mLCOMB1 = spu->regs.mlcomb1 * 8;
	spu_addr_t mLCOMB2 = spu->regs.mlcomb2 * 8;
	spu_addr_t mLCOMB3 = spu->regs.mlcomb3 * 8;
	spu_addr_t mLCOMB4 = spu->regs.mlcomb4 * 8;
	spu_addr_t mRCOMB1 = spu->regs.mrcomb1 * 8;
	spu_addr_t mRCOMB2 = spu->regs.mrcomb2 * 8;
	spu_addr_t mRCOMB3 = spu->regs.mrcomb3 * 8;
	spu_addr_t mRCOMB4 = spu->regs.mrcomb4 * 8;
	// Early Echo (Comb Filter, with input from buffer)
	int16_t Lout = SAT16(vmult(R(mLCOMB1), vCOMB1) + vmult(R(mLCOMB2), vCOMB2) + vmult(R(mLCOMB3), vCOMB3) + vmult(R(mLCOMB4), vCOMB4));
	int16_t Rout = SAT16(vmult(R(mRCOMB1), vCOMB1) + vmult(R(mRCOMB2), vCOMB2) + vmult(R(mRCOMB3), vCOMB3) + vmult(R(mRCOMB4), vCOMB4));

	spu_addr_t mLAPF1 = spu->regs.mlapf1 * 8;
	spu_addr_t mRAPF1 = spu->regs.mrapf1 * 8;
	spu_addr_t mLAPF2 = spu->regs.mlapf2 * 8;
	spu_addr_t mRAPF2 = spu->regs.mrapf2 * 8;
	spu_addr_t dAPF1 = spu->regs.dapf1 * 8;
	spu_addr_t dAPF2 = spu->regs.dapf2 * 8;
	// Late Reverb APF1 (All Pass Filter 1, with input from COMB)
	Lout = SAT16(Lout - vmult(R(mLAPF1 - dAPF1), spu->regs.vapf1));
	W(mLAPF1, Lout);
	Lout = SAT16(vmult(Lout, spu->regs.vapf1) + R(mLAPF1 - dAPF1));
	Rout = SAT16(Rout - vmult(R(mRAPF1 - dAPF1), spu->regs.vapf1));
	W(mRAPF1, Rout);
	Rout = SAT16(vmult(Rout, spu->regs.vapf1) + R(mRAPF1 - dAPF1));

	// Late Reverb APF2 (All Pass Filter 2, with input from APF1)
	Lout = SAT16(Lout - vmult(R(mLAPF2 - dAPF2), spu->regs.vapf2));
	W(mLAPF2, Lout);
	Lout = SAT16(vmult(Lout, spu->regs.vapf2) + R(mLAPF2 - dAPF2));
	Rout = SAT16(Rout - vmult(R(mRAPF2 - dAPF2), spu->regs.vapf2));
	W(mRAPF2, Rout);
	Rout = SAT16(vmult(Rout, spu->regs.vapf2) + R(mRAPF2 - dAPF2));

	// finally increment the reverb address
	spu_addr_t mbase = spu->regs.mbase * 8;
	spu->revb_addr = MAX(mbase, (spu->revb_addr + 2) & 0x7fffe);

	spu->fir_buf.out_l[spu->fir_buf.off] = Lout;
	spu->fir_buf.out_r[spu->fir_buf.off] = Rout;
	int32_t ul_out = 0, ur_out = 0;
	// upsample the output
	read_off = spu->fir_buf.off;
	for(int i = 0; i < 39; i++) {
		ul_out += (spu->fir_buf.out_l[read_off] * g_fir_filter[i]) >> 15;
		ur_out += (spu->fir_buf.out_r[read_off] * g_fir_filter[i]) >> 15;
		if(read_off == 0) {
			read_off = 38;
		} else {
			read_off--;
		}
	}

	*left_out = (ul_out * spu->regs.revb_lvolume) >> 15;
	*right_out = (ur_out * spu->regs.revb_rvolume) >> 15;
}

static sample_t spu_process_voice(struct psx_spu* spu, int n) {
	int32_t pitch_step = spu->regs.voice[n].sample_rate;
	if((spu->regs.pmon & BIT(n)) && n > 0) {
		int32_t factor = spu->prev_output;
		factor += 0x8000;
		pitch_step = (pitch_step << 16) >> 16;
		pitch_step = ((pitch_step * factor) >> 15) & 0xffff;
	}
	// the sample rate value is basically the fixed point ratio of the current rate over 44100Hz (the max is 4x speed)
	// with a 12 bit fractional part, so we can treat the pitch counter as such and extract the integer part to compute the current step 
	if(pitch_step > 0x4000) {
		pitch_step = 0x4000;
	}

	spu->voice_state[n].pitch_counter += pitch_step;
	int sample_step = spu->voice_state[n].pitch_counter >> 12;
	spu->voice_state[n].dec.off += sample_step;
	spu->voice_state[n].pitch_counter &= 0xfff;
	if(spu->voice_state[n].dec.off >= 28) {
		spu_adpcm_decode_block(spu, n);
		spu->voice_state[n].dec.off -= 28;
	}

	if(sample_step > 0) {
		spu->voice_state[n].sample[ADPCM_OLDEST] = spu->voice_state[n].sample[ADPCM_OLDER];
		spu->voice_state[n].sample[ADPCM_OLDER]  = spu->voice_state[n].sample[ADPCM_OLD];
		spu->voice_state[n].sample[ADPCM_OLD]    = spu->voice_state[n].sample[ADPCM_CUR];
		spu->voice_state[n].sample[ADPCM_CUR]    = spu->voice_state[n].dec.buf[spu->voice_state[n].dec.off];
	}

	int32_t new_sample;
	if(spu->regs.noise_en & BIT(n)) {
		new_sample = spu_process_noise(spu);
	} else {
#if USE_HERMITE
		new_sample = spu_hermite_interp(spu, n);
#else
		new_sample = spu_gauss_interp(spu, n);
#endif
	}

	new_sample = (new_sample * spu->voice_state[n].env.level) >> 15;
	spu->prev_output = SAT16(new_sample);
	if(n == 1) {
		int16_t* capture_dst = spu_get_capture_ptr(spu, CAP_VOICE1);
		*capture_dst = spu->prev_output;
	} else if(n == 3) {
		int16_t* capture_dst = spu_get_capture_ptr(spu, CAP_VOICE3);
		*capture_dst = spu->prev_output;
	}

	sample_t out = {
		.l = vmult(new_sample, spu->regs.voice[n].lvolume << 1),
		.r = vmult(new_sample, spu->regs.voice[n].rvolume << 1)
	};

	int direction, mode, shift, step;
	switch(spu->voice_state[n].env.phase) {
	case PHASE_ATTACK:
		direction = DIR_INCREASING;
		mode = (spu->regs.voice[n].adsr & ADSR_ATK_MODE) != 0;
		shift = ADSR_ATK_SH_GET(spu->regs.voice[n].adsr);
		step = ADSR_ATK_STEP_GET(spu->regs.voice[n].adsr);
		break;
	case PHASE_DECAY:
		direction = DIR_DECREASING;
		mode = MODE_EXPONENTIAL;
		shift = ADSR_DEC_SH_GET(spu->regs.voice[n].adsr);
		step = 0;
		break;
	case PHASE_SUSTAIN:
		direction = (spu->regs.voice[n].adsr & ADSR_STN_DIR) != 0;
		mode = (spu->regs.voice[n].adsr & ADSR_STN_MODE) != 0;
		shift = ADSR_STN_SH_GET(spu->regs.voice[n].adsr);
		step = ADSR_STN_STEP_GET(spu->regs.voice[n].adsr);
		break;
	case PHASE_RELEASE:
		direction = DIR_DECREASING;
		mode = (spu->regs.voice[n].adsr & ADSR_REL_MODE) != 0;
		shift = ADSR_REL_SH_GET(spu->regs.voice[n].adsr);
		step = 0;
		break;
	default:
		return out;
	}

	int32_t decrement = ENV_COUNTER_MAX >> MAX(0, shift - 11);
	if(direction == DIR_INCREASING && mode == MODE_EXPONENTIAL && spu->voice_state[n].env.level > 0x6000) {
		decrement >>= 2;
	}

	spu->voice_state[n].env.counter -= decrement;
	if(spu->voice_state[n].env.counter <= 0) {
		spu->voice_state[n].env.counter = ENV_COUNTER_MAX;

		spu->voice_state[n].env.level = spu_env_tick(spu->voice_state[n].env.level, direction, mode, shift, step);
		spu->regs.voice[n].adsr_volume = spu->voice_state[n].env.level;
		spu_update_phase(spu, n);
	}

	return out;
}

uint32_t psx_spu_available_samples(struct psx_spu* spu) {
	if(spu->out.read_off > spu->out.write_off) {
		return (spu->out.capacity - spu->out.read_off) + spu->out.write_off;
	} else {
		return spu->out.write_off - spu->out.read_off;
	}
}

void psx_spu_read_samples(struct psx_spu* spu, void* buf, uint32_t count) {
	uint16_t* dst = buf;

	for(uint32_t i = 0; i < count; i++) {
		dst[i] = spu->out.buf[spu->out.read_off++];
		if(spu->out.read_off == spu->out.capacity) {
			spu->out.read_off = 0;
		}
	}
}

int16_t psx_spu_pop_sample(struct psx_spu* spu) {
	int16_t sample = spu->out.buf[spu->out.read_off++];
	if(spu->out.read_off == spu->out.capacity) {
		spu->out.read_off = 0;
	}
	return sample;
}

static void spu_update(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_spu* spu = sched->sys->spu;

	sample_t current;
	int32_t left = 0, right = 0;
	int32_t revbl = 0, revbr = 0;
	for(int i = 0; i < 24; i++) {
		current = spu_process_voice(spu, i);
		left  += current.l;
		right += current.r;
		if(spu->regs.eon & BIT(i)) {
			revbl += current.l;
			revbr += current.r;
		}
	}
	if(!(spu->regs.spucnt & CNT_UNMUTE)) {
		left = right = 0;
	}

	int16_t* cd_left_cap  = spu_get_capture_ptr(spu, CAP_CD_LEFT);
	int16_t* cd_right_cap = spu_get_capture_ptr(spu, CAP_CD_RIGHT);
	if(spu->regs.spucnt & CNT_CD_EN) {
		int32_t cd_vol = spu->regs.cdin_vol;
		psx_cdr_sample_t cd = psx_cdr_pop_sample(spu->sys->cdrom);
		*cd_left_cap  = cd.l;
		*cd_right_cap = cd.r;
		cd.l = vmult(cd.l, cd_vol & 0xffff);
		cd.r = vmult(cd.r, cd_vol >> 16);
		left  += cd.l;
		right += cd.r;
		if(spu->regs.spucnt & CNT_CD_REVB) {
			revbl += cd.l;
			revbr += cd.r;
		}
	} else {
		*cd_left_cap = *cd_right_cap = 0;
	}

	if(!spu->revb_signal) {
		spu->fir_buf.in_l[spu->fir_buf.off] = revbl;
	} else {
		spu->fir_buf.in_r[spu->fir_buf.off] = revbr;
		int32_t revb_outl, revb_outr;
		spu_process_reverb(spu, &revb_outl, &revb_outr);
		left  += revb_outl;
		right += revb_outr;

		spu->fir_buf.off++;
		if(spu->fir_buf.off == 39) {
			spu->fir_buf.off = 0;
		}
	}
	spu->revb_signal ^= true;

	spu->capture_offset += 2;
	if(spu->capture_offset == 0x200) {
		spu->regs.spustat |= STAT_WR_REGION;
	} else if(spu->capture_offset == 0x400) {
		spu->capture_offset = 0;
		spu->regs.spustat &= ~STAT_WR_REGION;
	}

	spu->out.buf[spu->out.write_off++] = vmult(left,  spu->regs.main_lvolume << 1);
	spu->out.buf[spu->out.write_off++] = vmult(right, spu->regs.main_rvolume << 1);
	if(spu->out.write_off == spu->out.capacity) {
		spu->out.write_off = 0;
	}
	uint32_t available = psx_spu_available_samples(spu);
	if(available >= spu->out.capacity) {
		// log_error("SPU: buffer overflow");
		spu->out.read_off += 2;
		if(spu->out.read_off >= spu->out.capacity) {
			spu->out.read_off -= spu->out.capacity;
		}
	}
	
	psx_sched_remove_ev(sched, self->id);
	psx_sched_add_ev(sched, self);
}

void psx_spu_direct_in(struct psx_spu* spu, uint32_t word) {
	uint32_t* dst = spu_get_ptr(spu, spu->transfer_addr);
	*dst = word;
	spu->transfer_addr = (spu->transfer_addr + 4) & 0x7ffff;
	spu->regs.trn_addr = spu->transfer_addr / 8;
}

uint32_t psx_spu_direct_out(struct psx_spu* spu) {
	uint32_t* word = spu_get_ptr(spu, spu->transfer_addr);
	spu->transfer_addr = (spu->transfer_addr + 4) & 0x7ffff;
	spu->regs.trn_addr = spu->transfer_addr / 8;
	return *word;
}

static bool spu_handle_write(struct psx_spu* spu, uint32_t off, uint32_t val) {
	if((off >> 4) < 24 && (off & 0xf) == 0xe) {
		spu->loop_ignore |= BIT(off >> 4);
	} else switch(off) {
	// we deny writes to Read-Only registers
	// ENDX
	case 0x19c:
	case 0x19e:
	// SPUSTAT
	case 0x1ae:
		return true;
	// KON
	case 0x188:
	case 0x18a: {
		int shift = ((off & 2) != 0) ? 16 : 0;
		uint32_t on = (val << shift) & 0xffffff;
		spu_key_on(spu, on);
		break;
	}
	// KOFF
	case 0x18c:
	case 0x18e: {
		int shift = ((off & 2) != 0) ? 16 : 0;
		uint32_t off = (val << shift) & 0xffffff;
		spu_key_off(spu, off);
		break;
	}
	// MBASE
	case 0x1a2:
		spu->revb_addr = val * 8;
		break;
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

		int old_shift = CNT_NOISE_SH_GET(spu->regs.spucnt);
		int new_shift = CNT_NOISE_SH_GET(val);
		if(old_shift != new_shift) {
			spu->noise.timer = 0x20000 >> new_shift;
		}

		int mode = CNT_TRN_MODE_GET(val);
		if(mode == 1) {
			uint16_t* dst = spu_get_ptr(spu, spu->transfer_addr);

			for(int i = 0; i < spu->tfifo.idx; i++) {
				if(spu->transfer_addr % 8 == 0) {
					// "touch" the current 8-aligned block address to trigger potential IRQs
					(void) spu_get_ptr(spu, spu->transfer_addr);
				}
				dst[i] = spu->tfifo.buf[i];
				spu->transfer_addr += 2;
			}
			spu->regs.trn_addr = spu->transfer_addr / 8;
		}
		// for DMA modes, this ensures no stale data is left after the transfer,
		// since we avoid using the transfer fifo during DMA
		spu->tfifo.idx = 0;
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

	uint32_t val = *(uint32_t*)(&regs[register_offset]);
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

	if(spu_handle_write(spu, register_offset, val)) {
		return;
	}
	*(uint32_t*)(&regs[register_offset]) = val;
}

uint16_t psx_spu_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read16 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint16_t val = *(uint16_t*)(&regs[register_offset]);
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

	if(spu_handle_write(spu, register_offset, val)) {
		return;
	}
	*(uint16_t*)(&regs[register_offset]) = val;
}

uint8_t psx_spu_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_spu* spu = reg->peripheral;
	uint8_t* regs = reg->peripheral;

	if(register_offset >= sizeof(spu->regs)) {
		log_error("Unhandled SPU read8 (offset <0x%x>)", register_offset);
		return 0;
	}

	uint8_t val = regs[register_offset];
	return val;
}

void psx_spu_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	// 8-bit writes are executed as 16-bit writes
	if(addr % 2 == 0) {
		psx_spu_write16(reg, addr, val);
	}
}

