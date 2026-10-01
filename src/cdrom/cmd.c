#include <psx/sched.h>
#include <psx/irq.h>
#include <psx/cpu.h>
#include <psx/disc.h>

#include "cmd.h"
#include "queue.h"
#include "../util.h"
#include "../log.h"
#include "../rdef/cdrom.h"

#include <string.h>

#define MODE2_DISC_FLAG 0x20
#define AUDIO_DISC_FLAG 0
#define MSF_TO_LBA(m, s, f) ((((m) * 60 + (s)) * 75) + (f))
#define BCD_TO_BYTE(b) ((uint8_t)((((b) & 0xf0) >> 4) * 10 + ((b) & 0x0f)))
#define BYTE_TO_BCD(b) ((((b) / 10) << 4) | ((b) % 10))
#define INVALID_BCD(b) (((b) > 0x99) || (((b) & 0xf) > 9))
#define ACK_TIMESTAMP (cdr->ack.ev.eta)
#define SECTOR_HDR_OFF 12
#define IRQ_MIN_COOLDOWN 1000
#define IRQ_RETRY_RATE 500
#define MAKE_DELAY_SECS(n) (PSX_CPU_CLOCKS_PER_SEC * (n))
// TODO: proper seek timing calculation based on distance to the target location,
// a factor <= 4 will hang Legend of Dragoon
#define SEEK_DELAY_FACTOR 5

#define XA_MAX_OUTPUT_SAMPLES (((2016 * 2) * 7) / 6)
#define XA_MAX_OUTPUT_SIZE (XA_MAX_OUTPUT_SAMPLES * sizeof(int16_t))

#define CD_LEFT 0
#define CD_RIGHT 1
#define CD_MONO CD_LEFT

#define MSF_FMT "%02d:%02d:%02d"
#define MSF_ARG(lba) ((lba) / 4500), (((lba) / 75) % 60), ((lba) % 75)

typedef struct __psx_cdr_event cdr_ev_t;

enum {
	SEEK_TYPE_SUBQ,   // for CdlSeekP
	SEEK_TYPE_LOGICAL // for CdlReadN/CdlReadS/CdlSeekL
};

typedef struct sector_header {
	uint8_t minute;
	uint8_t second;
	uint8_t frame;
	uint8_t mode;
} sector_hdr_t;

typedef struct xa_header {
	uint8_t file;
	uint8_t channel;
	uint8_t submode;
	uint8_t coding_info;
} xa_hdr_t;

static int g_xa_fc_old[] = { 0, 60, 115, 98 };
static int g_xa_fc_older[] = { 0, 0, 52, 55 };

static int16_t g_zigzag_tables[7][29] = {
	{
		 0x0000,  0x0000,  0x0000,  0x0000,  0x0000, -0x0002,  0x000A, -0x0022,
		 0x0041, -0x0054,  0x0034,  0x0009, -0x010A,  0x0400, -0x0A78,  0x234C,
		 0x6794, -0x1780,  0x0BCD, -0x0623,  0x0350, -0x016D,  0x006B,  0x000A,
		-0x0010,  0x0011, -0x0008,  0x0003, -0x0001
	}, {
		 0x0000,  0x0000,  0x0000, -0x0002,  0x0000,  0x0003, -0x0013,  0x003C,
		-0x004B,  0x00A2, -0x00E3,  0x0132, -0x0043, -0x0267,  0x0C9D,  0x74BB,
		-0x11B4,  0x09B8, -0x05BF,  0x0372, -0x01A8,  0x00A6, -0x001B,  0x0005,
		 0x0006, -0x0008,  0x0003, -0x0001,  0x0000
	}, {
		 0x0000,  0x0000, -0x0001,  0x0003, -0x0002, -0x0005,  0x001F, -0x004A,
		 0x00B3, -0x0192,  0x02B1, -0x039E,  0x04F8, -0x05A6,  0x7939, -0x05A6,
		 0x04F8, -0x039E,  0x02B1, -0x0192,  0x00B3, -0x004A,  0x001F, -0x0005,
		-0x0002,  0x0003, -0x0001,  0x0000,  0x0000
	}, {
		 0x0000, -0x0001,  0x0003, -0x0008,  0x0006,  0x0005, -0x001B,  0x00A6,
		-0x01A8,  0x0372, -0x05BF,  0x09B8, -0x11B4,  0x74BB,  0x0C9D, -0x0267,
		-0x0043,  0x0132, -0x00E3,  0x00A2, -0x004B,  0x003C, -0x0013,  0x0003,
		 0x0000, -0x0002,  0x0000,  0x0000,  0x0000
	}, {
		 0x0001,  0x0003, -0x0008,  0x0011, -0x0010,  0x000A,  0x006B, -0x016D,
		 0x0350, -0x0623,  0x0BCD, -0x1780,  0x6794,  0x234C, -0x0A78,  0x0400,
		-0x010A,  0x0009,  0x0034, -0x0054,  0x0041, -0x0022,  0x000A, -0x0001,
		 0x0000,  0x0001,  0x0000,  0x0000,  0x0000
	}, {
		 0x0002, -0x0008,  0x0010, -0x0023,  0x002B,  0x001A, -0x00EB,  0x027B,
		-0x0548,  0x0AFA, -0x16FA,  0x53E0,  0x3C07, -0x1249,  0x080E, -0x0347,
		 0x015B, -0x0044, -0x0017,  0x0046, -0x0023,  0x0011, -0x0005,  0x0000,
		 0x0000,  0x0000,  0x0000,  0x0000,  0x0000
	}, {
		-0x0005,  0x0011, -0x0023,  0x0046, -0x0017, -0x0044,  0x015B, -0x0347,
		 0x080E, -0x1249,  0x3C07,  0x53E0, -0x16FA,  0x0AFA, -0x0548,  0x027B,
		-0x00EB,  0x001A,  0x002B, -0x0023,  0x0010, -0x0008,  0x0002,  0x0000,
		 0x0000,  0x0000,  0x0000,  0x0000,  0x0000
	}
};

