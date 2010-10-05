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
#include "dbpf-sync.h"
#include "pint-mem.h"
#include "pint-mgmt.h"
#include "pint-context.h"
#include "pint-op.h"

static gen_mutex_t dbpf_update_size_lock = GEN_MUTEX_INITIALIZER;

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
                                    off_t stream_size);

static size_t direct_locked_write(int fd,
                            void * buf,
                            off_t buf_offset,
                            size_t size,
                            off_t write_offset,
                            off_t stream_size);

#if 0
static size_t new_direct_write(int fd,
                               void * buf,
                               off_t buf_offset,
                               size_t size,
                               off_t write_offset,
                               off_t stream_size);
#endif

static size_t direct_write(int fd,
                           void * buf,
                           off_t buf_offset,
                           size_t size,
                           off_t write_offset,
                           off_t stream_size);

static size_t direct_aligned_read(int fd,
                                  void *buf,
                                  off_t buf_offset,
                                  size_t size,
                                  off_t file_offset,
                                  off_t stream_size);

static size_t direct_locked_read(int fd,
                            void * buf,
                            off_t buf_offset,
                            size_t size,
                            off_t file_offset,
                            off_t stream_size);

static size_t direct_read(int fd, 
                          void * buf, 
                          off_t buf_offset,
                          size_t size,
                          off_t file_offset, 
                          off_t stream_size);

#define BLOCK_SIZE 4096

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

extern PINT_manager_t io_thread_mgr;
extern PINT_worker_id io_worker_id;
extern PINT_queue_id io_queue_id;

#if 0
struct aligned_block
{
    void *ptr;
    struct qlist_head link;
};
static struct aligned_block *blocks;
static void *aligned_blocks_buffer;
static QLIST_HEAD(aligned_blocks_unused);
static QLIST_HEAD(aligned_blocks_used);
static gen_mutex_t aligned_blocks_mutex = GEN_MUTEX_INITIALIZER;
static int used_count;

int dbpf_aligned_blocks_init(void);
void * dbpf_aligned_block_get(void);
int dbpf_aligned_block_put(void *ptr);
int dbpf_aligned_blocks_finalize(void);
#endif

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
                                    off_t stream_size)
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
        return ret;
    }

    return ret;
}

/* static int writes_outstanding = 0; 
gen_mutex_t writes_lock = GEN_MUTEX_INITIALIZER; */

static size_t direct_locked_write(int fd,
                            void * buf,
                            off_t buf_offset,
                            size_t size,
                            off_t write_offset,
                            off_t stream_size)
{
    struct flock writelock;
    int ret, write_ret;
/*	struct timeval start, end; */

    writelock.l_type = F_WRLCK;
    writelock.l_whence = SEEK_SET;
    writelock.l_start = (off_t)ALIGNED_OFFSET(write_offset);
    writelock.l_len = (off_t)ALIGNED_SIZE(write_offset, size);
    ret = fcntl(fd, F_SETLKW, &writelock);
    if(ret < 0 && errno == EINTR)
    {
        gossip_err("%s: failed to lock flock before writing\n", __func__);
        return -trove_errno_to_trove_error(errno);
    }
    writelock.l_type = F_UNLCK;

    write_ret = direct_write(
        fd, buf, buf_offset, size, write_offset, stream_size);

    ret = fcntl(fd, F_SETLK, &writelock);
    if (ret < 0)
    {
        gossip_err("%s: failed to unlock flock after writing\n", __func__);
        return -trove_errno_to_trove_error (errno);
    }

#if 0
    if(write_ret > 0)
    {
        if((write_offset + size) > stream_size)
	{
            ret = DBPF_RESIZE(fd, (write_offset + size));
	    if(ret < 0)
	    {
		gossip_err("failed ftruncate of O_DIRECT fd to size: %d\n",
			   (write_offset + size));
		return -trove_errno_to_trove_error(errno);
	    }
	}
    }
#endif

    return write_ret;
}

