/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <assert.h>

#include <trove-extentlist.h>

enum extentlist_coalesce_status {
	COALESCE_ERROR=-1,
	COALESCE_NONE=0,
	COALESCE_SUCCESS=1
};
static int extentlist_coalesce_extent(struct avlnode **n, struct TROVE_handle_extent *e);

static void extent_init(struct TROVE_handle_extent *e, TROVE_handle first, TROVE_handle last);
static void extent_show(struct avlnode *n, int param, int depth);

/* constructor for an extent 
 * first: start of extent range
 * last: end of extent range
 * returns: nothing. what could go wrong?
 */
static void extent_init(struct TROVE_handle_extent *e,
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
 * we could check the time at every insert, but to avoid some system calls,
 * we'll update the timestamp at every EXTENTLIST_TIMECHECK_FREQ insertions 
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
	    fprintf(stderr, "error inserting key\n");
	    return -1;
	}
	elist->num_extents++;
    }
	
    if ( elist->num_handles % EXTENTLIST_TIMECHECK_FREQ == 0 )
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
    printf("merging extentlists\n");
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
	    fprintf(stderr, "error removing key %Ld\n", (*lesser)->first);
	    return COALESCE_ERROR;
	}
	merge_lesser = 1;
    }
    if ( (greater = avlaccess(*n, (e->last + 1)) )!= NULL ) {
	e->last = (*greater)->last;
	if (avlremove(n, (*greater)->first) == 0 ) {
	    fprintf(stderr, "error removing key %Ld\n", (*greater)->first);
	    return COALESCE_ERROR;
	}
	merge_greater = 1;
    }

    if (merge_lesser || merge_greater ) {
	if (avlinsert(n, e) == 0 ) {
	    fprintf(stderr, "error inserting key %Ld\n", e->first);
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
int64_t extentlist_get_and_dec_extent(struct TROVE_handle_extentlist *elist)
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
	fprintf(stderr, "no handles avaliable\n");
	return -1;
    }
    ext = *e;

    assert(ext->first <= ext->last);

    handle = ext->last;
    if (ext->first  == ext->last)	{
	/* just gave out the last handle in the range */
	if (avlremove(&(elist->index), ext->first) == 0) {
	    fprintf(stderr, "avlremove: index does not have that item\n");
	    return -1;
	}
	elist->num_extents--;
    } else {
	ext->last--; 
	elist->num_handles--;
    }

    return handle;
}

void extentlist_stats(struct TROVE_handle_extentlist *elist)
{
    printf("handle/extent ratio: %f\n", (double)elist->num_handles/ (double)elist->num_extents);
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
    printf("lb: %Ld ub: %Ld\n", e->first, e->last);
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
int extentlist_hit_cutoff(struct TROVE_handle_extentlist *elist) 
{
    return( (elist->num_handles > EXTENTLIST_CUTOFF) );
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 */
