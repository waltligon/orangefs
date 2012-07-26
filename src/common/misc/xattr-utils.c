/* 
 * (C) 2006 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "xattr-utils.h"

#ifndef HAVE_FGETXATTR
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
ssize_t fgetxattr(int filedes, const char *name, void *value, size_t size)
#else
ssize_t fgetxattr(int filedes, const char *name, void *value, size_t size, int pos, int opts )
#endif
{
    errno = ENOSYS;
    return -1;
}
#endif 

 /*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


