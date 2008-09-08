/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS2_INTERNAL_H
#define PVFS2_INTERNAL_H

#include "pvfs2-config.h"

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
#elif SIZEOF_LONG_INT == 8
#  define llu(x) (unsigned long long)(x)
#  define lld(x) (long long)(x)
#  define SCANF_lld "%ld"
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

/* Optional xattrs have "user.pvfs2." as a prefix */
#define SPECIAL_DIST_NAME_KEYSTR        "dist_name\0"
#define SPECIAL_DIST_NAME_KEYLEN         21
#define SPECIAL_DIST_PARAMS_KEYSTR      "dist_params\0"
#define SPECIAL_DIST_PARAMS_KEYLEN       23
#define SPECIAL_NUM_DFILES_KEYSTR       "num_dfiles\0"
#define SPECIAL_NUM_DFILES_KEYLEN        22
#define SPECIAL_METAFILE_HINT_KEYSTR    "meta_hint\0"
#define SPECIAL_METAFILE_HINT_KEYLEN    21

#define IO_MAX_REGIONS 64

#endif /* PVFS2_INTERNAL_H */
