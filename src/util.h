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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ABS(n) (((n) >= 0) ? (n) : -(n))
#define SAT(v, a, b) (((v) < (a)) ? (a) : (((v) > (b)) ? (b) : (v)))

#define BIT(n) (1 << (n))
#define BIT_RANGE(s, n) ((BIT(n) - 1) << (s))
#define SET_BITS(o, v, msk, s) o = (o & ~(msk)) | (((v) << (s)) & (msk))
#define GET_BITS(o, msk, s) (((o) & (msk)) >> (s))

#if defined(__GNUC__) || defined(__clang__)
#define LZC(n) __builtin_clz(n)
#elif defined(_MSC_VER)
#include <intrin.h>
#define LZC(n) __lzcnt(n)
#else

#define LZC(n) __fallback_clz(n)
static inline int __fallback_clz(uint32_t n) {
	int count = 0;
	while(!(n & 1)) {
		count++;
		n >>= 1;
	}
	return count;
}

#endif

#if defined(__GNUC__) || defined(__clang__)
#define UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define UNREACHABLE() __assume(0)
#else
#include <assert.h>
#define UNREACHABLE() assert(0 && "unreachable code")
#endif

#endif // #ifndef PSX_UTIL_H

