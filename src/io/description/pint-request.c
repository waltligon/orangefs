/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */       

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gossip.h>
#include <pvfs2-types.h>
#include <pvfs2-debug.h>
#include <pint-request.h>
#include <pint-distribution.h>

static int PINT_request_disp(PINT_Request *request);

/* this macro is only used in this file to add a segment to the
 * result list.
 */

#define PINT_ADD_SEGMENT(result,offset,size,mode) \
do { \
    if (size > 0) \
    { \
	    /* add a segment here */ \
	    gossip_debug(GOSSIP_REQUEST_DEBUG,"\tprocess a segment\n"); \
	    gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\t\tof %lld sz %lld\n", Ld(offset), Ld(size)); \
	    if (PINT_IS_CKSIZE(mode)) \
	    { \
		    gossip_debug(GOSSIP_REQUEST_DEBUG,"\tcount segment in checksize\n"); \
		    result->segs++; \
	    } \
	    else if (result->segs > 0 && \
				result->offset_array[result->segs-1] + \
				result->size_array[result->segs-1] == offset) \
	    { \
		    /* combine adjacent segments */ \
		    gossip_debug(GOSSIP_REQUEST_DEBUG,"\tcombine a segment %d\n", result->segs-1); \
		    result->size_array[result->segs-1] += size; \
	    } \
	    else \
	    { \
		    /* add a segment */ \
		    gossip_debug(GOSSIP_REQUEST_DEBUG,"\tadd a segment %d\n", result->segs); \
		    result->offset_array[result->segs] = offset; \
		    result->size_array[result->segs] = size; \
		    result->segs++; \
	    } \
	    result->bytes += size; \
    } \
} while (0)

/* end of the PINT_ADD_SEGMENT macro */


/* This function calls PVFS_Distribute for each contiguous chunk */
/* of the request.  PVFS_Distribute returns the number of bytes */
/* processed.  If this is less than the total bytes in the chunk */
/* this function returns otherwise it keeps processing until all */
/* chunks are done.  Returns the number of bytes processed.  It */
/* is assumed caller we retry if this is less than the total bytes */
/* in the request */
int PINT_process_request(PINT_Request_state *req,
	PINT_Request_state *mem,
	PINT_request_file_data *rfdata,
	PINT_Request_result *result,
	int mode)
{
	void *temp_space = NULL;    /* temp copy of req state for size call */
	PVFS_boolean lvl_flag;      /* indicates level should be decremented */
	PVFS_offset  contig_offset = 0; /* temp for offset of a contig region */
	PVFS_size    contig_size;   /* temp for size of a contig region */
	PVFS_size    retval;        /* return value from calls to distribute */

	if (!PINT_IS_MEMREQ(mode))
        gossip_debug(GOSSIP_REQUEST_DEBUG,
            "=========================================================\n");
	gossip_debug(GOSSIP_REQUEST_DEBUG,"PINT_process_request\n");
	/* do very basic error checking here */
	if (!req)
	{
		gossip_lerr("PINT_process_request: Bad PINT_Request_state!\n");
		return -1;
	}
	if (!result || !result->segmax || !result->bytemax)
	{
		gossip_lerr("PINT_process_request: NULL segmax or bytemax!\n");
		return -1;
	}
	if (result->segs >= result->segmax || result->bytes >= result->bytemax)
	{
		gossip_lerr("PINT_process_request: no segments or bytes requested!\n");
		return -1;
	}
	if (!PINT_IS_CKSIZE(mode) && (!result->offset_array || !result->size_array))
	{
		gossip_lerr("PINT_process_request: NULL offset or size array!\n");
		return -1;
	}
	/* initialize some variables */
	retval = 0;
	if (PINT_EQ_CKSIZE(mode)) /* be must be exact here */
	{
		/* request for a size check - do not alter request state */
		gossip_debug(GOSSIP_REQUEST_DEBUG,
				"\tsize request - copying state, hold on to your hat! dp %d\n",
				req->cur->rqbase->depth);
		temp_space = (void *)malloc(sizeof(PINT_Request_state)+
				(sizeof(PINT_reqstack)*req->cur->rqbase->depth));
		memcpy(temp_space,req,sizeof(PINT_Request_state));
		req = (PINT_Request_state *)temp_space;
		memcpy((char *)temp_space + sizeof(PINT_Request_state),
				req->cur,(sizeof(PINT_reqstack)*req->cur->rqbase->depth));
		req->cur = (PINT_reqstack *)((char *)temp_space + sizeof(PINT_Request_state));
	}
	/* check to see if we are picking up where we left off */
	if (req->lvl < 0)
	{
		gossip_debug(GOSSIP_REQUEST_DEBUG,
				"\tRequest state level < 0 - resetting request state\n");
		/* reinitialize the request state to zero */
		PINT_REQUEST_STATE_RST(req);
	}
	/* automatically set final_offset of req based on mem size */
	if (PINT_IS_CLIENT(mode) && mem && req->final_offset == 0)
	{
		req->final_offset = req->target_offset +
				mem->cur[0].rqbase->aggregate_size;
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\tsetting final offset %lld\n",
				Ld(req->final_offset));
	}
	/* automatically tile the req */
	if (!PINT_IS_MEMREQ(mode))
	{
		int64_t count;
		if (req->cur[0].rqbase)
		{
			count = req->final_offset / req->cur[0].rqbase->aggregate_size;
		}
		else
		{
			count = req->final_offset;
		}
		req->cur[0].maxel = count + 1;
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\ttiling %Ld copies\n", Ld(count+1));
	}
	/* deal with skipping over some bytes (type offset) */
	if (req->target_offset > req->type_offset)
	{
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\tskipping ahead to target_offset\n");
		/* find starting offset in request structure */
		PINT_SET_LOGICAL_SKIP(mode);
	}
	else
	{
		/* do we allow external setting of LOGICAL_SKIP */
		/* what about backwards skipping, as in seeking? */
    }
	
	/* we should be ready to begin */
	/* zero retval indicates everything flowing successfully */
	/* positive retval indicates a partial chunk was processed - so we */
	/* wait until later to retry */
	while(!retval)
	{
		if (req->cur[req->lvl].rq)
		{
		/* print the current state of the decoding process */
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\tDo seq of %lld ne %d st %lld nb %d "
		"ub %lld lb %lld as %lld co %llu\n",
				Ld(req->cur[req->lvl].rq->offset), req->cur[req->lvl].rq->num_ereqs,
				Ld(req->cur[req->lvl].rq->stride), req->cur[req->lvl].rq->num_blocks,
				Ld(req->cur[req->lvl].rq->ub), Ld(req->cur[req->lvl].rq->lb),
				Ld(req->cur[req->lvl].rq->aggregate_size),
				Ld(req->cur[req->lvl].chunk_offset));
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tlvl %d el %Ld blk %d by %lld\n",
				req->lvl, Ld(req->cur[req->lvl].el), req->cur[req->lvl].blk,
				Ld(req->bytes));
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tto %lld ta %lld fi %lld\n",
				Ld(req->type_offset), Ld(req->target_offset),
				Ld(req->final_offset));
            if (mem) /* if a mem type is specified print its state */
            {
                gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tmto %lld mta %lld mfi %lld\n",
                       Ld(mem->type_offset), Ld(mem->target_offset),
                       Ld(mem->final_offset));
            }
		}
		/* NULL type indicates packed data - handle directly */
		if (req->cur[req->lvl].rq == NULL)
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tnull type\n");
			contig_offset = req->cur[req->lvl].chunk_offset + req->bytes;
			contig_size = req->cur[req->lvl].maxel - req->bytes;
			lvl_flag = 1;
		}
		/* basic data type or contiguous data - handle directly */
		/* NULL ereq indicates current type is packed bytes */
		/* current type is contiguous because its size equals extent */
		/* AND the num_contig_chunks is 1 */
		else if ((req->cur[req->lvl].rq->ereq == NULL ||
				(req->cur[req->lvl].rq->aggregate_size ==
				(req->cur[req->lvl].rqbase->ub -
					req->cur[req->lvl].rqbase->lb) &&
				req->cur[req->lvl].rq->ereq->num_contig_chunks == 1)) &&
				req->cur[req->lvl].rq == req->cur[req->lvl].rqbase)
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tbasic type or contiguous data\n");
			contig_offset = req->cur[req->lvl].rq->offset +
					req->cur[req->lvl].chunk_offset + req->bytes +
					PINT_request_disp(req->cur[req->lvl].rq);
			contig_size = (req->cur[req->lvl].maxel *
					req->cur[req->lvl].rq->aggregate_size) - req->bytes;
			lvl_flag = 1;
		}
		/* subtype is contiguous because its size equals its extent */
		/* AND the num_contig_chunks is 1 */
		else if (req->cur[req->lvl].rq->ereq->aggregate_size ==
				(req->cur[req->lvl].rq->ereq->ub -
				req->cur[req->lvl].rq->ereq->lb) &&
				req->cur[req->lvl].rq->ereq->num_contig_chunks == 1)
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tsubtype is contiguous\n");
			contig_offset = req->cur[req->lvl].chunk_offset +
				(req->cur[req->lvl].el * (req->cur[req->lvl].rqbase->ub -
							  req->cur[req->lvl].rqbase->lb)) +
				req->cur[req->lvl].rq->offset + (req->cur[req->lvl].rq->stride *
						req->cur[req->lvl].blk) + req->bytes +
				PINT_request_disp(req->cur[req->lvl].rq);
			contig_size = (req->cur[req->lvl].rq->ereq->aggregate_size *
					req->cur[req->lvl].rq->num_ereqs) - req->bytes;
			lvl_flag = 0;
		}
		/* go to the next level and "recurse" */
		else
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tgoing to next level %d\n",req->lvl+1);
			if (!req->cur[req->lvl].rq->ereq ||
					req->lvl+1 >= req->cur[0].rqbase->depth)
			{
				gossip_lerr("PINT_process_request exceeded request depth - possibly corrupted request or request state\n");
				return -1;
			}
			req->cur[req->lvl+1].el = 0;
			req->cur[req->lvl+1].maxel = req->cur[req->lvl].rq->num_ereqs;
			req->cur[req->lvl+1].rq = req->cur[req->lvl].rq->ereq;
			req->cur[req->lvl+1].rqbase = req->cur[req->lvl].rq->ereq;
			req->cur[req->lvl+1].blk = 0;
			req->cur[req->lvl+1].chunk_offset = req->cur[req->lvl].chunk_offset +
					(req->cur[req->lvl].el * (req->cur[req->lvl].rqbase->ub -
					req->cur[req->lvl].rqbase->lb)) + req->cur[req->lvl].rq->offset +
					(req->cur[req->lvl].rq->stride * req->cur[req->lvl].blk);
			req->lvl++;
			continue;
		}
        gossip_debug(GOSSIP_REQUEST_DEBUG,
                "\tcontig_offset = %lld contig_size = %lld lvl_flag = %d\n",
                Ld(contig_offset), Ld(contig_size), lvl_flag);
		/* set this up for client processing */
		if (PINT_IS_CLIENT(mode))
		{
            /* The type_offset of the mem type and the req type should
             * track each other as the request is processed on the client
             * The value of the offset_array is used to set the mem target_offset
             * in the distribute routine, so we set it here to the type_offset of
             * the req - WBL
             */
			result->offset_array[result->segs] = req->type_offset - req->target_offset;
		}
		/*** BEFORE CALLING DISTRIBUTE ***/
		if (PINT_IS_LOGICAL_SKIP(mode))
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tprocess logical skip\n");
			if (req->type_offset + contig_size >= req->target_offset)
			{
				/* this contig chunk will exceed the target start offset */
				retval = req->target_offset - req->type_offset;
			}
			else
			{
				/* need to skip this whole block */
				retval = contig_size;
			}
			/* does this need to be here - or should it be elsewhere */
			req->eof_flag = (rfdata->fsize <= req->type_offset) &&
				!(rfdata->extend_flag);
		}
		/*** CALLING DISTRIBUTE - OR WHATEVER ***/
		else /* not logical skip or seeking */
		{
			PVFS_size sz = contig_size; /* don't modify contig_size here */
			/* stop at final offset */
			if (req->type_offset + sz > req->final_offset)
			{
				sz = req->final_offset - req->type_offset;
			}
			/* memreq mode doesn't do distribution */
			if (PINT_IS_MEMREQ(mode))
			{
				/* check for too many bytes or segs */
				if (result->bytes + sz >= result->bytemax )
				{
					sz = result->bytemax - result->bytes;
				}
				PINT_ADD_SEGMENT(result, contig_offset, sz, mode);
				retval = sz;
			}
			else
			{
				/* we process the whole thing at once */
				gossip_debug(GOSSIP_REQUEST_DEBUG,
                                             "\tcalling distribute\n");
				retval = PINT_distribute(contig_offset, sz,
                                                         rfdata, mem, result,
                                                         &req->eof_flag, mode);

                                if (-1 == retval)
                                {
                                    gossip_debug(GOSSIP_REQUEST_DEBUG,
                                                 "\tDistribute returned -1\n");
                                    req->type_offset = req->final_offset;
                                    result->segs = 0;
                                    result->bytes = 0;
                                    return 0;
                                }
			}
		}
		/*** AFTER CALLING DISTRIBUTE ***/
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\tretval = %lld\n", Ld(retval));
		req->type_offset += retval;
		/* see if we processed all of the bytes expected */
		if (retval != contig_size)
		{
			/* no so record the bytes processed */
			req->bytes += retval;
			if (PINT_IS_LOGICAL_SKIP(mode))
			{
				/* now starting processing for real */
				PINT_CLR_LOGICAL_SKIP(mode);
				retval = 0; /* keeps the loop going */
				gossip_debug(GOSSIP_REQUEST_DEBUG,
						"\texiting logical skip because distribute indicates done\n");
				continue;
			}
			else
			{
				/* all we can do for now get outta here */
				gossip_debug(GOSSIP_REQUEST_DEBUG,
						"\texiting distribute returned less than expected\n");
				break;
			}
		}
		/* processed all bytes so continue on and return from level */
		if (lvl_flag)
		{
			req->lvl--;
		}

	return_from_level:
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\treturn from level %d\n",req->lvl);
		retval = 0;
		req->bytes = 0;
		if (req->lvl < 0)
		{
			/* we have processed the entire request */
			break;
		}
		/* go to the next block */
		gossip_debug(GOSSIP_REQUEST_DEBUG,"\tgoing to next block\n");
		req->cur[req->lvl].blk++;
		if (req->cur[req->lvl].blk >= req->cur[req->lvl].rq->num_blocks)
		{
			/* that was the last block */
			req->cur[req->lvl].blk = 0;
			/* go to next item in sequence chain */
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tgoing to next item in sequence chain\n");
			req->cur[req->lvl].rq = req->cur[req->lvl].rq->sreq;
			if (req->cur[req->lvl].rq == NULL)
			{
				/* that was last item in sequence chain */
				req->cur[req->lvl].rq = req->cur[req->lvl].rqbase;
				/* go to next element in block of level above */
				gossip_debug(GOSSIP_REQUEST_DEBUG,
						"\tgoing to next element in block of level above\n");
				req->cur[req->lvl].el++;
				if (req->cur[req->lvl].el >= req->cur[req->lvl].maxel)
				{
					/* that was last element in block of level above */
					req->lvl--;
					/* go back up a level */
					goto return_from_level;
				}
			}
		}
		/* check to see if we are finished */
		if (result->bytes == result->bytemax ||
				(!PINT_IS_CKSIZE(mode) && (result->segs == result->segmax)))
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tran out of segments or bytes\n");
			break;
		}
		/* look for end of request */
		if (req->type_offset >= req->final_offset)
		{
			gossip_debug(GOSSIP_REQUEST_DEBUG,"\tend of the request\n");
			break;
		}
	} /* this is the end of the while loop */
	gossip_debug(GOSSIP_REQUEST_DEBUG,"\tdone sg %d sm %d by %lld bm %lld ta %lld to %lld fo %lld eof %d\n",
			result->segs, result->segmax, Ld(result->bytes), Ld(result->bytemax),
			Ld(req->target_offset), Ld(req->type_offset), Ld(req->final_offset),
			req->eof_flag);
	if (PINT_EQ_CKSIZE(mode)) /* must be exact here */
	{
		/* restore request state */
		free(temp_space);
	}
	if (!PINT_IS_MEMREQ(mode))
        gossip_debug(GOSSIP_REQUEST_DEBUG,
            "=========================================================\n");
	return result->bytes;
}

