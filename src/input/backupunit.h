#ifndef PSX_BACKUPUNIT_H
#include <psx/system.h>

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

struct psx_bu {
	struct sio_dev dev;
	bool session_active;
	bool processing_command;
	uint8_t current_command;
	uint8_t command_state;
	uint8_t response;
	uint8_t flag;
	uint16_t address;
	struct {
		uint8_t data[128];
		uint8_t checksum;
		uint32_t off;
	} sector;
	struct {
		void* data;
		psx_buwritefn_t write_sector;
		psx_bureadfn_t read_sector;
	} host;
};

struct sio_dev* bu_connect(int n, void* host_data, psx_buwritefn_t write_fn, psx_bureadfn_t read_fn);
void bu_reset(struct sio_dev* bu);
bool bu_send(struct sio_dev* bu, uint8_t byte);
uint8_t bu_recv(struct sio_dev* bu);
bool bu_tx_finished(struct sio_dev* bu);

#endif
