#ifndef PSX_PAD_H
#define PSX_PAD_H

#include <psx/system.h>

#include <stdbool.h>
#include <stdint.h>

struct psx_pad {
	struct {
		uint8_t buf[8];
		uint8_t nbytes;
		uint8_t off;
	} resp;
	bool config_mode;
	bool analog_mode;
	bool session_active;
	struct {
		void* data;
		psx_padpollfn_t poll;
	} host;

	bool (*send)(struct psx_pad*, uint8_t);
	uint8_t (*recv)(struct psx_pad*);
	void (*reset)(struct psx_pad*);
	bool (*tx_finished)(struct psx_pad*);
};

struct psx_pad* pad_connect(int n, void* host_data, psx_padpollfn_t poll_host);
void pad_reset(struct psx_pad* pad);
bool pad_send(struct psx_pad* pad, uint8_t byte);
uint8_t pad_recv(struct psx_pad* pad);
bool pad_tx_finished(struct psx_pad* pad);

#endif // #ifndef PSX_PAD_H
