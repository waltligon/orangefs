/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_REQUEST_H
#define __PINT_REQUEST_H

#include <pvfs2-types.h>
#include <pvfs-distribution.h>

/* modes for PINT_Process_request  and PINT_Distribute */
#define PINT_SERVER                000001
#define PINT_CLIENT                000002
#define PINT_CKSIZE                000004
#define PINT_MODIFY_OFFSET         000010
#define PINT_CKSIZE_MODIFY_OFFSET  000014
#define PINT_LOGICAL_SKIP          000020
#define PINT_CKSIZE_LOGICAL_SKIP   000024
#define PINT_SEEKING               000040
#define PINT_MEMREQ                000100

#define PINT_IS_SERVER(x)          ((x) & PINT_SERVER)
#define PINT_EQ_SERVER(x)          ((x) == PINT_SERVER)
#define PINT_IS_CLIENT(x)          ((x) & PINT_CLIENT)
#define PINT_EQ_CLIENT(x)          ((x) == PINT_CLIENT)
#define PINT_IS_CKSIZE(x)          ((x) & PINT_CKSIZE)
#define PINT_EQ_CKSIZE(x)          ((x) == PINT_CKSIZE)
#define PINT_IS_LOGICAL_SKIP(x)    ((x) & PINT_LOGICAL_SKIP)
#define PINT_EQ_LOGICAL_SKIP(x)    ((x) == PINT_LOGICAL_SKIP)
#define PINT_IS_SEEKING(x)         ((x) & PINT_SEEKING)
#define PINT_EQ_SEEKING(x)         ((x) == PINT_SEEKING)
#define PINT_IS_MEMREQ(x)          ((x) & PINT_MEMREQ)
#define PINT_EQ_MEMREQ(x)          ((x) == PINT_MEMREQ)
#define PINT_SET_SEEKING(x)        ((x) |= PINT_SEEKING)
#define PINT_CLR_SEEKING(x)        ((x) &= ~(PINT_SEEKING))
#define PINT_SET_LOGICAL_SKIP(x)   ((x) |= PINT_LOGICAL_SKIP)
#define PINT_CLR_LOGICAL_SKIP(x)   ((x) &= ~(PINT_LOGICAL_SKIP))

/* PVFS Request Processing Stuff */

typedef struct PINT_Request {
	PVFS_offset  offset;        /* offset from start of last set of elements */
	int32_t      num_ereqs;     /* number of ereqs in a block */
	int32_t      num_blocks;    /* number of blocks */
	PVFS_size    stride;        /* stride between blocks in bytes */
	PVFS_offset  ub;            /* upper bound of the type in bytes */
	PVFS_offset  lb;            /* lower bound of the type in bytes */
	PVFS_size    aggregate_size; /* amount of aggregate data in bytes */
	int32_t      num_contig_chunks; /* number of contiguous data chunks */
	int32_t      depth;    	    /* number of levels of nesting */
	int32_t      num_nested_req;/* number of requests nested under this one */
	int32_t      committed;     /* indicates if request has been commited */
	int32_t      refcount;      /* number of references to this request struct */
	struct PINT_Request *ereq;  /* element type */
	struct PINT_Request *sreq;  /* sequence type */
} PINT_Request;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
/* encode a linearized array of the above things, assumes space exists */
#define encode_PVFS_Request(pptr,rp) do { int i; \
    for (i=0; i<=(rp)->num_nested_req; i++) { \
	encode_PVFS_offset(pptr, &(rp+i)->offset); \
	encode_int32_t(pptr, &(rp+i)->num_ereqs); \
	encode_int32_t(pptr, &(rp+i)->num_blocks); \
	encode_PVFS_size(pptr, &(rp+i)->stride); \
	encode_PVFS_offset(pptr, &(rp+i)->ub); \
	encode_PVFS_offset(pptr, &(rp+i)->lb); \
	encode_PVFS_size(pptr, &(rp+i)->aggregate_size); \
	encode_int32_t(pptr, &(rp+i)->num_contig_chunks); \
	encode_int32_t(pptr, &(rp+i)->depth); \
	encode_int32_t(pptr, &(rp+i)->num_nested_req); \
	encode_int32_t(pptr, &(rp+i)->committed); \
	encode_int32_t(pptr, &(rp+i)->refcount); \
	/* XXX: this is not pretty, later will changes structures */ \
	encode_uint32_t(pptr, (u_int32_t*)&(rp+i)->ereq); \
	encode_uint32_t(pptr, (u_int32_t*)&(rp+i)->sreq); \
    } \
} while (0);
#define decode_PVFS_Request(pptr,rp) do { int i; \
    for (i=0; i == 0 || i<=(rp)->num_nested_req; i++) { \
	decode_PVFS_offset(pptr, &(rp+i)->offset); \
	decode_int32_t(pptr, &(rp+i)->num_ereqs); \
	decode_int32_t(pptr, &(rp+i)->num_blocks); \
	decode_PVFS_size(pptr, &(rp+i)->stride); \
	decode_PVFS_offset(pptr, &(rp+i)->ub); \
	decode_PVFS_offset(pptr, &(rp+i)->lb); \
	decode_PVFS_size(pptr, &(rp+i)->aggregate_size); \
	decode_int32_t(pptr, &(rp+i)->num_contig_chunks); \
	decode_int32_t(pptr, &(rp+i)->depth); \
	decode_int32_t(pptr, &(rp+i)->num_nested_req); \
	decode_int32_t(pptr, &(rp+i)->committed); \
	decode_int32_t(pptr, &(rp+i)->refcount); \
	decode_uint32_t(pptr, (u_int32_t*)&(rp+i)->ereq); \
	decode_uint32_t(pptr, (u_int32_t*)&(rp+i)->sreq); \
    } \
} while (0);
#endif

