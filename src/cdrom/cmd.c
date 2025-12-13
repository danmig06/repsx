#include <psx/sched.h>
#include <psx/irq.h>
#include <psx/cpu.h>
#include <psx/disc.h>

#include "cmd.h"
#include "queue.h"
#include "../util.h"
#include "../log.h"

#include <string.h>

#define MODE2_DISC_FLAG 0x20
#define AUDIO_DISC_FLAG 0
#define MSF_TO_LBA(m, s, f) (((m * 60 + s) * 75) + f)
#define BCD_TO_BYTE(b) (uint8_t)((((b) & 0xf0) >> 4) * 10 + ((b) & 0x0f))
#define BYTE_TO_BCD(b) ((((b) / 10) << 4) | ((b) % 10))
#define ACK_TIMESTAMP (ack_ev.ev.eta)

struct cdr_event {
	struct psx_sev ev;
	uint8_t response[PSX_CDROM_RESPBUF_SIZE];
	uint8_t resp_size;
	uint8_t ival;
	bool active;
};

static struct cdr_event ack_ev = {
	.ev.id = PSX_SEV_ID_CDROM_RESP1,
	.ev.eta = CDROM_CMD_AVG_DELAY,
	.ev.clocks_left = 0
};

static struct cdr_event completion_ev = {
	.ev.id = PSX_SEV_ID_CDROM_RESP2,
	.ev.eta = CDROM_CMD_AVG_DELAY,
	.ev.clocks_left = 0
};

typedef struct sector_header {
	uint8_t minute;
	uint8_t second;
	uint8_t frame;
	uint8_t mode;
} sector_hdr_t;

// TODO: move this elsewhere
typedef struct xa_header {
	uint8_t file;
	uint8_t channel;

	struct {
		bool eor: 1;
		bool is_video: 1;
		bool is_audio: 1;
		bool is_data: 1;
		bool trigger: 1;
		bool form2: 1;
		bool realtime: 1;
		bool eof: 1;
	} submode;

	struct {
		uint8_t is_stereo: 2;
		uint8_t sample_rate: 2;
		uint8_t sample_bits: 2;
		bool emphasis: 1;
		uint8_t unused: 1;
	} coding_info;
} xa_hdr_t;

static void cdr_raise_irq(struct psx_cdrom* cdr, uint8_t ival) {
	cdr->regs.irq_status.flags = ival;
	if(AS_UINT8(cdr->regs.irq_status) & AS_UINT8(cdr->regs.irq_mask)) {
		log_debug("CDROM: INT%d fired", cdr->regs.irq_status.flags);
		psx_irq_raise(cdr->sys->irq, PSX_IRQ_ID_CDROM);
	}
}

static void cdr_remove_ev(struct psx_sched* sched, struct cdr_event* ev) {
	psx_sched_remove_ev(sched, ev->ev.id);
	ev->active = false;
}

static void def_ack_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	struct cdr_event* self = (struct cdr_event*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(cdr->regs.irq_status.flags != 0) {
		log_error("CDROM: acknowledge should've been delayed");
	}
	queue_clear(cdr->resp_queue);
	if(self->resp_size > 0) {
		queue_push_buf(cdr->resp_queue, self->response, self->resp_size);
		self->resp_size = 0;
	}
	cdr_raise_irq(cdr, self->ival);
	cdr->regs.ctrl.busy = false;

	cdr_remove_ev(sched, self);
}

static void def_comp_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	struct cdr_event* self = (struct cdr_event*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(cdr->regs.irq_status.flags != 0) {
		log_error("CDROM: completion should've been delayed");
	}
	queue_clear(cdr->resp_queue);
	if(self->resp_size > 0) {
		queue_push_buf(cdr->resp_queue, self->response, self->resp_size);
		self->resp_size = 0;
	}
	cdr_raise_irq(cdr, self->ival);

	cdr_remove_ev(sched, self);
}

