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
		uint8_t atv0, atv1, atv2, atv3;
		uint8_t adpctl;
	} regs;

	uint8_t vol_ll, vol_lr, vol_rr, vol_rl;
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
	struct {
		uint8_t file;
		uint8_t channel;
		uint8_t coding_info;
		struct {
			uint8_t index;
			uint8_t counter;
			int16_t buf[32];
		} resample[2];
		int16_t hist[2][2];
	} xa;
	struct {
		uint32_t read_off;
		uint32_t write_off;
		int16_t* buf;
	} out[2];

	bool muted;
	bool report_absolute;

	struct psx_disc* disc;
	struct psx_system* sys;
};

typedef struct {
	int16_t l, r;
} psx_cdr_sample_t;

void psx_cdr_init(struct psx_cdrom* cdr);
void psx_cdr_reset(struct psx_cdrom* cdr);
psx_cdr_sample_t psx_cdr_pop_sample(struct psx_cdrom* cdr);

uint16_t psx_cdr_read16(struct psx_region* reg, uint32_t addr);
uint8_t psx_cdr_read8(struct psx_region* reg, uint32_t addr);
void psx_cdr_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif
