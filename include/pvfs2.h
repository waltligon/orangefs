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

#include "pvfs2-types.h"
#include "pvfs2-sysint.h"
#include "pvfs2-debug.h"
#include "pvfs2-util.h"

/* TODO: things that shouldn't be included here in the long run are 
 * below this line, along with some comments to help indicate what needs 
 * to be fixed to make them go away...
 */
/******************************************************************************/

/* needed to get string / path manipulation functions, maybe these should
 * be renamed and put in some kind of helper interface that looks more 
 * like something that's ok to call outside of the system interface?
 */
#include "str-utils.h"

#endif /* __PVFS2_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
