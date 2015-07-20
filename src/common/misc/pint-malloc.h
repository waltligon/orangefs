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

extern void init_glibc_malloc(void) GCC_CONSTRUCTOR(INIT_PRIORITY_MALLOC);

extern int PINT_check_address(void *ptr) GCC_UNUSED;
extern int PINT_check_malloc(void *ptr) GCC_UNUSED;

extern void *PINT_malloc_minimum(size_t size);
extern void *PINT_malloc(size_t size);
extern void *PINT_calloc(size_t nmemb, size_t size);
extern int   PINT_posix_memalign(void **mem, size_t alignment, size_t size);
extern void *PINT_memalign(size_t alignment, size_t size);
extern void *PINT_valloc(size_t size);
extern void *PINT_realloc(void *mem, size_t size);
extern char *PINT_strdup(const char *str);
extern char *PINT_strndup(const char *str, size_t size);
extern void  PINT_free(void *mem);

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

# ifdef malloc
#  undef malloc
# endif
# define malloc PINT_malloc

# ifdef calloc
#  undef calloc
# endif
# define calloc PINT_calloc

# ifdef posix_memalign
#  undef posix_memalign
# endif
# define posix_memalign PINT_posix_memalign

# ifdef memalign
#  undef memalign
# endif
# define memalign PINT_memalign

# ifdef valloc
#  undef valloc
# endif
# define valloc PINT_valloc

# ifdef realloc
#  undef realloc
# endif
# define realloc PINT_realloc

# ifdef strdup
#  undef strdup
# endif
# define strdup PINT_strdup

# ifdef strndup
#  undef strndup
# endif
# define strndup PINT_strndup

# ifdef free
#  undef free
# endif
# define free PINT_free

# ifdef cfree
#  undef cfree
# endif
# define cfree PINT_free

#else /* PVFS_MALLOC_REDEF */

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
# define PINT_calloc calloc

# ifdef PINT_posix_memalign
#  undef PINT_posix_memalign
# endif
# define PINT_posix_memalign posix_memalign

# ifdef PINT_memalign
#  undef PINT_memalign
# endif
# define PINT_memalign memalign

# ifdef PINT_valloc
#  undef PINT_valloc
# endif
# define PINT_valloc valloc

# ifdef PINT_realloc
#  undef PINT_realloc
# endif
# define PINT_realloc realloc

# ifdef PINT_strdup
#  undef PINT_strdup
# endif
# define PINT_strdup strdup

#  ifdef PINT_strndup
#  undef PINT_strndup
# endif
# define PINT_strndup strndup

# ifdef PINT_free
#  undef PINT_free
# endif
# define PINT_free free

#endif

#if !PVFS_MALLOC_ZERO || !PVFS_MALLOC_REDEF
# define ZEROMEM(p,s) memset((p), 0, (s))
#else
# define ZEROMEM(p,s) 
#endif

#if !PVFS_MALLOC_FREE_ZERO || !PVFS_MALLOC_REDEF
# define ZEROFREE(p,s) memset((p), 0, (s))
#else
# define ZEROFREE(p,s) 
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
