#define ONLY_MSPACES 1
#define MSPACES 1
#define MALLOC_ALIGNMENT 16
#define USE_LOCKS 1
/* HAVE_MORECORE defaults to 0 if ONLY_MSPACES is used. */
#define HAVE_MMAP 0

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