static uint32_t cdr_get_avg_delay(struct psx_cdrom* cdr) {
	return (cdr->state.motor_on) ? CDROM_CMD_AVG_DELAY : CDROM_CMD_AVG_SDELAY;
}

static uint32_t cdr_get_read_delay(struct psx_cdrom* cdr) {
	return (cdr->disc_mode.double_speed) ? (PSX_CPU_CLOCKS_PER_SEC / 150) : (PSX_CPU_CLOCKS_PER_SEC / 75);
}

static uint32_t cdr_get_pause_delay(struct psx_cdrom* cdr) {
	if(!cdr->state.reading && !cdr->state.playing) {
		return CDROM_CMD_PAUSE_NOPDELAY;	
	}

	return (cdr->disc_mode.double_speed) ? CDROM_CMD_PAUSE_DSDELAY : CDROM_CMD_PAUSE_DELAY;
}

static uint32_t cdr_get_stop_delay(struct psx_cdrom* cdr) {
	if(!cdr->state.motor_on) {
		return CDROM_CMD_STOP_NOPDELAY;	
	}

	return (cdr->disc_mode.double_speed) ? CDROM_CMD_STOP_DSDELAY : CDROM_CMD_STOP_DELAY;
}

#define cdr_schedule_ack_ev(cdr, ival, ...) \
	_cdr_schedule_int_ev(cdr, &ack_ev, ival, &(struct psx_sev){ \
			.eta = cdr_get_avg_delay(cdr), \
			.trigger = def_ack_evcb, __VA_ARGS__ })
#define cdr_schedule_comp_ev(cdr, ival, ...) \
	_cdr_schedule_int_ev(cdr, &completion_ev, ival, &(struct psx_sev){ \
			.eta = ack_ev.ev.eta + cdr_get_avg_delay(cdr), \
			.trigger = def_comp_evcb, __VA_ARGS__ })

static void _cdr_schedule_int_ev(struct psx_cdrom* cdr, struct cdr_event* cdev, int ival, struct psx_sev* ev_fields) {
	if(cdev->active) {
		log_error("CDROM: Canceling event because another is already active");
		cdr_remove_ev(cdr->sys->sched, cdev);
	}
	cdev->ival = ival;
	cdev->ev.trigger = ev_fields->trigger;
	cdev->ev.eta = ev_fields->eta;
	cdev->active = true;
	psx_sched_add_ev(cdr->sys->sched, (struct psx_sev*)cdev);
}

#define enq_error(cdr, eb, ...) enq_error_stat(cdr, eb, (psx_cdr_stat_t){ __VA_ARGS__ })
static void enq_error_stat(struct psx_cdrom* cdr, uint8_t err_byte, psx_cdr_stat_t status) {
	if(!status.seek_error && !status.id_error) {
		status.error = true;
	}

	ack_ev.response[0] = AS_UINT8(status);
	ack_ev.resp_size = 1;
	if(status.seek_error || status.error) {
		ack_ev.response[1] = err_byte;
		ack_ev.resp_size++;
	}
	cdr_schedule_ack_ev(cdr, 5);
}

static inline psx_cdr_stat_t cdr_get_stat(struct psx_cdrom* cdr) {
	// error bits are always cleared
	return cdr->state;
}

#define put_ack_response(rb, sz) _put_response(&ack_ev, rb, sz)
#define put_comp_response(rb, sz) _put_response(&completion_ev, rb, sz)
static void _put_response(struct cdr_event* ev, void* rb, uint8_t size) {
	if(ev->resp_size >= PSX_CDROM_RESPBUF_SIZE) {
		return;
	}

	if(size > (PSX_CDROM_RESPBUF_SIZE - ev->resp_size)) {
		size = PSX_CDROM_RESPBUF_SIZE - ev->resp_size;
	}

	memcpy(&ev->response[ev->resp_size], rb, size);
	ev->resp_size += size;
}

static uint8_t cdr_pop_param(struct psx_cdrom* cdr) {
	return queue_pop(cdr->param_queue);
}

