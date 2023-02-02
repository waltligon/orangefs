/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "pvfs2-internal.h"
#include "pvfs2-debug.h"
#include "gossip.h"

/* This function takes a string of comma separated key words in
 * event_logging and extracts them one at a time into t.  t is then
 * searched for in the table mask_map, found in
 * src/common/gossip/gossip-flags.c.  When found, the tablle indicates
 * the numerical value for the flag, which is or'd together one ata time
 * to form the final mask.  Negative inputs indicate the bitwise not of
 * the binary value should be included.
 */
static PVFS_debug_mask debug_to_mask(const __keyword_mask_t *mask_map, 
                                     int num_mask_map,
                                     const char *event_logging)
{
    PVFS_debug_mask mask = {GOSSIP_NO_DEBUG_INIT};
    char *s = NULL, *t = NULL;
    const char *toks = ", ";
    int i = 0, negate = 0;

    if (event_logging)
    {
        s = strdup(event_logging);
        t = strtok(s, toks);

        while(t)
        {
            gossip_debug(GOSSIP_GOSSIP_DEBUG, "dbg2mask: %lx %lx\n",
                         mask.mask1, mask.mask2);
            if (*t == '-')
            {
                negate = 1;
                ++t;
            }

            for(i = 0; i < num_mask_map; i++)
            {
                if (!strcmp(t, mask_map[i].keyword))
                {
                    if (negate)
                    {
                        mask = DBG_AND(2, mask, DBG_NOT(mask_map[i].mask));
                    }
                    else
                    {
                        mask = DBG_OR(2, mask, mask_map[i].mask);
                    }
                    break;
                }
            }
            t = strtok(NULL, toks);
        }
        free(s);
        gossip_debug(GOSSIP_GOSSIP_DEBUG, "dbg2mask: %lx %lx\n",
                    mask.mask1, mask.mask2);
    }
    return mask;
}

/*
 * Based on human readable keywords, translate them into
 * a mask value appropriate for the debugging level desired.
 * The 'computed' mask is returned; 0 if no keywords are
 * present or recognized.
 *
 * Prefix a keyword with "-" to turn it off.  All keywords
 * processed in specified order.
 */
PVFS_debug_mask PVFS_debug_eventlog_to_mask(const char *event_logging)
{
    //int num_keyword_mask_map = (int)           
    //    (sizeof(s_keyword_mask_map) / sizeof(__keyword_mask_t));

    return debug_to_mask(s_keyword_mask_map, 
                         num_keyword_mask_map, 
                         event_logging);
}

PVFS_debug_mask PVFS_kmod_eventlog_to_mask(const char *event_logging)
{
    //int num_kmod_keyword_mask_map = (int)           
    //    (sizeof(s_kmod_keyword_mask_map) / sizeof(__keyword_mask_t));

    return debug_to_mask(s_kmod_keyword_mask_map, 
                         num_kmod_keyword_mask_map, 
                         event_logging);
}

/*
 * returns the keyword matching the specified position.
 * returns NULL when an invlalid position is requested.
 * 
 * to simply iterate all keywords, position should start at 0
 * and be incremented repeatedly until this method returns NULL.
 */
const char *PVFS_debug_get_next_debug_keyword(int position)
{
    return (((position > -1) && (position < num_keyword_mask_map)) ?
            s_keyword_mask_map[position].keyword : NULL);
}

/* static table for decodinng a ds type variable
 */
static const char *typestring_array[] = 
{
    "PVFS_TYPE_NONE",
    "PVFS_TYPE_METAFILE",
    "PVFS_TYPE_DATAFILE",
    "PVFS_TYPE_DIRECTORY",
    "PVFS_TYPE_SYMLINK",
    "PVFS_TYPE_DIRDATA",
    "PVFS_TYPE_INTERNAL",
    NULL
};

/* convert a PVFS_ds_type variable into a string with a human
 * readable representation of the object type
 */
const char *PVFS_ds_type_to_string(PVFS_ds_type dstype)
{
    int ix;
    PVFS_ds_type_to_int(dstype, &ix);
    return typestring_array[ix];
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
