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
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ABS(n) (((n) >= 0) ? (n) : -(n))
#define SAT(v, a, b) (((v) < (a)) ? (a) : (((v) > (b)) ? (b) : (v)))

#define BIT(n) (1 << (n))
#define BIT_RANGE(s, n) ((BIT(n) - 1) << (s))
#define SET_BITS(o, v, msk, s) o = (o & ~(msk)) | (((v) << (s)) & (msk))
#define GET_BITS(o, msk, s) (((o) & (msk)) >> (s))

#endif // #ifndef PSX_UTIL_H

