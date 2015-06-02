#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>

#include "client-cache.h"

client_cache_t cc;

static void fent_lru_to_front(cc_fent_t *fentp);
static void fent_lru_remove(cc_fent_t *fentp);

static cc_fent_t *fent_get_next_free();
static int fent_match_key(cc_fent_t *fentp, uint64_t fhandle, uint32_t fsid);
static cc_fent_t *fent_getp_by_index(uint16_t index);

cc_fent_t *fent_lookup(uint64_t fhandle, uint32_t fsid);
cc_fent_t *fent_insert(uint64_t fhandle, uint32_t fsid);
int fent_remove_by_key(uint64_t fhandle, uint32_t fsid);
int fent_remove(cc_fent_t *fentp);

static void fent_lru_remove(cc_fent_t *fentp)
{
    cc_fent_t *prev = NULL;
    cc_fent_t *next = NULL;

    assert(fentp);

    prev = fent_getp_by_index(fentp->ru_prev);
    next = fent_getp_by_index(fentp->ru_next);

    fentp->ru_prev = NIL16;
    fentp->ru_next = NIL16;

    if(prev != NULL && next != NULL)
    {
        /* This entry is in between other entries, so we need to stitch the hole
         * in the DLL that would be created by removing this item. */
        /* There is no need to update MRU and LRU indexes. */
        next->ru_prev = prev->index;
        prev->ru_next = next->index;
    }
    if(prev == NULL && next == NULL)
    {
        /* Test if this is the only item on the LRU/MRU list, or this is a new
         * entry that cannot be removed. */
        if(cc.ftbl.mru == fentp->index || cc.ftbl.lru == fentp->index)
        {
            /* Must be the only item on the LRU/MRU list. */
            assert(cc.ftbl.mru == fentp->index && cc.ftbl.lru == fentp->index);
            cc.ftbl.mru = NIL16;
            cc.ftbl.lru = NIL16;
        }
        else
        {
            /* This entry must be a new entry, so do nothing. */
        }
    }
    else if(prev == NULL)
    {
        /* The first of multiple items on the LRU list (aka the MRU entry). */
        cc.ftbl.mru = next->index;
        next->ru_prev = NIL16;
        if(next->ru_next == NIL16)
        {
            cc.ftbl.lru = next->index;
        }
    }
    else if(next == NULL)
    {
        /* The last of multiple items on the LRU list (aka the LRU entry) */
        cc.ftbl.lru = prev->index;
        prev->ru_next = NIL16;
        if(prev->ru_prev == NIL16)
        {
            cc.ftbl.mru = prev->index;
        }
    }
    else
    {
        /* Shouldn't get here! */
        assert(0);
    }
}

static void fent_lru_to_front(cc_fent_t *fentp)
{
    cc_fent_t * mru_fentp = NULL;
    cc_fent_t * lru_fentp = NULL;

    assert(fentp);

    fent_lru_remove(fentp);

    mru_fentp = fent_getp_by_index(cc.ftbl.mru);
    lru_fentp = fent_getp_by_index(cc.ftbl.lru);

    if(mru_fentp == NULL && lru_fentp == NULL)
    {
        /* Previously, no entries in the DLL */
        cc.ftbl.mru = fentp->index;
        cc.ftbl.lru = fentp->index;
        /* ru_prev and ru_next should already be NIL16 due to fent_lru_remove */
    }
    else if(mru_fentp == lru_fentp)
    {
        /* Previously, only one entry in the DLL. */
        cc.ftbl.mru = fentp->index;
        fentp->ru_next = lru_fentp->index;
        lru_fentp->ru_prev = fentp->index;
        /* fentp->ru_prev should already be NIL16 due to fent_lru_remove */
    }
    else
    {
        /* Previously, multiple items in DLL. (MRU and LRU are different) */
        cc.ftbl.mru = fentp->index;
        fentp->ru_next = mru_fentp->index;
        mru_fentp->ru_prev = fentp->index;
        /* fentp->ru_prev should already be NIL16 due to fent_lru_remove */
    }
}

