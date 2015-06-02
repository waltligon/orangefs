
/* 
 * (C) 2013 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 *
 */

/* These are standard declaration and  must go before the undefs below */
#include "pvfs2-config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

/* locally configured options - must be edited here before compile */
#if 0
#define memdebug fprintf
#else
#define memdebug(stream, format, ...)
#endif

/*
 * These functions call the default version of the various
 * calls redefined here in case some code needs to call them - bypassing
 * all that we have here.  This is usually when mallocing memory that
 * will be freed by the caller and the caller is not our code.  Some
 * routines in client/usrint/stdio.h do this.  There may be others.
 *
 * These must go before pint-malloc.h which defines macros that will
 * conflict here.  There may be macros in std declareations we don't
 * want to interfere with so we keep these before the lower includes
 */

#include "pint-clean-malloc.h"

void *clean_malloc(size_t size)
{
    return malloc(size);
}

void *clean_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void *clean_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void *clean_valloc(size_t size)
{
    return valloc(size);
}

void *clean_memalign(size_t alignment, size_t size)

{
#ifdef __DARWIN__
    void *ptr;
    int rc;
    rc = posix_memalign(&ptr, alignment, size);
    if (rc)
    {
        return ptr;
    }
    else
    {
        return NULL;
    }
#else
    return memalign(alignment, size);
#endif
}

int clean_posix_memalign(void **ptr, size_t alignment, size_t size)
{
    return posix_memalign(ptr, alignment, size);
}

char *clean_strdup(const char *str)
{
    return strdup(str);
}

char *clean_strndup(const char *str, size_t n)
{
    return strndup(str, n);
}

void clean_free(void *ptr)
{
    return free(ptr);
}

/* These need to be after the clean functions which should call whatever
 * the default version is but before the undefs below that undo the
 * macros set in pint-malloc.h so that the glibc functions can call
 * correctly
 */

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pint-malloc.h"
#include "gen-locks.h"
#include "gossip.h"

#if PVFS2_SIZEOF_VOIDP == 64
    typedef uint64_t ptrint_t;
#else
    typedef uint32_t ptrint_t;
#endif

/* we don't want any crazy redefs below
 * want to call real malloc, free, etc.
 * our .h file does lots of defs, and we
 * don't want them here they are for users
 */
#ifdef malloc
#undef malloc
#endif

#ifdef PINT_malloc
#undef PINT_malloc
#endif

#ifdef PINT_malloc_minimum
#undef PINT_malloc_minimum
#endif

#ifdef calloc
#undef calloc
#endif

#ifdef PINT_calloc
#undef PINT_calloc
#endif

#ifdef realloc
#undef realloc
#endif

#ifdef PINT_realloc
#undef PINT_realloc
#endif

#ifdef valloc
#undef valloc
#endif

#ifdef PINT_valloc
#undef PINT_valloc
#endif

#ifdef posix_memalign
#undef posix_memalign
#endif

#ifdef PINT_posix_memalign
#undef PINT_posix_memalign
#endif

#ifdef memalign
#undef memalign
#endif

#ifdef PINT_memalign
#undef PINT_memalign
#endif

#ifdef strdup
#undef strdup
#endif

#ifdef PINT_strdup
#undef PINT_strdup
#endif

#ifdef strndup
#undef strndup
#endif

#ifdef PINT_strndup
#undef PINT_strndup
#endif

#ifdef free
#undef free
#endif

#ifdef PINT_free
#undef PINT_free
#endif

/* Struct to handle PVFS malloc features is allocated just before the
 * returned memory
 */

typedef struct extra_s
{
    void     *mem;
    size_t   size;
#if PVFS_MALLOC_MAGIC
    uint32_t magic;
#endif
#if PVFS_MALLOC_CHECK_ALIGN
    size_t   align;
#endif
} extra_t;

#define EXTRA_SIZE (sizeof(extra_t))

/* These routines call glibc version unless we don't have a pointer to
 * one in which case it calls the default version which we hope is
 * glibc.  We don't want our own macros defined in pint-malloc.h here so
 * these are after the undefs.
 */

