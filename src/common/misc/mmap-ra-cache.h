/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __MMAP_RA_CACHE_H
#define __MMAP_RA_CACHE_H

#include "quickhash.h"
#include "pvfs2-internal.h"

#define PVFS2_DEFAULT_RACACHE_BUFSZ   (2 * 1024 * 1024)
#define PVFS2_MAX_RACACHE_BUFSZ       (256 * 1024 * 1024)

#define PVFS2_DEFAULT_RACACHE_BUFCNT  (32)
#define PVFS2_MAX_RACACHE_BUFCNT      (256)

#define PVFS2_DEFAULT_RACACHE_READCNT (4)
#define PVFS2_MAX_RACACHE_READCNT     (16)

#define PVFS2_RACACHE_READSZ_NOVALUE  -1

/* racache_status values */
#define RACACHE_NONE          0
#define RACACHE_HIT           1 /* hit - copy data from buffer provided */
#define RACACHE_WAIT          2 /* hit - buffer is being read */
#define RACACHE_READ          3 /* miss - read a new buffer */
#define RACACHE_POSTED        4 /* miss - read of new buffer POSTED */

typedef struct gen_link_s
{
    struct qlist_head link;
    void *payload;
} gen_link_t;

/* for for each file with cached buffers */
typedef struct racache_file_s
{
    struct qlist_head hash_link; /* hash table link */
    PVFS_object_ref refn;
    struct qlist_head buff_list; /* list of buffers for this file in cache */
    PVFS_size readcnt;
} racache_file_t;

/* one for each buffer in cache */
typedef struct racache_buffer_s
{   
    struct qlist_head buff_link; /* list of buffers in a file */
    struct qlist_head buff_lru;  /* list of all buffers in cache */
    struct qlist_head vfs_link;  /* list of requests waiting for this buff */
    int valid;                   /* non zero if read into buffer is complete */
    int being_freed;             /* non zero if file has been flushed */
    int resizing;                /* non zero if buffers are resizing */
    int buff_id;
    int vfs_cnt;
    PVFS_size file_offset;
    PVFS_size data_sz;
    PVFS_size buff_sz;
    PVFS_size readcnt;
    char *buffer;          /* this is a buffer used to read and hold ra data */
    struct racache_file_s *file;
} racache_buffer_t;

/* only one of these */
typedef struct racache_s
{
    gen_mutex_t mutex;
    int    bufcnt;                     /* number of buffers in the cache */
    int    bufsz;                      /* size of each buffer */
    int    readcnt;                    /* default readahead count */
    struct qlist_head buff_free;       /* list of free buffers */
    struct qlist_head buff_lru;        /* lru list of buffers int use */
    struct qhash_table *hash_table;    /* file name index to find buffers */
    struct racache_buffer_s *buffarray;/* array of bufcnt buffer structs */
    struct racache_buffer_s *oldarray; /* temp storage of buffs after resize */
    int    oldarray_cnt;               /* count of original bufs in oldarray */
    int    oldarray_rem;               /* number of busy bufs in oldarray */
    int    oldarray_sz;                /* size of busy bufs in oldarray */
} racache_t;


/***********************************************
 * mmap_ra_cache methods - specifically for
 * caching data temporarily to optimize for
 * vfs mmap/execution cases, where many calls
 * are made for small amounts of data.
 *
 * all methods return 0 on success; -1 on failure
 * (unless noted)
 *
 ***********************************************/

PVFS_size pint_racache_buff_offset(PVFS_size offfset);

int pint_racache_buff_count(void);

int pint_racache_buff_size(void);

int pint_racache_read_count(void);

int pint_racache_set_read_count(int readcnt);

int pint_racache_set_buff_count(int bufcnt);

int pint_racache_set_buff_size(int bufsz);

int pint_racache_set_buff_count_size(int bufcnt, int bufsz);

int pint_racache_initialize(void);

int pint_racache_buf_resize(int bufcnt, int bufsz);

int pint_racache_finish_resize(racache_buffer_t *buff);

/*
 * search for a buffer in the cache
 * returns RACACHE_NONE if no buffers are available - do normal IO
 * returns RACACHE_HIT  on cache hit - data has been copied
 * returns RACACHE_WAIT if the block is currently being read
 * returns RACACHE_READ if the the block needs to be read
 */
int pint_racache_get_block(PVFS_object_ref refn,
                            PVFS_size offset,
                            PVFS_size len,
                            int readahead_speculative,
                            void *vfs_req,
                            racache_buffer_t **rbuf,
                            int *amt_returned);

/* remove all cache entries for a given file */
int pint_racache_flush(PVFS_object_ref refn);

/* remove all cache entries and prepare for exit */
int pint_racache_finalize(void);

void pint_racache_make_free(racache_buffer_t *buff);

#endif /* __MMAP_RA_CACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
