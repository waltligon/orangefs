#define ONLY_MSPACES 1
#define MSPACES 1
#define MALLOC_ALIGNMENT 16
#define USE_LOCKS 1
/* HAVE_MORECORE defaults to 0 if ONLY_MSPACES is used. */
#define HAVE_MMAP 0

/*
 * Avoid duplicate symbol errors when compiling 
 *  with ZeptoOS mpi compilers
 */
#pragma weak destroy_mspace
#pragma weak mspace_independent_comalloc
#pragma weak mspace_memalign
#pragma weak mspace_mallinfo
#pragma weak mspace_calloc
#pragma weak mspace_max_footprint
#pragma weak mspace_free
#pragma weak mspace_mallopt
#pragma weak mspace_independent_calloc
#pragma weak create_mspace_with_base
#pragma weak mspace_realloc
#pragma weak mspace_malloc
#pragma weak mspace_trim
#pragma weak mspace_malloc_stats
#pragma weak create_mspace
#pragma weak mspace_footprint

//#include "dlmalloc.h"
#include "dlmalloc.c"

static mspace pool = NULL;

void
zbmi_pool_init(void* start, size_t len)
{
    pool = create_mspace_with_base(start, len, 1);
}

void zbmi_pool_fini(void)
{
    destroy_mspace(pool);
    pool = NULL;
}

void*
zbmi_pool_malloc(size_t bytes)
{
    return mspace_malloc(pool, bytes);
}

void
zbmi_pool_free(void* mem)
{
    mspace_free(pool, mem);
}
