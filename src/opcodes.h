#ifndef PSX_OPCODES_H
#define PSX_OPCODES_H

#include <stdint.h>

#define FUNC(i) (i >> 26)
#define SUBFUNC(i) (i & 0x3f)
#define COPFUNC(i) ((i >> 21) & 0x1f)
#define IMM_SE(i) ((int16_t)(i & 0xffff))
#define IMM(i) (i & 0xffff)
#define ADDR(i) (i & 0x3ffffff)
#define A(i) ((i >> 6) & 0x1f)
#define D(i) ((i >> 11) & 0x1f)
#define T(i) ((i >> 16) & 0x1f)
#define S(i) ((i >> 21) & 0x1f)

#define COP_NUM(i) (FUNC(i) & 0x3)

enum opcode_t {
	OP_SPECIAL   = 0b000000,
	OP_COPMASK   = 0b010000,

	OP_BXX       = 0b000001,
	OP_J         = 0b000010,
	OP_JAL       = 0b000011, 

	OP_BEQ       = 0b000100,
	OP_BNE       = 0b000101,
	OP_BLEZ      = 0b000110,
	OP_BGTZ      = 0b000111,

	OP_ADDI      = 0b001000,
	OP_ADDIU     = 0b001001,
	OP_SLTI      = 0b001010,
	OP_SLTIU     = 0b001011,

	OP_ANDI      = 0b001100,
	OP_ORI       = 0b001101,
	OP_XORI      = 0b001110,
	OP_LUI       = 0b001111,

	OP_LB        = 0b100000,
	OP_LH        = 0b100001,
	OP_LWL       = 0b100010,
	OP_LW        = 0b100011,
	OP_LBU       = 0b100100,
	OP_LHU       = 0b100101,
	OP_LWR       = 0b100110,

	OP_SB        = 0b101000,
	OP_SH        = 0b101001,
	OP_SWL       = 0b101010,
	OP_SW        = 0b101011,
	OP_SWR       = 0b101110,
};

enum subfunc_t {
	SFNC_SLL     = 0b000000, 
	SFNC_SLLV    = 0b000100,
	SFNC_SRL     = 0b000010,
	SFNC_SRLV    = 0b000110,
	SFNC_SRA     = 0b000011,
	SFNC_SRAV    = 0b000111,

	SFNC_JR      = 0b001000,
	SFNC_JALR    = 0b001001,

	SFNC_SYSCALL = 0b001100,
	SFNC_BREAK   = 0b001101,

	SFNC_MFHI    = 0b010000,
	SFNC_MTHI    = 0b010001,
	SFNC_MFLO    = 0b010010,
	SFNC_MTLO    = 0b010011,

	SFNC_MULT    = 0b011000,
	SFNC_MULTU   = 0b011001,
	SFNC_DIV     = 0b011010,
	SFNC_DIVU    = 0b011011,

	SFNC_ADD     = 0b100000,
	SFNC_ADDU    = 0b100001,
	SFNC_SUB     = 0b100010,
	SFNC_SUBU    = 0b100011,

	SFNC_AND     = 0b100100,
	SFNC_OR      = 0b100101,
	SFNC_XOR     = 0b100110,
	SFNC_NOR     = 0b100111,

	SFNC_SLT     = 0b101010,
	SFNC_SLTU    = 0b101011
};

enum copfunc_t {
	CPFC_MFCN = 0b000000,
	CPFC_CFCN = 0b000010,
	CPFC_MTCN = 0b000100,
	CPFC_CTCN = 0b000110,
	CPFC_BCNC = 0b001000,
	CPFC_LWCN = 0b110000,
	CPFC_SWCN = 0b111000,
	CPFC_RFE  = 0b010000
};

extern bool do_disasm;

void enter_exception(struct psx_cpu* cpu, uint32_t cause);

