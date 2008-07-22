/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op.h"
#include "dbpf-op-queue.h"
#include "dbpf-attr-cache.h"
#include "dbpf-bstream.h"
#include "pint-mem.h"

typedef struct
{
    char *buffer;
    TROVE_size size;
    TROVE_offset offset;
} dbpf_stream_extents_t;

static int dbpf_bstream_get_extents(
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    int *ext_count,
    dbpf_stream_extents_t *extents);

static size_t direct_aligned_write(int fd, 
                                    void *buf, 
                                    off_t buf_offset,
                                    size_t size, 
                                    off_t write_offset,
                                    size_t stream_size);

static size_t direct_write(int fd,
                            void * buf,
                            off_t buf_offset,
                            size_t size,
                            off_t write_offset,
                            size_t stream_size);

static size_t direct_aligned_read(int fd,
                                   void *buf,
                                   off_t buf_offset,
                                   size_t size,
                                   off_t file_offset,
                                   size_t stream_size);

static size_t direct_read(int fd, 
                           void * buf, 
                           off_t buf_offset,
                           size_t size,
                           off_t file_offset, 
                           size_t stream_size);

#define BLOCK_SIZE 512

/* compute the mask of 1s that allows us to essentially throw away
 * all bits less than the block size.
 */
#define BLOCK_MULTIPLES_MASK (~((uintptr_t) BLOCK_SIZE - 1))

/* calculate the max offset that is a multiple of the block size but still
 * less than or equal to requested offset passed in
 */
#define ALIGNED_OFFSET(__offset) (__offset & BLOCK_MULTIPLES_MASK)

/* calculate the minimum size that is a multiple of the block size and
 * still greater than or equal to the requested size
 */
#define ALIGNED_SIZE(__offset, __size) \
    (((__offset + __size + BLOCK_SIZE - 1) \
      & BLOCK_MULTIPLES_MASK) - ALIGNED_OFFSET(__offset))

#define IS_ALIGNED_PTR(__ptr) \
    ((((uintptr_t)__ptr) & BLOCK_MULTIPLES_MASK) == (uintptr_t)__ptr)

/**
 * Perform an write in direct mode (no buffering).
 *
 * @param fd - The file descriptor of the bstream to do the write on.  THe
 * file descriptor is required to opened with O_DIRECT.  In debug mode,
 * the O_DIRECT option is checked.
 *
 * @param buf - the buffer containing the bytes to write to the bstream.  The
 * buffer is required to be allocated with the correct alignment (to a block
 * size of 512)
 *
 * @param buf_offset - the offset into the buffer that the write should start
 *
 * @param size - the size of bytes to write from the buffer to
 * the file.
 *
 * @param write_offset - the offset into the bstream to start the write
 *
 * @param stream_size - the actual size of the bstream (might be stored elsewhere)
 *
 * @returns bytes written, otherwise a negative errno error code
 */
static size_t direct_aligned_write(int fd, 
                                    void *buf, 
                                    off_t buf_offset,
                                    size_t size, 
                                    off_t write_offset,
                                    size_t stream_size)
{
    int ret;

#ifndef NDEBUG
    /* if debug is enabled, check that fd was opened with O_DIRECT */

    if(!(fcntl(fd, F_GETFL) & O_DIRECT))
    {
        return -EINVAL;
    }
#endif

    /* verify that the buffer is aligned properly */
    assert(IS_ALIGNED_PTR(buf));

    /* verify that the offset is aligned as well */
    assert(ALIGNED_OFFSET(buf_offset) == buf_offset);

    /* and the size */
    assert(ALIGNED_SIZE(write_offset, size) == size);

    /* and the offset into the file */
    assert(ALIGNED_OFFSET(write_offset) == write_offset);

    ret = dbpf_pwrite(fd, (((char *)buf) + buf_offset), size, write_offset);
    if(ret < 0)
    {
        gossip_err(
            "dbpf_direct_write: failed to perform aligned write\n");
        return -errno;
    }

    return ret;
}

