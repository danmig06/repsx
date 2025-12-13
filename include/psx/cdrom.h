#ifndef PSX_CDROM_H
#define PSX_CDROM_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdint.h>
#include <stdbool.h>

#define PSX_CDROM_DATABUF_SIZE 2352
#define PSX_CDROM_RESPBUF_SIZE 16
#define PSX_CDROM_PARMBUF_SIZE 16

typedef struct {
	uint8_t flags: 3;
	uint8_t buf_empty: 1;
	uint8_t buf_write_ready: 1;
	uint8_t reserved: 3;
} psx_cdr_intsts_t;

typedef struct {
	bool error: 1;
	bool motor_on: 1;
	bool seek_error: 1;
	bool id_error: 1;
	bool shell_open: 1;
	bool reading: 1;
	bool seeking: 1;
	bool playing: 1;
} psx_cdr_stat_t;

struct psx_cdrom {
	struct {
		struct {
			uint8_t address: 2;
			bool xa_busy: 1;
			bool param_empty: 1;
			bool param_wr_ready: 1;
			bool result_ready: 1;
			bool data_request: 1;
			bool busy: 1;
		} ctrl;
		uint16_t rd_data;
		uint8_t wr_data;
		psx_cdr_intsts_t irq_mask;
		psx_cdr_intsts_t irq_status;
		
		uint8_t command;
		struct {
			uint8_t reserved: 5;
			bool sm_enable: 1;
			bool buf_wr_req: 1;
			bool buf_rd_req: 1;
		} hphc_ctrl;
		// missing CD-XA audio stuff
	} regs;

	psx_cdr_stat_t state;
	psx_disc_mode_t disc_mode;
	struct queue* data_queue;
	struct queue* resp_queue;
	struct queue* param_queue;
	uint32_t loc;
	struct {
		uint32_t loc: 31;
		bool is_pending: 1;
	} seek;
	bool muted;

	struct psx_disc* disc;
	struct psx_system* sys;
};

void psx_cdr_init(struct psx_cdrom* cdr);
void psx_cdr_reset(struct psx_cdrom* cdr);
void psx_cdr_update(struct psx_cdrom* cdr, float clocks);

uint16_t psx_cdr_read16(struct psx_region* reg, uint32_t addr);
uint8_t psx_cdr_read8(struct psx_region* reg, uint32_t addr);
void psx_cdr_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif
