/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef EXTENTLIST_H
#define EXTENTLIST_H

#include <sys/time.h>

#include <trove.h>
#include <trove-internal.h>

#define EXTENTLIST_SIZE 1024
#define EXTENTLIST_TIMECHECK_FREQ 1

/* TODO: this has to be much larger than the pcache timout. the
 * problem is the clients set the pcache timeout.  Our  solution for now is to
 * set the purgatory timeout based on a config file value.  The pcache then
 * will treat that purgatory timeout as a hint for its own timeout.  Not
 * perfect, but it'll get the job done for a lot of cases. */
/* this is in units of seconds */
#define EXTENTLIST_PURGATORY_DEFAULT 50

struct TROVE_handle_extent {
    TROVE_handle first;	/* start of the extent */
    TROVE_handle last;	/* end of region extent covers */
    int64_t index;	/* where this extent sits in backing store ??? */
};

#define AVLDATUM struct TROVE_handle_extent *
#define AVLKEY_TYPE TROVE_handle
#define AVLKEY(p) p->first
/* the 'AVLALTKEY' is a hack to make searching for the extent->last member
 * easier. we get away with it only because we force extents to be
 * non-overlapping */
#define AVLALTKEY(p) p->last

#include "avltree.h"

/* TROVE_handle_extentlist
 *
 * Used in both extentlist.c and ledger.c
 */
struct TROVE_handle_extentlist {
    int64_t __size;		         /* how many elements memory allocated for */
    int64_t num_extents;	         /* how many extents are in the list */
    int64_t num_handles;	         /* how many handles are in the list */
    struct timeval timestamp;	         /* how long elements have sat on list */
    struct TROVE_handle_extent *extents; /* (growable) array  of extents */
    struct avlnode *index;	         /* in-memory index for fast lookups */
};

/* extentlist_xxx - functions for manipulating extent lists */
int extentlist_init(struct TROVE_handle_extentlist * elist );
void extentlist_free(struct TROVE_handle_extentlist *e);
int extentlist_merge(struct TROVE_handle_extentlist *dest, struct TROVE_handle_extentlist *src);
TROVE_handle extentlist_get_and_dec_extent(struct TROVE_handle_extentlist *elist);
TROVE_handle extentlist_get_from_extent(struct TROVE_handle_extentlist *elist, 
	TROVE_extent *extent);
int extentlist_handle_remove(struct TROVE_handle_extentlist *elist, TROVE_handle handle);
void extentlist_show(struct TROVE_handle_extentlist *elist);
void extentlist_count(struct TROVE_handle_extentlist *elist, uint64_t* count);
void extentlist_stats(struct TROVE_handle_extentlist *elist); 
int extentlist_hit_cutoff(struct TROVE_handle_extentlist *elist, TROVE_handle cutoff);
int extentlist_endured_purgatory(struct TROVE_handle_extentlist *querent, struct TROVE_handle_extentlist *reference);
int extentlist_set_purgatory(struct timeval * timeout);
int extentlist_addextent(struct TROVE_handle_extentlist *elist, TROVE_handle first, TROVE_handle last);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
