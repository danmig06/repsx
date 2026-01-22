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
#define MSF_TO_LBA(m, s, f) (((m * 60 + s) * 75) + f)
#define BCD_TO_BYTE(b) (uint8_t)((((b) & 0xf0) >> 4) * 10 + ((b) & 0x0f))
#define BYTE_TO_BCD(b) ((((b) / 10) << 4) | ((b) % 10))
#define ACK_TIMESTAMP (ack_ev.ev.eta)
#define SECTOR_HDR_OFF 12
#define IRQ_RETRY_RATE 50

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
	.ev.clocks_left = 0,
	.active = false
};

static struct cdr_event completion_ev = {
	.ev.id = PSX_SEV_ID_CDROM_RESP2,
	.ev.eta = CDROM_CMD_AVG_DELAY,
	.ev.clocks_left = 0,
	.active = false
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
	uint8_t submode;
	uint8_t coding_info;

	/*
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
	*/
} xa_hdr_t;

static void cdr_raise_irq(struct psx_cdrom* cdr, uint8_t ival) {
	INT_FLAGS_SET(cdr->regs.irq_status, ival);
	if(cdr->regs.irq_status & cdr->regs.irq_mask) {
		log_debug("CDROM: INT%d fired", INT_FLAGS_GET(cdr->regs.irq_status));
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
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		log_error("CDROM: acknowledge should've been delayed");
		psx_sched_remove_ev(sched, _self->id);
		_self->eta = IRQ_RETRY_RATE;
		psx_sched_add_ev(sched, _self);
		return;
	}
	queue_clear(cdr->resp_queue);
	if(self->resp_size > 0) {
		queue_push_buf(cdr->resp_queue, self->response, self->resp_size);
		cdr->regs.ctrl |= CTRL_RESULT_READY;
		self->resp_size = 0;
	}
	cdr_raise_irq(cdr, self->ival);
	cdr->regs.ctrl &= ~CTRL_BUSY;

	cdr_remove_ev(sched, self);
}

