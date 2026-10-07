/*
 * macOS has no malloc.h. Enough of the glibc declarations for this tree.
 */
#ifndef ORANGEFS_DARWIN_MALLOC_H
#define ORANGEFS_DARWIN_MALLOC_H

#include <stdlib.h>
#include <stddef.h>

static inline void *memalign(size_t alignment, size_t size)
{
    void *ptr = NULL;

    if (alignment < sizeof(void *))
    {
        alignment = sizeof(void *);
    }
    if (posix_memalign(&ptr, alignment, size) != 0)
    {
        return NULL;
    }
    return ptr;
}

#endif
