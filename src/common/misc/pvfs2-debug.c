/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "pvfs2-debug.h"

static uint64_t debug_to_mask(__keyword_mask_t *mask_map, 
        int num_mask_map, const char *event_logging)
{
    uint64_t mask = 0;
    char *s = NULL, *t = NULL;
    const char *toks = ", ";
    int i = 0, negate = 0;

    if (event_logging)
    {
        s = strdup(event_logging);
        t = strtok(s, toks);

        while(t)
        {
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
                        mask &= ~mask_map[i].mask_val;
                    }
                    else
                    {
                        mask |= mask_map[i].mask_val;
                    }
                    break;
                }
            }
            t = strtok(NULL, toks);
        }
        free(s);
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
uint64_t PVFS_debug_eventlog_to_mask(const char *event_logging)
{
    return debug_to_mask(s_keyword_mask_map, 
            num_keyword_mask_map, event_logging);
}

uint64_t PVFS_kmod_eventlog_to_mask(const char *event_logging)
{
    return debug_to_mask(s_kmod_keyword_mask_map, 
            num_kmod_keyword_mask_map, event_logging);
}

/*
  returns the keyword matching the specified position.
  returns NULL when an invlalid position is requested.
  
  to simply iterate all keywords, position should start at 0
  and be incremented repeatedly until this method returns NULL.
*/
const char *PVFS_debug_get_next_debug_keyword(int position)
{
    int num_entries = (int)(sizeof(s_keyword_mask_map) /
                            sizeof(__keyword_mask_t));

    return (((position > -1) && (position < num_entries)) ?
            s_keyword_mask_map[position].keyword : NULL);
}
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
