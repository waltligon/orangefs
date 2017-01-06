/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file defines a hash table based cache for the RA_CACHE
 * which does readaheads.  It is only used in the client-core and
 * really this file should be in that directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#ifndef WIN32
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "pvfs2-internal.h"
#include "pvfs2.h"
#include "gossip.h"
#include "quicklist.h"
#include "gen-locks.h"
#include "mmap-ra-cache.h"

static int racache_buf_init(racache_t *racache);
static racache_buffer_t *racache_buf_get(racache_file_t *racache_file);
static int lock_mem = 1; /* should we try to lock memory */

static int hash_key(const void *key, int table_size);
static int hash_key_compare(const void *key, struct qlist_head *link);
static void racache_init_buff(racache_buffer_t *buff);
static void racache_init_file(racache_file_t *file);
static void racache_buf_cull(racache_t *racache);

/* This represents the entire readahead cache */
static struct racache_s racache =
{
    PTHREAD_MUTEX_INITIALIZER,
    PVFS2_DEFAULT_RACACHE_BUFCNT,
    PVFS2_DEFAULT_RACACHE_BUFSZ,
    PVFS2_DEFAULT_RACACHE_READCNT,
    QLIST_HEAD_INIT(racache.buff_free),
    QLIST_HEAD_INIT(racache.buff_lru),
    NULL,
    NULL,
    NULL,
    0
};

#define RACACHE_INITIALIZED() (racache.hash_table)

#define DEFAULT_RACACHE_HTABLE_SIZE  19

PVFS_size pint_racache_buff_offset(PVFS_size offset)
{
    return offset - (offset % racache.bufsz);
}

int pint_racache_buff_size(void)
{
    return racache.bufsz;
}

int pint_racache_set_buff_size(int bufsz)
{
    return pint_racache_buf_resize(racache.bufcnt, bufsz);
}

int pint_racache_buff_count(void)
{
    return racache.bufcnt;
}

int pint_racache_set_buff_count(int bufcnt)
{
    return pint_racache_buf_resize(bufcnt, racache.bufsz);
}

int pint_racache_set_buff_count_size(int bufcnt, int bufsz)
{
    return pint_racache_buf_resize(bufcnt, bufsz);
}

int pint_racache_read_count(void)
{
    return racache.readcnt;
}

int pint_racache_set_read_count(int readcnt)
{
    racache.readcnt = readcnt;
    return 0;
}

int pint_racache_initialize(void)
{
    int ret = -1;

    if (!RACACHE_INITIALIZED())
    {
        if (racache_buf_init(&racache) < 0)
        {
            return -1;
        }

        racache.hash_table = qhash_init(hash_key_compare,
                                        hash_key,
                                        DEFAULT_RACACHE_HTABLE_SIZE);
        if (!racache.hash_table)
        {
            free(racache.buffarray); /* allocated in buf_init */
            return -1;
        }

        gossip_debug(GOSSIP_RACACHE_DEBUG,
                     "ra_cache_initialized\n");
        ret = 0;
    }
    else
    {
        gossip_debug(GOSSIP_RACACHE_DEBUG, "readahead cache already "
                     "initalized.  returning success\n");
        ret = 0;
    }
    return 0;
}

/* reset a file */
static void racache_init_file(racache_file_t *file)
{
    memset(&file->refn, 0, sizeof(PVFS_object_ref));

    /* eventually want a way to pass this in from the file */
    file->readcnt = racache.readcnt;

    INIT_QLIST_HEAD(&file->hash_link);
    INIT_QLIST_HEAD(&file->buff_list);
}

/* reset an individual buffer */
static void racache_init_buff(racache_buffer_t *buff)
{
    /* do not init vp, id, buff_sz, readcnt
     * this will be used to reinit buff struct */
    buff->valid = 0;
    buff->being_freed = 0;
    buff->resizing = 0;
    buff->vfs_cnt = 0;
    buff->file_offset = 0;
    buff->data_sz = 0;
    buff->file = NULL;

    INIT_QLIST_HEAD(&buff->buff_link);
    INIT_QLIST_HEAD(&buff->buff_lru);
    INIT_QLIST_HEAD(&buff->vfs_link);
}

