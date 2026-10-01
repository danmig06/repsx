#include <psx/timer.h>
#include <psx/irq.h>
#include <psx/cpu.h>
#include <psx/gpu.h>
#include <psx/sched.h>

#include "util.h"
#include "rdef/timer.h"

#include <string.h>

// This controls a hack for Parasite Eve 2, it may break some games.
// the CPU is running at 2 Clocks Per Instruction, however, there is a point in PE2's code
// which runs some kind of texture decoding algorithm and times it using timer2 (running at 1/8 clock rate),
// at 2 CPI, the CPU simply takes too long to decode the data, leading to infinite retries,
// we can try to unscale the timer2 value to prevent this timeout, which won't really solve the problem at its root,
// the game might be relying on the icache to speed up the code, so implementing that might solve the issue.
#define PE2_HACK 1

#define CHECK_TARGET(tmr, i, ts) do { \
		if((ts - tmr->tstatus[i].start_ts) >= tmr->tstatus[i].end_ts) { \
			handle_target(tmr, i); \
		} \
	} while(0)

struct timer_ev {
	struct psx_sev ev;
	bool is_active;
	int index;
};

static void tmr_update(struct psx_timer* tmr);

static void tmr_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	struct timer_ev* self = (void*)_self;
	// printf("timer%d completed, updating...\n", self->index);
	self->is_active = false;
	self->index = -1;
	psx_sched_remove_ev(sched, _self->id);

	tmr_update(sched->sys->timer);
}

static struct timer_ev timer_event = {
	.ev = { .id = PSX_SEV_ID_TMR, .trigger = tmr_evcb },
	.is_active = false,
	.index = -1
};

static inline uint64_t get_clocks(struct psx_timer* tmr) {
	return tmr->sys->sched->clocks_elapsed;
}

static void schedule_next(struct psx_timer* tmr) {
	uint64_t ts = get_clocks(tmr);
	uint64_t next = INT64_MAX;
	int64_t eta;
	int selected_timer = 0;
	for(int i = 0; i < 3; i++) {
		eta = tmr->tstatus[i].end_ts - (ts - tmr->tstatus[i].start_ts);
		if(eta < 0) {
			next = 0;
			selected_timer = i;
			break;
		}

		if(eta < (int64_t)next) {
			next = eta;
			selected_timer = i;
		}
	}

	if(timer_event.is_active) {
		psx_sched_remove_ev(tmr->sys->sched, timer_event.ev.id);
		// printf("canceling and ");
	}

	// printf("scheduling timer%d (mode=%d) in %ld ticks\n", selected_timer, tmr->t[selected_timer].mode.sync_mode, next);
	timer_event.ev.eta = next;
	timer_event.is_active = true;
	timer_event.index = selected_timer;
	psx_sched_add_ev(tmr->sys->sched, &timer_event.ev);
}

static uint32_t getcount(struct psx_timer* tmr, uint32_t i) {
	return (get_clocks(tmr) - tmr->tstatus[i].start_ts) / tmr->tstatus[i].rate;
}

static void setcount(struct psx_timer* tmr, uint32_t i, uint32_t val) {
	tmr->tstatus[i].start_ts = get_clocks(tmr) - (val * tmr->tstatus[i].rate);

	if(val < tmr->t[i].target) {
		tmr->tstatus[i].end_ts = tmr->t[i].target * tmr->tstatus[i].rate;
		// printf("timer%d set to 0x%04x, target=0x%04x\n", i, val, tmr->t[i].target);
		tmr->tstatus[i].count_to_target = true;
	} else {
		tmr->tstatus[i].end_ts = 0xffff * tmr->tstatus[i].rate;
		// printf("timer%d set to 0x%04x, target=0xffff\n", i, val);
		tmr->tstatus[i].count_to_target = false;
	}
}

void psx_tmr_init(struct psx_timer* tmr, struct psx_system* sys) {
	memset(tmr, 0, sizeof(*tmr));
	tmr->sys = sys;
	tmr->tstatus[0].rate = 1;
	tmr->tstatus[1].rate = 1;
	tmr->tstatus[2].rate = 1;
	setcount(tmr, 0, 0);
	setcount(tmr, 1, 0);
	setcount(tmr, 2, 0);
	schedule_next(tmr);
}

