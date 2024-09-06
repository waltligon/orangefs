/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup gossip
 *
 *  Implementation of gossip interface.
 */

#include <stdint.h>
#include <stdio.h>

#ifdef WIN32
#include "wincommon.h"
#else
#include <syslog.h>
#include <sys/time.h>
#endif

#include "pvfs2-internal.h"
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include "pvfs2-debug.h"

/* These are constants used to set gossip debug flags */

PVFS_DEBUG_MASK_DECL(GOSSIP_NO_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_TCP);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_CONTROL);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_OFFSETS);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_GM);
PVFS_DEBUG_MASK_DECL(GOSSIP_JOB_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SERVER_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_STO_DEBUG_CTRL);
PVFS_DEBUG_MASK_DECL(GOSSIP_STO_DEBUG_DEFAULT);
PVFS_DEBUG_MASK_DECL(GOSSIP_FLOW_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_GM_MEM);
PVFS_DEBUG_MASK_DECL(GOSSIP_REQUEST_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_FLOW_PROTO_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_NCACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_CLIENT_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_REQ_SCHED_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_ACACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_TROVE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_TROVE_OP_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_DIST_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_IB);
PVFS_DEBUG_MASK_DECL(GOSSIP_DBPF_ATTRCACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_RACACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_LOOKUP_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_REMOVE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_GETATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_READDIR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_IO_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_DBPF_OPEN_CACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_PERMISSIONS_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_CANCEL_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_MSGPAIR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_CLIENTCORE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_CLIENTCORE_TIMING_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SETATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_MKDIR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_VARSTRIP_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_GETEATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SETEATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_ENDECODE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_DELEATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_ACCESS_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_ACCESS_DETAIL_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_LISTEATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_PERFCOUNTER_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_STATE_MACHINE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_DBPF_KEYVAL_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_LISTATTR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_DBPF_COALESCE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_ACCESS_HOSTNAMES);
PVFS_DEBUG_MASK_DECL(GOSSIP_FSCK_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_MX);
PVFS_DEBUG_MASK_DECL(GOSSIP_BSTREAM_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_PORTALS);
PVFS_DEBUG_MASK_DECL(GOSSIP_USER_DEV_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_DIRECTIO_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_MGMT_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_MIRROR_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_WIN_CLIENT_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SECURITY_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_USRINT_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SECCACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SIDCACHE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_UNEXP_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_UNUSED_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_BMI_DEBUG_ALL);
PVFS_DEBUG_MASK_DECL(GOSSIP_GETATTR_SECURITY_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_SETATTR_SECURITY_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_CONFIG_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_CREATE_DEBUG);
PVFS_DEBUG_MASK_DECL(GOSSIP_GOSSIP_DEBUG);

/* NOTE you MUST add coresponding entries in
 * include/pvfs2-debug.h
 */

