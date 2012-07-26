#ifndef __AIOVEC_H
#define __AIOVEC_H

#include "internal.h"

/*
 * aiovec.h
 *
 * In many places it is efficient to batch an operation up against multiple
 * extents for one request to Trove. An aiovec is a multi-extent container 
 * which is used for that.
 */

#define AIOVEC_SIZE	6	

struct aiovec {
	unsigned nr;
        struct extent *extent_array[AIOVEC_SIZE];
        PVFS_offset stream_offset_array[AIOVEC_SIZE];
        PVFS_size stream_size_array[AIOVEC_SIZE];
        char *mem_offset_array[AIOVEC_SIZE];
        PVFS_size mem_size_array[AIOVEC_SIZE];
};


static inline void aiovec_init(struct aiovec *pvec)
{
	pvec->nr = 0;
        memset(pvec, 0, sizeof(struct aiovec) );
}

static inline void aiovec_reinit(struct aiovec *pvec)
{
	pvec->nr = 0;
}

static inline unsigned aiovec_count(struct aiovec *pvec)
{
	return pvec->nr;
}

static inline unsigned aiovec_space(struct aiovec *pvec)
{
	return AIOVEC_SIZE - pvec->nr;
}

/*
 * Add an extent to a pagevec.  Returns the number of slots still available.
 */
static inline unsigned aiovec_add(struct aiovec *pvec, struct extent *extent, PVFS_offset pos, PVFS_size fsize, char * memoff, PVFS_size msize)
{
        
	pvec->extent_array[pvec->nr] = extent;
	pvec->stream_offset_array[pvec->nr] = pos;
	pvec->stream_size_array[pvec->nr] = fsize;
	pvec->mem_offset_array[pvec->nr] = memoff;
	pvec->mem_size_array[pvec->nr++] = msize;
	return aiovec_space(pvec);
}


#endif