#if 0
static size_t new_direct_write(int fd,
                               void * buf,
                               off_t buf_offset,
                               size_t size,
                               off_t write_offset,
                               off_t stream_size)
{
    size_t ret;
    void *aligned_buf;
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

    aligned_buf = dbpf_aligned_block_get();
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
            gossip_debug(GOSSIP_DIRECTIO_DEBUG, "Doing RMW at front\n");
            /* read the first block */
            ret = dbpf_pread(fd, aligned_buf, BLOCK_SIZE, aligned_offset);
            if(ret < 0)
            {
                int pread_errno = errno;
                gossip_err(
                    "direct_memcpy_write: RMW failed at "
                    "beginning of request\n");
		dbpf_aligned_block_put(aligned_buf);

                return -trove_errno_to_trove_error(pread_errno);
            }
        }
        else
        {
            memset(aligned_buf, 0, BLOCK_SIZE);
        }

        memcpy(((char *)buf) - (write_offset - aligned_offset),
               aligned_buf, (write_offset - aligned_offset));
    }

    end_offset = write_offset + size;
    aligned_end_offset = aligned_offset + aligned_size;

    if(aligned_end_offset > end_offset)
    {
        ret = 0;
        if(ALIGNED_SIZE(0, stream_size) >= aligned_end_offset)
        {
            gossip_debug(GOSSIP_DIRECTIO_DEBUG, "Doing RMW at end\n");
            ret = dbpf_pread(fd,
                             aligned_buf,
                             BLOCK_SIZE,
                             aligned_end_offset - BLOCK_SIZE);
            if(ret < 0)
            {
                int pread_errno = errno;
                gossip_err(
                    "direct_memcpy_write: RMW failed at end of request\n");
                dbpf_aligned_block_put(aligned_buf);

                return -trove_errno_to_trove_error(pread_errno);
            }
        }
        else
        {
            memset(aligned_buf, 0, BLOCK_SIZE);
        }

        memcpy(((char *)buf) + size,
               ((char *)aligned_buf) + (end_offset % BLOCK_SIZE),
               (aligned_end_offset - end_offset));
    }

    ret = direct_aligned_write(
        fd,
        ((char *)buf) - (write_offset - aligned_offset), 0,
        aligned_size, aligned_offset, stream_size);

    dbpf_aligned_block_put(aligned_buf);

    return size;
}
#endif

static size_t direct_write(int fd,
                           void * buf,
                           off_t buf_offset,
                           size_t size,
                           off_t write_offset,
                           off_t stream_size)
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
                 "stream_size: %llu\n",
                 buf, 
                 llu(buf_offset), 
                 size,
                 llu(write_offset),
                 llu(stream_size));

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
            gossip_debug(GOSSIP_DIRECTIO_DEBUG, "Doing RMW at front\n");
            ret = dbpf_pread(fd, aligned_buf, BLOCK_SIZE, aligned_offset);
            if(ret < 0)
            {
                int pread_errno = errno;
                gossip_err(
                    "direct_memcpy_write: RMW failed at "
                    "beginning of request\n");
                PINT_mem_aligned_free(aligned_buf);

                return -trove_errno_to_trove_error(pread_errno);
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
            gossip_debug(GOSSIP_DIRECTIO_DEBUG, "Doing RMW at end\n");
            ret = dbpf_pread(
                fd,
                ((char *)aligned_buf) + aligned_size - BLOCK_SIZE,
                BLOCK_SIZE,
                aligned_end_offset - BLOCK_SIZE);
            if(ret < 0)
            {
                int pread_errno = errno;
                gossip_err(
                    "direct_memcpy_write: RMW failed at end of request\n");
                PINT_mem_aligned_free(aligned_buf);

                return -trove_errno_to_trove_error(pread_errno);
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

    return (ret < 0) ? ret : size;
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
                                   off_t stream_size)
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
        return -trove_errno_to_trove_error(errno);
    }

    return ret;
}

