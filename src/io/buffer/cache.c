#include <stdio.h>
#include <stdlib.h>

#include "internal.h"
#include "state.h"
#include "flags.h"
#include "cache.h"

/* contains core functions about cache */



/*
 * Given the index of an extent, look up whether this extent is cached
 * or not.
 * Cached: retured the extent 
 * NOT cached: return NULL.
 *
 * No cache position management. NOT cache policy related.
 */
struct extent * lookup_cache_item(struct inode *mapping, unsigned long index)
{
    struct extent *extent;

    extent = radix_tree_lookup(&mapping->page_tree, index);

    return extent;
}


/* ==================================================================
 *                add an item into the cache:						*
 * (1) add an item into the radix tree (add_cache_item_no_policy).  *
 * (2) add an item into the cache (add_cache_item_with_policy).		*
 * ================================================================== 
 */

/* add_cache_item_no_policy(): add an extent into the cache tree.
 * Each inode has a cache tree, protected by its "lock". 
 * NOT cache policy related.
 */
int add_cache_item_no_policy(struct extent *extent, struct inode *mapping, unsigned long index)
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
void add_cache_item_with_policy(struct extent *extent)
{
    struct cache_stack *cache_stack = NULL;

    cache_stack = get_extent_cache_stack(extent);

    if ( TestSetPageLRU(extent) )
		NCAC_error("flag error");

    add_page_to_inactive_list(cache_stack, extent);

	return;
}