static struct glibc_malloc_ops_s glibc_malloc_ops = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static inline void *my_glibc_malloc(size_t size)
{
    if (glibc_malloc_ops.malloc)
    {
        return glibc_malloc_ops.malloc(size);
    }
    else
    {
        init_glibc_malloc();
        if (glibc_malloc_ops.malloc)
        {
            return glibc_malloc_ops.malloc(size);
        }
        else
        {
            return malloc(size);
        }
    }
}

static inline void *my_glibc_realloc(void *mem, size_t size)
{
    if (glibc_malloc_ops.realloc)
    {
        return glibc_malloc_ops.realloc(mem, size);
    }
    else
    {
        init_glibc_malloc();
        if (glibc_malloc_ops.realloc)
        {
            return glibc_malloc_ops.realloc(mem, size);
        }
        else
        {
            return realloc(mem, size);
        }
    }
}

static inline void my_glibc_free(void *mem)
{
    if (glibc_malloc_ops.free)
    {
        return glibc_malloc_ops.free(mem);
    }
    else
    {
        init_glibc_malloc();
        if (glibc_malloc_ops.free)
        {
            return glibc_malloc_ops.free(mem);
        }
        else
        {
            return free(mem);
        }
    }
}

static inline int my_glibc_posix_memalign(void **mem,
                                          size_t alignment,
                                          size_t size)
{
    if (glibc_malloc_ops.posix_memalign)
    {
        return glibc_malloc_ops.posix_memalign(mem, alignment, size);
    }
    else
    {
        init_glibc_malloc();
        if (glibc_malloc_ops.posix_memalign)
        {
            return glibc_malloc_ops.posix_memalign(mem, alignment, size);
        }
        else
        {
            return posix_memalign(mem, alignment, size);
        }
    }
}

int PINT_check_address(void *ptr)
{
    int is_valid = 0;
    int fd[2];
    if (!ptr)
    {
        return 0;
    }
    if (glibc_malloc_ops.pipe(fd) >= 0)
    {
        if (glibc_malloc_ops.write(fd[1], ptr, 128) > 0)
        {
            is_valid = 1;
        }
        else
        {
            is_valid = 0;
        }
    }
    glibc_malloc_ops.close(fd[0]);
    glibc_malloc_ops.close(fd[1]);
    return is_valid;
}

int PINT_check_malloc(void *ptr)
{
    extra_t *extra;

    if (!ptr)
    {
        return 0;
    }
    extra = (void *)((ptrint_t)ptr - EXTRA_SIZE);
    if (!PINT_check_address((void *)extra))
    {
        return 0;
    }
    if (extra->magic == PVFS_MALLOC_MAGIC_NUM)
    {
        return 1;
    }
    return 0;
}

void *PINT_malloc_minimum(size_t size)
{
    void *mem;
    mem = my_glibc_malloc(size);
    if (!mem)
    {
        return NULL;
    }
    memset(mem, 0, size);
    return mem;
}

void *PINT_malloc(size_t size)
{
    void *mem;
    size_t sizeplus;
    extra_t *extra;

    sizeplus = size + EXTRA_SIZE;

    mem = my_glibc_malloc(sizeplus);
    if (!mem)
    {
        return NULL;
    }
#if PVFS_MALLOC_ZERO
    memset(mem, 0, sizeplus);
#endif
    extra = (extra_t *)mem;
#if !PVFS_MALLOC_ZERO
    memset(extra, 0, EXTRA_SIZE);
#endif
    extra->mem   = mem;
#if PVFS_MALLOC_MAGIC
    extra->magic = PVFS_MALLOC_MAGIC_NUM;
#endif
    extra->size  = sizeplus;
#if PVFS_MALLOC_CHECK_ALIGN
    extra->align = 0;
#endif

    memdebug(stderr, "call to MALLOC size %d addr %p returning %p \n",
             (int)size, mem, (void *)((ptrint_t)mem + EXTRA_SIZE));

    return (void *)((ptrint_t)mem + EXTRA_SIZE);
}

void *PINT_calloc(size_t nmemb, size_t size)
{
    return PINT_malloc(nmemb * size);
}