static int16_t g_zigzag_tables_hr[7][25] = {
	{
		 0x0000, -0x0005,  0x0011, -0x0023,  0x0046, -0x0017, -0x0044,  0x015b, -0x0347,  0x080e, -0x1249,  0x3c07,  0x53e0,
		-0x16fa,  0x0afa, -0x0548,  0x027b, -0x00eb,  0x001a,  0x002b, -0x0023,  0x0010, -0x0008,  0x0002,  0x0000
	}, {
		 0x0000, -0x0002,  0x000a, -0x0022,  0x0041, -0x0054,  0x0034,  0x0009, -0x010a,  0x0400, -0x0a78,  0x234c,  0x6794,
		-0x1780,  0x0bcd, -0x0623,  0x0350, -0x016d,  0x006b,  0x000a, -0x0010,  0x0011, -0x0008,  0x0003, -0x0001
	}, {
		-0x0002,  0x0000,  0x0003, -0x0013,  0x003c, -0x004b,  0x00a2, -0x00e3,  0x0132, -0x0043, -0x0267,  0x0c9d,  0x74bb,
		-0x11b4,  0x09b8, -0x05bf,  0x0372, -0x01a8,  0x00a6, -0x001b,  0x0005,  0x0006, -0x0008,  0x0003, -0x0001
	}, {
		-0x0001,  0x0003, -0x0002, -0x0005,  0x001f, -0x004a,  0x00b3, -0x0192,  0x02b1, -0x039e,  0x04f8, -0x05a6,  0x7939,
		-0x05a6,  0x04f8, -0x039e,  0x02b1, -0x0192,  0x00b3, -0x004a,  0x001f, -0x0005, -0x0002,  0x0003, -0x0001
	}, {
		-0x0001,  0x0003, -0x0008,  0x0006,  0x0005, -0x001b,  0x00a6, -0x01a8,  0x0372, -0x05bf,  0x09b8, -0x11b4,  0x74bb,
		 0x0c9d, -0x0267, -0x0043,  0x0132, -0x00e3,  0x00a2, -0x004b,  0x003c, -0x0013,  0x0003,  0x0000, -0x0002
	}, {
		-0x0001,  0x0003, -0x0008,  0x0011, -0x0010,  0x000a,  0x006b, -0x016d,  0x0350, -0x0623,  0x0bcd, -0x1780,  0x6794,
		 0x234c, -0x0a78,  0x0400, -0x010a,  0x0009,  0x0034, -0x0054,  0x0041, -0x0022,  0x000a, -0x0002,  0x0000
	}, {
		 0x0000,  0x0002, -0x0008,  0x0010, -0x0023,  0x002b,  0x001a, -0x00eb,  0x027b, -0x0548,  0x0afa, -0x16fa,  0x53e0,
		 0x3c07, -0x1249,  0x080e, -0x0347,  0x015b, -0x0044, -0x0017,  0x0046, -0x0023,  0x0011, -0x0005,  0x0000
	},
};

void cdr_run_cmd(struct psx_cdrom* cdr);

static void cdr_raise_irq(struct psx_cdrom* cdr, uint8_t ival) {
	INT_FLAGS_SET(cdr->regs.irq_status, ival);
	if(cdr->regs.irq_status & cdr->regs.irq_mask) {
		log_debug("CDROM: INT%d fired", INT_FLAGS_GET(cdr->regs.irq_status));
		psx_irq_raise(cdr->sys->irq, PSX_IRQ_ID_CDROM);
	}
}

static void cdr_remove_ev(struct psx_sched* sched, cdr_ev_t* ev) {
	psx_sched_remove_ev(sched, ev->ev.id);
	ev->active = false;
}

static void cdr_clear_response(struct psx_cdrom* cdr) {
	queue_clear(&cdr->resp.fifo);
	cdr->regs.ctrl &= ~CTRL_RESULT_READY;
}

#define RESCHEDULE(ev) do { \
		psx_sched_remove_ev(sched, (ev)->id); \
		(ev)->eta = IRQ_RETRY_RATE; \
		psx_sched_add_ev(sched, (ev)); \
	} while(0)

static void def_ack_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	cdr_ev_t* self = (cdr_ev_t*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(_self);
		return;
	}

	cdr_clear_response(cdr);
	if(self->resp_size > 0) {
		queue_push_buf(&cdr->resp.fifo, self->response, self->resp_size);
		cdr->regs.ctrl |= CTRL_RESULT_READY;
		self->resp_size = 0;
	}
	cdr_raise_irq(cdr, self->ival);
	cdr->regs.ctrl &= ~CTRL_BUSY;

	cdr_remove_ev(sched, self);
	if(cdr->regs.command != 0) {
		cdr_run_cmd(cdr);
	}
}

static void def_comp_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	cdr_ev_t* self = (cdr_ev_t*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(_self);
		return;
	}

	cdr_clear_response(cdr);
	if(self->resp_size > 0) {
		queue_push_buf(&cdr->resp.fifo, self->response, self->resp_size);
		cdr->regs.ctrl |= CTRL_RESULT_READY;
		self->resp_size = 0;
	}
	cdr_raise_irq(cdr, self->ival);

	cdr_remove_ev(sched, self);
	if(cdr->regs.command != 0) {
		cdr_run_cmd(cdr);
	}
}

