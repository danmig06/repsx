#include <psx/disc.h>

#include <string.h>

#define PS_STR_LOC 166
#define PS_STR_OFFSET 0x20
#define PS_STR_SIZE (sizeof("PLAYSTATION") - 1)

void psx_disc_verify(struct psx_disc* disc) {
	uint8_t sector_buf[PSX_DISC_SECTOR_SIZE];
	if(!disc->read_sector(disc->host_data, PS_STR_LOC, sector_buf)) {
		disc->type = PSX_DT_INVALID;
		return;
	}

	if(memcmp(&sector_buf[PS_STR_OFFSET], "PLAYSTATION", PS_STR_SIZE)) {
		disc->type = PSX_DT_AUDIO;
		return;
	}

	disc->type = PSX_DT_LICENSED;
}

