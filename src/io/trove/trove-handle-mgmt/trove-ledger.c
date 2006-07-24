/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "trove-extentlist.h"
#include "trove-ledger.h"
#include "trove.h"
#include "gossip.h"

/* struct handle_ledger
 *
 * Structure used internally for maintaining state.  Opaque pointers
 * passed back to the caller.
 */
struct handle_ledger {
    struct TROVE_handle_extentlist free_list;
    struct TROVE_handle_extentlist recently_freed_list;
    struct TROVE_handle_extentlist overflow_list;
    char *store_name;
    FILE *backing_store; 
    TROVE_handle free_list_handle;
    TROVE_handle recently_freed_list_handle;
    TROVE_handle overflow_list_handle;
    uint64_t cutoff;	/* when to start trying to reuse handles */
};

/* Functions used only internally:
 */

/*
  handle_recycle - takes care of moving handles among the various free
  lists
*/
static int handle_recycle(struct handle_ledger *hl);

/* handle_ledger_init()
 *
 * initializes the structure that tracks the extent lists. for lack of
 * a better term we call it a 'handle_ledger'.  If the backing store
 * file exists, reads it into memory.
 *
 * coll_name: name of collection for which this handle ledger applies
 * admin_name: name of collection from which we read and dump
 * extentlist bstreams
 *
 * returns: pointer to an initialized  handle_ledger struct
 */
struct handle_ledger * trove_handle_ledger_init(
    TROVE_coll_id coll_id, char *admin_name)
{
    struct handle_ledger *ledger;

    ledger = calloc(1, sizeof(struct handle_ledger));
    if (ledger == NULL) {
	perror("load_handle_ledger: malloc");
	return ledger;
    }

    if (extentlist_init(&(ledger->free_list))) return NULL;
    if (extentlist_init(&(ledger->recently_freed_list))) return NULL;
    if (extentlist_init(&(ledger->overflow_list))) return NULL;
	
    /* we work with extents through trove_setinfo now.  We still don't
     * have a good way to save and restore state, but we can limp
     * along with our 'good enough' way for a bit.  The commented-out
     * code below might be one way to load and restore state */
#if 0
    if (admin_name == NULL ) {
	/* load with defaults */
	/* XXX: should it be an error to run without a backing store?
	 * maybe in the final cut...*/
	ret = extentlist_addextent(&(ledger->free_list), MIN_HANDLE, MAX_HANDLE);
	if (ret != 0) {
	    gossip_lerr("error adding extent\n");
	    return NULL;
	}
    } else {
	/* handle_store_load will fail if the collection is not
	 * already created ... say by a mkfs */
	ret = handle_store_load(coll_id, admin_name, ledger);
	if (ret < 0) {
	    gossip_lerr("backing store initialization failed\n");
	    return NULL;
	}		
    }
#endif
    return ledger;
}


/* handle_ledger_dump()
 *
 * the publicly visible function
 */
int trove_handle_ledger_dump(struct handle_ledger *hl) 
{
    return -1;
}

/* 
 * return all allocated memory back to the system
 */
void trove_handle_ledger_free(struct handle_ledger *hl)
{
    extentlist_free(&(hl->free_list));
    extentlist_free(&(hl->recently_freed_list));
    extentlist_free((&hl->overflow_list));
    free(hl);
}


TROVE_handle trove_ledger_handle_alloc(struct handle_ledger *hl)
{
    return (hl ? extentlist_get_and_dec_extent(
                &(hl->free_list)) : TROVE_HANDLE_NULL);
}

/* if possible, allocate a handle within the given starting point and
 * ending point (provided by the TROVE_extent)
 *
 * if no handle avaliable in the extent, return 0
 */
TROVE_handle trove_ledger_handle_alloc_from_range(
    struct handle_ledger *hl, TROVE_extent *extent)
{
    return (hl ? extentlist_get_from_extent(
                &(hl->free_list), extent) : TROVE_HANDLE_NULL);
}

