/* Specific functions related to LRU cache policy */
#include <stdio.h>
#include <stdlib.h>

#include "internal.h"
#include "state.h"
#include "flags.h"
#include "cache.h"
#include "ncac-lru.h"


/* add an extent into a lru cache list. The caller should hold the lock 
 * of "cache". 
 */
void LRU_add_cache_item(struct cache_stack *cache,struct extent *extent)
{
    /* Insert an entry after the specified head "active_list". */
    list_add(&extent->lru, &cache->active_list);
    SetPageLRU(extent);
    extent->mapping->nrpages++;
    cache->nr_active++;
}

/* remove an extent from a lru cache list. The caller should hold the
 * the lock of cache.
 */
void LRU_remove_cache_item(struct cache_stack *cache, struct extent *extent)
{
    list_del(&extent->lru);
    extent->mapping->nrpages--;
    cache->nr_active--;
}

/* shrink the LRU cache list by discarding some extents from the list.
 * The expected number of extents discarded is "expected", while the
 * real number of discarded extents is "shrinked".
 */
int LRU_shrink_cache(struct cache_stack *cache, unsigned int expected,
                unsigned int *shrinked)
{
    struct list_head *lru_head, *lru_tail;
    struct extent *victim;
    int ret = 0;

    fprintf(stderr, "%s: expected:%d\n", __FUNCTION__, expected);
     
    *shrinked = 0;
    lru_head = &cache->active_list;
    lru_tail = lru_head->prev;

    while (*shrinked < expected && lru_tail != (& cache->active_list) ){
        victim = list_entry(lru_tail, struct extent, lru);

        if ( !PageLRU(victim) ){
            NCAC_error("extent flag is wrong. LRU flag is expected\n");
            ret = NCAC_INVAL_FLAGS;
            break;
        }

        lru_tail = lru_tail->prev;

        if (PageReadPending(victim) || PageWritePending(victim)){
            ret = NCAC_check_ioreq(victim);
            if (ret < 0){
                NCAC_error("NCAC_check_ioreq error: index=%ld, ioreq=%Ld\n",
                        victim->index, Ld(victim->ioreq));
                break;
            }

            if (ret) { /* completion */
                list_set_clean_page(victim);
            }
        }

        if ( is_extent_discardable(victim) ){
            LRU_remove_cache_item(cache, victim);
            list_add_tail(&victim->list, &cache->free_extent_list);
            *shrinked++;
        }
    }
    return ret;
}
