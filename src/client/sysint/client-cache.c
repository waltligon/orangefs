#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

#include "client-cache.h"
#include <gossip.h>

client_cache_t cc;

static uint16_t blk_get_next_free(void);
static void * blk_getp_by_index(uint16_t index);
static void blk_push_free(uint16_t index);
static void blk_zero(void *voidp);

static int fent_evict_mru(void);
static int fent_evict_lru(void);
static cc_fent_t *fent_get_next_free(void);
static INLINE cc_fent_t *fent_getp_by_index(uint16_t index);
static void fent_ht_remove(cc_fent_t *fentp);
static void fent_ht_to_front(cc_fent_t *fentp);
static cc_fent_t *fent_insert(uint64_t fhandle,
                              uint32_t fsid,
                              PVFS_uid uid,
                              PVFS_gid gid);
static cc_fent_t *fent_lookup(uint64_t fhandle,
                              uint32_t fsid,
                              PVFS_uid uid,
                              PVFS_gid gid);
static void fent_lru_remove(cc_fent_t *fentp);
static void fent_lru_to_front(cc_fent_t *fentp);
static int fent_match_key(cc_fent_t *fentp,
                          uint64_t fhandle,
                          uint32_t fsid,
                          PVFS_uid uid,
                          PVFS_gid gid);
static int fent_remove(cc_fent_t *fentp);
static int fent_remove_all(void);
static int fent_remove_by_index(uint16_t index);
static int fent_remove_by_key(uint64_t fhandle,
                              uint32_t fsid,
                              PVFS_uid uid,
                              PVFS_gid gid);
