#include <psx/cpu.h>
#include <psx/timer.h>
#include <psx/memory.h>

#include "util.h"
#include "log.h"
#include "opcodes.h"
#include "gte.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define CAUSE_IP  0x00000400
#define SR_IMASK2 0x00000004

#define HOOK_KCALLS
// #define NO_LOAD_CANCELING

void psx_cpu_init(struct psx_cpu* cpu) {
	psx_cpu_reset(cpu);
}

void psx_cpu_reset(struct psx_cpu* cpu) {
	memset(&cpu->regs, 0, sizeof(cpu->regs));
	cpu->regs.pc = PSX_RESET_ADDR;
	cpu->next_pc = cpu->regs.pc + 4;
	memset(&cpu->cop0_regs, 0, sizeof(cpu->cop0_regs));
	memset(&cpu->gte_regs, 0, sizeof(cpu->gte_regs));
	memset(&cpu->load_slot, 0, sizeof(cpu->load_slot));
	cpu->branch = false;
	cpu->cop0_regs.sr.cop0_enable = true;
	cpu->cop0_regs.prid = 2;
}

uint32_t psx_cpu_get_reg(struct psx_cpu* cpu, unsigned index) {
	return cpu->regs.r[index];
}

void psx_cpu_set_reg(struct psx_cpu* cpu, unsigned index, uint32_t val) {
	if(index == 0) {
		return;
	}

#ifndef NO_LOAD_CANCELING
	if(cpu->load_slot.target == index) {
		cpu->load_slot.target = 0;
	}
#endif

	cpu->regs.r[index] = val;
}

static const uint32_t cop0_reg_write_mask_table[] = {
	0x00000000, // r0
	0x00000000, // r1
	0x00000000, // r2
	0xffffffff, // BPC
	0x00000000, // r4
	0xffffffff, // BDA
	0x00000000, // JUMPDEST
	0xffc0f03f, // DCIC
	0x00000000, // BadVaddr
	0xffffffff, // BDAM
	0x00000000, // r10
	0xffffffff, // BPCM
	0xffffffff, // SR
	0x00000300, // CAUSE
	0x00000000, // EPC
	0x00000000  // PRID
};

uint32_t psx_cpu_get_cop0_reg(struct psx_cpu* cpu, unsigned index) {
	return cpu->cop0_regs.r[index];
}

void psx_cpu_set_cop0_reg(struct psx_cpu* cpu, unsigned index, uint32_t val) {
	cpu->cop0_regs.r[index] = val & cop0_reg_write_mask_table[index];
}

static uint32_t fetch(struct psx_cpu* cpu) {
	return psx_mem_read32(cpu->sys->memory, cpu->regs.pc);
}

static void execute(struct psx_cpu* cpu, uint32_t insn) {
	if(cpu->regs.pc % 4) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_LOAD_ERR);
	}

	cpu->clocks = PSX_CPU_CPI;
	if(FUNC(insn) == OP_SPECIAL) {
		switch(SUBFUNC(insn)) {
		case SFNC_SLL: sll(cpu, insn); return;
		case SFNC_OR: or(cpu, insn); return;
		case SFNC_JR: jr(cpu, insn); return;
		case SFNC_SLTU: sltu(cpu, insn); return;
		case SFNC_ADDU: addu(cpu, insn); return;
		case SFNC_ADD: add(cpu, insn); return;
		case SFNC_AND: and(cpu, insn); return;
		case SFNC_JALR: jalr(cpu, insn); return;
		case SFNC_SUB: sub(cpu, insn); return;
		case SFNC_SUBU: subu(cpu, insn); return;
		case SFNC_SRA: sra(cpu, insn); return;
		case SFNC_SLT: slt(cpu, insn); return;
		case SFNC_DIV: ediv(cpu, insn); return;
		case SFNC_MFLO: mflo(cpu, insn); return;
		case SFNC_SRL: srl(cpu, insn); return;
		case SFNC_DIVU: divu(cpu, insn); return;
		case SFNC_MFHI: mfhi(cpu, insn); return;
		case SFNC_SYSCALL: scall(cpu, insn); return;
		case SFNC_MTLO: mtlo(cpu, insn); return;
		case SFNC_MTHI: mthi(cpu, insn); return;
		case SFNC_SLLV: sllv(cpu, insn); return;
		case SFNC_NOR: nor(cpu, insn); return;
		case SFNC_SRAV: srav(cpu, insn); return;
		case SFNC_SRLV: srlv(cpu, insn); return;
		case SFNC_MULTU: multu(cpu, insn); return;
		case SFNC_XOR: xor(cpu, insn); return;
		case SFNC_MULT: mult(cpu, insn); return;
		case SFNC_BREAK: bk(cpu, insn); return;
		default:
			ill(cpu, insn);
			cpu->clocks -= PSX_CPU_CPI;
			return;
		}
	}

	if(FUNC(insn) & OP_COPMASK) {
		/*
		if(COP_NUM(insn) == 2) {
			fprintf(stderr, "unhandled COP2 instruction 0x%08x at 0x%08x\n", insn, cpu->regs.pc);
			return;
		}
		*/
		// COP2 <imm25> (fast path)
		if((insn & 0xfe000000) == 0x4a000000) { 
			gte_run_cmd(cpu, insn);
			return;
		}
		
		switch(FUNC(insn) & 0b111000) {
		case CPFC_LWCN: lwcn(cpu, insn); return;
		case CPFC_SWCN: swcn(cpu, insn); return;
		default:
			break;
		}	

		switch(COPFUNC(insn)) {
		case CPFC_MFCN: mfcn(cpu, insn); return;
		case CPFC_MTCN: mtcn(cpu, insn); return;
		case CPFC_CFCN: cfcn(cpu, insn); return;
		case CPFC_CTCN: ctcn(cpu, insn); return;
		case CPFC_RFE:  rfe(cpu, insn);  return;
		default:
			ill(cpu, insn);
			cpu->clocks -= PSX_CPU_CPI;
			return;
		}

	}

	switch(FUNC(insn)) {
	case OP_LUI: lui(cpu, insn); break;
	case OP_ORI: ori(cpu, insn); break;
	case OP_SW: sw(cpu, insn); break;
	case OP_J: j(cpu, insn); break;
	case OP_ADDIU: addiu(cpu, insn); break;
	case OP_BNE: bne(cpu, insn); break;
	case OP_ADDI: addi(cpu, insn); break;
	case OP_LW: lw(cpu, insn); break;
	case OP_SH: sh(cpu, insn); break;
	case OP_JAL: jal(cpu, insn); break;
	case OP_ANDI: andi(cpu, insn); break;
	case OP_SB: sb(cpu, insn); break;
	case OP_LB: lb(cpu, insn); break;
	case OP_BEQ: beq(cpu, insn); break;
	case OP_BGTZ: bgtz(cpu, insn); break;
	case OP_BLEZ: blez(cpu, insn); break;
	case OP_LBU: lbu(cpu, insn); break;
	case OP_BXX: bxx(cpu, insn); break;
	case OP_SLTI: slti(cpu, insn); break;
	case OP_SLTIU: sltiu(cpu, insn); break;
	case OP_LHU: lhu(cpu, insn); break;
	case OP_LH: lh(cpu, insn); break;
	case OP_LWR: lwr(cpu, insn); break;
	case OP_LWL: lwl(cpu, insn); break;
	case OP_SWR: swr(cpu, insn); break;
	case OP_SWL: swl(cpu, insn); break;
	case OP_XORI: xori(cpu, insn); break;
	default:
		ill(cpu, insn);
		cpu->clocks -= PSX_CPU_CPI;
		return;
	}

}

