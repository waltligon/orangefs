/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gossip.h>
#include <pvfs2-types.h>
#include <pvfs2-debug.h>
#include <pint-request.h>
#include <pvfs-distribution.h>
#include <pint-distribution.h>

/* This function calls PVFS_Distribute for each contiguous chunk */
/* of the request.  PVFS_Distribute returns the number of bytes */
/* processed.  If this is less than the total bytes in the chunk */
/* this function returns otherwise it keeps processing until all */
/* chunks are done.  Returns the number of bytes processed.  It */
/* is assumed caller we retry if this is less than the total bytes */
/* in the request */

int PINT_Process_request(PINT_Request_state *req,
	PINT_Request_file_data *rfdata, int32_t *segmax,
	PVFS_offset *offset_array, PVFS_size *size_array,
	PVFS_offset *start_offset, PVFS_size *bytemax,
	PVFS_boolean *eof_flag, int mode)
{
	void *temp_space = NULL;    /* temp copy of req state for size call */
	PVFS_boolean lvl_flag;      /* indicates level should be decremented */
	PVFS_offset  contig_offset; /* temp for offset of a contig region */
	PVFS_size    contig_size;   /* temp for size of a contig region */
	PVFS_size    retval;        /* return value from calls to distribute */
	PVFS_size    bytes_processed;
	int32_t segs_processed;
	gossip_debug(REQUEST_DEBUG,"PINT_Process_request\n");
	/* do very basic error checking here */
	if (!req)
	{
		gossip_lerr("PINT_Process_request: Bad PINT_Request_state!\n");
		return -1;
	}
	if (!segmax || !bytemax)
	{
		gossip_lerr("PINT_Process_request: NULL segmax or bytemax!\n");
		return -1;
	}
	if (!PINT_IS_CKSIZE(mode) && (!offset_array || !size_array))
	{
		gossip_lerr("PINT_Process_request: NULL offset or size array!\n");
		return -1;
	}
	/* initialize some variables */
	segs_processed = 0;
	bytes_processed = 0;
	retval = 0;
	if (*start_offset == -1)
	{
		/* this indicates we already finished the request */
		gossip_lerr("PINT_Process_request: start offset -1!\n");
		*bytemax = 0;
		*segmax = 0;
		return 0;
	}
	if (PINT_EQ_CKSIZE(mode)) /* be must be exact here */
	{
		/* request for a size check - do not alter request state */
		gossip_debug(REQUEST_DEBUG,"\tsize request - copying state, hold on to your hat! dp %d\n",req->cur->rqbase->depth);
		temp_space = (void *)malloc(sizeof(PVFS_offset)+
				sizeof(PINT_Request_state)+
				(sizeof(PINT_reqstack)*req->cur->rqbase->depth));
		memcpy(temp_space, start_offset, sizeof(PVFS_offset));
		start_offset = (PVFS_offset *)temp_space;
		memcpy((temp_space + sizeof(PVFS_offset)),req,sizeof(PINT_Request_state));
		req = (PINT_Request_state *)(temp_space + sizeof(PVFS_offset));
		memcpy((temp_space + sizeof(PVFS_offset) + sizeof(PINT_Request_state)),
				req->cur,(sizeof(PINT_reqstack)*req->cur->rqbase->depth));
		req->cur = (PINT_reqstack *)(temp_space + sizeof(PVFS_offset) +
				sizeof(PINT_Request_state));
	}
	gossip_debug(REQUEST_DEBUG,"\tstart_offset == %lld\n", *start_offset);
	/* check to see if we are picking up where we left off */
	if (req->lvl < 0 || *start_offset < req->last_offset)
	{
		gossip_debug(REQUEST_DEBUG,
				"\trequested start_offset before current offset\n");
		/* reinitialize the request state to zero */
		req->last_offset = 0;
		req->buf_offset = 0;
		req->lvl = 0;
		req->bytes = 0;
		req->cur[0].el = 0;
		req->cur[0].rq = req->cur[0].rqbase;
		req->cur[0].blk = 0;
		req->cur[0].chunk_offset = 0;
	}
	/* check to see of we are skipping some bytes */
	if (*start_offset > req->last_offset)
	{
		gossip_debug(REQUEST_DEBUG,"\tskipping ahead to start_offset\n");
		/* find start_offset in request structure */
		PINT_SET_SEEKING(mode);
	}
	else
	{
		PINT_CLR_SEEKING(mode);
	}
	/* we should be ready to begin */
	/* zero retval indicates everything flowing successfully */
	/* positive retval indicates a partial chunk was processed - so we */
	/* wait until later to retry */
	while(!retval)
	{
		if (req->cur[req->lvl].rq)
		{
		gossip_debug(REQUEST_DEBUG,"\tDo seq of %lld ne %d st %lld nb %d ",
				req->cur[req->lvl].rq->offset, req->cur[req->lvl].rq->num_ereqs,
				req->cur[req->lvl].rq->stride, req->cur[req->lvl].rq->num_blocks);
		gossip_debug(REQUEST_DEBUG,"ub %lld lb %lld as %lld co %llu\n",
				req->cur[req->lvl].rq->ub, req->cur[req->lvl].rq->lb,
				req->cur[req->lvl].rq->aggregate_size, req->cur[req->lvl].chunk_offset);
		gossip_debug(REQUEST_DEBUG,"\t\tlvl %d el %d blk %d by %lld bo %lld\n", req->lvl,
				req->cur[req->lvl].el, req->cur[req->lvl].blk, req->bytes,
				req->buf_offset);
		}
		/* NULL type indicates packed data - handle directly */
		if (req->cur[req->lvl].rq == NULL)
		{
			gossip_debug(REQUEST_DEBUG,"\tnull type\n");
			contig_offset = req->cur[req->lvl].chunk_offset + req->bytes;
			contig_size = req->cur[req->lvl].maxel - req->bytes;
			lvl_flag = 1;
		}
		/* basic data type or contiguous data - handle directly */
		/* NULL ereq indicates current type is packed bytes */
		/* current type is contiguous because its size equals ub minus lb */
		else if ((req->cur[req->lvl].rq->ereq == NULL ||
				req->cur[req->lvl].rq->aggregate_size ==
				(req->cur[req->lvl].rqbase->ub -
					req->cur[req->lvl].rqbase->lb)) &&
				req->cur[req->lvl].rq == req->cur[req->lvl].rqbase)
		{
			gossip_debug(REQUEST_DEBUG,"\tbasic type or contiguous data\n");
			contig_offset = req->cur[req->lvl].rq->offset +
					req->cur[req->lvl].chunk_offset + req->bytes;
			contig_size = (req->cur[req->lvl].maxel *
					req->cur[req->lvl].rq->aggregate_size) - req->bytes;
			lvl_flag = 1;
		}
		/* subtype is contiguous because its size equals its ub minus lb */
		else if (req->cur[req->lvl].rq->ereq->aggregate_size ==
				(req->cur[req->lvl].rq->ereq->ub -
				req->cur[req->lvl].rq->ereq->lb))
		{
			gossip_debug(REQUEST_DEBUG,"\tsubtype is contiguous\n");
			contig_offset = req->cur[req->lvl].chunk_offset +
				(req->cur[req->lvl].el * (req->cur[req->lvl].rqbase->ub -
												  req->cur[req->lvl].rqbase->lb)) +
				req->cur[req->lvl].rq->offset + (req->cur[req->lvl].rq->stride *
						req->cur[req->lvl].blk) + req->bytes;
			contig_size = (req->cur[req->lvl].rq->ereq->aggregate_size *
					req->cur[req->lvl].rq->num_ereqs) - req->bytes;
			lvl_flag = 0;
		}
		/* go to the next level and "recurse" */
		else
		{
			gossip_debug(REQUEST_DEBUG,"\tgoing to next level %d\n",req->lvl+1);
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
		/* set this up for client processing */
		if (PINT_IS_CLIENT(mode))
		{
			offset_array[segs_processed] = req->buf_offset;
		}
		if (PINT_IS_SEEKING(mode))
		{
			/* don't need to call distribute */
			if (*start_offset <= contig_offset + contig_size)
			{
				retval = *start_offset - contig_offset;
				/* this contig chunk will exceed the target start offset */
				if (retval < 0)
				{
					gossip_debug(REQUEST_DEBUG, "\texiting seek midway\n");
					retval = 0; /* keeps loop going */
				}
				else
				{
					gossip_debug(REQUEST_DEBUG,
							"\tchunk exceeds target offset rv:%lld\n", retval);
				}
			}
			else
			{
				/* need to skip this whole block */
				retval = contig_size;
				gossip_debug(REQUEST_DEBUG,
						"\tskipping whole block rv:%lld\n", retval);
			}
		}
		else if (PINT_IS_LOGICAL_SKIP(mode))
		{
			gossip_debug(REQUEST_DEBUG,"\tskip distribute\n");
			if (bytes_processed + contig_size > *bytemax)
			{
				retval = *bytemax - bytes_processed;
				bytes_processed = *bytemax;
			}
			else
			{
				retval = contig_size;
				bytes_processed += contig_size;
			}
			*eof_flag = (rfdata->fsize <= bytes_processed);
		}
		else
		{
			/* we process the whole thing at once */
			retval = PINT_Distribute(contig_offset, contig_size, rfdata,
					&bytes_processed, *bytemax, &segs_processed, *segmax,
					offset_array, size_array, eof_flag, mode);
		}
		req->buf_offset += retval;
		/* see if we processed all of the bytes expected */
		if (retval != contig_size)
		{
			/* no so record the bytes processed */
			req->bytes += retval;
			if (PINT_IS_SEEKING(mode))
			{
				/* now starting processing for real */
				PINT_CLR_SEEKING(mode);
				gossip_debug(REQUEST_DEBUG,
						"\texiting seek because distribute indicates done\n");
				continue;
			}
			else
			{
				/* all we can do for now get outta here */
				gossip_debug(REQUEST_DEBUG,
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
		gossip_debug(REQUEST_DEBUG,"\treturn from level %d\n",req->lvl);
		retval = 0;
		req->bytes = 0;
		if (req->lvl < 0)
		{
			/* we have processed the entire request */
			break;
		}
		/* go to the next block */
		gossip_debug(REQUEST_DEBUG,"\tgoing to next block\n");
		req->cur[req->lvl].blk++;
		if (req->cur[req->lvl].blk >= req->cur[req->lvl].rq->num_blocks)
		{
			/* that was the last block */
			req->cur[req->lvl].blk = 0;
			/* go to next item in sequence chain */
			gossip_debug(REQUEST_DEBUG,"\tgoing to next item in sequence chain\n");
			req->cur[req->lvl].rq = req->cur[req->lvl].rq->sreq;
			if (req->cur[req->lvl].rq == NULL)
			{
				/* that was last item in sequence chain */
				req->cur[req->lvl].rq = req->cur[req->lvl].rqbase;
				/* go to next element in block of level above */
				gossip_debug(REQUEST_DEBUG,
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
		if (bytes_processed == *bytemax ||
				(!PINT_IS_CKSIZE(mode) && (segs_processed == *segmax)))
		{
			break;
		}
	} /* this is the end of the while loop */
	if (req->lvl < 0 || *eof_flag)
	{
		*start_offset = -1;
	}
	else
	{
		*start_offset = req->last_offset = (req->cur[req->lvl].chunk_offset +
				(req->cur[req->lvl].el * (req->cur[req->lvl].rqbase->ub -
				req->cur[req->lvl].rqbase->lb)) + req->cur[req->lvl].rq->offset +
				(req->cur[req->lvl].rq->stride * req->cur[req->lvl].blk)) +
				req->bytes;
	}
	*segmax = segs_processed;
	*bytemax = bytes_processed;
	gossip_debug(REQUEST_DEBUG,"\tdone\n");
	gossip_debug(REQUEST_DEBUG,"\t\tsm %d bm %lld so %lld bo %lld eof %d\n",
			*segmax, *bytemax, *start_offset, req->buf_offset, *eof_flag);
	if (PINT_EQ_CKSIZE(mode)) /* must be exact here */
	{
		/* restore request state */
		free(temp_space);
	}
	return bytes_processed;
}

/* This function creates a request state and sets it up to begin */
/* processing a request */
struct PINT_Request_state *PINT_New_request_state (PINT_Request *request)
{
	struct PINT_Request_state *req;
	int32_t rqdepth;
	gossip_debug(REQUEST_DEBUG,"PINT_New_req\n");
	if (!(req = (struct PINT_Request_state *)malloc(sizeof(struct PINT_Request_state))))
	{
		gossip_lerr("PINT_New_req failed to malloc req !!!!!!!\n");
		return NULL;
	}
	req->lvl = 0;
	req->bytes = 0;
	req->buf_offset = 0;
	req->last_offset = 0;
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
		gossip_lerr("PINT_New_req failed to malloc rqstack !!!!!!\n");
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
void PINT_Free_request_state (PINT_Request_state *req)
{
	free(req->cur);
	free(req);
}

/* This function should return the byte displacement from the input argument
 * offset to the last byte in the segment processed regardless of
 * whether that byte is in the current distribution or not.
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
PVFS_size PINT_Distribute(PVFS_offset offset, PVFS_size size,
		PINT_Request_file_data *rfdata, PVFS_size *bytes, PVFS_size bytemax,
		int32_t *segs, int32_t segmax, PVFS_offset *offset_array,
		PVFS_size *size_array, PVFS_boolean *eof_flag, int mode)
{
	PVFS_offset orig_offset;
	PVFS_size   orig_size;
   PVFS_offset loff;    /* next logical offset within requested region */
   PVFS_offset diff;    /* difference between loff and offset of region */
   PVFS_offset poff;    /* physical offste corresponding to loff */
   PVFS_size   sz;      /* number of bytes in requested region after loff */
   PVFS_size   fraglen; /* length of physical strip contiguous on server */

	gossip_debug(REQUEST_DEBUG,"\tPINT_Distribute\n");
	gossip_debug(REQUEST_DEBUG,
			"\t\tof %lld sz %lld ix %d sm %d by %lld bm %lld ",
			offset, size, *segs, segmax, *bytes, bytemax);
	gossip_debug(REQUEST_DEBUG,
			"fsz %lld exfl %d\n",
			rfdata->fsize, rfdata->extend_flag);
	orig_offset = offset;
	orig_size = size;
	*eof_flag = 0;
	if ((!PINT_IS_CKSIZE(mode) && (*segs >= segmax)) ||
			*bytes >= bytemax || size == 0)
	{
		/* not an error, but we didn't process any bytes */
		gossip_debug(REQUEST_DEBUG,"\t\trequested zero segs or zero bytes\n");
		return 0;
	}
	/* find next logical offset on this server */
   loff = (*rfdata->dist->methods->next_mapped_offset)
			(rfdata->dist->params, rfdata->iod_num, rfdata->iod_count, offset);
	/* make sure loff is still within requested region */
   while ((diff = loff - offset) < size)
   {
		gossip_debug(REQUEST_DEBUG,"\t\tbegin iteration loff: %lld\n", loff);
		/* find physical offset for this loff */
      poff = (*rfdata->dist->methods->logical_to_physical_offset)
				(rfdata->dist->params, rfdata->iod_num, rfdata->iod_count, loff);
		/* find how much of requested region remains after loff */
      sz = size - diff;
		/* find how much data after loff/poff is on this server */
      fraglen = (*rfdata->dist->methods->contiguous_length)
				(rfdata->dist->params, rfdata->iod_num, rfdata->iod_count, poff);
		/* compare that amount to amount of data in requested region */
      if (sz > fraglen && rfdata->iod_count != 1)
		{
			/* frag extends beyond this strip */
			gossip_debug(REQUEST_DEBUG,"\t\tfrag extends beyond strip\n");
			sz = fraglen;
		}
		/* check to see if exceeds bytemax */
		if (*bytes + sz > bytemax)
		{
			/* contiguous segment extends beyond byte limit */
			gossip_debug(REQUEST_DEBUG,"\t\tsegment exceeds byte limit\n");
			sz = bytemax - *bytes;
		}
		/* check to se if exceeds end of file */
		if (poff+sz > rfdata->fsize)
		{
			/* check for append */
			if (rfdata->extend_flag)
			{
         	/* update the file size info */
				gossip_debug(REQUEST_DEBUG,"\t\tfile being extended\n");
				rfdata->fsize = poff + sz;
			}
			else
			{
				/* hit end of file */
				gossip_debug(REQUEST_DEBUG,
						"\t\thit end of file: po %lld sz %lld fsz %lld\n",
						poff, sz, rfdata->fsize);
				*eof_flag = 1;
				sz = rfdata->fsize - poff;
				if (sz <= 0)
				{
					/* not even any more bytes before EOF */
					gossip_debug(REQUEST_DEBUG,
							"\t\tend of file and no more bytes\n");
					break;
				}
			}
		}
		/* add a segment entry */
		gossip_debug(REQUEST_DEBUG,"\t\tadd a segment\n");
		switch (mode)
		{
		case PINT_CLIENT :
			gossip_debug(REQUEST_DEBUG,"\t\t\tsg %d of %lld sz %lld\n", *segs,
					offset_array[*segs] + diff, sz);
			offset_array[*segs] += diff;
			break;
		case PINT_SERVER :
			gossip_debug(REQUEST_DEBUG,"\t\t\tsg %d of %lld sz %lld\n", *segs,
					poff, sz);
			offset_array[*segs] = poff;
			break;
		case PINT_CKSIZE :
		case PINT_CKSIZE_MODIFY_OFFSET :
		default :
			break;
		}
		*bytes += sz;
		/* this code checks for contiguous segments */
		if (PINT_IS_CKSIZE(mode))
		{
			/* check size all we do is add up the sizes and count segs */
			gossip_debug(REQUEST_DEBUG,"\t\t\tcheck size request sz %lld\n",sz);
			(*segs)++;
		}
		else if (*segs > 0 &&
				offset_array[*segs] == offset_array[*segs-1] + size_array[*segs-1])
		{
			gossip_debug(REQUEST_DEBUG,"\t\t\tcombining contiguous segment\n");
			size_array[*segs-1] += sz;
		}
		else
		{
			size_array[*segs] = sz;
			(*segs)++;
		}
		/* this is used by client code */
		if (PINT_IS_CLIENT(mode) && *segs < segmax)
		{
			offset_array[*segs] = offset_array[*segs - 1] + sz;
		}
      /* prepare for next iteration */
      loff  += sz;
      size  -= loff - offset;
      offset = loff;
		/* find next logical offset on this server */
      loff = (*rfdata->dist->methods->next_mapped_offset)
				(rfdata->dist->params, rfdata->iod_num, rfdata->iod_count, offset);
		gossip_debug(REQUEST_DEBUG,"\t\tend iteration\n");
		/* see if we are finished */
		if (*bytes >= bytemax || (!PINT_IS_CKSIZE(mode) && (*segs >= segmax)))
		{
			gossip_debug(REQUEST_DEBUG,"\t\tdone with segments or bytes\n");
			break;
		}
   }
	gossip_debug(REQUEST_DEBUG,"\t\tfinished\n");
	gossip_debug(REQUEST_DEBUG,
			"\t\t\tof %lld sz %lld sg %d sm %d by %lld bm %lld\n",
			offset, size, *segs, segmax, *bytes, bytemax);
	/* find physical offset for this loff */
	gossip_debug(REQUEST_DEBUG,"\t\t\tnext loff: %lld ", loff);
   poff = (*rfdata->dist->methods->logical_to_physical_offset)
			(rfdata->dist->params, rfdata->iod_num, rfdata->iod_count, loff);
	gossip_debug(REQUEST_DEBUG,"next poff: %lld\n", poff);
	if (poff >= rfdata->fsize && !rfdata->extend_flag)
	{
		/* end of file - thus end of request */
		*eof_flag = 1;
		gossip_debug(REQUEST_DEBUG,"\t\t\t[return value] %lld (EOF)\n",
				orig_size);
		return orig_size;
	}
	if (loff >= orig_offset + orig_size)
	{
		gossip_debug(REQUEST_DEBUG,"\t\t\t(return value) %lld", orig_size);
		if (*eof_flag)
			gossip_debug(REQUEST_DEBUG," (EOF)\n");
		else
			gossip_debug(REQUEST_DEBUG,"\n");
		return orig_size;
	}
	else
	{
		gossip_debug(REQUEST_DEBUG,"\t\t\treturn value %lld",
				offset - orig_offset);
		if (*eof_flag)
			gossip_debug(REQUEST_DEBUG," (EOF)\n");
		else
			gossip_debug(REQUEST_DEBUG,"\n");
		return (offset - orig_offset);
	}
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
int PINT_Request_commit(PINT_Request *region, PINT_Request *node,
		int32_t *index)
{
	int32_t start_index = *index;
	int32_t child_index;

	/* Leaf Node? */
	if(node == NULL)
		return -1;

	/* this node was previously committed */
	if (node->committed)
	{
		return node->committed; /* should contain the index */
	}
  
	/* Copy node to contiguous region */
	memcpy(&region[*index], node, sizeof(struct PINT_Request));
	*index = *index + 1;

	/* Update ereq so that the relative positions are maintained */
	child_index = PINT_Request_commit(region, node->ereq, index);
	if (child_index == -1)
		region[start_index].ereq = NULL;
	else
		region[start_index].ereq = &region[child_index];

	/* prevents re-packing of the ereq */
	if (node->ereq)
		node->ereq->committed = child_index;

	/* Update sreq so that the relative positions are maintained */
	child_index = PINT_Request_commit(region, node->sreq, index);
	if (child_index == -1)
		region[start_index].sreq = NULL;
	else
		region[start_index].sreq = &region[child_index];

	/* restore committed value */
	if (node->ereq)
		node->ereq->committed = 0;

	/* Mark this node as committed */
	region[start_index].committed = 1;

	/* Return the index of the committed struct */ 
	return *index; 
}

/* This function converts pointers to array indexes for transport
 * The input Request MUST be committed
 */
int PINT_Request_encode(struct PINT_Request *req)
{
	int r;
	if (req->committed != 1)
		return -1;
	for (r = 0; r < req->num_nested_req; r++)
	{
		if (req[r].ereq)
			(int)(req[r].ereq) = req[r].ereq - &(req[0]);
		else
			(int)(req[r].ereq) = -1;
		if (req[r].sreq)
			(int)(req[r].sreq) = req[r].sreq - &(req[0]);
		else
			(int)(req[r].sreq) = -1;
	}
	return 0;
}

/* This function coverts array indexes back to pointers after transport
 * The input Request MUST be committed
 */
int PINT_Request_decode(struct PINT_Request *req)
{
	int r;
	if (req->committed != 1)
		return -1;
	for (r = 0; r < req->num_nested_req; r++)
	{
		if ((int)(req[r].ereq) == -1)
			req[r].ereq = NULL;
		else
			req[r].ereq = &(req[0]) + (int)(req[r].ereq);
		if ((int)(req[r].sreq) == -1)
			req[r].sreq = NULL;
		else
			req[r].sreq = &(req[0]) + (int)(req[r].sreq);
	}
	return 0;
}
    
