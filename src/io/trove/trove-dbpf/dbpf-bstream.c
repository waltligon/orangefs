/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <assert.h>
#include <errno.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-bstream.h"

#define DBPF_OPEN open
#define DBPF_WRITE write
#define DBPF_LSEEK lseek
#define DBPF_READ read
#define DBPF_CLOSE close
#define DBPF_SYNC fsync

#define AIOCB_ARRAY_SZ 8

/* Internal prototypes */
static inline int dbpf_bstream_rw_list(TROVE_coll_id coll_id,
				       TROVE_handle handle,
				       char **mem_offset_array, 
				       TROVE_size *mem_size_array,
				       int mem_count,
				       TROVE_offset *stream_offset_array, 
				       TROVE_size *stream_size_array,
				       int stream_count,
				       TROVE_size *out_size_p,
				       TROVE_ds_flags flags, 
				       TROVE_vtag_s *vtag,
				       void *user_ptr,
				       TROVE_context_id context_id,
				       TROVE_op_id *out_op_id_p,
				       int opcode);
static int dbpf_bstream_read_at_op_svc(struct dbpf_op *op_p);
static int dbpf_bstream_write_at_op_svc(struct dbpf_op *op_p);
static int dbpf_bstream_rw_list_op_svc(struct dbpf_op *op_p);
static int dbpf_bstream_flush_op_svc(struct dbpf_op *op_p);

/* Functions */

/* dbpf_bstream_read_at()
 */
static int dbpf_bstream_read_at(TROVE_coll_id coll_id,
				TROVE_handle handle,
				void *buffer,
				TROVE_size *inout_size_p,
				TROVE_offset offset,
				TROVE_ds_flags flags,
				TROVE_vtag_s *vtag, 
				void *user_ptr,
				TROVE_context_id context_id,
				TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;
    
    /* find the collection */
    /* TODO: how are we going to handle this?!?!? */
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -1;
    
    /* validate the handle, check permissions */
    
    /* Q: do we want to somehow lock the handle here so that it
     *    doesn't get removed while we're working on it?  To allow
     *    for atomic access?
     */
    
    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -1;
    
    /* initialize all the common members */
    dbpf_queued_op_init(
			q_op_p,
			BSTREAM_READ_AT,
			handle,
			coll_p,
			dbpf_bstream_read_at_op_svc,
			user_ptr,
			flags,
                        context_id);
    
    /* initialize the op-specific members */
    q_op_p->op.u.b_read_at.offset =  offset;
    q_op_p->op.u.b_read_at.size   = *inout_size_p;
    q_op_p->op.u.b_read_at.buffer =  buffer;
   
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    
    return 0;
}

/* dbpf_bstream_read_at_op_svc()
 *
 * Returns 1 on completion, -1 on error, 0 on not done.
 */
static int dbpf_bstream_read_at_op_svc(struct dbpf_op *op_p)
{
    int ret, fd, got_fd = 0;

    /* grab the FD (also increments a reference count) */
    /* TODO: CONSIDER PUTTING COLL_ID IN THE OP INSTEAD OF THE PTR */
    ret = dbpf_bstream_fdcache_try_get(op_p->coll_p->coll_id, op_p->handle, 0, &fd);
    switch (ret) {
	case DBPF_BSTREAM_FDCACHE_ERROR:
	    goto return_error;
	case DBPF_BSTREAM_FDCACHE_BUSY:
	    return 0;
	case DBPF_BSTREAM_FDCACHE_SUCCESS:
	    got_fd = 1;
	    /* drop through */
    }

    /* we have a dataspace now, maybe a new one. */
    
    ret = DBPF_LSEEK(fd, op_p->u.b_read_at.offset, SEEK_SET);
    if (ret < 0) goto return_error;
    
    ret = DBPF_READ(fd, op_p->u.b_read_at.buffer, op_p->u.b_read_at.size);
    if (ret < 0) goto return_error;
    
    /* sync if user requested it */
    if (op_p->flags & TROVE_SYNC) {
	if ((ret = DBPF_SYNC(fd)) != 0) {
	    goto return_error;
	}
    }

    dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
    
    /* TODO: any way to return partial success? */
    
    printf("read %d bytes.\n", ret);
    return 1;
   
 return_error:
    if (got_fd) dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
    return -1;
}