void psx_cpu_register_irq(struct psx_cpu* cpu) {
	// write a 1 to the "Interrupt Pending" field
	// interrupts force exceptions
	cpu->cop0_regs.cause.ip = 1;
}

void psx_cpu_clear_irq(struct psx_cpu* cpu) {
	// write a 0 to the "Interrupt Pending" field
	cpu->cop0_regs.cause.ip = 0;
}

bool psx_cpu_check_irqs(struct psx_cpu* cpu) {
	return cpu->cop0_regs.sr.ie && ((cpu->cop0_regs.sr.imask >> 2) & cpu->cop0_regs.cause.ip);
}

void psx_cpu_fetch_execute(struct psx_cpu* cpu) {
	cpu->saved_pc = cpu->regs.pc;
	cpu->branch_delay = cpu->branch;
	cpu->branch = false;
	cpu->branch_taken = false;

	uint32_t current_instruction = fetch(cpu);
	cpu->regs.pc = cpu->next_pc;
	cpu->next_pc += 4;
#ifdef HOOK_KCALLS
	if(PSX_MEM_REAL_ADDR(cpu->regs.pc) == 0xa4) { 
		if(cpu->regs.r[9] == 0x3c) {
			char c = cpu->regs.r[4];
			fputc(c, stderr);
		}

		if(cpu->regs.r[9] == 0xa1) {
			log_fatal("SystemError('%c', %d)", cpu->regs.r[4], cpu->regs.r[5]);
			exit(cpu->regs.r[5]);
		}
	}

	if(PSX_MEM_REAL_ADDR(cpu->regs.pc) == 0xb4) { 
		if(cpu->regs.r[9] == 0x3d) {
			char c = cpu->regs.r[4];
			fputc(c, stderr);
		}
	}

	// jr $ra on a shell jump skips the intro and boots straight into the disc, 
	// it can only be done once per reset, the kernel assumes the disc is inserted
	/*
	if(cpu->regs.pc == 0x80030000) {
		cpu->regs.pc = cpu->regs.ra;
		cpu->next_pc = cpu->regs.pc + 4;
		return;
	}
	*/

	
#endif
	if(psx_cpu_check_irqs(cpu)) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_INT);
		return;
	}
	execute(cpu, current_instruction);
	if(cpu->load_slot.delay) {
		cpu->load_slot.delay = false;
	} else if(cpu->load_slot.target != 0) {
		// psx_cpu_set_reg(cpu, cpu->load_slot.target, cpu->load_slot.value);
		cpu->regs.r[cpu->load_slot.target] = cpu->load_slot.value;
		memset(&cpu->load_slot, 0, sizeof(cpu->load_slot));
	}

	// cpu->clocks += cpu->sys->memory->current_access_delay;
}
