#ifndef PSX_TIMER_H
#define PSX_TIMER_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct __attribute__((packed)) psx_ctr  {
	uint32_t base;
	uint16_t mode;
	uint32_t target;
	uint32_t pad;
};

struct psx_timer {
	struct psx_ctr t[3];

	struct {
		uint32_t rate;
		bool irq_triggered;
		bool count_to_target;
		uint64_t start_ts, end_ts;
	} tstatus[3];
	struct psx_system* sys;
};

void psx_tmr_init(struct psx_timer* tmr);
// vblank/hblank tick events
void psx_tmr_hsync(struct psx_timer* tmr); 
void psx_tmr_vsync(struct psx_timer* tmr); 

uint32_t psx_tmr_read32(struct psx_region* reg, uint32_t addr);
void psx_tmr_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_tmr_read16(struct psx_region* reg, uint32_t addr);
void psx_tmr_write16(struct psx_region* reg, uint32_t addr, uint16_t val);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_TIMER_H

