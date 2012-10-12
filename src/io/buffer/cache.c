/* common functions for cache management. These functions are shared
 * by all cache policies.
 */
#include <stdio.h>
#include <stdlib.h>

#include "internal.h"
#include "state.h"
#include "flags.h"
#include "cache.h"
#include "ncac-lru.h"

/*
 * lookup_cache_item: Given the index of an extent, look up whether this 
 * extent is cached or not.
 *     Cached: retured the extent 
 *     NOT cached: return NULL.
 */
struct extent * lookup_cache_item(struct inode *mapping, unsigned long index)
{
    struct extent *extent;

    extent = radix_tree_lookup(&mapping->page_tree, index);

    return extent;
}

/* ==================================================================
 * add an item into the cache:                                      *
 * (1) add an item into the radix tree (add_cache_item_no_policy).  *
 * (2) add an item into the cache (add_cache_item_with_policy).     *
 * ==================================================================
 */

/* add_cache_item_no_policy(): add an extent into the cache tree.
 * Each inode has a cache tree, protected by its "lock". 
 * NOT cache policy related.
 */
static inline int add_cache_item_no_policy(struct extent *extent, 
         struct inode *mapping, unsigned long index)
{
	int error;

	error = radix_tree_insert(&mapping->page_tree, index, extent); 
	if ( !error ) {
	    list_add(&extent->list, &mapping->clean_pages);
	    extent->mapping = mapping;
	    extent->index = index;
	    mapping->nrpages++;
	}
	return error;
}

/* add an item into a cache list with certain policy.
 * Current implementation is related to LRU. This function is
 * cache policy related.
 */
static inline void add_cache_item_with_policy(struct extent *extent, int cache_policy)
{
    struct cache_stack *cache_stack = NULL;

    cache_stack = get_extent_cache_stack(extent);

    switch (cache_policy) {
        case LRU_POLICY:
            LRU_add_cache_item(cache_stack, extent);
            break;

        default:
		    NCAC_error("unknown cache policy");
            break;
    }
}

/* add an extent into the cache. */
int add_cache_item(struct extent *extent, struct inode *mapping,
            unsigned long index, int policy)
{
    /* 1. bookkeeping in the radix tree */
    int ret = add_cache_item_no_policy(extent, mapping, index);

    /* 2. put into cache list with respect to the cache policy */ 
	if (ret == 0){
		add_cache_item_with_policy(extent, policy);
		return ret;
	}
    return ret;    
}

/* ==================================================================
 *                remove an item from the cache:					*
 * (1) remove it from the radix tree (remove_cache_item_no_policy). *
 * (2) remove it from its cache list.(remove_cache_item_with_policy)*
 * (3) add this item into the free extent list (add_free_extent_list_item	*
 * ================================================================== 
 */

static void remove_cache_item_no_policy(struct extent *extent)
{
    struct inode *mapping = extent->mapping;

    radix_tree_delete(&mapping->page_tree, extent->index);

    /* get this back if the "list" field is used */
    //list_del(&page->list);
    extent->mapping = NULL;
}

static void remove_cache_item_with_policy(struct extent *victim, int policy)
{
    struct cache_stack *cache;

    cache = get_extent_cache_stack(victim);
    if ( NULL == cache ){
        NCAC_error("extent cache stack is NULL");
        return;
    }
    switch (policy){
        case LRU_POLICY:
            LRU_remove_cache_item(cache, victim);
            break;
        default:
		    NCAC_error("unknown cache policy");
            break;
    }
}

void remove_cache_item(struct extent *extent, int policy)
{

    remove_cache_item_with_policy(extent, policy);
    remove_cache_item_no_policy(extent);
}

struct extent * get_free_extent_list_item(struct list_head *list)
{
    struct extent *new;
    struct list_head *delete;

    if ( list_empty(list) ) return NULL;

    delete = list->next;
    list_del_init(delete);

    new = list_entry(delete->prev, struct extent, list);

    return new;
}


/* shrink_cache: shrink a cache with expected number of extents. The
 * real number of extents which have been shrinked is returned by
 * "scanned". This number might be less than "expected". All shrinked
 * extents are returned into the extent free list.
 * Different cache policies take their own ways to do shrink.
 */
int shrink_cache(struct cache_stack *cache_stack, 
                 unsigned int expected, 
                 int policy, 
                 unsigned int *shrinked)
{
    int ret=-1;

    switch (policy){
        case LRU_POLICY:
            ret = LRU_shrink_cache(cache_stack, expected, shrinked);
            break;

        case ARC_POLICY: 
            ret = LRU_shrink_cache(cache_stack, expected, shrinked);
            break;
        
        default:
		    NCAC_error("unknown cache policy");
            break;
    }
    return ret;
}


int is_extent_discardable(struct extent *victim)
{
    if ( PageClean(victim) && 0 == victim->reads && 0 == victim->writes ) 
        return 1;
    else 
        return 0;
}

/* hit_cache_item: cache hit, change the position according to the policy */
void hit_cache_item(struct extent *extent, int cache_policy)
{
    remove_cache_item_with_policy(extent, cache_policy);
    add_cache_item_with_policy(extent, cache_policy);
    return;
}