/* initialize buffer management for readahead cache */
static int racache_buf_init(racache_t *racache)
{
    int i = 0;

    if (racache->bufcnt * racache->bufsz == 0)
    {
        /* racache turned off */
        return 0;
    }
    
    racache->buffarray =
                (racache_buffer_t *)malloc(sizeof(racache_buffer_t) * 
                                           racache->bufcnt);
    if (!racache->buffarray)
    {
        return -1;
    }
    for(i = 0; i < racache->bufcnt; i++)
    {
        void *vp;
        /* init the buffer struct - not vp */
        racache_init_buff(&racache->buffarray[i]);

        /* allocate buffer memory */
        posix_memalign(&vp,
                       sysconf(_SC_PAGESIZE),
                       racache->bufsz);
        if (!vp)
        {
            free(racache->buffarray);
            return -1;
        }
        if (lock_mem)
        {
            int ret = 0;
            int cnt = 0;
            
            do {
                ret = mlock(vp, racache->bufsz);
            } while (ret && errno == EAGAIN && cnt++ < 10);
            if (ret == -1)
            {
                gossip_err("locking memory failed, proceeding without it\n");
                lock_mem = 0; /* don't try to lock memory any more */
            }
        }

        racache->buffarray[i].buff_sz = racache->bufsz;
        racache->buffarray[i].readcnt = racache->readcnt;
        racache->buffarray[i].buffer = vp;
        racache->buffarray[i].buff_id = i;

        qlist_add_tail(&racache->buffarray[i].buff_link,
                       &racache->buff_free);
    }
    return 0;
}


static void racache_buf_cull(racache_t *racache)
{
    /* run through all the buffers and free up any that are not busy
     * keep a count so me know many are left, and if there are any
     * move the buffer list to the oldarray
     */
    int i;
    int remaining = 0;

    for (i = 0; i < racache->bufcnt; i++)
    {
        /* take this buffer off of any racache lists 
         * free list / file list
         */
        if (racache->buffarray[i].buff_link.next &&
            racache->buffarray[i].buff_link.prev)
        {
            qlist_del_init(&racache->buffarray[i].buff_link);
        }
        /* lru list */
        if (racache->buffarray[i].buff_lru.next &&
            racache->buffarray[i].buff_lru.prev)
        {
            qlist_del_init(&racache->buffarray[i].buff_lru);
        }
        /* now see if this buffer is still busy */
        if (racache->buffarray[i].valid == 0 &&
            racache->buffarray[i].vfs_cnt > 0)
        {
            /* this buffer is still wating so make it remaining */
            remaining++;
            /* this buffer is busy we will mark it */
            racache->buffarray[i].resizing = 1;
        }
        else
        {
            /* this buffer is finished so close it up */
            if (lock_mem)
            {
                int ret = 0;
                int cnt = 0;
                do {
                    munlock(racache->buffarray[i].buffer, racache->bufsz);
                } while (ret && errno == EAGAIN && cnt++ < 10);
            }
            free(racache->buffarray[i].buffer);
            memset(&racache->buffarray[i], 0, sizeof(racache_buffer_t));
        }
    }
    if (remaining)
    {
        /* some buffers are busy so move old buffarray to oldarray
         * where we will keep them until they are done
         */
        racache->oldarray = racache->buffarray;
        racache->oldarray_rem = remaining;
        racache->oldarray_cnt = racache->bufcnt;
        racache->oldarray_sz = racache->bufsz;
    }
    else
    {
        free(racache->buffarray);
    }
    racache->buffarray = NULL;
    racache->bufcnt = 0;
    racache->bufsz = 0;
    INIT_QLIST_HEAD(&racache->buff_free);
    INIT_QLIST_HEAD(&racache->buff_lru);
}