/* map all config keywords to pvfs2 debug masks here */
const __keyword_mask_t s_keyword_mask_map[] =
{
    /* Log trove debugging info.  Same as 'trove'.*/
    { "storage", {GOSSIP_TROVE_DEBUG_INIT} },
    /* Log trove debugging info.  Same as 'storage'. */
    { "trove", {GOSSIP_TROVE_DEBUG_INIT} },
    /* Log trove operations. */
    { "trove_op", {GOSSIP_TROVE_OP_DEBUG_INIT} },
    /* Log network debug info. */
    { "network", {GOSSIP_BMI_DEBUG_ALL_INIT} },
    /* Log server info, including new operations. */
    { "server", {GOSSIP_SERVER_DEBUG_INIT} },
    /* Log client sysint info.  This is only useful for the client. */
    { "client", {GOSSIP_CLIENT_DEBUG_INIT} },
    /* Debug the varstrip distribution */
    { "varstrip", {GOSSIP_VARSTRIP_DEBUG_INIT} },
    /* Log job info */
    { "job", {GOSSIP_JOB_DEBUG_INIT} },
    /* Debug PINT_process_request calls.  EXTREMELY verbose! */
    { "request", {GOSSIP_REQUEST_DEBUG_INIT} },
    /* Log request scheduler events */
    { "reqsched", {GOSSIP_REQ_SCHED_DEBUG_INIT} },
    /* Log the flow protocol events, including flowproto_multiqueue */
    { "flowproto", {GOSSIP_FLOW_PROTO_DEBUG_INIT} },
    /* Log flow calls */
    { "flow", {GOSSIP_FLOW_DEBUG_INIT} },
    /* Debug the client name cache.  Only useful on the client. */
    { "ncache", {GOSSIP_NCACHE_DEBUG_INIT} },
    /* Debug read-ahead cache events.  Only useful on the client. */
    { "racache", {GOSSIP_RACACHE_DEBUG_INIT} },
    /* Debug the attribute cache.  Only useful on the client. */
    { "acache", {GOSSIP_ACACHE_DEBUG_INIT} },
    /* Log/Debug distribution calls */
    { "distribution", {GOSSIP_DIST_DEBUG_INIT} },
    /* Debug the server-side dbpf attribute cache */
    { "dbpfattrcache", {GOSSIP_DBPF_ATTRCACHE_DEBUG_INIT} },
    /* Debug the client lookup state machine. */
    { "lookup", {GOSSIP_LOOKUP_DEBUG_INIT} },
    /* Debug the client remove state macine. */
    { "remove", {GOSSIP_REMOVE_DEBUG_INIT} },
    /* Debug the server getattr state machine. */
    { "getattr", {GOSSIP_GETATTR_DEBUG_INIT} },
    /* Debug the server setattr state machine. */
    { "setattr", {GOSSIP_SETATTR_DEBUG_INIT} },
    /* vectored getattr server state machine */
    { "listattr", {GOSSIP_LISTATTR_DEBUG_INIT} },
    /* Debug the client and server get ext attributes SM. */
    { "geteattr", {GOSSIP_GETEATTR_DEBUG_INIT} },
    /* Debug the client and server set ext attributes SM. */
    { "seteattr", {GOSSIP_SETEATTR_DEBUG_INIT} },
    /* Debug the readdir operation (client and server) */
    { "readdir", {GOSSIP_READDIR_DEBUG_INIT} },
    /* Debug the mkdir operation (server only) */
    { "mkdir", {GOSSIP_MKDIR_DEBUG_INIT} },
    /* Debug the io operation (reads and writes) 
       for both the client and server */
    { "io", {GOSSIP_IO_DEBUG_INIT} },
    /* Debug the server's open file descriptor cache */
    { "open_cache", {GOSSIP_DBPF_OPEN_CACHE_DEBUG_INIT} },
    /* Debug permissions checking on the server */
    { "permissions", {GOSSIP_PERMISSIONS_DEBUG_INIT} },
    /* Debug the cancel operation */
    { "cancel", {GOSSIP_CANCEL_DEBUG_INIT} },
    /* Debug the msgpair state machine */
    { "msgpair", {GOSSIP_MSGPAIR_DEBUG_INIT} },
    /* Debug the client core app */
    { "clientcore", {GOSSIP_CLIENTCORE_DEBUG_INIT} },
    /* Debug the client timing state machines (job timeout, etc.) */
    { "clientcore_timing", {GOSSIP_CLIENTCORE_TIMING_DEBUG_INIT} },
    /* network encoding */
    { "endecode", {GOSSIP_ENDECODE_DEBUG_INIT} },
    /* Show server file (metadata) accesses (both modify and read-only). */
    { "access", {GOSSIP_ACCESS_DEBUG_INIT} },
    /* Show more detailed server file accesses */
    { "access_detail", {GOSSIP_ACCESS_DETAIL_DEBUG_INIT} },
    /* Debug the listeattr operation */
    { "listeattr", {GOSSIP_LISTEATTR_DEBUG_INIT} },
    /* Debug the state machine management code */
    { "sm", {GOSSIP_STATE_MACHINE_DEBUG_INIT} },
    /* Debug the metadata dbpf keyval functions */
    { "keyval", {GOSSIP_DBPF_KEYVAL_DEBUG_INIT} },
    /* Debug the metadata sync coalescing code */
    { "coalesce", {GOSSIP_DBPF_COALESCE_DEBUG_INIT} },
    /* Display the  hostnames instead of IP addrs in debug output */
    { "access_hostnames", {GOSSIP_ACCESS_HOSTNAMES_INIT} },
    /* Show the client device events */
    { "user_dev", {GOSSIP_USER_DEV_DEBUG_INIT} },
    /* Debug the fsck tool */
    { "fsck", {GOSSIP_FSCK_DEBUG_INIT} },
    /* Debug the bstream code */
    { "bstream", {GOSSIP_BSTREAM_DEBUG_INIT} },
    /* Debug trove in direct io mode */
    { "directio", {GOSSIP_DIRECTIO_DEBUG_INIT} },
    /* Debug direct io thread management */
    { "mgmt", {GOSSIP_MGMT_DEBUG_INIT} },
    /* Debug mirroring process */
    { "mirror",{GOSSIP_MIRROR_DEBUG_INIT} },
    /* Windows client */
    { "win_client", {GOSSIP_WIN_CLIENT_DEBUG_INIT} },
    /* Debug robust security code */
    { "security", {GOSSIP_SECURITY_DEBUG_INIT} },
    /* Capability Cache */
    { "seccache", {GOSSIP_SECCACHE_DEBUG_INIT} },
    /* Client User Interface */
    { "usrint", {GOSSIP_USRINT_DEBUG_INIT} },
    /* Sidcache debugging */
    { "sidcache", {GOSSIP_SIDCACHE_DEBUG_INIT} },
    /* unexpected messages debugging */
    { "unexp", {GOSSIP_UNEXP_DEBUG_INIT} },
    /* Configuration file debugging */
    { "getattrsecure", {GOSSIP_GETATTR_SECURITY_DEBUG_INIT} },
    /* Everything except the periodic events.  Useful for debugging */
    { "setattrsecure", {GOSSIP_SETATTR_SECURITY_DEBUG_INIT} },
    /* Config file parsing */
    { "config", {GOSSIP_CONFIG_DEBUG_INIT} },
    /* Create op and state machine server and client */
    { "create", {GOSSIP_CREATE_DEBUG_INIT} },
    /* Debug the debugger, in particular some critial flags and such */
    { "gossip", {GOSSIP_GOSSIP_DEBUG_INIT} },
    /* Everything except the periodic events.  Useful for debugging */
    { "verbose", { __DEBUG_ALL_INIT } },
    /* No debug output */
    { "none", {GOSSIP_NO_DEBUG_INIT} },
    /* Everything */
    { "all",  {__DEBUG_ALL_INIT} }
};

const int num_keyword_mask_map = (int)           \
        (sizeof(s_keyword_mask_map) / sizeof(__keyword_mask_t));

const __keyword_mask_t s_kmod_keyword_mask_map[] = { };

const int num_kmod_keyword_mask_map = (int)           \
        (sizeof(s_kmod_keyword_mask_map) / sizeof(__keyword_mask_t));

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