int PINT_posix_memalign(void **mem, size_t alignment, size_t size)
{
    size_t sizeplus;
    size_t alignplus;
    extra_t *extra;
    void *mem_orig;
    void *aligned;

    /* make sure alignment is nonzero and a power of two */
    if (alignment == 0 || !((alignment & (~alignment + 1)) == alignment))
    {
        *mem = NULL;
        return EINVAL;
    }
    /* make sure alignment is at least the size of a pointere */
    alignplus = alignment;
    if (alignment < sizeof(void *))
    {
        alignplus = sizeof(void *);
    }

    sizeplus = size + EXTRA_SIZE + alignplus;

    mem_orig = my_glibc_malloc(sizeplus);
    if (mem_orig == NULL)
    {
        return errno;
    }
#if PVFS_MALLOC_ZERO
    memset(mem_orig, 0, sizeplus);
#endif
    aligned = (void *)(((ptrint_t)mem_orig + EXTRA_SIZE + alignplus - 1) &
                       (~alignplus + 1));
    *mem = aligned;

    extra = (extra_t *)((ptrint_t)aligned - EXTRA_SIZE);
#if !PVFS_MALLOC_ZERO
    memset(extra, 0, EXTRA_SIZE);
#endif
    extra->mem   = mem_orig;
#if PVFS_MALLOC_MAGIC
    extra->magic = PVFS_MALLOC_MAGIC_NUM;
#endif
    extra->size  = sizeplus;
#if PVFS_MALLOC_CHECK_ALIGN
    extra->align = alignment;
#endif

    memdebug(stderr, "call to MEMALIGN size %d addr %p "
                     "align %d returning %p \n",
                     (int)size, mem_orig, (int)alignment, *mem);

    return 0;
}

void *PINT_memalign(size_t alignment, size_t size)
{
    int rc = 0;
    void *mem = NULL;

    rc = PINT_posix_memalign(&mem, alignment, size);
    if (rc)
    {
        errno = rc;
        mem = NULL;
    }
    return mem;
}

void *PINT_valloc(size_t size)
{
    size_t align;
    align = sysconf(_SC_PAGESIZE);
    if (align == -1)
    {
        align = sysconf(_SC_PAGE_SIZE);
        if (align == -1)
        {
            /* can't get page size. assume 4K */
            align = 4096;
        }
    }
    return PINT_memalign(align, size);
}

void *PINT_realloc(void *mem, size_t size)
{
    void *ptr = NULL;
    size_t newsize = 0;
    extra_t *extra = NULL;
    ptrint_t region_offset;

    if (mem == NULL)
    {
        return PINT_malloc(size);
    }

    if (size == 0)
    {
        PINT_free(mem);
        return NULL;
    }

    extra = (void *)((ptrint_t)mem - EXTRA_SIZE);
#if PVFS_MALLOC_MAGIC
    if (extra->magic != PVFS_MALLOC_MAGIC_NUM)
    {
        gossip_err("PINT_realloc: realloc fails magic number test\n");
        gossip_err("mem = %p size = %d, emem = %p, esize = %d\n",
                   mem, (int) size, extra->mem, (int)extra->size);
        return NULL;
    }
#endif
    region_offset = (ptrint_t)mem - (ptrint_t)extra->mem;
    newsize = region_offset + size;
    /* glibc realloc will keep our extra structures in place */
    ptr =  my_glibc_realloc(extra->mem, newsize);
    if (ptr == NULL)
    {
        return NULL;
    }
    extra = (extra_t *)(((ptrint_t)ptr + region_offset) - EXTRA_SIZE);
    extra->mem = ptr;
    extra->size = newsize;

    memdebug(stderr, "call to REALLOC size %d addr %p newaddr %p returned %p\n",
             (int)size, mem, ptr, (void *)((ptrint_t)ptr + region_offset));

    return (void *)((ptrint_t)ptr + region_offset);
}

char *PINT_strdup(const char *str)
{
    int str_size = strlen(str);
    char *new_str = NULL;
    if (str_size < 0)
    {
        return NULL;
    }
    new_str = (char *)PINT_malloc(str_size + 1);
    if (!new_str)
    {
        return NULL;
    }
    memcpy(new_str, str, str_size + 1); /* assume last byte is NULL */
    new_str[str_size] = 0;              /* just to be sure */
    return new_str;
}