static void deliver_async_irq(struct psx_sched* sched, struct psx_sev* _self) {
	cdr_ev_t* self = (cdr_ev_t*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if((cdr->regs.ctrl & CTRL_BUSY) || INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(_self);
		return;
	}

	uint64_t time_diff = sched->clocks_elapsed - cdr->last_ack_timestamp;
	if(time_diff <= IRQ_MIN_COOLDOWN) {
		RESCHEDULE(_self);
		return;
	}
	cdr_clear_response(cdr);
	if(self->resp_size > 0) {
		queue_push_buf(&cdr->resp.fifo, self->response, self->resp_size);
		cdr->regs.ctrl |= CTRL_RESULT_READY;
		self->resp_size = 0;
	}
	cdr_raise_irq(cdr, self->ival);

	cdr_remove_ev(sched, self);
}

static uint32_t cdr_get_avg_delay(struct psx_cdrom* cdr) {
	return (cdr->state & STAT_MOTOR_ON) ? CDROM_CMD_AVG_DELAY : CDROM_CMD_AVG_SDELAY;
}

static uint32_t cdr_get_read_delay(struct psx_cdrom* cdr) {
	return (cdr->disc_mode & MODE_DOUBLE_SPEED) ? (PSX_CPU_CLOCKS_PER_SEC / 150) : (PSX_CPU_CLOCKS_PER_SEC / 75);
}

static uint32_t cdr_get_pause_delay(struct psx_cdrom* cdr) {
	if(!(cdr->state & (STAT_READING | STAT_PLAYING))) {
		return CDROM_CMD_PAUSE_NOPDELAY;	
	}

	return (cdr->disc_mode & MODE_DOUBLE_SPEED) ? CDROM_CMD_PAUSE_DSDELAY : CDROM_CMD_PAUSE_DELAY;
}

static uint32_t cdr_get_stop_delay(struct psx_cdrom* cdr) {
	if(!(cdr->state & STAT_MOTOR_ON)) {
		return CDROM_CMD_STOP_NOPDELAY;	
	}

	return (cdr->disc_mode & MODE_DOUBLE_SPEED) ? CDROM_CMD_STOP_DSDELAY : CDROM_CMD_STOP_DELAY;
}

#define cdr_schedule_ack_ev(cdr, ival, ...) \
	_cdr_schedule_int_ev(cdr, &cdr->ack, ival, &(struct psx_sev){ \
			.eta = (cdr->state & STAT_MOTOR_ON) ? CDROM_CMD_AVG_DELAY : CDROM_CMD_AVG_SDELAY, \
			.trigger = def_ack_evcb, __VA_ARGS__ })
#define cdr_schedule_comp_ev(cdr, ival, ...) \
	_cdr_schedule_int_ev(cdr, &cdr->comp, ival, &(struct psx_sev){ \
			.eta = cdr->ack.ev.eta + ((cdr->state & STAT_MOTOR_ON) ? CDROM_CMD_AVG_DELAY : CDROM_CMD_AVG_SDELAY), \
			.trigger = def_comp_evcb, __VA_ARGS__ })

static void _cdr_schedule_int_ev(struct psx_cdrom* cdr, cdr_ev_t* cdev, int ival, struct psx_sev* ev_fields) {
	if(cdev->active) {
		if(cdr->regs.ctrl & CTRL_BUSY) {
			log_error("CDROM: Canceling event because another is already active (while busy)");
		}
		cdr_remove_ev(cdr->sys->sched, cdev);
	}

	cdev->ival = ival;
	cdev->ev.trigger = ev_fields->trigger;
	cdev->ev.eta = ev_fields->eta;
	cdev->active = true;
	psx_sched_add_ev(cdr->sys->sched, &cdev->ev);
}

static void cdr_schedule_async_irq(struct psx_cdrom* cdr, int ival) {
	if(cdr->async_irq.active) {
		psx_sched_remove_ev(cdr->sys->sched, cdr->async_irq.ev.id);
	}

	if(!(cdr->regs.ctrl & CTRL_BUSY) && INT_FLAGS_GET(cdr->regs.irq_status) == 0) {
		uint64_t time_diff = cdr->sys->sched->clocks_elapsed - cdr->last_ack_timestamp;
		if(time_diff > IRQ_MIN_COOLDOWN) {
			cdr_clear_response(cdr);
			if(cdr->async_irq.resp_size > 0) {
				queue_push_buf(&cdr->resp.fifo, cdr->async_irq.response, cdr->async_irq.resp_size);
				cdr->regs.ctrl |= CTRL_RESULT_READY;
				cdr->async_irq.resp_size = 0;
			}
			cdr_raise_irq(cdr, ival);
			return;
		}
	}

	cdr->async_irq.ival = ival;
	cdr->async_irq.ev.trigger = deliver_async_irq;
	cdr->async_irq.ev.eta = IRQ_RETRY_RATE;
	cdr->async_irq.active = true;
	psx_sched_add_ev(cdr->sys->sched, &cdr->async_irq.ev);
}

static void enq_error(struct psx_cdrom* cdr, uint8_t err_byte, uint8_t status) {
	if(!(status & (STAT_SEEK_ERROR | STAT_ID_ERROR))) {
		status |= STAT_ERROR;
	}

	cdr->ack.response[0] = cdr->state | (status & (STAT_SEEK_ERROR | STAT_ID_ERROR | STAT_ERROR));
	cdr->ack.resp_size = 1;
	if(status & (STAT_SEEK_ERROR | STAT_ERROR)) {
		cdr->ack.response[1] = err_byte;
		cdr->ack.resp_size++;
	}
	cdr_schedule_ack_ev(cdr, 5);
}

#define put_ack_response(rb, sz) _put_response(&cdr->ack, rb, sz)
#define put_comp_response(rb, sz) _put_response(&cdr->comp, rb, sz)
#define put_async_response(rb, sz) _put_response(&cdr->async_irq, rb, sz)
static void _put_response(cdr_ev_t* ev, void* rb, uint8_t size) {
	if(ev->resp_size >= PSX_CDROM_RESPBUF_SIZE) {
		return;
	}

	if(size > (PSX_CDROM_RESPBUF_SIZE - ev->resp_size)) {
		size = PSX_CDROM_RESPBUF_SIZE - ev->resp_size;
	}

	memcpy(&ev->response[ev->resp_size], rb, size);
	ev->resp_size += size;
}

static inline uint8_t cdr_pop_param(struct psx_cdrom* cdr) {
	return queue_pop(&cdr->param.fifo);
}

static inline void cdr_reset_loc(struct psx_cdrom* cdr) {
	psx_disc_track_t* track = &cdr->disc->tracks[0];
	cdr->current_track.idx = 0;
	cdr->current_track.start = track->abs_start;
	cdr->current_track.end = track->abs_end;
	cdr->current_track.is_audio = track->is_audio;
	cdr->loc = cdr->current_track.start;
}

static void read_drive_evcb(struct psx_sched* sched, struct psx_sev* _self);
static void play_drive_evcb(struct psx_sched* sched, struct psx_sev* _self);
static void seek_drive_evcb(struct psx_sched* sched, struct psx_sev* _self);

static inline void cdr_set_drive_state(struct psx_cdrom* cdr, uint8_t new_stat) {
	uint8_t prev_stat = cdr->state & (STAT_READING | STAT_PLAYING | STAT_SEEKING);
	if(prev_stat != 0) {
		psx_sched_remove_ev(cdr->sys->sched, cdr->drive_event.id);
		// if Play is being stopped, clear the sample buffer to prevent glitches
		if((prev_stat ^ new_stat) & STAT_PLAYING) {
			cdr->out[CD_LEFT].read_off = cdr->out[CD_LEFT].write_off = 0;
			cdr->out[CD_RIGHT].read_off = cdr->out[CD_RIGHT].write_off = 0;
		}
		cdr->state &= ~prev_stat;
	}

	cdr->state |= new_stat;
}

static bool cdr_perform_seek(struct psx_cdrom* cdr, int type) {
	if(cdr->seek.loc < cdr->current_track.start || cdr->seek.loc >= cdr->current_track.end) {
		psx_disc_track_t* track;
		int i = cdr->disc->n_tracks - 1;

		// search in reverse so we can fall back to Track 1
		for(; i >= 0; i--) {
			track = &cdr->disc->tracks[i];
			if(cdr->seek.loc >= track->abs_start && cdr->seek.loc < track->abs_end) {
				break;
			}
		}
		cdr->current_track.idx = i;
		cdr->current_track.start = track->abs_start;
		cdr->current_track.end = track->abs_end;
		cdr->current_track.is_audio = track->is_audio;
	}

	if(type == SEEK_TYPE_LOGICAL && cdr->current_track.is_audio) {
		log_error("CDROM: Logical seek failed (landed on an audio track)");
		cdr_reset_loc(cdr);
		cdr->seek.is_pending = false;
		cdr_set_drive_state(cdr, 0);
		cdr->state &= ~STAT_MOTOR_ON;
		return false;
	}
	cdr->loc = cdr->seek.loc;
	cdr->seek.is_pending = false;
	return true;
}

static void cdr_seek_next_track(struct psx_cdrom* cdr) {
	if(cdr->next_track != cdr->current_track.idx + 1) {
		psx_disc_track_t* track = &cdr->disc->tracks[cdr->next_track - 1];
		cdr->current_track.idx = cdr->next_track - 1;
		cdr->current_track.start = track->abs_start;
		cdr->current_track.end = track->abs_end;
		cdr->current_track.is_audio = track->is_audio;
	}

	cdr->loc = cdr->current_track.start;
}

void psx_cdr_tray_open(struct psx_cdrom* cdr) {
	if(cdr->ack.active) {
		cdr_remove_ev(cdr->sys->sched, &cdr->ack);
	}
	if(cdr->comp.active) {
		cdr_remove_ev(cdr->sys->sched, &cdr->comp);
	}
	// all status bits besides shell open and error are cleared
	cdr_set_drive_state(cdr, 0);
	cdr->state = STAT_SHELL_OPEN;
	cdr_clear_response(cdr);
	cdr->shell_open = true;
	queue_push(&cdr->resp.fifo, cdr->state | STAT_SEEK_ERROR);
	queue_push(&cdr->resp.fifo, CDROM_ERR_SHELL_OPENED);
	cdr_raise_irq(cdr, 5);
}

static inline void run_cmd(struct psx_cdrom* cdr, void (*cmd_func)(struct psx_cdrom*), uint32_t n_args, bool check_disc) {
	if(queue_items(&cdr->param.fifo) != n_args) {
		enq_error(cdr, CDROM_ERR_PARAMETERS, 0);
		return;
	} else if(check_disc && !cdr->disc) {
		enq_error(cdr, CDROM_ERR_RESP_NOT_READY, 0);
		return;
	}
	cdr->regs.ctrl |= CTRL_BUSY;
	cmd_func(cdr);
}

void cdr_run_cmd(struct psx_cdrom* cdr) {
	if((cdr->regs.ctrl & CTRL_BUSY) || cdr->async_irq.active) {
		return;
	}

	switch(cdr->regs.command) {
	case CMD_NOP:
		run_cmd(cdr, CdlNop, 0, false);
		break;
	case CMD_SETLOC:
		run_cmd(cdr, CdlSetloc, 3, true);
		break;
	case CMD_PLAY:
		if(queue_items(&cdr->param.fifo) > 1) {
			enq_error(cdr, CDROM_ERR_PARAMETERS, 0);
			break;
		} else if(!cdr->disc) {
			enq_error(cdr, CDROM_ERR_RESP_NOT_READY, 0);
			break;
		}
		cdr->regs.ctrl |= CTRL_BUSY;
		CdlPlay(cdr);
		break;
	case CMD_READN:
	case CMD_READS:
		run_cmd(cdr, CdlRead, 0, true);
		break;
	case CMD_MOTORON:
		run_cmd(cdr, CdlMotorOn, 0, true);
		break;
	case CMD_STOP:
		run_cmd(cdr, CdlStop, 0, true);
		break;
	case CMD_PAUSE:
		run_cmd(cdr, CdlPause, 0, true);
		break;
	case CMD_INIT:
		run_cmd(cdr, CdlInit, 0, false);
		break;
	case CMD_MUTE:
		run_cmd(cdr, CdlMute, 0, true);
		break;
	case CMD_DEMUTE:
		run_cmd(cdr, CdlDemute, 0, true);
		break;
	case CMD_SETFILTER:
		run_cmd(cdr, CdlSetfilter, 2, true);
		break;
	case CMD_SETMODE:
		run_cmd(cdr, CdlSetmode, 1, false);
		break;
	case CMD_GETLOCL:
		run_cmd(cdr, CdlGetlocL, 0, true);
		break;
	case CMD_GETLOCP:
		run_cmd(cdr, CdlGetlocP, 0, true);
		break;
	case CMD_GETTN:
		run_cmd(cdr, CdlGetTN, 0, true);
		break;
	case CMD_GETTD:
		run_cmd(cdr, CdlGetTD, 1, true);
		break;
	case CMD_TEST:
		run_cmd(cdr, CdlTest, 1, false);
		break;
	case CMD_SEEKL:
		cdr->seek.type = SEEK_TYPE_LOGICAL;
		run_cmd(cdr, CdlSeek, 0, true);
		break;
	case CMD_SEEKP:
		cdr->seek.type = SEEK_TYPE_SUBQ;
		run_cmd(cdr, CdlSeek, 0, true);
		break;
	case CMD_GETID:
		run_cmd(cdr, CdlGetID, 0, false);
		break;
	case CMD_READTOC:
		run_cmd(cdr, CdlReadTOC, 0, false);
		break;
	default:
		log_fatal("CDROM: unhandled command 0x%02x", cdr->regs.command);
		break;
	}

	queue_clear(&cdr->param.fifo);
	cdr->regs.ctrl |= CTRL_PARAM_EMPTY | CTRL_PARAM_READY;
	cdr->regs.command = 0;
}

void CdlNop(struct psx_cdrom* cdr) {
	if(!cdr->shell_open) {
		cdr->state &= ~STAT_SHELL_OPEN;
	}
	log_debug("CDROM: CdlNop() -> 0x%02x", cdr->state);
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlSetloc(struct psx_cdrom* cdr) {
	uint8_t m = cdr_pop_param(cdr);
	uint8_t s = cdr_pop_param(cdr);
	uint8_t f = cdr_pop_param(cdr);
	log_debug("CDROM: CdlSetloc(%02x:%02x:%02x)", m, s, f);
	m = BCD_TO_BYTE(m);
	s = BCD_TO_BYTE(s);
	f = BCD_TO_BYTE(f);
	cdr->seek.loc = MSF_TO_LBA(m, s, f);
	cdr->seek.is_pending = true;
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

static void play_drive_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	// clear previous seek state if any
	if(cdr->state & STAT_SEEKING) {
		cdr->state &= ~STAT_SEEKING;
		cdr->state |= STAT_PLAYING;
	}

	cdr->async_irq.resp_size = 0;
	queue_clear(&cdr->sector.fifo);
	int sample_step = (cdr->disc_mode & MODE_DOUBLE_SPEED) ? 2 : 1;
	if(cdr->current_track.is_audio) {
		cdr->disc->read_sector(cdr->disc->host_data, &cdr->disc->tracks[cdr->current_track.idx], cdr->loc, cdr->sector.data);
		int16_t* samples = (int16_t*)cdr->sector.data;
		for(int i = 0; i < (PSX_CDROM_DATABUF_SIZE / 2); i += sample_step) {
			cdr->out[CD_LEFT ].buf[i] = samples[(i * 2) + 0];
			cdr->out[CD_RIGHT].buf[i] = samples[(i * 2) + 1];
		}
		cdr->out[CD_LEFT ].write_off = PSX_CDROM_DATABUF_SIZE / 2;
		cdr->out[CD_LEFT ].read_off  = 0;
		cdr->out[CD_RIGHT].write_off = PSX_CDROM_DATABUF_SIZE / 2;
		cdr->out[CD_RIGHT].read_off  = 0;
	}
	cdr->loc++;
	if(cdr->loc == cdr->current_track.end) {
		cdr->current_track.idx++;
		if(cdr->current_track.idx == cdr->disc->n_tracks) {
			cdr_reset_loc(cdr);
			put_async_response(&cdr->state, 1);
			cdr_schedule_async_irq(cdr, 4);
			psx_sched_remove_ev(sched, self->id);
			return;
		}

		psx_disc_track_t* track = &cdr->disc->tracks[cdr->current_track.idx];
		cdr->current_track.start = track->abs_start;
		cdr->current_track.end = track->abs_end;
		cdr->current_track.is_audio = track->is_audio;
		if(cdr->disc_mode & MODE_AUTOPAUSE) {
			cdr->state &= ~STAT_PLAYING;
			put_async_response(&cdr->state, 1);
			cdr_schedule_async_irq(cdr, 4);
			psx_sched_remove_ev(sched, self->id);
			return;
		}
	}

	uint8_t asect = BYTE_TO_BCD(cdr->loc % 75);
	if((cdr->disc_mode & MODE_REPORT) && (asect % 16) == 0) {
		int16_t peakl = cdr->out[CD_LEFT].buf[0], peakr = cdr->out[CD_RIGHT].buf[0];
		for(int i = sample_step; i < (PSX_CDROM_DATABUF_SIZE / 2); i += sample_step) {
			peakl = MAX(peakl, cdr->out[CD_LEFT ].buf[i]);
			peakr = MAX(peakr, cdr->out[CD_RIGHT].buf[i]);
		}
		// we use the current absolute second from the position to simulate the
		// L/R channel bit toggling every ~75 frames
		uint8_t ass = BYTE_TO_BCD((cdr->loc / 75) % 60);
		int16_t peak_vol = MAX(peakl, peakr);
		uint16_t peakval = ((ass & 1) << 15) | (ABS(peak_vol) & 0x7fff);
		uint8_t cm, cs, cf;
		if((asect / 16) % 2 == 0) {
			cm = BYTE_TO_BCD(cdr->loc / 4500);
			cs = ass;
			cf = asect;
		} else {
			psx_lba_t rel = cdr->loc - cdr->current_track.start;
			cm = BYTE_TO_BCD(rel / 4500);
			cs = BYTE_TO_BCD((rel / 75) % 60) + 0x80;
			cf = BYTE_TO_BCD(rel % 75);
		}
		int track = BYTE_TO_BCD(cdr->current_track.idx + 1);
		psx_lba_t data_start = cdr->current_track.start + cdr->disc->tracks[cdr->current_track.idx].pregap_len;
		int index = (cdr->loc >= data_start) ? 1 : 0;
		uint8_t response[] = { cdr->state, track, index, cm, cs, cf, peakval & 0xff, peakval >> 8 };
		log_debug("CDROM: Play report: stat=%02x track %d @ index %d, pos: %02x:%02x:%02x peak={ 0x%04x | %c }",
				cdr->state, track, index, cm, cs & 0x7f, cf, peakval & 0x7fff, peakval & 0x8000 ? 'R' : 'L');
		put_async_response(response, sizeof(response));
		cdr_schedule_async_irq(cdr, 1);
	}

	psx_sched_remove_ev(sched, self->id);
	self->eta = cdr_get_read_delay(cdr) - (sched->clocks_elapsed - self->clocks_left);
	psx_sched_add_ev(sched, self);
}

static void play_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(self);
		return;
	}

	put_ack_response(&cdr->state, 1);
	def_ack_evcb(sched, self);

	int next_stat = STAT_PLAYING;
	if(cdr->next_track) {
		if(cdr->next_track > cdr->disc->n_tracks) {
			enq_error(cdr, CDROM_ERR_SEEK_FAILED, STAT_SEEK_ERROR);
			return;
		}
		cdr_seek_next_track(cdr);
		log_debug("-> Play seek to Track %d (start at "MSF_FMT")", cdr->next_track, MSF_ARG(cdr->loc));
		next_stat = STAT_SEEKING;
	} else if(cdr->seek.is_pending) {
		if(!cdr_perform_seek(cdr, SEEK_TYPE_SUBQ)) {
			return;
		}
		log_debug("-> Play seek to "MSF_FMT"", MSF_ARG(cdr->loc));
		next_stat = STAT_SEEKING;
	}

	if(cdr->state & STAT_PLAYING) {
		return;
	}

	// kickstart the play loop
	cdr->drive_event.eta = cdr_get_read_delay(cdr) * SEEK_DELAY_FACTOR;
	if(!(cdr->state & STAT_MOTOR_ON)) {
		cdr->drive_event.eta += cdr_get_avg_delay(cdr);
		cdr->state |= STAT_MOTOR_ON;
	}
	cdr->drive_event.trigger = play_drive_evcb;
	cdr_set_drive_state(cdr, next_stat);
	psx_sched_add_ev(sched, &cdr->drive_event);
}

