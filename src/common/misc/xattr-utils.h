/* 
 * (C) 2006 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __XATTR_UTILS_H
#define __XATTR_UTILS_H

#include "pvfs2-config.h"

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#endif

#ifndef HAVE_FGETXATTR_PROTOTYPE
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
/* prototype taken from fgetxattr(2) on Fedora FC4 */
ssize_t fgetxattr(int filedes, const char *name, void *value, size_t size);
#else
ssize_t fgetxattr(int filedes, const char *name, void *value, size_t size, int pos, int opts);
#endif
#endif

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