int trove_ledger_peek_handles(
    struct handle_ledger *hl,
    TROVE_handle *out_handle_array,
    int max_num_handles,
    int *returned_handle_count)
{
    return (hl ? extentlist_peek_handles(
                &(hl->free_list), out_handle_array, max_num_handles,
                returned_handle_count) : TROVE_HANDLE_NULL);
}

int trove_ledger_peek_handles_from_extent(
    struct handle_ledger *hl,
    TROVE_extent *extent,
    TROVE_handle *out_handle_array,
    int max_num_handles,
    int *returned_handle_count)
{
    return (hl ? extentlist_peek_handles_from_extent(
                &(hl->free_list), extent, out_handle_array,
                max_num_handles, returned_handle_count) :
            TROVE_HANDLE_NULL);
}

int trove_ledger_handle_free(struct handle_ledger *hl, TROVE_handle handle)
{
    /* won't actually return to the free list until timeout stuff is
     * in place */
    if (extentlist_hit_cutoff(&(hl->recently_freed_list), hl->cutoff))
    {
	extentlist_addextent(&(hl->overflow_list), handle, handle);
    }
    else
    {
	extentlist_addextent(&(hl->recently_freed_list), handle, handle);
    }

    /* don't even bother checking the timeouts until the free_list
     * starts running out */
    if (hl->free_list.num_handles > hl->cutoff)
    {
	return 0;
    }

    if (extentlist_endured_purgatory(
            &(hl->recently_freed_list), &(hl->overflow_list)) )
    {
	handle_recycle(hl);
    }
    return 0;
}

/* trove_ledger_set_timeout: 
 *	set the handle purgatory on the underlying extentlists 
 */
int trove_ledger_set_timeout(
    struct handle_ledger *hl, 
    struct timeval *timeout)
{
    return (extentlist_set_purgatory(timeout));
}
/* 
 * handle_recycle: recently_freed items become free items.  overflow
 * items become recently_freed items
 *
 * call this function when the purgatory time for freed handles
 * expires
 */
static int handle_recycle(struct handle_ledger *hl)
{
    extentlist_merge(&(hl->free_list), &(hl->recently_freed_list));
    extentlist_free(&(hl->recently_freed_list));
    hl->recently_freed_list = hl->overflow_list;
    memset(&(hl->overflow_list), 0, sizeof(struct TROVE_handle_extentlist));
    gettimeofday(&(hl->overflow_list.timestamp), NULL);

    return 0;
}

/* trove_handle_ledger_get_statistics()
 *
 * returns statistics on a given ledger, for now just free handle count
 *
 * no return value
 */
void trove_handle_ledger_get_statistics(
    struct handle_ledger *hl,
    uint64_t *free_count)
{
    uint64_t tmp_count = 0;
    *free_count = 0;

    extentlist_count(&(hl->free_list), &tmp_count);
    *free_count += tmp_count;
    extentlist_count(&(hl->recently_freed_list), &tmp_count);
    *free_count += tmp_count;
    extentlist_count(&(hl->overflow_list), &tmp_count);
    *free_count += tmp_count;
}


void trove_handle_ledger_show(struct handle_ledger *hl) 
{
    gossip_debug(GOSSIP_TROVE_DEBUG, "====== free list\n");
    extentlist_show(&(hl->free_list));
    gossip_debug(GOSSIP_TROVE_DEBUG, "====== recently_freed list\n");
    extentlist_show(&(hl->recently_freed_list));
    gossip_debug(GOSSIP_TROVE_DEBUG, "====== overflow list\n");
    extentlist_show(&(hl->overflow_list));

    gossip_debug(GOSSIP_TROVE_DEBUG, "====== free list\n");
    extentlist_stats(&(hl->free_list));
    gossip_debug(GOSSIP_TROVE_DEBUG, "====== recently_freed list\n");
    extentlist_stats(&(hl->recently_freed_list));
    gossip_debug(GOSSIP_TROVE_DEBUG, "====== overflow list\n");
    extentlist_stats(&(hl->overflow_list));
}