static size_t direct_locked_read(int fd,
                           void * buf,
                           off_t buf_offset,
                           size_t size,
                           off_t file_offset,
                           off_t stream_size)
{
    int ret, read_ret;
    struct flock readlock;

    readlock.l_type = F_RDLCK;
    readlock.l_whence = SEEK_SET;
    readlock.l_start = (off_t)ALIGNED_OFFSET(file_offset);
    readlock.l_len = (off_t)ALIGNED_SIZE(file_offset, size);
    ret = fcntl(fd, F_SETLKW, &readlock);
    if(ret < 0 && errno == EINTR)
    {
        return -trove_errno_to_trove_error(errno);
    }
    readlock.l_type = F_UNLCK;

    read_ret = direct_read(fd, buf, buf_offset, size, file_offset, stream_size);

    ret = fcntl(fd, F_SETLK, &readlock);
    if(ret < 0)
    {
        return -trove_errno_to_trove_error(errno);
    }

    return read_ret;
}

static size_t direct_read(int fd,
                           void * buf,
                           off_t buf_offset,
                           size_t size,
                           off_t file_offset,
                           off_t stream_size)
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

    return ret;
}

static int dbpf_bstream_direct_read_op_svc(void *ptr, PVFS_hint hint)
{
    int ret = -TROVE_EINVAL;
    TROVE_object_ref ref;
    TROVE_ds_attributes attr;
    dbpf_queued_op_t *qop_p;
    struct dbpf_bstream_rw_list_op *rw_op;
    dbpf_stream_extents_t *stream_extents = NULL;
    int i, extent_count;

    rw_op = (struct dbpf_bstream_rw_list_op *)ptr;
    qop_p = (dbpf_queued_op_t *)rw_op->queued_op_ptr;

    ref.fs_id = qop_p->op.coll_p->coll_id;
    ref.handle = qop_p->op.handle;

    /* not in attribute cache.  get the size from dspace */
    ret = dbpf_dspace_attr_get(qop_p->op.coll_p, ref, &attr);
    if(ret != 0)
    {
        gossip_err("%s: failed to get size in dspace attr: (error=%d)\n", __func__, ret);
        goto done;
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
        gossip_err("%s: failed to get bstream extents from offset/sizes: (error=%d)\n", __func__, ret);
        goto done;
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
        gossip_err("%s: failed to get bstream extents from offset/sizes: (error=%d)\n", __func__, ret);
        goto done;
    }

    for(i = 0; i < extent_count; ++ i)
    {
        ret = direct_locked_read(rw_op->open_ref.fd,
                          stream_extents[i].buffer,
                          0,
                          stream_extents[i].size,
                          stream_extents[i].offset,
                          attr.u.datafile.b_size);
        if(ret < 0)
        {
            ret = -trove_errno_to_trove_error(-ret);
            gossip_err("%s: direct_locked_read failed: (error=%d)\n", __func__, ret);
            goto done;
        }
    }

    ret = DBPF_OP_COMPLETE;

done:
    if(stream_extents)
    {
        free(stream_extents);
    }
    dbpf_open_cache_put(&rw_op->open_ref);
    return ret;
}