static int dbpf_bstream_write_at(TROVE_coll_id coll_id,
				 TROVE_handle handle,
				 void *buffer,
				 TROVE_size *inout_size_p,
				 TROVE_offset offset,
				 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_context_id context_id,
				 TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;
    
    /* find the collection */
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -1;
    
    /* validate the handle, check permissions?? */
    
    /* Q: do we want to somehow lock the handle here so that it
     *    doesn't get removed while we're working on it?  To allow
     *    for atomic access?
     */
    
    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -1;
    
    /* initialize all the common members */
    dbpf_queued_op_init(
			q_op_p,
			BSTREAM_WRITE_AT,
			handle,
			coll_p,
			dbpf_bstream_write_at_op_svc,
			user_ptr,
			flags,
                        context_id);
    
    /* initialize the op-specific members */
    q_op_p->op.u.b_write_at.offset =  offset;
    q_op_p->op.u.b_write_at.size   = *inout_size_p;
    q_op_p->op.u.b_write_at.buffer =  buffer;

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    
    return 0;
}

/* dbpf_bstream_write_at_op_svc()
 *
 */
static int dbpf_bstream_write_at_op_svc(struct dbpf_op *op_p)
{
    int ret, fd, got_fd = 0;

    /* grab the FD (also increments a reference count) */
    ret = dbpf_bstream_fdcache_try_get(op_p->coll_p->coll_id, op_p->handle, 1, &fd);
    switch (ret) {
	case DBPF_BSTREAM_FDCACHE_ERROR:
	    goto return_error;
	case DBPF_BSTREAM_FDCACHE_BUSY:
	    return 0;
	case DBPF_BSTREAM_FDCACHE_SUCCESS:
	    got_fd = 1;
	    /* drop through */
    }
    
    /* we have a dataspace now, maybe a new one. */
    
    ret = DBPF_LSEEK(fd, op_p->u.b_write_at.offset, SEEK_SET);
    if (ret < 0) goto return_error;
    
    ret = DBPF_WRITE(
		     fd,
		     op_p->u.b_write_at.buffer,
		     op_p->u.b_write_at.size);
    if (ret < 0) goto return_error;
    

    /* sync if user requested it */
    if (op_p->flags & TROVE_SYNC) {
	if ((ret = DBPF_SYNC(fd)) != 0) {
	    goto return_error;
	}
    }

    /* release the FD */
    dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
    
    /* TODO: any way to return partial success? */
    
    printf("wrote %d bytes.\n", ret);
    return 1;
    
 return_error:
    if (got_fd) dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
    return -1;
}

static int dbpf_bstream_flush(
				TROVE_coll_id coll_id,
				TROVE_handle handle,
				TROVE_ds_flags flags,
				void *user_ptr,
				TROVE_context_id context_id,
				TROVE_op_id *out_op_id_p)
{
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;
    
    /* find the collection */
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;
    
    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;
    
    /* initialize all the common members */
    dbpf_queued_op_init(
			q_op_p,
			BSTREAM_FLUSH,
			handle,
			coll_p,
			dbpf_bstream_flush_op_svc,
			user_ptr,
			flags,
                        context_id);
    
    /* initialize the op-specific members */
    /* there are none for flush */
   
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    
    return 0;
}
/* dbpf_bstream_flush_op_svc
 * returns 1 on completion, -1 on error, 0 on not done
 */
static int dbpf_bstream_flush_op_svc(struct dbpf_op *op_p)
{
    int ret, error, fd, got_fd = 0;

    /* grab the FD (also increments a reference count) */
    /* TODO: CONSIDER PUTTING COLL_ID IN THE OP INSTEAD OF THE PTR */
    ret = dbpf_bstream_fdcache_try_get(op_p->coll_p->coll_id, op_p->handle, 0, &fd);
    switch (ret) {
	case DBPF_BSTREAM_FDCACHE_ERROR:
	    error = -1;
	    goto return_error;
	case DBPF_BSTREAM_FDCACHE_BUSY:
	    return 0;
	case DBPF_BSTREAM_FDCACHE_SUCCESS:
	    got_fd = 1;
	    /* drop through */
    }

    ret = DBPF_SYNC(fd);
    if ( ret != 0) {
	error = -trove_errno_to_trove_error(errno);
	goto return_error;
    }
    dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
    return 1;

return_error:
    if (got_fd) dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
    return error;
}

