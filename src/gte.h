#ifndef PSX_GTE_H
#define PSX_GTE_H

#include <psx/cpu.h>

enum {
	GTE_FC_RTPS  = 0x01,
	GTE_FC_RTPT  = 0x02,
	GTE_FC_MVMVA = 0x04,
	GTE_FC_DCPL  = 0x06,
	GTE_FC_DPCS  = 0x07,
	GTE_FC_DPCT  = 0x08,
	GTE_FC_INTPL = 0x09,
	GTE_FC_SQR   = 0x0a,
	GTE_FC_NCS   = 0x0c,
	GTE_FC_NCT   = 0x0d,
	GTE_FC_NCDS  = 0x0e,
	GTE_FC_NCDT  = 0x0f,
	GTE_FC_NCCS  = 0x10,
	GTE_FC_NCCT  = 0x11,
	GTE_FC_CDP   = 0x12,
	GTE_FC_CC    = 0x13,
	GTE_FC_NCLIP = 0x14,
	GTE_FC_AVSZ3 = 0x15,
	GTE_FC_AVSZ4 = 0x16,
	GTE_FC_OP    = 0x17,
	GTE_FC_GPF   = 0x19,
	GTE_FC_GPL   = 0x1a
};

enum {
	GTE_RC_RTPS  = 0x01,
	GTE_RC_NCLIP = 0x06,
	GTE_RC_OP    = 0x0c,
	GTE_RC_DPCS  = 0x10,
	GTE_RC_INTPL = 0x11,
	GTE_RC_MVMVA = 0x12,
	GTE_RC_NCDS  = 0x13,
	GTE_RC_CDP   = 0x14,
	GTE_RC_NCDT  = 0x16,
	GTE_RC_NCCS  = 0x1b,
	GTE_RC_CC    = 0x1c,
	GTE_RC_NCS   = 0x1e,
	GTE_RC_NCT   = 0x20,
	GTE_RC_SQR   = 0x28,
	GTE_RC_DCPL  = 0x29,
	GTE_RC_DPCT  = 0x2a,
	GTE_RC_AVSZ3 = 0x2d,
	GTE_RC_AVSZ4 = 0x2e,
	GTE_RC_RTPT  = 0x30,
	GTE_RC_GPF   = 0x3d,
	GTE_RC_GPL   = 0x3e,
	GTE_RC_NCCT  = 0x3f,
};

uint32_t gte_read_register(struct psx_cpu* cpu, uint32_t idx);
void gte_write_register(struct psx_cpu* cpu, uint32_t idx, uint32_t val);
void gte_run_cmd(struct psx_cpu* cpu, uint32_t insn);

#endif // #ifndef PSX_GTE_H