static int ment_dirty_flush(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static int ment_dirty_flush_all(cc_mtbl_t *mtblp);
static int ment_dirty_flush_by_index(cc_mtbl_t *mtblp, uint16_t index);
static int ment_dirty_flush_by_key(cc_mtbl_t *mtblp, uint64_t tag);
static void ment_dirty_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static void ment_dirty_to_front(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static int ment_evict_mru(cc_mtbl_t *mtblp);
static int ment_evict_lru(cc_mtbl_t *mtblp);
static cc_ment_t *ment_get_next_free(cc_mtbl_t *mtblp);
static INLINE cc_ment_t *ment_getp_by_index(cc_mtbl_t *mtblp, uint16_t index);
static void ment_ht_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static void ment_ht_to_front(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static cc_ment_t *ment_insert(cc_mtbl_t *mtblp, uint64_t tag);
static cc_ment_t *ment_lookup(cc_mtbl_t *mtblp, uint64_t tag);
static void ment_lru_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static void ment_lru_to_front(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static int ment_match_key(cc_ment_t *mentp, uint64_t tag);
static int ment_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp);
static int ment_remove_by_index(cc_mtbl_t *mtblp, uint16_t index);
static int ment_remove_by_key(cc_mtbl_t *mtblp, uint64_t tag);

static int mtbl_fini(cc_mtbl_t *mtblp);
static int mtbl_init(cc_mtbl_t *mtblp,
                     uint16_t ment_limit,
                     uint16_t ment_ht_limit,
                     uint16_t fent_index);

static uint16_t blk_get_next_free(void)
{
    cc_free_block_t *free_blkp = NULL;
    uint16_t free_blk = NIL16;

    /* If there are no more free blocks then evict the LRU ment of the LRU
     * block */
    if(cc.free_blk == NIL16)
    {
        cc_fent_t * fentp = NULL;
        int ret = 0;

        /* TODO: Check return value of eviction */
        fentp = fent_getp_by_index(cc.ftbl.lru);
        assert(fentp);
        ret = ment_evict_lru(&fentp->mtbl);
        assert(ret == 0);
    }

    /* Remove Block from the free block list. */
    free_blk = cc.free_blk;
    free_blkp = (cc_free_block_t *) blk_getp_by_index(free_blk);
    assert(free_blkp);
    cc.free_blk = free_blkp->next;

    /* Zero out all bytes in this block */
    blk_zero((void *) free_blkp);

    return free_blk;
}

static void * blk_getp_by_index(uint16_t index)
{
    if(index < cc.num_blks)
    {
        return cc.blks + (index * cc.blk_size);
    }
    return NULL;
}

static void blk_push_free(uint16_t index)
{
    cc_free_block_t *blk = NULL;

    blk = (cc_free_block_t *) blk_getp_by_index(index);
    assert(blk);

    blk->next = cc.free_blk;
    cc.free_blk = index;
}

static void blk_zero(void *voidp)
{
    assert(voidp);

    memset(voidp, 0, cc.blk_size);
}

static int fent_evict_mru(void)
{
    gossip_err("%s: cc.ftbl.mru = %hu\n", __func__, cc.ftbl.mru);
    assert(cc.ftbl.mru != NIL16);
    return fent_remove_by_index(cc.ftbl.mru);
}

static int fent_evict_lru(void)
{
    //printf("%s: cc.ftbl.lru = %hu\n", __func__, cc.ftbl.lru);
    assert(cc.ftbl.lru != NIL16);
    return fent_remove_by_index(cc.ftbl.lru);
}

static cc_fent_t *fent_get_next_free(void)
{
    cc_fent_t *fentp = NULL;

    if(cc.ftbl.free_fent == NIL16)
    {
        /* TODO attempt to evict file */
        fent_evict_lru();
    }

    fentp = fent_getp_by_index(cc.ftbl.free_fent);
    assert(fentp);
    cc.ftbl.free_fent = fentp->next;

    /* Initialize ht DLL values*/
    fentp->prev = NIL16;
    fentp->next = NIL16;

    /* Initialize recently used list values. */
    fentp->ru_prev = NIL16;
    fentp->ru_next = NIL16;

    return fentp;
}

static INLINE cc_fent_t *fent_getp_by_index(uint16_t index)
{
    if(index < cc.fent_limit)
    {
        return cc.ftbl.fents + index;
    }
    return NULL;
}

static void fent_ht_remove(cc_fent_t *fentp)
{
    cc_fent_t *prev = NULL;
    cc_fent_t *next = NULL;
    uint16_t bucket = NIL16;

    assert(fentp);

    bucket = (fentp->file_handle + fentp->fsid) % cc.fent_ht_limit;

    prev = fent_getp_by_index(fentp->prev);
    next = fent_getp_by_index(fentp->next);

    fentp->prev = NIL16;
    fentp->next = NIL16;

    if(prev != NULL && next != NULL)
    {
        /* This entry is in between other entries, so we need to stitch the hole
         * in the DLL that would be created by removing this item. */
        /* There is no need to update the index stored in the bucket */
        next->prev = prev->index;
        prev->next = next->index;
    }
    if(prev == NULL && next == NULL)
    {
        /* Test if this is the only item on the DLL, or this is a new
         * entry that cannot be removed. */
        if(cc.ftbl.fents_ht[bucket] == fentp->index)
        {
            /* Must be the only item on the DLL. */
            cc.ftbl.fents_ht[bucket] = NIL16;
        }
        else
        {
            /* This entry must be a new entry, so do nothing. */
        }
    }
    else if(prev == NULL)
    {
        /* The first of multiple items on the DLL (aka the MRU entry). */
        cc.ftbl.fents_ht[bucket] = next->index;
        next->prev = NIL16;
    }
    else if(next == NULL)
    {
        /* The last of multiple items on the DLL (aka the LRU entry) */
        prev->next = NIL16;
    }
    else
    {
        /* Shouldn't get here! */
        assert(0);
    }
}

static void fent_ht_to_front(cc_fent_t *fentp)
{
    cc_fent_t *old_head_fentp = NULL;
    uint16_t bucket = NIL16;

    assert(fentp);

    fent_ht_remove(fentp);

    bucket = (fentp->file_handle + fentp->fsid) % cc.fent_ht_limit;
    old_head_fentp = fent_getp_by_index(cc.ftbl.fents_ht[bucket]);

    if(old_head_fentp == NULL)
    {
        /* Previously, no entries in the DLL */
        cc.ftbl.fents_ht[bucket] = fentp->index;
        /* prev and next should already be NIL16 due to fent_ht_remove */
    }
    else
    {
        /* Previously, entry(s) in DLL. */
        cc.ftbl.fents_ht[bucket] = fentp->index;
        fentp->next = old_head_fentp->index;
        old_head_fentp->prev = fentp->index;
        /* fentp->prev should already be NIL16 due to fent_ht_remove */
    }
}

static cc_fent_t *fent_insert(uint64_t fhandle,
                              uint32_t fsid,
                              PVFS_uid uid,
                              PVFS_gid gid)
{
    cc_fent_t * new_fentp = NULL;
    int ret = 0;

    new_fentp = fent_get_next_free();
    if(new_fentp == NULL)
    {
        return NULL;
    }

    /* Fill in fent values */
    new_fentp->file_handle = fhandle;
    new_fentp->fsid = fsid;
    new_fentp->uid = uid;
    new_fentp->gid = gid;
    /* Note: prev, next, ru_prev, and ru_next already initialized to NIL16 */

    /* Allocate and Initialize mtbl */
    ret = mtbl_init(&new_fentp->mtbl,
                    cc.ment_limit,
                    cc.ment_ht_limit,
                    new_fentp->index);
    if(ret < 0)
    {
        return NULL;
    }

    fent_ht_to_front(new_fentp);
    fent_lru_to_front(new_fentp);

    return new_fentp;
}

static cc_fent_t *fent_lookup(uint64_t fhandle,
                              uint32_t fsid,
                              PVFS_uid uid,
                              PVFS_gid gid)
{
    uint16_t bucket = 0;
    cc_fent_t *fentp = NULL;

    bucket = (fhandle + fsid + uid + gid) % cc.fent_ht_limit;

    fentp = fent_getp_by_index(cc.ftbl.fents_ht[bucket]);
    while(fentp != NULL)
    {
        if(fent_match_key(fentp, fhandle, fsid, uid, gid))
        {
            fent_ht_to_front(fentp);
            return fentp;
        }
        fentp = fent_getp_by_index(fentp->next);
    }

    return NULL;
}

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
    }
    else if(next == NULL)
    {
        /* The last of multiple items on the LRU list (aka the LRU entry) */
        cc.ftbl.lru = prev->index;
        prev->ru_next = NIL16;
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

static int fent_match_key(cc_fent_t *fentp,
                          uint64_t fhandle,
                          uint32_t fsid,
                          PVFS_uid uid,
                          PVFS_gid gid)
{
    assert(fentp);
    if(fentp->file_handle == fhandle && fentp->fsid == fsid &&
       fentp->uid == uid && fentp->gid == gid)
    {
        return 1;
    }
    return 0;
}

static int fent_remove(cc_fent_t *fentp)
{
    assert(fentp != NULL);

    /* TODO: Check error codes... */
    mtbl_fini(&fentp->mtbl);

    /* Remove from fents_ht */
    fent_ht_remove(fentp);

    /* Remove from LRU/MRU DLL */
    fent_lru_remove(fentp);

    /* Place back on fent free list */
    fentp->next = cc.ftbl.free_fent;
    cc.ftbl.free_fent = fentp->index;

    return 0;
}

static int fent_remove_all(void)
{
    int ret = 0;
    while(cc.ftbl.mru != NIL16)
    {
        ret = fent_remove_by_index(cc.ftbl.mru);
        if(ret == 0)
        {
            return -1;
        }
    }
    return 0;
}

static int fent_remove_by_index(uint16_t index)
{
    cc_fent_t *fentp = fent_getp_by_index(index);

    if(fentp == NULL)
    {
        return -1;
    }
    return fent_remove(fentp);
}

static int fent_remove_by_key(uint64_t fhandle,
                              uint32_t fsid,
                              PVFS_uid uid,
                              PVFS_gid gid)
{
    cc_fent_t *fentp = fent_lookup(fhandle, fsid, uid, gid);

    if(fentp == NULL)
    {
        return -1;
    }
    return fent_remove(fentp);
}

static int ment_dirty_flush(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    int ret = 0;

    assert(mtblp && mentp);

    if(mentp->dirty)
    {
        /* TODO Flush dirty block data */
        
        ment_dirty_remove(mtblp, mentp);
    }

    return 0;
}

static int ment_dirty_flush_all(cc_mtbl_t *mtblp)
{
    int ret = 0;

    assert(mtblp);

    while(mtblp->dirty_first != NIL16)
    {
        ret = ment_dirty_flush_by_index(mtblp, mtblp->dirty_first);
        if(ret < 0)
        {
            /* Error */
            return -1;
        }
    }
    return 0;
}

static int ment_dirty_flush_by_index(cc_mtbl_t *mtblp, uint16_t index)
{
    assert(mtblp);
    return ment_dirty_flush(mtblp, ment_getp_by_index(mtblp, index));
}

static int ment_dirty_flush_by_key(cc_mtbl_t *mtblp, uint64_t tag)
{
    cc_ment_t *mentp = NULL;

    assert(mtblp);

    mentp = ment_lookup(mtblp, tag);

    if(mentp == NULL)
    {
        return -1;
    }

    return ment_dirty_flush(mtblp, mentp);
}

static void ment_dirty_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    cc_ment_t *dirty_prev = NULL;
    cc_ment_t *dirty_next = NULL;

    assert(mtblp && mentp);

    dirty_prev = ment_getp_by_index(mtblp, mentp->dirty_prev);
    dirty_next = ment_getp_by_index(mtblp, mentp->dirty_next);

    mentp->dirty_prev = NIL16;
    mentp->dirty_next = NIL16;
    mentp->dirty = 0;

    if(dirty_prev != NULL && dirty_next != NULL)
    {
        /* This entry is in between other entries, so we need to stitch the hole
         * in the DLL that would be created by removing this item. */
        dirty_next->dirty_prev = dirty_prev->index;
        dirty_prev->dirty_next = dirty_next->index;
    }
    if(dirty_prev == NULL && dirty_next == NULL)
    {
        /* Test if this is the only item on the DLL, or this is a new
         * entry that cannot be removed. */
        if(mtblp->dirty_first == mentp->index)
        {
            /* Must be the only item on the DLL. */
            mtblp->dirty_first = NIL16;
        }
        else
        {
            /* This entry must be a new entry, so do nothing. */
        }
    }
    else if(dirty_prev == NULL)
    {
        /* The first of multiple items on the DLL (aka the MRU entry). */
        mtblp->dirty_first = dirty_next->index;
        dirty_next->dirty_prev = NIL16;
    }
    else if(dirty_next == NULL)
    {
        /* The last of multiple items on the DLL (aka the LRU entry) */
        dirty_prev->dirty_next = NIL16;
    }
    else
    {
        /* Shouldn't get here! */
        assert(0);
    }
}

static void ment_dirty_to_front(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    cc_ment_t *old_dirty_first_mentp = NULL;

    assert(mtblp && mentp);

    ment_dirty_remove(mtblp, mentp);

    old_dirty_first_mentp = ment_getp_by_index(mtblp, mtblp->dirty_first);

    if(old_dirty_first_mentp == NULL)
    {
        /* Previously, no entries in the DLL */
        mtblp->dirty_first = mentp->index;
        /* dirty_prev and dirty_next should already be NIL16 due to ment_dirty_remove */
    }
    else
    {
        /* Previously, entry(s) in DLL. */
        mtblp->dirty_first = mentp->index;
        mentp->dirty_next = old_dirty_first_mentp->index;
        old_dirty_first_mentp->dirty_prev = mentp->index;
        /* mentp->dirty_prev should already be NIL16 due to ment_dirty_remove */
    }

    mentp->dirty = 1;
}

static int ment_evict_mru(cc_mtbl_t *mtblp)
{
    gossip_err("%s: mtblp->mru = %hu\n", __func__, mtblp->mru);
    assert(mtblp->mru != NIL16);
    return ment_remove_by_index(mtblp, mtblp->mru);
}

static int ment_evict_lru(cc_mtbl_t *mtblp)
{
    //printf("%s: mtblp->lru = %hu\n", __func__, mtblp->lru);
    assert(mtblp->lru != NIL16);
    return ment_remove_by_index(mtblp, mtblp->lru);
}

static cc_ment_t *ment_get_next_free(cc_mtbl_t *mtblp)
{
    cc_ment_t *mentp = NULL;
    uint16_t blk_index = 0;

    assert(mtblp);

    /* If no free ment, then evict LRU ment of this fent/mtbl */
    if(mtblp->free_ment == NIL16)
    {
        ment_evict_lru(mtblp);
    }

    /* Get next free block (if necessary, blk_get_next_free() evicts LRU ment
     * of LRU fent to evict the overall LRU block) */
    blk_index = blk_get_next_free();

    mentp = ment_getp_by_index(mtblp, mtblp->free_ment);
    assert(mentp);
    mtblp->free_ment = mentp->next;

    /* Associate block with the ment */
    mentp->blk_index = blk_index;

    /* Initialize ht DLL values*/
    mentp->prev = NIL16;
    mentp->next = NIL16;

    /* Initialize recently used list values. */
    mentp->ru_prev = NIL16;
    mentp->ru_next = NIL16;

    /* Initialize dirty list values */
    mentp->dirty_prev = NIL16;
    mentp->dirty_next = NIL16;
    mentp->dirty = 0;

    return mentp;
}

static INLINE cc_ment_t *ment_getp_by_index(cc_mtbl_t *mtblp, uint16_t index)
{
    if(index < cc.ment_limit)
    {
        return mtblp->ments + index;
    }
    return NULL;
}

static void ment_ht_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    cc_ment_t *prev = NULL;
    cc_ment_t *next = NULL;
    uint16_t bucket = NIL16;

    assert(mentp);

    bucket = mentp->tag % cc.ment_ht_limit;

    prev = ment_getp_by_index(mtblp, mentp->prev);
    next = ment_getp_by_index(mtblp, mentp->next);

    mentp->prev = NIL16;
    mentp->next = NIL16;

    if(prev != NULL && next != NULL)
    {
        /* This entry is in between other entries, so we need to stitch the hole
         * in the DLL that would be created by removing this item. */
        /* There is no need to update the index stored in the bucket */
        next->prev = prev->index;
        prev->next = next->index;
    }
    if(prev == NULL && next == NULL)
    {
        /* Test if this is the only item on the DLL, or this is a new
         * entry that cannot be removed. */
        if(mtblp->ments_ht[bucket] == mentp->index)
        {
            /* Must be the only item on the DLL. */
            mtblp->ments_ht[bucket] = NIL16;
        }
        else
        {
            /* This entry must be a new entry, so do nothing. */
        }
    }
    else if(prev == NULL)
    {
        /* The first of multiple items on the DLL (aka the MRU entry). */
        mtblp->ments_ht[bucket] = next->index;
        next->prev = NIL16;
    }
    else if(next == NULL)
    {
        /* The last of multiple items on the DLL (aka the LRU entry) */
        prev->next = NIL16;
    }
    else
    {
        /* Shouldn't get here! */
        assert(0);
    }
}

static void ment_ht_to_front(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    cc_ment_t *old_head_mentp = NULL;
    uint16_t bucket = NIL16;

    assert(mtblp && mentp);

    ment_ht_remove(mtblp, mentp);

    bucket = mentp->tag % cc.ment_ht_limit;
    old_head_mentp = ment_getp_by_index(mtblp, mtblp->ments_ht[bucket]);

    if(old_head_mentp == NULL)
    {
        /* Previously, no entries in the DLL */
        mtblp->ments_ht[bucket] = mentp->index;
        /* prev and next should already be NIL16 due to ment_ht_remove */
    }
    else
    {
        /* Previously, entry(s) in DLL. */
        mtblp->ments_ht[bucket] = mentp->index;
        mentp->next = old_head_mentp->index;
        old_head_mentp->prev = mentp->index;
        /* mentp->prev should already be NIL16 due to ment_ht_remove */
    }
}

static cc_ment_t *ment_insert(cc_mtbl_t *mtblp, uint64_t tag)
{
    cc_ment_t * new_mentp = NULL;
    int ret = 0;

    new_mentp = ment_get_next_free(mtblp);
    assert(new_mentp);
    mtblp->num_blks++;

    /* Fill in ment values */
    new_mentp->tag = tag;
    /* Note: DLL items already initialized to NIL16 */


    /* TODO perform R/W here or outside this function? */

    ment_ht_to_front(mtblp, new_mentp);
    ment_lru_to_front(mtblp, new_mentp);

    return new_mentp;
}

static cc_ment_t *ment_lookup(cc_mtbl_t *mtblp, uint64_t tag)
{
    uint16_t bucket = 0;
    cc_ment_t *mentp = NULL;

    assert(mtblp);

    bucket = tag % cc.ment_ht_limit;

    mentp = ment_getp_by_index(mtblp, mtblp->ments_ht[bucket]);
    while(mentp != NULL)
    {
        if(ment_match_key(mentp, tag))
        {
            ment_ht_to_front(mtblp, mentp);
            return mentp;
        }
        mentp = ment_getp_by_index(mtblp, mentp->next);
    }

    return NULL;
}

static void ment_lru_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    cc_ment_t *prev = NULL;
    cc_ment_t *next = NULL;

    assert(mtblp && mentp);

    prev = ment_getp_by_index(mtblp, mentp->ru_prev);
    next = ment_getp_by_index(mtblp, mentp->ru_next);

    mentp->ru_prev = NIL16;
    mentp->ru_next = NIL16;

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
        if(mtblp->mru == mentp->index || mtblp->lru == mentp->index)
        {
            /* Must be the only item on the LRU/MRU list. */
            assert(mtblp->mru == mentp->index && mtblp->lru == mentp->index);
            mtblp->mru = NIL16;
            mtblp->lru = NIL16;
        }
        else
        {
            /* This entry must be a new entry, so do nothing. */
        }
    }
    else if(prev == NULL)
    {
        /* The first of multiple items on the LRU list (aka the MRU entry). */
        mtblp->mru = next->index;
        next->ru_prev = NIL16;
    }
    else if(next == NULL)
    {
        /* The last of multiple items on the LRU list (aka the LRU entry) */
        mtblp->lru = prev->index;
        prev->ru_next = NIL16;
    }
    else
    {
        /* Shouldn't get here! */
        assert(0);
    }
    
}

