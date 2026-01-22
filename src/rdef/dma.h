#ifndef RDEF_DMA
#define RDEF_DMA

#include "../util.h"

// CHCR - CHannel Control Register
enum {
	CHCR_DIR  = BIT(0),
	CHCR_INC  = BIT(1),
	CHCR_CHOP = BIT(8),
	CHCR_MODE = BIT_RANGE(9, 2),
#define CHCR_MODE_GET(o) GET_BITS(o, CHCR_MODE, 9)
#define CHCR_MODE_SET(o, v) SET_BITS(o, v, CHCR_MODE, 9)
	
	CHCR_DMA_CW = BIT_RANGE(16, 3),
#define CHCR_DMA_CW_GET(o) GET_BITS(o, CHCR_DMA_CW, 16)
#define CHCR_DMA_CW_SET(o, v) SET_BITS(o, v, CHCR_DMA_CW, 16)
	
	CHCR_CPU_CW = BIT_RANGE(20, 3),
#define CHCR_CPU_CW_GET(o) GET_BITS(o, CHCR_CPU_CW, 20)
#define CHCR_CPU_CW_SET(o, v) SET_BITS(o, v, CHCR_CPU_CW, 20)

	CHCR_START = BIT(24),
	CHCR_FORCE = BIT(28),
	CHCR_PAUSE = BIT(29),
	CHCR_SNOOP = BIT(30)
};

// DPCR - DMA Primary Control Register
#define DPCR_EN_GET(o, ch) (((o) >> (((ch) * 4) + 3)) & 1)
#define DPCR_PR_GET(o, ch) (((o) >> ((ch) * 4)) & 7)

// DICR - DMA Interrupt Control Register
enum {
	DICR_CHNCTRL = BIT_RANGE(0, 7),
#define DICR_CHNCTRL_GET(o) GET_BITS(o, DICR_CHNCTRL, 0)
#define DICR_CHNCTRL_SET(o, v) SET_BITS(o, v, DICR_CHNCTRL, 0)
	
	DICR_BUSERROR = BIT(15),
	DICR_CHNMASK = BIT_RANGE(16, 7),
#define DICR_CHNMASK_GET(o) GET_BITS(o, DICR_CHNMASK, 16)
#define DICR_CHNMASK_SET(o, v) SET_BITS(o, v, DICR_CHNMASK, 16)
	
	DICR_IRQ_EN = BIT(23),
	DICR_CHNFLAGS = BIT_RANGE(24, 7),
#define DICR_CHNFLAGS_GET(o) GET_BITS(o, DICR_CHNFLAGS, 24)
#define DICR_CHNFLAGS_SET(o, v) SET_BITS(o, v, DICR_CHNFLAGS, 24)

	DICR_IRQ = BIT(31)
};

#endif