static void def_comp_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	struct cdr_event* self = (struct cdr_event*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		log_error("CDROM: completion should've been delayed");
		psx_sched_remove_ev(sched, _self->id);
		_self->eta = IRQ_RETRY_RATE;
		psx_sched_add_ev(sched, _self);
		return;
	}
	queue_clear(cdr->resp_queue);
	if(self->resp_size > 0) {
		queue_push_buf(cdr->resp_queue, self->response, self->resp_size);
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
	_cdr_schedule_int_ev(cdr, &ack_ev, ival, &(struct psx_sev){ \
			.eta = cdr_get_avg_delay(cdr), \
			.trigger = def_ack_evcb, __VA_ARGS__ })
#define cdr_schedule_comp_ev(cdr, ival, ...) \
	_cdr_schedule_int_ev(cdr, &completion_ev, ival, &(struct psx_sev){ \
			.eta = ack_ev.ev.eta + cdr_get_avg_delay(cdr), \
			.trigger = def_comp_evcb, __VA_ARGS__ })

static void _cdr_schedule_int_ev(struct psx_cdrom* cdr, struct cdr_event* cdev, int ival, struct psx_sev* ev_fields) {
	if(cdev->active) {
		log_error("CDROM: Canceling event because another is already active (busy=%d)", (cdr->regs.ctrl & CTRL_BUSY) != 0);
		cdr_remove_ev(cdr->sys->sched, cdev);
	}
	cdev->ival = ival;
	cdev->ev.trigger = ev_fields->trigger;
	cdev->ev.eta = ev_fields->eta;
	cdev->active = true;
	psx_sched_add_ev(cdr->sys->sched, (struct psx_sev*)cdev);
}

static void enq_error(struct psx_cdrom* cdr, uint8_t err_byte, uint8_t status) {
	if(!(status & (STAT_SEEK_ERROR | STAT_ID_ERROR))) {
		status |= STAT_ERROR;
	}

	ack_ev.response[0] = cdr->state | (status & (STAT_SEEK_ERROR | STAT_ID_ERROR | STAT_ERROR));
	ack_ev.resp_size = 1;
	if(status & (STAT_SEEK_ERROR | STAT_ERROR)) {
		ack_ev.response[1] = err_byte;
		ack_ev.resp_size++;
	}
	cdr_schedule_ack_ev(cdr, 5);
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
		enq_error(cdr, CDROM_ERR_PARAMETERS, 0);
		return;
	}
	cdr->regs.ctrl |= CTRL_BUSY;
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
	case CMD_PLAY:
		if(queue_items(cdr->param_queue) > 1) {
			enq_error(cdr, CDROM_ERR_PARAMETERS, 0);
			break;
		}
		cdr->regs.ctrl |= CTRL_BUSY;
		CdlPlay(cdr);
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
	case CMD_MUTE:
		run_cmd(cdr, 0, CdlMute);
		break;
	case CMD_DEMUTE:
		run_cmd(cdr, 0, CdlDemute);
		break;
	case CMD_SETFILTER:
		run_cmd(cdr, 2, CdlSetfilter);
		break;
	case CMD_GETLOCL:
		run_cmd(cdr, 0, CdlGetlocL);
		break;
	case CMD_GETLOCP:
		run_cmd(cdr, 0, CdlGetlocP);
		break;
	case CMD_GETTN:
		run_cmd(cdr, 0, CdlGetTN);
		break;
	case CMD_GETTD:
		run_cmd(cdr, 1, CdlGetTD);
		break;
	case CMD_SEEKL:
	case CMD_SEEKP:
		run_cmd(cdr, 0, CdlSeek);
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
	case CMD_READTOC:
		run_cmd(cdr, 0, CdlReadTOC);
		break;
	default:
		log_fatal("CDROM: unhandled command 0x%02x", cdr->regs.command);
		break;
	}
	queue_clear(cdr->param_queue);
	cdr->regs.ctrl |= CTRL_PARAM_EMPTY | CTRL_PARAM_READY;
}

void CdlNop(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlNop() -> 0x%02x", cdr->state);
	// TODO: reportedly "CdlNop resets the ShellOpen flag for all subsequent commands, unless the shell is still open"
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

static void play_comp_evcb(struct psx_sched* sched, struct psx_sev* _self) { 
	struct cdr_event* self = (struct cdr_event*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(cdr->state & STAT_SEEKING) {
		cdr->state &= ~STAT_SEEKING;
		cdr->state |= STAT_PLAYING;
	}
	uint32_t delay = cdr_get_read_delay(cdr);
	if(!(cdr->disc_mode & MODE_REPORT)) {
		goto play_end;
	}
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		log_error("CDROM: completion should've been delayed");
		delay = IRQ_RETRY_RATE;
		goto play_end;
	}
	queue_clear(cdr->data_queue);
	// read CDDA sector
	if(cdr->loc % 16 == 0) {
		log_debug("CDROM: play stub hit");
		uint8_t cm, cs, cf;
		if(cdr->report_absolute) {
			uint32_t loc = cdr->loc - 150;
			cm = BYTE_TO_BCD((loc / 4500) % 60);
			cs = BYTE_TO_BCD((loc / 75) % 60) + 0x80;
			cf = BYTE_TO_BCD(loc % 75);
		} else {
			uint32_t global_loc = cdr->loc;
			cm = BYTE_TO_BCD((global_loc / 4500) % 60);
			cs = BYTE_TO_BCD((global_loc / 75) % 60);
			cf = BYTE_TO_BCD(global_loc % 75);
		}
		cdr->report_absolute = !cdr->report_absolute;

		int track = 1;
		uint8_t response[] = { cdr->state, track, 1, cm, cs, cf, 0x80, 0x80 };
		queue_clear(cdr->resp_queue);
		queue_push_buf(cdr->resp_queue, response, sizeof(response));
		cdr->regs.ctrl |= CTRL_RESULT_READY;
		cdr_raise_irq(cdr, self->ival);
	}
	cdr->loc++;
play_end:
	psx_sched_remove_ev(sched, _self->id);
	_self->eta = delay; 
	psx_sched_add_ev(sched, _self);
}

static void play_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	// handle the selected track
	// if cdr->next_track == 0 seek to setloc
	cdr->state |= STAT_SEEKING;
	put_ack_response(&cdr->state, 1);
	def_ack_evcb(sched, self);

	uint32_t delay = cdr_get_read_delay(cdr) * 4;

	// kickstart the play loop
	cdr_schedule_comp_ev(cdr, 1, .trigger = play_comp_evcb, .eta = delay);
}