static void run_cmd(struct psx_cdrom* cdr, unsigned n_args, void (*cmd_func)(struct psx_cdrom*)) {
	if(queue_items(cdr->param_queue) != n_args) {
		enq_error(cdr, CDROM_ERR_PARAMETERS);
		return;
	}
	cdr->regs.ctrl.busy = true;
	cmd_func(cdr);
}

void cdr_run_cmd(struct psx_cdrom* cdr) {
	switch(cdr->regs.command) {
	case CMD_NOP:
		run_cmd(cdr, 0, CdlNop);
		break;
	case CMD_SETLOC:
		run_cmd(cdr, 3, CdlSetloc);
		break;
	case CMD_READN:
	case CMD_READS:
		run_cmd(cdr, 0, CdlRead);
		break;
	case CMD_MOTORON:
		run_cmd(cdr, 0, CdlMotorOn);
		break;
	case CMD_STOP:
		run_cmd(cdr, 0, CdlStop);
		break;
	case CMD_PAUSE:
		run_cmd(cdr, 0, CdlPause);
		break;
	case CMD_TEST:
		run_cmd(cdr, 1, CdlTest);
		break;
	case CMD_DEMUTE:
		run_cmd(cdr, 0, CdlDemute);
		break;
	case CMD_SETFILTER:
		run_cmd(cdr, 2, CdlSetfilter);
		break;
	case CMD_GETLOCP:
		run_cmd(cdr, 0, CdlGetlocP);
		break;
	case CMD_GETTN:
		run_cmd(cdr, 0, CdlGetTN);
		break;
	case CMD_SEEKL:
		run_cmd(cdr, 0, CdlSeekL);
		break;
	case CMD_SETMODE:
		run_cmd(cdr, 1, CdlSetmode);
		break;
	case CMD_INIT:
		run_cmd(cdr, 0, CdlInit);
		break;
	case CMD_GETID:
		run_cmd(cdr, 0, CdlGetID);
		break;
	default:
		log_fatal("CDROM: unhandled command 0x%02x", cdr->regs.command);
		break;
	}
	queue_clear(cdr->param_queue);
	cdr->regs.ctrl.param_empty = true;
	cdr->regs.ctrl.param_wr_ready = true;
}

void CdlNop(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlNop()");
	// TODO: reportedly "CdlNop resets the ShellOpen flag for all subsequent commands, unless the shell is still open"
	psx_cdr_stat_t stat = cdr_get_stat(cdr);
	put_ack_response(&stat, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlSetloc(struct psx_cdrom* cdr) {
	uint8_t m = cdr_pop_param(cdr);
	uint8_t s = cdr_pop_param(cdr);
	uint8_t f = cdr_pop_param(cdr);
	m = BCD_TO_BYTE(m);
	s = BCD_TO_BYTE(s);
	f = BCD_TO_BYTE(f);
	log_debug("CDROM: CdlSetloc(%02hhu:%02hhu:%02hhu)", m, s, f);
	cdr->seek.loc = MSF_TO_LBA(m, s, f);
	cdr->seek.is_pending = true;
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

static void read_comp_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	struct cdr_event* self = (struct cdr_event*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	queue_clear(cdr->resp_queue);
	queue_push(cdr->resp_queue, AS_UINT8(cdr->state));
	queue_clear(cdr->data_queue);
	uint32_t delay = cdr_get_read_delay(cdr);
	if(cdr->seek.is_pending) {
		cdr->loc = cdr->seek.loc;
		cdr->seek.is_pending = false;
		cdr->state.seeking = true;
		delay = cdr_get_avg_delay(cdr);
		goto read_end;
	} else {
		cdr->state.seeking = false;
	}

	cdr->state.reading = true;
	int sync_size = (cdr->disc_mode.sector_size) ? 12 : 24;
	cdr->disc->read_sector(cdr->disc->host_data, cdr->loc++, cdr->data_queue->buf);
	cdr->data_queue->read_off = sync_size;
	cdr->data_queue->write_off = (cdr->disc_mode.sector_size) ? 0x924 : 0x800;
	cdr->data_queue->write_off += cdr->data_queue->read_off;
	sector_hdr_t* header = (void*)&cdr->data_queue->buf[12];
	xa_hdr_t* subheader = (void*)&cdr->data_queue->buf[12 + sizeof(header)];
	if(header->mode == 2 && cdr->disc_mode.xa_mode) {
		if(subheader->submode.is_audio && subheader->submode.realtime) {
			log_debug("CDROM: XA sector skipped at LBA %d", cdr->loc);
			goto read_end;
		}
	}
	log_debug("CDROM: read at LBA %d | mode=0x%02x submode=0x%02x", cdr->loc, header->mode, AS_UINT8(subheader->submode));
	cdr->regs.ctrl.data_request = true;
	cdr_raise_irq(cdr, self->ival);

read_end:
	psx_sched_remove_ev(sched, self->ev.id);
	_self->eta = delay; 
	psx_sched_add_ev(sched, _self);
}

static void read_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	def_ack_evcb(sched, self);
	// kickstart the read loop
	cdr_schedule_comp_ev(cdr, 1, .trigger = read_comp_evcb, .eta = cdr_get_read_delay(cdr));
}

void CdlRead(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlRead()");
	// no disc error?
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = read_ack_evcb);
}

static void spinup_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	cdr->state.motor_on = true;
	put_comp_response(&cdr->state, 1);
	def_comp_evcb(sched, self);
}