static int fent_match_key(cc_fent_t *fentp, uint64_t fhandle, uint32_t fsid)
{
    assert(fentp);
    if(fentp->file_handle == fhandle && fentp->fsid == fsid)
    {
        return 1;
    }
    return 0;
}

static cc_fent_t *fent_getp_by_index(uint16_t index)
{
    if(index < cc.fent_limit)
    {
        return cc.ftbl.fents + index;
    }
    return NULL;
}

cc_fent_t *fent_lookup(uint64_t fhandle, uint32_t fsid)
{
    uint16_t bucket = 0;
    cc_fent_t *fentp = NULL;

    bucket = (fhandle + fsid) % cc.fent_ht_limit;

    fentp = fent_getp_by_index(cc.ftbl.fents_ht[bucket]);
    while(fentp != NULL)
    {
        if(fent_match_key(fentp, fhandle, fsid))
        {
            return fentp;
        }
        fentp = fent_getp_by_index(fentp->next);
    }

    return NULL;
}

static cc_fent_t *fent_get_next_free()
{
    cc_fent_t *fentp = NULL;

    if(cc.ftbl.free_fent == NIL16)
    {
        /* TODO attempt to evict file */
        return NULL;
    }

    fentp = fent_getp_by_index(cc.ftbl.free_fent);
    assert(fentp);
    cc.ftbl.free_fent = fentp->next;
    return fentp;
}

cc_fent_t *fent_insert(uint64_t fhandle, uint32_t fsid)
{
    cc_fent_t * new_fentp = NULL;
    cc_fent_t * old_head = NULL;
    uint16_t bucket = 0;

    new_fentp = fent_get_next_free();
    if(new_fentp == NULL)
    {
        return NULL;
    }

    /* Fill in fent values */
    new_fentp->file_handle = fhandle;
    new_fentp->fsid = fsid;
    new_fentp->file_size = 0;

    /* TODO */
    /* Allocate + Initialize mtbl */

    /* Insert at head of hash table chain */
    bucket = (fhandle + fsid) % cc.fent_ht_limit;
    old_head = fent_getp_by_index(cc.ftbl.fents_ht[bucket]);

    if(old_head == NULL)
    {
        new_fentp->next = NIL16;
    }
    else
    {
        new_fentp->next = old_head->index;
    }

    /* Insert at head of ht chain */
    cc.ftbl.fents_ht[bucket] = new_fentp->index;

    return new_fentp;
}

