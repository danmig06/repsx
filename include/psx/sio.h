#ifndef PSX_SIO_H
#define PSX_SIO_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	PSX_SIO_DEV_NONE       = 0x00,
	PSX_SIO_DEV_CONTROLLER = 0x01,
	PSX_SIO_DEV_MEMCARD    = 0x81
};

struct psx_sio {
	struct {
		uint32_t rx_data;
		uint32_t stat;
		uint16_t mode;
		uint16_t ctrl;
		uint16_t padding;
		uint16_t baudrate_counter;
	} regs;
	bool irq_scheduled;
	struct sio_dev* selected_dev;
	struct {
		struct sio_dev* in[2];
		struct sio_dev* bu[2];
	} dev;

	struct psx_system* sys;
};

void psx_sio_init(struct psx_sio* sio, struct psx_system* sys);
void psx_sio_reset(struct psx_sio* sio);

uint32_t psx_sio_read32(struct psx_region* reg, uint32_t addr);
void psx_sio_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_sio_read16(struct psx_region* reg, uint32_t addr);
void psx_sio_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_sio_read8(struct psx_region* reg, uint32_t addr);
void psx_sio_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_SIO_H
