/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file just defines debugging masks to be used with the gossip
 * logging utility.  All debugging masks for PVFS2 are kept here to make
 * sure we don't have collisions.
 */

#ifndef __PVFS2_DEBUG_H
#define __PVFS2_DEBUG_H

#include <string.h>

enum
{
    BMI_DEBUG_TCP =	    (1 << 0),
    BMI_DEBUG_CONTROL =	    (1 << 1),
    BMI_DEBUG_OFFSETS =	    (1 << 2),
    BMI_DEBUG_GM =	    (1 << 3),
    JOB_DEBUG =		    (1 << 4),
    SERVER_DEBUG =	    (1 << 5),
    STO_DEBUG_CTRL =	    (1 << 6),
    STO_DEBUG_DEFAULT =	    (1 << 7),
    FLOW_DEBUG =	    (1 << 8),
    BMI_DEBUG_GM_MEM =	    (1 << 9),
    REQUEST_DEBUG =	    (1 << 10),
    FLOW_PROTO_DEBUG =	    (1 << 11),
    DCACHE_DEBUG =	    (1 << 12),
    CLIENT_DEBUG =	    (1 << 13),
    REQ_SCHED_DEBUG =	    (1 << 14),
    PCACHE_DEBUG =	    (1 << 15),
    TROVE_DEBUG =           (1 << 16),
    DIST_DEBUG =            (1 << 17),

    BMI_DEBUG_ALL = BMI_DEBUG_TCP + BMI_DEBUG_CONTROL +
	+BMI_DEBUG_GM + BMI_DEBUG_OFFSETS
};

typedef struct 
{
    char *keyword;
    int mask_val;
} keyword_mask_t;

/* map all config keywords to pvfs2 debug masks here */
static keyword_mask_t s_keyword_mask_map[] =
{
    { "storage", TROVE_DEBUG },
    { "trove", TROVE_DEBUG },
    { "network", BMI_DEBUG_ALL },
    { "server", SERVER_DEBUG },
    { "client", CLIENT_DEBUG },
    { "job", JOB_DEBUG },
    { "request", REQUEST_DEBUG },
    { "reqsched", REQ_SCHED_DEBUG },
    { "flowproto", FLOW_PROTO_DEBUG },
    { "flow", FLOW_DEBUG },
    { "pcache", PCACHE_DEBUG },
    { "distribution", DIST_DEBUG }
};

/*
  based on human readable keywords, translate them into
  a mask value appropriate for the debugging level desired.

  the 'computed' mask is returned; 0 if no keywords are
  present or recognized.
*/
static inline int PVFS_debug_eventlog_to_mask(
    char *event_logging)
{
    char *ptr = NULL;
    int mask = 0, i = 0, num_entries = 0;

    num_entries = (sizeof(s_keyword_mask_map) / sizeof(keyword_mask_t));

    if (event_logging)
    {
        for(i = 0; i < num_entries; i++)
        {
            ptr = strstr(event_logging, s_keyword_mask_map[i].keyword);
            if (ptr)
            {
                ptr += (int)strlen(s_keyword_mask_map[i].keyword);
                if ((*ptr == '\0') || (*ptr == ' ') || (*ptr == ','))
                {
                    mask |= s_keyword_mask_map[i].mask_val;
                }
            }
        }
    }
    return mask;
}

#endif /* __PVFS2_DEBUG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
