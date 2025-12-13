#include <psx/timer.h>
#include <psx/irq.h>

#include <string.h>

#include "util.h"

// TODO: timer rate is slightly inaccurate for almost everything, step is also uneven, don't know if that's a problem
// some of these issues may be causing SMT to read an invalid address and DSSH to drop inputs

void psx_tmr_init(struct psx_timer* tmr) {
	memset(tmr->t, 0, sizeof(tmr->t));
	memset(tmr->tstatus, 0, sizeof(tmr->tstatus));
}

static void handle_target(struct psx_timer* tmr, int i) {
	bool target_reached = tmr->t[i].value > tmr->t[i].target;
	bool max_reached = tmr->t[i].value > 0xffff;
	bool irq = false;

	if(target_reached) {
		tmr->t[i].mode.target_reached = true;
		if(tmr->t[i].mode.reset_on_target) {
			tmr->t[i].value = 0;
		}
		if(tmr->t[i].mode.irq_on_target) {
			irq = true;
		}
	}

	if(max_reached) {
		tmr->t[i].value = 0;
		tmr->t[i].mode.max_reached = true;
		if(tmr->t[i].mode.irq_on_max) {
			irq = true;
		}
	}

	if(!irq) {
		return;
	}

	if(tmr->t[i].mode.irq_toggle) {
		tmr->t[i].mode.no_irq ^= true;
	} else {
		tmr->t[i].mode.no_irq = false;
	}

	log_trace("timer%d: IRQ triggered", i);
	// timings are inaccurate, this might be unsafe for some games, in that case:
	// return;
	bool trigger = !tmr->t[i].mode.no_irq;
	if(!tmr->t[i].mode.irq_repeat) {
		if(trigger && !tmr->tstatus[i].irq_triggered) {
			tmr->tstatus[i].irq_triggered = true;
		} else {
			return;
		}
	}

	tmr->t[i].mode.no_irq = true;

	if(trigger) {
		psx_irq_raise(tmr->sys->irq, PSX_IRQ_ID_TMR0 << i);
	}
}

static inline void tick_timer0(struct psx_timer* tmr, int clocks) {
	if(tmr->t[0].mode.clk_source & 1) {
		// dotclock update
	} else {
		tmr->t[0].value += clocks;
	}

	handle_target(tmr, 0);
}

static inline void tick_timer1(struct psx_timer* tmr, int clocks) {
	// else the hblank event manages that
	if(!(tmr->t[1].mode.clk_source & 1)) {
		tmr->t[1].value += clocks;
	}

	handle_target(tmr, 1);
}

static inline void tick_timer2(struct psx_timer* tmr, int clocks) {
	if((tmr->t[2].mode.clk_source >> 1) != 0) {
		if((tmr->tstatus[2].ctr + clocks) >= 8) {
			tmr->t[2].value += (tmr->tstatus[2].ctr + clocks) / 8;
			tmr->tstatus[2].ctr = (tmr->tstatus[2].ctr + clocks) % 8;
		/*
		} else if(tmr->tstatus[2].ctr >= 8) {
			tmr->tstatus[2].ctr = 0;
			tmr->t[2].value++;
		*/
		} else {
			tmr->tstatus[2].ctr += clocks;
		}
	} else {
		tmr->t[2].value += clocks;
	}

	handle_target(tmr, 2);
}

void psx_tmr_tick(struct psx_timer* tmr, int clocks) {
	if(!tmr->tstatus[0].paused) {
		tick_timer0(tmr, clocks);
	}

	if(!tmr->tstatus[1].paused) {
		tick_timer1(tmr, clocks);
	}

	if(!tmr->tstatus[2].paused) {
		tick_timer2(tmr, clocks);
	}
}

void psx_tmr_hblank(struct psx_timer* tmr) {
	switch(tmr->t[0].mode.sync_mode) {
	case 1:
	case 2:
		tmr->t[0].value = 0;
		break;
	case 3:
		tmr->t[0].value = 0;
		tmr->tstatus[0].paused = false;
		break;
	default:
		tmr->tstatus[0].paused = true;
		break;
	}

	if(!tmr->tstatus[1].paused && tmr->t[1].mode.clk_source & 1) {
		tmr->t[1].value++;
		handle_target(tmr, 1);
	}
}