int add_cache_item(struct extent *extent, struct inode *mapping,
unsigned long index)
{
    int ret = add_cache_item_no_policy(extent, mapping, index);

	if (ret == 0){
		add_cache_item_with_policy(extent);
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

void remove_cache_item_no_policy(struct extent *extent)
{
    struct inode *mapping = extent->mapping;

    radix_tree_delete(&mapping->page_tree, extent->index);

    /* get this back if the "list" field is used */
    //list_del(&page->list);
    extent->mapping = NULL;

}

void remove_cache_item_with_policy(struct extent *victim)
{
    list_del(&victim->lru);
    victim->mapping->nrpages--;
}

void remove_cache_item(struct extent *extent)
{

    remove_cache_item_with_policy(extent);
    remove_cache_item_no_policy(extent);
}


void add_free_extent_list_item(struct list_head *head, struct extent *page)
{
     list_add_tail(&page->list, head);
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


int shrink_extent_cache( struct cache_stack *cache_stack, 
						 unsigned int expected, unsigned int *scanned);
int wakeup_dirty_flush(void);
int shrink_extent_inactive( struct cache_stack *cache_stack, int max_scan, int expected );
int refill_inactive_list( struct cache_stack *cache_stack, int expected);
static inline int is_extent_movable(struct extent *victim);


/* here is the main entry point part of cache replacement. 
 * When we run out of free extents, someone should be discarded 
 * from the cache. 
 */

/* try_to_discard_extents(): this function scans the inactive_list
 * and try to discard up to  "expected" extents.
 * The discardable extents are clean and no referenced. If there is not
 * enough clean extents present, the dirty flush thread will be
 * waken up.
 *
 * this call return the number of extents which have been discarded.
 * if the return value is less than 0, error occurs.
 *
 */
int  try_to_discard_extents( struct cache_stack *cache_stack, 
			     unsigned int expected ) 
{
   int ret=0;
   unsigned int scan;

   DPRINT("try_to_discard_extents: dirty=%ld, expected=%d\n",cache_stack->nr_dirty, expected);

   ret = shrink_extent_cache( cache_stack, expected, &scan);

   if ( ret < 0 ) {
	   NCAC_error("try_to_discard_extents: error in shrink_extent_cache\n");
	   return ret;
   }

   DPRINT("try_to_discard_extents: expected=%d, flushed=%d\n", expected, ret);

   if ( ret < expected && cache_stack->nr_dirty ) {
       wakeup_dirty_flush();
   }
   
   return ret;
}

/* if there is no pending write or read operations on this
 * extent, this extent is movable.
 */
static inline int is_extent_movable(struct extent *victim)
{
    if ( PageClean(victim) && victim->writes == victim->wcmp && victim->reads == victim->rcmp )
        return 1;
    else return 0;
}



/* shrink_extent_cache(): this function is to discard as many as
 * "expected" clean extents from the cache.
 *
 * This function is dependent on the cache replacement policy. 
 * The current implementation is more and less a simplified
 * version of the LRU-2Q policy.
 *
 * return value is less than 0, if error. Otherwise, return
 * the number of extents shrinked.
 *
 */
int shrink_extent_cache(struct cache_stack *cache_stack, unsigned int expected, unsigned int *scanned)
{
   int ret;
   unsigned int nr_reclaimed = 0;
   unsigned int nr_refilled = 0;
   
   DPRINT("shrink_extent_cache: to shrink inactive list: max_scan=%ld, expected=%d\n",cache_stack->nr_inactive, expected);

   ret = shrink_extent_inactive(cache_stack, cache_stack->nr_inactive, expected);
   if ( ret < 0 ) {
       NCAC_error("shrink_extent_inactive error: error=%d\n", ret);
       return ret;
   }

   DPRINT("shrink_extent_cache: to shrink inactive list: expected=%d, shrinked=%d\n",expected, ret);

   nr_reclaimed += ret;

   if ( nr_reclaimed >= expected) return nr_reclaimed;

   /* how many extents are moved from active list to the inactive list? 
    * In Linux, it tries to keep the active list of 2/3 size of the cache.
    */
   nr_refilled = expected * cache_stack->nr_active/ 
					( (cache_stack->nr_inactive | 1) *2 );			

   DPRINT("----------:refill inactive: num=%d, active=%ld, inactive=%ld\n", nr_refilled, cache_stack->nr_active, cache_stack->nr_inactive);

   if ( !nr_refilled ) return nr_reclaimed;

   /* Limit the number of refilled extents in one run. */
   if ( nr_refilled > 2*REFILL_CLUSTER_MAX )
       nr_refilled = 2*REFILL_CLUSTER_MAX;

   ret = refill_inactive_list(cache_stack, nr_refilled);
   if ( ret < 0 ) {
       NCAC_error("refill_inactive_list error: error=%d\n", ret);
       return ret;
   }

   ret = shrink_extent_inactive(cache_stack, cache_stack->nr_inactive, expected - nr_reclaimed);
   if ( ret < 0 ) {
       NCAC_error("shrink_extent_inactive error: error=%d\n", ret);
       return ret;
   }

   nr_reclaimed += ret;

   return nr_reclaimed;
}


/* shrink_extent_inactive(): dicards clean extents from the inactive_list.
 */
int shrink_extent_inactive( struct cache_stack *cache_stack, int max_scan, int expected )
{
    int nr_to_process;
	int error;
    int ret = 0;
    struct extent * victim;
	struct list_head * inactive_list, *tail;

    DPRINT("shrink_extent_inactive: max_scan=%d, expected=%d\n", max_scan, expected);

    nr_to_process = expected;
    if (nr_to_process < DISCARD_CLUSTER_MIN)
        nr_to_process = DISCARD_CLUSTER_MIN;
	
    inactive_list =&cache_stack->inactive_list; 

    tail  = inactive_list->prev;
    while ( nr_to_process && tail != inactive_list ) {

        victim = list_entry(tail, struct extent, lru);
        tail = tail->prev;

        if ( !PageLRU(victim) ) {
			NCAC_error("extent flag is wrong\n");
            return NCAC_INVAL_FLAGS;
        }

		DPRINT("victim.flags=%lx, wcnt=%d, rcnt=%d, wcmp=%d, rcmp=%d, index=%ld\n", victim->flags, victim->writes, victim->reads, victim->wcmp, victim->rcmp, victim->index);

        if ( is_extent_movable(victim) ) {
			remove_cache_item(victim);
            add_free_extent_list_item(&cache_stack->free_extent_list, victim);

            DPRINT("discard extent: %p\n", victim);

			nr_to_process --; 
			ret ++;
        }            


		if ( PageReadPending(victim) || PageWritePending(victim)) {
			error = NCAC_check_ioreq(victim);
			if (error <0) {

				NCAC_error("NCAC_check_ioreq error: index=%ld, flags=%lx\n", victim->index, victim->flags);

				return error;
			}

			if (error) { /* completion */
                /* set all other related extents */
				list_set_clean_page(victim);
			}
		}
	}

    return ret;
}


/*
 * refill_inactive_list(): Try to move extents from "cache_stack" active
 * list to its inactive list.
 * If the extent is not movable, we move it to the head of the active
 * list. 
 * TODO: to verify this does make sense.
 * 
 * Returns how many extents moved, may be less than expected.
 */
int refill_inactive_list( struct cache_stack *cache_stack, int expected)
{
    struct list_head *tail;
    struct list_head *active_list;
    int  moved = 0;
	int  error;

    DPRINT("refill_inactive_list: expected=%d\n", expected);

    active_list =&cache_stack->active_list; 

    tail  = active_list->prev;
    while ( expected && tail != active_list ) {
        struct extent * victim;

        victim = list_entry(tail, struct extent, lru);
        tail = tail->prev;

		if ( PageReadPending(victim) || PageWritePending(victim)) {
			error = NCAC_check_ioreq(victim);
			if (error <0) {
				NCAC_error("NCAC_check_ioreq error");
				return error;
			}

			if (error) { /* completion */
                /* set all other related extents */
				list_set_clean_page(victim);
			}
		}

        if ( !is_extent_movable(victim) ) {  /* not movable */
            //list_del(&victim->lru);
            //list_add(&victim->lru, active_list);
            continue;
        }

		DPRINT("victim.flags=%lx, wcnt=%d, rcnt=%d, wcmp=%d, rcmp=%d\n", victim->flags, victim->writes, victim->reads, victim->wcmp, victim->rcmp);

        expected --;
        moved ++;
        list_move(&victim->lru, &cache_stack->inactive_list);

        /* set reference here to show that this extent was once "hot".
         * If there is a reference on it again when it is still in
         * inactive list, this extent will be quickly promoted into
         * the active list.
         */
        SetPageReferenced(victim);
		ClearPageActive(victim);
    }

    cache_stack->nr_active -= moved;
    cache_stack->nr_inactive += moved;

    DPRINT("************ move %d extents into inactive list\n", moved);

    return (moved);
}

int wakeup_dirty_flush()
{
   return 0;
}
