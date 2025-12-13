#ifndef PSX_SCHED_H
#define PSX_SCHED_H

#include <psx/system.h>

#include <stdint.h>

enum {
	PSX_SEV_ID_HBLANK,
	PSX_SEV_ID_HBLANK_END,
	PSX_SEV_ID_VBLANK,
	PSX_SEV_ID_DMAEND,
};

struct psx_sev;
struct psx_sched;

typedef void (*psx_evcb_t)(struct psx_sched* sched, struct psx_sev* self);

struct psx_sev {
	uint8_t id;
	uint32_t eta;
	uint32_t clocks_left;
	psx_evcb_t trigger;
	struct psx_sev* next;
};

struct psx_sched {
	uint32_t clocks_elapsed;
	struct psx_system* sys;
	struct psx_sev* ev_list;
};

void psx_sched_init(struct psx_sched* sched);
void psx_sched_add_ev(struct psx_sched* sched, struct psx_sev* ev);
void psx_sched_remove_ev(struct psx_sched* sched, uint8_t id);
void psx_sched_update(struct psx_sched* sched, uint32_t clocks);

#endif // #ifndef PSX_SCHED_H

