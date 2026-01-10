#include <psx/system.h>

#include "backupunit.h"
#include "util.h"

#include <string.h>

#define BU_SUCCESS 0x47
#define BU_BAD_CHK 0x4e
#define BU_BADSECT 0xff
#define BU_INDEX(bu, bu_array) (((bu) - (bu_array)) / sizeof(*(bu)))

static struct psx_bu backup_unit[2] = { 
	{ .send = bu_send, .recv = bu_recv, .reset = bu_reset, .tx_finished = bu_tx_finished },
	{ .send = bu_send, .recv = bu_recv, .reset = bu_reset, .tx_finished = bu_tx_finished },
};

enum {
	CMD_READ_DATA  = 'R',
	CMD_WRITE_DATA = 'W',
	CMD_GET_ID     = 'S'
};

enum {
	FLG_DIR_READ_OK   = 0x00,
	FLG_DIR_READ_FAIL = 0x04,
	FLG_DEFAULT       = 0x08
};

enum {
	BU_STATE_COMMAND = 0,
	BU_STATE_ID1,
	BU_STATE_ID2,
};

// states for read
enum {
	BU_STATE_R_ADDRHI = BU_STATE_ID2 + 1,
	BU_STATE_R_ADDRLO,
	BU_STATE_R_ACK1,
	BU_STATE_R_ACK2,
	BU_STATE_R_CONFHI,
	BU_STATE_R_CONFLO,
	BU_STATE_R_DATA,
	BU_STATE_R_CHK,
	BU_STATE_R_END
};

// states for write
enum {
	BU_STATE_W_ADDRHI = BU_STATE_ID2 + 1,
	BU_STATE_W_ADDRLO,
	BU_STATE_W_DATA,
	BU_STATE_W_CHK,
	BU_STATE_W_ACK1,
	BU_STATE_W_ACK2,
	BU_STATE_W_END
};

// states for get_id
enum {
	BU_STATE_S_ACK1 = BU_STATE_ID2 + 1,
	BU_STATE_S_ACK2,
	BU_STATE_S_ID0,
	BU_STATE_S_ID1,
	BU_STATE_S_ID2,
	BU_STATE_S_ID_END
};

static void update_write(struct psx_bu* bu, uint8_t val) {
	switch(bu->command_state) {
	case BU_STATE_W_ADDRHI:
		log_error("BU: write started");
		bu->address = val << 8;
		bu->response = val;
		break;
	case BU_STATE_W_ADDRLO:
		bu->address |= val & 0xff;
		bu->response = val;
		bu->sector.checksum = (bu->address >> 8) ^ (bu->address & 0xff);
		bu->address &= 0x3ff;
		break;
	case BU_STATE_W_DATA:
		bu->response = (bu->sector.off > 0) ? bu->sector.data[bu->sector.off - 1] : bu->address & 0xff;
		bu->sector.data[bu->sector.off++] = val;
		bu->sector.checksum ^= val;
		if(bu->sector.off == PSX_BU_SECTOR_SIZE) {
			bu->command_state++;
		}
		return;
	case BU_STATE_W_CHK:
		bu->response = bu->sector.data[PSX_BU_SECTOR_SIZE - 1];
		if(bu->sector.checksum != val) {
			log_warn("BU: mismatching checksums (have: 0x%02x, got: 0x%02x)");
		}
		break;
	case BU_STATE_W_ACK1:
		bu->response = 0x5c;
		break;
	case BU_STATE_W_ACK2:
		bu->response = 0x5d;
		break;
	case BU_STATE_W_END:
		log_error("BU: write complete (addr=0x%04x)", bu->address);
		if(bu->host.write_sector) {
			// could allow read-only memorycards in the API? maybe store the current file in memory?
			bu->host.write_sector(bu->host.data, bu->address, bu->sector.data);
		}
		bu->session_active = false;
		bu->response = BU_SUCCESS;
		bu->flag = FLG_DIR_READ_OK;
		break;
	default:
		log_error("BU: bad write byte 0x%02x received, state=%d", val, bu->command_state);
		bu->response = 0x5c;
		return;
	}
	bu->command_state++;
}

