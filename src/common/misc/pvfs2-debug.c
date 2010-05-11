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
    const char *keyword;
    uint64_t mask_val;
} __keyword_mask_t;

#define __DEBUG_ALL ((uint64_t) -1)

/* map all config keywords to pvfs2 debug masks here */
static __keyword_mask_t s_keyword_mask_map[] =
{
    /* Log trove debugging info.  Same as 'trove'.*/
    { "storage", GOSSIP_TROVE_DEBUG },
    /* Log trove debugging info.  Same as 'storage'. */
    { "trove", GOSSIP_TROVE_DEBUG },
    /* Log trove operations. */
    { "trove_op", GOSSIP_TROVE_OP_DEBUG },
    /* Log network debug info. */
    { "network", GOSSIP_BMI_DEBUG_ALL },
    /* Log server info, including new operations. */
    { "server", GOSSIP_SERVER_DEBUG },
    /* Log client sysint info.  This is only useful for the client. */
    { "client", GOSSIP_CLIENT_DEBUG },
    /* Debug the varstrip distribution */
    { "varstrip", GOSSIP_VARSTRIP_DEBUG },
    /* Log job info */
    { "job", GOSSIP_JOB_DEBUG },
    /* Debug PINT_process_request calls.  EXTREMELY verbose! */
    { "request", GOSSIP_REQUEST_DEBUG },
    /* Log request scheduler events */
    { "reqsched", GOSSIP_REQ_SCHED_DEBUG },
    /* Log the flow protocol events, including flowproto_multiqueue */
    { "flowproto", GOSSIP_FLOW_PROTO_DEBUG },
    /* Log flow calls */
    { "flow", GOSSIP_FLOW_DEBUG },
    /* Debug the client name cache.  Only useful on the client. */
    { "ncache", GOSSIP_NCACHE_DEBUG },
    /* Debug read-ahead cache events.  Only useful on the client. */
    { "mmaprcache", GOSSIP_MMAP_RCACHE_DEBUG },
    /* Debug the attribute cache.  Only useful on the client. */
    { "acache", GOSSIP_ACACHE_DEBUG },
    /* Log/Debug distribution calls */
    { "distribution", GOSSIP_DIST_DEBUG },
    /* Debug the server-side dbpf attribute cache */
    { "dbpfattrcache", GOSSIP_DBPF_ATTRCACHE_DEBUG },
    /* Debug the client lookup state machine. */
    { "lookup", GOSSIP_LOOKUP_DEBUG },
    /* Debug the client remove state macine. */
    { "remove", GOSSIP_REMOVE_DEBUG },
    /* Debug the server getattr state machine. */
    { "getattr", GOSSIP_GETATTR_DEBUG },
    /* Debug the server setattr state machine. */
    { "setattr", GOSSIP_SETATTR_DEBUG },
    /* vectored getattr server state machine */
    { "listattr", GOSSIP_LISTATTR_DEBUG },
    /* Debug the client and server get ext attributes SM. */
    { "geteattr", GOSSIP_GETEATTR_DEBUG },
    /* Debug the client and server set ext attributes SM. */
    { "seteattr", GOSSIP_SETEATTR_DEBUG },
    /* Debug the readdir operation (client and server) */
    { "readdir", GOSSIP_READDIR_DEBUG },
    /* Debug the mkdir operation (server only) */
    { "mkdir", GOSSIP_MKDIR_DEBUG },
    /* Debug the io operation (reads and writes) 
     * for both the client and server */
    { "io", GOSSIP_IO_DEBUG },
    /* Debug the server's open file descriptor cache */
    { "open_cache", GOSSIP_DBPF_OPEN_CACHE_DEBUG }, 
    /* Debug permissions checking on the server */
    { "permissions", GOSSIP_PERMISSIONS_DEBUG }, 
    /* Debug the cancel operation */
    { "cancel", GOSSIP_CANCEL_DEBUG },
    /* Debug the msgpair state machine */
    { "msgpair", GOSSIP_MSGPAIR_DEBUG },
    /* Debug the client core app */
    { "clientcore", GOSSIP_CLIENTCORE_DEBUG },
    /* Debug the client timing state machines (job timeout, etc.) */
    { "clientcore_timing", GOSSIP_CLIENTCORE_TIMING_DEBUG },
    /* network encoding */
    { "endecode", GOSSIP_ENDECODE_DEBUG },
    /* Show server file (metadata) accesses (both modify and read-only). */ 
    { "access", GOSSIP_ACCESS_DEBUG },
    /* Show more detailed server file accesses */
    { "access_detail", GOSSIP_ACCESS_DETAIL_DEBUG },
    /* Debug the listeattr operation */
    { "listeattr", GOSSIP_LISTEATTR_DEBUG },
    /* Debug the state machine management code */
    { "sm", GOSSIP_STATE_MACHINE_DEBUG },
    /* Debug the metadata dbpf keyval functions */
    { "keyval", GOSSIP_DBPF_KEYVAL_DEBUG },
    /* Debug the metadata sync coalescing code */
    { "coalesce", GOSSIP_DBPF_COALESCE_DEBUG },
    /* Display the  hostnames instead of IP addrs in debug output */
    { "access_hostnames", GOSSIP_ACCESS_HOSTNAMES },
    /* Show the client device events */
    { "user_dev", GOSSIP_USER_DEV_DEBUG },
    /* Debug the fsck tool */
    { "fsck", GOSSIP_FSCK_DEBUG },
    /* Debug the bstream code */
    { "bstream", GOSSIP_BSTREAM_DEBUG },
    /* Debug trove in direct io mode */
    {"directio", GOSSIP_DIRECTIO_DEBUG},
    /* trove I/O timing */
    {"iotime", GOSSIP_IO_TIMING},
    /* Everything except the periodic events.  Useful for debugging */
    { "verbose",
      (__DEBUG_ALL & ~(GOSSIP_PERFCOUNTER_DEBUG | GOSSIP_STATE_MACHINE_DEBUG |
                       GOSSIP_ENDECODE_DEBUG | GOSSIP_USER_DEV_DEBUG))
    },
    /* No debug output */
    { "none", GOSSIP_NO_DEBUG },
    /* Everything */
    { "all",  __DEBUG_ALL }
};
#undef __DEBUG_ALL

