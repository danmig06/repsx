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
	struct __attribute__((packed)) {
		uint32_t rx_data;
		uint32_t stat;
		uint16_t mode;
		uint32_t ctrl;
		uint16_t baudrate_counter;
	} regs;
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
