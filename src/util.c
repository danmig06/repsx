#include "util.h"

bool psx_is_host_be(void) {
	union {
		uint32_t i;
		char c[4];
	} bint = {0x01020304};

	return bint.c[0] == 1;
}

uint16_t psx_be16(uint16_t h) {
	return (h & 0xff) | ((h & 0xff00) << 8);
}

uint32_t psx_be32(uint32_t w) {
	return ((w >> 24) & 0xff) | ((w << 8) & 0xff0000) | ((w >> 8) & 0xff00) | ((w << 24) & 0xff000000);
}