static int dbpf_bstream_direct_write_op_svc(void *ptr, PVFS_hint hint)
{
    int ret = -TROVE_EINVAL;
    TROVE_object_ref ref;
    TROVE_ds_attributes attr;
    dbpf_stream_extents_t *stream_extents = NULL;
    int i, extent_count;
    struct dbpf_bstream_rw_list_op *rw_op;
    dbpf_queued_op_t *qop_p;
    PVFS_size eor = -1;
    int sync_required = 0;

    rw_op = (struct dbpf_bstream_rw_list_op *)ptr;
    qop_p = (dbpf_queued_op_t *)rw_op->queued_op_ptr;

    ref.fs_id = qop_p->op.coll_p->coll_id;
    ref.handle = qop_p->op.handle;

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
        gossip_err("%s: failed to count extents from stream offset/sizes: (error=%d)\n", __func__, ret);
        goto cache_put;
    }

    stream_extents = malloc(sizeof(*stream_extents) * extent_count);
    if(!stream_extents)
    {
        ret = -TROVE_ENOMEM;
        goto cache_put;
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
        gossip_err("%s: failed to get stream extents from stream offset/sizes: (error=%d)\n", __func__, ret);
        goto cache_put;
    }

    ret = dbpf_dspace_attr_get(qop_p->op.coll_p, ref, &attr);
    if(ret != 0)
    {
        gossip_err("%s: failed to get dspace attr for bstream: (error=%d)\n", __func__, ret);
        goto cache_put;
    }

    *rw_op->out_size_p = 0;
    for(i = 0; i < extent_count; ++ i)
    {
        ret = direct_locked_write(rw_op->open_ref.fd,
                                  stream_extents[i].buffer,
                                  0,
                                  stream_extents[i].size,
                                  stream_extents[i].offset,
                                  attr.u.datafile.b_size);
        if(ret < 0)
        {
            gossip_err("%s: failed to perform direct locked write: (error=%d)\n", __func__, ret);
            goto cache_put;
        }

        if(eor < stream_extents[i].offset + stream_extents[i].size)
        {
            eor = stream_extents[i].offset + stream_extents[i].size;
        }

        *rw_op->out_size_p += ret;
    }

    if(eor > attr.u.datafile.b_size)
    {
        int outcount;

        gen_mutex_lock(&dbpf_update_size_lock);
        ret = dbpf_dspace_attr_get(qop_p->op.coll_p, ref, &attr);
        if(ret != 0)
        {
            gossip_err("%s: failed to get size from dspace attr: (error=%d)\n", __func__, ret);
            gen_mutex_unlock(&dbpf_update_size_lock);
            goto cache_put;
        }

        if(eor > attr.u.datafile.b_size)
        {
            /* set the size of the file */
            attr.u.datafile.b_size = eor;
            ret = dbpf_dspace_attr_set(qop_p->op.coll_p, ref, &attr);
            if(ret != 0)
            {
                gossip_err("%s: failed to update size in dspace attr: (error=%d)\n", __func__, ret);
                gen_mutex_unlock(&dbpf_update_size_lock);
                goto cache_put;
            }
            sync_required = 1;
        }
        gen_mutex_unlock(&dbpf_update_size_lock);

        if(sync_required == 1)
        {
            gossip_debug(GOSSIP_DIRECTIO_DEBUG, 
                "directio updating size for handle %llu\n", llu(ref.handle));

            dbpf_open_cache_put(&rw_op->open_ref);

            /* If we updated the size, then convert cur_op into a setattr.
             * Note that we are not actually going to perform a setattr.
             * We just want the coalescing path to treat it like a setattr
             * so that the size update is synced before we complete.
             */
            dbpf_queued_op_init(qop_p,
                                DSPACE_SETATTR,
                                ref.handle,
                                qop_p->op.coll_p,
                                dbpf_dspace_setattr_op_svc,
                                qop_p->op.user_ptr,
                                TROVE_SYNC,
                                qop_p->op.context_id);
            qop_p->op.state = OP_IN_SERVICE;
            ret = dbpf_sync_coalesce(qop_p, 0, &outcount);
            if(ret < 0)
            {
                gossip_err("%s: failed to coalesce size update in dspace attr: (error=%d)\n", __func__, ret);
                goto done;
            }

            ret = PINT_MGMT_OP_CONTINUE;
            goto done;
        }
    }

    ret = PINT_MGMT_OP_COMPLETED;

cache_put:
    dbpf_open_cache_put(&rw_op->open_ref);
