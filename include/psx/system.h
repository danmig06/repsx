#ifndef PSX_SYSTEM_H
#define PSX_SYSTEM_H

#include <psx/disc.h>

#include <stdio.h>
#include <stdbool.h>

#define PSX_NO_DISC NULL

#ifdef __cplusplus
extern "C" {
#endif

enum {
	PSX_LOG_LEVEL_TRACE,
	PSX_LOG_LEVEL_DEBUG,
	PSX_LOG_LEVEL_INFO,
	PSX_LOG_LEVEL_WARN,
	PSX_LOG_LEVEL_ERROR,
	PSX_LOG_LEVEL_FATAL,
	PSX_LOG_LEVEL_DISABLE,
};

enum {
	PSX_SIG_TRAY_OPEN,
	PSX_SIG_TRAY_CLOSED,
};

// needed for pad drivers
enum {
	PSX_PADBTN_SELECT   = (1 << 0),
	PSX_PADBTN_L3       = (1 << 1),
	PSX_PADBTN_R3       = (1 << 2),
	PSX_PADBTN_START    = (1 << 3),
	PSX_PADBTN_UP       = (1 << 4),
	PSX_PADBTN_RIGHT    = (1 << 5),
	PSX_PADBTN_DOWN     = (1 << 6),
	PSX_PADBTN_LEFT     = (1 << 7),
	PSX_PADBTN_L2       = (1 << 8),
	PSX_PADBTN_R2       = (1 << 9),
	PSX_PADBTN_L1       = (1 << 10),
	PSX_PADBTN_R1       = (1 << 11),
	PSX_PADBTN_TRIANGLE = (1 << 12),
	PSX_PADBTN_CIRCLE   = (1 << 13),
	PSX_PADBTN_CROSS    = (1 << 14),
	PSX_PADBTN_SQUARE   = (1 << 15),
};
typedef uint16_t (*psx_padpollfn_t)(void*);

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
	struct psx_mdec* mdec;
	struct psx_sched* sched;
	struct psx_exe* current_exe;
};

void psx_set_log_level(int level);

struct psx_system* psx_system_alloc(void);
bool psx_system_init(struct psx_system* sys, const char* bios_path);
void psx_system_uninit(struct psx_system* sys);
void psx_system_add_pad(struct psx_system* sys, int port, void* host_data, psx_padpollfn_t pollfn);
void psx_system_remove_dev(struct psx_system* sys, int port);
void psx_system_add_mcd(struct psx_system* sys, int port, void* host_data, psx_buwritefn_t write_fn, psx_bureadfn_t read_fn);
void psx_system_remove_mcd(struct psx_system* sys, int port);

void psx_system_signal(struct psx_system* sys, int sig);
void psx_system_set_disc(struct psx_system* sys, struct psx_disc* disc);

void psx_system_update(struct psx_system* sys);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_SYSTEM_H