#if 0
/*
 * handle_store_extentlist_exists:
 *
 * given a collection id and a handle, see if that handle is being used by anything
 *
 * 1	yes, something is using that handle
 * 0	nope, nothing is
 */

static int handle_store_extentlist_exists(TROVE_coll_id coll_id,
					  TROVE_handle bstream_handle) 
{
    TROVE_ds_type type;
    TROVE_ds_state state;
    TROVE_op_id op_id;

    int ret, count;

    ret = trove_dspace_verify(coll_id, bstream_handle, &type, 0, NULL, &op_id);
    while (ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    if ( ret < 0 ) {
	/* XXX: -1 could be a lot of different error conditions.  don't
	 * know if the state variable is being filled in correctly,
	 * though, so assume "error" means "not found"
	 */
	return 0;
    } else {
	return 1;
    }
}

/*
 * handle_store_extentlist_create:
 *
 * Given a collection id and a handle ( presumably to a dspace containing a
 * bstream), create a bstream holding an empty extentlist.
 *
 * coll_id		id of the collection  we are using
 * bstream_handle	handle of the dataspace holding the bstream
 * elist		extentlist we are going to populate
 *
 * returns:
 *  0			all went well
 *  nonzero		trouble
 */
static int handle_store_extentlist_create(TROVE_coll_id coll_id,
					  TROVE_handle bstream_handle,
					  struct TROVE_handle_extentlist *elist)
{
    int ret = -1, count, i = 0;
    TROVE_ds_state state;
    TROVE_op_id op_id;
    TROVE_size buffsz;
    TROVE_handle_extent_array extent_array;

    /*
      convert from a TROVE_handle_extentlist type to a
      TROVE_handle_extent_array type.  This bites.
    */
    extent_array.extent_count = elist->num_extents;
    extent_array.extent_array = malloc(
        elist->num_extents * sizeof(TROVE_extent));
    if (!extent_array.extent_array)
    {
        return ret;
    }

    for(i = 0; i < elist->num_extents; i++)
    {
        extent_array.extent_array[i].first = elist->extents[i].first;
        extent_array.extent_array[i].last = elist->extents[i].last;
    }
	
    ret = trove_dspace_create(coll_id,
                              &extent_array,
			      &bstream_handle,
			      TROVE_PLAIN_FILE, /* not sure exactly what to
						   call this, but
						   TROVE_TEST_BSTREAM is
						   defintely wrong */ 
			      NULL,
			      TROVE_SYNC,
			      NULL,
			      &op_id);
    while ( ret == 0) trove_dspace_test(coll_id, op_id, &count, NULL, NULL, &state);
    free(extent_array.extent_array);
    if (ret < 0) {
	gossip_lerr("dspace create failed\n");
	return -1;
    }

    buffsz = sizeof(struct TROVE_handle_extentlist);
    ret = trove_bstream_write_at(coll_id, bstream_handle, elist, 
				 &buffsz, 0, 0, NULL, NULL, &op_id);
    while (ret == 0) {
	ret = trove_dspace_test(coll_id, op_id, &count, 
				NULL, NULL, &state);
    }
    if (ret < 0 ) {
	gossip_lerr("handle_store_create: write to bstream failed\n");
	return -1;
    }
    return 0;
}

/*
 * handle_store_load() - takes care of staging free lists back from storage
 *
 * free list:
 * if dspace exists
 * 	load the dspace
 * else
 * 	write out an initial bstream
 * 	add initial big extent to list
 * recently_freed and overflow:
 * if dspace exits
 * 	load the dspace
 * else
 * 	write out initial bstream
 */
	
static int handle_store_load(TROVE_coll_id coll_id,
			     char *admin_name,
			     struct handle_ledger *ledger) 
{
    /* initialize lists to empty */
    extentlist_init(&ledger->free_list);
    extentlist_init(&ledger->recently_freed_list);
    extentlist_init(&ledger->overflow_list);

    /* we used to add everything onto the free list, then take it off.  we now
     * do this somewhere else (trove_map_handle_ranges */
    
#if 0
    /* ONE DAY THIS CODE WILL READ THE FREE LISTS FROM BSTREAMS, BUT NOT TODAY. */
    ret = trove_collection_lookup(admin_name, &admin_id, NULL, &op_id);
    if (ret < 0) {
	gossip_lerr("collection lookup failed");
	return -1;
    }

    /* great. we've got our collection.  start looking for bstreams. */
    /* free list is just a little different: it starts off w/ one extent */
    if (handle_store_extentlist_exists(admin_id, free_list_handle)) {
	ret = handle_store_extentlist_read(admin_id, free_list_handle, &(ledger->free_list));
	if (ret < 0 ) {
	    gossip_lerr("loading from backing store failed\n");
	    return -1;
	}
    } else {
	ret = handle_store_extentlist_create(admin_id, free_list_handle, &(ledger->free_list));
	if (ret < 0 ) {
	    gossip_lerr("creation of backing store failed\n");
	    return -1;
	}
	ret = extentlist_addextent(&(ledger->free_list), MIN_HANDLE, MAX_HANDLE);
	if (ret < 0 ) {
	    gossip_lerr("adding extent failed\n");
	    return -1;
	}
    }
    if (handle_store_extentlist_exists(admin_id, recently_freed_list_handle)) {
	ret = handle_store_extentlist_read(admin_id, recently_freed_list_handle, &(ledger->recently_freed_list));
	if ( ret < 0 ) {
	    gossip_lerr("loading from backing store failed\n");
	    return -1;
	}
    } else {
	ret = handle_store_extentlist_create(admin_id, recently_freed_list_handle, 
					     &(ledger->recently_freed_list));
	if (ret < 0) {
	    gossip_lerr("creation of backing store failed\n");
	    return -1;
	}
    }
    if(handle_store_extentlist_exists(admin_id, overflow_list_handle )) {
	ret = handle_store_extentlist_read(admin_id, overflow_list_handle, &(ledger->overflow_list));
	if (ret < 0 ) {
	    gossip_lerr("loading from backing store failed\n");
	    return -1;
	}
    }  else {
	ret = handle_store_extentlist_create(admin_id, overflow_list_handle, 
					     &(ledger->overflow_list));
	if (ret < 0) {
	    gossip_lerr("creation of backing store failed\n");
	    return -1;
	}
    }
#endif

    return 0;
}
#endif

/* trove_handle_ledger_addextent:  add a new legal extent from which the ledger
 *   can dole out handles.  
 *
 *	hl	struct handle_ledger to which we add stuff
 *	extent	the new legal extent
 *
 * return:
 *    0 if ok
 *    nonzero if not
 */
inline int trove_handle_ledger_addextent(struct handle_ledger *hl, 
	TROVE_extent * extent)
{
   return extentlist_addextent(&(hl->free_list), 
	   extent->first, extent->last);
}

/* trove_handle_remove: 
 *	take a specific handle out of the valid handle space 
 *
 * returns
 *  0 if ok
 *  nonzero  on error
 */

inline int trove_handle_remove(struct handle_ledger *hl, TROVE_handle handle)
{
    return extentlist_handle_remove(&(hl->free_list), handle);
}

/* trove_handle_ledger_set_threshold()
 * hl:  handle ledger object we will modify
 * nhandles:  number of total handles in the system. we will make a cutoff
 *                 value based upon this number
 */
inline void trove_handle_ledger_set_threshold(struct handle_ledger *hl,
					    uint64_t nhandles)
{
    hl->cutoff = (nhandles/4)+1;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