static const int num_keyword_mask_map = (int)           \
(sizeof(s_keyword_mask_map) / sizeof(__keyword_mask_t));

/* map all kmod keywords to kmod debug masks here */
static __keyword_mask_t s_kmod_keyword_mask_map[] =
{
    {"super" , GOSSIP_SUPER_DEBUG},
    {"inode" , GOSSIP_INODE_DEBUG},
    {"file"  , GOSSIP_FILE_DEBUG},
    {"dir"   , GOSSIP_DIR_DEBUG},
    {"utils" , GOSSIP_UTILS_DEBUG},
    {"wait"  , GOSSIP_WAIT_DEBUG},
    {"acl"   , GOSSIP_ACL_DEBUG},
    {"dcache", GOSSIP_DCACHE_DEBUG},
    {"dev"   , GOSSIP_DEV_DEBUG},
    {"name"  , GOSSIP_NAME_DEBUG},
    {"bufmap", GOSSIP_BUFMAP_DEBUG},
    {"cache" , GOSSIP_CACHE_DEBUG},
    {"proc"  , GOSSIP_PROC_DEBUG},
    {"xattr" , GOSSIP_XATTR_DEBUG},
    {"init"  , GOSSIP_INIT_DEBUG},
    {"none"  , GOSSIP_NO_DEBUG},
    {"all"   , GOSSIP_MAX_DEBUG}
};

static const int num_kmod_keyword_mask_map = (int)           \
(sizeof(s_kmod_keyword_mask_map) / sizeof(__keyword_mask_t));

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