/* dbpf_bstream_resize()
 */
static int dbpf_bstream_resize(
			       TROVE_coll_id coll_id,
			       TROVE_handle handle,
			       TROVE_size *inout_size_p,
			       TROVE_ds_flags flags,
			       TROVE_vtag_s *vtag,
			       void *user_ptr,
			       TROVE_context_id context_id,
			       TROVE_op_id *out_op_id_p)
{
    return -TROVE_ENOSYS;
}

/* dbpf_bstream_validate()
 */
static int dbpf_bstream_validate(
				 TROVE_coll_id coll_id,
				 TROVE_handle handle,
			       	 TROVE_ds_flags flags,
				 TROVE_vtag_s *vtag,
				 void *user_ptr,
				 TROVE_context_id context_id,
				 TROVE_op_id *out_op_id_p)
{
    return -TROVE_ENOSYS;
}


/* dbpf_bstream_read_list()
 */
static int dbpf_bstream_read_list(TROVE_coll_id coll_id,
				  TROVE_handle handle,
				  char **mem_offset_array, 
				  TROVE_size *mem_size_array,
				  int mem_count,
				  TROVE_offset *stream_offset_array, 
				  TROVE_size *stream_size_array,
				  int stream_count,
				  TROVE_size *out_size_p,
				  TROVE_ds_flags flags, 
				  TROVE_vtag_s *vtag,
				  void *user_ptr,
				  TROVE_context_id context_id,
				  TROVE_op_id *out_op_id_p)
{
    return dbpf_bstream_rw_list(coll_id,
				handle,
				mem_offset_array, 
				mem_size_array,
				mem_count,
				stream_offset_array, 
				stream_size_array,
				stream_count,
				out_size_p,
				flags, 
				vtag,
				user_ptr,
				context_id,
				out_op_id_p,
				LIO_READ);
}

/* dbpf_bstream_write_list()
 */
static int dbpf_bstream_write_list(TROVE_coll_id coll_id,
				   TROVE_handle handle,
				   char **mem_offset_array, 
				   TROVE_size *mem_size_array,
				   int mem_count,
				   TROVE_offset *stream_offset_array, 
				   TROVE_size *stream_size_array,
				   int stream_count,
				   TROVE_size *out_size_p,
				   TROVE_ds_flags flags, 
				   TROVE_vtag_s *vtag,
				   void *user_ptr,
				   TROVE_context_id context_id,
				   TROVE_op_id *out_op_id_p)
{
    return dbpf_bstream_rw_list(coll_id,
				handle,
				mem_offset_array, 
				mem_size_array,
				mem_count,
				stream_offset_array, 
				stream_size_array,
				stream_count,
				out_size_p,
				flags, 
				vtag,
				user_ptr,
				context_id,
				out_op_id_p,
				LIO_WRITE);
}

/* dbpf_bstream_rw_list()
 *
 * Handles queueing of both read and write list operations
 *
 * opcode parameter should be LIO_READ or LIO_WRITE
 */
