/*
 * (C) 2003 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS2_TYPES_DEBUG_H
#define PVFS2_TYPES_DEBUG_H

/* This file defined PINT_attrmask_print(), a useful debugging tool for
 * printing the contents of attrmasks.
 */

/* helper function for debugging */
void PINT_attrmask_print(int debug, uint32_t attrmask);

#endif
