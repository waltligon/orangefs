/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "str-utils.h"
#include "extent-utils.h"
#include "llist.h"

/* PINT_create_extent_list()
 *
 * Return an extent llist based on extent string input
 *
 * Parameters:
 * extent_str   - pointer to string
 *
 * Returns an extent list matching structure of
 * the input extent_str on success; returns NULL
 * the extent_str is invalid, or an error occurs
 *
 */
struct llist *PINT_create_extent_list(char *extent_str)
{
    PVFS_handle_extent *cur_extent = NULL;
    struct llist *extent_list = NULL;
    int first = 0, last = 0, status = 0;

    if (extent_str)
    {
        extent_list = llist_new();
        assert(extent_list);

        while(PINT_parse_handle_ranges(extent_str,&first,&last,&status))
        {
            cur_extent = malloc(sizeof(PVFS_handle_extent));
            assert(cur_extent);

            cur_extent->first = (int64_t)first;
            cur_extent->last = (int64_t)last;

            llist_add_to_tail(extent_list,(void *)cur_extent);
        }
    }
    return extent_list;
}

/* PINT_handle_in_extent()
 *
 * Parameters:
 * PVFS_handle_extent   - extent structure
 * PVFS_handle     - a handle
 *
 * Returns 1 if the specified handle is within the
 * range of the specified extent.  Returns 0 otherwise.
 *
 */
int PINT_handle_in_extent(PVFS_handle_extent *ext, PVFS_handle handle)
{
    return (((int64_t)handle > ext->first-1) &&
            ((int64_t)handle < ext->last+1));
}

/* PINT_handle_in_extent_list()
 *
 * Parameters:
 * struct llist *extent_list   - llist of extent structures
 * PVFS_handle                 - a handle
 *
 * Returns 1 if the specified handle is within any of the
 * extents in the specified list of extents.  Returns 0
 * otherwise.
 *
 */
int PINT_handle_in_extent_list(struct llist *extent_list,
                               PVFS_handle handle)
{
    int ret = 0;
    struct llist *cur = NULL;
    PVFS_handle_extent *cur_extent = NULL;

    if (extent_list)
    {
        cur = extent_list;
        while(cur)
        {
            cur_extent = llist_head(cur);
            if (!cur_extent)
            {
                break;
            }
            if (PINT_handle_in_extent(cur_extent,handle))
            {
                ret = 1;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

/* PINT_release_extent_list()
 *
 * Parameters:
 * struct llist *extent_list   - llist of extent structures
 *
 * Frees extent objects within the specified extent_list if any.
 *
 */
void PINT_release_extent_list(struct llist *extent_list)
{
    if (extent_list)
    {
        llist_free(extent_list,free);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
