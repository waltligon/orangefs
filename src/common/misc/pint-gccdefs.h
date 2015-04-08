/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_GCCDEFS_H
#define PINT_GCCDEFS_H

/* some compiler portability macros - used by gcc maybe not others */
#if __GNUC__ 
#  if __GNUC__ >= 4 && __GNUC_MINOR__ >= 4
#    define GCC_CONSTRUCTOR(priority) __attribute__((constructor(priority)))
#    define GCC_DESTRUCTOR(priority)  __attribute__((destructor(priority)))
#    define GCC_UNUSED  __attribute__((__unused__))
#    define GCC_MALLOC __attribute__((__malloc__))
#    define PVFS_INIT(f) 
#  else
#    define GCC_CONSTRUCTOR(priority) __attribute__((__constructor__))
#    define GCC_DESTRUCTOR(priority)  __attribute__((__destructor__))
#    define GCC_MALLOC __attribute__((__malloc__))
#    define GCC_UNUSED  __attribute__((__unused__))
#    define PVFS_INIT(f) 
#  endif
#else
#  define GCC_CONSTRUCTOR(priority) 
#  define GCC_DESTRUCTOR(priority)
#  define GCC_MALLOC
#  define GCC_UNUSED  __attribute__((__unused__))
#  define PVFS_INIT(f) f()
#endif

/* Init priorities define the order of initialization - defined here so
 * it is in one place - each init module should have one of these
 * Init runs from low to high
 */
#define INIT_PRIORITY_MALLOC        1001
#define INIT_PRIORITY_STDIO         1002
#define INIT_PRIORITY_PVFSLIB       1003

/* Cleanup runs from high to low
 * run cleanup stdio before pvfslib
 */
#define CLEANUP_PRIORITY_STDIO      1003
#define CLEANUP_PRIORITY_PVFSLIB    1002

#endif /* PINT_GCCDEFS_H */
