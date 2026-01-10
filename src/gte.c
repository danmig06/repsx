#include "gte.h"
#include "util.h"

#include <stdbool.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SAT(v, a, b) (((v) < (a)) ? (a) : (((v) > (b)) ? (b) : (v)))

#define VX(i) cpu->gte_regs.v[i].x
#define VY(i) cpu->gte_regs.v[i].y
#define VXY(i) cpu->gte_regs.v[i].xy
#define VZ(i) cpu->gte_regs.v[i].z

#define RC cpu->gte_regs.rgbc.r
#define GC cpu->gte_regs.rgbc.g
#define BC cpu->gte_regs.rgbc.b
#define CC cpu->gte_regs.rgbc.c
#define RGBC cpu->gte_regs.rgbc.rgb

#define OTZ cpu->gte_regs.otz
#define IR(i) cpu->gte_regs.ir[i]

#define SX(i) cpu->gte_regs.sxy[i].x
#define SY(i) cpu->gte_regs.sxy[i].y
#define SZ(i) cpu->gte_regs.sz[i]
#define SXY2 cpu->gte_regs.sxy[2].xy
#define SXYP cpu->gte_regs.sxyp

#define R(i) cpu->gte_regs.rgb[i].r
#define G(i) cpu->gte_regs.rgb[i].g
#define B(i) cpu->gte_regs.rgb[i].b
#define CODE(i) cpu->gte_regs.rgb[i].c
#define RGB(i) cpu->gte_regs.rgb[i].rgb

#define MAC(i) cpu->gte_regs.mac[i]
#define IRGB cpu->gte_regs.irgb
#define ORGB cpu->gte_regs.orgb
#define LZCS cpu->gte_regs.lzcs
#define LZCR cpu->gte_regs.lzcr

#define RT(i, j) cpu->gte_regs.rt.elements[(3 * (i - 1)) + (j - 1)]
#define TRX cpu->gte_regs.trx
#define TRY cpu->gte_regs.try
#define TRZ cpu->gte_regs.trz

#define L(i, j) cpu->gte_regs.l.elements[(3 * (i - 1)) + (j - 1)]
#define RBK cpu->gte_regs.rbk
#define GBK cpu->gte_regs.gbk
#define BBK cpu->gte_regs.bbk

#define LR(j) cpu->gte_regs.ls._1##j
#define LG(j) cpu->gte_regs.ls._2##j
#define LB(j) cpu->gte_regs.ls._3##j
#define RFC cpu->gte_regs.rfc
#define GFC cpu->gte_regs.gfc
#define BFC cpu->gte_regs.bfc

#define OFX cpu->gte_regs.ofx
#define OFY cpu->gte_regs.ofy

#define H cpu->gte_regs.h
#define DQA cpu->gte_regs.dqa
#define DQB cpu->gte_regs.dqb
#define ZSF3 cpu->gte_regs.zsf3
#define ZSF4 cpu->gte_regs.zsf4
#define FLAG *((uint32_t*)(&cpu->gte_regs.flag))
#define FLAG_ERR_MASK 0x7f87e000
#define I64(v) ((int64_t)(v))

#define SXY_SHIFT() do { \
		cpu->gte_regs.sxy[0].xy = cpu->gte_regs.sxy[1].xy; \
		cpu->gte_regs.sxy[1].xy = cpu->gte_regs.sxy[2].xy; \
	} while(0)

#define SZ_SHIFT() do { \
		cpu->gte_regs.sz[0] = cpu->gte_regs.sz[1]; \
		cpu->gte_regs.sz[1] = cpu->gte_regs.sz[2]; \
		cpu->gte_regs.sz[2] = cpu->gte_regs.sz[3]; \
	} while(0)

#define RGB_SHIFT() do { \
		cpu->gte_regs.rgb[0].rgb = cpu->gte_regs.rgb[1].rgb; \
		cpu->gte_regs.rgb[1].rgb = cpu->gte_regs.rgb[2].rgb; \
	} while(0)

// disgusting hack :)
#define REG_S16 \
	1: \
	case 3: \
	case 5: \
	case 8: \
	case 9: \
	case 10: \
	case 11: \
	case 36: \
	case 44: \
	case 52: \
	case 58: \
	case 59: \
	case 61: \
	case 62

#define REG_U16 \
	7: \
	case 16: \
	case 17: \
	case 18: \
	case 19 \

#define REG_U15 \
	28: \
	case 29

#define SF cpu->gte_cmd.sf
#define LM cpu->gte_cmd.lm
#define GTE_DEBUG 0
#if GTE_DEBUG
#define gte_log(...) fprintf(__VA_ARGS__)
#else
#define gte_log(...) 
#endif

enum {
	GF_SX2_SAT      = 1 << 14,
	GF_Z_SAT        = 1 << 18,
	GF_R_SAT        = 1 << 21,
	GF_IR1_SAT      = 1 << 24,
	GF_MAC1_OVF_NEG = 1 << 27,
	GF_MAC1_OVF_POS = 1 << 30,
};

#define CHK_MAC1(expr) check_mac(cpu, 0, expr)
#define CHK_MAC2(expr) check_mac(cpu, 1, expr)
#define CHK_MAC3(expr) check_mac(cpu, 2, expr)
#define CHK_MAC(i, expr) check_mac(cpu, i - 1, expr)
static int64_t check_mac(struct psx_cpu* cpu, int flag_idx, int64_t value) {
	gte_log(stderr, "checking MAC%d input: 0x%016lx ", flag_idx + 1, value);
	if (value < -0x80000000000ll) {
		FLAG |= GF_MAC1_OVF_NEG >> flag_idx;
		gte_log(stderr, "(negative overflow)");
		
	} else if (value > 0x7ffffffffffll) {
		FLAG |= GF_MAC1_OVF_POS >> flag_idx;
		gte_log(stderr, "(positive overflow)");
	}

	int64_t res = (value << 20) >> 20;
	gte_log(stderr, "\n-> 0x%016lx\n", res);
	return res;
}

#define SAT_MAC1(expr) saturate_mac(cpu, 0, expr)
#define SAT_MAC2(expr) saturate_mac(cpu, 1, expr)
#define SAT_MAC3(expr) saturate_mac(cpu, 2, expr)
#define SAT_MAC(i, expr) saturate_mac(cpu, i - 1, expr)
static int32_t saturate_mac(struct psx_cpu* cpu, int flag_idx, int64_t value) {
	gte_log(stderr, "saturating MAC%d input: 0x%016lx ", flag_idx + 1, value);
	if (value < -0x80000000000ll) {
		FLAG |= GF_MAC1_OVF_NEG >> flag_idx;
		gte_log(stderr, "(negative overflow)");
		
	} else if (value > 0x7ffffffffffll) {
		FLAG |= GF_MAC1_OVF_POS >> flag_idx;
		gte_log(stderr, "(positive overflow)");
	}

	int32_t res = (int32_t)(((value << 20) >> 20) >> (SF * 12));
	gte_log(stderr, "\n-> 0x%08x\n", res);
	return res;
}
#define CHK_MAC0(expr) check_mac0(cpu, expr)
static int64_t check_mac0(struct psx_cpu* cpu, int64_t value) {
	gte_log(stderr, "checking MAC0 input: 0x%016lx ", value);
	if(value < -0x80000000ll) {
		cpu->gte_regs.flag.mac0_ovf_neg = true;
		gte_log(stderr, "(negative overflow)");
		
	} else if(value > 0x7fffffffll) {
		cpu->gte_regs.flag.mac0_ovf_pos = true;
		gte_log(stderr, "(positive overflow)");
	}

	gte_log(stderr, "\n");
	return value;
}

#define CHK_SX2(expr) check_sxy2(cpu, 0, expr)
#define CHK_SY2(expr) check_sxy2(cpu, 1, expr)
static int16_t check_sxy2(struct psx_cpu* cpu, int flag_idx, int64_t value) {
	gte_log(stderr, "checking S%c2 input: 0x%016lx ", (!flag_idx) ? 'X' : 'Y', value);
	if(value < -0x400) {
		FLAG |= GF_SX2_SAT >> flag_idx;
		gte_log(stderr, "(negative overflow)");

		value = -0x400;
	} else if(value > 0x3ff) {
		FLAG |= GF_SX2_SAT >> flag_idx;
		gte_log(stderr, "(positive overflow)");

		value = 0x3ff;
	}

	gte_log(stderr, "\n-> 0x%04x\n", (int16_t)value);
	return value;
}

#define CHK_SZ3(expr) check_otz(cpu, expr)
#define CHK_OTZ(expr) check_otz(cpu, expr)
static int32_t check_otz(struct psx_cpu* cpu, int32_t value) {
	gte_log(stderr, "checking OTZ/SZ3 input: 0x%08x ", value);
	if(value < 0) {
		cpu->gte_regs.flag.z_saturated = true;
		gte_log(stderr, "(negative overflow)");

		value = 0;
	} else if(value > 0xffff) {
		cpu->gte_regs.flag.z_saturated = true;
		gte_log(stderr, "(positive overflow)");

		value = 0xffff;
	}

	gte_log(stderr, "\n-> 0x%08x\n", value);
	return value;
}

#define CHK_IR1(expr) check_ir(cpu, 0, expr)
#define CHK_IR2(expr) check_ir(cpu, 1, expr)
#define CHK_IR3(expr) check_ir(cpu, 2, expr)
#define CHK_IR(i, expr) check_ir(cpu, i - 1, expr)
static int32_t check_ir(struct psx_cpu* cpu, int flag_idx, int64_t value) {
	int64_t lower_bound = (LM) ? 0x0000 : -0x8000;
	gte_log(stderr, "checking IR%d (min: 0x%08lx) input: 0x%016lx ", flag_idx + 1, lower_bound, value);
	if(value < lower_bound) {
		FLAG |= GF_IR1_SAT >> flag_idx;
		gte_log(stderr, "(negative overflow)");

		value = lower_bound;
	} else if(value > 0x7fff) {
		FLAG |= GF_IR1_SAT >> flag_idx;
		gte_log(stderr, "(positive overflow)");

		value = 0x7fff;
	}

	gte_log(stderr, "\n-> 0x%08lx\n", value);
	return value;
}

#define CHK_IR0(expr) check_ir0(cpu, expr)
static int32_t check_ir0(struct psx_cpu* cpu, int32_t value) {
	gte_log(stderr, "checking IR0 input: 0x%08x ", value);
	if(value < 0) {
		cpu->gte_regs.flag.ir0_saturated = true;
		gte_log(stderr, "(negative overflow)");

		value = 0;
	} else if(value > 0x1000) {
		cpu->gte_regs.flag.ir0_saturated = true;
		gte_log(stderr, "(positive overflow)");

		value = 0x1000;
	}

	gte_log(stderr, "\n-> 0x%08x\n", value);
	return value;
}

#define CHK_R(expr) check_rgb(cpu, 0, expr)
#define CHK_G(expr) check_rgb(cpu, 1, expr)
#define CHK_B(expr) check_rgb(cpu, 2, expr)
static uint8_t check_rgb(struct psx_cpu* cpu, int flag_idx, int32_t value) {
	gte_log(stderr, "checking %c input: 0x%08x ", "RGB"[flag_idx], value);
	if (value < 0) {
		FLAG |= GF_R_SAT >> flag_idx;
		gte_log(stderr, "(negative overflow)");

		value = 0;
	} else if(value > 0xff) {
		FLAG |= GF_R_SAT >> flag_idx;
		gte_log(stderr, "(positive overflow)");

		value = 0xff;
	}

	gte_log(stderr, "\n-> 0x%02x\n", value);
	return value;
}

#define PORTABLE_LZC 0
#if PORTABLE_LZC

static uint32_t npw2(uint32_t n) {
	--n;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	return n + 1;
}

static int bcnt(uint32_t n) {
	n = n - ((n >> 1) & 0x55555555);
	n = (n & 0x33333333) + ((n >> 2) & 0x33333333);
	return (((n + (n >> 4) & 0xf0f0f0f) * 0x1010101) >> 24) & 0x3f;
}

static int tzc(uint32_t n) {
	return 32 - bcnt((~n) ^ (n | ((n & -n) - 1)));
}

// broken
static inline int lzc(uint32_t n) {
	return 32 - tzc(npw2(n + !(n & 1)));
}

#else

static inline int lzc(uint32_t n) {
	return __builtin_clz((n & 0x80000000) ? ~n : n);
}

#endif

static inline void update_lzcr(struct psx_cpu* cpu) {
	if(LZCS == -1 || !LZCS) {
		LZCR = 32;

		return;
	}

	LZCR = lzc(LZCS);
}

static uint8_t g_unr_table[] = {
    0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1, 0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3,
    0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5, 0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8,
    0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb, 0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
    0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4, 0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a,
    0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86,
    0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
    0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
    0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d, 0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55,
    0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b,
    0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f,
    0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
    0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
    0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
    0x00
};

static inline uint32_t unr_divide(struct psx_cpu* cpu, uint64_t n, uint64_t d) {
	if(n >= d * 2) {
		cpu->gte_regs.flag.div_ovf = true;
		return 0x1ffff;
	}

	int z = lzc(d) - 16;
	n = n << z;
	d = d << z;
	int32_t u = g_unr_table[(d - 0x7fc0) >> 7] + 0x101;
	d = (0x2000080 - (d * u)) >> 8;
	d = (0x0000080 + (d * u)) >> 8;
	return MIN(0x1ffff, ((n * d) + 0x8000) >> 16);
}

/*
static inline uint32_t unr_divide(struct psx_cpu* cpu, uint16_t n, uint16_t d) {
	if (n >= d * 2) {
	    cpu->gte_regs.flag.div_ovf = true;

            return 0x1ffff;
	}
	int shift = lzc(d) - 16;

	int r1 = (d << shift) & 0x7fff;
	int r2 = g_unr_table[((r1 + 0x40) >> 7)] + 0x101;
	int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;

	uint32_t reciprocal = ((r2 * r3) + 0x80) >> 8;
	uint32_t res = ((((uint64_t)reciprocal * (n << shift)) + 0x8000) >> 16);

	return MIN(0x1ffff, res);
}
*/

static void avsz3(struct psx_cpu* cpu) {
	int64_t avg = I64((int16_t)ZSF3) * (SZ(1) + SZ(2) + SZ(3));
	MAC(0) = (int32_t)CHK_MAC0(avg);
	OTZ = CHK_OTZ(avg >> 12);
}

static void avsz4(struct psx_cpu* cpu) {
	int64_t avg = I64((int16_t)ZSF4) * (SZ(0) + SZ(1) + SZ(2) + SZ(3));
	MAC(0) = (int32_t)CHK_MAC0(avg);
	OTZ = CHK_OTZ(avg >> 12);
}

// only MAC0 comes out wrong at the end sometimes, which will eventually cause wrong SXY2 results
// amidog reports ~12000 successful tests before giving up, looks like it could be UNR division
static void rtps(struct psx_cpu* cpu, int vi, bool finalize) {
	int64_t vx = I64(VX(vi));
	int64_t vy = I64(VY(vi));
	int64_t vz = I64((int16_t)VZ(vi));
	int64_t in_mac1 = CHK_MAC1(CHK_MAC1(I64(TRX) * 0x1000 + I64(RT(1, 1)) * vx) + I64(RT(1, 2)) * vy) + I64(RT(1, 3)) * vz;
	MAC(1) = SAT_MAC1(in_mac1);
	IR(1) = CHK_IR1(MAC(1));
	
	int64_t in_mac2 = CHK_MAC2(CHK_MAC2(I64(TRY) * 0x1000 + I64(RT(2, 1)) * vx) + I64(RT(2, 2)) * vy) + I64(RT(2, 3)) * vz;
	MAC(2) = SAT_MAC2(in_mac2);
	IR(2) = CHK_IR2(MAC(2));

	int64_t in_mac3 = CHK_MAC3(CHK_MAC3(I64(TRZ) * 0x1000 + I64(RT(3, 1)) * vx) + I64(RT(3, 2)) * vy) + I64(RT(3, 3)) * vz;
	MAC(3) = SAT_MAC3(in_mac3);

	int32_t new_ir3_sf = in_mac3 >> (SF * 12);
	int32_t new_ir3_shift = in_mac3 >> 12;
	if(new_ir3_shift < -((int32_t)0x8000) || new_ir3_shift > 0x7fff) {
		cpu->gte_regs.flag.ir3_saturated = true;
	}
	IR(3) = SAT(new_ir3_sf, (!LM) ? -((int32_t)0x8000) : 0, 0x7fff);

	SZ_SHIFT();
	SZ(3) = CHK_SZ3(in_mac3 >> 12);
	int32_t perspective_factor = unr_divide(cpu, H, SZ(3));
	/*
	if(H < (SZ(3) * 2)) {
		perspective_factor = unr_divide(cpu, H, SZ(3));
	} else {
		perspective_factor = 0x1ffff;
		cpu->gte_regs.flag.div_ovf = true;
	}
	*/

	gte_log(stderr, "UNR division (0x%x / 0x%x) returned 0x%x\n", H, SZ(3), perspective_factor);

	SXY_SHIFT();
	SX(2) = CHK_SX2(CHK_MAC0(I64((int32_t)OFX) + (I64((int16_t)IR(1)) * perspective_factor)) >> 16);
	SY(2) = CHK_SY2(CHK_MAC0(I64((int32_t)OFY) + (I64((int16_t)IR(2)) * perspective_factor)) >> 16);

	if(finalize) {
		int64_t in_mac0 = I64(DQB) + (I64((int16_t)DQA) * perspective_factor);
		MAC(0) = CHK_MAC0(in_mac0);
		IR(0) = CHK_IR0(in_mac0 >> 12);
	}
}

