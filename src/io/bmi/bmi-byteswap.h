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
 * functions, so we will use these and hope the optimizer is good. */

#else

static inline uint16_t htobmi16(uint16_t x)
{
    uint16_t y = 0;
    unsigned char *buf;
    buf = (unsigned char *)&y;
    buf[1] = x>>8&0xff;
    buf[0] = x&0xff;
    return y;
}

static inline uint32_t htobmi32(uint32_t x)
{
    uint32_t y = 0;
    unsigned char *buf;
    buf = (unsigned char *)&y;
    buf[3] = x>>24&0xff;
    buf[2] = x>>16&0xff;
    buf[1] = x>>8&0xff;
    buf[0] = x&0xff;
    return y;
}

static inline uint64_t htobmi64(uint64_t x)
{
    uint64_t y = 0;
    unsigned char *buf;
    buf = (unsigned char *)&y;
    buf[7] = x>>56&0xff;
    buf[6] = x>>48&0xff;
    buf[5] = x>>40&0xff;
    buf[4] = x>>32&0xff;
    buf[3] = x>>24&0xff;
    buf[2] = x>>16&0xff;
    buf[1] = x>>8&0xff;
    buf[0] = x&0xff;
    return y;
}

static inline uint16_t bmitoh16(uint16_t x)
{
    uint16_t y = 0;
    unsigned char *buf;
    buf = (unsigned char *)&x;
    y = (buf[1]&0xff)<<8 | (buf[0]&0xff);
    return y;
}

static inline uint32_t bmitoh32(uint32_t x)
{
    uint32_t y = 0;
    unsigned char *buf;
    buf = (unsigned char *)&x;
    y = (uint32_t)(buf[3]&0xff)<<24 | (uint32_t)(buf[2]&0xff)<<16 |
            (uint32_t)(buf[1]&0xff)<<8 | (buf[0]&0xff);
    return y;
}

static inline uint64_t bmitoh64(uint64_t x)
{
    uint64_t y = 0;
    unsigned char *buf;
    buf = (unsigned char *)&x;
    y = (uint64_t)(buf[7]&0xff)<<56 | (uint64_t)(buf[6]&0xff)<<48 |
            (uint64_t)(buf[5]&0xff)<<40 | (uint64_t)(buf[4]&0xff)<<32 |
            (uint64_t)(buf[3]&0xff)<<24 | (uint64_t)(buf[2]&0xff)<<16 |
            (uint64_t)(buf[1]&0xff)<<8 | (buf[0]&0xff);
    return y;
}

#endif

/* Little endians systems do not need byte swapping. */

#else

#define htobmi16(x) (x)
#define htobmi32(x) (x)
#define htobmi64(x) (x)
#define bmitoh16(x) (x)
#define bmitoh32(x) (x)
#define bmitoh64(x) (x)

#endif
