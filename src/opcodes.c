#include <psx/cpu.h>
#include <psx/memory.h>

#include "util.h"
#include "log.h"
#include "opcodes.h"
#include "gte.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#ifdef PSX_DO_DISASM
#define disasm(fmt, ...) \
	do { \
		if(do_disasm) \
			printf("0x%08x: "fmt "\n", cpu->saved_pc, ##__VA_ARGS__); \
	} while(0)
#else
#define disasm(fmt, ...)
#endif

bool do_disasm = false;

#define REG(idx) psx_cpu_get_reg(cpu, idx)
#define SET_REG(idx, val) psx_cpu_set_reg(cpu, idx, val)

#define COP_REG(idx) get_cop_register(cpu, insn, idx)
uint32_t get_cop_register(struct psx_cpu* cpu, uint32_t insn, uint32_t idx) {
	switch(COP_NUM(insn)) {
	case 0:
		return psx_cpu_get_cop0_reg(cpu, idx);
	case 2:
		return gte_read_register(cpu, idx);
	default:
		panic("0x%08x: invalid coprocessor target", cpu->regs.pc);
		return 0;
	}
}

#define SET_COP_REG(idx, val) set_cop_register(cpu, insn, idx, val)
void set_cop_register(struct psx_cpu* cpu, uint32_t insn, uint32_t idx, uint32_t val) { 
	switch(COP_NUM(insn)) { 
	case 0: 
		psx_cpu_set_cop0_reg(cpu, idx, val);
		break;
	case 2:
		gte_write_register(cpu, idx, val);
		break;
	default:
		panic("invalid coprocessor target");
		break;
	}
}

static void do_branch(struct psx_cpu* cpu, int32_t offset) {
	cpu->branch_taken = true;
	uint32_t loc = cpu->next_pc + (offset * 4);
	cpu->next_pc = loc - 4;
}

static bool check_overflow(int a, int b) {
	int64_t r = a + b;
	if(r < INT_MIN) {
		return true;
	}
	if(r > INT_MAX) {
		return true;
	}
	return false;
}

static void prepare_load(struct psx_cpu* cpu, unsigned target_reg, uint32_t value) {
	cpu->load_slot.delay = true;
	cpu->load_slot.target = target_reg;
	cpu->load_slot.value = value;
}

void enter_exception(struct psx_cpu* cpu, uint32_t cause) {
	uint32_t handler = (cpu->cop0_regs.sr.bev) ? 0xbfc00180 : 0x80000080;

	AS_UINT32(cpu->cop0_regs.cause) = cause << 2;
	cpu->cop0_regs.epc = cpu->saved_pc;

	if(cpu->branch_delay) {
		cpu->cop0_regs.epc -= 4;
		// set the BD flag
		cpu->cop0_regs.cause.bd = true;
		// set the BT flag
		cpu->cop0_regs.cause.bt = cpu->branch_taken;
	}

	// "push" next interrupt mode in the SR
	uint32_t sr = AS_UINT32(cpu->cop0_regs.sr);
	uint32_t prev_mode = sr & 0x3f;
	sr &= ~(0x3f);
	sr |= (prev_mode << 2) & 0x3f;
	AS_UINT32(cpu->cop0_regs.sr) = sr;

	cpu->regs.pc = handler;
	cpu->next_pc = cpu->regs.pc + 4;
}

void ill(struct psx_cpu* cpu, uint32_t insn) {
	disasm("ill");
	log_fatal("illegal opcode 0x%08x at 0x%08x", insn, cpu->regs.pc);
	enter_exception(cpu, PSX_CPU_EXCAUSE_ILLEGAL);
}

void lui(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lui $%u, 0x%04x", T(insn), IMM(insn));
	SET_REG(T(insn), IMM(insn) << 16);
}

void ori(struct psx_cpu* cpu, uint32_t insn) {
	disasm("ori $%u, $%u, 0x%04x", T(insn), S(insn), IMM(insn));
	SET_REG(T(insn), REG(S(insn)) | IMM(insn));
}

void sw(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sw $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->cop0_regs.sr.isc == true) {
		return;
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	if(addr % 4) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_STOR_ERR);
		return;
	}
	psx_mem_write32(cpu->sys->memory, addr, REG(T(insn)));
}