typedef struct PINT_reqstack {
	int32_t      el;           /* number of element being processed */
	int32_t      maxel;        /* total number of these elements to process */
	PINT_Request *rq;    		/* pointer to request structure */
	PINT_Request *rqbase; 		/* pointer to first request is sequence chain */
	int32_t      blk;          /* number of block being processed */
	PVFS_offset  chunk_offset; /* offset of beginning of current contiguous chunk */
} PINT_reqstack;           
          
typedef struct PINT_Request_state { 
	struct PINT_reqstack *cur; /* request element chain stack */
	int32_t      lvl;          /* level in element chain */
	PVFS_size    bytes;        /* bytes in current contiguous chunk processed */
	PVFS_offset  type_offset;  /* logical offset within request type */
	PVFS_offset  target_offset;/* first type offset to process */
	PVFS_offset  final_offset; /* last type offset to process */
#if 0
	PVFS_offset  file_offset;	/* last file offset in previous call to process*/
	PVFS_offset  start_offset;
#endif
	PVFS_boolean eof_flag;     /* is file at end of flile */
} PINT_Request_state;           
/* NOTE - I think buf_offset is superceded by type_offset
 * and start_offset can be completely replced with last_offset
 * at which point target_offset could be renamed start_offset
 * and last_offset could be renamed file_offset
 */

typedef struct PINT_Request_result {
	PVFS_offset  *offset_array;/* array of offsets for each segment output */
	PVFS_size    *size_array;  /* array of sizes for each segment output */
	int32_t      segmax;       /* maximum number of segments to output */
	int32_t      segs;         /* number of segments output */
	PVFS_size    bytemax;      /* maximum number of bytes to output */
	PVFS_size    bytes;        /* number of bytes output */
} PINT_Request_result;

typedef struct PINT_Request_file_data {
	PVFS_size    fsize;			/* actual size of local storage object */
	uint32_t     iod_num;		/* ordinal number of THIS server for this file */
	uint32_t     iod_count;		/* number of servers for this file */
	PVFS_Dist    *dist;			/* dist struct for the file */
	PVFS_boolean extend_flag;	/* if zero, file will not be extended */
} PINT_Request_file_data;

struct PINT_Request_state *PINT_New_request_state (PINT_Request *request);

void PINT_Free_request_state (PINT_Request_state *req);

/* generate offset length pairs from request and dist */
int PINT_Process_request(PINT_Request_state *req,
		PINT_Request_state *mem,
		PINT_Request_file_data *rfdata,
		PINT_Request_result *result,
		int mode);

