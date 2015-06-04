/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS2_INTERNAL_H
#define PVFS2_INTERNAL_H

#ifndef __KERNEL__
#include "pvfs2-config.h"

/* in special cases like the statecomp we want to override the config
 * and force the malloc redirect off
 */

#ifdef PVFS_MALLOC_REDEF_OVERRIDE
#  ifdef PVFS_MALLOC_REDEF
#    undef PVFS_MALLOC_REDEF
#  endif
#  define PVFS_MALLOC_REDEF 0
#endif

/* some compiler portability macros - used by gcc maybe not others */
#if __GNUC__ 
#  if __GNUC__ >= 4 && __GNUC_MINOR__ >= 4
#    define GCC_CONSTRUCTOR(priority) __attribute__((constructor(priority)))
#    define GCC_DESTRUCTOR(priority)  __attribute__((destructor(priority)))
#    define GCC_UNUSED  __attribute__((unused))
#    define PVFS_INIT(f) 
#  else
#    define GCC_CONSTRUCTOR(priority) __attribute__((constructor))
#    define GCC_DESTRUCTOR(priority)  __attribute__((destructor))
#    define GCC_UNUSED  __attribute__((unused))
#    define PVFS_INIT(f) 
#  endif
#else
#  define GCC_CONSTRUCTOR(priority) 
#  define GCC_DESTRUCTOR(priority)
#  define GCC_UNUSED
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

/* Temporarily turn PVFS_INIT on
 * This macro is placed in various entry point routines in the library
 * to ensure that the library is initialized before it is used.  In
 * theory we init the libs via GCC_CONSTRUCTOR above which runs the init
 * before mains, BUT other libs may end up calling out lib during their
 * own init routines and foil the plan.  Until we find a way to
 * guarantee our init is done first, we have to have these calls
 * inserted in the code.  The calls DO first make a quick check to see
 * if the lib is initialized so they are not TOO slow (although probably
 * should be inlined).
 */
#if 1
#undef PVFS_INIT
#define PVFS_INIT(f) f()
#endif

/* This should be included everywhere in the code */
#include "pint-malloc.h" 
#endif /* not __KERNEL__ */

/* Printf wrappers for 32- and 64-bit compatibility.  Imagine trying
 * to print out a PVFS_handle, which is typedefed to a uint64_t.  On
 * a 32-bit machine, you use format "%llu", while a 64-bit machine wants
 * the format "%lu", and each machine complains at the use of the opposite.
 * This is only a problem on primitive types that are bigger than the
 * smallest supported machine, i.e. bigger than 32 bits.
 *
 * Rather than changing the printf format string, which is the "right"
 * thing to do, we instead cast the parameters to the printf().  But only
 * on one of the architectures so the other one will complain if the format
 * string really is incorrect.
 *
 * Here we choose 32-bit machines as the dominant type.  If a format
 * specifier and a parameter are mismatched, that machine will issue
 * a warning, while 64-bit machines will silently perform the cast.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#ifdef BITS_PER_LONG
/* don't use SIZEOF_* fields generated at configure */
#if BITS_PER_LONG == 32
#  define llu(x) (x)
#  define lld(x) (x)
#  define SCANF_lld "%lld"
#elif BITS_PER_LONG == 64
#  define llu(x) (unsigned long long)(x)
#  define lld(x) (long long)(x)
#define SCANF_lld "%ld"
#else
#  error Unexpected  BITS_PER_LONG
#endif

#else
 
#if SIZEOF_LONG_INT == 4 
#  define llu(x) (x)
#  define lld(x) (x)
#  define SCANF_lld "%lld"
#  define SCANF_lld_type long
#elif SIZEOF_LONG_INT == 8
#  define llu(x) (unsigned long long)(x)
#  define lld(x) (long long)(x)
#  define SCANF_lld "%ld"
#  define SCANF_lld_type long
#else
#  error Unexpected sizeof(long int)
#endif

#endif /* BITS_PER_LONG */

/* key string definition macros.  These are used by the server and
 * by the client (in the case of xattrs with viewdist, etc).
 */
#define ROOT_HANDLE_KEYSTR      "rh\0"
#define ROOT_HANDLE_KEYLEN      3

#define DIRECTORY_ENTRY_KEYSTR  "de\0"
#define DIRECTORY_ENTRY_KEYLEN  3

#define DATAFILE_HANDLES_KEYSTR "dh\0"
#define DATAFILE_HANDLES_KEYLEN 3

#define METAFILE_DIST_KEYSTR    "md\0"
#define METAFILE_DIST_KEYLEN    3

#define SYMLINK_TARGET_KEYSTR   "st\0"
#define SYMLINK_TARGET_KEYLEN   3

#define METAFILE_LAYOUT_KEYSTR  "ml\0"
#define METAFILE_LAYOUT_KEYLEN  3

#define NUM_DFILES_REQ_KEYSTR   "nd\0"
#define NUM_DFILES_REQ_KEYLEN   3

/* new keys for distributed directory, '/' makes sure no conflict with dirent names */
#define DIST_DIR_ATTR_KEYSTR   "/dda\0"
#define DIST_DIR_ATTR_KEYLEN   5

#define DIST_DIRDATA_HANDLES_KEYSTR   "/ddh\0"
#define DIST_DIRDATA_HANDLES_KEYLEN   5

#define DIST_DIRDATA_BITMAP_KEYSTR   "/ddb\0"
#define DIST_DIRDATA_BITMAP_KEYLEN   5

/* Optional xattrs have "user.pvfs2." as a prefix */
#define SPECIAL_DIST_NAME_KEYSTR        "dist_name\0"
#define SPECIAL_DIST_NAME_KEYLEN         21
#define SPECIAL_DIST_PARAMS_KEYSTR      "dist_params\0"
#define SPECIAL_DIST_PARAMS_KEYLEN       23
#define SPECIAL_NUM_DFILES_KEYSTR       "num_dfiles\0"
#define SPECIAL_NUM_DFILES_KEYLEN        22
#define SPECIAL_METAFILE_HINT_KEYSTR    "meta_hint\0"
#define SPECIAL_METAFILE_HINT_KEYLEN    21
#define SPECIAL_MIRROR_PARAMS_KEYSTR	"mirror\0"
#define SPECIAL_MIRROR_PARAMS_KEYLEN	18
#define SPECIAL_MIRROR_COPIES_KEYSTR    "mirror.copies\0"
#define SPECIAL_MIRROR_COPIES_KEYLEN    25
#define SPECIAL_MIRROR_HANDLES_KEYSTR   "mirror.handles\0"
#define SPECIAL_MIRROR_HANDLES_KEYLEN   26
#define SPECIAL_MIRROR_STATUS_KEYSTR    "mirror.status\0"
#define SPECIAL_MIRROR_STATUS_KEYLEN    25


#define IO_MAX_REGIONS 64

#endif /* PVFS2_INTERNAL_H */
