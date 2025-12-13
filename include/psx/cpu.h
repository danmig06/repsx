#ifndef PSX_CPU_H
#define PSX_CPU_H

#include <psx/system.h>

#include <stdbool.h>
#include <stdint.h>

#define PSX_DO_DISASM

#define PSX_RESET_ADDR 0xbfc00000
#define PSX_CPU_CLOCKS_PER_SEC 33868800
#define PSX_CPU_FREQ 33.868800f
#define PSX_CPU_CPI 2

typedef struct __psx_regstate {
	union {
		struct {
			uint32_t zero, at;
			uint32_t v0, v1;
			uint32_t a0, a1, a2, a3;
			uint32_t t0, t1, t2, t3, t4, t5, t6, t7;
			uint32_t s0, s1, s2, s3, s4, s5, s6, s7;
			uint32_t t8, t9;
			uint32_t k0, k1;
			uint32_t gp;
			uint32_t sp;
			union {
				uint32_t fp;
				uint32_t s8;
			};
			uint32_t ra;
		};
		uint32_t r[32];
	};
	uint32_t pc;
	uint32_t hi, lo;
} psx_regstate_t;

enum {
	PSX_CPU_EXCAUSE_INT      = 0x0,
	PSX_CPU_EXCAUSE_LOAD_ERR = 0x4,
	PSX_CPU_EXCAUSE_STOR_ERR = 0x5,
	PSX_CPU_EXCAUSE_SYSCALL  = 0x8,
	PSX_CPU_EXCAUSE_BREAK    = 0x9,
	PSX_CPU_EXCAUSE_ILLEGAL  = 0xa,
	PSX_CPU_EXCAUSE_COPERROR = 0xb,
	PSX_CPU_EXCAUSE_OVERFLOW = 0xc
};

typedef struct __psx_cop0_status {
	bool ie: 1;
	uint32_t ku: 1;
	bool prev_ie: 1;
	uint32_t prev_ku: 1;
	bool old_ie: 1;
	uint32_t old_ku: 1;
	uint32_t unused: 2;
	uint32_t imask: 8;
	bool isc: 1;
	uint32_t swc: 1;
	uint32_t pz: 1;
	uint32_t cm: 1;
	uint32_t pe: 1;
	uint32_t ts: 1;
	uint32_t bev: 1;
	uint32_t unused1: 2;
	uint32_t re: 1;
	uint32_t unused2: 2;
	bool cop0_enable: 1;
	bool cop1_enable: 1;
	bool cop2_enable: 1;
	bool cop3_enable: 1;
} psx_cop0_status_t;

typedef struct __psx_cop0_cause {
	uint32_t unused: 2;
	uint32_t ex_code: 5;
	uint32_t unused1: 1;
	uint32_t sw: 2;
	uint32_t ip: 6;
	uint32_t unused2: 12;
	uint32_t ce: 2;
	bool bt: 1;
	bool bd: 1;
} psx_cop0_cause_t;

typedef union __psx_cop0_regstate {
	struct {
		uint32_t r0, r1, r2;
		uint32_t bpc;
		uint32_t r4;
		uint32_t bda;
		uint32_t tar;
		uint32_t dcic;
		uint32_t bad_a;
		uint32_t bda_mask;
		uint32_t r10;
		uint32_t bpc_mask;
		psx_cop0_status_t sr;
		psx_cop0_cause_t cause;
		uint32_t epc;
		uint32_t prid;
	};
	uint32_t r[64];
} psx_cop0_regstate_t;

typedef struct {
	union {
		struct {
			int16_t x, y;
		};
		uint32_t xy;
	};

	int32_t z;
} psx_gte_vec3_t;

typedef struct {
	union {
		struct {
			int16_t x, y;
		};
		uint32_t xy;
	};
} psx_gte_vec2_t;

typedef struct {
	union {
		uint32_t rgb;
		struct {
			uint8_t r;
			uint8_t g;
			uint8_t b;
			uint8_t c;
		};
	};
} psx_gte_color_t;

typedef union {
	struct {
		int16_t _11, _12, _13;
		int16_t _21, _22, _23;
		int16_t _31, _32, _33;
		int16_t pad;
	};
	int16_t elements[10];
} psx_gte_mat_t;

typedef struct {
	uint32_t real_code: 6;
	uint32_t unused0: 4;
	bool lm: 1;
	uint32_t unused1: 2;
	uint32_t translation_vec: 2;
	uint32_t mult_vec: 2;
	uint32_t mult_mat: 2;
	bool sf: 1;
	uint32_t fake_code: 5;
	uint32_t opcode: 7;
} psx_gte_cmd_t;

