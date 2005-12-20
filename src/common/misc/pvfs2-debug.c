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
    uint64_t mask_val;
} __keyword_mask_t;

#define __DEBUG_ALL                                               \
(GOSSIP_TROVE_DEBUG | GOSSIP_BMI_DEBUG_ALL | GOSSIP_SERVER_DEBUG |\
GOSSIP_CLIENT_DEBUG | GOSSIP_JOB_DEBUG | GOSSIP_REQUEST_DEBUG |   \
GOSSIP_REQ_SCHED_DEBUG | GOSSIP_FLOW_PROTO_DEBUG |                \
GOSSIP_FLOW_DEBUG | GOSSIP_NCACHE_DEBUG | GOSSIP_ACACHE_DEBUG |   \
GOSSIP_DIST_DEBUG | GOSSIP_DBPF_ATTRCACHE_DEBUG |                 \
GOSSIP_MMAP_RCACHE_DEBUG | GOSSIP_LOOKUP_DEBUG |                  \
GOSSIP_REMOVE_DEBUG | GOSSIP_GETATTR_DEBUG | GOSSIP_READDIR_DEBUG|\
GOSSIP_IO_DEBUG | GOSSIP_DBPF_OPEN_CACHE_DEBUG |                  \
GOSSIP_PERMISSIONS_DEBUG | GOSSIP_CANCEL_DEBUG |                  \
GOSSIP_MSGPAIR_DEBUG | GOSSIP_CLIENTCORE_DEBUG |                  \
GOSSIP_SETATTR_DEBUG | GOSSIP_MKDIR_DEBUG |                       \
GOSSIP_SETEATTR_DEBUG | GOSSIP_GETEATTR_DEBUG |                   \
GOSSIP_LISTEATTR_DEBUG |                                          \
GOSSIP_ACCESS_DEBUG | GOSSIP_ACCESS_DETAIL_DEBUG |                \
GOSSIP_PERFCOUNTER_DEBUG)

/* map all config keywords to pvfs2 debug masks here */
static __keyword_mask_t s_keyword_mask_map[] =
{
    { "storage", GOSSIP_TROVE_DEBUG },
    { "trove", GOSSIP_TROVE_DEBUG },
    { "trove_op", GOSSIP_TROVE_OP_DEBUG },
    { "network", GOSSIP_BMI_DEBUG_ALL },
    { "server", GOSSIP_SERVER_DEBUG },
    { "client", GOSSIP_CLIENT_DEBUG },
    { "varstrip", GOSSIP_VARSTRIP_DEBUG },
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
    { "getattr", GOSSIP_GETATTR_DEBUG },
    { "setattr", GOSSIP_SETATTR_DEBUG },
    { "geteattr", GOSSIP_GETEATTR_DEBUG },
    { "seteattr", GOSSIP_SETEATTR_DEBUG },
    { "readdir", GOSSIP_READDIR_DEBUG },
    { "mkdir", GOSSIP_MKDIR_DEBUG },
    { "io", GOSSIP_IO_DEBUG },
    { "open_cache", GOSSIP_DBPF_OPEN_CACHE_DEBUG }, 
    { "permissions", GOSSIP_PERMISSIONS_DEBUG }, 
    { "cancel", GOSSIP_CANCEL_DEBUG },
    { "msgpair", GOSSIP_MSGPAIR_DEBUG },
    { "clientcore", GOSSIP_CLIENTCORE_DEBUG },
    { "clientcore_timing", GOSSIP_CLIENTCORE_TIMING_DEBUG },
    { "access", GOSSIP_ACCESS_DEBUG },
    { "access_detail", GOSSIP_ACCESS_DETAIL_DEBUG },
    { "listeattr", GOSSIP_LISTEATTR_DEBUG },
    { "sm", GOSSIP_STATE_MACHINE_DEBUG },
    { "verbose",  (__DEBUG_ALL & ~GOSSIP_REQ_SCHED_DEBUG)},
    { "none", GOSSIP_NO_DEBUG },
    { "all",  __DEBUG_ALL }
};
#undef __DEBUG_ALL

static const int num_keyword_mask_map = (int)           \
(sizeof(s_keyword_mask_map) / sizeof(__keyword_mask_t));

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

            for(i = 0; i < num_keyword_mask_map; i++)
            {
                if (!strcmp(t, s_keyword_mask_map[i].keyword))
                {
                    if (negate)
                    {
                        mask &= ~s_keyword_mask_map[i].mask_val;
                    }
                    else
                    {
                        mask |= s_keyword_mask_map[i].mask_val;
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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
