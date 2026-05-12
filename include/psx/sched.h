#ifndef PSX_SCHED_H
#define PSX_SCHED_H

#include <psx/system.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	PSX_SEV_ID_HBLANK,
	PSX_SEV_ID_HBLANK_END,
	PSX_SEV_ID_VBLANK,
	PSX_SEV_ID_TMR,
	PSX_SEV_ID_DMAEND,
	PSX_SEV_ID_CDROM_RESP1,
	PSX_SEV_ID_CDROM_RESP2,
	PSX_SEV_ID_CDROM_IRQ,
	PSX_SEV_ID_CDROM_DRIVE,
	PSX_SEV_ID_SIO_RESPONSE,
	PSX_SEV_ID_SPU_UPDATE
};

struct psx_sev;
struct psx_sched;

typedef void (*psx_evcb_t)(struct psx_sched* sched, struct psx_sev* self);

struct psx_sev {
	uint8_t id;
	uint32_t eta;
	uint64_t clocks_left;
	psx_evcb_t trigger;
	struct psx_sev* next;
};

struct psx_sched {
	uint64_t clocks_elapsed;
	struct psx_sev* ev_list;
	struct psx_system* sys;
};

void psx_sched_init(struct psx_sched* sched, struct psx_system* sys);
void psx_sched_add_ev(struct psx_sched* sched, struct psx_sev* ev);
void psx_sched_remove_ev(struct psx_sched* sched, uint8_t id);
void psx_sched_update(struct psx_sched* sched, uint32_t clocks);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_SCHED_H

