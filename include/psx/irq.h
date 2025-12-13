#ifndef PSX_IRQ_H
#define PSX_IRQ_H

#include <psx/system.h>
#include <psx/memory.h>

#include <stdint.h>
#include <stdbool.h>

enum psx_irq_id {
	PSX_IRQ_ID_VBLANK = 0x001,
	PSX_IRQ_ID_GPU    = 0x002,
	PSX_IRQ_ID_CDROM  = 0x004,
	PSX_IRQ_ID_DMA    = 0x008,
	PSX_IRQ_ID_TMR0   = 0x010,
	PSX_IRQ_ID_TMR1   = 0x020,
	PSX_IRQ_ID_TMR2   = 0x040,
	PSX_IRQ_ID_BRI    = 0x080,
	PSX_IRQ_ID_SIO    = 0x100,
	PSX_IRQ_ID_SPU    = 0x200,
	PSX_IRQ_ID_PIO    = 0x400
};

typedef struct psx_irq_status {
	bool vblank: 1;
	bool irq: 1;
	bool cdrom: 1;
	bool dma: 1;
	bool tmr0: 1;
	bool tmr1: 1;
	bool tmr2: 1;
	bool bri: 1;
	bool sio: 1;
	bool spu: 1;
	bool pio: 1;
	uint32_t unused: 21;
} psx_irq_status_t;

struct psx_irq {
	psx_irq_status_t stat;
	psx_irq_status_t mask;

	struct psx_system* sys;
};

void psx_irq_init(struct psx_irq* irq);
void psx_irq_reset(struct psx_irq* irq);
void psx_irq_raise(struct psx_irq* irq, enum psx_irq_id id);

uint32_t psx_irq_read32(struct psx_region* reg, uint32_t addr);
void psx_irq_write32(struct psx_region* reg, uint32_t addr, uint32_t val);
uint16_t psx_irq_read16(struct psx_region* reg, uint32_t addr);
void psx_irq_write16(struct psx_region* reg, uint32_t addr, uint16_t val);
uint8_t psx_irq_read8(struct psx_region* reg, uint32_t addr);
void psx_irq_write8(struct psx_region* reg, uint32_t addr, uint8_t val);

#endif // #ifndef PSX_IRQ_H
