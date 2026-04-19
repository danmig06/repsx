#ifndef PSX_DISC_H
#define PSX_DISC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PSX_DISC_SECTOR_SIZE 2352

#ifdef __cplusplus
extern "C" {
#endif

enum {
	PSX_DT_INVALID,
	PSX_DT_LICENSED,
	PSX_DT_AUDIO
};

typedef struct {
	uint8_t m, s, f;
} psx_disc_msf_t;

typedef uint32_t psx_lba_t;

typedef struct psx_disc_info {
	uint8_t number;
	psx_lba_t start;
	psx_lba_t end;
} psx_disc_track_info_t;

typedef bool (*psx_disc_readfn_t)(void* host_data, psx_lba_t loc, void* sector_buf);
typedef void (*psx_disc_infofn_t)(void* host_data, psx_lba_t loc, psx_disc_track_info_t* info);
struct psx_disc {
	uint8_t type;
	uint8_t n_tracks;

	void* host_data;
	psx_disc_readfn_t read_sector;
	psx_disc_infofn_t get_track_info;
};

void psx_disc_verify(struct psx_disc* disc);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_DISC_H
