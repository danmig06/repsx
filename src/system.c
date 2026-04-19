#include <psx.h>

#include "input/pad.h"
#include "input/backupunit.h"
#include "log.h"
#include "rdef/cdrom.h"

#include <stdbool.h>

#define ALIGN(s, n) ((s & ~(n - 1)) + n)
#define AS(s) (ALIGN(sizeof(s), 8))
#define SYS_TOTAL_SIZE(s) (AS(*s) + AS(*s->cpu) + AS(*s->memory) + AS(*s->bios) + AS(*s->mc) + \
			   AS(*s->gpu) + AS(*s->dmac) + AS(*s->cdrom) + AS(*s->irq) + \
			   AS(*s->sio) + AS(*s->timer) + AS(*s->spu) + AS(*s->mdec) + \
			   AS(*s->sched))

void psx_set_log_level(int level) {
	log_set_quiet(false);
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

#define CLOBBER(o) \
	do { \
		o = (void*)(em); \
		em += AS(*o); \
	} while(0)

struct psx_system* psx_system_alloc(void) {
	struct psx_system* sys;
	size_t total_size = SYS_TOTAL_SIZE(sys);
	uint8_t* em = calloc(total_size, 1);
	CLOBBER(sys);
	CLOBBER(sys->cpu);
	CLOBBER(sys->memory);
	CLOBBER(sys->bios);
	CLOBBER(sys->mc);
	CLOBBER(sys->gpu);
	CLOBBER(sys->dmac);
	CLOBBER(sys->cdrom);
	CLOBBER(sys->irq);
	CLOBBER(sys->sio);
	CLOBBER(sys->timer);
	CLOBBER(sys->spu);
	CLOBBER(sys->mdec);
	CLOBBER(sys->sched);
	return sys;
}

bool psx_system_init(struct psx_system* sys, const char* bios_path) {
	psx_mem_init(sys->memory);
	psx_memctl_init(sys->mc);
	sys->memory->sys = sys;

	psx_bios_load(sys->bios, bios_path);
	sys->bios->sys = sys;

	psx_sched_init(sys->sched);
	sys->sched->sys = sys;

	psx_cpu_init(sys->cpu);
	sys->cpu->sys = sys;

	sys->gpu->sys = sys;
	psx_gpu_init(sys->gpu);

	sys->timer->sys = sys;
	psx_tmr_init(sys->timer);

	psx_dmac_init(sys->dmac);
	sys->dmac->sys = sys;

	psx_cdr_init(sys->cdrom);
	sys->cdrom->sys = sys;

	psx_irq_init(sys->irq);
	sys->irq->sys = sys;

	psx_sio_init(sys->sio);
	sys->sio->sys = sys;

	sys->spu->sys = sys;
	psx_spu_init(sys->spu);

	sys->mdec->sys = sys;
	psx_mdec_init(sys->mdec);

	#include "hwmap.inc"

	return true;
}

void psx_system_uninit(struct psx_system* sys) {
	free(sys->cdrom->out[0].buf);
	free(sys->cdrom->resp_queue);
	free(sys->cdrom->data_queue);
	free(sys->cdrom->param_queue);
	free(sys->mdec->mb_data);
	free(sys->spu->out.buf);
	free(sys->spu->mem);
	free(sys->memory->phys);
	free(sys->bios->rom);
}

// TODO: psx_system_reset

void psx_system_add_pad(struct psx_system* sys, int port, void* host_data, psx_padpollfn_t pollfn) {
	sys->sio->dev.in[port] = pad_connect(port, host_data, pollfn);
}

void psx_system_remove_pad(struct psx_system* sys, int port) {
	sys->sio->dev.in[port] = NULL;
}

void psx_system_add_mcd(struct psx_system* sys, int port, void* host_data, psx_buwritefn_t write_fn, psx_bureadfn_t read_fn) {
	sys->sio->dev.bu[port] = bu_connect(port, host_data, write_fn, read_fn);
}

void psx_system_remove_mcd(struct psx_system* sys, int port) {
	sys->sio->dev.bu[port] = NULL;
}

void psx_system_signal(struct psx_system* sys, int sig) {
	switch(sig) {
	case PSX_SIG_TRAY_OPEN:
		psx_cdr_tray_open(sys->cdrom);
		break;
	case PSX_SIG_TRAY_CLOSED:
		sys->cdrom->shell_open = false;
		break;
	}
}

void psx_system_set_disc(struct psx_system* sys, struct psx_disc* disc) {
	if(disc) {
		psx_disc_verify(disc);
	}
	sys->cdrom->disc = disc;
}

void psx_system_update(struct psx_system* sys) {
	psx_cpu_fetch_execute(sys->cpu);

	/*
	if(sys->cpu->regs.pc == 0x800507d4) {
		int w = *(uint16_t*)(&sys->memory->phys[(sys->cpu->regs.a0 + 4) & 0x1fffff]);
		int h = *(uint16_t*)(&sys->memory->phys[(sys->cpu->regs.a0 + 6) & 0x1fffff]);
		fprintf(stderr, "FinalizeGPUStore(cmd_chain_halfwords=0x%08x, dstbuf=0x%08x) w=%d, h=%d\n", sys->cpu->regs.a0, sys->cpu->regs.a1, w, h);
	}
	*/

	psx_sched_update(sys->sched, sys->cpu->clocks);
	
	if(sys->current_exe && sys->cpu->next_pc == 0x80030000) {
		fprintf(stderr, "exe loaded\n");
		psx_exe_load(sys->current_exe, sys);
	}
}

void psx_system_start(struct psx_system* sys) {
	while(1) {
		psx_system_update(sys);
	}
}