static size_t direct_write(int fd,
                            void * buf,
                            off_t buf_offset,
                            size_t size,
                            off_t write_offset,
                            size_t stream_size)
{
    size_t ret;
    void * aligned_buf;
    size_t aligned_size;
    off_t aligned_offset, end_offset, aligned_end_offset;

    aligned_size = ALIGNED_SIZE(write_offset, size);
    aligned_offset = ALIGNED_OFFSET(write_offset);

    /* if the buffer passed in, the offsets, and the size are all
     * aligned properly, just pass through directly
     */
    if(IS_ALIGNED_PTR(buf) && 
       ALIGNED_OFFSET(buf_offset) == buf_offset &&
       aligned_size == size)
    {
        return direct_aligned_write(fd, buf, buf_offset, 
                                     size, write_offset, stream_size);
    }

    gossip_debug(GOSSIP_DIRECTIO_DEBUG, 
                 "requested write is not aligned, doing memcpy:\n\t"
                 "buf: %p, "
                 "buf_offset: %llu, "
                 "size: %zu, \n\t"
                 "write_offset: %llu, "
                 "stream_size: %zu\n",
                 buf, 
                 llu(buf_offset), 
                 size,
                 llu(write_offset),
                 stream_size);

    aligned_buf = PINT_mem_aligned_alloc(aligned_size, BLOCK_SIZE);
    if(!aligned_buf)
    {
        return -ENOMEM;
    }

    /* Do read-modify-write on the ends of the buffer if
     * the offsets and sizes aren't aligned properly
     */
    if(aligned_offset < write_offset)
    {
        ret = 0;
        if(ALIGNED_SIZE(0, stream_size) > aligned_offset)
        {
            /* read the first block */
            ret = dbpf_pread(fd, aligned_buf, BLOCK_SIZE, aligned_offset);
            if(ret < 0)
            {
                gossip_err(
                    "direct_memcpy_write: RMW failed at "
                    "beginning of request\n");
                PINT_mem_aligned_free(aligned_buf);
                return -errno;
            }
        }
        else
        {
            memset(aligned_buf, 0, BLOCK_SIZE);
        }
    }

    end_offset = write_offset + size;
    aligned_end_offset = aligned_offset + aligned_size;

    if(aligned_end_offset > end_offset)
    {
        ret = 0;
        if(ALIGNED_SIZE(0, stream_size) >= aligned_end_offset)
        {
            ret = dbpf_pread(fd,
                            ((char *)aligned_buf) + aligned_size - BLOCK_SIZE,
                            BLOCK_SIZE,
                            aligned_end_offset - BLOCK_SIZE);
            if(ret < 0)
            {
                gossip_err(
                    "direct_memcpy_write: RMW failed at end of request\n");
                PINT_mem_aligned_free(aligned_buf);
                return -errno;
            }
        }
        else
        {
            memset(((char *)aligned_buf) + aligned_size - BLOCK_SIZE, 
                   0, BLOCK_SIZE);
        }
    }

    /* now we're read to memcpy the actual (unaligned) request into the
     * aligned buffer
     */
    memcpy(((char *)aligned_buf) + (write_offset - aligned_offset),
           ((char *)buf) + buf_offset, size);

    ret = direct_aligned_write(fd, aligned_buf, 0, 
                                aligned_size, aligned_offset, stream_size);

    PINT_mem_aligned_free(aligned_buf);

    return size;
}

/**
 * Perform a read in direct mode (no buffering).
 *
 * @param fd - The file descriptor of the bstream to do the read from.  The
 * file descriptor is required to be opened with O_DIRECT.  In debug mode,
 * the O_DIRECT option is checked, and if it doesn't exist on the open file
 * descriptor, EINVAL is returned.
 *
 * @param buf - The buffer to read data into.  This function assumes that
 * the buffer has been allocated with the correct alignment (i.e. to a block
 * size of 512, using posix_memalign or such).
 *
 * @param buf_offset - The offset into the buffer that data is read.
 *
 * @param buf_size - The available size of the buffer
 *
 * @param file_offset - offset into the file to start the read
 *
 * @param request_size - number of bytes to read from the file
 *
 * @param stream_size - size of the file
 *
 * @return number of bytes read
 */
