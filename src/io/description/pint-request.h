/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_REQUEST_H
#define __PINT_REQUEST_H

#include <pvfs2-types.h>
#include <pvfs-distribution.h>

/* modes for PINT_Process_request */
#define PINT_SERVER 1
#define PINT_CLIENT 2
#define PINT_CKSIZE 3

/* PVFS Request Processing Stuff */

typedef struct PINT_Request {
	PVFS_offset  offset;        /* offset from start of last set of elements */
	PVFS_count32 num_ereqs;     /* number of ereqs in a block */
	PVFS_count32 num_blocks;    /* number of blocks */
	PVFS_size    stride;        /* stride between blocks in bytes */
	PVFS_offset  ub;            /* upper bound of the type in bytes */
	PVFS_offset  lb;            /* lower bound of the type in bytes */
	PVFS_size    aggregate_size; /* amount of aggregate data in bytes */
	PVFS_count32 num_contig_chunks; /* number of contiguous data chunks */
	PVFS_count32 depth;    	    /* number of levels of nesting */
	PVFS_count32 num_nested_req;/* number of requests nested under this one */
	PVFS_count32 committed;     /* indicates if request has been commited */
	struct PINT_Request *ereq;  /* element type */
	struct PINT_Request *sreq;  /* sequence type */
} PINT_Request;

typedef struct PINT_reqstack {
	PVFS_count32 el;           /* number of element being processed */
	PVFS_count32 maxel;        /* total number of these elements to process */
	PINT_Request *rq;    		/* pointer to request structure */
	PINT_Request *rqbase; 		/* pointer to first request is sequence chain */
	PVFS_count32 blk;          /* number of block being processed */
	PVFS_offset  chunk_offset; /* offset of beginning of current contiguous chunk */
} PINT_reqstack;           
          
typedef struct PINT_Request_state { 
	struct PINT_reqstack *cur; /* request element chain stack */
	PVFS_count32 lvl;          /* level in element chain */
	PVFS_size    bytes;        /* bytes in current contiguous chunk processed */
	PVFS_offset  buf_offset;   /* byte offset in user buffer */
	PVFS_offset  last_offset;	/* last offset in previous call to process */
} PINT_Request_state;           

typedef struct PINT_Request_file_data {
	PVFS_size    fsize;			/* actual size of local storage object */
	PVFS_count32 iod_num;		/* ordinal number of THIS server for this file */
	PVFS_count32 iod_count;		/* number of servers for this file */
	PVFS_Dist    *dist;			/* dist struct for the file */
	PVFS_boolean extend_flag;	/* if zero, file will not be extended */
} PINT_Request_file_data;

struct PINT_Request_state *PINT_New_request_state (PINT_Request *request);

void PINT_Free_request_state (PINT_Request_state *req);

/* generate offset length pairs from request and dist */
int PINT_Process_request(PINT_Request_state *req,
		PINT_Request_file_data *rfdata, PVFS_count32 *segmax,
		PVFS_offset *offset_array, PVFS_size *size_array,
		PVFS_offset *start_offset, PVFS_size *bytemax,
		PVFS_boolean *eof_flag, PVFS_flag mode);

/* internal function */
PVFS_size PINT_Distribute(PVFS_offset offset, PVFS_size size,
		PINT_Request_file_data *rfdata, PVFS_size *bytes, PVFS_size bytemax,
		PVFS_count32 *segs, PVFS_count32 segmax, PVFS_offset *offset_array,
		PVFS_size *size_array, PVFS_boolean *eof_flag, PVFS_flag mode);

/* pack request from node into a contiguous buffer pointed to by region */
int PINT_Request_commit(PINT_Request *region, PINT_Request *node,
		      PVFS_count32 *index);

/* encode packed request in place for sending over wire */
int PINT_Request_encode(struct PINT_Request *req);

/* decode packed request in place after receiving from wire */
int PINT_Request_decode(struct PINT_Request *req);

/********* macros for accessing key fields in a request *********/

/* returns the number of bytes used by a contiguous packing of the
 * request struct pointed to by reqp
 */
#define PINT_REQUEST_PACK_SIZE(reqp)\
	(((reqp)->num_nested_req + 1) * sizeof(struct PINT_Request))

/* returns true if the request struct pointed to by reqp is a packed
 * struct
 */
#define PINT_REQUEST_IS_PACKED(reqp)\
	((reqp)->committed == 1)

/* returns the number of contiguous memory regions referenced by the
 * request struct pointed to by reqp
 */
#define PINT_REQUEST_NUM_CONTIG(reqp)\
	((reqp)->num_contig_chunks)

/* returns the total number of bytes referenced by the struct pointed to
 * by reqp - bytes might not be contiguous
 */
#define PINT_REQUEST_TOTAL_BYTES(reqp)\
	((reqp)->aggregate_size)

#endif /* __PINT_REQUEST_H */
