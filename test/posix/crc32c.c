/* 
 * Cryptographic API.
 *
 * CRC32C chksum
 *
 * This module file is a wrapper to invoke the lib/crc32c routines.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 * 
 */
#include <stdio.h>
#include <errno.h>
#include "crc32c.h"

#define CHKSUM_BLOCK_SIZE	32
#define CHKSUM_DIGEST_SIZE	4
#define __cpu_to_le32(x)  (x)
#define __le32_to_cpu(x)  (x)

/*
 * Steps through buffer one byte at at time, calculates reflected 
 * crc using table.
 */

void chksum_init(void *ctx)
{
	struct chksum_ctx *mctx = ctx;

	mctx->crc = ~(unsigned int)0;			/* common usage */
}

void chksum_update(void *ctx, const unsigned char *data, unsigned int length)
{
	struct chksum_ctx *mctx = ctx;
	unsigned int mcrc;

	mcrc = crc32c(mctx->crc, data, (size_t)length);

	mctx->crc = mcrc;
}

void chksum_final(void *ctx, unsigned char *out)
{
	struct chksum_ctx *mctx = ctx;
	unsigned int mcrc = (mctx->crc ^ ~(unsigned int)0);
	
	*(unsigned int *)out = __le32_to_cpu(mcrc);
}

int32_t compute_check_sum(unsigned char *text, int len)
{
	struct chksum_ctx ctx;
	int32_t csum;
	chksum_init(&ctx);
	chksum_update(&ctx, text, len);
	chksum_final(&ctx, (unsigned char *) &csum);
	return csum;
}