static size_t direct_aligned_read(int fd,
                                   void * buf,
                                   off_t buf_offset,
                                   size_t size,
                                   off_t file_offset,
                                   size_t stream_size)
{
    int ret;

    if(file_offset >= stream_size)
    {
        /* the offset is past EOF, return 0 bytes read */
        return 0;
    }

#ifndef NDEBUG
    /* if debug is enabled, check that fd was opened with O_DIRECT */

    if(!(fcntl(fd, F_GETFL) & O_DIRECT))
    {
        gossip_err("dbpf_direct_read: trying to do direct IO but file wasn't "
                   "opened with O_DIRECT\n");
        return -EINVAL;
    }
#endif

    /* verify that stuff is aligned properly */
    assert(IS_ALIGNED_PTR(buf));
    assert(ALIGNED_OFFSET(buf_offset) == buf_offset);
    assert(ALIGNED_SIZE(file_offset, size) == size);
    assert(ALIGNED_OFFSET(file_offset) == file_offset);

    ret = dbpf_pread(fd, (((char *)buf) + buf_offset), size, file_offset);
    if(ret < 0)
    {
        gossip_err("dbpf_direct_read: failed to perform aligned read\n");
        return -errno;
    }

    return ret;
}

static size_t direct_read(int fd,
                           void * buf,
                           off_t buf_offset,
                           size_t size,
                           off_t file_offset,
                           size_t stream_size)
{
    void * aligned_buf;
    off_t aligned_offset;
    size_t aligned_size, read_size;
    size_t ret;

    if(file_offset > stream_size)
    {
        return 0;
    }

    read_size = size;
    if(stream_size < (file_offset + size))
    {
        read_size = stream_size - file_offset;
    }

    aligned_offset = ALIGNED_OFFSET(file_offset);
    aligned_size = ALIGNED_SIZE(file_offset, read_size);

    if(IS_ALIGNED_PTR(buf) &&
       ALIGNED_OFFSET(buf_offset) == buf_offset &&
       aligned_size == read_size)
    {
        return direct_aligned_read(fd, buf, buf_offset, read_size, 
                                    file_offset, stream_size);
    }

    aligned_buf = PINT_mem_aligned_alloc(aligned_size, BLOCK_SIZE);
    if(!aligned_buf)
    {
        return -ENOMEM;
    }

    ret = direct_aligned_read(fd, aligned_buf, 0, aligned_size, 
                               aligned_offset, stream_size);
    if(ret < 0)
    {
        PINT_mem_aligned_free(aligned_buf);
        return ret;
    }

    memcpy(((char *)buf) + buf_offset,
           ((char *)aligned_buf) + (file_offset - aligned_offset),
           read_size);

    PINT_mem_aligned_free(aligned_buf);

    return read_size;
}

static int dbpf_bstream_direct_read_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    TROVE_object_ref ref;
    TROVE_ds_attributes attr;
    dbpf_queued_op_t *q_op_p;
    struct dbpf_bstream_rw_list_op *rw_op;
    dbpf_stream_extents_t *stream_extents;
    int i, extent_count;

    q_op_p = (dbpf_queued_op_t *)op_p->u.b_rw_list.queued_op_ptr;
    rw_op = &op_p->u.b_rw_list;

    ref.fs_id = op_p->coll_p->coll_id;
    ref.handle = op_p->handle;

    /* not in attribute cache.  get the size from dspace */
    ret = dbpf_dspace_attr_get(op_p->coll_p, ref, &attr);
    if(ret != 0)
    {
        return ret;
    }

    ret = dbpf_bstream_get_extents(
        rw_op->mem_offset_array,
        rw_op->mem_size_array,
        rw_op->mem_array_count,
        rw_op->stream_offset_array,
        rw_op->stream_size_array,
        rw_op->stream_array_count,
        &extent_count,
        NULL);
    if(ret != 0)
    {
        return ret;
    }

    stream_extents = malloc(sizeof(*stream_extents) * extent_count);
    if(!stream_extents)
    {
        return -TROVE_ENOMEM;
    }

    ret = dbpf_bstream_get_extents(
        rw_op->mem_offset_array,
        rw_op->mem_size_array,
        rw_op->mem_array_count,
        rw_op->stream_offset_array,
        rw_op->stream_size_array,
        rw_op->stream_array_count,
        &extent_count,
        stream_extents);
    if(ret != 0)
    {
        free(stream_extents);
        return ret;
    }

    for(i = 0; i < extent_count; ++ i)
    {
        ret = direct_read(rw_op->open_ref.fd,
                          stream_extents[i].buffer,
                          0,
                          stream_extents[i].size,
                          stream_extents[i].offset,
                          attr.u.datafile.b_size);
        if(ret < 0)
        {
            free(stream_extents);
            return -trove_errno_to_trove_error(-ret);
        }
    }
    return DBPF_OP_COMPLETE;
}

