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

/* a private internal type */
typedef struct 
{
    char *keyword;
    int mask_val;
} __keyword_mask_t;


#define __DEBUG_ALL                                               \
(GOSSIP_TROVE_DEBUG | GOSSIP_BMI_DEBUG_ALL | GOSSIP_SERVER_DEBUG |\
GOSSIP_CLIENT_DEBUG | GOSSIP_JOB_DEBUG | GOSSIP_REQUEST_DEBUG |   \
GOSSIP_REQ_SCHED_DEBUG | GOSSIP_FLOW_PROTO_DEBUG |                \
GOSSIP_FLOW_DEBUG | GOSSIP_NCACHE_DEBUG | GOSSIP_ACACHE_DEBUG |   \
GOSSIP_DIST_DEBUG | GOSSIP_DBPF_ATTRCACHE_DEBUG |                 \
GOSSIP_MMAP_RCACHE_DEBUG | GOSSIP_LOOKUP_DEBUG |                  \
GOSSIP_REMOVE_DEBUG)

/* map all config keywords to pvfs2 debug masks here */
static __keyword_mask_t s_keyword_mask_map[] =
{
    { "storage", GOSSIP_TROVE_DEBUG },
    { "trove", GOSSIP_TROVE_DEBUG },
    { "network", GOSSIP_BMI_DEBUG_ALL },
    { "server", GOSSIP_SERVER_DEBUG },
    { "client", GOSSIP_CLIENT_DEBUG },
    { "job", GOSSIP_JOB_DEBUG },
    { "request", GOSSIP_REQUEST_DEBUG },
    { "reqsched", GOSSIP_REQ_SCHED_DEBUG },
    { "flowproto", GOSSIP_FLOW_PROTO_DEBUG },
    { "flow", GOSSIP_FLOW_DEBUG },
    { "ncache", GOSSIP_NCACHE_DEBUG },
    { "mmaprcache", GOSSIP_MMAP_RCACHE_DEBUG },
    { "acache", GOSSIP_ACACHE_DEBUG },
    { "distribution", GOSSIP_DIST_DEBUG },
    { "dbpfattrcache", GOSSIP_DBPF_ATTRCACHE_DEBUG },
    { "lookup", GOSSIP_LOOKUP_DEBUG },
    { "remove", GOSSIP_REMOVE_DEBUG },
    { "verbose",  (__DEBUG_ALL & ~GOSSIP_REQ_SCHED_DEBUG) },
    { "none", GOSSIP_NO_DEBUG },
    { "all",  __DEBUG_ALL }
};
#undef __DEBUG_ALL

/*
  based on human readable keywords, translate them into
  a mask value appropriate for the debugging level desired.

  the 'computed' mask is returned; 0 if no keywords are
  present or recognized.
*/
int PVFS_debug_eventlog_to_mask(char *event_logging)
{
    char *ptr = NULL;
    int mask = 0, i = 0, num_entries = 0;

    num_entries = (int)(sizeof(s_keyword_mask_map) /
                        sizeof(__keyword_mask_t));
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

/*
  returns the keyword matching the specified position.
  returns NULL when an invlalid position is requested.
  
  to simply iterate all keywords, position should start at 0
  and be incremented repeatedly until this method returns NULL.
*/
char *PVFS_debug_get_next_debug_keyword(int position)
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