void CdlPlay(struct psx_cdrom* cdr) {
	int track = 0;
	if(queue_items(&cdr->param.fifo)) {
		track = cdr_pop_param(cdr);
		log_debug("CdlPlay(%02x)", track);
	} else {
		log_debug("CdlPlay("MSF_FMT")", MSF_ARG(cdr->loc));
	}
	cdr->next_track = track;
	cdr_schedule_ack_ev(cdr, 3, .trigger = play_ack_evcb);
}

static int16_t zigzag_interp(struct psx_cdrom* cdr, int ch, int t) {
	int32_t sum = 0;
	int p = cdr->xa.resample[ch].index;
	for(int i = 0; i < 29; i++) {
		sum += (cdr->xa.resample[ch].buf[(p - i) & 0x1f] * g_zigzag_tables[t][i]) / 0x8000;
	}
	return SAT(sum, -0x8000, 0x7fff);
}

static void cdr_xa_resample(struct psx_cdrom* cdr, int16_t sample, int ch) {
	cdr->xa.resample[ch].buf[cdr->xa.resample[ch].index++] = sample;
	if(cdr->xa.resample[ch].index == 32) {
		cdr->xa.resample[ch].index = 0;
	}

	cdr->xa.resample[ch].counter--;
	if(cdr->xa.resample[ch].counter == 0) {
		cdr->xa.resample[ch].counter = 6;
		for(int t = 0; t < 7; t++) {
			cdr->out[ch].buf[cdr->out[ch].write_off++] = zigzag_interp(cdr, ch, t);
		}
	}
}