static int dbpf_bstream_direct_write_op_svc(struct dbpf_op *op_p)
{
    int ret = -TROVE_EINVAL;
    TROVE_object_ref ref;
    TROVE_ds_attributes attr;
    dbpf_stream_extents_t *stream_extents;
    int i, extent_count;
    dbpf_queued_op_t *q_op_p;
    struct dbpf_bstream_rw_list_op *rw_op;
    int eor = -1;

    q_op_p = (dbpf_queued_op_t *)(op_p->u.b_rw_list.queued_op_ptr);
    rw_op = &op_p->u.b_rw_list;

    ref.fs_id = op_p->coll_p->coll_id;
    ref.handle = op_p->handle;

    ret = dbpf_dspace_attr_get(op_p->coll_p, ref, &attr);
    if(ret != 0)
    {
        return ret;
    }

    ret = dbpf_bstream_get_extents(
        rw_op->mem_offset_array,
        rw_op->mem_size_array,
        rw_op->mem_array_count,
        rw_op->stream_offset_array,
        rw_op->stream_size_array,
        rw_op->stream_array_count,
        &extent_count,
        NULL);
    if(ret != 0)
    {
        return ret;
    }

    stream_extents = malloc(sizeof(*stream_extents) * extent_count);
    if(!stream_extents)
    {
        return -TROVE_ENOMEM;
    }

    ret = dbpf_bstream_get_extents(
        rw_op->mem_offset_array,
        rw_op->mem_size_array,
        rw_op->mem_array_count,
        rw_op->stream_offset_array,
        rw_op->stream_size_array,
        rw_op->stream_array_count,
        &extent_count,
        stream_extents);
    if(ret != 0)
    {
        free(stream_extents);
        return ret;
    }

    *rw_op->out_size_p = 0;
    for(i = 0; i < extent_count; ++ i)
    {
        ret = direct_write(rw_op->open_ref.fd,
                           stream_extents[i].buffer,
                           0,
                           stream_extents[i].size,
                           stream_extents[i].offset,
                           attr.u.datafile.b_size);
        if(ret < 0)
        {
            return -trove_errno_to_trove_error(-ret);
        }

        if(eor < stream_extents[i].offset + stream_extents[i].size)
        {
            eor = stream_extents[i].offset + stream_extents[i].size;
        }

        *rw_op->out_size_p += ret;
    }

    if(eor > attr.u.datafile.b_size)
    {
        /* set the size of the file */
        attr.u.datafile.b_size = eor;
        /* We want to hit the coalesce path, so we queue up the setattr */
        dbpf_queued_op_init(q_op_p,
                            DSPACE_SETATTR,
                            ref.handle,
                            op_p->coll_p,
                            dbpf_dspace_setattr_op_svc,
                            q_op_p->op.user_ptr,
                            TROVE_SYNC,
                            q_op_p->op.context_id);
        op_p->u.d_setattr.attr_p = malloc(sizeof(*op_p->u.d_setattr.attr_p));
        if(!op_p->u.d_setattr.attr_p)
        {
            dbpf_queued_op_free(q_op_p);
            return -TROVE_ENOMEM;
        }
        *op_p->u.d_setattr.attr_p = attr;

        return DBPF_OP_CONTINUE;
    }

    return DBPF_OP_COMPLETE;
}