static void ment_lru_to_front(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    cc_ment_t * mru_mentp = NULL;
    cc_ment_t * lru_mentp = NULL;

    assert(mtblp && mentp);

    ment_lru_remove(mtblp, mentp);

    mru_mentp = ment_getp_by_index(mtblp, mtblp->mru);
    lru_mentp = ment_getp_by_index(mtblp, mtblp->lru);

    if(mru_mentp == NULL && lru_mentp == NULL)
    {
        /* Previously, no entries in the DLL */
        mtblp->mru = mentp->index;
        mtblp->lru = mentp->index;
        /* ru_prev and ru_next should already be NIL16 due to ment_lru_remove */
    }
    else if(mru_mentp == lru_mentp)
    {
        /* Previously, only one entry in the DLL. */
        mtblp->mru = mentp->index;
        mentp->ru_next = lru_mentp->index;
        lru_mentp->ru_prev = mentp->index;
        /* fentp->ru_prev should already be NIL16 due to ment_lru_remove */
    }
    else
    {
        /* Previously, multiple items in DLL. (MRU and LRU are different) */
        mtblp->mru = mentp->index;
        mentp->ru_next = mru_mentp->index;
        mru_mentp->ru_prev = mentp->index;
        /* fentp->ru_prev should already be NIL16 due to fent_lru_remove */
    }
}