int init_cache(
    uint64_t cache_size,
    uint64_t block_size,
    uint16_t fent_limit,
    uint16_t fent_ht_limit,
    uint16_t ment_limit,
    uint16_t ment_ht_limit)
{
    uint16_t *uint16p = NULL;
    void *voidp = NULL;
    /* int ret = 0; */
    int i = 0;

    printf("%s\n", __func__);

    /* Store limits w/ cc */
    cc.block_size = block_size;
    cc.fent_limit = fent_limit;
    cc.fent_ht_limit = fent_ht_limit;
    cc.fent_size = (uint16_t) sizeof(cc_fent_t);
    cc.ment_limit = ment_limit;
    cc.ment_ht_limit = ment_ht_limit;
    cc.ment_size = (uint16_t) sizeof(cc_ment_t);

    printf("%s: block_size = %llu\n",
           __func__,
           (long long unsigned int) cc.block_size);
    printf("%s: fent_limit = %hu\n", __func__, cc.fent_limit);
    printf("%s: fent_ht_limit = %hu\n", __func__, cc.fent_ht_limit);
    printf("%s: fent_size = %hu\n", __func__, cc.fent_size);
    printf("%s: ment_limit = %hu\n", __func__, cc.ment_limit);
    printf("%s: ment_ht_limit = %hu\n", __func__, cc.ment_ht_limit);
    printf("%s: ment_size = %hu\n", __func__, cc.ment_size);

    /* Allocate memory for blocks. */
    cc.num_blocks = cache_size / block_size;
    printf("%s: num_blocks = %d\n", __func__, cc.num_blocks);
    if(cc.num_blocks == 0)
    {
        fprintf(stderr, "%s: WARN num_blocks is ZERO!\n", __func__);
        return 0;
    }
    cc.blks = calloc(1, cc.num_blocks * block_size);
    if(cc.blks == NULL)
    {
        fprintf(stderr, "%s: ERROR allocating memory for blks!\n", __func__);
        return -1;
    }

    cc.cache_size = cc.num_blocks * block_size;
    printf("%s: cacheable bytes = %llu = %Lf MiB\n",
           __func__,
           (long long unsigned int) cc.cache_size,
           ((long double) (cc.cache_size) / (1024.0 * 1024.0)));

    /* Setup free blocks LL */
    printf("%s: first block address = %p\n", __func__, cc.blks);
    cc.free_blk = 0;
    for(i = 0, voidp = cc.blks;
        i < (cc.num_blocks - 1);
        voidp += cc.block_size, i++)
    {
        /* printf("%s: block address = %p\n", __func__, voidp); */
        ((free_block_t *) voidp)->next = i + 1;
    }
    ((free_block_t *) voidp)->next = NIL16;

    /* Allocate memory for fents */
    cc.ftbl.fents = calloc(1, fent_limit * sizeof(cc_fent_t));
    if(cc.ftbl.fents == NULL)
    {
        fprintf(stderr, "%s: ERROR allocating memory for fents!\n", __func__);
        return -1;
    }
    printf("%s: fents bytes = %llu\n",
           __func__,
           (long long unsigned int) fent_limit * sizeof(cc_fent_t));

    /* Setup free fents LL */
    printf("%s: first fent address = %p\n", __func__, cc.ftbl.fents);
    cc.ftbl.free_fent = 0;
    for(i = 0, voidp = cc.ftbl.fents;
        i < (cc.fent_limit - 1);
        voidp += cc.fent_size, i++)
    {
        /* printf("%s: fent address = %p\n", __func__, voidp); */
        ((cc_fent_t *) voidp)->index = i;
        ((cc_fent_t *) voidp)->next = i + 1;
    }
    ((cc_fent_t *) voidp)->next = NIL16;

    /* Allocate memory for fents ht */
    cc.ftbl.fents_ht = calloc(1, fent_ht_limit * sizeof(uint16_t));
    if(cc.ftbl.fents_ht == NULL)
    {
        fprintf(stderr,
                "%s: ERROR allocating memory for fents_ht!\n",
                __func__);
        return -1;
    }

    /* Set all hash table buckets to NIL16 */
    for(i = 0, uint16p = cc.ftbl.fents_ht; i < fent_ht_limit; i++, uint16p++)
    {
        /* printf("%s: uint16p = %p\n", __func__, uint16p); */
        *uint16p = NIL16;
    }
    printf("%s: fents_ht bytes = %llu\n",
           __func__,
           (long long unsigned int) fent_ht_limit * sizeof(uint16_t));

    return 0;
}

int finalize_cache(void)
{
    /* int ret = 0; */
    /* int i = 0; */
    printf("%s\n", __func__);

    /* Flush all dirty blocks */
    /* TODO */

    /* Free blks memory region */
    if(cc.blks != NULL)
    {
        free(cc.blks);
    }

    /* Free fents memory region: */
    if(cc.ftbl.fents != NULL)
    {
        /* TODO delve into fent and free underlying allocated structs like
         * ments. */

        free(cc.ftbl.fents);
    }

    /* Free fents_ht memory region: */
    if(cc.ftbl.fents_ht != NULL)
    {
        free(cc.ftbl.fents_ht);
    }

    /* TODO */
    /* For every bucket, walk the list and...*/
        /* Free ments memory region */
        /* For every bucket, walk the list and */

    return 0;
}

int main(int argc, char** argv)
{
    int ret = 0;
    printf("%s\n", __func__);
    printf("FYI: sizeof(pthread_rwlock_t) = %zu\n", sizeof(pthread_rwlock_t));
    ret = init_cache(BYTE_LIMIT,
                     BLOCK_SIZE_B,
                     FENT_LIMIT,
                     FENT_HT_LIMIT,
                     MENT_LIMIT,
                     MENT_HT_LIMIT);
    if(ret != 0)
    {
        fprintf(stderr, "%s: init_cache returned %d\n", __func__, ret);
        finalize_cache();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