char *PINT_strndup(const char *str, size_t size)
{
    int str_size = strlen(str);
    char *new_str = NULL;
    if (str_size > size)
    {
        str_size = size;
    }
    if (str_size < 0)
    {
        return NULL;
    }
    new_str = (char *)PINT_malloc(str_size + 1);
    if (!new_str)
    {
        return NULL;
    }
    memcpy(new_str, str, str_size + 1); /* assume laste byte is NULL */
    new_str[str_size] = 0;              /* just to be sure */
    return new_str;
}

void PINT_free(void *mem)
{
    extra_t *extra;
    void *orig_mem; /* so we can zero the mem before free */

    if (!mem)
    {
        memdebug(stderr, "call to FREE addr %p \n", mem);
        return;
    }

    extra = (void *)((ptrint_t)mem - EXTRA_SIZE);
    orig_mem = extra->mem;

    memdebug(stderr, "call to FREE addr %p real addr %p", mem, orig_mem);
    memdebug(stderr, " size %d", (int)extra->size);
#if PVFS_MALLOC_CHECK_ALIGN
    memdebug(stderr, " align %d", (int)extra->align);
#endif
    memdebug(stderr, "\n");

#if PVFS_MALLOC_MAGIC
    if (extra->magic != PVFS_MALLOC_MAGIC_NUM)
    {
        gossip_lerr("PINT_free: free fails magic number test\n");
        return;
    }
#endif

#if PVFS_MALLOC_FREE_ZERO
    memset(orig_mem, 0, extra->size);
#endif

    my_glibc_free(orig_mem);
}

/* initialize the redirect table for glibc malloc 
 * This expects you to pass a shared library handle that is already open
 * for the library you want to use.
 */
void init_glibc_malloc(void)
{
    static int init_flag = 0;
    static int recurse_flag = 0;
    static gen_mutex_t init_mutex = 
                       (gen_mutex_t)GEN_RECURSIVE_MUTEX_INITIALIZER_NP;
    void *libc_handle;

    /* prevent multiple threads from running this */
    if (init_flag)
    {
        return;
    }
    gen_mutex_lock(&init_mutex);
    if (init_flag || recurse_flag)
    {
        gen_mutex_unlock(&init_mutex);
        return;
    }

    /* running init_glibc_malloc */
    recurse_flag = 1;
    memdebug(stderr, "init_glibc_malloc running\n");

    libc_handle = dlopen("libc.so.6", RTLD_LAZY|RTLD_GLOBAL);
    if (!libc_handle)
    {
        libc_handle = RTLD_DEFAULT;
    }
    /* this structure defined in common/misc/pint-malloc.h */
    glibc_malloc_ops.malloc = dlsym(libc_handle, "malloc");
    glibc_malloc_ops.calloc = dlsym(libc_handle, "calloc");
    glibc_malloc_ops.posix_memalign = dlsym(libc_handle, "posix_memalign");
    glibc_malloc_ops.memalign = dlsym(libc_handle, "memalign");
    glibc_malloc_ops.valloc = dlsym(libc_handle, "valloc");
    glibc_malloc_ops.realloc = dlsym(libc_handle, "realloc");
    glibc_malloc_ops.strdup = dlsym(libc_handle, "strdup");
    glibc_malloc_ops.strndup = dlsym(libc_handle, "strndup");
    glibc_malloc_ops.free = dlsym(libc_handle, "free");
    glibc_malloc_ops.pipe = dlsym(libc_handle, "pipe");
    glibc_malloc_ops.write = dlsym(libc_handle, "write");
    glibc_malloc_ops.close = dlsym(libc_handle, "close");
    if (libc_handle != RTLD_DEFAULT) /* was NEXT but I think that was wrong */
    {
        dlclose(libc_handle);
    }

    /* Finished */
    init_flag = 1;
    recurse_flag = 0;
    gen_mutex_unlock(&init_mutex);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