void CdlPlay(struct psx_cdrom* cdr) {
	int track = 0;
	if(queue_items(cdr->param_queue)) {
		track = cdr_pop_param(cdr);
	}
	// cdr->next_track = track;
	log_error("CdlPlay(%02x)", track);
	cdr_schedule_ack_ev(cdr, 3, .trigger = play_ack_evcb);
}

static void read_comp_evcb(struct psx_sched* sched, struct psx_sev* _self) {
	struct cdr_event* self = (struct cdr_event*)_self;
	struct psx_cdrom* cdr = sched->sys->cdrom;
	uint32_t delay;
	if(INT_FLAGS_GET(cdr->regs.irq_status) != 0) {
		log_error("CDROM: completion should've been delayed");
		delay = IRQ_RETRY_RATE;
		goto read_end;
	}
	queue_clear(cdr->data_queue);

	// clear previous seek state if any
	if(cdr->state & STAT_SEEKING) {
		cdr->state &= ~STAT_SEEKING;
		cdr->state |= STAT_READING;
	}
	delay = cdr_get_read_delay(cdr);
	queue_clear(cdr->resp_queue);
	queue_push(cdr->resp_queue, cdr->state);
	cdr->regs.ctrl |= CTRL_RESULT_READY;

	int sync_size = (cdr->disc_mode & MODE_SECTOR_SIZE) ? 12 : 24;
	cdr->disc->read_sector(cdr->disc->host_data, cdr->loc++, cdr->data_queue->buf);

	sector_hdr_t* header = (void*)&cdr->data_queue->buf[SECTOR_HDR_OFF];
	xa_hdr_t* subheader = (void*)&cdr->data_queue->buf[SECTOR_HDR_OFF + sizeof(header)];
	if(header->mode == 2 && (cdr->disc_mode & MODE_XA)) {
		if((subheader->submode & XA_SM_AUDIO) && (subheader->submode & XA_SM_REALTIME)) {
			log_debug("CDROM: XA sector skipped at LBA %d", cdr->loc - 1);
			goto read_end;
		}
	}

	cdr->data_queue->read_off = sync_size;
	cdr->data_queue->write_off = cdr->data_queue->read_off + ((cdr->disc_mode & MODE_SECTOR_SIZE) ? 0x924 : 0x800);
	log_debug("CDROM: read at LBA %d | mode=0x%02x submode=0x%02x", cdr->loc - 1, header->mode, subheader->submode);
	cdr->regs.ctrl |= CTRL_DATA_REQUEST;
	cdr_raise_irq(cdr, self->ival);
read_end:
	psx_sched_remove_ev(sched, _self->id);
	_self->eta = delay; 
	psx_sched_add_ev(sched, _self);
}