static void handle_target(struct psx_timer* tmr, int i) {
	bool irq = false;
	uint32_t count = getcount(tmr, i);

	if(tmr->tstatus[i].count_to_target) {
		tmr->t[i].mode |= MODE_TARGET_REACHED;
		if(tmr->t[i].mode & MODE_RESET_ON_TARGET) {
			count -= tmr->t[i].target;
		}
		if(tmr->t[i].mode & MODE_IRQ_ON_TARGET) {
			irq = true;
		}
	} else {
		tmr->t[i].mode |= MODE_MAX_REACHED;
		count -= 0xffff;
		if(tmr->t[i].mode & MODE_IRQ_ON_MAX) {
			irq = true;
		}
	}
	setcount(tmr, i, count & 0xffff);
	schedule_next(tmr);

	if(!irq) {
		return;
	}

	if(tmr->t[i].mode & MODE_IRQ_TOGGLE) {
		tmr->t[i].mode ^= MODE_NO_IRQ;
	} else {
		tmr->t[i].mode &= ~MODE_NO_IRQ;
	}

	bool trigger = (tmr->t[i].mode & MODE_NO_IRQ) == 0;
	if(!(tmr->t[i].mode & MODE_IRQ_REPEAT)) {
		if(trigger && !tmr->tstatus[i].irq_triggered) {
			tmr->tstatus[i].irq_triggered = true;
		} else {
			return;
		}
	}

	tmr->t[i].mode |= MODE_NO_IRQ;

	if(trigger) {
		// log_trace("timer%d: IRQ triggered\n", i);
		psx_irq_raise(tmr->sys->irq, PSX_IRQ_ID_TMR0 << i);
	}
}

static void tmr_update(struct psx_timer* tmr) {
	uint64_t ts = get_clocks(tmr);
	
	CHECK_TARGET(tmr, 0, ts);
	CHECK_TARGET(tmr, 1, ts);
	CHECK_TARGET(tmr, 2, ts);
}

void psx_tmr_hsync(struct psx_timer* tmr) {
	if((tmr->t[0].mode & MODE_SYNC_EN) == 0) {
		return;
	}
	switch(MODE_SYNC_GET(tmr->t[0].mode)) {
	case 1:
	case 2:
		setcount(tmr, 0, 0);
		schedule_next(tmr);
		break;
	case 3:
		tmr->t[0].mode &= ~MODE_SYNC_EN;
		break;
	default:
		break;
	}
}

void psx_tmr_vsync(struct psx_timer* tmr) {
	if((tmr->t[1].mode & MODE_SYNC_EN) == 0) {
		return;
	}
	switch(MODE_SYNC_GET(tmr->t[1].mode)) {
	case 1:
	case 2:
		setcount(tmr, 1, 0);
		schedule_next(tmr);
		break;
	case 3:
		tmr->t[1].mode &= ~MODE_SYNC_EN;
		break;
	default:
		break;
	}
}

static inline void update_mode(struct psx_timer* tmr, uint32_t idx) {
	tmr->t[idx].mode &= ~(MODE_TARGET_REACHED | MODE_MAX_REACHED);
	tmr->t[idx].mode |= MODE_NO_IRQ;
	tmr->tstatus[idx].irq_triggered = false;

	int clk_source = MODE_CLK_SRC_GET(tmr->t[idx].mode);
	switch(idx) {
	case 0:
		if(clk_source & 1) {
			tmr->tstatus[idx].rate = 5;
		} else {
			tmr->tstatus[idx].rate = 1;
		}
		break;
	case 1:
		if(clk_source & 1) {
			// causes slightly faster speeds for games like Silent Hill, 
			// values like 2048 bring it closer to normal speeds
			tmr->tstatus[idx].rate = 2048; // 1629; // PSX_GPU_CLOCKS_PER_HBLANK;
		} else {
			tmr->tstatus[idx].rate = 1;
		}
		break;
	case 2:
		if(clk_source & 2) {
			tmr->tstatus[idx].rate = 8;
		} else {
			tmr->tstatus[idx].rate = 1;
		}

		if(tmr->t[idx].mode & MODE_SYNC_EN) {
			int sync_mode = MODE_SYNC_GET(tmr->t[idx].mode);
			if(sync_mode == 0 || sync_mode == 3) {
				tmr->tstatus[idx].rate = 0xffffff;
			} else {
				tmr->t[idx].mode &= ~MODE_SYNC_EN;
			}
		}
		break;
	default: UNREACHABLE();
	}

	setcount(tmr, idx, 0);
}