void sll(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sll $%u, $%u, %u", D(insn), T(insn), A(insn));
	SET_REG(D(insn), REG(T(insn)) << A(insn));
}

void j(struct psx_cpu* cpu, uint32_t insn) {
	cpu->branch = true;
	disasm("j 0x%08x", ADDR(insn));
	cpu->next_pc = (cpu->next_pc & 0xf0000000) | (ADDR(insn) << 2);
}

void addiu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("addiu $%u, $%u, %d", T(insn), S(insn), IMM_SE(insn));
	SET_REG(T(insn), REG(S(insn)) + IMM_SE(insn));
}

void or(struct psx_cpu* cpu, uint32_t insn) {
	disasm("or $%u, $%u, $%u", D(insn), S(insn), T(insn));
	SET_REG(D(insn), REG(S(insn)) | REG(T(insn)));
}

void mtcn(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mtc%u $%u, $%u", COP_NUM(insn), T(insn), D(insn));
	if(COP_NUM(insn) != 0 && COP_NUM(insn) != 2) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_COPERROR);
		return;
	}

	SET_COP_REG(D(insn), REG(T(insn)));
}

void bne(struct psx_cpu* cpu, uint32_t insn) {
	disasm("bne $%u, $%u, %d", S(insn), T(insn), IMM_SE(insn));
	cpu->branch = true;

	if(REG(S(insn)) != REG(T(insn))) {
		do_branch(cpu, IMM_SE(insn));
	}
}

void addi(struct psx_cpu* cpu, uint32_t insn) {
	disasm("addi $%u, $%u, %d", T(insn), S(insn), IMM_SE(insn));

	int32_t val = REG(S(insn));
	int32_t i = IMM_SE(insn);
	int32_t res;
	
	int overflow = __builtin_sadd_overflow(val, i, &res);
	if(overflow) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_OVERFLOW);
		return;
	}

	SET_REG(T(insn), res);
}

void lw(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lw $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	if(addr % 4) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_LOAD_ERR);
		return;
	}
	prepare_load(cpu, T(insn), psx_mem_read32(cpu->sys->memory, addr));
}

void jr(struct psx_cpu* cpu, uint32_t insn) {
	disasm("jr $%u", S(insn));
	cpu->branch = true;
	cpu->next_pc = REG(S(insn));
}

void sltu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sltu $%u, $%u, $%u", D(insn), S(insn), T(insn));
	SET_REG(D(insn), REG(S(insn)) < REG(T(insn)));
}

void addu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("addu $%u, $%u, $%u", D(insn), S(insn), T(insn));
	SET_REG(D(insn), REG(S(insn)) + REG(T(insn)));
}

void sh(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sh $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->cop0_regs.sr.isc == true) {
		return;
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	if(addr % 2) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_STOR_ERR);
		return;
	}
	psx_mem_write16(cpu->sys->memory, addr, REG(T(insn)));
}

void jal(struct psx_cpu* cpu, uint32_t insn) {
	disasm("jal 0x%08x", ADDR(insn));
	cpu->branch = true;
	SET_REG(31, cpu->next_pc);
	cpu->next_pc = (cpu->next_pc & 0xf0000000) | (ADDR(insn) << 2);
}

void andi(struct psx_cpu* cpu, uint32_t insn) {
	disasm("andi $%u, $%u, 0x%04x", T(insn), S(insn), IMM(insn));
	SET_REG(T(insn), REG(S(insn)) & IMM(insn));
}

void sb(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sb $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->cop0_regs.sr.isc == true) {
		return;
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	psx_mem_write8(cpu->sys->memory, addr, REG(T(insn)));
}