int pint_racache_finish_resize(racache_buffer_t *buff)
{
    int i;

    /* find the buffer in oldarray */
    for(i = 0; i < racache.oldarray_cnt; i++)
    {
        if (buff == &racache.oldarray[i])
        {
            break;
        }
    }

    if (i >= racache.oldarray_cnt)
    {
        /* error, not found */
        return -1;
    }

    if (lock_mem)
    {
        int ret = 0;
        int cnt = 0;
        do {
            munlock(racache.oldarray[i].buffer, racache.oldarray_sz);
        } while (ret && errno == EAGAIN && cnt++ < 10);
    }
    free(racache.oldarray[i].buffer);
    memset(&racache.buffarray[i], 0, sizeof(racache_buffer_t));
    racache.oldarray_rem--;

    if (!racache.oldarray_rem)
    {
        /* done with the resize */
        free(racache.oldarray);
        racache.oldarray_cnt = 0;
    }
    return 0;
}

int pint_racache_buf_resize(int bufcnt, int bufsz)
{
    /* get rid of all but busy buffers */
    racache_buf_cull(&racache);

    /* clear hash table */
    qhash_destroy_and_finalize(racache.hash_table,
                               racache_file_t,
                               hash_link,
                               free);

    /* set up the new sizes */
    racache.bufcnt = bufcnt;
    racache.bufsz = bufsz;

    /* recreate buffer space */
    if (racache_buf_init(&racache) < 0)
    {
        return -1;
    }

    /* recreate hash table */
    racache.hash_table = qhash_init(hash_key_compare,
                                    hash_key,
                                    DEFAULT_RACACHE_HTABLE_SIZE);
    if (!racache.hash_table)
    {
        free(racache.buffarray); /* allocated in buf_init */
        return -1;
    }
    return 0;
}

/* find a bufffer for a new readahead block */
static racache_buffer_t *racache_buf_get(racache_file_t *racache_file)
{
    struct qlist_head *link;
    racache_buffer_t *buff;
    /* try to take buffer off free list */
    if ((link = qlist_pop(&racache.buff_free)))
    {
        /* there is a free buffer so use it */
        buff = qlist_entry(link, racache_buffer_t, buff_link);
        gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_buff_get "
                     "found free buff %d - adding to lists\n",
                     buff->buff_id);
    }
    /* try to take buffer off head of lru list */
    else if ((link = qlist_pop(&racache.buff_lru)))
    {
        /* take a buff from the lru chain */
        buff = qlist_entry(link, racache_buffer_t, buff_lru);
        gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_buff_get "
                     "got buff %d from lru - adding to lists\n",
                     buff->buff_id);
        /* make sure buffer is not busy */
        if (!qlist_empty(&buff->vfs_link))
        {
            /* outstanding requests */
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_buff_get "
                         "this buff is busy with %d waiters - skip ra\n",
                         buff->vfs_cnt);
            /* should wait until they are all done */
            /* for now put on lru back where it was */
            qlist_add_tail(&buff->buff_lru, &racache.buff_lru);
            /* very busy cache just do a regular read */
            return NULL;
        }
        /* remove from prev file's buffer list */
        qlist_del(&buff->buff_link);
    }
    else /* can't find a usable buffer */
    {
        gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_buff_get "
                    "no buffer - this is probably not right\n");
        /* something wrong - order a regular read */
        return NULL;
    }
    /* wipes non-permanent fields and inits lists */
    racache_init_buff(buff);
    /* set file read count */
    if (racache_file->readcnt >= 0 &&
        racache_file->readcnt <= PVFS2_MAX_RACACHE_READCNT)
    {
        /* set from value attached to file */
        buff->readcnt = racache_file->readcnt;
    }
    else
    {
        /* set from default value */
        buff->readcnt = pint_racache_read_count();
    }
    /* put at tail of lru list */
    qlist_add_tail(&buff->buff_lru, &racache.buff_lru);
    /* add to this file's buffer list */
    qlist_add(&buff->buff_link, &racache_file->buff_list);
    /* readahead into this buffer */
    return buff;
}

/* reset a buffer on the lru list */
static void racache_buf_lru(racache_buffer_t *buff)
{
    /* remove from current place in lru list */
    qlist_del(&buff->buff_lru);
    /* add to the tail of lru list */
    qlist_add_tail(&buff->buff_lru, &racache.buff_lru);
}