static int ment_match_key(cc_ment_t *mentp, uint64_t tag)
{
    assert(mentp);
    if(mentp->tag == tag)
    {
        return 1;
    }
    return 0;
}

static int ment_remove(cc_mtbl_t *mtblp, cc_ment_t *mentp)
{
    assert(mtblp && mentp);

    /* Flushes and removes the entry from the dirty list (if dirty) */
    ment_dirty_flush(mtblp, mentp);

    /* Place the block back on the block free list */
    blk_push_free(mentp->blk_index);

    /* Decrement number of blocks this fent has cached */
    mtblp->num_blks--;

    /* Remove from fents_ht */
    ment_ht_remove(mtblp, mentp);

    /* Remove from LRU/MRU DLL */
    ment_lru_remove(mtblp, mentp);

    /* Place back on fent free list */
    mentp->next = mtblp->free_ment;
    mtblp->free_ment = mentp->index;

    return 0;
}

static int ment_remove_by_index(cc_mtbl_t *mtblp, uint16_t index)
{
    cc_ment_t *mentp = NULL;

    assert(mtblp);

    mentp = ment_getp_by_index(mtblp, index);
    if(mentp == NULL)
    {
        return -1;
    }

    return ment_remove(mtblp, mentp);
}

static int ment_remove_by_key(cc_mtbl_t *mtblp, uint64_t tag)
{
    cc_ment_t *mentp = NULL;

    assert(mtblp);

    mentp = ment_lookup(mtblp, tag);
    if(mentp == NULL)
    {
        return -1;
    }

    return ment_remove(mtblp, mentp);
}

