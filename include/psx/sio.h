#ifndef PSX_SIO_H
#define PSX_SIO_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdbool.h>

enum psx_sio_dev {
	PSX_SIO_DEV_NONE       = 0x00,
	PSX_SIO_DEV_CONTROLLER = 0x01,
	PSX_SIO_DEV_MEMCARD    = 0x81
};

struct psx_sio {
	struct {
		uint32_t rx_data;
		struct {
			bool tx_not_full: 1;
			bool rx_not_empty: 1;
			bool tx_idle: 1;
			bool rx_pe: 1;
			bool rx1_overrun: 1;
			bool rx1_bsb: 1;
			bool rx1_input_level: 1;
			bool dsr_input_level: 1;
			bool cts1_input_level: 1;
			bool irq: 1;
			uint8_t unk: 1;
			uint32_t baudrate_timer: 21;
		} stat;
		struct {
			uint8_t baudrate_reload: 2;
			uint8_t char_len: 2;
			bool parity_enable: 1;
			bool parity_type: 1;
			uint8_t sio1_stop_len: 2;
			bool sio0_cpol: 1;
			uint8_t unused: 7;
		} mode;
		struct {
			bool tx_enable: 1;
			bool dtr_out_level: 1;
			bool rx_enable: 1;
			bool sio1_tx_out_level: 1;
			bool acknowledge: 1;
			bool sio1_rts_out_level: 1;
			bool reset: 1;
			uint8_t unk: 1;
			uint8_t rx_irq_mode: 2;
			bool tx_irq_enable: 1;
			bool rx_irq_enable: 1;
			bool dsr_irq_enable: 1;
			uint8_t sio0_port_select: 1;
			uint8_t unused: 2;
		} ctrl;
		uint16_t baudrate_counter;
	} regs;
	bool rx_high_z;
	bool irq_scheduled;
	enum psx_sio_dev tx_address;
	struct {
		struct psx_pad* pad[2];
		struct psx_bu* bu[2];
	} dev;

	struct psx_system* sys;
};

void psx_sio_init(struct psx_sio* sio);
void psx_sio_reset(struct psx_sio* sio);

uint32_t psx_sio_read32(struct psx_region* reg, uint32_t addr);
void psx_sio_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_sio_read16(struct psx_region* reg, uint32_t addr);
void psx_sio_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_sio_read8(struct psx_region* reg, uint32_t addr);
void psx_sio_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif // #ifndef PSX_SIO_H