typedef struct {
	uint32_t unused: 12;
	bool ir0_saturated: 1; // 0x0000 > IR0 || 0x1009 < IR0
	bool sy2_saturated: 1; // -0x0400 > SY2 || 0x03ff < SY2
	bool sx2_saturated: 1; // -0x0400 > SX2 || 0x03ff < SX2
	bool mac0_ovf_neg: 1;
	bool mac0_ovf_pos: 1;
	bool div_ovf: 1; // RTPS/RTPT saturated result to 0x1ffff
	bool z_saturated: 1; // 0x0000 > SZ3/OTZ || 0xffff < SZ3/OTZ
	bool cb_saturated: 1; // 0x00 > B || 0xff < B
	bool cg_saturated: 1; // 0x00 > G || 0xff < G
	bool cr_saturated: 1; // 0x00 > R || 0xff < R
	bool ir3_saturated: 1; // -0x8000/0x0000 > IR3 || 0x7fff < IR3
	bool ir2_saturated: 1; // -0x8000/0x0000 > IR2 || 0x7fff < IR2
	bool ir1_saturated: 1; // -0x8000/0x0000 > IR1 || 0x7fff < IR1
	bool mac3_ovf_neg: 1;
	bool mac2_ovf_neg: 1;
	bool mac1_ovf_neg: 1;
	bool mac3_ovf_pos: 1;
	bool mac2_ovf_pos: 1;
	bool mac1_ovf_pos: 1;
	bool error: 1; // ((FLAG & 0x7f87e000) != 0) => IR3, RGB and SZ3/OTZ saturations do not count as errors
} psx_gte_flags_t;

typedef union __psx_gte_regstate {
	struct {
		// data registers
		psx_gte_vec3_t v[3];        // r0~5
		psx_gte_color_t rgbc;       // r6
		uint32_t otz;               // r7
		int32_t ir[4];              // r8~11
		psx_gte_vec2_t sxy[3];      // r12~14
		uint32_t sxyp;              // r15, move-on-write, reads yield sxy2
		uint32_t sz[4];             // r16~19
		psx_gte_color_t rgb[3];     // r20~22
		uint32_t res1;              // r23, prohibited
		int32_t mac[4];             // r24~27
		uint32_t irgb, orgb;        // r28~29, reads on orgb yield irgb
		int32_t lzcs, lzcr;         // r30~31
		
		// control registers
		psx_gte_mat_t rt;           // r32~36 rotation matrix
		int32_t trx, try, trz;      // r37~39 translation vector
		psx_gte_mat_t l;            // r40~44 light matrix
		uint32_t rbk, gbk, bbk;     // r45~47 background color
		psx_gte_mat_t ls;           // r48~52 light source matrix
		uint32_t rfc, gfc, bfc;     // r53~55 far color
		int32_t ofx, ofy;           // r56~57 screen offset/origin
		uint32_t h;                 // r58 projection plane distance
		int32_t dqa;                // r59 depth cueing coefficient
		int32_t dqb;                // r60 depth cueing offset
		int32_t zsf3, zsf4;         // r61~62 average z scale factors
		psx_gte_flags_t flag;       // r63 error flags
	};
	uint32_t r[64];
} psx_gte_regstate_t;

struct psx_cpu {
	psx_regstate_t regs;
	psx_cop0_regstate_t cop0_regs;
	psx_gte_regstate_t gte_regs;
	psx_gte_cmd_t gte_cmd;
	struct {
		unsigned target;
		uint32_t value;
		bool delay;
	} load_slot;
	uint32_t clocks;
	bool branch;
	bool branch_delay;
	bool branch_taken;
	uint32_t next_pc;
	uint32_t saved_pc;
	struct psx_system* sys;
};

void psx_cpu_init(struct psx_cpu* cpu);
void psx_cpu_reset(struct psx_cpu* cpu);
void psx_cpu_register_irq(struct psx_cpu* cpu);
void psx_cpu_clear_irq(struct psx_cpu* cpu);
uint32_t psx_cpu_get_reg(struct psx_cpu* cpu, unsigned index);
void psx_cpu_set_reg(struct psx_cpu* cpu, unsigned index, uint32_t val);
uint32_t psx_cpu_get_cop0_reg(struct psx_cpu* cpu, unsigned index);
void psx_cpu_set_cop0_reg(struct psx_cpu* cpu, unsigned index, uint32_t val);
void psx_cpu_fetch_execute(struct psx_cpu* cpu);

#endif // #ifndef PSX_CPU_H

