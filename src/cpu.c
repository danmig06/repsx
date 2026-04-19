#include <psx/cpu.h>
#include <psx/timer.h>
#include <psx/irq.h>
#include <psx/memory.h>

#include "util.h"
#include "log.h"
#include "opcodes.h"
#include "gte.h"
#include "rdef/cpu.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define HOOK_KCALLS

void psx_cpu_init(struct psx_cpu* cpu) {
	psx_cpu_reset(cpu);
}

void psx_cpu_reset(struct psx_cpu* cpu) {
	memset(&cpu->regs, 0, sizeof(cpu->regs));
	memset(&cpu->cop0_regs, 0, sizeof(cpu->cop0_regs));
	memset(&cpu->gte_regs, 0, sizeof(cpu->gte_regs));
	memset(&cpu->load_slot, 0, sizeof(cpu->load_slot));
	cpu->regs.pc = PSX_RESET_ADDR;
	cpu->next_pc = cpu->regs.pc + 4;
	cpu->branch = false;
	cpu->cop0_regs.sr |= SR_COP0_EN;
	cpu->cop0_regs.prid = 2;
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
		case SFNC_SLL: sll(cpu, insn); break;
		case SFNC_OR: or(cpu, insn); break;
		case SFNC_JR: jr(cpu, insn); break;
		case SFNC_SLTU: sltu(cpu, insn); break;
		case SFNC_ADDU: addu(cpu, insn); break;
		case SFNC_ADD: add(cpu, insn); break;
		case SFNC_AND: and(cpu, insn); break;
		case SFNC_JALR: jalr(cpu, insn); break;
		case SFNC_SUB: sub(cpu, insn); break;
		case SFNC_SUBU: subu(cpu, insn); break;
		case SFNC_SRA: sra(cpu, insn); break;
		case SFNC_SLT: slt(cpu, insn); break;
		case SFNC_DIV: ediv(cpu, insn); break;
		case SFNC_MFLO: mflo(cpu, insn); break;
		case SFNC_SRL: srl(cpu, insn); break;
		case SFNC_DIVU: divu(cpu, insn); break;
		case SFNC_MFHI: mfhi(cpu, insn); break;
		case SFNC_SYSCALL: scall(cpu, insn); break;
		case SFNC_MTLO: mtlo(cpu, insn); break;
		case SFNC_MTHI: mthi(cpu, insn); break;
		case SFNC_SLLV: sllv(cpu, insn); break;
		case SFNC_NOR: nor(cpu, insn); break;
		case SFNC_SRAV: srav(cpu, insn); break;
		case SFNC_SRLV: srlv(cpu, insn); break;
		case SFNC_MULTU: multu(cpu, insn); break;
		case SFNC_XOR: xor(cpu, insn); break;
		case SFNC_MULT: mult(cpu, insn); break;
		case SFNC_BREAK: bk(cpu, insn); break;
		default:
			ill(cpu, insn);
			cpu->clocks -= PSX_CPU_CPI;
		}
	} else if(FUNC(insn) & OP_COPMASK) {
		// COP2 <imm25> (fast path)
		if((insn & 0xfe000000) == 0x4a000000) { 
			gte_run_cmd(cpu, insn);
			return;
		}

		switch(FUNC(insn) & 0b111000) {
		case CPFC_LWCN: lwcn(cpu, insn); return;
		case CPFC_SWCN: swcn(cpu, insn); return;
		}

		switch(COPFUNC(insn)) {
		case CPFC_MFCN: mfcn(cpu, insn); break;
		case CPFC_MTCN: mtcn(cpu, insn); break;
		case CPFC_CFCN: cfcn(cpu, insn); break;
		case CPFC_CTCN: ctcn(cpu, insn); break;
		case CPFC_RFE:  rfe(cpu, insn);  break;
		default:
			ill(cpu, insn);
			cpu->clocks -= PSX_CPU_CPI;
		}
	} else switch(FUNC(insn)) {
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
	}
}

void psx_cpu_register_irq(struct psx_cpu* cpu) {
	// write a 1 to the "Interrupt Pending" field
	// interrupts force exceptions
	CAUSE_IP_SET(cpu->cop0_regs.cause, 1);
}

void psx_cpu_clear_irq(struct psx_cpu* cpu) {
	// write a 0 to the "Interrupt Pending" field
	CAUSE_IP_SET(cpu->cop0_regs.cause, 0);
}

static inline bool cpu_check_irqs(struct psx_cpu* cpu) {
	return (cpu->cop0_regs.sr & SR_IE) && ((SR_IMASK_GET(cpu->cop0_regs.sr) >> 2) & CAUSE_IP_GET(cpu->cop0_regs.cause));
}

void psx_cpu_fetch_execute(struct psx_cpu* cpu) {
	cpu->saved_pc = cpu->regs.pc;
	cpu->branch_delay = cpu->branch;
	cpu->branch = false;
	cpu->branch_taken = false;

	uint32_t current_instruction = fetch(cpu);
#ifdef HOOK_KCALLS
	if(PSX_MEM_REAL_ADDR(cpu->regs.pc) == 0xa0) {
		switch(cpu->regs.r[9]) {
		case 0x09:
		case 0x3c:
			fputc(cpu->regs.a0, stderr);
			break;
		case 0x3e:
			if(!cpu->regs.a0) {
				fputs("<NULL>", stderr);
				break;
			}

			char* str = (char*)&cpu->sys->memory->phys[cpu->regs.a0 & 0x1fffff];
			fputs(str, stderr);
			break;
		case 0x40:
			log_fatal("0x%08x: SystemErrorUnresolvedException() (sr=0x%08x, cause=0x%08x)", cpu->cop0_regs.epc, cpu->cop0_regs.sr, cpu->cop0_regs.cause);
			__asm__ volatile ("int3");
			break;
		case 0xa1:
			log_fatal("0x%08x: SystemError('%c', %d)", cpu->regs.pc, cpu->regs.r[4], cpu->regs.r[5]);
			exit(cpu->regs.r[5]);
			break;
		}
	} else if(PSX_MEM_REAL_ADDR(cpu->regs.pc) == 0xb0) {
		switch(cpu->regs.r[9]) {
		case 0x3b:
		case 0x3d:
			fputc(cpu->regs.a0, stderr);
			break;
		case 0x3f:
			if(!cpu->regs.a0) {
				fputs("<NULL>", stderr);
				break;
			}

			char* str = (char*)&cpu->sys->memory->phys[cpu->regs.a0 & 0x1fffff];
			fprintf(stderr, "%s\r\n", str);
			break;

		}
	}
#endif

	// jr $ra on a shell jump skips the intro and boots straight into the disc, 
	// it can only be done once per reset, the kernel assumes the disc is inserted
	/*
	if(cpu->regs.pc == 0x80030000) {
		cpu->regs.pc = cpu->regs.ra;
		cpu->next_pc = cpu->regs.pc + 4;
		return;
	}
	*/

	cpu->regs.pc = cpu->next_pc;
	cpu->next_pc += 4;
	if(cpu_check_irqs(cpu)) {
		if((current_instruction & 0xfe000000) == 0x4a000000) {
			gte_run_cmd(cpu, current_instruction);
		}

		enter_exception(cpu, PSX_CPU_EXCAUSE_INT);
		return;
	}
	execute(cpu, current_instruction);
	if(cpu->load_slot.delay) {
		cpu->load_slot.delay = false;
	} else if(cpu->load_slot.target != 0) {
		cpu->regs.r[cpu->load_slot.target] = cpu->load_slot.value;
		cpu->load_slot.target = 0;
	}
}
