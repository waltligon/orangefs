/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "trove-extentlist.h"
#include "gossip.h"

enum extentlist_coalesce_status {
	COALESCE_ERROR=-1,
	COALESCE_NONE=0,
	COALESCE_SUCCESS=1
};
static int extentlist_coalesce_extent(struct avlnode **n, struct TROVE_handle_extent *e);

static int avltree_extent_search(struct avlnode *n, TROVE_handle handle, TROVE_handle *f_p, TROVE_handle *l_p);

static inline void extent_init(struct TROVE_handle_extent *e, TROVE_handle first, TROVE_handle last);
static void extent_show(struct avlnode *n, int param, int depth);

static int64_t avltree_extent_search_in_range(struct avlnode *n, TROVE_extent * req_extent);

/* constructor for an extent 
 * first: start of extent range
 * last: end of extent range
 * returns: nothing. what could go wrong?
 */
static inline void extent_init(struct TROVE_handle_extent *e,
			       TROVE_handle first,
			       TROVE_handle last) 
{
    e->first = first;
    e->last = last;
}

/* initialize an extentlist.  memory for the list will be allocated, but no
 * extents will be in the list 
 *
 * input: 	pointer to the list we want to initialize 
 * returns: 0 on success, -1 if error
 */

int extentlist_init(struct TROVE_handle_extentlist *elist) 
{

    elist->extents = calloc(EXTENTLIST_SIZE, sizeof(struct TROVE_handle_extent));
    if ( elist->extents == NULL ) {
	perror("extentlist_init: malloc");
	return -1;
    }
    gettimeofday(&elist->timestamp, NULL);
#if 0 
    /* XXX: implement on-disk later */
    elist->num_entries = 0;
    elist->__size = EXTENTLIST_SIZE;
#endif

    return 0;
}

/*
 * helper function to free memory in the index 
 */
void extentlist_node_free(struct avlnode *n,
			  int p,
			  int d)
{
    free(n->d);
    free(n);
}
/*
 * return all allocated memory back to the system 
 */
void extentlist_free(struct TROVE_handle_extentlist *e)
{
    avlpostorder(e->index, extentlist_node_free, 0, 0); 
    free(e->extents);
    memset(e, 0,  sizeof(struct TROVE_handle_extentlist));
}
/* add an extent to a list
 *
 * see if we can coalesce this extent with another in the index. if not,
 * add it to the tree ourselves
 */
int extentlist_addextent(struct TROVE_handle_extentlist *elist,
			 TROVE_handle first,
			 TROVE_handle last)
{
    struct TROVE_handle_extent *e;

    if ((e = (struct TROVE_handle_extent *)malloc(sizeof(struct TROVE_handle_extent)) ) == NULL ) {
	perror("extentlist_addextent: malloc");
	return -1;
    } 
    extent_init(e, first, last);

    if (extentlist_coalesce_extent(&elist->index,e)  == COALESCE_NONE) {
	/* if the index is empty, avlinsert will allocate space */
	if ( avlinsert(&elist->index, e) == 0 ) {
	    gossip_lerr("error inserting key\n");
	    return -1;
	}
	elist->num_extents++;
    }
	
    /* it might be expensive to check the time every time an extent is
     * released, but we'll cross that bridge when we get there */
    gettimeofday(&elist->timestamp, NULL );
    elist->num_handles += (last - first + 1);

    /* XXX: implement on-disk stuff later */
#if 0    
    void * p;
    extent_init(&(elist->extents[elist->num_entries]), first, last);
    if ( elist->num_entries % EXTENTLIST_TIMECHECK_FREQ == 0 )
	gettimeofday(&elist->timestamp, NULL);
    elist->num_entries++;

    /* grow the array if too many extents */
    if ( elist->num_entries == elist->__size ) {

	p =  realloc(elist->extents, sizeof(struct TROVE_handle_extent) * elist->__size * 2);
	if (p == NULL ) {
	    perror("extentlist_addextent: realloc");
	    return -1;
	} else {
	    elist->extents = p;
	    elist->__size *= 2;
	}
    }
#endif
    return 0;
}

/*
 * extentlist_merge:  
 * merge items from extentlist 'src' into extentlist 'dest'
 * 
 * note: this first pass is very inefficent.  a better idea might be to do a
 *   postorder traversal of 'src', inserting extents into 'dest' and deleting
 *   nodes from src  without rebalancing. 
 */
int extentlist_merge(struct TROVE_handle_extentlist *dest,
		     struct TROVE_handle_extentlist *src)
{
    struct avlnode *n = src->index;
    gossip_debug(TROVE_DEBUG, "merging extentlists\n");
    while(n != NULL) {
	extentlist_addextent(dest, n->d->first, n->d->last);
	avlremove(&n, n->d->first);
    }
    src->index = NULL;
    return 0;
}


