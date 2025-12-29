#ifndef PSX_TIMER_H
#define PSX_TIMER_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdbool.h>
#include <stdint.h>

struct psx_ctr {
	uint32_t base;
	struct {
		bool sync_enable: 1;
		uint32_t sync_mode: 2;
		bool reset_on_target: 1;
		bool irq_on_target: 1;
		bool irq_on_max: 1;
		bool irq_repeat: 1;
		bool irq_toggle: 1;
		uint32_t clk_source: 2;
		bool no_irq: 1;
		bool target_reached: 1;
		bool max_reached: 1;
		uint32_t unk: 3;
		uint16_t pad;
	} mode;
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
void psx_tmr_tick(struct psx_timer* tmr, int clocks);
// vblank/hblank tick events
void psx_tmr_hblank(struct psx_timer* tmr); 
void psx_tmr_hblank_end(struct psx_timer* tmr, bool vblank_end); 
void psx_tmr_vblank(struct psx_timer* tmr); 

uint32_t psx_tmr_read32(struct psx_region* reg, uint32_t addr);
void psx_tmr_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_tmr_read16(struct psx_region* reg, uint32_t addr);
void psx_tmr_write16(struct psx_region* reg, uint32_t addr, uint16_t val);

#endif // #ifndef PSX_TIMER_H