static int dbpf_bstream_direct_read_at(TROVE_coll_id coll_id,
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
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_direct_write_at(TROVE_coll_id coll_id,
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
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_direct_read_list(TROVE_coll_id coll_id,
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

    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_bstream_rw_list_op *op;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        BSTREAM_READ_LIST,
                        handle,
                        coll_p,
                        dbpf_bstream_direct_read_op_svc,
                        user_ptr,
                        flags,
                        context_id);
    op = (struct dbpf_bstream_rw_list_op *)&q_op_p->op.u.b_rw_list;

    /* initialize the op-specific members */
    op->stream_array_count = stream_count;
    op->stream_offset_array = stream_offset_array;
    op->stream_size_array = stream_size_array;
    op->out_size_p = out_size_p;

    op->mem_array_count = mem_count;
    op->mem_offset_array = mem_offset_array;
    op->mem_size_array = mem_size_array;
    op->queued_op_ptr = q_op_p;

    ret = dbpf_open_cache_get(
        coll_id, handle,
        DBPF_FD_DIRECT_READ,
        &op->open_ref);
    if(ret < 0)
    {
        if(ret == -TROVE_ENOENT)
        {
            /* We create the bstream lazily, so here we'll just assume the read
             * was done before writes to this bstream occured, and return
             * a successful read of size 0.
             */
            *out_size_p = 0;
            ret = DBPF_OP_COMPLETE;
        }
        dbpf_queued_op_free(q_op_p);
        return ret;
    }

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return DBPF_OP_CONTINUE;
}

static int dbpf_bstream_direct_write_list(TROVE_coll_id coll_id,
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

    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_bstream_rw_list_op *op;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        BSTREAM_WRITE_LIST,
                        handle,
                        coll_p,
                        dbpf_bstream_direct_write_op_svc,
                        user_ptr,
                        flags,
                        context_id);
    op = (struct dbpf_bstream_rw_list_op *)(&q_op_p->op.u.b_rw_list);

    /* initialize the op-specific members */
    op->stream_array_count = stream_count;
    op->stream_offset_array = stream_offset_array;
    op->stream_size_array = stream_size_array;
    op->out_size_p = out_size_p;

    op->mem_array_count = mem_count;
    op->mem_offset_array = mem_offset_array;
    op->mem_size_array = mem_size_array;
    op->queued_op_ptr = q_op_p;

    ret = dbpf_open_cache_get(
        coll_id, handle,
        DBPF_FD_DIRECT_WRITE,
        &op->open_ref);
    if(ret < 0)
    {
        dbpf_queued_op_free(q_op_p);
        return ret;
    }

    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return DBPF_OP_CONTINUE;
}

static int dbpf_bstream_direct_resize_op_svc(struct dbpf_op *op_p)
{
    int ret;
    TROVE_ds_attributes attr;
    TROVE_object_ref ref;
    dbpf_queued_op_t *q_op_p;

    q_op_p = (dbpf_queued_op_t *)op_p->u.b_resize.queued_op_ptr;
    ref.fs_id = op_p->coll_p->coll_id;
    ref.handle = op_p->handle;

    ret = dbpf_dspace_attr_get(op_p->coll_p, ref, &attr);
    if(ret != 0)
    {
        return ret;
    }

    ret = ftruncate(op_p->u.b_resize.open_ref.fd, op_p->u.b_resize.size);
    if(ret == -1)
    {
        return -trove_errno_to_trove_error(errno);
    }

    attr.u.datafile.b_size = op_p->u.b_resize.size;

    dbpf_queued_op_init(q_op_p,
                        DSPACE_SETATTR,
                        ref.handle,
                        op_p->coll_p,
                        dbpf_dspace_setattr_op_svc,
                        q_op_p->op.user_ptr,
                        TROVE_SYNC,
                        q_op_p->op.context_id);
    op_p->u.d_setattr.attr_p = malloc(sizeof(*op_p->u.d_setattr.attr_p));
    if(!op_p->u.d_setattr.attr_p)
    {
        dbpf_queued_op_free(q_op_p);
        return -TROVE_ENOMEM;
    }
    *op_p->u.d_setattr.attr_p = attr;

    /* we don't have to add the op back to the queue, the dbpf_thread does
     * that for us when we return 0 instead of 1
     */
    return DBPF_OP_CONTINUE;
}

static int dbpf_bstream_direct_resize(TROVE_coll_id coll_id,
                                      TROVE_handle handle,
                                      TROVE_size *inout_size_p,
                                      TROVE_ds_flags flags,
                                      TROVE_vtag_s *vtag,
                                      void *user_ptr,
                                      TROVE_context_id context_id,
                                      TROVE_op_id *out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        BSTREAM_RESIZE,
                        handle,
                        coll_p,
                        dbpf_bstream_direct_resize_op_svc,
                        user_ptr,
                        flags,
                        context_id);

    /* initialize the op-specific members */
    ret = dbpf_open_cache_get(
        coll_id, handle,
        DBPF_FD_DIRECT_WRITE,
        &q_op_p->op.u.b_resize.open_ref);
    if(ret < 0)
    {
        dbpf_queued_op_free(q_op_p);
        return ret;
    }

    q_op_p->op.u.b_resize.size = *inout_size_p;
    q_op_p->op.u.b_resize.queued_op_ptr = q_op_p;
    *out_op_id_p = dbpf_queued_op_queue(q_op_p);

    return 0;
}