static int mtbl_fini(cc_mtbl_t *mtblp)
{
    int ret = 0;

    assert(mtblp);

    /* Just flush all instead of removing all, since once the dirty data is
     * flushed, the rest of the memory entrees can be freed without needing
     * to do all that ment_remove does. */
    /* TODO Check return code, log error, and resume gracefully if possible. */
    ment_dirty_flush_all(mtblp);

    assert(mtblp->dirty_first == NIL16);

    free(mtblp->ments);
    free(mtblp->ments_ht);
    return 0;
}

static int mtbl_init(cc_mtbl_t *mtblp,
                     uint16_t ment_limit,
                     uint16_t ment_ht_limit,
                     uint16_t fent_index)
{
    cc_ment_t *mentp = NULL;
    unsigned int i = 0;

    assert(mtblp);

    /* TODO: We could support embedding the ment_limit and ment_ht_limit to
     * support custom memory table limits on a per file basis via hints.*/
#if 0
    mtblp->ment_limit = ment_limit;
    mtblp->ment_ht_limit = ment_ht_limit;
#endif

    /* Allocate memory for ments */
    mtblp->ments = malloc(ment_limit * sizeof(cc_ment_t));
    if(mtblp->ments == NULL)
    {
        gossip_err("%s: ERROR allocating memory for ments!\n", __func__);
        return -1;
    }

#if 0
    gossip_err("%s: ments bytes = %llu\n",
           __func__,
           (long long unsigned int) ment_limit * sizeof(cc_ment_t));
#endif

    /* Setup free ments LL */
#if 0
    gossip_err("%s: first ment address = %p\n", __func__, mtblp->ments);
#endif
    mtblp->free_ment = 0;
    for(i = 0, mentp = mtblp->ments;
        i < (ment_limit - 1);
        mentp++, i++)
    {
#if 0
        gossip_err("%s: ment address = %p\n", __func__, mentp);
#endif
        mentp->index = i;
        mentp->next = i + 1;
    }
    mentp->index = i;
    mentp->next = NIL16;

    /* Allocate memory for ments ht */
    mtblp->ments_ht = malloc(ment_ht_limit * sizeof(uint16_t));
    if(mtblp->ments_ht == NULL)
    {
        fprintf(stderr,
                "%s: ERROR allocating memory for ments_ht!\n",
                __func__);
        return -1;
    }

    /* Set all hash table buckets to NIL16 */
    memset(mtblp->ments_ht, NIL8, ment_ht_limit * sizeof(uint16_t));
#if 0
    gossip_err("%s: ments_ht bytes = %llu\n",
           __func__,
           (long long unsigned int) ment_ht_limit * sizeof(uint16_t));
#endif

    mtblp->max_offset_seen = 0;
    mtblp->num_blks = 0;
    mtblp->mru = NIL16;
    mtblp->lru = NIL16;
    mtblp->dirty_first = NIL16;
    mtblp->ref_cnt = 0;
    mtblp->fent_index = fent_index;

    return 0;
}