void CdlMotorOn(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlMotorOn");
	if(cdr->state.motor_on) {
		enq_error_stat(cdr, CDROM_ERR_PARAMETERS, cdr->state);
		return;
	}
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	cdr_schedule_ack_ev(cdr, 2, .trigger = spinup_comp_evcb);
}

static void stop_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	cdr->state.motor_on = false;
	put_comp_response(&cdr->state, 1);
	// cdr->loc = begin of current track;
	cdr->loc = 0;
	def_comp_evcb(sched, self);
}

void CdlStop(struct psx_cdrom* cdr) {
	log_error("CDROM: CdlStop()");
	if(cdr->state.reading) {
		log_debug("CDROM: Read command aborted via Stop");
		cdr->state.reading = false;
		cdr_remove_ev(cdr->sys->sched, &completion_ev);
	} // else if playing ... else return
	
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	cdr_schedule_comp_ev(cdr, 2, .eta = cdr_get_stop_delay(cdr), .trigger = stop_comp_evcb);
}

static void pause_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	def_ack_evcb(sched, self);
	
	uint32_t delay = cdr_get_pause_delay(cdr);
	if(cdr->state.reading) {
		log_debug("CDROM: Read command aborted via Pause");
		cdr->state.reading = false;
		cdr_remove_ev(sched, &completion_ev);
	} // else if playing ... else return
	
	put_comp_response(&cdr->state, 1);
	cdr_schedule_comp_ev(cdr, 2, .eta = delay);
}

void CdlPause(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlPause()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = pause_ack_evcb);
}

static uint8_t dummy_date[] = { 0x94, 0x09, 0x19, 0xc0 };

void CdlInit(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlInit()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .eta = CDROM_CMD_INIT_DELAY);
	put_comp_response(&cdr->state, 1);
	cdr_schedule_comp_ev(cdr, 2);
}

