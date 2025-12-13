#include <psx.h>

#include "pad.h"
#include "log.h"

#include <stdbool.h>

struct psx_spu g_dummy_spu;

void psx_set_log_level(int level) {
	switch(level) {
	case PSX_LOG_LEVEL_TRACE:
		log_set_level(LOG_TRACE);
		break;
	case PSX_LOG_LEVEL_DEBUG:
		log_set_level(LOG_DEBUG);
		break;
	case PSX_LOG_LEVEL_INFO:
		log_set_level(LOG_INFO);
		break;
	case PSX_LOG_LEVEL_WARN:
		log_set_level(LOG_WARN);
		break;
	case PSX_LOG_LEVEL_ERROR:
		log_set_level(LOG_ERROR);
		break;
	case PSX_LOG_LEVEL_FATAL:
		log_set_level(LOG_FATAL);
		break;
	case PSX_LOG_LEVEL_DISABLE:
		log_set_quiet(true);
		break;
	default:
		break;
	}
}

bool psx_system_init(struct psx_system* sys, const char* bios_path) {
	psx_mem_init(sys->memory);
	psx_memctl_init(sys->mc);
	sys->memory->sys = sys;

	psx_bios_load(sys->bios, bios_path, true);
	sys->bios->sys = sys;

	psx_sched_init(sys->sched);
	sys->sched->sys = sys;

	psx_cpu_init(sys->cpu);
	sys->cpu->sys = sys;

	sys->gpu->sys = sys;
	psx_gpu_init(sys->gpu);

	psx_tmr_init(sys->timer);
	sys->timer->sys = sys;

	psx_dmac_init(sys->dmac);
	sys->dmac->sys = sys;

	psx_cdr_init(sys->cdrom);
	sys->cdrom->sys = sys;

	psx_irq_init(sys->irq);
	sys->irq->sys = sys;

	psx_sio_init(sys->sio);
	sys->sio->sys = sys;

	sys->spu = &g_dummy_spu;
	psx_spu_init(sys->spu);
	sys->spu->sys = sys;

	#include "hwmap.inc"

	return true;
}

// TODO: psx_system_reset

void psx_system_add_pad(struct psx_system* sys, int port, void* host_data, psx_padpollfn_t pollfn) {
	sys->sio->dev.pad[port] = pad_connect(port, host_data, pollfn);
}

void psx_system_remove_pad(struct psx_system* sys, int port) {
	memset(sys->sio->dev.pad[port], 0, sizeof(*sys->sio->dev.pad[port]));
	sys->sio->dev.pad[port] = NULL;
}

void psx_system_set_tray_open(struct psx_system* sys, bool opened) {
	sys->cdrom->state.shell_open = opened;
}

void psx_system_insert_disc(struct psx_system* sys, struct psx_disc* disc) {
	psx_disc_verify(disc);
	sys->cdrom->disc = disc;
}

void psx_system_eject_disc(struct psx_system* sys) {
	// reset console?
	sys->cdrom->disc = NULL;
}

void psx_system_update(struct psx_system* sys) {
	psx_cpu_fetch_execute(sys->cpu);
	if(sys->current_exe) {
		if(sys->cpu->next_pc == 0x80030000) {
			fprintf(stderr, "exe loaded\n");
			psx_exe_load(sys->current_exe, sys);
		}
	}

	// psx_dmac_update(sys->dmac);
	psx_gpu_update(sys->gpu, sys->cpu->clocks);
}

void psx_system_update_s(struct psx_system* sys) {
	psx_cpu_fetch_execute(sys->cpu);

	/*
	if(sys->cpu->regs.pc == 0x800507d4) {
		int w = *(uint16_t*)(&sys->memory->phys[(sys->cpu->regs.a0 + 4) & 0x1fffff]);
		int h = *(uint16_t*)(&sys->memory->phys[(sys->cpu->regs.a0 + 6) & 0x1fffff]);
		fprintf(stderr, "FinalizeGPUStore(cmd_chain_halfwords=0x%08x, dstbuf=0x%08x) w=%d, h=%d\n", sys->cpu->regs.a0, sys->cpu->regs.a1, w, h);
	}
	*/

	if(sys->current_exe) {
		if(sys->cpu->next_pc == 0x80030000) {
			fprintf(stderr, "exe loaded\n");
			psx_exe_load(sys->current_exe, sys);
		}
	}

	psx_tmr_tick(sys->timer, sys->cpu->clocks);
	psx_sched_update(sys->sched, sys->cpu->clocks);
}

void psx_system_start(struct psx_system* sys) {
	while(1) {
		psx_system_update(sys);
	}
}