static void read_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	if(cdr->seek.is_pending) {
		cdr->loc = cdr->seek.loc;
		cdr->seek.is_pending = false;
		if(cdr->state & STAT_READING) {
			cdr->state &= ~STAT_READING;
			cdr_remove_ev(sched, &completion_ev);
		}
		uint8_t cm = BYTE_TO_BCD((cdr->loc / 4500) % 60);
		uint8_t cs = BYTE_TO_BCD((cdr->loc / 75) % 60);
		uint8_t cf = BYTE_TO_BCD(cdr->loc % 75);
		log_debug("-> Read seek to %02x:%02x:%02x", cm, cs, cf);
	}
	cdr->state |= STAT_SEEKING;
	put_ack_response(&cdr->state, 1);
	def_ack_evcb(sched, self);

	// cdr_get_read_delay(cdr) * 4 and lower values will hang Legend of Dragoon
	uint32_t delay = cdr_get_read_delay(cdr) * 5;

	// kickstart the read loop
	cdr_schedule_comp_ev(cdr, 1, .trigger = read_comp_evcb, .eta = delay);
}

void CdlRead(struct psx_cdrom* cdr) {
	uint32_t loc = cdr->loc - (150 + (cdr->state & STAT_READING) ? 1 : 0);
	uint8_t cm = BYTE_TO_BCD((loc / 4500) % 60);
	uint8_t cs = BYTE_TO_BCD((loc / 75) % 60);
	uint8_t cf = BYTE_TO_BCD(loc % 75);
	log_debug("CDROM: CdlRead(%02x:%02x:%02x)", cm, cs, cf);
	// no disc error?
	cdr_schedule_ack_ev(cdr, 3, .trigger = read_ack_evcb);
}

static void spinup_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
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

static void stop_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	cdr->state &= ~STAT_MOTOR_ON;
	put_comp_response(&cdr->state, 1);
	// cdr->loc = beginning of first track;
	cdr->loc = 0;
	def_comp_evcb(sched, self);
}

static void stop_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	// ACK -> clear read/play -> complete -> turn motor off/reset position
	struct psx_cdrom* cdr = sched->sys->cdrom;
	def_ack_evcb(sched, self);
	
	if(cdr->state & (STAT_READING | STAT_PLAYING)) {
		log_debug("CDROM: Read command aborted via Stop");
		cdr->state &= ~(STAT_READING | STAT_PLAYING);
		cdr_remove_ev(sched, &completion_ev);
	}
	
	put_comp_response(&cdr->state, 1);
	cdr_schedule_comp_ev(cdr, 2, .eta = cdr_get_stop_delay(cdr), .trigger = stop_comp_evcb);
}

void CdlStop(struct psx_cdrom* cdr) {
	log_error("CDROM: CdlStop()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = stop_ack_evcb);
}

static void pause_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	def_ack_evcb(sched, self);
	
	if(cdr->state & (STAT_READING | STAT_PLAYING)) {
		log_debug("CDROM: Read command aborted via Pause");
		cdr->state &= ~(STAT_READING | STAT_PLAYING);
		cdr_remove_ev(sched, &completion_ev);
	}
	
	put_comp_response(&cdr->state, 1);
	cdr_schedule_comp_ev(cdr, 2, .eta = cdr_get_pause_delay(cdr));
}

void CdlPause(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlPause()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = pause_ack_evcb);
}

static void init_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	cdr->state = STAT_MOTOR_ON;
	cdr->disc_mode = MODE_SECTOR_SIZE;
	put_comp_response(&cdr->state, 1);
	def_comp_evcb(sched, self);
}

void CdlInit(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlInit()");
	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	// ensure that all other commands are canceled
	if(completion_ev.active) {
		cdr_remove_ev(cdr->sys->sched, &completion_ev);
	}

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
	uint8_t file = cdr_pop_param(cdr);
	uint8_t chan = cdr_pop_param(cdr);
	log_debug("CDROM: CdlSetfilter() -> XA file: 0x%02x, XA channel: 0x%02x", file, chan);
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
	uint8_t dummy_track_data[] = { cdr->state, 0x01, 0x01 };
	put_ack_response(dummy_track_data, sizeof(dummy_track_data));
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlGetTD(struct psx_cdrom* cdr) {
	int track = cdr_pop_param(cdr);
	log_debug("CDROM: CdlGetTD(%02x)", track);
	uint8_t tm, ts;
	tm = 0;
	if(track == 0) {
		// get current track size
		ts = 4;
	} else {
		ts = 2;
	}
	uint8_t dummy_track_data[] = { cdr->state, tm, ts };
	put_ack_response(dummy_track_data, sizeof(dummy_track_data));
	cdr_schedule_ack_ev(cdr, 3);
}

static void seek_comp_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	// what if cdr->seek.is_pending == false?
	cdr->loc = cdr->seek.loc;
	cdr->seek.is_pending = false;
	cdr->state &= ~STAT_SEEKING;
	put_comp_response(&cdr->state, 1);
	def_comp_evcb(sched, self);
}