void CdlDemute(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlDemute()");
	cdr->muted = false;
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlSetfilter(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlSetfilter()");
	uint8_t file = cdr_pop_param(cdr);
	uint8_t chan = cdr_pop_param(cdr);
	log_debug("XA file: 0x%02x, XA channel: 0x%02x", file, chan);
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlSetmode(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlSetmode()");
	psx_disc_mode_t new_mode;
	AS_UINT8(new_mode) = cdr_pop_param(cdr);
	cdr->disc_mode = new_mode;
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlGetTN(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetTN()");
	uint8_t dummy_track_data[] = { AS_UINT8(cdr->state), 0x01, 0x01 };
	put_ack_response(dummy_track_data, sizeof(dummy_track_data));
	cdr_schedule_ack_ev(cdr, 3);
}

static void seek_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	def_comp_evcb(sched, self);
	cdr->state.seeking = false;
}

void CdlSeekL(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlSeekL()");
	// what if cdr->seek.is_pending == false?
	cdr->loc = cdr->seek.loc;
	cdr->seek.is_pending = false;
	cdr->state.seeking = true;
	// dynamic calculation?? includes a random additional 0.5~1.0ms delay
	uint32_t completion_delay = ACK_TIMESTAMP + cdr_get_avg_delay(cdr);
	if(!cdr->state.motor_on) {
		cdr->state.motor_on = true;
		completion_delay += cdr_get_avg_delay(cdr);
	}

	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);

	put_comp_response(&cdr->state, 1);
	cdr_schedule_comp_ev(cdr, 2, .trigger = seek_comp_evcb, .eta = completion_delay);
}

void CdlTest(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlTest()");
	uint8_t subfunc = cdr_pop_param(cdr);
	switch(subfunc) {
	case 0x20:
		put_ack_response(dummy_date, 4);
		cdr_schedule_ack_ev(cdr, 3);
		break;
	default:
		log_debug("CdlTest(): unhandled subfunction 0x%x", subfunc);
		break;
	}
}

void CdlGetID(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetID()");
	psx_cdr_stat_t ack_stat = cdr_get_stat(cdr);
	if(ack_stat.shell_open /* OR spin-up OR detect-busy */) {
		enq_error_stat(cdr, CDROM_ERR_RESP_NOT_READY, ack_stat);
		return;
	}

	put_ack_response(&ack_stat, 1);
	cdr_schedule_ack_ev(cdr, 3);
	struct {
		psx_cdr_stat_t stat;
		struct {
			uint8_t unused: 4;
			bool is_audio: 1;
			uint8_t unused1: 1;
			bool no_disc: 1;
			bool invalid: 1;
		} flags;
		uint8_t disc_type;
		uint8_t atip;
		char validation_str[4];
	} response = { 0 };
	
	response.stat = cdr_get_stat(cdr);
	if(cdr->disc) {
		// TODO: USA region is forced here
		char scex_str[] = "SCEA";
		switch(cdr->disc->type) {
		case PSX_DT_LICENSED:
			// if MODE1 is used for a data disc this won't work
			response.disc_type = MODE2_DISC_FLAG;
			memcpy(response.validation_str, scex_str, 4);
			break;
		case PSX_DT_AUDIO:
			response.disc_type = AUDIO_DISC_FLAG;
			response.flags.is_audio = true;
			// maybe not?
			memcpy(response.validation_str, scex_str, 4);
			break;
		default:
		case PSX_DT_INVALID:
			response.stat.id_error = response.flags.invalid = true;
			break;
		}
	} else {
		response.flags.no_disc = true;
	}

	put_comp_response(&response, sizeof(response));
	cdr_schedule_comp_ev(cdr, (response.stat.id_error || response.flags.no_disc) ? 5 : 2);
}

void CdlGetlocP(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetlocP()");
	uint8_t track = 1, index = 1;
	uint8_t cm = BYTE_TO_BCD((cdr->loc / 4500) % 60);
	uint8_t cs = BYTE_TO_BCD((cdr->loc / 75) % 60);
	uint8_t cf = BYTE_TO_BCD(cdr->loc % 75);
	log_debug(" -> track=%02d, index=%02d @ track %02x:%02x:%02x | global %02x:%02x:%02x", track, index, cm, cs, cf, cm, cs, cf);
	// TODO: make a getloc for the disc backend
	uint8_t response[] = { AS_UINT8(cdr->state), track, index, cm, cs, cf, cm, cs, cf };
	put_ack_response(response, sizeof(response));
	cdr_schedule_ack_ev(cdr, 3);
}