int pint_racache_get_block(PVFS_object_ref refn,
                           PVFS_size offset,
                           PVFS_size len,
                           int readahead_speculative, /* add to wait list? */
                           void *vfs_req,
                           racache_buffer_t **rbuf,
                           int *amt_returned)
{
    struct gen_link_s *glink = NULL;
    struct qlist_head *hash_link = NULL;
    racache_file_t *racache_file = NULL;
    racache_buffer_t *buff = NULL;

    gossip_debug(GOSSIP_RACACHE_DEBUG, "Checking racache"
                 " for %d bytes at offset %lu\n",
                 (int)len, (unsigned long)offset);

    if (RACACHE_INITIALIZED())
    {
        gen_mutex_lock(&racache.mutex);
        /* find file rec in hash table */
        hash_link = qhash_search(racache.hash_table, &refn);
        if (hash_link)
        {
            gossip_debug(GOSSIP_RACACHE_DEBUG, "found file rec\n");
            racache_file = qhash_entry(hash_link,
                                       racache_file_t,
                                       hash_link);
            assert(racache_file);

            /* found the file, now search for a buffer */
            qlist_for_each_entry(buff, &racache_file->buff_list, buff_link)
            {
                /* cache hit must have all of the requested data */
                if (offset >= buff->file_offset &&
                    (offset + len) <= buff->file_offset + buff->buff_sz)
                {
                    /* data in cache - reset lru and set up return */
                    racache_buf_lru(buff);
                    /* found a matching buffer */
                    if (buff->valid)
                    {
                        gossip_debug(GOSSIP_RACACHE_DEBUG,
                                     "racache_get_block got buffer %d at "
                                     "file_offset %llu, data_sz %llu\n",  
                                     buff->buff_id,
                                     llu(buff->file_offset),
                                     llu(buff->data_sz));
                        if (rbuf)
                        {
                            *rbuf = buff;
                        }
                        if (amt_returned)
                        {
                            *amt_returned = (buff->file_offset +
                                             buff->data_sz) - offset;
                            if (len < *amt_returned)
                            {
                                *amt_returned = len;
                            }
                        }

                        gen_mutex_unlock(&racache.mutex);
                        return RACACHE_HIT;
                    }
                    else /* found buffer but not valid */
                    {
                        if (!readahead_speculative)
                        {
                            gossip_debug(GOSSIP_RACACHE_DEBUG,
                                         "racache_get_block "
                                         "found invalid buffer %d "
                                         "- will wait\n",
                                         buff->buff_id);
                            /* add request to waiting list */
                            glink = (gen_link_t *)malloc(sizeof(gen_link_t));
                            glink->payload = vfs_req;
                            /* makes list FIFO */
                            qlist_add_tail(&glink->link, &buff->vfs_link);
                            buff->vfs_cnt++;
                            gossip_debug(GOSSIP_RACACHE_DEBUG,
                                         "racache_get_block "
                                         "adding request %p to buffer "
                                         "%d vfs list #%d \n",
                                         vfs_req, buff->buff_id, buff->vfs_cnt);
                        }
                        else
                        {
                            gossip_debug(GOSSIP_RACACHE_DEBUG,
                                         "racache_get_block "
                                         "found invalid buffer %d\n",
                                         buff->buff_id);
                        }
                        /* return buffer */
                        if (rbuf)
                        {
                            *rbuf = buff;
                        }
                        if (amt_returned)
                        {
                            *amt_returned = 0;
                        }
                        gen_mutex_unlock(&racache.mutex);
                        return RACACHE_WAIT;
                    }
                }
            } /* end of for loop */
            /* No matching buffer for this file found */
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block "
                         "short cache miss (no buffer)\n");

            buff = racache_buf_get(racache_file);
            if (!buff)
            {
                gen_mutex_unlock(&racache.mutex);
                return RACACHE_NONE;
            }
            buff->file = racache_file;
            buff->file_offset = pint_racache_buff_offset(offset);
            gossip_debug(GOSSIP_RACACHE_DEBUG,
                         "racache_get_block offset %llu(%llu) size %llu\n",
                         llu(offset), llu(buff->file_offset),
                         llu(buff->buff_sz));

            /* add request to waiting list */
            glink = (gen_link_t *)malloc(sizeof(gen_link_t));
            INIT_QLIST_HEAD(&glink->link);
            glink->payload = vfs_req;
            /* makes list FIFO */
            qlist_add_tail(&glink->link, &buff->vfs_link);
            buff->vfs_cnt++;
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block "
                         "adding request %p to buffer %d vfs list #%d \n",
                         vfs_req, buff->buff_id, buff->vfs_cnt);
            /* return new buffer */
            if (rbuf)
            {
                *rbuf = buff;
            }
            if (amt_returned)
            {
                *amt_returned = 0;
            }

            /* set up new request */
            gen_mutex_unlock(&racache.mutex);
            return RACACHE_READ;
        }
        else /* hash lookup miss */
        {
            racache_file_t *rcfile;

            /* cache miss  - no file rec found */
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block "
                         "clean cache miss (nothing here)\n");
            /* try to get a buffer, if we can set up a file
             * rec and indicate a new readahead */
            rcfile = (racache_file_t *)malloc(sizeof(racache_file_t));
            if (!rcfile)
            {
                gen_mutex_unlock(&racache.mutex);
                return RACACHE_NONE;
            }
            racache_init_file(rcfile);
            rcfile->refn = refn;
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block "
                         "adding new file rec to hash table\n");
            qhash_add(racache.hash_table, &refn, &rcfile->hash_link);

            buff = racache_buf_get(rcfile);
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block "
                         "getting a buffer %d\n", buff->buff_id);
            if (!buff)
            {
                gen_mutex_unlock(&racache.mutex);
                return RACACHE_NONE;
            }
                
            buff->file = rcfile;
            buff->file_offset = pint_racache_buff_offset(offset);
            buff->data_sz = 0;

            /* add request to waiting list */
            glink = (gen_link_t *)malloc(sizeof(gen_link_t));
            INIT_QLIST_HEAD(&glink->link);
            glink->payload = vfs_req;
            /* makes lists FIFO */
            qlist_add_tail(&glink->link, &buff->vfs_link);
            buff->vfs_cnt++;
            gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block "
                         "adding request %p to buffer %d vfs list #%d \n",
                         vfs_req, buff->buff_id, buff->vfs_cnt);

            /* return new buffer */
            if (rbuf)
            {
                *rbuf = buff;
            }
            if (amt_returned)
            {
                *amt_returned = 0;
            }
            gen_mutex_unlock(&racache.mutex);
            return RACACHE_READ;
        }
    }
    gossip_debug(GOSSIP_RACACHE_DEBUG, "racache_get_block error \n");
    return -1;
}

