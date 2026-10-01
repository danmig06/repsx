#ifndef PSX_PAD_H
#define PSX_PAD_H

#include <psx/system.h>

#include <stdbool.h>
#include <stdint.h>

#include "common.h"

typedef struct psx_pad {
	struct sio_dev dev;
	struct {
		uint8_t buf[8];
		uint8_t nbytes;
		uint8_t off;
	} resp;
	bool config_mode;
	bool analog_mode;
	bool session_active;
	bool processing_command;
	struct {
		void* data;
		psx_padpollfn_t poll;
	} host;
} psx_pad;

ASSERT_EXTRA_SIZE(psx_pad);

void pad_connect(struct sio_dev* dev, void* host_data, psx_padpollfn_t poll_host);
void pad_reset(struct sio_dev* dev);
bool pad_send(struct sio_dev* dev, uint8_t byte);
uint8_t pad_recv(struct sio_dev* dev);
bool pad_tx_finished(struct sio_dev* dev);

#endif // #ifndef PSX_PAD_H