void lb(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lb $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	int8_t val = psx_mem_read8(cpu->sys->memory, addr);
	prepare_load(cpu, T(insn), val);
}

void beq(struct psx_cpu* cpu, uint32_t insn) {
	disasm("beq $%u, $%u, %d", S(insn), T(insn), IMM_SE(insn));
	cpu->branch = true;

	if(REG(S(insn)) == REG(T(insn))) {
		do_branch(cpu, IMM_SE(insn));
	}
}

void mfcn(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mfc%u $%u, $%u", COP_NUM(insn), T(insn), D(insn));
	if(COP_NUM(insn) != 0 && COP_NUM(insn) != 2) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_COPERROR);
		return;
	}
 
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	prepare_load(cpu, T(insn), COP_REG(D(insn)));
}

void add(struct psx_cpu* cpu, uint32_t insn) {
	disasm("add $%u, $%u, $%u", D(insn), S(insn), T(insn));

	int32_t s = REG(S(insn));
	int32_t t = REG(T(insn));
	int32_t res;
	
	int overflow = __builtin_sadd_overflow(s, t, &res);
	if(overflow) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_OVERFLOW);
		return;
	}

	SET_REG(D(insn), res);
}

void and(struct psx_cpu* cpu, uint32_t insn) {
	disasm("and $%u, $%u, $%u", D(insn), S(insn), T(insn));
	SET_REG(D(insn), REG(S(insn)) & REG(T(insn)));
}

void bgtz(struct psx_cpu* cpu, uint32_t insn) {
	disasm("bgtz $%u, %d", S(insn), IMM_SE(insn));
	cpu->branch = true;

	int32_t val = REG(S(insn));
	if(val > 0) {
		do_branch(cpu, IMM_SE(insn));
	}
}

void blez(struct psx_cpu* cpu, uint32_t insn) {
	disasm("blez $%u, %d", S(insn), IMM_SE(insn));
	cpu->branch = true;

	int32_t val = REG(S(insn));
	if(val <= 0) {
		do_branch(cpu, IMM_SE(insn));
	}
}

void lbu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lbu $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	prepare_load(cpu, T(insn), psx_mem_read8(cpu->sys->memory, addr));
}

void jalr(struct psx_cpu* cpu, uint32_t insn) {
	disasm("jalr $%u, $%u", D(insn), S(insn));
	cpu->branch = true;
	uint32_t s = REG(S(insn));
	SET_REG(D(insn), cpu->next_pc);
	cpu->next_pc = s;
}

void bxx(struct psx_cpu* cpu, uint32_t insn) {
	bool is_ge = (insn >> 16) & 1;
	bool link = ((insn >> 17) & 0xf) == 8;
	disasm("b%sz%s $%u, %d", (is_ge) ? "ge" : "lt", (link) ? "al" : "", S(insn), IMM_SE(insn));
	cpu->branch = true;

	int32_t v = REG(S(insn));
	bool test = false;
	if(is_ge) {
		test = v >= 0;
	} else {
		test = v < 0;
	}

	if(link) {
		cpu->regs.ra = cpu->next_pc;
	}

	if(test) {
		do_branch(cpu, IMM_SE(insn));
	}
}

void slti(struct psx_cpu* cpu, uint32_t insn) {
	disasm("slti $%u, $%u, %d", T(insn), S(insn), IMM_SE(insn));
	int32_t val = REG(S(insn));
	SET_REG(T(insn), val < IMM_SE(insn));
}

void subu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("subu $%u, $%u, $%u", D(insn), S(insn), T(insn));
	SET_REG(D(insn), REG(S(insn)) - REG(T(insn)));
}

void sra(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sra $%u, $%u, %u", D(insn), T(insn), A(insn));
	int32_t val = REG(T(insn));
	SET_REG(D(insn), val >> A(insn));
}

