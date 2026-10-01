#include "pad.h"
#include "../util.h"

#include <psx/sio.h>

#include <string.h>

#define DUALSHOCK_ID(pad) ((pad->config_mode) ? 0xf3 : (pad->analog_mode) ? 0x73 : 0x41)

enum {
	CMD_READ_BUTTONS = 'B',
	CMD_CONFIG_MODE  = 'C',
	CMD_SET_MODE     = 'D',
	CMD_GET_MODE     = 'E',
	CMD_RUMBLE_CTRL  = 'M',
};

void pad_connect(struct sio_dev* dev, void* host_data, psx_padpollfn_t poll_host) {
	sio_dev_clear(dev);
	struct psx_pad* pad = (struct psx_pad*)dev;
	pad->host.data = host_data;
	pad->host.poll = poll_host;
	pad->dev = (struct sio_dev) {
		.id = PSX_SIO_DEV_CONTROLLER,
		.send = pad_send, .recv = pad_recv,
		.reset = pad_reset, .tx_finished = pad_tx_finished
	};
}

void pad_reset(struct sio_dev* dev) {
	struct psx_pad* pad = (struct psx_pad*)dev;
	pad->session_active = false;
	memset(&pad->resp, 0, sizeof(pad->resp));
}

bool pad_send(struct sio_dev* dev, uint8_t byte) {
	struct psx_pad* pad = (struct psx_pad*)dev;
	if(!pad->session_active) {
		// prepare our throwaway HiZ response, pull ACK high
		pad->resp.buf[0] = 0xff;
		pad->resp.off = 0;
		pad->resp.nbytes = 1;
		pad->session_active = true;
		pad->processing_command = false;
		goto ack;
	}

	if(pad->processing_command) {
		goto ack;
	}
	switch(byte) {
	case CMD_READ_BUTTONS:
		pad->processing_command = true;
		uint16_t buttons = pad->host.poll(pad->host.data);
		// ignore analog mode for now
		pad->resp.off = 0;
		if(pad->config_mode) {
			uint8_t resp[] = { DUALSHOCK_ID(pad), 0x5a, buttons & 0xff, buttons >> 8, 0x80, 0x80, 0x80, 0x80 };
			memcpy(pad->resp.buf, resp, sizeof(resp));
			pad->resp.nbytes = 8;
		} else {
			uint8_t resp[] = { DUALSHOCK_ID(pad), 0x5a, buttons & 0xff, buttons >> 8 };
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
ack:
	// pull ACK high
	return true;
}

uint8_t pad_recv(struct sio_dev* dev) {
	struct psx_pad* pad = (struct psx_pad*)dev;
	if(pad->resp.off == pad->resp.nbytes) {
		log_error("PAD: read with no data");
		pad->session_active = false;
		return 0xff;
	}

	log_trace("PAD: sent back 0x%02x", pad->resp.buf[pad->resp.off]);
	return pad->resp.buf[pad->resp.off++];
}

bool pad_tx_finished(struct sio_dev* dev) {
	struct psx_pad* pad = (struct psx_pad*)dev;
	return (pad->resp.off == (pad->resp.nbytes - 1));
}