void psx_tmr_hblank_end(struct psx_timer* tmr, bool vblank_end) {
	if(vblank_end) {
		switch(tmr->t[1].mode.sync_mode) {
		case 0:
			tmr->tstatus[1].paused = true;
			break;
		case 3:
			tmr->tstatus[1].paused = false;
			tmr->t[1].mode.sync_enable = false;
			break;
		}	
	}

	switch(tmr->t[0].mode.sync_mode) {
	case 0:
		tmr->tstatus[0].paused = false;
		break;
	case 2:
		tmr->tstatus[0].paused = true;
		break;
	case 3:
		tmr->t[0].mode.sync_enable = false;
		break;
	default:
		break;
	}
}

void psx_tmr_vblank(struct psx_timer* tmr) {
	switch(tmr->t[1].mode.sync_mode) {
	case 0:
		tmr->tstatus[1].paused = false;
		break;
	case 1:
		tmr->t[1].value = 0;
		break;
	case 2:
		tmr->t[1].value = 0;
		tmr->tstatus[1].paused = false;
		break;
	default:
		break;
	}
}

void tmr_write(struct psx_timer* tmr, uint32_t off, uint32_t val) {
	int idx = off >> 4;

	switch(off & 0xf) {
	case 0:
		// printf("timer%d write value -> 0x%08x\n", off >> 4, val);
		tmr->t[idx].value = val & 0xffff;
		break;
	case 4:
		// printf("timer%d write mode -> 0x%08x\n", off >> 4, val);
		AS_UINT32(tmr->t[idx].mode) = (AS_UINT32(tmr->t[idx].mode) & 0xffffc00) | (val & 0x3ff);
		tmr->t[idx].mode.target_reached = false;
		tmr->t[idx].mode.max_reached = false;
		tmr->t[idx].mode.no_irq = true;
		tmr->t[idx].value = 0;
		tmr->tstatus[idx].irq_triggered = false;
		tmr->tstatus[idx].paused = false;
		tmr->tstatus[idx].ctr = 0;
		
		handle_target(tmr, idx);
		break;
	case 8:
		// printf("timer%d write target -> 0x%08x\n", off >> 4, val);
		tmr->t[idx].target = val & 0xffff;
		break;
	default:
		break;
	}
}

uint32_t psx_tmr_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	uint8_t* regs = reg->peripheral;

	uint32_t val = 0;
	if(register_offset < 0x30) {
		memcpy(&val, &regs[register_offset], sizeof(val));
	}

	if((register_offset & 0xf) == 4) {
		struct psx_timer* tmr = reg->peripheral;
		int idx = register_offset >> 4;
		tmr->t[idx].mode.target_reached = false;
		tmr->t[idx].mode.max_reached = false;
	}

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
	printf("timer%d read %s <- 0x%08x\n", register_offset / 0x10, regname, val);
	*/
	return val;
}

void psx_tmr_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = addr - reg->start;

	tmr_write(reg->peripheral, register_offset, val);
}

uint16_t psx_tmr_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = addr - reg->start;
	uint8_t* regs = reg->peripheral;

	uint16_t val = 0;
	if(register_offset < 0x30) {
		memcpy(&val, &regs[register_offset], sizeof(val));
	}

	if((register_offset & 0xf) == 4) {
		struct psx_timer* tmr = reg->peripheral;
		int idx = register_offset >> 4;
		tmr->t[idx].mode.target_reached = false;
		tmr->t[idx].mode.max_reached = false;
	}

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
	// printf("timer%d read %s <- 0x%04x\n", register_offset / 0x10, regname, val);
	*/
	return val;
}

void psx_tmr_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = addr - reg->start;

	tmr_write(reg->peripheral, register_offset, val);
}