done:
    if(stream_extents)
    {
        free(stream_extents);
    }
    return ret;
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
                                       TROVE_op_id *out_op_id_p,
				       PVFS_hint hints)
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
                                        TROVE_op_id *out_op_id_p,
					PVFS_hint hints)
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
                                         TROVE_op_id *out_op_id_p,
                                         PVFS_hint hints)
{

    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_bstream_rw_list_op *op;
    struct dbpf_collection *coll_p = NULL;
    int ret;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        gossip_err("%s: failed to find collection with fsid %d\n", __func__, coll_id);
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
                        NULL,
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

    *out_op_id_p = q_op_p->op.id;
    ret = PINT_manager_id_post(
        io_thread_mgr, q_op_p, &q_op_p->mgr_op_id,
        dbpf_bstream_direct_read_op_svc, op, NULL, io_queue_id);
    if(ret < 0)
    {
        gossip_err("%s: failed to post direct read op: (error=%d)\n", __func__, ret);
        return ret;
    }

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
                                          TROVE_op_id *out_op_id_p,
                                          PVFS_hint hints)
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
    if(!q_op_p)
    {
        return -TROVE_ENOMEM;
    }
    dbpf_queued_op_init(q_op_p,
                        BSTREAM_WRITE_LIST,
                        handle,
                        coll_p,
                        NULL,
                        user_ptr,
                        TROVE_SYNC,
                        context_id);

    op = &q_op_p->op.u.b_rw_list;

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

    *out_op_id_p = q_op_p->op.id;

    gossip_debug(GOSSIP_DIRECTIO_DEBUG, "%s: queuing direct write operation\n", __func__);
    PINT_manager_id_post(
        io_thread_mgr, q_op_p, &q_op_p->mgr_op_id,
        dbpf_bstream_direct_write_op_svc, op, NULL, io_queue_id);

    return DBPF_OP_CONTINUE;
}

static int dbpf_bstream_direct_resize_op_svc(struct dbpf_op *op_p)
{
    int ret;
    TROVE_ds_attributes attr;
    TROVE_object_ref ref;
    dbpf_queued_op_t *q_op_p;
    struct open_cache_ref open_ref;
    PVFS_size tmpsize;

    q_op_p = (dbpf_queued_op_t *)op_p->u.b_resize.queued_op_ptr;
    ref.fs_id = op_p->coll_p->coll_id;
    ref.handle = op_p->handle;

    gen_mutex_lock(&dbpf_update_size_lock);
    ret = dbpf_dspace_attr_get(op_p->coll_p, ref, &attr);
    if(ret != 0)
    {
        gen_mutex_unlock(&dbpf_update_size_lock);
        return ret;
    }

    tmpsize = op_p->u.b_resize.size;
    attr.u.datafile.b_size = tmpsize;

    ret = dbpf_dspace_attr_set(op_p->coll_p, ref, &attr);
    if(ret < 0)
    {
        gen_mutex_unlock(&dbpf_update_size_lock);
        return ret;
    }
    gen_mutex_unlock(&dbpf_update_size_lock);

    /* setup op for sync coalescing */
    dbpf_queued_op_init(q_op_p,
                        DSPACE_SETATTR,
                        ref.handle,
                        q_op_p->op.coll_p,
                        dbpf_dspace_setattr_op_svc,
                        q_op_p->op.user_ptr,
                        TROVE_SYNC,
                        q_op_p->op.context_id);
    q_op_p->op.state = OP_IN_SERVICE;

    /* truncate file after attributes are set */
    ret = dbpf_open_cache_get(
        op_p->coll_p->coll_id, op_p->handle,
        DBPF_FD_DIRECT_WRITE,
        &open_ref);
    if(ret < 0)
    {
        return ret;
    }

    ret = DBPF_RESIZE(open_ref.fd, tmpsize);
    if(ret < 0)
    {
        return(ret);
    }

    dbpf_open_cache_put(&open_ref);

    return DBPF_OP_COMPLETE;
}

