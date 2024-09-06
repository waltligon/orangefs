/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This is the master header file for pvfs2.  It pulls in all header
 * files needed by client side for software that operates at or above
 * the system interface level.
 */

#ifndef __PVFS2_H
#define __PVFS2_H

#ifndef PVFS2_VERSION_MAJOR
#define PVFS2_VERSION_MAJOR 3
#define PVFS2_VERSION_MINOR 0
#define PVFS2_VERSION_SUB   7
#endif

#include "pvfs2-types.h"
#include "pvfs2-sysint.h"
#include "pvfs2-debug.h"
#include "pvfs2-util.h"
#include "pvfs2-request.h"
#include "pvfs3-handle.h"

#endif /* __PVFS2_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
