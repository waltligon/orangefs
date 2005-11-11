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

#endif /* PVFS2_INTERNAL_H */