static int dbpf_bstream_direct_resize(TROVE_coll_id coll_id,
                                      TROVE_handle handle,
                                      TROVE_size *inout_size_p,
                                      TROVE_ds_flags flags,
                                      TROVE_vtag_s *vtag,
                                      void *user_ptr,
                                      TROVE_context_id context_id,
                                      TROVE_op_id *out_op_id_p,
				      PVFS_hint hints)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

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
                                        TROVE_op_id *out_op_id_p,
                                        PVFS_hint hints)
{
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_direct_flush(TROVE_coll_id coll_id,
                                     TROVE_handle handle,
                                     TROVE_ds_flags flags,
                                     void *user_ptr,
                                     TROVE_context_id context_id,
                                     TROVE_op_id *out_op_id_p,
                                     PVFS_hint hints)
{
    return DBPF_OP_COMPLETE;
}

static int dbpf_bstream_direct_cancel(
    TROVE_coll_id coll_id,
    TROVE_op_id cancel_id,
    TROVE_context_id context_id)
{
    dbpf_queued_op_t *op;
    int ret;

    op = id_gen_fast_lookup(cancel_id);
    if(!op)
    {
        gossip_lerr("Invalid op-id to cancel\n");
        return -TROVE_EINVAL;
    }

    ret = PINT_manager_cancel(io_thread_mgr, op->mgr_op_id);
    if(ret < 0)
    {
        return ret|PVFS_ERROR_TROVE;
    }

    return ret;
}

struct TROVE_bstream_ops dbpf_bstream_direct_ops =
{
    dbpf_bstream_direct_read_at,
    dbpf_bstream_direct_write_at,
    dbpf_bstream_direct_resize,
    dbpf_bstream_direct_validate,
    dbpf_bstream_direct_read_list,
    dbpf_bstream_direct_write_list,
    dbpf_bstream_direct_flush,
    dbpf_bstream_direct_cancel
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
	    ext_ptr = cur_mem_off;
	    ext_off = cur_stream_off;

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

#if 0
int dbpf_aligned_blocks_init(void)
{
    int i;

    aligned_blocks_buffer = PINT_mem_aligned_alloc(BLOCK_SIZE*256, BLOCK_SIZE);
    blocks = malloc(sizeof(*blocks) * 256);
    used_count = 0;
    gen_mutex_lock(&aligned_blocks_mutex);
    for(i = 0; i < 256; ++i)
    {
        blocks[i].ptr = ((char *)aligned_blocks_buffer) + (i*BLOCK_SIZE);
        qlist_add_tail(&(blocks[i].link), &aligned_blocks_unused);
    }
    gen_mutex_unlock(&aligned_blocks_mutex);
    return 0;
}

int dbpf_aligned_blocks_finalize(void)
{
    free(blocks);
    PINT_mem_aligned_free(aligned_blocks_buffer);
    return 0;
}

void *dbpf_aligned_block_get(void)
{
    void *ptr;
    struct aligned_block *ablock;
    gen_mutex_lock(&aligned_blocks_mutex);
    if(used_count > 255)
    {
        gossip_debug(GOSSIP_DIRECTIO_DEBUG, "ran out of aligned blocks: %d\n",
                     used_count);
        gen_mutex_unlock(&aligned_blocks_mutex);
        return NULL;
    }
    if(qlist_empty(&aligned_blocks_unused))
    {
	gossip_debug(GOSSIP_DIRECTIO_DEBUG,
                     "aligned_block_get: unused list empty.\n");
        gen_mutex_unlock(&aligned_blocks_mutex);
        return NULL;
    }

    ablock = qlist_entry(aligned_blocks_unused.next, struct aligned_block, link);
    qlist_del(&ablock->link);
    ptr = ablock->ptr;
    ablock->ptr = NULL;
    qlist_add_tail(&ablock->link, &aligned_blocks_used);
    ++used_count;
    gen_mutex_unlock(&aligned_blocks_mutex);
    return ptr;
}

int dbpf_aligned_block_put(void *ptr)
{
    struct aligned_block *ablock;

    gen_mutex_lock(&aligned_blocks_mutex);
    ablock = qlist_entry(aligned_blocks_used.next, struct aligned_block, link);
    qlist_del(&ablock->link);
    ablock->ptr = ptr;
    qlist_add_tail((&(ablock->link)), &aligned_blocks_unused);
    --used_count;
    gen_mutex_unlock(&aligned_blocks_mutex);
    return 0;
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