/* internal function */
PVFS_size PINT_Distribute(PVFS_offset offset,
		PVFS_size size,
		PINT_Request_file_data *rfdata,
		PINT_Request_state *mem,
		PINT_Request_result *result,
		PVFS_boolean *eof_flag,
		int mode);

/* pack request from node into a contiguous buffer pointed to by region */
int PINT_Request_commit(PINT_Request *region, PINT_Request *node);
PINT_Request *PINT_Do_Request_commit(PINT_Request *region, PINT_Request *node,
		int32_t *index, int32_t depth);
int PINT_Do_clear_commit(PINT_Request *node, int32_t depth);

/* encode packed request in place for sending over wire */
int PINT_Request_encode(struct PINT_Request *req);

/* decode packed request in place after receiving from wire */
int PINT_Request_decode(struct PINT_Request *req);

void PINT_Dump_packed_request(struct PINT_Request *req);
void PINT_Dump_request(struct PINT_Request *req);

/********* macros for accessing key fields in a request *********/

#define PINT_REQUEST_NEST_SIZE(reqp)\
		((reqp)->num_nested_req)

/* returns the number of bytes used by a contiguous packing of the
 * request struct pointed to by reqp
 */
#define PINT_REQUEST_PACK_SIZE(reqp)\
	((PINT_REQUEST_NEST_SIZE(reqp) + 1) * sizeof(struct PINT_Request))

/* returns true if the request struct pointed to by reqp is a packed
 * struct
 */
#define PINT_REQUEST_IS_PACKED(reqp)\
	((reqp)->committed < 0)

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

#if 0
/* returns the start_offset */
#define PINT_REQUEST_STATE_OFFSET(reqp)\
 	((reqp)->start_offset)

/* sets the start_offset to the given value */
#define PINT_REQUEST_STATE_SET_OFFSET(reqp,val)\
 	((reqp)->start_offset) = (val)
#endif

/* sets the target_offset to the given value */
#define PINT_REQUEST_STATE_SET_TARGET(reqp,val)\
	((reqp)->target_offset) = (val)

/* sets the final_offset to the given value */
#define PINT_REQUEST_STATE_SET_FINAL(reqp,val)\
	((reqp)->final_offset) = (val)

/* this one does not zero the start_offset 
 * mainly used inside of process_request */
#define PINT_REQUEST_STATE_RST(reqp)\
	do {\
	((reqp)->lvl) = 0;\
	((reqp)->bytes) = 0;\
	((reqp)->type_offset) = 0;\
/*	((reqp)->file_offset) = 0;*/\
	((reqp)->eof_flag) = 0;\
	((reqp)->cur[0].el) = 0;\
	((reqp)->cur[0].rq) = ((reqp)->cur[0].rqbase);\
	((reqp)->cur[0].blk) = 0;\
	((reqp)->cur[0].chunk_offset) = 0;\
	}while(0)

/* this one DOES zero the start_offset 
 * intended for flow code to reset a request to the beginning */
#define PINT_REQUEST_STATE_RESET(reqp)\
	do {\
	PINT_REQUEST_STATE_RST(reqp);\
/*	PINT_REQUEST_STATE_SET_OFFSET((reqp),0);*/\
	}while(0)

/* checks to see if you have run out of request */
#define PINT_REQUEST_DONE(reqp)\
	(((reqp)->type_offset >= (reqp)->final_offset) ||\
	 ((reqp)->eof_flag))

/* checks to see if you have hit EOF */
#define PINT_REQUEST_EOF(reqp)\
	  (reqp)->eof_flag

/* set ref count of request to 1
 * never modify a refcount below zero
 */
#define PINT_REQUEST_REFSET(reqp)\
	do { \
		if ((reqp)->refcount >= 0) \
			(reqp)->refcount = 1; \
	} while(0)

/* increments ref count of request 
 * never modify a refcount below zero
 */
#define PINT_REQUEST_REFINC(reqp)\
	do { \
		if ((reqp)->refcount >= 0) \
			(reqp)->refcount++; \
	} while(0)

/* decrements ref count of request 
 * never decrement below zero
 * need to add function to recursively free request
 */
#define PINT_REQUEST_REFDEC(reqp)\
	do { \
		if ((reqp)->refcount > 0) \
			(reqp)->refcount--; \
	} while(0)

#endif /* __PINT_REQUEST_H */