/* this function runs down the ereq list and adds up the offsets */
/* present in the request records */
static int PINT_request_disp(PINT_Request *request)
{
	int disp = 0;
	PINT_Request *r;
	gossip_debug(GOSSIP_REQUEST_DEBUG,"\tRequest disp\n");
	for (r = request->ereq; r; r = r->ereq)
	{
		disp += r->offset;
	}
	return disp;
}

/* This function creates a request state and sets it up to begin */
/* processing a request */
struct PINT_Request_state *PINT_new_request_state(PINT_Request *request)
{
	struct PINT_Request_state *req;
	int32_t rqdepth;
	gossip_debug(GOSSIP_REQUEST_DEBUG,"PINT_New_request_state\n");
	if (!(req = (struct PINT_Request_state *)malloc(sizeof(struct PINT_Request_state))))
	{
		gossip_lerr("PINT_New_request_state failed to malloc req !!!!!!!\n");
		return NULL;
	}
	req->lvl = 0;
	req->bytes = 0;
	req->type_offset = 0;
	req->target_offset = 0;
	req->final_offset = request->aggregate_size;
	req->eof_flag = 0;
	/* we assume null request is a contiguous byte range depth 1 */
	if (request)
	{
		rqdepth = request->depth;
	}
	else
	{
		rqdepth = 1;
	}
	if (!(req->cur = (struct PINT_reqstack *)malloc(rqdepth *
			sizeof(struct PINT_reqstack))))
	{
		gossip_lerr("PINT_New_request_state failed to malloc rqstack !!!!!!\n");
		return NULL;
	}
	req->cur[req->lvl].maxel = 1; /* transfer one instance of request */
	req->cur[req->lvl].el = 0;
	req->cur[req->lvl].rq = request;
	req->cur[req->lvl].rqbase = request;
	req->cur[req->lvl].blk = 0;
	req->cur[req->lvl].chunk_offset = 0; /* transfer from inital file offset */
	return req;
}

