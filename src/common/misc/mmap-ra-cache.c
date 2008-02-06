/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>

#include "pvfs2.h"
#include "gossip.h"
#include "gen-locks.h"
#include "mmap-ra-cache.h"
#include "pvfs2-internal.h"

static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);

static gen_mutex_t s_mmap_ra_cache_mutex = GEN_MUTEX_INITIALIZER;
static struct qhash_table *s_key_to_data_table = NULL;

#define MMAP_RA_CACHE_INITIALIZED() \
(s_key_to_data_table)

#define DEFAULT_MMAP_RA_CACHE_HTABLE_SIZE  19

int pvfs2_mmap_ra_cache_initialize(void)
{
    int ret = -1;

    if (!MMAP_RA_CACHE_INITIALIZED())
    {
        s_key_to_data_table = qhash_init(
            hash_key_compare, hash_key,
            DEFAULT_MMAP_RA_CACHE_HTABLE_SIZE);
        if (!s_key_to_data_table)
        {
            goto return_error;
        }

        gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG,
                     "mmap_ra_cache_initialized\n");
        ret = 0;
    }
    else
    {
        gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG, "mmap readahead cache already "
                     "initalized.  returning success\n");
        ret = 0;
    }

  return_error:
    return ret;
}

int pvfs2_mmap_ra_cache_register(PVFS_object_ref refn,
                                 void *data, int data_len)
{
    int ret = -1;
    mmap_ra_cache_elem_t *cache_elem = NULL;

    if (MMAP_RA_CACHE_INITIALIZED())
    {
        pvfs2_mmap_ra_cache_flush(refn);

        cache_elem = (mmap_ra_cache_elem_t *)
            malloc(sizeof(mmap_ra_cache_elem_t));
        if (!cache_elem)
        {
            goto return_exit;
        }
        memset(cache_elem, 0, sizeof(mmap_ra_cache_elem_t));
        cache_elem->refn = refn;
        cache_elem->data = malloc(data_len);
        if (!cache_elem->data)
        {
            goto return_exit;
        }
        memcpy(cache_elem->data, data, data_len);
        cache_elem->data_sz = data_len;

        gen_mutex_lock(&s_mmap_ra_cache_mutex);
        qhash_add(s_key_to_data_table,
                  &refn, &cache_elem->hash_link);
        gen_mutex_unlock(&s_mmap_ra_cache_mutex);

        gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG, "Inserted mmap ra cache "
                     "element %llu, %d of size %llu\n",
                     llu(cache_elem->refn.handle),
                     cache_elem->refn.fs_id,
                     llu(cache_elem->data_sz));

        ret = 0;
    }

  return_exit:
    return ret;
}

int pvfs2_mmap_ra_cache_get_block(
    PVFS_object_ref refn, PVFS_size offset,
    PVFS_size len, void *dest, int *amt_returned)
{
    int ret = -1;
    void *ptr = NULL;
    struct qlist_head *hash_link = NULL;
    mmap_ra_cache_elem_t *cache_elem = NULL;

    if (MMAP_RA_CACHE_INITIALIZED())
    {
        gen_mutex_lock(&s_mmap_ra_cache_mutex);
        hash_link = qhash_search(s_key_to_data_table, &refn);
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, mmap_ra_cache_elem_t, hash_link);
            assert(cache_elem);

            if (cache_elem->data_sz > (offset + len))
            {
                gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG,
                             "mmap_ra_cache_get_block got block at "
                             "offset %llu, len %llu\n",  llu(offset),
                             llu(len));

                ptr = (void *)(((char *)cache_elem->data + offset));
                memcpy(dest, ptr, len);

                if (amt_returned)
                {
                    *amt_returned = len;
                }
                ret = 0;
            }
            else
            {
                int actual_len = (int)
                    ((offset + len) - cache_elem->data_sz);

                gossip_debug(
                    GOSSIP_MMAP_RCACHE_DEBUG, "mmap_ra_cache_get_block "
                    "found invalid block [%llu/%llu]\n",
                    llu(offset), llu(len));

                if (actual_len > 0)
                {
                    gossip_debug(
                        GOSSIP_MMAP_RCACHE_DEBUG, " data_sz is %llu, "
                        "offset is %llu len is %llu\n\t(filling partial %d "
                        "bytes)\n", llu(cache_elem->data_sz), llu(offset),
                        llu(len), actual_len);

                    ptr = (void *)(((char *)cache_elem->data + offset));
                    memcpy(dest, ptr, actual_len);

                    if (amt_returned)
                    {
                        *amt_returned = actual_len;
                    }
                    ret = 0;
                }
            }
        }
        else
        {
            gossip_debug(
                GOSSIP_MMAP_RCACHE_DEBUG, "mmap_ra_cache_get_block "
                "clean cache miss (nothing here)\n");
        }
        gen_mutex_unlock(&s_mmap_ra_cache_mutex);
    }
    return ret;
}

