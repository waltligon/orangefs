/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <assert.h>
#include "trove-types.h"
#include "trove-proto.h"
#include "llist.h"
#include "extent-utils.h"
#include "trove-handle-mgmt.h"

/* trove_check_handle_ranges:
 *  internal function to verify that handles
 *  on disk match our assigned handles.
 *  this function is *very* expensive.
 *
 * coll_id: id of collection which we will verify
 * extent_list: llist of legal handle ranges/extents
 *
 * returns on success; -1 otherwise
 */
int trove_check_handle_ranges(TROVE_coll_id coll_id,
                              struct llist *extent_list)
{
    int ret = -1, i = 0, count = 0, op_count = 0;
    TROVE_op_id op_id = 0;
    TROVE_ds_state state = 0;
    TROVE_ds_position pos = TROVE_ITERATE_START;
    static TROVE_handle handles[MAX_NUM_VERIFY_HANDLE_COUNT] = {0};

    if (extent_list)
    {
        count = MAX_NUM_VERIFY_HANDLE_COUNT;

        while(count > 0)
        {
            ret = trove_dspace_iterate_handles(coll_id,&pos,handles,
                                               &count,0,NULL,NULL,&op_id);
            while(ret == 0)
            {
                ret = trove_dspace_test(coll_id,op_id,&op_count,NULL,
                                        NULL,&state);
            }

            if (ret != 1)
            {
                /* gossip or log something */
                printf("trove_dspace_iterate_handles failed\n");
                return -1;
            }

            if (count > 0)
            {
                for(i = 0; i != count; i++)
                {
                    /* check every item in our range list */
                    if (!PINT_handle_in_extent_list(extent_list,
                                                    handles[i]))
                    {
                        /* gossip or log the invalid handle */
                        printf("handle %Ld is invalid (out of bounds)\n",
                               handles[i]);
                        break;
                    }
#if 0
                    else
                    {
                        printf("Handle %Ld is valid (i = %d | "
                               "out_count = %d)\n",
                               handles[i],i,count);
                    }
#endif
                }
                ret = ((i == count) ? 0 : -1);
            }
        }
    }
    return ret;
}

int trove_map_handle_ranges(TROVE_coll_id coll_id,
                            struct llist *extent_list)
{
    int ret = -1;
    struct llist *cur = NULL;
    struct extent *cur_extent = NULL;

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
            assert(cur_extent);

            /* FIXME: add to handle mgmt book keeping */
            printf("* Trove got handle range %Ld-%Ld\n",cur_extent->first,
                   cur_extent->last);
            printf("KLUDGE: We're not adding this to handle mgmt "
                   "book keeping yet\n");
            ret = 0;

            cur = llist_next(cur);
        }
    }
    return ret;
}

int trove_set_handle_ranges(TROVE_coll_id coll_id,
                            char *handle_range_str)
{
    int ret = -1;
    struct llist *extent_list = NULL;

    if (handle_range_str)
    {
        extent_list = PINT_create_extent_list(handle_range_str);
        if (extent_list)
        {
            if (trove_check_handle_ranges(coll_id,extent_list))
            {
                ret = trove_map_handle_ranges(coll_id,extent_list);
            }
            PINT_release_extent_list(extent_list);
        }
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
