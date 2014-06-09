/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#ifndef BMI_BYTESWAP_H
#define BMI_BYTESWAP_H

#include <stdint.h>
#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif

/* Byte swapping is only necessary on big endian systems. */

#ifdef WORDS_BIGENDIAN

/* On Linux glibc, byteswap.h will exist and contain fast machine-dependent
 * byteswapping functions. */

#ifdef HAVE_BYTESWAP_H

#define htobmi16 bswap_16(x)
#define htobmi32 bswap_32(x)
#define htobmi64 bswap_64(x)
#define bmitoh16 bswap_16(x)
#define bmitoh32 bswap_32(x)
#define bittoh64 bswap_64(x)

/* Otherwise, we do not know about any fast machine-dependent byteswapping
 * functions, so we will use these and hope the optimizer is good. These
 * are functions so that the argument only gets evaluated once.
 */

#else /* HAVE_BYTESWAP_H */

static inline uint16_t PVFS_bswap16(uint16_t x)
{
    return (((x)&0xff)<<8 | ((x)&0xff00)>>8);
}

static inline uint32_t PVFS_bswap32(uint32_t x)
{
    x = ((x&0xff00ff00u)>>8 | (x&0xff00ffu)<<8);
    return (x&0xffff0000u)>>16 | (x&0xffff)<<16;
}

static inline uint64_t PVFS_bswap64(uint64_t x)
{
    x = ((x&0xff00ff00ff00ff00ull)>>8 | (x&0xff00ff00ff00ffull)<<8);
    x = ((x&0xffff0000ffff0000ull)>>16 | (x&0xffff0000ffffull)<<16);
    return ((x&0xffffffff00000000ull)>>32 | (x&0xffffffffu)<<32);
}

#define htobmi16 PVFS_bswap16(x)
#define htobmi32 PVFS_bswap32(x)
#define htobmi64 PVFS_bswap64(x)
#define bmitoh16 PVFS_bswap16(x)
#define bmitoh32 PVFS_bswap32(x)
#define bittoh64 PVFS_bswap64(x)

#endif

/* Little endians systems do not need byte swapping. */

#else /* WORDS_BIGENDIAN */

#define htobmi16(x) (x)
#define htobmi32(x) (x)
#define htobmi64(x) (x)
#define bmitoh16(x) (x)
#define bmitoh32(x) (x)
#define bmitoh64(x) (x)

#endif /* WORDS_BIGENDIAN */

#endif /* BMI_BYTESWAP_H */
