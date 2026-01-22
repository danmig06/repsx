#ifndef RDEF_CDROM
#define RDEF_CDROM

#include "../util.h"

// ADDRESS - Bank Address/Control register
enum {
	CTRL_BANK         = BIT_RANGE(0, 2),
#define CTRL_BANK_GET(o) GET_BITS(o, CTRL_BANK, 0)
#define CTRL_BANK_SET(o, v) SET_BITS(o, v, CTRL_BANK, 0)

	CTRL_ADPBUSY      = BIT(2), // ADPCM Busy
	CTRL_PARAM_EMPTY  = BIT(3),
	CTRL_PARAM_READY  = BIT(4),
	CTRL_RESULT_READY = BIT(5),
	CTRL_DATA_REQUEST = BIT(6), // Data Request (Sector Buffer Ready)
	CTRL_BUSY         = BIT(7)  // Command Busy
};

// HCHPCTL - Sound Map & Sector Request
enum {
	HCHP_SMEN = BIT(5), // Sound Map ENable
	HCHP_BFWR = BIT(6), // BuFfer Write Request
	HCHP_BFRD = BIT(7)  // BuFfer ReaD request
};

// HINTSTS/MSK - Interrupt Status/Mask
enum {
	INT_FLAGS = BIT_RANGE(0, 3), // Interrupt Flag (INT1~INT5)
#define INT_FLAGS_GET(o) GET_BITS(o, INT_FLAGS, 0)
#define INT_FLAGS_SET(o, v) SET_BITS(o, v, INT_FLAGS, 0)

	INT_BFEMPT = BIT(3), // Buffer Empty - Unused
	INT_BFWRDY = BIT(4)  // Buffer Write Ready - Unused
};

// SM - XA-ADPCM Submode
enum {
	XA_SM_EOR      = BIT(0), // End of Record
	XA_SM_VIDEO    = BIT(1), // somehow STR files are declared as DATA, not VIDEO
	XA_SM_AUDIO    = BIT(2),
	XA_SM_DATA     = BIT(3),
	XA_SM_TRIGGER  = BIT(4), // Application-specific
	XA_SM_FORM2    = BIT(5),
	XA_SM_REALTIME = BIT(6),
	XA_SM_EOF      = BIT(7)  // End of XA file
};

// CI - XA-ADPCM Coding Info
enum {
	XA_CI_SM       = BIT(0), // Stereo/Mono     (0=mono, 1=stereo)
	XA_CI_FS       = BIT(2), // Sample rate     (0=37800Hz, 1=18900Hz)
	XA_CI_8BITS    = BIT(4), // Bits per sample (0=4bit, 1=8bit)
	XA_CI_EMPHASIS = BIT(6)  // Emphasis Filter Enable
};

// Stat Byte Flags
enum {
	STAT_ERROR      = BIT(0),
	STAT_MOTOR_ON   = BIT(1),
	STAT_SEEK_ERROR = BIT(2),
	STAT_ID_ERROR   = BIT(3),
	STAT_SHELL_OPEN = BIT(4),
	STAT_READING    = BIT(5),
	STAT_SEEKING    = BIT(6),
	STAT_PLAYING    = BIT(7)
};

// GetID flags
enum {
	IDFLAG_AUDIO      = BIT(4), // Disc is CDDA or Mode1
	IDFLAG_NO_DISC    = BIT(6), // Disc is missing
	IDFLAG_UNLICENSED = BIT(7)  // Disc is invalid (no SCEx string found)
};

// Setmode flags
enum {
	MODE_CDDA         = BIT(0),
	MODE_AUTOPAUSE    = BIT(1),
	MODE_REPORT       = BIT(2),
	MODE_XA_FILTER    = BIT(3),
	MODE_IGNORE       = BIT(4),
	MODE_SECTOR_SIZE  = BIT(5),
	MODE_XA           = BIT(6),
	MODE_DOUBLE_SPEED = BIT(7)
};

#endif
