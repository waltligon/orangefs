/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#ifndef RSA_SECURITY_H
#define RSA_SECURITY_H

#include <string.h>
#include "pvfs2-types.h"

/* The PVFS_sig struct should be a multiple of 64 bits - 8 bytes) */
#define PVFS_RSA_SIG_SIZE 128
#define PVFS_MSG_DIG_SIZE 96

typedef unsigned char PVFS_sig[PVFS_RSA_SIG_SIZE];

/*#define encode_PVFS_sig (pptr,pbuf) do {              \
	memcpy(*(pptr), *(pbuf), PVFS_RSA_SIG_SIZE);
         *(pptr) += PVFS_RSA_SIG_SIZE;
} while (0)

#define decode_PVFS_sig (pptr,pbuf) do { \
	memcpy(*(pptr), *(pbuf), PVFS_RSA_SIG_SIZE);
         *(pptr) += PVFS_RSA_SIG_SIZE;
} while (0)*/

#endif
	
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 * 	 
 * vim: ts=8 sts=4 sw=4 expandtab
 */
