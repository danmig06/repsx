#ifndef PSX_UTIL_H
#define PSX_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "log.h"

#define panic(...) \
	do { \
		log_fatal(__VA_ARGS__); \
		exit(1); \
	} while(0)

#define AS_UINT32(s) (*(uint32_t*)(&(s)))
#define AS_UINT16(s) (*(uint16_t*)(&(s)))
#define AS_UINT8(s) (*(uint8_t*)(&(s)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#define BIT(n) (1 << (n))
#define BIT_RANGE(s, n) ((BIT(n) - 1) << (s))
#define SET_BITS(o, v, msk, s) o = (o & ~(msk)) | (((v) << (s)) & (msk))
#define GET_BITS(o, msk, s) (((o) & (msk)) >> (s))

#define BE16(h) \
	do { \
		if(!psx_is_host_be()) { \
			psx_be16(h); \
		} \
	} while(0)

#define BE32(w) \
	do { \
		if(!psx_is_host_be()) { \
			psx_be32(w); \
		} \
	} while(0)

bool psx_is_host_be(void);
uint16_t psx_be16(uint16_t h);
uint32_t psx_be32(uint32_t w);

#endif // #ifndef PSX_UTIL_H