static uint32_t tmr_read(struct psx_timer* tmr, uint32_t off) {
	int idx = (off >> 4) & 3;
	if(idx >= 3) {
		return 0;
	}
	uint32_t val = 0;

	switch(off & 0xf) {
	case 0:
		tmr_update(tmr);
		int sync_mode = MODE_SYNC_GET(tmr->t[idx].mode);
		if(idx == 2) {
			if((tmr->t[idx].mode & MODE_SYNC_EN) && (sync_mode == 0 || sync_mode == 3)) {
				val = tmr->t[idx].base;
			} else {
				val = getcount(tmr, idx);
#if PE2_HACK
				if(tmr->tstatus[idx].count_to_target && (tmr->t[idx].mode & 0x2ff) == 0x248) {
					val /= PSX_CPU_CPI;
				}
#endif
			}
		} else {
			val = getcount(tmr, idx);
		}
		// log_debug("timer%d read value -> 0x%08x", idx, val);
		val &= 0xffff;
		break;
	case 4:
		tmr_update(tmr);
		val = tmr->t[idx].mode;
		tmr->t[idx].mode &= ~(MODE_TARGET_REACHED | MODE_MAX_REACHED);
		break;
	case 8:
		val = tmr->t[idx].target;
		break;
	default:
		break;
	}

	return val;
}

static void tmr_write(struct psx_timer* tmr, uint32_t off, uint32_t val) {
	int idx = (off >> 4) & 3;
	if(idx >= 3) {
		return;
	}

	tmr_update(tmr);
	switch(off & 0xf) {
	case 0:
		// printf("timer%d write value -> 0x%08x\n", off >> 4, val);
		tmr->t[idx].base = val & 0xffff;
		setcount(tmr, idx, tmr->t[idx].base);
		break;
	case 4:
		// printf("timer%d write mode -> 0x%08x\n", off >> 4, val);
		tmr->t[idx].mode = (tmr->t[idx].mode & 0xffffc00) | (val & 0x3ff);
		update_mode(tmr, idx);
		break;
	case 8:
		// printf("timer%d write target -> 0x%08x\n", off >> 4, val);
		tmr->t[idx].target = val & 0xffff;
		setcount(tmr, idx, getcount(tmr, idx));
		break;
	default:
		break;
	}
	schedule_next(tmr);
}

uint32_t psx_tmr_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	uint32_t val = tmr_read(reg->peripheral, register_offset);
	/*
	char* regname;
	switch(register_offset & 0xf) {
	case 0:
		regname = "value";
		break;
	case 4:
		regname = "mode";
		break;
	case 8:
		regname = "target";
		break;
	default:
		regname = "<invalid>";
		break;
	}
	log_debug("timer%d read %s <- 0x%08x", register_offset / 0x10, regname, val);
	*/
	return val;
}

void psx_tmr_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	tmr_write(reg->peripheral, register_offset, val);
}

uint16_t psx_tmr_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	uint16_t val = tmr_read(reg->peripheral, register_offset);
	/*
	char* regname;
	switch(register_offset & 0xf) {
	case 0:
		regname = "value";
		break;
	case 4:
		regname = "mode";
		break;
	case 8:
		regname = "target";
		break;
	default:
		regname = "<invalid>";
		break;
	}
	log_debug("timer%d read %s <- 0x%04x", register_offset / 0x10, regname, val);
	*/
	return val;
}

void psx_tmr_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	tmr_write(reg->peripheral, register_offset, val);
}