static int dbpf_bstream_direct_validate(TROVE_coll_id coll_id,
                                        TROVE_handle handle,
                                        TROVE_ds_flags flags,
                                        TROVE_vtag_s *vtag,
                                        void *user_ptr,
                                        TROVE_context_id context_id,
                                        TROVE_op_id *out_op_id_p)
{
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_direct_flush(TROVE_coll_id coll_id,
                                     TROVE_handle handle,
                                     TROVE_ds_flags flags,
                                     void *user_ptr,
                                     TROVE_context_id context_id,
                                     TROVE_op_id *out_op_id_p)
{
    return DBPF_OP_COMPLETE;
}

struct TROVE_bstream_ops dbpf_bstream_direct_ops =
{
    dbpf_bstream_direct_read_at,
    dbpf_bstream_direct_write_at,
    dbpf_bstream_direct_resize,
    dbpf_bstream_direct_validate,
    dbpf_bstream_direct_read_list,
    dbpf_bstream_direct_write_list,
    dbpf_bstream_direct_flush
};

static int dbpf_bstream_get_extents(
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    int *ext_count,
    dbpf_stream_extents_t *extents)
{
    int mct = 0, sct = 0, act = 0;
    int oom = 0, oos = 0;
    TROVE_size cur_mem_size = 0;
    char *cur_mem_off = NULL;
    char *ext_ptr = NULL;
    TROVE_size ext_size = 0, cur_stream_size = 0;
    TROVE_offset ext_off = 0, cur_stream_off = 0;

    cur_mem_size = mem_size_array[mct];
    cur_mem_off = mem_offset_array[mct];

    cur_stream_size = stream_size_array[sct];
    cur_stream_off = stream_offset_array[sct];

    while (1)
    {
        /*
          determine if we're either out of memory (oom) regions, or
          out of stream (oos) regions
        */
        /* in many (all?) cases mem_count is 1, so oom will end up being 1 */
        oom = (((mct + 1) < mem_count) ? 0 : 1);
        oos = (((sct + 1) < stream_count) ? 0 : 1);

        if (cur_mem_size == cur_stream_size)
        {
            /* consume both mem and stream regions */
            ext_size = cur_mem_size;
            ext_ptr = cur_mem_off;
            ext_off = cur_stream_off;

            if (!oom)
            {
                cur_mem_size = mem_size_array[++mct];
                cur_mem_off  = mem_offset_array[mct];
            }
            else
            {
                cur_mem_size = 0;
            }
            if (!oos)
            {
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
            else
            {
                cur_stream_size = 0;
            }
        }
        else if (cur_mem_size < cur_stream_size)
        {
            /* consume mem region and update stream region */
            ext_size = cur_mem_size;
            ext_ptr = cur_mem_off;
            ext_off = cur_stream_off;

            cur_stream_size -= cur_mem_size;
            cur_stream_off  += cur_mem_size;

            if (!oom)
            {
                cur_mem_size = mem_size_array[++mct];
                cur_mem_off  = mem_offset_array[mct];
            }
            else
            {
                cur_mem_size = 0;
            }
        }
        else /* cur_mem_size > cur_stream_size */
        {
            /* consume stream region and update mem region */
            ext_size = cur_stream_size;

            cur_mem_size -= cur_stream_size;
            cur_mem_off  += cur_stream_size;

            if (!oos)
            {
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
            else
            {
                cur_stream_size = 0;
            }
        }

        if(extents)
        {
            extents[act].buffer = ext_ptr;
            extents[act].offset = ext_off;
            extents[act].size =   ext_size;
        }
        act++;

        /* process until there are no bytes left in the current piece */
        if ((oom && cur_mem_size == 0) || (oos && cur_stream_size == 0))
        {
            break;
        }
    }

    /* return the number actually used */
    *ext_count = act;
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