static inline int dbpf_bstream_rw_list(TROVE_coll_id coll_id,
				       TROVE_handle handle,
				       char **mem_offset_array, 
				       TROVE_size *mem_size_array,
				       int mem_count,
				       TROVE_offset *stream_offset_array, 
				       TROVE_size *stream_size_array,
				       int stream_count,
				       TROVE_size *out_size_p,
				       TROVE_ds_flags flags, 
				       TROVE_vtag_s *vtag,
				       void *user_ptr,
				       TROVE_context_id context_id,
				       TROVE_op_id *out_op_id_p,
				       int opcode)
{
    int ret, fd;
    struct dbpf_queued_op *q_op_p;
    struct dbpf_collection *coll_p;

    /* find the collection */
    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL) return -TROVE_EINVAL;

    /* validate the handle, check permissions */

    /* grab a queued op structure */
    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL) return -TROVE_ENOMEM;

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
			BSTREAM_WRITE_LIST,
			handle,
			coll_p,
			dbpf_bstream_rw_list_op_svc,
			user_ptr,
			flags,
                        context_id);

    /* initialize op-specific members */
    q_op_p->op.u.b_rw_list.fd                  = -1;
    q_op_p->op.u.b_rw_list.opcode              = opcode;
    q_op_p->op.u.b_rw_list.mem_array_count     = mem_count;
    q_op_p->op.u.b_rw_list.mem_offset_array    = mem_offset_array;
    q_op_p->op.u.b_rw_list.mem_size_array      = mem_size_array;
    q_op_p->op.u.b_rw_list.stream_array_count  = stream_count;
    q_op_p->op.u.b_rw_list.stream_offset_array = stream_offset_array;
    q_op_p->op.u.b_rw_list.stream_size_array   = stream_size_array;
    q_op_p->op.u.b_rw_list.aiocb_array_count   = 0;
    q_op_p->op.u.b_rw_list.aiocb_array         = NULL;

    /* initialize list processing state (more op-specific members) */
    q_op_p->op.u.b_rw_list.lio_state.mem_ct          = 0;
    q_op_p->op.u.b_rw_list.lio_state.stream_ct       = 0;
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_size    = mem_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_mem_off     = mem_offset_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_size = stream_size_array[0];
    q_op_p->op.u.b_rw_list.lio_state.cur_stream_off  = stream_offset_array[0];

    q_op_p->op.u.b_rw_list.list_proc_state = LIST_PROC_INITIALIZED;

    /* get an FD so we can access the file; last parameter controls file creation */
    /* TODO: MOVE THIS INTO THE OP_SVC FUNCTION AS ANOTHER STATE */
    ret = dbpf_bstream_fdcache_try_get(coll_id, handle, (opcode == LIO_WRITE) ? 1 : 0, &fd);
    if (ret < 0) {
	dbpf_queued_op_free(q_op_p);
	gossip_ldebug(SERVER_DEBUG, "warning: useless error value\n");
	return -1;
    }
    q_op_p->op.u.b_rw_list.fd = fd;

    /* queue op*/
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}


/* dbpf_bstream_rw_list_op_svc()
 *
 * This function is used to service both read_list and write_list operations.
 * State maintained in the struct dbpf_op (pointed to by op_p) keeps up with
 * which type of operation this is via the "opcode" field in the b_rw_list member.
 *
 * Assumptions:
 * - FD has been retrieved from fdcache, is refct'd so it won't go away
 * - lio_state in the op is valid
 * - opcode is set to LIO_READ or LIO_WRITE (corresponding to a read_list or
 *   write_list, respectively)
 *
 * This function is responsible for alloating and deallocating the space reserved
 * for the aiocb array.
 *
 * Outline:
 * - look to see if we have an aiocb array
 *   - if we don't, allocate one
 *   - if we do, then check on progress of each operation (having array implies that
 *     we have put some things in service)
 *     - if we got an error, ???
 *     - if op is finished, mark w/NOP
 *
 * - look to see if there are unfinished but posted operations
 *   - if there are, return 0
 *   - if not, and we are in the LIST_PROC_ALLPOSTED state, then we're done!
 *   - otherwise convert some more elements and post them.
 * 
 */
