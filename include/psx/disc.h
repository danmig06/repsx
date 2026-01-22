#ifndef PSX_DISC_H
#define PSX_DISC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define PSX_DISC_SECTOR_SIZE 2352

enum {
	PSX_DT_INVALID,
	PSX_DT_LICENSED,
	PSX_DT_AUDIO
};

typedef struct {
	uint8_t m, s, f;
} psx_disc_msf_t;

typedef struct psx_disc_info {
	uint8_t n_tracks;
	struct { 
		uint8_t number;
		psx_disc_msf_t start;
		psx_disc_msf_t end;
	} current_track;
} psx_disc_info_t;

typedef bool (*psx_disc_readfn_t)(void* host_data, uint32_t loc, void* sector_buf);
typedef void (*psx_disc_infofn_t)(void* host_data, uint32_t loc, psx_disc_info_t* info);
struct psx_disc {
	uint8_t type;

	void* host_data;
	psx_disc_readfn_t read_sector;
	psx_disc_infofn_t get_info;
};

void psx_disc_verify(struct psx_disc* disc);

#endif // #ifndef PSX_DISC_H
