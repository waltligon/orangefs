/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __WIRE_HARNESS_H
#define __WIRE_HARNESS_H

#include <inttypes.h>

#include <pvfs2-types.h>
#include <pvfs2-req-proto.h>

#define WIRE_HARNESS_READ 1
#define WIRE_HARNESS_WRITE 2

/* request structure */
struct wire_harness_req
{
	PVFS_fs_id fs_id;    /* file system or collection id */
	PVFS_handle handle;  /* data space handle */
	int op;              /* read or write */
	int32_t io_req_size; /* how big is the trailing info? */
	int32_t dist_size;
};
/* NOTE: the I/O description and the distribution will be packed
 * immediatedly after the above struct in all requests
 */


/* ack structure */
struct wire_harness_ack
{
    PVFS_handle handle; /* returned handle for writes to new files??? */
	int32_t error_code;     /* 0 or -error */
	PVFS_size dspace_size;  /* so both sides can handle EOF */
};


#endif /* __WIRE_HARNESS_H */