/* 
 * struct avlnode **n	head of extent index
 * struct TROVE_handle_extent *e	extent we might coalesce.  allocated by caller.
 *
 * There are some constraints on the data set we can make to simplify things:
 * 	. extents are integers (discrete values)
 * 	. extents do not overlap
 * 	. extents are indexed in the tree by 'first', but because they do not
 * 	overlap, can also be searched by 'last'
 *
 * given the extent
 * search the tree for lesser-adjacent
 * 	record and delete
 * search the tree for greater-adjacent
 * 	record and delete
 * if adjacent extents exist
 * 	insert new, coaleced extent
 *
 * (The caller has to add the extent to the tree if we return COALESCE_NONE,
 * else this function will do it )
 *
 * we will modify the extent given us if there are adjacent extents.  If no
 * adjacent extents found, the given extent will be unmoleste and COALESCE_NONE
 * returned.  
 *
 *   returns:  
 *   	COALESCE_NONE		no extents were coalesced
 *   	COALESCE_SUCCESS	extents were coalesced sucessfully
 *   	COALESCE_ERROR		some error occured
 *
 */
static int extentlist_coalesce_extent(struct avlnode **n,
				      struct TROVE_handle_extent *e)
{
    struct TROVE_handle_extent **lesser, **greater; 

    int merge_lesser=0, merge_greater=0;

    if ( (lesser = avlaltaccess(*n, (e->first - 1))) != NULL) {
	e->first = (*lesser)->first;
	if ( avlremove(n, (*lesser)->first) == 0 ) {
	    gossip_lerr("error removing key %Ld\n", (*lesser)->first);
	    return COALESCE_ERROR;
	}
	merge_lesser = 1;
    }
    if ( (greater = avlaccess(*n, (e->last + 1)) )!= NULL ) {
	e->last = (*greater)->last;
	if (avlremove(n, (*greater)->first) == 0 ) {
	    gossip_lerr("error removing key %Ld\n", (*greater)->first);
	    return COALESCE_ERROR;
	}
	merge_greater = 1;
    }

    if (merge_lesser || merge_greater ) {
	if (avlinsert(n, e) == 0 ) {
	    gossip_lerr("error inserting key %Ld\n", e->first);
	    return COALESCE_ERROR;
	} else
	    return COALESCE_SUCCESS;
    }
    return COALESCE_NONE;
}

/* extentlist_get_and_dec
 *
 * takes the first extent from the index: in this implementation there is no
 * gaurantee if that extent is 'low' or 'high'.  
 *
 * if the extent is  one-length, it's removed from the index, else decrement
 * upper bound
 *
 * extents will never overlap.  this property means we can decrement the upper
 * bound without affecting the search properties of the index.
 *
 * returns: a handle
 * side-effects: the first extent in the list is reduced by one and possibly
 * deleted altogether
 *
 */
TROVE_handle extentlist_get_and_dec_extent(struct TROVE_handle_extentlist *elist)
{
    /* get the extent from the index 
     * pull a handle out of the extent
     * if extent is empty
     * 	delete key from tree
     *
     * XXX: on disk stuff
     */

    struct TROVE_handle_extent **e;
    struct TROVE_handle_extent *ext;
    int64_t handle;

    e = avlgethighest(elist->index);

    /* could either be called w/o calling the setup functions, or we gave
     * out the last handle in the list */
    if (e == NULL ) {
	gossip_lerr("no handles avaliable\n");
	return -1;
    }
    ext = *e;

    assert(ext->first <= ext->last);

    handle = ext->last;
    if (ext->first  == ext->last)	{
	/* just gave out the last handle in the range */
	if (avlremove(&(elist->index), ext->first) == 0) {
	    gossip_lerr("avlremove: index does not have that item\n");
	    return -1;
	}
	elist->num_extents--;
    } else {
	ext->last--; 
	elist->num_handles--;
    }

    return handle;
}

/*
 * instead of returning an arbitrary handle, return a handle from within the
 * given range
 * elist    index of avaliable extents
 * extent   the range from which we want to allocate a handle
 *
 * returns 
 *   a valid trove handle from within the range specified by 'extent' 
 *   
 *   0 (an invalid handle) if error
 */
TROVE_handle extentlist_get_from_extent(struct TROVE_handle_extentlist *elist, 
	TROVE_extent *extent)
{
    int ret;
    TROVE_handle handle;

    handle = avltree_extent_search_in_range(elist->index, extent); 
    if (handle == 0) 
	return 0;

    ret = extentlist_handle_remove(elist, handle);
    if (ret == -1) 
	return 0;

    return handle;

}
void extentlist_stats(struct TROVE_handle_extentlist *elist)
{
    gossip_debug(TROVE_DEBUG, "handle/extent ratio: %f\n", (double)elist->num_handles/ (double)elist->num_extents);
}

void extentlist_show(struct TROVE_handle_extentlist *elist)
{
    avldepthfirst(elist->index, extent_show, 0 , 0);
}

static void extent_show(struct avlnode *n,
			int param,
			int depth)
{
    struct TROVE_handle_extent *e = (struct TROVE_handle_extent *)(n->d);
    gossip_debug(TROVE_DEBUG, "lb: %Ld ub: %Ld\n", e->first, e->last);
}

/*
 * have so many extents been added to this list that it's time to start adding
 * extents to another list?
 *
 * struct TROVE_handle_extentlist *elist	list in question
 *
 *  0				plenty of room
 *  nonzero			time to move on
 */