static void seekl_ack_evcb(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_cdrom* cdr = sched->sys->cdrom;
	// dynamic calculation?? includes a random additional 0.5~1.0ms delay
	uint32_t completion_delay = cdr_get_read_delay(cdr) * 5;
	if(!(cdr->state & STAT_MOTOR_ON)) {
		cdr->state |= STAT_MOTOR_ON;
		completion_delay += cdr_get_avg_delay(cdr);
	}

	if(cdr->state & STAT_READING) {
		log_debug("CDROM: Read command aborted via Seek");
		cdr->state &= ~STAT_READING;
		cdr_remove_ev(sched, &completion_ev);
	}

	cdr->state |= STAT_SEEKING;
	put_ack_response(&cdr->state, 1);
	def_ack_evcb(sched, self);
	cdr_schedule_comp_ev(cdr, 2, .trigger = seek_comp_evcb, .eta = completion_delay);
}

void CdlSeek(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlSeek()");

	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3, .trigger = seekl_ack_evcb);
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
		break;
	}
}

void CdlGetID(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetID()");
	if(cdr->state & STAT_SHELL_OPEN /* OR spin-up OR detect-busy */) {
		enq_error(cdr, CDROM_ERR_RESP_NOT_READY, 0);
		return;
	}

	put_ack_response(&cdr->state, 1);
	cdr_schedule_ack_ev(cdr, 3);
	struct __attribute__((packed)) {
		uint8_t stat;
		uint8_t flags;
		uint8_t disc_type;
		uint8_t atip;
		char validation_str[4];
	} response = { 0 };
	
	response.stat = cdr->state;
	if(cdr->disc) {
		// TODO: USA region is forced here
		char scex_str[] = "SCEA";
		switch(cdr->disc->type) {
		case PSX_DT_LICENSED:
			response.disc_type = MODE2_DISC_FLAG;
			memcpy(response.validation_str, scex_str, 4);
			break;
		case PSX_DT_AUDIO:
			response.disc_type = AUDIO_DISC_FLAG;
			response.flags |= IDFLAG_AUDIO;
			memcpy(response.validation_str, scex_str, 4);
			break;
		default:
		case PSX_DT_INVALID:
			response.stat |= STAT_ID_ERROR;
			response.flags |= IDFLAG_UNLICENSED;
			break;
		}
	} else {
		response.flags |= IDFLAG_NO_DISC;
	}

	put_comp_response(&response, sizeof(response));
	cdr_schedule_comp_ev(cdr, (response.stat & STAT_ID_ERROR) ? 5 : 2);
}

void CdlGetlocL(struct psx_cdrom* cdr) {
	log_debug("CDROM: CdlGetlocL()");
	put_ack_response(&cdr->data_queue->buf[SECTOR_HDR_OFF], 8);
	cdr_schedule_ack_ev(cdr, 3);
}

void CdlGetlocP(struct psx_cdrom* cdr) {
	uint8_t track = 1, index = 1;
	uint32_t loc = cdr->loc - 150;
	uint8_t cm = BYTE_TO_BCD((loc / 4500) % 60);
	uint8_t cs = BYTE_TO_BCD((loc / 75) % 60);
	uint8_t cf = BYTE_TO_BCD(loc % 75);
	uint8_t gm = BYTE_TO_BCD((cdr->loc / 4500) % 60);
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