int pvfs2_mmap_ra_cache_flush(PVFS_object_ref refn)
{
    int ret = -1;
    struct qlist_head *hash_link = NULL;
    mmap_ra_cache_elem_t *cache_elem = NULL;

    if (MMAP_RA_CACHE_INITIALIZED())
    {
        gen_mutex_lock(&s_mmap_ra_cache_mutex);
        hash_link = qhash_search_and_remove(s_key_to_data_table, &refn);
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, mmap_ra_cache_elem_t, hash_link);
            assert(cache_elem);
            assert(cache_elem->data);

            gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG, "Flushed mmap ra cache "
                         "element %llu, %d of size %llu\n",
                         llu(cache_elem->refn.handle),
                         cache_elem->refn.fs_id,
                         llu(cache_elem->data_sz));

            free(cache_elem->data);
            free(cache_elem);
            ret = 0;
        }
        gen_mutex_unlock(&s_mmap_ra_cache_mutex);
    }
    return ret;
}

int pvfs2_mmap_ra_cache_finalize(void)
{
    int ret = -1, i = 0;

    struct qlist_head *hash_link = NULL;
    mmap_ra_cache_elem_t *cache_elem = NULL;

    if (MMAP_RA_CACHE_INITIALIZED())
    {
        gen_mutex_lock(&s_mmap_ra_cache_mutex);
        for(i = 0; i < s_key_to_data_table->table_size; i++)
        {
            do
            {
                hash_link = qhash_search_and_remove_at_index(
                    s_key_to_data_table,i);
                if (hash_link)
                {
                    cache_elem = qhash_entry(
                        hash_link, mmap_ra_cache_elem_t, hash_link);

                    assert(cache_elem);
                    assert(cache_elem->data);

                    free(cache_elem->data);
                    free(cache_elem);
                }
            } while(hash_link);
        }

        ret = 0;
        qhash_finalize(s_key_to_data_table);
        s_key_to_data_table = NULL;
        gen_mutex_unlock(&s_mmap_ra_cache_mutex);

        /* FIXME: race condition here */
        gen_mutex_destroy(&s_mmap_ra_cache_mutex);
        gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG, "mmap_ra_cache_finalized\n");
    }
    return ret;
}

/* hash_key()
 *
 * hash function for pinode_refns added to table
 *
 * returns integer offset into table
 */
static int hash_key(void *key, int table_size)
{
    unsigned long tmp = 0;
    PVFS_object_ref *refn = (PVFS_object_ref *)key;

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
static int hash_key_compare(void *key, struct qlist_head *link)
{
    mmap_ra_cache_elem_t *cache_elem = NULL;
    PVFS_object_ref *refn = (PVFS_object_ref *)key;

    cache_elem = qlist_entry(link, mmap_ra_cache_elem_t, hash_link);
    assert(cache_elem);

    return (((cache_elem->refn.handle == refn->handle) &&
             (cache_elem->refn.fs_id == refn->fs_id)) ? 1 : 0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