void sltiu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sltiu $%u, $%u, %u", T(insn), S(insn), IMM_SE(insn));
	uint32_t val = IMM_SE(insn);
	SET_REG(T(insn), REG(S(insn)) < val);
}

void slt(struct psx_cpu* cpu, uint32_t insn) {
	disasm("slt $%u, $%u, $%u", D(insn), S(insn), T(insn));
	int32_t s = REG(S(insn));
	int32_t t = REG(T(insn));
	SET_REG(D(insn), s < t);
}

void ediv(struct psx_cpu* cpu, uint32_t insn) {
	disasm("div $%u, $%u", S(insn), T(insn));
	int32_t n = REG(S(insn));
	int32_t d = REG(T(insn));

	if(d == 0) {
		cpu->regs.hi = n;

		if(n >= 0) {
			cpu->regs.lo = -1;
		} else {
			cpu->regs.lo = 1;
		}
	} else if(REG(S(insn)) == 0x80000000 && d == -1) {
		cpu->regs.hi = 0;
		cpu->regs.lo = 0x80000000;
	} else {
		cpu->regs.hi = n % d;
		cpu->regs.lo = n / d;
	}
}

void mflo(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mflo $%u", D(insn));
	SET_REG(D(insn), cpu->regs.lo);
}

void srl(struct psx_cpu* cpu, uint32_t insn) {
	disasm("srl $%u, $%u, %u", D(insn), T(insn), A(insn));
	SET_REG(D(insn), REG(T(insn)) >> A(insn));
}

void divu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("divu $%u, $%u", S(insn), T(insn));
	uint32_t n = REG(S(insn));
	uint32_t d = REG(T(insn));

	if(d == 0) {
		cpu->regs.hi = n;
		cpu->regs.lo = -1;
	} else {
		cpu->regs.hi = n % d;
		cpu->regs.lo = n / d;
	}
}

void mfhi(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mfhi $%u", D(insn));
	SET_REG(D(insn), cpu->regs.hi);
}

void scall(struct psx_cpu* cpu, uint32_t insn) {
	disasm("syscall");
	enter_exception(cpu, PSX_CPU_EXCAUSE_SYSCALL);	
}

void mtlo(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mtlo $%u", S(insn));
	cpu->regs.lo = REG(S(insn));
}

void mthi(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mthi $%u", S(insn));
	cpu->regs.hi = REG(S(insn));
}

void rfe(struct psx_cpu* cpu, uint32_t insn) {
	disasm("rfe");
	if((insn & 0x3f) != 0b010000) {
		panic("Invalid COP0 instruction 0x%08x", insn);
	}

	// invert the mode changes made by the CPU when handling the current exception
	uint32_t mode = AS_UINT32(cpu->cop0_regs.sr) & 0x3f;
	// old ie and ku bits don't get cleared by rfe
	AS_UINT32(cpu->cop0_regs.sr) &= ~(0xf);
	AS_UINT32(cpu->cop0_regs.sr) |= (mode >> 2) & 0xf;
}

void lhu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lhu $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	if(addr % 2) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_LOAD_ERR);
		return;
	}
	prepare_load(cpu, T(insn), psx_mem_read16(cpu->sys->memory, addr));
}

void sllv(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sllv $%u, $%u, $%u", D(insn), T(insn), S(insn));
	unsigned amount = REG(S(insn)) & 0x1f;
	SET_REG(D(insn), REG(T(insn)) << amount);
}

void lh(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lh $%u, %d($%u)", T(insn), IMM_SE(insn), S(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	if(addr % 2) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_LOAD_ERR);
		return;
	}
	int16_t val = psx_mem_read16(cpu->sys->memory, addr);
	prepare_load(cpu, T(insn), val);
}

void nor(struct psx_cpu* cpu, uint32_t insn) {
	disasm("nor $%u, $%u, $%u", D(insn), T(insn), S(insn));
	uint32_t val = ~(REG(T(insn)) | REG(S(insn)));
	SET_REG(D(insn), val);
}