/* This function frees request state structures */
void PINT_free_request_state (PINT_Request_state *req)
{
	free(req->cur);
	free(req);
}

/**
 * Returns:
 *     - If the distribute finds file data on the server, then the byte
 *       displacement from the input argument offset to the last byte
 *       in the segment processed regardless of whether that byte is in
 *       the current distribution or not
 *     - -1 if there is no distribution data available on the server
 * Inputs:
 * 	- offset and size are the contiguous region in logical file
 * 	space we are to process
 * 	- segmax and segs are the maximum segments we can create and
 * 	the number created so far
 * 	- bytemax and bytes are the maximum number of bytes we can
 * 	process and the number processed so far
 * 	- offset array and size array are where we output segments
 * 	- extend flags indicates we should proceed past EOF
 * Outputs:
 * 	- updates index, bytes, offset_array, size_array
 * 	- sets eof_flag if at end of file
 * 	- returns logical file space offset differential of last
 * 	byte processed
 * When client flag is set
 * 	segment offsets are computed based on buffer offset in
 * 	offset_array[*segs]
 */
PVFS_size PINT_distribute(PVFS_offset offset,
                          PVFS_size size,
                          PINT_request_file_data *rfdata,
                          PINT_Request_state *mem,
                          PINT_Request_result *result,
                          PVFS_boolean *eof_flag,
                          int mode)
{
    PVFS_offset orig_offset;
    PVFS_size   orig_size;
    PVFS_offset loff;    /* next logical offset within requested region */
    PVFS_offset diff;    /* difference between loff and offset of region */
    PVFS_offset poff;    /* physical offste corresponding to loff */
    PVFS_size   sz;      /* number of bytes in requested region after loff */
    PVFS_size   fraglen; /* length of physical strip contiguous on server */
    PVFS_size   retval;

    gossip_debug(GOSSIP_REQUEST_DEBUG,"\tPINT_distribute\n");
    gossip_debug(GOSSIP_REQUEST_DEBUG,
                 "\t\tof %lld sz %lld ix %d sm %d by %lld bm %lld "
                 "fsz %lld exfl %d\n",
                 Ld(offset), Ld(size), result->segs, result->segmax,
                 Ld(result->bytes),
                 Ld(result->bytemax),
                 Ld(rfdata->fsize), rfdata->extend_flag);
    orig_offset = offset;
    orig_size = size;
    *eof_flag = 0;
    
    /* check if we have maxed out result */
    if ((!PINT_IS_CKSIZE(mode) && (result->segs >= result->segmax)) ||
        result->bytes >= result->bytemax || size == 0)
    {
        /* not an error, but we didn't process any bytes */
        gossip_debug(GOSSIP_REQUEST_DEBUG,
                     "\t\trequested zero segs or zero bytes\n");
        return 0;
    }
    
    /* verify some critical pointers */
    if (!rfdata || !rfdata->dist || !rfdata->dist->methods ||
        !rfdata->dist->params)
    {
        if (!rfdata)
            gossip_debug(GOSSIP_REQUEST_DEBUG,"rfdata is NULL\n");
        else if (!rfdata->dist)
            gossip_debug(GOSSIP_REQUEST_DEBUG,"rfdata->dist is NULL\n");
        else if (!rfdata->dist->methods)
            gossip_debug(GOSSIP_REQUEST_DEBUG,"rfdata->dist->methods is NULL\n");
        else if (!rfdata->dist->params)
            gossip_debug(GOSSIP_REQUEST_DEBUG,"rfdata->dist->params is NULL\n");
        gossip_lerr("Bad Distribution! Bailing out!\n");
        return 0;
    }
    
    /* find next logical offset on this server */
    loff = (*rfdata->dist->methods->next_mapped_offset)(rfdata->dist->params,
                                                        rfdata,
                                                        offset);

    /* If there is no data on this server, immediately return */
    if (-1 == loff)
    {
        gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\treturn, dist says no data\n");
        return -1;
    }
    
    /* make sure loff is still within requested region */
    while ((diff = loff - offset) < size)
    {
        gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tbegin iteration loff: %lld\n",
                     Ld(loff));
        
        /* find physical offset for this loff */
        poff = (*rfdata->dist->methods->logical_to_physical_offset)
            (rfdata->dist->params,
             rfdata,
             loff);
        
        /* find how much of requested region remains after loff */
        sz = size - diff;
        
        /* find how much data after loff/poff is on this server */
        fraglen = (*rfdata->dist->methods->contiguous_length)
            (rfdata->dist->params,
             rfdata,
             poff);
        
        /* compare that amount to amount of data in requested region */
        if (sz > fraglen && rfdata->server_ct != 1)
        {
            /* frag extends beyond this strip */
            gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tfrag extends beyond strip\n");
            sz = fraglen;
        }
        /* check to see if exceeds bytemax */
        if (result->bytes + sz > result->bytemax)
        {
            /* contiguous segment extends beyond byte limit */
            gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tsegment exceeds byte limit\n");
            sz = result->bytemax - result->bytes;
        }
        /* check to se if exceeds end of file */
        if (poff+sz > rfdata->fsize)
        {
            /* check for append */
            if (rfdata->extend_flag)
            {
         	/* update the file size info */
                gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tfile being extended\n");
                rfdata->fsize = poff + sz;
            }
            else
            {
                /* hit end of file */
                gossip_debug(GOSSIP_REQUEST_DEBUG,
                             "\t\thit end of file: po %lld sz %lld fsz %lld\n",
                             Ld(poff), Ld(sz), Ld(rfdata->fsize));
                *eof_flag = 1;
                sz = rfdata->fsize - poff;
                if (sz <= 0)
                {
                    /* not even any more bytes before EOF */
                    gossip_debug(GOSSIP_REQUEST_DEBUG,
                                 "\t\tend of file and no more bytes\n");
                    break;
                }
            }
        }
        /* process a segment */
        if (PINT_IS_CLIENT(mode))
        {
            poff = result->offset_array[result->segs] + diff;
            gossip_debug(GOSSIP_REQUEST_DEBUG,
                         "\t\tclient lstof %lld diff %lld sgof %lld\n",
                         Ld(result->offset_array[result->segs]), Ld(diff),
                         Ld(poff));
        }
        /* else poff is the offset of the segment */
        if (PINT_IS_CLIENT(mode) && mem)
        {
            gossip_debug(GOSSIP_REQUEST_DEBUG,
                         "**********CALL***PROCESS*********\n");
            gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tsegment of %lld sz %lld\n",
                         Ld(poff), Ld(sz));

            /* call request processor to decode request type */
            /* sequential offset is offset_array[*segs] */
            /* size is sz */
            PINT_REQUEST_STATE_SET_TARGET(mem, poff);
            PINT_REQUEST_STATE_SET_FINAL(mem, poff + sz);
            PINT_process_request(mem, NULL, rfdata, result, mode|PINT_MEMREQ);
            sz = mem->type_offset - poff;
            
            gossip_debug(GOSSIP_REQUEST_DEBUG,
                         "*****RETURN***FROM***PROCESS*****\n");
        }
        else
        {
            PINT_ADD_SEGMENT(result, poff, sz, mode);
        }
        /* this is used by client code */
        if (PINT_IS_CLIENT(mode) && result->segs < result->segmax)
        {
            result->offset_array[result->segs] =
                result->offset_array[result->segs - 1] + 
                result->size_array[result->segs - 1];
        }
        /* sz should never be zero or negative */
        if (sz < 1)
        {
            gossip_lerr("Error in distribution processing!\n");
            break;
        }
        /* prepare for next iteration */
        loff  += sz;
        size  -= loff - offset;
        offset = loff;
        /* find next logical offset on this server */
        loff = (*rfdata->dist->methods->next_mapped_offset)
            (rfdata->dist->params,
             rfdata,
             offset);
        assert(-1 != loff);
        
        gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tend iteration\n");
        /* see if we are finished */
        if (result->bytes >= result->bytemax ||
            (!PINT_IS_CKSIZE(mode) && (result->segs >= result->segmax)))
        {
            gossip_debug(GOSSIP_REQUEST_DEBUG,
                         "\t\tdone with segments or bytes\n");
            break;
        }
    }
    
    gossip_debug(GOSSIP_REQUEST_DEBUG,
                 "\t\t\tof %lld sz %lld sg %d sm %d by %lld bm %lld\n",
                 Ld(offset), Ld(size), result->segs, result->segmax,
                 Ld(result->bytes),
                 Ld(result->bytemax));
    
    /* find physical offset for this loff */
    poff = (*rfdata->dist->methods->logical_to_physical_offset)
        (rfdata->dist->params, rfdata, loff);
    
    gossip_debug(GOSSIP_REQUEST_DEBUG,
                 "\t\t\tnext loff: %lld next poff: %lld\n",
                 Ld(loff), Ld(poff));
    
    if (poff >= rfdata->fsize && !rfdata->extend_flag)
    {
        /* end of file - thus end of request */
        *eof_flag = 1;
        gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\t\t[return value] %lld (EOF)\n",
                     Ld(orig_size));
        retval = orig_size;
    }
    if (loff >= orig_offset + orig_size)
    {
        gossip_debug(GOSSIP_REQUEST_DEBUG,
                     "\t\t\t(return value) %lld%s\n", Ld(orig_size),
                     *eof_flag ? " (EOF)" : "");
        retval = orig_size;
    }
    else
    {
        gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\t\treturn value %lld%s\n",
                     Ld(offset - orig_offset), *eof_flag ? " (EOF)" : "");
        retval = (offset - orig_offset);
    }
    gossip_debug(GOSSIP_REQUEST_DEBUG,"\t\tfinished\n");

    return retval;
}