// Half-Rate upsampling works a little differently, just copying the same sample twice won't work well

// According to Mednafen and Duckstation:

static int16_t zigzag_interp_hr(struct psx_cdrom* cdr, int ch) {
	int32_t sum = 0;
	int p = cdr->xa.resample[ch].index;
	int t = cdr->xa.resample[ch].counter;
	for(int i = 0; i < 25; i++) {
		sum += cdr->xa.resample[ch].buf[(p + 32 - 25 + i) & 0x1f] * g_zigzag_tables_hr[t][i];
	}
	return SAT(sum / 0x8000, -0x8000, 0x7fff);
}

static void cdr_xa_resample_hr(struct psx_cdrom* cdr, int16_t sample, int ch) {
	bool frame_processed = false;
	do {
		if(cdr->xa.resample[ch].counter >= 7) {
			cdr->xa.resample[ch].buf[cdr->xa.resample[ch].index] = sample;
			cdr->xa.resample[ch].index = (cdr->xa.resample[ch].index + 1) & 0x1f;
			cdr->xa.resample[ch].counter -= 7;
			frame_processed = true;
		}

		cdr->out[ch].buf[cdr->out[ch].write_off++] = zigzag_interp_hr(cdr, ch);
		cdr->xa.resample[ch].counter += 3;
	} while(!frame_processed);
}

