/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#if !defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 600
#define _XOPEN_SOURCE 600
#endif

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "pvfs2-config.h"

#ifdef WIN32
#include "wincommon.h"

/* do not declare inline on Windows (can't be exported)*/
#undef inline
#define inline
#endif

/* prototype definitions */
inline void* PINT_mem_aligned_alloc(size_t size, size_t alignment);
inline void PINT_mem_aligned_free(void *ptr);

/* PINT_mem_aligned_alloc()
 *
 * allocates a memory region of the specified size and returns a 
 * pointer to the region.  The address of the memory will be evenly
 * divisible by alignment.
 *
 * returns pointer to memory on success, NULL on failure
 */
inline void* PINT_mem_aligned_alloc(size_t size, size_t alignment)
{
    int ret = 0;
    void *ptr;

#if defined(WIN32)
    ret = 0;
    ptr = _aligned_malloc(size, alignment);
    if (ptr == NULL)
    {
        ret = ENOMEM;
    }
#elif defined(HAVE_LIBEFENCE)    
    /* Electric Fence only works with malloc */
    ptr = malloc(size);
    if (ptr == NULL)
    {
        ret = errno;
    }
#else
    /* bash uses its own malloc implementation without */
    /* posix_memalign - for the moment want to support bash */
    ret = posix_memalign(&ptr, alignment, size);
    /* ptr = memalign(alignment, size); */
    /* ptr = malloc(size); */
#endif
    if(ret != 0)
    {
        errno = ret;
        return NULL;
    }
    memset(ptr, 0, size);
    return ptr;
}

/* PINT_mem_aligned_free()
 *
 * frees memory region previously allocated with
 * PINT_mem_aligned_alloc()
 *
 * no return value
 */
inline void PINT_mem_aligned_free(void *ptr)
{
#ifdef WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


