#ifndef RDEF_SIO
#define RDEF_SIO

#include "../util.h"

// SIO#_STAT
enum {
	STAT_TXREADY = BIT(0),
	STAT_RXREADY = BIT(1),
	STAT_TXIDLE  = BIT(2),
	STAT_RX_PE   = BIT(3),
	STAT_RX_BSB  = BIT(5),
	STAT_DSR     = BIT(7),
	STAT_IRQ     = BIT(9)
};

// SIO#_MODE ignored

// SIO#_CTRL
enum {
	CTRL_TXEN      = BIT(0),
	CTRL_DTR       = BIT(1),
	CTRL_RXEN      = BIT(2),
	CTRL_ACK       = BIT(4),
	CTRL_RESET     = BIT(6),
	CTRL_SIO0_PORT = BIT(13)
};

#endif