static void cdr_xa_decode_block(struct psx_cdrom* cdr, uint8_t* src, int blk, int nibble, int ch) {
	if(cdr->xa.coding_info & XA_CI_8BITS) {
		log_error("CDROM: XA 8bit sample data is not supported");
		memset(cdr->out[CD_LEFT].buf, 0, XA_MAX_OUTPUT_SIZE);
		return;
	}
	uint8_t hdr = src[4 + (blk * 2) + nibble];

	int shift = hdr & 0xf;
	if(shift > 12) {
		shift = 9;
	}
	int filter = (hdr >> 4) & 3;

	int16_t* hist = cdr->xa.hist[ch];
	int16_t raw = 0;
	int32_t sample = 0;
	int old_coef = g_xa_fc_old[filter];
	int older_coef = g_xa_fc_older[filter];
	int8_t cur_byte;
	for(int i = 0; i < 28; i++) {
		cur_byte = (src[16 + blk + (i * 4)] >> (nibble * 4)) & 0xf;
		raw = ((int8_t)(cur_byte * 16)) / 16;

		sample = ((uint32_t)(int32_t)raw) << (12 - shift);
		sample += ((old_coef * hist[0]) - (older_coef * hist[1]) + 32) / 64;

		hist[1] = hist[0];
		hist[0] = SAT(sample, -0x8000, 0x7fff);
		if(cdr->xa.coding_info & XA_CI_FS) {
			cdr_xa_resample_hr(cdr, hist[0], ch);
		} else {
			cdr_xa_resample(cdr, hist[0], ch);
		}
	}
}

static void cdr_xa_decode_sector(struct psx_cdrom* cdr) {
	uint8_t* src = cdr->sector.data;
	cdr->xa.coding_info = src[12 + 4 + 3];
	bool is_stereo = (cdr->xa.coding_info & XA_CI_SM) != 0;
	cdr->out[CD_LEFT].read_off = 0;
	cdr->out[CD_LEFT].write_off = 0;
	cdr->out[CD_RIGHT].read_off = 0;
	cdr->out[CD_RIGHT].write_off = 0;
	src += 12 + 4 + 8;
	for(int i = 0; i < 18; i++) {
		for(int blk = 0; blk < 4; blk++) {
			if(is_stereo) {
				cdr_xa_decode_block(cdr, src, blk, 0, CD_LEFT);
				cdr_xa_decode_block(cdr, src, blk, 1, CD_RIGHT);
			} else {
				cdr_xa_decode_block(cdr, src, blk, 0, CD_MONO);
				cdr_xa_decode_block(cdr, src, blk, 1, CD_MONO);
			}
		}
		src += 128;
	}
}

static void cdr_handle_xa(struct psx_cdrom* cdr, xa_hdr_t* subheader) {
	if(cdr->disc_mode & MODE_XA_FILTER) {
		if(cdr->xa.file != subheader->file || cdr->xa.channel != subheader->channel) {
			/*
			log_error("CDROM: XA sector skipped at LBA %d (file: %d, chan: %d, have: (%d, %d))", cdr->loc - 1, 
					subheader->file, subheader->channel, cdr->xa.file, cdr->xa.channel);
			*/
			return;
		}
	}

	cdr->xa.coding_info = subheader->coding_info;
	cdr_xa_decode_sector(cdr);
}

