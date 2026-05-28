#ifndef PSX_DISC_H
#define PSX_DISC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PSX_DISC_SECTOR_SIZE 2352

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PSX_DT_INVALID,
	PSX_DT_LICENSED,
	PSX_DT_AUDIO
} psx_disc_type_t;

typedef uint32_t psx_lba_t;

typedef struct psx_disc_track_info {
	uint8_t number;
	bool is_audio;
	uint32_t pregap_len;
	psx_lba_t abs_start;
	psx_lba_t abs_end;
} psx_disc_track_t;

typedef bool (*psx_disc_readfn_t)(void* host_data, const psx_disc_track_t* track, psx_lba_t loc, void* sector_buf);

struct psx_disc {
	uint8_t n_tracks;
	uint8_t allocated_tracks;
	psx_disc_track_t* tracks;

	void* host_data;
	psx_disc_readfn_t read_sector;
};

psx_disc_type_t psx_disc_verify(struct psx_disc* disc);
void psx_disc_reserve_tracks(struct psx_disc* disc, uint8_t n_tracks);
void psx_disc_add_track(struct psx_disc* disc, size_t pregap_sectors, psx_lba_t file_offset, size_t sectors, bool is_audio);
void psx_disc_free_tracks(struct psx_disc* disc);

#ifdef __cplusplus
};
#endif

#endif // #ifndef PSX_DISC_H
