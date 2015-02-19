/* 
 * (C) 2013 Clemson University and The University of Chicago 
 * 
 * See COPYING in top-level directory.
 *
 */

#ifndef PINT_CLEAN_MALLOC_H
#define PINT_CLEAN_MALLOC_H

#include <pint-gccdefs.h>

extern void *clean_malloc(size_t size) GCC_MALLOC;
extern void *clean_calloc(size_t nmemb, size_t size) GCC_MALLOC;
extern int   clean_posix_memalign(void **ptr, size_t alignment, size_t size);
extern void *clean_memalign(size_t alignment, size_t size) GCC_MALLOC;
extern void *clean_valloc(size_t size) GCC_MALLOC;
extern void *clean_realloc(void *ptr, size_t size);
extern char *clean_strdup(const char *str) GCC_MALLOC;
extern char *clean_strndup(const char *str, size_t n) GCC_MALLOC;
extern void  clean_free(void *ptr);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