static void read_drive_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;

	// clear previous seek state if any
	if(cdr->state & STAT_SEEKING) {
		cdr->state &= ~STAT_SEEKING;
		cdr->state |= STAT_READING;
	}

	queue_clear(&cdr->sector.fifo);
	int sync_size = (cdr->disc_mode & MODE_SECTOR_SIZE) ? 12 : 24;
	psx_lba_t read_loc = cdr->loc++;
	cdr->disc->read_sector(cdr->disc->host_data, &cdr->disc->tracks[cdr->current_track.idx], read_loc, cdr->sector.data);
	sector_hdr_t* header = (void*)&cdr->sector.data[SECTOR_HDR_OFF];
	xa_hdr_t* subheader = (void*)&cdr->sector.data[SECTOR_HDR_OFF + sizeof(header)];
	if(header->mode == 2 && (cdr->disc_mode & MODE_XA)) {
		if((subheader->submode & XA_SM_AUDIO) && (subheader->submode & XA_SM_REALTIME)) {
			cdr->regs.ctrl &= ~CTRL_DATA_REQUEST;
			log_debug("CDROM: XA sector at "MSF_FMT" (%d LBA) | mode=0x%02x submode=0x%02x",
					MSF_ARG(read_loc), read_loc, header->mode, subheader->submode);
			cdr_handle_xa(cdr, subheader);
			goto read_end;
		}
	}

	cdr->regs.ctrl |= CTRL_DATA_REQUEST;
	cdr->sector.fifo.read_off = sync_size;
	cdr->sector.fifo.write_off = cdr->sector.fifo.read_off + ((cdr->disc_mode & MODE_SECTOR_SIZE) ? 0x924 : 0x800);
	log_debug("CDROM: read at "MSF_FMT" (%d LBA) | mode=0x%02x submode=0x%02x",
			MSF_ARG(read_loc), read_loc, header->mode, subheader->submode);
	cdr->async_irq.resp_size = 0;
	put_async_response(&cdr->state, 1);
	cdr_schedule_async_irq(cdr, 1);
read_end:
	psx_sched_remove_ev(sched, self->id);
	self->eta = cdr_get_read_delay(cdr) - (sched->clocks_elapsed - self->clocks_left);
	psx_sched_add_ev(sched, self);
}

static void read_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(self);
		return;
	}
	put_ack_response(&cdr->state, 1);
	def_ack_evcb(sched, self);

	int next_stat = STAT_READING;
	if(cdr->seek.is_pending) {
		if(!cdr_perform_seek(cdr, SEEK_TYPE_LOGICAL)) {
			return;
		}
		log_debug("-> Read seek to "MSF_FMT"", MSF_ARG(cdr->loc));
		next_stat = STAT_SEEKING;
	}

	if(cdr->state & STAT_READING) {
		return;
	}

	cdr_set_drive_state(cdr, next_stat);
	cdr->drive_event.trigger = read_drive_evcb;
	cdr->drive_event.eta = cdr_get_read_delay(cdr) * SEEK_DELAY_FACTOR;
	// kickstart the read loop
	psx_sched_add_ev(sched, &cdr->drive_event);
}

void CdlRead(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlRead("MSF_FMT")", MSF_ARG(cdr->loc));
	cdr_schedule_ack_ev(cdr, 3, .trigger = read_ack_evcb);
}

static void spinup_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(self);
		return;
	}
	cdr->state |= STAT_MOTOR_ON;
	put_comp_response(&cdr->state, 1);
	def_comp_evcb(sched, self);
}

void CdlMotorOn(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlMotorOn()");
	if(cdr->state & STAT_MOTOR_ON) {
		enq_error(cdr, CDROM_ERR_PARAMETERS, 0);
		return;
	}
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	cdr_schedule_ack_ev(cdr, 2, .trigger = spinup_comp_evcb);
}

static void stop_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	// clear read/play -> ACK -> complete -> turn motor off/reset position
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(self);
		return;
	}
	def_ack_evcb(sched, self);
	
	uint8_t new_state = cdr->state & ~STAT_MOTOR_ON;
	put_comp_response(&new_state, 1);
	cdr_schedule_comp_ev(cdr, 2, .eta = cdr_get_stop_delay(cdr));
	if(cdr->state != new_state) {
		cdr->state &= ~STAT_MOTOR_ON;
	}
	// cdr->loc = beginning of first track;
	cdr->loc = 150;
}

void CdlStop(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlStop()");
	cdr_set_drive_state(cdr, 0);
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = stop_ack_evcb);
}

static void pause_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(self);
		return;
	}
	def_ack_evcb(sched, self);
	
	uint8_t new_state = cdr->state & ~(STAT_READING | STAT_PLAYING);
	put_comp_response(&new_state, 1);
	cdr_schedule_comp_ev(cdr, 2, .eta = cdr_get_pause_delay(cdr));
	if(cdr->state != new_state) {
		log_debug("CDROM: Read command aborted via Pause");
		cdr_set_drive_state(cdr, 0);
	}
}

void CdlPause(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlPause()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = pause_ack_evcb);
}

static void init_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	// keep the shell open bit
	cdr->state &= STAT_SHELL_OPEN;
	cdr->state |= STAT_MOTOR_ON;
	cdr->disc_mode = MODE_SECTOR_SIZE;
	put_comp_response(&cdr->state, 1);
	def_comp_evcb(sched, self);
}

void CdlInit(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlInit()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	// ensure that all other commands are canceled
	if(cdr->comp.active) {
		cdr_remove_ev(cdr->sys->sched, &cdr->comp);
	}
	cdr->regs.ctrl &= ~CTRL_DATA_REQUEST;
	cdr_set_drive_state(cdr, 0);
	cdr_schedule_comp_ev(cdr, 2, .trigger = init_comp_evcb, .eta = ACK_TIMESTAMP + CDROM_CMD_INIT_DELAY);
}

