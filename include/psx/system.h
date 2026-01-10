#ifndef PSX_SYSTEM_H
#define PSX_SYSTEM_H

#include <psx/disc.h>

#include <stdio.h>
#include <stdbool.h>

enum {
	PSX_LOG_LEVEL_TRACE,
	PSX_LOG_LEVEL_DEBUG,
	PSX_LOG_LEVEL_INFO,
	PSX_LOG_LEVEL_WARN,
	PSX_LOG_LEVEL_ERROR,
	PSX_LOG_LEVEL_FATAL,
	PSX_LOG_LEVEL_DISABLE,
};

// needed for pad drivers
typedef struct psx_btnstate {
	bool select: 1;
	bool L3: 1;
	bool R3: 1;
	bool start: 1;
	bool up: 1;
	bool right: 1;
	bool down: 1;
	bool left: 1;
	bool L2: 1;
	bool R2: 1;
	bool L1: 1;
	bool R1: 1;
	bool triangle: 1;
	bool circle: 1;
	bool cross: 1;
	bool square: 1;
} psx_btnstate_t;
typedef psx_btnstate_t (*psx_padpollfn_t)(void*, int);

// TODO: move these elsewhere, make a public memcard API, maybe with filesystem parsing
#define PSX_BU_SECTOR_SIZE 128
typedef void (*psx_buwritefn_t)(void*, uint32_t addr, const uint8_t* buf);
typedef void (*psx_bureadfn_t)(void*, uint32_t addr, uint8_t* buf);

struct psx_system {
	struct psx_cpu* cpu;
	struct psx_mem* memory;
	struct psx_bios* bios;
	struct psx_memctl* mc;
	struct psx_gpu* gpu;
	struct psx_dmac* dmac;
	struct psx_cdrom* cdrom;
	struct psx_irq* irq;
	struct psx_sio* sio;
	struct psx_timer* timer;
	struct psx_spu* spu;
	struct psx_sched* sched;
	struct psx_exe* current_exe;
};

void psx_set_log_level(int level);

bool psx_system_init(struct psx_system* sys, const char* bios_path);
void psx_system_add_pad(struct psx_system* sys, int port, void* host_data, psx_padpollfn_t pollfn);
void psx_system_remove_pad(struct psx_system* sys, int port);
void psx_system_add_mcd(struct psx_system* sys, int port, void* host_data, psx_buwritefn_t write_fn, psx_bureadfn_t read_fn);
void psx_system_remove_mcd(struct psx_system* sys, int port);

void psx_system_set_tray_open(struct psx_system* sys, bool opened);
void psx_system_insert_disc(struct psx_system* sys, struct psx_disc* disc);
void psx_system_eject_disc(struct psx_system* sys);

void psx_system_update(struct psx_system* sys);
void psx_system_start(struct psx_system* sys);

#endif // #ifndef PSX_SYSTEM_H
