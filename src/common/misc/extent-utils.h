/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __EXTENT_UTILS_H
#define __EXTENT_UTILS_H

#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "str-utils.h"
#include "llist.h"

PINT_llist *PINT_create_extent_list(
    char *extent_str);
int PINT_handle_in_extent(
    PVFS_handle_extent *ext,
    PVFS_handle handle);
int PINT_handle_in_extent_list(
    PINT_llist *extent_list,
    PVFS_handle handle);
void PINT_extent_list_count_total(
    PINT_llist *extent_list,
    uint64_t* count);
void PINT_release_extent_list(
    PINT_llist *extent_list);

#endif /* __EXTENT_UTILS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