int pint_racache_flush(PVFS_object_ref refn)
{
    int ret = 0;
    struct qlist_head *hash_link = NULL;
    racache_file_t *racache_file = NULL;
    struct qlist_head *blink = NULL;
    racache_buffer_t *buff = NULL;

    if (RACACHE_INITIALIZED())
    {
        gen_mutex_lock(&racache.mutex);
        hash_link = qhash_search_and_remove(racache.hash_table, &refn);
        if (hash_link)
        {
            gossip_debug(GOSSIP_RACACHE_DEBUG,
                         "racache_flush found file\n");
            racache_file = qhash_entry(hash_link, racache_file_t, hash_link);

            assert(racache_file);

            /* found the file, now pop all buffers */
            while ((blink = qlist_pop(&racache_file->buff_list)))
            {
                /* get a pointer to the buffer */
                buff = qhash_entry(blink, racache_buffer_t, buff_link);
                gossip_debug(GOSSIP_RACACHE_DEBUG,
                             "racache_flush removing buffer %d\n",
                             buff->buff_id);
                /* remove buffer from the lru list */
                qlist_del(&buff->buff_lru);
                /* clear reference to file record */
                buff->file = NULL;
                /* check for active requests */
                if (!buff->valid ||
                    !qlist_empty(&buff->vfs_link))
                {
                    gossip_debug(GOSSIP_RACACHE_DEBUG,
                                 "--- Flushed racache - wait for buffer "
                                 "id %d of size %llu\n",
                                 buff->buff_id,
                                 llu(buff->buff_sz));
                    /* make sure vfs list is clean */
                    buff->being_freed = 1;
                }
                else
                {
                    gossip_debug(GOSSIP_RACACHE_DEBUG,
                                 "--- Flushed racache - free buffer "
                                 "id %d of size %llu\n",
                                 buff->buff_id,
                                 llu(buff->buff_sz));
                    /* add buffer to free list */
                    pint_racache_make_free(buff);
                }
            }
            /* remove file from hash table */
            gossip_debug(GOSSIP_RACACHE_DEBUG,
                         "racache_flush removing file\n");
            qlist_del(&racache_file->hash_link);
            free(racache_file);
        }
        gen_mutex_unlock(&racache.mutex);
    }
    return ret;
}