void ill(struct psx_cpu* cpu, uint32_t insn);
void lui(struct psx_cpu* cpu, uint32_t insn);
void ori(struct psx_cpu* cpu, uint32_t insn);
void sw(struct psx_cpu* cpu, uint32_t insn);
void sll(struct psx_cpu* cpu, uint32_t insn);
void j(struct psx_cpu* cpu, uint32_t insn);
void addiu(struct psx_cpu* cpu, uint32_t insn);
void or(struct psx_cpu* cpu, uint32_t insn);
void mtcn(struct psx_cpu* cpu, uint32_t insn);
void bne(struct psx_cpu* cpu, uint32_t insn);
void addi(struct psx_cpu* cpu, uint32_t insn);
void lw(struct psx_cpu* cpu, uint32_t insn);
void jr(struct psx_cpu* cpu, uint32_t insn);
void sltu(struct psx_cpu* cpu, uint32_t insn);
void addu(struct psx_cpu* cpu, uint32_t insn);
void sh(struct psx_cpu* cpu, uint32_t insn);
void jal(struct psx_cpu* cpu, uint32_t insn);
void andi(struct psx_cpu* cpu, uint32_t insn);
void sb(struct psx_cpu* cpu, uint32_t insn);
void lb(struct psx_cpu* cpu, uint32_t insn);
void beq(struct psx_cpu* cpu, uint32_t insn);
void mfcn(struct psx_cpu* cpu, uint32_t insn);
void add(struct psx_cpu* cpu, uint32_t insn);
void and(struct psx_cpu* cpu, uint32_t insn);
void bgtz(struct psx_cpu* cpu, uint32_t insn);
void blez(struct psx_cpu* cpu, uint32_t insn);
void bxx(struct psx_cpu* cpu, uint32_t insn);
void lbu(struct psx_cpu* cpu, uint32_t insn);
void jalr(struct psx_cpu* cpu, uint32_t insn);
void slti(struct psx_cpu* cpu, uint32_t insn);
void sub(struct psx_cpu* cpu, uint32_t insn);
void subu(struct psx_cpu* cpu, uint32_t insn);
void sra(struct psx_cpu* cpu, uint32_t insn);
void sltiu(struct psx_cpu* cpu, uint32_t insn);
void slt(struct psx_cpu* cpu, uint32_t insn);
void ediv(struct psx_cpu* cpu, uint32_t insn);
void mflo(struct psx_cpu* cpu, uint32_t insn);
void srl(struct psx_cpu* cpu, uint32_t insn);
void divu(struct psx_cpu* cpu, uint32_t insn);
void mfhi(struct psx_cpu* cpu, uint32_t insn);
void scall(struct psx_cpu* cpu, uint32_t insn);
void mtlo(struct psx_cpu* cpu, uint32_t insn);
void mthi(struct psx_cpu* cpu, uint32_t insn);
void rfe(struct psx_cpu* cpu, uint32_t insn);
void lhu(struct psx_cpu* cpu, uint32_t insn);
void sllv(struct psx_cpu* cpu, uint32_t insn);
void lh(struct psx_cpu* cpu, uint32_t insn);
void nor(struct psx_cpu* cpu, uint32_t insn);
void srav(struct psx_cpu* cpu, uint32_t insn);
void srlv(struct psx_cpu* cpu, uint32_t insn);
void multu(struct psx_cpu* cpu, uint32_t insn);
void xor(struct psx_cpu* cpu, uint32_t insn);
void xori(struct psx_cpu* cpu, uint32_t insn);
void bk(struct psx_cpu* cpu, uint32_t insn);
void mult(struct psx_cpu* cpu, uint32_t insn);
void lwr(struct psx_cpu* cpu, uint32_t insn);
void lwl(struct psx_cpu* cpu, uint32_t insn);
void swr(struct psx_cpu* cpu, uint32_t insn);
void swl(struct psx_cpu* cpu, uint32_t insn);
void cfcn(struct psx_cpu* cpu, uint32_t insn);
void ctcn(struct psx_cpu* cpu, uint32_t insn);
void lwcn(struct psx_cpu* cpu, uint32_t insn);
void swcn(struct psx_cpu* cpu, uint32_t insn);

#endif // #ifndef PSX_OPCODES_H