/* Function: PINT_Request_commit
 * Objective: Write out the request tree to a contiguous
 * region - return the offset of the next empty space in region
 */
/* Traverse the binary tree, pick up each request struct, modify its type
 * and set the offsets in the ereq and sreq of the request struct. These
 * offsets must reflect the current offsets and then write each struct 
 * to the contiguous memory region
 */
int PINT_request_commit(PINT_Request *region, PINT_Request *node)
{
    int32_t index = 0;
    PINT_do_request_commit(region, node, &index, 0);
    return 0;
}

int PINT_do_clear_commit(PINT_Request *node, int32_t depth)
{
	if (node == NULL)
		return -1;

	if (!node->committed && depth > 0)
		return 0;
	
	node->committed = 0;

	if (node->ereq)
	{
		PINT_do_clear_commit(node->ereq, depth+1);
	}
	if (node->sreq)
	{
		PINT_do_clear_commit(node->sreq, depth+1);
	}
	return 0;
}

PINT_Request *PINT_do_request_commit(PINT_Request *region, PINT_Request *node,
		int32_t *index, int32_t depth)
{
	int node_was_committed = 0;

	/* Leaf Node? */
	if(node == NULL)
		return NULL;
  
	gossip_debug(GOSSIP_REQUEST_DEBUG,"%s: commit node %p\n", __func__, node);

	/* catches any previously packed structures */
	if (node->committed == -1)
	{
		node->committed = 0;
		node_was_committed = 1;
	}

	/* this node was previously committed */
	if (node->committed)
	{
		gossip_debug(GOSSIP_REQUEST_DEBUG,"previously commited %d\n", node->committed);
		return &region[node->committed]; /* should contain the index */
	}

	/* Copy node to contiguous region */
	gossip_debug(GOSSIP_REQUEST_DEBUG,"node stored at %d\n", *index);
	memcpy(&region[*index], node, sizeof(struct PINT_Request));
	node->committed = *index;
	*index = *index + 1;

	/* Update ereq so that the relative positions are maintained */
	if (node->ereq)
	{
		region[node->committed].ereq =
				PINT_do_request_commit(region, node->ereq, index, depth+1);
	}
	else
	{
		region[node->committed].ereq = NULL;
	}

	/* Update sreq so that the relative positions are maintained */
	if (node->sreq)
	{
		region[node->committed].sreq =
				PINT_do_request_commit(region, node->sreq, index, depth+1);
	}
	else
	{
		region[node->committed].sreq = NULL;
	}

	if (depth == 0)
	{
		gossip_debug(GOSSIP_REQUEST_DEBUG,"clearing tree\n");
		PINT_do_clear_commit(node, 0);
		/* this indicates the region is packed */
		region->committed = -1;
		/* if the original request was committed, this restores that */
		if (node_was_committed)
		{
			node->committed = -1;
		}
	}

	/* Return the index of the committed struct */ 
	return &region[node->committed]; 
}

