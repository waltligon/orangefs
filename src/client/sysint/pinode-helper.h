/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _PINODE_HELPER_H
#define _PINODE_HELPER_H

#include "acache.h"

int phelper_get_pinode(
    PVFS_pinode_reference pref,
    PINT_pinode **pinode_p,
    uint32_t attrmask,
    PVFS_credentials credentials);

int phelper_fill_timestamps(
    PINT_pinode *pinode);

int phelper_fill_attr(
    PINT_pinode *pinode,
    PVFS_object_attr attr);

int phelper_release_pinode(
    PINT_pinode *pinode);

#endif
