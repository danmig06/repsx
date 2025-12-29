#include "pad.h"
#include "util.h"

#include <string.h>

#define DUALSHOCK_ID(pad) ((pad->config_mode) ? 0xf3 : (pad->analog_mode) ? 0x73 : 0x41)
#define PAD_INDEX(pad, pad_array) (((pad) - (pad_array)) / sizeof(*(pad)))

static struct psx_pad pads[2] = { 
	{ .send = pad_send, .recv = pad_recv, .reset = pad_reset, .tx_finished = pad_tx_finished },
	{ .send = pad_send, .recv = pad_recv, .reset = pad_reset, .tx_finished = pad_tx_finished },
};

enum {
	CMD_READ_BUTTONS = 'B',
	CMD_CONFIG_MODE  = 'C',
	CMD_SET_MODE     = 'D',
	CMD_GET_MODE     = 'E',
	CMD_RUMBLE_CTRL  = 'M',
};

struct psx_pad* pad_connect(int n, void* host_data, psx_padpollfn_t poll_host) {
	n &= 1;
	pads[n].host.data = host_data;
	pads[n].host.poll = poll_host;
	memset(&pads[n].resp, 0, sizeof(pads[n].resp));
	return &pads[n];
}

void pad_reset(struct psx_pad* pad) {
	pad->session_active = false;
	memset(&pad->resp, 0, sizeof(pad->resp));
}

bool pad_send(struct psx_pad* pad, uint8_t byte) {
	if(!pad->session_active) {
		// prepare our throwaway HiZ response, pull ACK high
		pad->resp.buf[0] = 0xff;
		pad->resp.off = 0;
		pad->resp.nbytes = 1;
		pad->session_active = true;
		return true;
	}

	psx_btnstate_t buttons;
	switch(byte) {
	case CMD_READ_BUTTONS:
		buttons = pad->host.poll(pad->host.data, PAD_INDEX(pad, pads));
		uint16_t btn_data = AS_UINT16(buttons);
		// ignore analog mode for now
		pad->resp.off = 0;
		if(pad->config_mode) {
			uint8_t resp[] = { DUALSHOCK_ID(pad), 0x5a, btn_data & 0xff, btn_data >> 8, 0x80, 0x80, 0x80, 0x80 };
			memcpy(pad->resp.buf, resp, sizeof(resp));
			pad->resp.nbytes = 8;
		} else {
			uint8_t resp[] = { DUALSHOCK_ID(pad), 0x5a, btn_data & 0xff, btn_data >> 8 };
			memcpy(pad->resp.buf, resp, sizeof(resp));
			pad->resp.nbytes = 4;
		}
		break;
	default:
		if(!pad->session_active) {
			log_error("PAD: unknown command byte 0x%02x\n", byte);
			return false;
		}
		break;
	}
	// pull ACK high
	return true;
}

uint8_t pad_recv(struct psx_pad* pad) {
	if(pad->resp.off == pad->resp.nbytes) {
		log_error("PAD: read with no data");
		pad->session_active = false;
		return 0xff;
	}

	log_trace("PAD: sent back 0x%02x", pad->resp.buf[pad->resp.off]);
	return pad->resp.buf[pad->resp.off++];
}

bool pad_tx_finished(struct psx_pad* pad) {
	return (pad->resp.off == (pad->resp.nbytes - 1));
}

