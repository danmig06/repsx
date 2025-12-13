#ifndef PSX_EXE_H
#define PSX_EXE_H

#include <psx/system.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PSX_EXE_PROG_START_OFF 0x800

struct psx_exe_header {
	char ident[16];
	uint32_t initial_pc;
	uint32_t initial_gp;
	uint32_t load_addr;
	uint32_t file_size;
	uint32_t unknown0;
	uint32_t unknown1;
	uint32_t mem_fill_start;
	uint32_t mem_fill_size;
	uint32_t initial_stack_base;
	uint32_t initial_sp_offset;
};

struct psx_exe {
	struct psx_exe_header hdr;
	bool is_loaded;
	FILE* file;
};

bool psx_exe_open(struct psx_exe* exe, const char* filename);
void psx_exe_load(struct psx_exe* exe, struct psx_system* sys);
void psx_exe_close(struct psx_exe* exe);

#endif // #ifndef PSX_EXE_H