void srav(struct psx_cpu* cpu, uint32_t insn) {
	disasm("srav $%u, $%u, $%u", D(insn), T(insn), S(insn));
	unsigned amount = REG(S(insn)) & 0x1f;
	int32_t val = REG(T(insn));
	SET_REG(D(insn), val >> amount);
}

void srlv(struct psx_cpu* cpu, uint32_t insn) {
	disasm("srav $%u, $%u, $%u", D(insn), T(insn), S(insn));
	unsigned amount = REG(S(insn)) & 0x1f;
	SET_REG(D(insn), REG(T(insn)) >> amount);
}

void multu(struct psx_cpu* cpu, uint32_t insn) {
	disasm("multu $%u, $%u", S(insn), T(insn));
	uint64_t a = REG(S(insn));
	uint64_t b = REG(T(insn));

	uint64_t result = a * b;
	cpu->regs.hi = (result >> 32);
	cpu->regs.lo = result & 0xffffffff;
}

void xor(struct psx_cpu* cpu, uint32_t insn) {
	disasm("xor $%u, $%u, $%u", D(insn), S(insn), T(insn));
	SET_REG(D(insn), REG(S(insn)) ^ REG(T(insn)));
}

void bk(struct psx_cpu* cpu, uint32_t insn) {
	disasm("break");
	enter_exception(cpu, PSX_CPU_EXCAUSE_BREAK);
}

void mult(struct psx_cpu* cpu, uint32_t insn) {
	disasm("mult $%u, $%u", S(insn), T(insn));

	int64_t a = ((int32_t)REG(S(insn)));
	int64_t b = ((int32_t)REG(T(insn)));
	
	uint64_t result = a * b;
	cpu->regs.hi = result >> 32;
	cpu->regs.lo = result & 0xffffffff;
}

void sub(struct psx_cpu* cpu, uint32_t insn) {
	disasm("sub $%u, $%u, $%u", D(insn), S(insn), T(insn));
	int32_t s = REG(S(insn));
	int32_t t = REG(T(insn));
	int32_t res;
	
	int overflow = __builtin_ssub_overflow(s, t, &res);
	if(overflow) {
		enter_exception(cpu, PSX_CPU_EXCAUSE_OVERFLOW);
		return;
	}

	SET_REG(D(insn), res);
}

void xori(struct psx_cpu* cpu, uint32_t insn) {
	disasm("xori $%u, $%u, 0x%04x", T(insn), S(insn), IMM(insn));
	SET_REG(T(insn), REG(S(insn)) ^ IMM(insn));
}

void lwr(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lwr $%u, 0x%04x($%u)", T(insn), IMM_SE(insn), S(insn));
	uint32_t t = REG(T(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	} else {
		t = cpu->load_slot.value;
	}

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	uint32_t misalignment = addr % 4;
	uint32_t mval = psx_mem_read32(cpu->sys->memory, addr - misalignment);

	int shamt = misalignment * 8;
	uint32_t res_mask = 0xffffff00 << (24 - shamt);
	uint32_t val = (t & res_mask) | (mval >> shamt);

	prepare_load(cpu, T(insn), val);
}

void lwl(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lwl $%u, 0x%04x($%u)", T(insn), IMM_SE(insn), S(insn));
	uint32_t t = REG(T(insn));
	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	} else {
		t = cpu->load_slot.value;
	}
	
	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	uint32_t misalignment = addr % 4;
	uint32_t mval = psx_mem_read32(cpu->sys->memory, addr - misalignment);

	int shamt = misalignment * 8;
	uint32_t res_mask = 0x00ffffff >> shamt;
	uint32_t val = (t & res_mask) | (mval << (24 - shamt));

	prepare_load(cpu, T(insn), val);
}