/* This function converts pointers to array indexes for transport
 * The input Request MUST be committed
 */
int PINT_request_encode(struct PINT_Request *req)
{
	int r;
	if (!PINT_REQUEST_IS_PACKED(req))
		return -1;
	for (r = 0; r <= PINT_REQUEST_NEST_SIZE(req); r++)
	{
		if (req[r].ereq)
			req[r].ereq = (PINT_Request *) (req[r].ereq - &req[0]);
		else
			req[r].ereq = (PINT_Request *) -1;
		if (req[r].sreq)
			req[r].sreq = (PINT_Request *) (req[r].sreq - &req[0]);
		else
			req[r].sreq = (PINT_Request *) -1;
	}
	return 0;
}

/* This function coverts array indexes back to pointers after transport
 * The input Request MUST be committed
 */
int PINT_request_decode(struct PINT_Request *req)
{
	int r;
	if (!PINT_REQUEST_IS_PACKED(req))
		return -1;
	for (r = 0; r <= PINT_REQUEST_NEST_SIZE(req); r++)
	{
		/* type must match the encoding type in encode_PVFS_Request */
		if ((u_int32_t)(intptr_t) req[r].ereq == (u_int32_t) -1)
			req[r].ereq = 0;
		else
			req[r].ereq = &req[0] + (unsigned long) req[r].ereq;
		if ((u_int32_t)(intptr_t) req[r].sreq == (u_int32_t) -1)
			req[r].sreq = 0;
		else
			req[r].sreq = &req[0] + (unsigned long) req[r].sreq;
	}
	return 0;
}
    

