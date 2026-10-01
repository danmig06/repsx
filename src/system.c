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
	if(!psx_bios_load(sys->bios, bios_path)) {
		return false;
	}

	psx_mem_init(sys->memory);
	sys->memory->sys = sys;
	psx_memctl_init(sys->mc);
	psx_sched_init(sys->sched, sys);
	psx_cpu_init(sys->cpu, sys);
	psx_gpu_init(sys->gpu, sys);
	psx_tmr_init(sys->timer, sys);
	psx_dmac_init(sys->dmac, sys);
	psx_cdr_init(sys->cdrom, sys);
	psx_irq_init(sys->irq, sys);
	psx_sio_init(sys->sio, sys);
	psx_spu_init(sys->spu, sys);
	psx_mdec_init(sys->mdec, sys);

	#include "hwmap.inc"

	return true;
}

void psx_system_uninit(struct psx_system* sys) {
	free(sys->cdrom->out[0].buf);
	free(sys->sio->dev.in[0]);
	free(sys->mdec->mb_data);
	free(sys->spu->out.buf);
	free(sys->spu->mem);
	free(sys->memory->phys);
	free(sys->bios->rom);
}

// TODO: psx_system_reset

void psx_system_add_pad(struct psx_system* sys, int port, void* host_data, psx_padpollfn_t pollfn) {
	struct sio_dev* slot = sys->sio->dev.in[port & 1];
	pad_connect(slot, host_data, pollfn);
}

void psx_system_remove_dev(struct psx_system* sys, int port) {
	sio_dev_clear(sys->sio->dev.in[port]);
}

void psx_system_add_mcd(struct psx_system* sys, int port, void* host_data, psx_buwritefn_t write_fn, psx_bureadfn_t read_fn) {
	struct sio_dev* slot = sys->sio->dev.bu[port & 1];
	bu_connect(slot, host_data, write_fn, read_fn);
}

void psx_system_remove_mcd(struct psx_system* sys, int port) {
	sio_dev_clear(sys->sio->dev.bu[port]);
}

void psx_system_signal(struct psx_system* sys, int sig) {
	switch(sig) {
	case PSX_SIG_TRAY_OPEN:
		psx_cdr_tray_open(sys->cdrom);
		break;
	case PSX_SIG_TRAY_CLOSED:
		sys->cdrom->shell_open = false;
		sys->cdrom->state = 0x02;
		break;
	}
}

void psx_system_set_disc(struct psx_system* sys, struct psx_disc* disc) {
	struct psx_cdrom* cdr = sys->cdrom;
	if(disc) {
		if(!disc->tracks || disc->n_tracks == 0 || disc->n_tracks > 99) {
			log_error("SYSTEM: invalid disc inserted");
			return;
		}
		cdr->current_track.idx = 0;
		cdr->current_track.start = disc->tracks[0].abs_start;
		cdr->current_track.end = disc->tracks[0].abs_end;
		cdr->current_track.is_audio = disc->tracks[0].is_audio;
		cdr->disc_type = psx_disc_verify(disc);
	}
	cdr->disc = disc;
}

void psx_system_update(struct psx_system* sys) {
	psx_cpu_fetch_execute(sys->cpu);
	psx_sched_update(sys->sched, sys->cpu->clocks);
	
	if(sys->current_exe && sys->cpu->next_pc == 0x80030000) {
		log_info("SYSTEM: exe loaded");
		psx_exe_load(sys->current_exe, sys);
	}
}