void swr(struct psx_cpu* cpu, uint32_t insn) {
	disasm("swr $%u, 0x%04x($%u)", T(insn), IMM_SE(insn), S(insn));
	uint32_t t = REG(T(insn));

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	uint32_t misalignment = addr % 4;
	uint32_t mval = psx_mem_read32(cpu->sys->memory, addr - misalignment);

	int shamt = misalignment * 8;
	uint32_t res_mask = 0x00ffffff >> (24 - shamt);
	uint32_t val = (mval & res_mask) | (t << shamt);

	psx_mem_write32(cpu->sys->memory, addr - misalignment, val);
}

void swl(struct psx_cpu* cpu, uint32_t insn) {
	disasm("swl $%u, 0x%04x($%u)", T(insn), IMM_SE(insn), S(insn));
	uint32_t t = REG(T(insn));

	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	uint32_t misalignment = addr % 4;
	uint32_t mval = psx_mem_read32(cpu->sys->memory, addr - misalignment);

	int shamt = misalignment * 8;
	uint32_t res_mask = 0xffffff00 << shamt;
	uint32_t val = (mval & res_mask) | (t >> (24 - shamt));

	psx_mem_write32(cpu->sys->memory, addr - misalignment, val);
}

void cfcn(struct psx_cpu* cpu, uint32_t insn) {
	disasm("cfc%u $%u, $%u", COP_NUM(insn), T(insn), D(insn));
	if(COP_NUM(insn) != 0 && COP_NUM(insn) != 2) {
		log_fatal("cfc%u $%u, $%u", COP_NUM(insn), T(insn), D(insn));
		enter_exception(cpu, PSX_CPU_EXCAUSE_COPERROR);
		return;
	}

	if(cpu->load_slot.target != T(insn)) {
		SET_REG(cpu->load_slot.target, cpu->load_slot.value);
	}

	prepare_load(cpu, T(insn), COP_REG(D(insn) + 32));
}

void ctcn(struct psx_cpu* cpu, uint32_t insn) {
	disasm("ctc%u $%u, $%u", COP_NUM(insn), T(insn), D(insn));
	if(COP_NUM(insn) != 0 && COP_NUM(insn) != 2) {
		log_fatal("ctc%u $%u, $%u", COP_NUM(insn), T(insn), D(insn));
		enter_exception(cpu, PSX_CPU_EXCAUSE_COPERROR);
		return;
	}

	SET_COP_REG(D(insn) + 32, REG(T(insn)));
}

void lwcn(struct psx_cpu* cpu, uint32_t insn) {
	disasm("lwc%d $%d, %d($%d)", COP_NUM(insn), T(insn), IMM_SE(insn), S(insn));
	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	switch(COP_NUM(insn)) {
	case 1:
	case 3:
		log_fatal("0x%08x -> SystemErrorUnresolvedException()\n", cpu->regs.pc - 4);
		exit(0);
		break;
	case 0:
		enter_exception(cpu, PSX_CPU_EXCAUSE_COPERROR);
		break;
	case 2:
		if(addr % 4) {
			enter_exception(cpu, PSX_CPU_EXCAUSE_LOAD_ERR);
			break;
		}
		gte_write_register(cpu, T(insn), psx_mem_read32(cpu->sys->memory, addr));
		break;
	}
}

void swcn(struct psx_cpu* cpu, uint32_t insn) {
	disasm("swc%d $%d, %d($%d)", COP_NUM(insn), T(insn), IMM_SE(insn), S(insn));
	uint32_t addr = REG(S(insn)) + IMM_SE(insn);
	switch(COP_NUM(insn)) {
	case 1:
	case 3:
		log_fatal("0x%08x -> SystemErrorUnresolvedException()\n", cpu->regs.pc - 4);
		exit(0);
		break;
	case 0:
		enter_exception(cpu, PSX_CPU_EXCAUSE_COPERROR);
		break;
	case 2:
		if(addr % 4) {
			enter_exception(cpu, PSX_CPU_EXCAUSE_STOR_ERR);
			break;
		}
		psx_mem_write32(cpu->sys->memory, addr, gte_read_register(cpu, T(insn)));
		break;
	}
}

