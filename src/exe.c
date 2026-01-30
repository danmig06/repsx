#include <psx/exe.h>
#include <psx/memory.h>
#include <psx/cpu.h>
#include "log.h"
#define NUM_ARGS 2

bool psx_exe_open(struct psx_exe* exe, const char* filename) {
	exe->file = fopen(filename, "rb");
	if(!exe->file) {
		log_error("failed to open exe file at \"%s\"", filename);
		exe->is_loaded = false;
		return false;
	}

	if(!fread(&exe->hdr, 1, sizeof(exe->hdr), exe->file)) {
		return false;
	}

	fseek(exe->file, PSX_EXE_PROG_START_OFF, SEEK_SET);
	exe->is_loaded = false;
	return true;
}

void psx_exe_load(struct psx_exe* exe, struct psx_system* sys) {
	if(exe->is_loaded) {
		return;
	}

	uint32_t load_addr = PSX_MEM_REAL_ADDR(exe->hdr.load_addr);
	if((load_addr + exe->hdr.file_size) > PSX_MEM_PHYS_SIZE) {
		log_error("invalid PS-EXE");
		return;
	}

	sys->cpu->regs.pc = exe->hdr.initial_pc;
	sys->cpu->next_pc = exe->hdr.initial_pc + 4;
	sys->cpu->regs.gp = exe->hdr.initial_gp;

	if(exe->hdr.initial_stack_base != 0) {
		sys->cpu->regs.fp = exe->hdr.initial_stack_base;
		sys->cpu->regs.sp = sys->cpu->regs.fp + exe->hdr.initial_sp_offset;
	}

	exe->is_loaded = fread(&(sys->memory->phys[load_addr]), 1, exe->hdr.file_size, exe->file);
	// setup amidog testrom debug functions
	uint8_t* scratchbuf = sys->memory->scratch;
	int num_args = NUM_ARGS;
	char* args[NUM_ARGS] = { "console", "release" };
	int len = 0;
	uint32_t str_start = 0x1f800044;
	uint32_t str_offset = 0;
	memcpy(scratchbuf, &num_args, 4);
	for(int i = 0; i < NUM_ARGS; i++) {
		len = strlen(args[i]);
		memcpy(&scratchbuf[0x4 + (i * 4)], &str_start, 4);
		memcpy(&scratchbuf[0x44 + str_offset], args[i], len);
		str_start += len + 1;
		str_offset += len + 1;
	}
}

void psx_exe_close(struct psx_exe* exe) {
	if(exe->file) {
		fclose(exe->file);
		exe->file = NULL;
	}
	memset(&exe->hdr, 0, sizeof(exe->hdr));
}

