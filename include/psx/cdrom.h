#ifndef PSX_CDROM_H
#define PSX_CDROM_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdint.h>
#include <stdbool.h>

#define PSX_CDROM_DATABUF_SIZE 2352
#define PSX_CDROM_RESPBUF_SIZE 16
#define PSX_CDROM_PARMBUF_SIZE 16

struct psx_cdrom {
	struct {
		uint8_t ctrl;
		uint16_t rd_data;
		uint8_t wr_data;
		uint8_t irq_mask;
		uint8_t irq_status;
		uint8_t command;
		uint8_t hchp_ctrl;
		// missing CD-XA audio stuff
	} regs;

	uint8_t state;
	uint8_t disc_mode;
	struct queue* data_queue;
	struct queue* resp_queue;
	struct queue* param_queue;
	uint32_t loc;
	struct {
		uint32_t loc;
		bool is_pending;
	} seek;
	bool muted;
	bool report_absolute;

	struct psx_disc* disc;
	struct psx_system* sys;
};

void psx_cdr_init(struct psx_cdrom* cdr);
void psx_cdr_reset(struct psx_cdrom* cdr);

uint16_t psx_cdr_read16(struct psx_region* reg, uint32_t addr);
uint8_t psx_cdr_read8(struct psx_region* reg, uint32_t addr);
void psx_cdr_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif
