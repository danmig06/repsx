#ifndef PSX_CDROM_CMD_H
#define PSX_CDROM_CMD_H

#include <psx/cdrom.h>

#define CDROM_CMD_AVG_DELAY 0xc4e1
#define CDROM_CMD_AVG_SDELAY 0x5cf4
#define CDROM_CMD_INIT_DELAY 0x13cce
#define CDROM_CMD_PAUSE_NOPDELAY 0x1df2
#define CDROM_CMD_PAUSE_DELAY 0x21181c
#define CDROM_CMD_PAUSE_DSDELAY 0x10bd93
#define CDROM_CMD_STOP_NOPDELAY 0x1d7b
#define CDROM_CMD_STOP_DELAY 0xd38aca
#define CDROM_CMD_STOP_DSDELAY 0x18a6076

enum {
	CDROM_ERR_SEEK_FAILED      = 0x04,
	CDROM_ERR_SHELL_OPENED     = 0x08,
	CDROM_ERR_INVALID_ARGUMENT = 0x10,
	CDROM_ERR_PARAMETERS       = 0x20,
	CDROM_ERR_INVALID_COMMAND  = 0x40,
	CDROM_ERR_RESP_NOT_READY   = 0x80
};

enum {
	CMD_NOP       = 0x01,
	CMD_SETLOC    = 0x02,
	CMD_PLAY      = 0x03,
	CMD_READN     = 0x06,
	CMD_MOTORON   = 0x07,
	CMD_STOP      = 0x08,
	CMD_PAUSE     = 0x09,
	CMD_INIT      = 0x0a,
	CMD_MUTE      = 0x0b,
	CMD_DEMUTE    = 0x0c,
	CMD_SETFILTER = 0x0d,
	CMD_SETMODE   = 0x0e,
	CMD_GETLOCL   = 0x10,
	CMD_GETLOCP   = 0x11,
	CMD_GETTN     = 0x13,
	CMD_GETTD     = 0x14,
	CMD_SEEKL     = 0x15,
	CMD_SEEKP     = 0x16,
	CMD_TEST      = 0x19,
	CMD_GETID     = 0x1a,
	CMD_READS     = 0x1b,
	CMD_READTOC   = 0x1e,
};

void cdr_run_cmd(struct psx_cdrom* cdr);
void CdlNop(struct psx_cdrom* cdr);
void CdlSetloc(struct psx_cdrom* cdr);
void CdlPlay(struct psx_cdrom* cdr);
void CdlForward(struct psx_cdrom* cdr);
void CdlBackward(struct psx_cdrom* cdr);
void CdlRead(struct psx_cdrom* cdr);
void CdlStandby(struct psx_cdrom* cdr);
void CdlStop(struct psx_cdrom* cdr);
void CdlMotorOn(struct psx_cdrom* cdr);
void CdlPause(struct psx_cdrom* cdr);
void CdlInit(struct psx_cdrom* cdr);
void CdlMute(struct psx_cdrom* cdr);
void CdlDemute(struct psx_cdrom* cdr);
void CdlSetfilter(struct psx_cdrom* cdr);
void CdlSetmode(struct psx_cdrom* cdr);
void CdlGetparam(struct psx_cdrom* cdr);
void CdlGetlocL(struct psx_cdrom* cdr);
void CdlGetlocP(struct psx_cdrom* cdr);
void CdlSetsession(struct psx_cdrom* cdr);
void CdlGetTN(struct psx_cdrom* cdr);
void CdlGetTD(struct psx_cdrom* cdr);
void CdlSeek(struct psx_cdrom* cdr);
void CdlTest(struct psx_cdrom* cdr);
void CdlGetID(struct psx_cdrom* cdr);
void CdlReset(struct psx_cdrom* cdr);
void CdlGetQ(struct psx_cdrom* cdr);
void CdlReadTOC(struct psx_cdrom* cdr);
void CdlVideoCD(struct psx_cdrom* cdr);

#endif // #ifndef PSX_CDROM_CMD_H 