static void update_read(struct psx_bu* bu, uint8_t val) {
	switch(bu->command_state) {
	case BU_STATE_R_ADDRHI:
		log_error("BU: read started");
		bu->address = val << 8;
		break;
	case BU_STATE_R_ADDRLO:
		bu->address |= val & 0xff;
		break;
	case BU_STATE_R_ACK1:
		bu->response = 0x5c;
		break;
	case BU_STATE_R_ACK2:
		bu->response = 0x5d;
		break;
	case BU_STATE_R_CONFHI:
		bu->response = bu->address >> 8;
		break;
	case BU_STATE_R_CONFLO:
		bu->response = bu->address & 0xff;
		log_error("BU: reading sector %d", bu->address);
		bu->sector.checksum = (bu->address >> 8) ^ (bu->address & 0xff);
		if(bu->host.read_sector) {
			bu->host.read_sector(bu->host.data, bu->address, bu->sector.data);
		}
		bu->sector.off = 0;
		break;
	case BU_STATE_R_DATA:
		bu->response = bu->sector.data[bu->sector.off++];
		bu->sector.checksum ^= bu->response;
		if(bu->sector.off == PSX_BU_SECTOR_SIZE) {
			bu->command_state++;
		}
		return;
	case BU_STATE_R_CHK:
		bu->response = bu->sector.checksum;
		break;
	case BU_STATE_R_END:
		log_error("BU: read complete (addr=0x%04x)", bu->address);
		// reads always return success
		bu->session_active = false;
		bu->response = BU_SUCCESS;
		break;
	default:
		log_error("BU: bad read byte 0x%02x received, state=%d", val, bu->command_state);
		bu->response = 0x5c;
		return;
	}
	bu->command_state++;
}

static void update_getid(struct psx_bu* bu, uint8_t val) {
	switch(bu->command_state) {
	case BU_STATE_S_ACK1:
		bu->response = 0x5c;
		break;
	case BU_STATE_S_ACK2:
		bu->response = 0x5d;
		break;
	case BU_STATE_S_ID0:
		bu->response = 0x04;
		break;
	case BU_STATE_S_ID1:
		bu->response = 0x00;
		break;
	case BU_STATE_S_ID2:
		bu->response = 0x00;
		break;
	case BU_STATE_S_ID_END:
		bu->session_active = false;
		bu->response = 0x80;
		break;
	default:
		log_error("BU: bad getid byte 0x%02x received, state=%d", val, bu->command_state);
		return;
	}
	bu->command_state++;
}

static void process_send(struct psx_bu* bu, uint8_t val) {
	if(bu->command_state == BU_STATE_ID1) {
		bu->command_state++;
		bu->response = 0x5a;
		return;
	} else if(bu->command_state == BU_STATE_ID2) {
		bu->command_state++;
		bu->response = 0x5d;
		return;
	}

	bu->response = 0;
	switch(bu->current_command) {
	case CMD_WRITE_DATA:
		update_write(bu, val);
		break;
	case CMD_READ_DATA:
		update_read(bu, val);
		break;
	case CMD_GET_ID:
		update_getid(bu, val);
		break;
	default:
		break;
	}
}

struct psx_bu* bu_connect(int n, void* host_data, psx_buwritefn_t write_fn, psx_bureadfn_t read_fn) {
	n &= 1;
	backup_unit[n].host.data = host_data;
	backup_unit[n].host.write_sector = write_fn;
	backup_unit[n].host.read_sector = read_fn;
	memset(&backup_unit[n].sector, 0, sizeof(backup_unit[n].sector));
	backup_unit[n].flag = FLG_DEFAULT;
	return &backup_unit[n];
}

void bu_reset(struct psx_bu* bu) {
	if(bu->current_command != 0 && bu->command_state != 0) {
		log_warn("BU: reset");
	}
	bu->session_active = false;
	bu->processing_command = false;
	bu->current_command = 0;
	bu->command_state = 0;
	memset(&bu->sector, 0, sizeof(bu->sector));
}

bool bu_send(struct psx_bu* bu, uint8_t byte) {
	if(!bu->session_active) {
		// prepare our initial response 
		bu->response = 0xff;
		memset(&bu->sector, 0, sizeof(bu->sector));
		bu->session_active = true;
		bu->processing_command = false;
		bu->current_command = bu->command_state = 0;
		return true;
	} else if(bu->processing_command) {
		process_send(bu, byte);
		return true;
	}

	switch(byte) {
	case CMD_READ_DATA:
	case CMD_WRITE_DATA:
	case CMD_GET_ID:
		bu->processing_command = true;
		bu->current_command = byte;
		bu->command_state = BU_STATE_ID1;
		bu->response = bu->flag;
		break;
	default:
		log_error("BU: unknown command byte 0x%02x\n", byte);
		bu->session_active = false;
		break;
		// return false;
	}

	// pull ACK high
	return true;
}

uint8_t bu_recv(struct psx_bu* bu) {
	uint8_t resp = bu->response;
	bu->response = 0xff;
	return resp;
}

bool bu_tx_finished(struct psx_bu* bu) {
	switch(bu->current_command) {
	case CMD_READ_DATA:
		// log_error("read command %s (state=%d)", (bu->command_state == (BU_STATE_R_END + 1)) ? "finished" : "ongoing", bu->command_state);
		return bu->command_state == (BU_STATE_R_END + 1);
	case CMD_WRITE_DATA:
		// log_error("write command %s (state=%d)", (bu->command_state == (BU_STATE_W_END + 1)) ? "finished" : "ongoing", bu->command_state);
		return bu->command_state == (BU_STATE_W_END + 1);
	case CMD_GET_ID:
		return bu->command_state == (BU_STATE_S_ID_END + 1);	
	default:
		break;
	}
	return !bu->session_active;
}

