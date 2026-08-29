/* Shared types and constants for the native client.
 *
 * The client is built 32-bit so its struct layouts match the base engine it
 * links with; the fixed-width types here are chosen to stay stable under -m32. */
#ifndef MMO_H
#define MMO_H

#include <stddef.h>
#include <stdint.h>

/* A 64-bit integer is four-aligned here, and on ARM that has to be said. */
#if defined(__arm__)
#define MMO_ALIGN64 __attribute__((aligned(4)))
#else
#define MMO_ALIGN64
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t MMO_ALIGN64 u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  MMO_ALIGN64 s64;

#define MMO_CLIENT_NAME    "openmmo-client"
#define MMO_CLIENT_VERSION "0.1.0"

#endif /* MMO_H */