int extentlist_hit_cutoff(struct TROVE_handle_extentlist *elist, 
			    uint64_t cutoff) 
{
    return( (elist->num_handles > cutoff) );
}

/*
 * have the handles on extentlist 'querrent' sat out long enough?
 *
 * struct TROVE_handle_extentlist *querent		extentlist in question
 * struct TROVE_handle_extentlist *reference		is the querent sufficiently older 
 * 					than this one?
 */
int extentlist_endured_purgatory(struct TROVE_handle_extentlist *querent, struct TROVE_handle_extentlist *reference) {
    return ( (reference->timestamp.tv_sec - querent->timestamp.tv_sec) > EXTENTLIST_PURGATORY );
}


/* extentlist_handle_remove()
 *
 * finds a specific handle in an extentlist and removes it from the free list,
 * splitting the original extent into two if necessary.
 *
 * returns 0 on success, -1 on failure (not present in extentlist).
 */
int extentlist_handle_remove(struct TROVE_handle_extentlist *elist,
			     TROVE_handle handle)
{
    int ret;
    TROVE_handle key_handle, last_handle;
    struct TROVE_handle_extent *old_e, *new_e;

    ret = avltree_extent_search(elist->index, handle, &key_handle, &last_handle);
    if (ret == -1) return -1;

    avlremove(&(elist->index), key_handle);
    if (key_handle == last_handle) return 0; /* done, length 1 extent now gone */
    else {
	if ((old_e = (struct TROVE_handle_extent *)malloc(sizeof(struct TROVE_handle_extent)) ) == NULL ) {
	    assert(0);
	}
	if (key_handle == handle) {
	    old_e->first = key_handle + 1;
	    old_e->last  = last_handle;
	    avlinsert(&(elist->index), old_e);
	}
	else if (last_handle == handle) {
	    old_e->first = key_handle;
	    old_e->last  = last_handle - 1;
	    avlinsert(&(elist->index), old_e);
	}
	else {
	    /* splitting extent into two */
	    if ((new_e = (struct TROVE_handle_extent *)malloc(sizeof(struct TROVE_handle_extent)) ) == NULL ) {
		assert(0);
	    }
	    old_e->first = key_handle;
	    old_e->last  = handle - 1;
	    new_e->first = handle + 1;
	    new_e->last  = last_handle;
	    avlinsert(&(elist->index), old_e);
	    avlinsert(&(elist->index), new_e);
	}
	return 0;
    }
}

/* avltree_extent_search()
 *
 * finds an extent containing the given handle.
 *
 * returns -1 if not found, or 0 if the handle is found in an
 * extent.  In that case first and last are returned...
 */
static int avltree_extent_search(struct avlnode *n,
				 TROVE_handle handle,
				 TROVE_handle *first_p,
				 TROVE_handle *last_p)
{
    struct TROVE_handle_extent *e;

    if (!n) return -1;

    e = (struct TROVE_handle_extent *) n->d;
    
    if (e->first > handle) return avltree_extent_search(n->left, handle, first_p, last_p);
    else if (e->last < handle) return avltree_extent_search(n->right, handle, first_p, last_p);
    else {
	*first_p = e->first;
	*last_p  = e->last;
	return 0;
    }
}

/* avltree_extent_search_in_range()
 * given a low and a high range for a handle, search the index for any extent
 * in that range.  
 * return 0 if no extents are within the range
 * otherwise, return a handle within the given range 
 *
 * it is expected that something else will delete the handle from the index
 */

static int64_t avltree_extent_search_in_range(struct avlnode *n,
				TROVE_extent * req_extent)
{
    struct TROVE_handle_extent *e, *left, *right;
    if (!n) return 0;

    e = (struct TROVE_handle_extent *) n->d;
    left = NULL;
    right = NULL;
    if (n->left != NULL) 
	left = (struct TROVE_handle_extent *) n->left->d;
    if (n->right != NULL)
	right = (struct TROVE_handle_extent *) n->right->d;

    /* request matches at one edge or the other of an existing extent */
    if ( (req_extent->first == e->first) || req_extent->first == e->last)
	return(req_extent->first);

    else if ( (req_extent->last == e->first) || (req_extent->last == e->last))
	return(req_extent->last);

    /* request fits within an existing extent */
    else if ( req_extent->first > e->first && req_extent->last < e->last)
	return (req_extent->last);

    /* request completely overlaps */
    else if ( req_extent->first < e->first && req_extent->last > e->last)
	return (e->last);
    
    /* request left-overlaps */
    else if ( req_extent->first < e->first && req_extent->last < e->last && req_extent->last > e->first )
	return (e->first);
    
    /* request right-overlaps */
    else if (req_extent->first > e->first && req_extent->last > e->last && req_extent->first < e->last )
	return (e->last);

    /* otherwise, look at children */
    else if (left != NULL && (req_extent->last < e->first) )
	return avltree_extent_search_in_range(n->left, req_extent);
    else if ( right != NULL && (req_extent->first > e->last) )
	return avltree_extent_search_in_range(n->right, req_extent);
    else /* if no chance to match with the kids, we give up */
	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