void PINT_dump_packed_request(PINT_Request *req)
{
	int i;
	if (!PINT_REQUEST_IS_PACKED(req))
		return;
	for (i = 0; i < PINT_REQUEST_NEST_SIZE(req)+1; i++)
	{
		PINT_dump_request(req+i);
	}
}

void PINT_dump_request(PINT_Request *req)
{
	gossip_debug(GOSSIP_REQUEST_DEBUG,"**********************\n");
	gossip_debug(GOSSIP_REQUEST_DEBUG,"address:\t%p\n",req);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"offset:\t\t%d\n",(int)req->offset);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"num_ereqs:\t%d\n",(int)req->num_ereqs);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"num_blocks:\t%d\n",(int)req->num_blocks);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"stride:\t\t%d\n",(int)req->stride);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"ub:\t\t%d\n",(int)req->ub);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"lb:\t\t%d\n",(int)req->lb);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"agg_size:\t%d\n",(int)req->aggregate_size);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"num_chunk:\t%d\n",(int)req->num_contig_chunks);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"depth:\t\t%d\n",(int)req->depth);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"num_nest:\t%d\n",(int)req->num_nested_req);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"commit:\t\t%d\n",(int)req->committed);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"refcount:\t\t%d\n",(int)req->refcount);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"ereq:\t\t%p\n",req->ereq);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"sreq:\t\t%p\n",req->sreq);
	gossip_debug(GOSSIP_REQUEST_DEBUG,"**********************\n");
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=4 sts=4 sw=4 expandtab
 */