static int dbpf_bstream_rw_list_op_svc(struct dbpf_op *op_p)
{
    int ret, i, aiocb_inuse_count, op_in_progress_count = 0;
    struct aiocb *aiocb_p, *aiocb_ptr_array[AIOCB_ARRAY_SZ];

#if 0
    printf("dbpf_bstream_rw_list_op_svc() entered.\n");
#endif

    /* allocate space for aiocb array if necessary */
    /* TODO: watch memory allocation, delay this operation if no memory avail. */
    if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_INITIALIZED) {
	/* first call; need to allocate aiocb array and ptr array */
	aiocb_p = (struct aiocb *) malloc(AIOCB_ARRAY_SZ*sizeof(struct aiocb));

	if (aiocb_p == NULL) {
	    return -TROVE_ENOMEM;
	}

	/* initialize, paying particular attention to set the opcode to NOP.
	 *
	 * Also, it appears to be important to do all this SIGEV_NONE stuff,
	 * although some man pages seem to indicate that it is not.
	 */
	memset(aiocb_p, 0, AIOCB_ARRAY_SZ*sizeof(struct aiocb));
	for (i=0; i < AIOCB_ARRAY_SZ; i++) {
	    aiocb_p[i].aio_lio_opcode            = LIO_NOP;
	    aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	}

	op_p->u.b_rw_list.aiocb_array_count  = AIOCB_ARRAY_SZ;
	op_p->u.b_rw_list.aiocb_array        = aiocb_p;
	op_p->u.b_rw_list.list_proc_state    = LIST_PROC_INPROGRESS;
	op_p->u.b_rw_list.sigev.sigev_notify = SIGEV_NONE;
    }
    else {
	/* operations potentially in progress */
	aiocb_p = op_p->u.b_rw_list.aiocb_array;

	/* check to see how we're progressing on previous operations */
	for (i=0; i < op_p->u.b_rw_list.aiocb_array_count; i++) {
	    if (aiocb_p[i].aio_lio_opcode == LIO_NOP) continue;

	    ret = aio_error(&aiocb_p[i]); /* gets the "errno" value of the individual op */
	    if (ret == 0) {
		/* this particular operation completed w/out error */

		ret = aio_return(&aiocb_p[i]); /* gets the return value of the individual op */
		printf("   aio_return() says %d\n", ret);
		/* WHAT DO WE DO WITH PARTIAL READ/WRITES??? */

		/* mark as a NOP so we ignore it from now on */
		aiocb_p[i].aio_lio_opcode = LIO_NOP;
	    }
	    else if (ret != EINPROGRESS) {
		/* we got an error of some sort */
		printf("error %d (%s) from aio_error/aio_return on block %d; skipping\n", ret, strerror(ret), i);
		aiocb_p[i].aio_lio_opcode = LIO_NOP;
	    }
	    else {
		/* otherwise the operation is still in progress; skip it */
		op_in_progress_count++;
	    }
	}
    }

    /* if we're not done with the last set of operations, break out */
    if (op_in_progress_count > 0) return 0;
    else if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLPOSTED) {
	/* we've posted everything, and it all completed, so we're done. */

	/* free the aiocb array, release the FD, and mark the whole op as complete */
	free(aiocb_p);

	/* TODO: HOW DO WE DO A SYNC IN HERE?  WE DON'T HAVE THE FD */

	dbpf_bstream_fdcache_put(op_p->coll_p->coll_id, op_p->handle);
	return 1;
    }
    else {
	/* no operations currently in progress; convert and post some more */

	/* convert listio arguments into aiocb structures */
	aiocb_inuse_count = op_p->u.b_rw_list.aiocb_array_count;
	ret = dbpf_bstream_listio_convert(op_p->u.b_rw_list.fd,
					  op_p->u.b_rw_list.opcode,
					  op_p->u.b_rw_list.mem_offset_array,
					  op_p->u.b_rw_list.mem_size_array,
					  op_p->u.b_rw_list.mem_array_count,
					  op_p->u.b_rw_list.stream_offset_array,
					  op_p->u.b_rw_list.stream_size_array,
					  op_p->u.b_rw_list.stream_array_count,
					  aiocb_p,
					  &aiocb_inuse_count,
					  &op_p->u.b_rw_list.lio_state);
	
	/* TODO: REMOVE THE ALLCONVERTED STATE ENTIRELY?  IS IT NEEDED? */
	if (ret == 1) op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLCONVERTED;
	
	/* if we didn't use the entire array, mark the unused ones with LIO_NOPs */
	for (i=aiocb_inuse_count; i < op_p->u.b_rw_list.aiocb_array_count; i++) {
	    /* for simplicity just mark these as NOPs and we'll ignore them */
	    aiocb_p[i].aio_lio_opcode = LIO_NOP;
	}

	for (i=0; i < aiocb_inuse_count; i++) {
	    aiocb_ptr_array[i] = &aiocb_p[i];
	}

	printf("calling lio_listio w/count of %d\n", aiocb_inuse_count);
	fflush(stdout);

	ret = lio_listio(LIO_NOWAIT, aiocb_ptr_array, aiocb_inuse_count, &op_p->u.b_rw_list.sigev);
	if (ret != 0) {
	    gossip_lerr("lio_listio() returned %d\n", ret);
	    return -trove_errno_to_trove_error(errno);
	}

	/* TODO: JUST SET TO ALLPOSTED UP ABOVE? */
	if (op_p->u.b_rw_list.list_proc_state == LIST_PROC_ALLCONVERTED)
	    op_p->u.b_rw_list.list_proc_state = LIST_PROC_ALLPOSTED;

	return 0;
    }
}

struct TROVE_bstream_ops dbpf_bstream_ops =
{
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    dbpf_bstream_read_list,
    dbpf_bstream_write_list,
    dbpf_bstream_flush
};


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