void PINT_client_cache_debug_flags(PINT_client_cache_flag flags)
{
    gossip_err("PINT_client_cache_flag:\n"
               "\tCLIENT_CACHE_ENABLED = %c\n"
               "\tCLIENT_CACHE_DEFAULT_CACHE = %c\n"
               "\tCLIENT_CACHE_FOR_CLIENT_CORE = %c\n",
               (cc.status & CLIENT_CACHE_ENABLED) ? 'T' : 'F',
               (cc.status & CLIENT_CACHE_DEFAULT_CACHE) ? 'T' : 'F',
               (cc.status & CLIENT_CACHE_FOR_CLIENT_CORE) ? 'T' : 'F');
}

int PINT_client_cache_finalize(void)
{
    /* int ret = 0; */

    gossip_err("%s\n", __func__);

    while(cc.ftbl.mru != NIL16)
    {
        fent_remove_by_index(cc.ftbl.mru);
    }

    /* Free blks memory region */
    free(cc.blks);

    /* Free fents memory region: */
    /* TODO delve into fent and free underlying allocated structs like
     * ments. */
    free(cc.ftbl.fents);

    /* Free fents_ht memory region: */
    free(cc.ftbl.fents_ht);

    return 0;
}

int PINT_client_cache_initialize(
    PINT_client_cache_flag flags,
    uint64_t cache_size,
    uint64_t block_size,
    uint16_t fent_limit,
    uint16_t fent_ht_limit,
    uint16_t ment_limit,
    uint16_t ment_ht_limit)
{
    void *voidp = NULL;
    cc_fent_t *fentp = NULL;
    /* int ret = 0; */
    int i = 0;

    gossip_err("%s\n", __func__);

    if(flags == CLIENT_CACHE_NONE || !(flags & CLIENT_CACHE_ENABLED))
    {
        return 0;
    }
    else
    {
        cc.status = flags;
    }

    PINT_client_cache_debug_flags(cc.status);

    /* Store limits w/ cc */
    cc.blk_size = block_size;
    cc.fent_limit = fent_limit;
    cc.fent_ht_limit = fent_ht_limit;
    cc.fent_size = (uint16_t) sizeof(cc_fent_t);
    cc.ment_limit = ment_limit;
    cc.ment_ht_limit = ment_ht_limit;
    cc.ment_size = (uint16_t) sizeof(cc_ment_t);

    gossip_err("%s: block_size = %llu\n",
           __func__,
           (long long unsigned int) cc.blk_size);
    gossip_err("%s: fent_limit = %hu\n", __func__, cc.fent_limit);
    gossip_err("%s: fent_ht_limit = %hu\n", __func__, cc.fent_ht_limit);
    gossip_err("%s: fent_size = %hu\n", __func__, cc.fent_size);
    gossip_err("%s: ment_limit = %hu\n", __func__, cc.ment_limit);
    gossip_err("%s: ment_ht_limit = %hu\n", __func__, cc.ment_ht_limit);
    gossip_err("%s: ment_size = %hu\n", __func__, cc.ment_size);

    /* Allocate memory for blocks. */
    cc.num_blks = cache_size / block_size;
    gossip_err("%s: num_blocks = %d\n", __func__, cc.num_blks);
    if(cc.num_blks == 0)
    {
        gossip_err("%s: WARN num_blocks is ZERO!\n", __func__);
        return 0;
    }
    else if(cc.num_blks < cc.ment_limit)
    {
        fprintf(stderr,
                "%s: WARN Calculated num_blks is less than memory entry\n"
                "limit! You may want to increase your cache size, reduce your\n"
                "block size, or both.\n",
                __func__);
    }

    cc.blks = calloc(1, cc.num_blks * block_size);
    if(cc.blks == NULL)
    {
        gossip_err("%s: ERROR allocating memory for blks!\n", __func__);
        return -1;
    }

    cc.cache_size = cc.num_blks * block_size;
    gossip_err("%s: cacheable bytes = %llu = %Lf MiB\n",
           __func__,
           (long long unsigned int) cc.cache_size,
           ((long double) (cc.cache_size) / (1024.0 * 1024.0)));

    /* Setup free blocks LL */
    //printf("%s: first block address = %p\n", __func__, cc.blks);
    cc.free_blk = 0;
    for(i = 0, voidp = cc.blks;
        i < (cc.num_blks - 1);
        voidp += cc.blk_size, i++)
    {
        /* gossip_err("%s: block address = %p\n", __func__, voidp); */
        ((cc_free_block_t *) voidp)->next = i + 1;
    }
    ((cc_free_block_t *) voidp)->next = NIL16;

    /* Allocate memory for fents */
    cc.ftbl.fents = malloc(fent_limit * sizeof(cc_fent_t));
    if(cc.ftbl.fents == NULL)
    {
        gossip_err("%s: ERROR allocating memory for fents!\n", __func__);
        return -1;
    }
    gossip_err("%s: fents bytes = %llu\n",
           __func__,
           (long long unsigned int) fent_limit * sizeof(cc_fent_t));

    /* Setup free fents LL */
    //printf("%s: first fent address = %p\n", __func__, cc.ftbl.fents);
    cc.ftbl.free_fent = 0;
    for(i = 0, fentp = cc.ftbl.fents;
        i < (cc.fent_limit - 1);
        fentp++, i++)
    {
        //printf("%s: fent address = %p\n", __func__, fentp);
        fentp->index = i;
        fentp->next = i + 1;
    }
    fentp->index = i;
    fentp->next = NIL16;

    /* Allocate memory for fents ht */
    cc.ftbl.fents_ht = malloc(fent_ht_limit * sizeof(uint16_t));
    if(cc.ftbl.fents_ht == NULL)
    {
        fprintf(stderr,
                "%s: ERROR allocating memory for fents_ht!\n",
                __func__);
        return -1;
    }

    /* Set all hash table buckets to NIL16 */
    memset((void *) cc.ftbl.fents_ht, NIL8, fent_ht_limit * sizeof(uint16_t));
    gossip_err("%s: fents_ht bytes = %llu\n",
           __func__,
           (long long unsigned int) fent_ht_limit * sizeof(uint16_t));

    /* Set ftbl mru and lru */
    cc.ftbl.mru = NIL16;
    cc.ftbl.lru = NIL16;

    return 0;
}

int PINT_client_cache_initialize_defaults(void)
{
    return PINT_client_cache_initialize(CLIENT_CACHE_ENABLED |
                                        CLIENT_CACHE_DEFAULT_CACHE |
                                        CLIENT_CACHE_FOR_CLIENT_CORE,
                                        BYTE_LIMIT,
                                        BLOCK_SIZE_B,
                                        FENT_LIMIT,
                                        FENT_HT_LIMIT,
                                        MENT_LIMIT,
                                        MENT_HT_LIMIT);
}