static void nclip(struct psx_cpu* cpu) {
	MAC(0) = CHK_MAC0(I64(SX(0) * SY(1)) + I64(SX(1) * SY(2)) + I64(SX(2) * SY(0)) - I64(SX(0) * SY(2)) - I64(SX(1) * SY(0)) - I64(SX(2) * SY(1)));
}

static void sqr(struct psx_cpu* cpu) {
	MAC(1) = SAT_MAC1(I64((int16_t)IR(1)) * I64((int16_t)IR(1)));
	MAC(2) = SAT_MAC2(I64((int16_t)IR(2)) * I64((int16_t)IR(2)));
	MAC(3) = SAT_MAC3(I64((int16_t)IR(3)) * I64((int16_t)IR(3)));

	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
}

static void ncs(struct psx_cpu* cpu, int vi) {
	int64_t vx = I64(VX(vi));
	int64_t vy = I64(VY(vi));
	int64_t vz = I64((int16_t)VZ(vi));
	MAC(1) = SAT_MAC1((I64(L(1, 1)) * vx) + (I64(L(1, 2)) * vy) + (I64(L(1, 3)) * vz));
	MAC(2) = SAT_MAC2((I64(L(2, 1)) * vx) + (I64(L(2, 2)) * vy) + (I64(L(2, 3)) * vz));
	MAC(3) = SAT_MAC3((I64(L(3, 1)) * vx) + (I64(L(3, 2)) * vy) + (I64(L(3, 3)) * vz));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	int64_t in_ir1 = I64((int16_t)IR(1)), in_ir2 = I64((int16_t)IR(2)), in_ir3 = I64((int16_t)IR(3));
	MAC(1) = SAT_MAC1(CHK_MAC1(CHK_MAC1(I64((int32_t)RBK) * 0x1000 + I64(LR(1)) * in_ir1) + I64(LR(2)) * in_ir2) + I64(LR(3)) * in_ir3);
	MAC(2) = SAT_MAC2(CHK_MAC2(CHK_MAC2(I64((int32_t)GBK) * 0x1000 + I64(LG(1)) * in_ir1) + I64(LG(2)) * in_ir2) + I64(LG(3)) * in_ir3);
	MAC(3) = SAT_MAC3(CHK_MAC3(CHK_MAC3(I64((int32_t)BBK) * 0x1000 + I64(LB(1)) * in_ir1) + I64(LB(2)) * in_ir2) + I64(LB(3)) * in_ir3);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static void nccs(struct psx_cpu* cpu, int vi) {
	int64_t vx = I64(VX(vi));
	int64_t vy = I64(VY(vi));
	int64_t vz = I64((int16_t)VZ(vi));
	MAC(1) = SAT_MAC1((I64(L(1, 1)) * vx) + (I64(L(1, 2)) * vy) + (I64(L(1, 3)) * vz));
	MAC(2) = SAT_MAC2((I64(L(2, 1)) * vx) + (I64(L(2, 2)) * vy) + (I64(L(2, 3)) * vz));
	MAC(3) = SAT_MAC3((I64(L(3, 1)) * vx) + (I64(L(3, 2)) * vy) + (I64(L(3, 3)) * vz));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	int64_t in_ir1 = I64((int16_t)IR(1)), in_ir2 = I64((int16_t)IR(2)), in_ir3 = I64((int16_t)IR(3));
	MAC(1) = SAT_MAC1(CHK_MAC1(CHK_MAC1(I64((int32_t)RBK) * 0x1000 + I64(LR(1)) * in_ir1) + I64(LR(2)) * in_ir2) + I64(LR(3)) * in_ir3);
	MAC(2) = SAT_MAC2(CHK_MAC2(CHK_MAC2(I64((int32_t)GBK) * 0x1000 + I64(LG(1)) * in_ir1) + I64(LG(2)) * in_ir2) + I64(LG(3)) * in_ir3);
	MAC(3) = SAT_MAC3(CHK_MAC3(CHK_MAC3(I64((int32_t)BBK) * 0x1000 + I64(LB(1)) * in_ir1) + I64(LB(2)) * in_ir2) + I64(LB(3)) * in_ir3);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	MAC(1) = SAT_MAC1((RC * I64((int16_t)IR(1))) << 4); 
	MAC(2) = SAT_MAC2((GC * I64((int16_t)IR(2))) << 4);
	MAC(3) = SAT_MAC3((BC * I64((int16_t)IR(3))) << 4);
	
	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
}

static void ncds(struct psx_cpu* cpu, int vi) {
	int64_t vx = I64(VX(vi));
	int64_t vy = I64(VY(vi));
	int64_t vz = I64((int16_t)VZ(vi));
	MAC(1) = SAT_MAC1((I64(L(1, 1)) * vx) + (I64(L(1, 2)) * vy) + (I64(L(1, 3)) * vz));
	MAC(2) = SAT_MAC2((I64(L(2, 1)) * vx) + (I64(L(2, 2)) * vy) + (I64(L(2, 3)) * vz));
	MAC(3) = SAT_MAC3((I64(L(3, 1)) * vx) + (I64(L(3, 2)) * vy) + (I64(L(3, 3)) * vz));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	int64_t in_ir1 = I64((int16_t)IR(1)), in_ir2 = I64((int16_t)IR(2)), in_ir3 = I64((int16_t)IR(3));
	MAC(1) = SAT_MAC1(CHK_MAC1(CHK_MAC1(I64((int32_t)RBK) * 0x1000 + I64(LR(1)) * in_ir1) + I64(LR(2)) * in_ir2) + I64(LR(3)) * in_ir3);
	MAC(2) = SAT_MAC2(CHK_MAC2(CHK_MAC2(I64((int32_t)GBK) * 0x1000 + I64(LG(1)) * in_ir1) + I64(LG(2)) * in_ir2) + I64(LG(3)) * in_ir3);
	MAC(3) = SAT_MAC3(CHK_MAC3(CHK_MAC3(I64((int32_t)BBK) * 0x1000 + I64(LB(1)) * in_ir1) + I64(LB(2)) * in_ir2) + I64(LB(3)) * in_ir3);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	bool saved_lm = cpu->gte_cmd.lm;
	cpu->gte_cmd.lm = false;
	in_ir1 = CHK_IR1(SAT_MAC1(((I64((int32_t)RFC)) << 12) - (I64(RC << 4) * I64((int16_t)IR(1)))));
	in_ir2 = CHK_IR2(SAT_MAC2(((I64((int32_t)GFC)) << 12) - (I64(GC << 4) * I64((int16_t)IR(2)))));
	in_ir3 = CHK_IR3(SAT_MAC3(((I64((int32_t)BFC)) << 12) - (I64(BC << 4) * I64((int16_t)IR(3)))));
	cpu->gte_cmd.lm = saved_lm;
	MAC(1) = SAT_MAC1((I64(RC << 4) * I64((int16_t)IR(1))) + (I64((int16_t)IR(0)) * in_ir1));
	MAC(2) = SAT_MAC2((I64(GC << 4) * I64((int16_t)IR(2))) + (I64((int16_t)IR(0)) * in_ir2));
	MAC(3) = SAT_MAC3((I64(BC << 4) * I64((int16_t)IR(3))) + (I64((int16_t)IR(0)) * in_ir3));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
	
	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static void cc(struct psx_cpu* cpu) {
	int64_t in_ir1 = I64((int16_t)IR(1)), in_ir2 = I64((int16_t)IR(2)), in_ir3 = I64((int16_t)IR(3));
	MAC(1) = SAT_MAC1(CHK_MAC1(CHK_MAC1(I64((int32_t)RBK) * 0x1000 + I64(LR(1)) * in_ir1) + I64(LR(2)) * in_ir2) + I64(LR(3)) * in_ir3);
	MAC(2) = SAT_MAC2(CHK_MAC2(CHK_MAC2(I64((int32_t)GBK) * 0x1000 + I64(LG(1)) * in_ir1) + I64(LG(2)) * in_ir2) + I64(LG(3)) * in_ir3);
	MAC(3) = SAT_MAC3(CHK_MAC3(CHK_MAC3(I64((int32_t)BBK) * 0x1000 + I64(LB(1)) * in_ir1) + I64(LB(2)) * in_ir2) + I64(LB(3)) * in_ir3);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
	MAC(1) = SAT_MAC1((I64(RC) * I64((int16_t)IR(1))) << 4);
	MAC(2) = SAT_MAC2((I64(GC) * I64((int16_t)IR(2))) << 4);
	MAC(3) = SAT_MAC3((I64(BC) * I64((int16_t)IR(3))) << 4);

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
}

static void cdp(struct psx_cpu* cpu) {
	int64_t in_ir1 = I64((int16_t)IR(1)), in_ir2 = I64((int16_t)IR(2)), in_ir3 = I64((int16_t)IR(3));
	MAC(1) = SAT_MAC1(CHK_MAC1(CHK_MAC1(I64((int32_t)RBK) * 0x1000 + I64(LR(1)) * in_ir1) + I64(LR(2)) * in_ir2) + I64(LR(3)) * in_ir3);
	MAC(2) = SAT_MAC2(CHK_MAC2(CHK_MAC2(I64((int32_t)GBK) * 0x1000 + I64(LG(1)) * in_ir1) + I64(LG(2)) * in_ir2) + I64(LG(3)) * in_ir3);
	MAC(3) = SAT_MAC3(CHK_MAC3(CHK_MAC3(I64((int32_t)BBK) * 0x1000 + I64(LB(1)) * in_ir1) + I64(LB(2)) * in_ir2) + I64(LB(3)) * in_ir3);
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	bool saved_lm = cpu->gte_cmd.lm;
	cpu->gte_cmd.lm = false;
	in_ir1 = CHK_IR1(SAT_MAC1((I64((int32_t)RFC) << 12) - (I64(RC << 4) * I64((int16_t)IR(1)))));
	in_ir2 = CHK_IR2(SAT_MAC2((I64((int32_t)GFC) << 12) - (I64(GC << 4) * I64((int16_t)IR(2)))));
	in_ir3 = CHK_IR3(SAT_MAC3((I64((int32_t)BFC) << 12) - (I64(BC << 4) * I64((int16_t)IR(3)))));
	cpu->gte_cmd.lm = saved_lm;
	
	MAC(1) = SAT_MAC1(((I64(RC << 4)) * I64((int16_t)IR(1))) + (I64((int16_t)IR(0)) * in_ir1));
	MAC(2) = SAT_MAC2(((I64(GC << 4)) * I64((int16_t)IR(2))) + (I64((int16_t)IR(0)) * in_ir2));
	MAC(3) = SAT_MAC3(((I64(BC << 4)) * I64((int16_t)IR(3))) + (I64((int16_t)IR(0)) * in_ir3));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static void op(struct psx_cpu* cpu) {
	int64_t d1 = RT(1, 1), d2 = RT(2, 2), d3 = RT(3, 3);
	int64_t in_ir1 = I64((int16_t)IR(1)), in_ir2 = I64((int16_t)IR(2)), in_ir3 = I64((int16_t)IR(3));

	MAC(1) = SAT_MAC1((in_ir3 * d2) - (in_ir2 * d3));
	MAC(2) = SAT_MAC2((in_ir1 * d3) - (in_ir3 * d1));
	MAC(3) = SAT_MAC3((in_ir2 * d1) - (in_ir1 * d2));

	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
}

static void dpcs(struct psx_cpu* cpu, bool is_triple) {
	int64_t r, g, b;
	if(is_triple) {
		r = cpu->gte_regs.rgb[0].r;
		g = cpu->gte_regs.rgb[0].g;
		b = cpu->gte_regs.rgb[0].b;
	} else {
		r = RC;
		g = GC;
		b = BC;
	}
	int64_t in_mac1 = SAT_MAC1((I64((int32_t)RFC) << 12) - (r << 16));
	int64_t in_mac2 = SAT_MAC2((I64((int32_t)GFC) << 12) - (g << 16));
	int64_t in_mac3 = SAT_MAC3((I64((int32_t)BFC) << 12) - (b << 16));

	bool saved_lm = cpu->gte_cmd.lm;
	cpu->gte_cmd.lm = false;
	int64_t in_ir1 = CHK_IR1(in_mac1);
	int64_t in_ir2 = CHK_IR2(in_mac2);
	int64_t in_ir3 = CHK_IR3(in_mac3);
	cpu->gte_cmd.lm = saved_lm;

	MAC(1) = SAT_MAC1((r << 16) + (in_ir1 * I64((int16_t)IR(0))));
	MAC(2) = SAT_MAC2((g << 16) + (in_ir2 * I64((int16_t)IR(0))));
	MAC(3) = SAT_MAC3((b << 16) + (in_ir3 * I64((int16_t)IR(0))));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static void intpl(struct psx_cpu* cpu) {
	bool saved_lm = cpu->gte_cmd.lm;
	cpu->gte_cmd.lm = false;
	int64_t in_mac1 = SAT_MAC1((I64((int32_t)RFC) << 12) - (I64((int16_t)IR(1)) << 12));
	int64_t in_mac2 = SAT_MAC2((I64((int32_t)GFC) << 12) - (I64((int16_t)IR(2)) << 12));
	int64_t in_mac3 = SAT_MAC3((I64((int32_t)BFC) << 12) - (I64((int16_t)IR(3)) << 12));
	int64_t in_ir1 = CHK_IR1(in_mac1);
	int64_t in_ir2 = CHK_IR2(in_mac2);
	int64_t in_ir3 = CHK_IR3(in_mac3);

	cpu->gte_cmd.lm = saved_lm;
	MAC(1) = SAT_MAC1((I64((int16_t)IR(1)) << 12) + (in_ir1 * I64((int16_t)IR(0))));
	MAC(2) = SAT_MAC2((I64((int16_t)IR(2)) << 12) + (in_ir2 * I64((int16_t)IR(0))));
	MAC(3) = SAT_MAC3((I64((int16_t)IR(3)) << 12) + (in_ir3 * I64((int16_t)IR(0))));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static void dcpl(struct psx_cpu* cpu) {
	MAC(1) = SAT_MAC1((RC * I64((int16_t)IR(1))) << 4); 
	MAC(2) = SAT_MAC2((GC * I64((int16_t)IR(2))) << 4);
	MAC(3) = SAT_MAC3((BC * I64((int16_t)IR(3))) << 4);
	
	bool saved_lm = cpu->gte_cmd.lm;
	cpu->gte_cmd.lm = false;
	int64_t in_ir1 = CHK_IR1(SAT_MAC1((I64((int32_t)RFC) << 12) - ((I64(RC << 4)) * I64((int16_t)IR(1)))));
	int64_t in_ir2 = CHK_IR2(SAT_MAC2((I64((int32_t)GFC) << 12) - ((I64(GC << 4)) * I64((int16_t)IR(2)))));
	int64_t in_ir3 = CHK_IR3(SAT_MAC3((I64((int32_t)BFC) << 12) - ((I64(BC << 4)) * I64((int16_t)IR(3)))));
	cpu->gte_cmd.lm = saved_lm;
	MAC(1) = SAT_MAC1(((I64(RC << 4)) * I64((int16_t)IR(1))) + (I64((int16_t)IR(0)) * in_ir1));
	MAC(2) = SAT_MAC2(((I64(GC << 4)) * I64((int16_t)IR(2))) + (I64((int16_t)IR(0)) * in_ir2));
	MAC(3) = SAT_MAC3(((I64(BC << 4)) * I64((int16_t)IR(3))) + (I64((int16_t)IR(0)) * in_ir3));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static void mvmva(struct psx_cpu* cpu) {
	int32_t txx = 0, txy = 0, txz = 0;
	switch(cpu->gte_cmd.translation_vec) {
	case 0:
		txx = TRX;
		txy = TRY;
		txz = TRZ;
		break;
	case 1:
		txx = RBK;
		txy = GBK;
		txz = BBK;
		break;
	case 2:
		txx = RFC;
		txy = GFC;
		txz = BFC;
		break;
	default:
		break;
	}
	// invalid matrix
	psx_gte_mat_t garbage_mat = {
		._11 = -(RC << 4), ._12 = (RC << 4), ._13 = IR(0),
		._21 = RT(1, 3),   ._22 = RT(1, 3),  ._23 = RT(1, 3),
		._31 = RT(2, 2),   ._32 = RT(2, 2),  ._33 = RT(2, 2)
	};
	psx_gte_mat_t* mx = &garbage_mat;
	switch(cpu->gte_cmd.mult_mat) {
	case 0:
		mx = &cpu->gte_regs.rt;
		break;
	case 1:
		mx = &cpu->gte_regs.l;
		break;
	case 2:
		mx = &cpu->gte_regs.ls;
		break;
	default:
		break;
	}

	// invalid vector
	psx_gte_vec3_t vx = { .x = IR(1), .y = IR(2), .z = IR(3) };
	if(cpu->gte_cmd.mult_vec < 3) {
		vx = cpu->gte_regs.v[cpu->gte_cmd.mult_vec];
	}

	int64_t vxz = I64((int16_t)vx.z);
	if(cpu->gte_cmd.translation_vec == 2) {
		MAC(1) = SAT_MAC1(CHK_MAC1(I64(mx->_12) * I64(vx.y)) + (I64(mx->_13) * vxz));
		MAC(2) = SAT_MAC2(CHK_MAC2(I64(mx->_22) * I64(vx.y)) + (I64(mx->_23) * vxz));
		MAC(3) = SAT_MAC3(CHK_MAC3(I64(mx->_32) * I64(vx.y)) + (I64(mx->_33) * vxz));
		int64_t in_mac1 = SAT_MAC1((I64(txx) << 12) + (I64(mx->_11) * I64(vx.x)));
		int64_t in_mac2 = SAT_MAC2((I64(txy) << 12) + (I64(mx->_21) * I64(vx.x)));
		int64_t in_mac3 = SAT_MAC3((I64(txz) << 12) + (I64(mx->_31) * I64(vx.x)));
		bool saved_lm = cpu->gte_cmd.lm;
		cpu->gte_cmd.lm = false;
		CHK_IR1(in_mac1);
		CHK_IR2(in_mac2);
		CHK_IR3(in_mac3);
		cpu->gte_cmd.lm = saved_lm;
	} else {
		MAC(1) = SAT_MAC1(CHK_MAC1(CHK_MAC1((I64(txx) << 12) + (I64(mx->_11) * I64(vx.x))) + (I64(mx->_12) * I64(vx.y))) + (I64(mx->_13) * vxz));
		MAC(2) = SAT_MAC2(CHK_MAC2(CHK_MAC2((I64(txy) << 12) + (I64(mx->_21) * I64(vx.x))) + (I64(mx->_22) * I64(vx.y))) + (I64(mx->_23) * vxz));
		MAC(3) = SAT_MAC3(CHK_MAC3(CHK_MAC3((I64(txz) << 12) + (I64(mx->_31) * I64(vx.x))) + (I64(mx->_32) * I64(vx.y))) + (I64(mx->_33) * vxz));
	}

	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));
}

void gpf(struct psx_cpu* cpu) {
	MAC(1) = SAT_MAC1(I64((int16_t)IR(0)) * I64((int16_t)IR(1)));
	MAC(2) = SAT_MAC2(I64((int16_t)IR(0)) * I64((int16_t)IR(2)));
	MAC(3) = SAT_MAC3(I64((int16_t)IR(0)) * I64((int16_t)IR(3)));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

void gpl(struct psx_cpu* cpu) {
	MAC(1) = SAT_MAC1((I64(MAC(1)) << (SF * 12)) + (I64((int16_t)IR(0)) * I64((int16_t)IR(1))));
	MAC(2) = SAT_MAC2((I64(MAC(2)) << (SF * 12)) + (I64((int16_t)IR(0)) * I64((int16_t)IR(2))));
	MAC(3) = SAT_MAC3((I64(MAC(3)) << (SF * 12)) + (I64((int16_t)IR(0)) * I64((int16_t)IR(3))));
	IR(1) = CHK_IR1(MAC(1));
	IR(2) = CHK_IR2(MAC(2));
	IR(3) = CHK_IR3(MAC(3));

	RGB_SHIFT();
	CODE(2) = CC;
	R(2) = CHK_R(MAC(1) >> 4);
	G(2) = CHK_G(MAC(2) >> 4);
	B(2) = CHK_B(MAC(3) >> 4);
}

static inline void rgb_wr_update(struct psx_cpu* cpu) {
	cpu->gte_regs.ir[1] = (int16_t)((cpu->gte_regs.irgb & 0x001f) << 7);
	cpu->gte_regs.ir[2] = (int16_t)((cpu->gte_regs.irgb & 0x03e0) << 2);
	cpu->gte_regs.ir[3] = (int16_t)((cpu->gte_regs.irgb & 0x7c00) >> 3);
}

static inline void rgb_rd_update(struct psx_cpu* cpu) {
	cpu->gte_regs.ir[1] = SAT((int16_t)(cpu->gte_regs.ir[1] & 0xffff), 0, 0x7fff);
	cpu->gte_regs.ir[2] = SAT((int16_t)(cpu->gte_regs.ir[2] & 0xffff), 0, 0x7fff);
	cpu->gte_regs.ir[3] = SAT((int16_t)(cpu->gte_regs.ir[3] & 0xffff), 0, 0x7fff);

	int r = SAT(cpu->gte_regs.ir[1] >> 7, 0x00, 0x1f);
	int g = SAT(cpu->gte_regs.ir[2] >> 7, 0x00, 0x1f);
	int b = SAT(cpu->gte_regs.ir[3] >> 7, 0x00, 0x1f);

	cpu->gte_regs.irgb = r | (g << 5) | (b << 10);
}

uint32_t gte_read_register(struct psx_cpu* cpu, uint32_t idx) {
	// gte_log(stderr, "GTE: read r%d (0x%08x)\n", idx, cpu->gte_regs.r[idx]);
	switch(idx) {
	case REG_S16:
		return (int32_t)(int16_t)cpu->gte_regs.r[idx];
	case 28:
		rgb_rd_update(cpu);
	case 29:
		return cpu->gte_regs.irgb;
	case 15:
		return cpu->gte_regs.sxy[2].xy;
	case 63:
		cpu->gte_regs.flag.error = (FLAG & FLAG_ERR_MASK) != 0;
		break;
	default:
		break;
	}

	return cpu->gte_regs.r[idx];
}

void gte_write_register(struct psx_cpu* cpu, uint32_t idx, uint32_t val) {
	// gte_log(stderr, "GTE: write 0x%08x to r%d\n", val, idx);
	switch(idx) {
	case REG_U16:
	case REG_S16:
		val &= 0xffff;
		break;
	case 15:
		SXY_SHIFT();
		cpu->gte_regs.sxy[2].xy = val;
		break;
	case 28:
		cpu->gte_regs.irgb = val & 0x7fff;
		rgb_wr_update(cpu);
	case 29:
		return;
	case 30:
		cpu->gte_regs.lzcs = val;
		update_lzcr(cpu);
	case 31:
		return;
	case 63:
		FLAG = val & 0x7ffff000;
		return;
	default:
		break;
	}

	cpu->gte_regs.r[idx] = val;
}

void gte_run_cmd(struct psx_cpu* cpu, uint32_t insn) {
	AS_UINT32(cpu->gte_cmd) = insn;
	char* name = NULL;
	FLAG = 0;
	switch(cpu->gte_cmd.real_code) {
	case GTE_RC_RTPS: rtps(cpu, 0, true); cpu->clocks = 15; return;
	case GTE_RC_RTPT:
		rtps(cpu, 0, false);
		rtps(cpu, 1, false);
		rtps(cpu, 2, true);
		cpu->clocks = 23;
		return;
	case GTE_RC_MVMVA: mvmva(cpu); cpu->clocks = 8; return;
	case GTE_RC_DCPL: dcpl(cpu); cpu->clocks = 8; return;
	case GTE_RC_DPCS: dpcs(cpu, false); cpu->clocks = 8; return;
	case GTE_RC_DPCT:
		dpcs(cpu, true);
		dpcs(cpu, true);
		dpcs(cpu, true);
		cpu->clocks = 17;
		return;
	case GTE_RC_INTPL: intpl(cpu); cpu->clocks = 8; return;
	case GTE_RC_SQR: sqr(cpu); cpu->clocks = 5; return;
	case GTE_RC_NCS: ncs(cpu, 0); cpu->clocks = 14; return;
	case GTE_RC_NCT:
		ncs(cpu, 0);
		ncs(cpu, 1);
		ncs(cpu, 2);
		cpu->clocks = 30;
		return;
	case GTE_RC_NCDS: ncds(cpu, 0); cpu->clocks = 19; return;
	case GTE_RC_NCDT:
		ncds(cpu, 0);
		ncds(cpu, 1);
		ncds(cpu, 2);
		cpu->clocks = 44;
		return;
	case GTE_RC_NCCS: nccs(cpu, 0); cpu->clocks = 17; return;
	case GTE_RC_NCCT:
		nccs(cpu, 0);
		nccs(cpu, 1);
		nccs(cpu, 2);
		cpu->clocks = 39;
		return;
	case GTE_RC_CDP: cdp(cpu); cpu->clocks = 13; return;
	case GTE_RC_CC: cc(cpu); cpu->clocks = 11; return;
	case GTE_RC_NCLIP: nclip(cpu); cpu->clocks = 8; return;
	case GTE_RC_AVSZ3: avsz3(cpu); cpu->clocks = 5; return;
	case GTE_RC_AVSZ4: avsz4(cpu); cpu->clocks = 6; return;
	case GTE_RC_OP: op(cpu); cpu->clocks = 6; return;
	case GTE_RC_GPF: gpf(cpu); cpu->clocks = 5; return;
	case GTE_RC_GPL: gpl(cpu); cpu->clocks = 5; return;
	default:
		break;
	}

	fprintf(stderr, "GTE: unhandled %s(sf=%d, lm=%d, tx=%d, vx=%d, mx=%d)\n", 
			name, cpu->gte_cmd.sf, cpu->gte_cmd.lm, cpu->gte_cmd.translation_vec, 
			cpu->gte_cmd.mult_vec, cpu->gte_cmd.mult_mat);
}

