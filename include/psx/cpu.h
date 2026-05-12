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

#ifdef __cplusplus
extern "C" {
#endif

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
		uint32_t sr;
		uint32_t cause;
		uint32_t epc;
		uint32_t prid;
	};
	uint32_t r[16];
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

typedef union {
	struct {
		int16_t x, y;
	};
	uint32_t xy;
} psx_gte_vec2_t;

typedef union {
	uint32_t rgb;
	struct {
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t c;
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
		int32_t tr_x, tr_y, tr_z;   // r37~39 translation vector
		psx_gte_mat_t l;            // r40~44 light matrix
		uint32_t rbk, gbk, bbk;     // r45~47 background color
		psx_gte_mat_t ls;           // r48~52 light source matrix
		uint32_t rfc, gfc, bfc;     // r53~55 far color
		int32_t ofx, ofy;           // r56~57 screen offset/origin
		uint32_t h;                 // r58 projection plane distance
		int32_t dqa;                // r59 depth cueing coefficient
		int32_t dqb;                // r60 depth cueing offset
		int32_t zsf3, zsf4;         // r61~62 average z scale factors
		uint32_t flag;              // r63 error flags
	};
	uint32_t r[64];
} psx_gte_regstate_t;

struct psx_cpu {
	psx_regstate_t regs;
	psx_cop0_regstate_t cop0_regs;
	psx_gte_regstate_t gte_regs;
	struct {
		uint32_t raw;
		bool lm;
		bool sf;
		int translation_vec;
		int mult_vec;
		int mult_mat;
	} gte_cmd;
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

void psx_cpu_init(struct psx_cpu* cpu, struct psx_system* sys);
void psx_cpu_reset(struct psx_cpu* cpu);
void psx_cpu_register_irq(struct psx_cpu* cpu);
void psx_cpu_clear_irq(struct psx_cpu* cpu);
void psx_cpu_fetch_execute(struct psx_cpu* cpu);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_CPU_H

