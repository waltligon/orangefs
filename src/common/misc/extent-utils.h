/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __EXTENT_UTILS_H
#define __EXTENT_UTILS_H

struct llist *PINT_create_extent_list(
    char *extent_str);
int PINT_handle_in_extent(
    struct extent *ext,
    PVFS_handle handle);
int PINT_handle_in_extent_list(
    struct llist *extent_list,
    PVFS_handle handle);
void PINT_release_extent_list(
    struct llist *extent_list);

#endif /* __EXTENT_UTILS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