void CdlMute(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlMute()");
	cdr->muted = true;
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlDemute(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlDemute()");
	cdr->muted = false;
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlSetfilter(struct psx_cdrom* cdr) {
	cdr->xa.file = cdr_pop_param(cdr);
	cdr->xa.channel = cdr_pop_param(cdr);
	log_debug("CDROM: CdlSetfilter() -> XA file: 0x%02x, XA channel: 0x%02x", cdr->xa.file, cdr->xa.channel);
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlSetmode(struct psx_cdrom* cdr) {
	cdr->disc_mode = cdr_pop_param(cdr);
	log_debug("CDROM: CdlSetmode() -> 0x%02x", cdr->disc_mode);
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlGetTN(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetTN()");
	uint8_t track_nums[] = { cdr->state, 0x01, BYTE_TO_BCD(cdr->disc->n_tracks) };
	put_ack_response(track_nums, sizeof(track_nums));
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlGetTD(struct psx_cdrom* cdr) {
	int tn = cdr_pop_param(cdr);
	int track_idx;
	if(tn == 0) {
		track_idx = cdr->disc->n_tracks - 1;
	} else {
		track_idx = BCD_TO_BYTE(tn) - 1;
		if(INVALID_BCD(tn) || track_idx >= cdr->disc->n_tracks) {
			enq_error(cdr, CDROM_ERR_INVALID_ARGUMENT, 0);
			return;
		}
	}
	psx_disc_track_t* track = &cdr->disc->tracks[track_idx];
	psx_lba_t tpos = tn ? track->abs_start + track->pregap_len : track->abs_end;

	uint8_t tm = BYTE_TO_BCD(tpos / 4500);
	uint8_t ts = BYTE_TO_BCD((tpos / 75) % 60);
	log_debug("CDROM: CdlGetTD(%02x) -> %02x:%02x", tn, tm, ts);
	uint8_t track_data[] = { cdr->state, tm, ts };
	put_ack_response(track_data, sizeof(track_data));
	cdr_schedule_ack_ev(cdr, 3);
}

static void seek_drive_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	// what if cdr->seek.is_pending == false?
	cdr->state &= ~STAT_SEEKING;
	psx_sched_remove_ev(sched, self->id);
	if(!cdr_perform_seek(cdr, cdr->seek.type)) {
		uint8_t err[] = { cdr->state | STAT_SEEK_ERROR, CDROM_ERR_SEEK_FAILED };
		put_async_response(err, 2);
		cdr->async_irq.ev.eta = MAKE_DELAY_SECS(1);
		psx_sched_add_ev(sched, &cdr->async_irq.ev);
		return;
	}

	put_async_response(&cdr->state, 1);
	cdr_schedule_async_irq(cdr, 2);
}

static void seek_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		RESCHEDULE(self);
		return;
	}
	uint32_t completion_delay = cdr_get_read_delay(cdr) * SEEK_DELAY_FACTOR;
	if(!(cdr->state & STAT_MOTOR_ON)) {
		cdr->state |= STAT_MOTOR_ON;
		completion_delay += cdr_get_avg_delay(cdr);
	}

	if(cdr->state & STAT_READING) {
		log_debug("CDROM: Read command aborted via Seek");
	}
	cdr_set_drive_state(cdr, STAT_SEEKING);
	put_ack_response(&cdr->state, 1);
	def_ack_evcb(sched, self);

	cdr->drive_event.trigger = seek_drive_evcb;
	cdr->drive_event.eta = completion_delay;
	psx_sched_add_ev(sched, &cdr->drive_event);
}

void CdlSeek(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlSeek()");

	cdr_schedule_ack_ev(cdr, 3, .trigger = seek_ack_evcb);
}

static uint8_t dummy_date[] = { 0x94, 0x09, 0x19, 0xc0 };

void CdlTest(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlTest()");
	uint8_t subfunc = cdr_pop_param(cdr);
	switch(subfunc) {
	case 0x20:
		put_ack_response(dummy_date, 4);
		cdr_schedule_ack_ev(cdr, 3);
		break;
	case 0x04:
		put_ack_response(&cdr->state, 1);
		cdr_schedule_ack_ev(cdr, 3);
		break;
	case 0x05:
		;uint8_t response[] = { cdr->state, 0, 0 };
		put_ack_response(response, sizeof(response));
		cdr_schedule_ack_ev(cdr, 3);
		break;
	default:
		log_error("CdlTest(): unhandled subfunction 0x%x", subfunc);
		enq_error(cdr, CDROM_ERR_INVALID_ARGUMENT, 0);
		break;
	}
}

void CdlGetID(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetID()");
	if(cdr->shell_open) {
		enq_error(cdr, CDROM_ERR_RESP_NOT_READY, 0);
		return;
	}

	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	struct {
		uint8_t stat;
		uint8_t flags;
		uint8_t disc_type;
		uint8_t atip;
		char validation_str[4];
	} response = { 0 };
	
	response.stat = cdr->state;
	if(cdr->disc) {
		// TODO: USA region is forced here
		const char scex_str[] = "SCEA";
		switch(cdr->disc_type) {
		case PSX_DT_LICENSED:
			response.disc_type = MODE2_DISC_FLAG;
			memcpy(response.validation_str, scex_str, 4);
			break;
		case PSX_DT_AUDIO:
			response.disc_type = AUDIO_DISC_FLAG;
			response.flags |= IDFLAG_AUDIO | IDFLAG_UNLICENSED;
			break;
		default:
		case PSX_DT_INVALID:
			response.flags |= IDFLAG_UNLICENSED;
			break;
		}
	} else {
		response.flags |= IDFLAG_NO_DISC | IDFLAG_UNLICENSED;
	}

	int intr = 2;
	if(response.flags & IDFLAG_UNLICENSED) {
		intr = 5;
		response.stat |= STAT_ID_ERROR;
	}
	put_comp_response(&response, sizeof(response));
	cdr_schedule_comp_ev(cdr, intr);
}

void CdlGetlocL(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetlocL()");
	put_ack_response(&cdr->sector.data[SECTOR_HDR_OFF], 8);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlGetlocP(struct psx_cdrom* cdr) {
	uint8_t track = BYTE_TO_BCD(cdr->current_track.idx + 1);
	psx_lba_t data_start = cdr->current_track.start + cdr->disc->tracks[cdr->current_track.idx].pregap_len;
	uint8_t index = (cdr->loc >= data_start) ? 1 : 0;

	psx_lba_t rel = (index == 0) ? data_start - cdr->loc : cdr->loc - cdr->current_track.start;
	uint8_t cm = BYTE_TO_BCD(rel / 4500);
	uint8_t cs = BYTE_TO_BCD((rel / 75) % 60);
	uint8_t cf = BYTE_TO_BCD(rel % 75);
	uint8_t gm = BYTE_TO_BCD(cdr->loc / 4500);
	uint8_t gs = BYTE_TO_BCD((cdr->loc / 75) % 60);
	uint8_t gf = BYTE_TO_BCD(cdr->loc % 75);
	log_debug("CDROM: CdlGetlocP() -> track=%02d, index=%02d @ track %02x:%02x:%02x | global %02x:%02x:%02x", 
			track, index, cm, cs, cf, gm, gs, gf);
	uint8_t response[] = { track, index, cm, cs, cf, gm, gs, gf };
	put_ack_response(response, sizeof(response));
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlReadTOC(struct psx_cdrom* cdr) {
	log_error("CDROM: CdlReadTOC() -> invalid");
	enq_error(cdr, CDROM_ERR_INVALID_COMMAND, 0);
}