/* this buff must not be on any buff list lru list or
 * have any vfs_requests waiting on it.
 * This should only be called internal to this module
 * and the caller should manage locking and unlocking
 */
void pint_racache_make_free(racache_buffer_t *buff)
{
    gossip_debug(GOSSIP_RACACHE_DEBUG,
                 "Making buffer %d with %d waiters free\n",
                  buff->buff_id, buff->vfs_cnt);
    /* add buffer to free list */
    racache_init_buff(buff);
    qlist_add_tail(&buff->buff_link, &racache.buff_free);
}

int pint_racache_finalize(void)
{
    int ret = -1;
    int i = 0;

    struct qlist_head *hash_link = NULL;
    racache_file_t *racache_file = NULL;
    struct qlist_head *buff_link = NULL;
    racache_buffer_t *racache_buffer = NULL;
    struct qlist_head *vfs_link = NULL;

    if (RACACHE_INITIALIZED())
    {
        gen_mutex_lock(&racache.mutex);
        for (i = 0; i < racache.hash_table->table_size; i++)
        {
            while((hash_link = qhash_search_and_remove_at_index(
                                                   racache.hash_table, i)))
            {
                racache_file = qlist_entry(hash_link,
                                           racache_file_t,
                                           hash_link);

                while ((buff_link = qlist_pop(&racache_file->buff_list)));
                {
                    racache_buffer = qlist_entry(buff_link,
                                                 racache_buffer_t,
                                                 buff_link);
                    gossip_debug(GOSSIP_RACACHE_DEBUG,
                                 "Freeing buffer %d with %d waiters\n",
                                 racache_buffer->buff_id,
                                 racache_buffer->vfs_cnt);
                    while((vfs_link = qlist_pop(&racache_buffer->vfs_link)));
                    {
                        racache_buffer->vfs_cnt--;
                        /* don't worry about the vfs_request
                         * we don't deal with that here */
                        free(qlist_entry(vfs_link, gen_link_t, link));
                    }
                }
                free(racache_file);
            } /* while hash_link */
        } /* for i < table_size */

        ret = 0;
        qhash_finalize(racache.hash_table);
        racache.hash_table = NULL; /* is this properly freed? */
        free(racache.buffarray); /* this frees the array of buffer recs */
        gen_mutex_unlock(&racache.mutex);

        /* FIXME: race condition here */
        gen_mutex_destroy(&racache.mutex);
        gossip_debug(GOSSIP_RACACHE_DEBUG, "ra_cache_finalized\n");
    }
    return ret;
}

/* hash_key()
 *
 * hash function for pinode_refns added to table
 *
 * returns integer offset into table
 */
static int hash_key(const void *key, int table_size)
{
    unsigned long tmp = 0;
    const PVFS_object_ref *refn = (const PVFS_object_ref *)key;

    tmp += ((refn->handle << 2) | (refn->fs_id));
    tmp = (tmp % table_size);

    return ((int)tmp);
}

/* hash_key_compare()
 *
 * performs a comparison of a hash table entry to a given key
 * (used for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_key_compare(const void *key, struct qlist_head *link)
{
    racache_file_t *racache_file = NULL;
    const PVFS_object_ref *refn = (const PVFS_object_ref *)key;

    racache_file = qlist_entry(link, racache_file_t, hash_link);
    assert(racache_file);

    return (((racache_file->refn.handle == refn->handle) &&
             (racache_file->refn.fs_id == refn->fs_id)) ? 1 : 0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
