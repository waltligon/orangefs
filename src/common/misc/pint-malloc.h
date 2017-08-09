/* 
 * (C) 2013 Clemson University and The University of Chicago 
 * 
 * See COPYING in top-level directory.
 *
 */

#ifndef PINT_MALLOC_H
#define PINT_MALLOC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* pint-malloc.h should not be included before malloc.h
 * so we'll just go ahead and include it here
 */
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifndef WIN32
/* pint-malloc.c is not used on Windows */
#include <unistd.h>

struct glibc_malloc_ops_s
{
    void *(*malloc)(size_t size);
    void *(*calloc)(size_t nmemb, size_t size);
    int   (*posix_memalign)(void **mem, size_t alignment, size_t size);
    void *(*memalign)(size_t alignment, size_t size);
    void *(*valloc)(size_t size);
    void *(*realloc)(void *mem, size_t size);
    char *(*strdup)(const char *str);
    char *(*strndup)(const char *str, size_t size);
    void  (*free)(void *mem);
    int   (*pipe)(int fds[2]);
    size_t (*write)(int fd, const void *mem, size_t count);
    void  (*close)(int fd);
};

#ifndef PVFS_MALLOC_DEBUG
# define PVFS_MALLOC_DEBUG 0
#endif

#if PVFS_MALLOC_DEBUG
#define __PMDBG   ,char *file,int line
#else
#define __PMDBG
#endif

extern void init_glibc_malloc(void) GCC_CONSTRUCTOR(INIT_PRIORITY_MALLOC);

extern int PINT_check_address(void *ptr) GCC_UNUSED;
extern int PINT_check_malloc(void *ptr) GCC_UNUSED;

extern void *PINT_malloc_minimum(size_t size);

extern void *PINT_malloc (size_t size __PMDBG);
extern void *PINT_calloc (size_t nmemb, size_t size __PMDBG);
extern int   PINT_posix_memalign (void **mem,
                                       size_t alignment,
                                       size_t size __PMDBG);
extern void *PINT_memalign (size_t alignment, size_t size __PMDBG);
extern void *PINT_valloc (size_t size __PMDBG);
extern void *PINT_realloc (void *mem, size_t size __PMDBG);
extern char *PINT_strdup (const char *str __PMDBG);
extern char *PINT_strndup (const char *str, size_t size __PMDBG);
extern void  PINT_free (void *mem __PMDBG);
extern void  PINT_free2(void *mem);

#include "pint-clean-malloc.h"
#else
# define PVFS_MALLOC_REDEF 0
# define PVFS_MALLOC_REDEF_OVERRIDE 1
#endif

/* Defaults if not defined in pvfs2-config.h */

#ifndef PVFS_MALLOC_REDEF
# define PVFS_MALLOC_REDEF 1
#endif

#ifndef PVFS_MALLOC_MAGIC
# define PVFS_MALLOC_MAGIC 1
#endif

#ifndef PVFS_MALLOC_CHECK_ALIGN
# define PVFS_MALLOC_CHECK_ALIGN 1
#endif

#ifndef PVFS_MALLOC_ZERO
# define PVFS_MALLOC_ZERO 1
#endif

#ifndef PVFS_MALLOC_FREE_ZERO
# define PVFS_MALLOC_FREE_ZERO 1
#endif

#define PVFS_MALLOC_MAGIC_NUM 0xFAE00000

#if PVFS_MALLOC_REDEF

/* Make sure code that calls default malloc gets our version */

# if PVFS_MALLOC_DEBUG
#  define __PMDBGARGS ,__FILE__,__LINE__
# else
#  define __PMDBGARGS
# endif 

# ifdef malloc
#  undef malloc
# endif
# define malloc(x) PINT_malloc((x) __PMDBGARGS)

# ifdef calloc
#  undef calloc
# endif
# define calloc(x, y) PINT_calloc((x), (y) __PMDBGARGS)

# ifdef posix_memalign
#  undef posix_memalign
# endif
# define posix_memalign(x, y, z) \
                 PINT_posix_memalign((x), (y), (z) __PMDBGARGS)

# ifdef memalign
#  undef memalign
# endif
# define memalign(x, y) PINT_memalign((x), (y) __PMDBGARGS)

# ifdef valloc
#  undef valloc
# endif
# define valloc(x) PINT_valloc((x) __PMDBGARGS)

# ifdef realloc
#  undef realloc
# endif
# define realloc(x, y) PINT_realloc((x), (y) __PMDBGARGS)

# ifdef strdup
#  undef strdup
# endif
# define strdup(x) PINT_strdup((x) __PMDBGARGS)

# ifdef strndup
#  undef strndup
# endif
# define strndup(x, y) PINT_strndup((x), (y) __PMDBGARGS)

# ifdef free
#  undef free
# endif
# define free(x) PINT_free((x) __PMDBGARGS)

# ifdef cfree
#  undef cfree
# endif
# define cfree(x) PINT_free((x) __PMDBGARGS)

#else /* not PVFS_MALLOC_REDEF */

/* Make sure code that directly calls our malloc just gets the default */

# ifdef malloc
#  undef malloc
# endif
# if !defined(PVFS_MALLOC_REDEF_OVERRIDE) && !defined(WITH_MTRACE)
#  define malloc PINT_malloc_minimum
# endif

# ifdef PINT_malloc
#  undef PINT_malloc
# endif
#if 0
-- ifndef PVFS_MALLOC_REDEF_OVERRIDE
--  define PINT_malloc PINT_malloc_minimum
# endif

# ifdef PINT_calloc
#  undef PINT_calloc
# endif
# define PINT_calloc(w, x, y, z) calloc((w), (x))

# ifdef PINT_posix_memalign
#  undef PINT_posix_memalign
# endif
# define PINT_posix_memalign(v, w, x, y, z) posix_memalign((v), (w), (x))

# ifdef PINT_memalign
#  undef PINT_memalign
# endif
# define PINT_memalign(w, x, y, z) memalign((w), (x))

# ifdef PINT_valloc
#  undef PINT_valloc
# endif
# define PINT_valloc(x, y, z) valloc((x))

# ifdef PINT_realloc
#  undef PINT_realloc
# endif
# define PINT_realloc(w, x, y, z) realloc((w), (x))

# ifdef PINT_strdup
#  undef PINT_strdup
# endif
# define PINT_strdup(x, y, z) strdup((x))

#  ifdef PINT_strndup
#  undef PINT_strndup
# endif
# define PINT_strndup(w, x, y, z) strndup((w), (x))

# ifdef PINT_free
#  undef PINT_free
# endif
# define PINT_free(x, y, z) free((x))

# ifdef PINT_free2
#  undef PINT_free2
# endif
# define PINT_free2 free

#endif

#if !PVFS_MALLOC_ZERO || !PVFS_MALLOC_REDEF
# define ZEROMEM(p, s) memset((p), 0, (s))
#else
# define ZEROMEM(p, s) 
#endif

#if !PVFS_MALLOC_FREE_ZERO || !PVFS_MALLOC_REDEF
# define ZEROFREE(p, s) memset((p), 0, (s))
#else
# define ZEROFREE(p, s) 
#endif

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
