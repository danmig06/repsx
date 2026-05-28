#include <psx/disc.h>

#include <string.h>
#include <stdlib.h>

#define PS_STR_LOC 166
#define PS_STR_OFFSET 0x20
#define PS_STR_SIZE (sizeof("PLAYSTATION") - 1)

psx_disc_type_t psx_disc_verify(struct psx_disc* disc) {
	uint8_t sector_buf[PSX_DISC_SECTOR_SIZE];
	if(!disc->read_sector(disc->host_data, &disc->tracks[0], PS_STR_LOC, sector_buf)) {
		return PSX_DT_INVALID;
	}

	// PlayStation discs have a PLAYSTATION string in the system area
	if(memcmp(&sector_buf[PS_STR_OFFSET], "PLAYSTATION", PS_STR_SIZE) != 0) {
		return PSX_DT_AUDIO;
	}

	return PSX_DT_LICENSED;
}

void psx_disc_reserve_tracks(struct psx_disc* disc, uint8_t n_tracks) {
	disc->allocated_tracks = n_tracks;
	disc->tracks = calloc(sizeof(*disc->tracks), disc->allocated_tracks);
	disc->n_tracks = 0;
}

void psx_disc_add_track(struct psx_disc* disc, size_t pregap_sectors, psx_lba_t file_offset, size_t sectors, bool is_audio) {
	if(disc->n_tracks >= disc->allocated_tracks) {
		disc->allocated_tracks = disc->n_tracks + 1;
		disc->tracks = realloc(disc->tracks, disc->allocated_tracks * sizeof(*disc->tracks));
	}
	// if this is the first track, we use a dummy "Track 0" which sets up the initial pregap for track 1
	psx_disc_track_t prev = disc->n_tracks ? disc->tracks[disc->n_tracks - 1] : (psx_disc_track_t){ .abs_end = 150 };
	psx_disc_track_t* track = &disc->tracks[disc->n_tracks++];
	track->number = prev.number + 1;
	track->pregap_len = pregap_sectors;
	track->abs_start = prev.abs_end;
	track->abs_end = track->abs_start + (sectors - file_offset);
	track->is_audio = is_audio;
}

void psx_disc_free_tracks(struct psx_disc* disc) {
	if(disc->tracks) {
		free(disc->tracks);
		disc->allocated_tracks = 0;
		disc->n_tracks = 0;
	}
}

