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
PINT_llist *PINT_create_extent_list(char *extent_str)
{
    PVFS_handle_extent cur_extent, *new_extent = NULL;
    PINT_llist *extent_list = NULL;
    int status = 0;

    if (extent_str)
    {
        extent_list = PINT_llist_new();
        assert(extent_list);

        while(PINT_parse_handle_ranges(extent_str,&cur_extent,&status))
        {
            new_extent = malloc(sizeof(PVFS_handle_extent));
            assert(new_extent);

            new_extent->first = cur_extent.first;
            new_extent->last = cur_extent.last;

            PINT_llist_add_to_tail(extent_list,(void *)new_extent);
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
    return ((handle > ext->first-1) &&
            (handle < ext->last+1));
}

/* PINT_handle_in_extent_array()
 *
 * Parameters:
 * PVFS_handle_extent_array    - array of extents
 * PVFS_handle                 - a handle
 *
 * Returns 1 if the specified handle is within any of the
 * extents in the specified list of extents.  Returns 0
 * otherwise.
 *
 */
int PINT_handle_in_extent_array(
    PVFS_handle_extent_array *ext_array, PVFS_handle handle)
{
    int i, ret;
    for(i = 0; i < ext_array->extent_count; ++i)
    {
        ret = PINT_handle_in_extent(&ext_array->extent_array[i], handle);
        if(ret)
        {
            return ret;
        }
    }
    return 0;
}


/* PINT_handle_in_extent_list()
 *
 * Parameters:
 * PINT_llist *extent_list   - PINT_llist of extent structures
 * PVFS_handle                 - a handle
 *
 * Returns 1 if the specified handle is within any of the
 * extents in the specified list of extents.  Returns 0
 * otherwise.
 *
 */
int PINT_handle_in_extent_list(
    PINT_llist *extent_list,
    PVFS_handle handle)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    PVFS_handle_extent *cur_extent = NULL;

    if (extent_list)
    {
        cur = extent_list;
        while(cur)
        {
            cur_extent = PINT_llist_head(cur);
            if (!cur_extent)
            {
                break;
            }
            if (PINT_handle_in_extent(cur_extent,handle))
            {
                ret = 1;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/* PINT_extent_list_count_total()
 *
 * counts the total number of handles represented in an extent list
 *
 * returns the 0 on success and fills in the specified count argument
 * with the extent count total.  returns -PVFS_error on error
 */
uint64_t PINT_extent_array_count_total(
    PVFS_handle_extent_array *extent_array)
{
    int i;
    uint64_t count = 0;

    for(i = 0; i < extent_array->extent_count; ++i)
    {
        count += (extent_array->extent_array[i].last -
                  extent_array->extent_array[i].first + 1);
    }
    return count;
}

/* PINT_release_extent_list()
 *
 * Parameters:
 * PINT_llist *extent_list   - PINT_llist of extent structures
 *
 * Frees extent objects within the specified extent_list if any.
 *
 */
void PINT_release_extent_list(PINT_llist *extent_list)
{
    if (extent_list)
    {
        PINT_llist_free(extent_list,free);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
