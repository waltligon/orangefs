/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <getopt.h>
#include <net/if.h>
#include <netinet/in.h>

#ifdef __PVFS2_SEGV_BACKTRACE__
#include <execinfo.h>
#define __USE_GNU
#include <ucontext.h>
#endif

#ifdef __PVFS2_SEGV_BACKTRACE__
#include <execinfo.h>
#define __USE_GNU
#include <ucontext.h>
#endif

#ifdef __PVFS2_SEGV_BACKTRACE__
#include <execinfo.h>
#define __USE_GNU
#include <ucontext.h>
#endif

#include "pvfs2.h"
#include "gossip.h"
#include "job.h"
#include "acache.h"
#include "ncache.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-util.h"
#include "pint-cached-config.h"
#include "pvfs2-sysint.h"
#include "server-config-mgr.h"
#include "client-state-machine.h"
#include "pint-perf-counter.h"
#include "pvfs2-encode-stubs.h"
#include "pint-event.h"

#ifdef USE_MMAP_RA_CACHE
#include "mmap-ra-cache.h"
#define MMAP_RA_MIN_THRESHOLD     76800
#define MMAP_RA_MAX_THRESHOLD  16777216
#define MMAP_RA_SMALL_BUF_SIZE    16384
#endif

/*
  an arbitrary limit to the max number of operations we'll support in
  flight at once, and the max number of items we can write into the
  device file as a response
*/
#define MAX_NUM_OPS                 64
#define MAX_LIST_SIZE      MAX_NUM_OPS
#define IOX_HINDEXED_COUNT          64

#define REMOUNT_PENDING     0xFFEEFF33
#define OP_IN_PROGRESS      0xFFEEFF34

/*
  default timeout value to wait for completion of in progress
  operations
*/
#define PVFS2_CLIENT_DEFAULT_TEST_TIMEOUT_MS 10

/*
  uncomment for timing of individual operation information to be
  emitted to the pvfs2-client logging output
*/
#define CLIENT_CORE_OP_TIMING

#ifdef CLIENT_CORE_OP_TIMING
#include "pint-util.h"
#include "pvfs2-internal.h"
#endif

#define DEFAULT_LOGFILE "/tmp/pvfs2-client.log"

typedef struct
{
    /* client side attribute cache timeout; 0 is effectively disabled */
    int acache_timeout;
    int ncache_timeout;
    char* logfile;
    char* logtype;
    unsigned int acache_hard_limit;
    int acache_hard_limit_set;
    unsigned int acache_soft_limit;
    int acache_soft_limit_set;
    unsigned int acache_reclaim_percentage;
    int acache_reclaim_percentage_set;
    unsigned int ncache_hard_limit;
    int ncache_hard_limit_set;
    unsigned int ncache_soft_limit;
    int ncache_soft_limit_set;
    unsigned int ncache_reclaim_percentage;
    int ncache_reclaim_percentage_set;
    unsigned int perf_time_interval_secs;
    unsigned int perf_history_size;
    char* gossip_mask;
    int logstamp_type;
    int logstamp_type_set;
    int child;
    /* kernel module buffer size settings */
    unsigned int dev_buffer_count;
    int dev_buffer_count_set;
    unsigned int dev_buffer_size;
    int dev_buffer_size_set;
    char *events;
} options_t;

/*
  this client core *requires* pthreads now, regardless of if the pvfs2
  system interface has threading enabled or not.  we need it for async
  remounts on restart to retrieve our dynamic mount information (if
  any) from the kernel, which means we call a blocking ioctl that must
  be serviced by our regular handlers.  to do both, we use a thread
  for the blocking ioctl.
*/
static pthread_t remount_thread;
static pthread_mutex_t remount_mutex = PTHREAD_MUTEX_INITIALIZER;
static int remount_complete = 0;

/* used for generating unique dynamic mount point names */
static int dynamic_mount_id = 1;

typedef struct
{
    int is_dev_unexp;
    pvfs2_upcall_t in_upcall;
    pvfs2_downcall_t out_downcall;

    job_status_s jstat;
    struct PINT_dev_unexp_info info;
    PVFS_hint hints;
    
    /* iox requests may post multiple operations at one shot */
    int num_ops, num_incomplete_ops;
    PVFS_sys_op_id op_id;
    PVFS_sys_op_id *op_ids;

#ifdef USE_MMAP_RA_CACHE
    void *io_tmp_buf;
    int readahead_posted;
    char *io_tmp_small_buf[MMAP_RA_SMALL_BUF_SIZE];
#endif
    PVFS_Request file_req;
    PVFS_Request mem_req;
    PVFS_ds_keyval  key;/* used only by geteattr, seteattr */
    PVFS_ds_keyval  val;
    void *io_kernel_mapped_buf;
    /* The next few fields are used only by readx, writex */
    int32_t  iox_count;
    int32_t  *iox_sizes;
    PVFS_size *iox_offsets;
    PVFS_Request *file_req_a;
    PVFS_Request *mem_req_a;

    struct PVFS_sys_mntent* mntent; /* used only by mount */

    int was_handled_inline;
    int was_cancelled_io;

    struct qlist_head hash_link;

    union
    {
        PVFS_sysresp_lookup lookup;
        PVFS_sysresp_create create;
        PVFS_sysresp_symlink symlink;
        PVFS_sysresp_getattr getattr;
        PVFS_sysresp_mkdir mkdir;
        PVFS_sysresp_readdir readdir;
        PVFS_sysresp_statfs statfs;
        PVFS_sysresp_io io;
        PVFS_sysresp_geteattr geteattr;
        PVFS_sysresp_listeattr listeattr;
        PVFS_sysresp_readdirplus readdirplus;
        PVFS_sysresp_io *iox;
    } response;

#ifdef CLIENT_CORE_OP_TIMING
    PINT_time_marker start;
    PINT_time_marker end;
#endif

} vfs_request_t;

static options_t s_opts;

static job_context_id s_client_dev_context;
static int s_client_is_processing = 1;
static int s_client_signal = 0;

/* We have 2 sets of description buffers, one used for staging I/O 
 * and one for readdir/readdirplus */
#define NUM_MAP_DESC 2
static struct PVFS_dev_map_desc s_io_desc[NUM_MAP_DESC];
static struct PINT_dev_params s_desc_params[NUM_MAP_DESC];

static struct PINT_perf_counter* acache_pc = NULL;
static struct PINT_perf_counter* static_acache_pc = NULL;
static struct PINT_perf_counter* ncache_pc = NULL;
/* static char hostname[100]; */

/* used only for deleting all allocated vfs_request objects */
vfs_request_t *s_vfs_request_array[MAX_NUM_OPS] = {NULL};

/* this hashtable is used to keep track of operations in progress */
#define DEFAULT_OPS_IN_PROGRESS_HTABLE_SIZE 67
static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);
static struct qhash_table *s_ops_in_progress_table = NULL;

static void parse_args(int argc, char **argv, options_t *opts);
static void print_help(char *progname);
static void reset_acache_timeout(void);
#ifndef GOSSIP_DISABLE_DEBUG
static char *get_vfs_op_name_str(int op_type);
#endif
static int set_acache_parameters(options_t* s_opts);
static void set_device_parameters(options_t *s_opts);
static void reset_ncache_timeout(void);
static int set_ncache_parameters(options_t* s_opts);
inline static void fill_hints(PVFS_hint *hints, vfs_request_t *req);

static PVFS_object_ref perform_lookup_on_create_error(
    PVFS_object_ref parent,
    char *entry_name,
    PVFS_credentials *credentials,
    int follow_link,
    PVFS_hint hints);

static int write_device_response(
    void *buffer_list,
    int *size_list,
    int list_size,
    int total_size,
    PVFS_id_gen_t tag,
    job_id_t *job_id,
    job_status_s *jstat,
    job_context_id context);

#define write_inlined_device_response(vfs_request)                           \
do {                                                                         \
    void *buffer_list[MAX_LIST_SIZE];                                        \
    int size_list[MAX_LIST_SIZE];                                            \
    int list_size = 0, total_size = 0;                                       \
                                                                             \
    log_operation_timing(vfs_request);                                       \
    buffer_list[0] = &vfs_request->out_downcall;                             \
    size_list[0] = sizeof(pvfs2_downcall_t);                                 \
    total_size = sizeof(pvfs2_downcall_t);                                   \
    list_size = 1;                                                           \
    if(vfs_request->out_downcall.trailer_size > 0)                           \
    {                                                                        \
        buffer_list[1] = vfs_request->out_downcall.trailer_buf;              \
        size_list[1] = vfs_request->out_downcall.trailer_size;               \
        list_size++;                                                         \
        total_size += vfs_request->out_downcall.trailer_size;                \
    }                                                                        \
    ret = write_device_response(                                             \
        buffer_list,size_list,list_size, total_size,                         \
        vfs_request->info.tag, &vfs_request->op_id,                          \
        &vfs_request->jstat, s_client_dev_context);                          \
    if (ret < 0)                                                             \
    {                                                                        \
        gossip_err("write_device_response failed (tag=%lld)\n",              \
                   lld(vfs_request->info.tag));                              \
    }                                                                        \
    vfs_request->was_handled_inline = 1;                                     \
} while(0)

#ifdef __PVFS2_SEGV_BACKTRACE__

#if defined(REG_EIP)
#  define REG_INSTRUCTION_POINTER REG_EIP
#elif defined(REG_RIP)
#  define REG_INSTRUCTION_POINTER REG_RIP
#else
#  error Unknown instruction pointer location for your architecture, configure without --enable-segv-backtrace.
#endif

static void client_segfault_handler(int signum, siginfo_t *info, void *secret)
{
    void *trace[16];
    char **messages = (char **)NULL;
    int i, trace_size = 0;
    ucontext_t *uc = (ucontext_t *)secret;

    /* Do something useful with siginfo_t */
    if (signum == SIGSEGV)
    {
        gossip_err("PVFS2 client: signal %d, faulty address is %p, " 
            "from %p\n", signum, info->si_addr, 
            (void*)uc->uc_mcontext.gregs[REG_INSTRUCTION_POINTER]);
    }
    else
    {
        gossip_err("PVFS2 client: signal %d\n", signum);
    }

    trace_size = backtrace(trace, 16);
    /* overwrite sigaction with caller's address */
    trace[1] = (void *) uc->uc_mcontext.gregs[REG_INSTRUCTION_POINTER];

    messages = backtrace_symbols(trace, trace_size);
    /* skip first stack frame (points here) */
    for (i=1; i<trace_size; ++i)
        gossip_err("[bt] %s\n", messages[i]);

#else
static void client_segfault_handler(int signum)
{
    gossip_err("pvfs2-client-core: caught signal %d\n", signum);
    gossip_disable();
#endif
    abort();
}

static void client_core_sig_handler(int signum)
{
    s_client_is_processing = 0;
    s_client_signal = signum;
}

static int hash_key(void *key, int table_size)
{
    PVFS_id_gen_t tag = *((PVFS_id_gen_t *)key);
    return (tag % table_size);
}

static int hash_key_compare(void *key, struct qlist_head *link)
{
    vfs_request_t *vfs_request = NULL;
    PVFS_id_gen_t tag = *((PVFS_id_gen_t *)key);

    vfs_request = qlist_entry(link, vfs_request_t, hash_link);
    assert(vfs_request);

    return ((vfs_request->info.tag == tag) ? 1 : 0);
}

static int initialize_ops_in_progress_table(void)
{
    if (!s_ops_in_progress_table)
    {
        s_ops_in_progress_table = qhash_init(
            hash_key_compare, hash_key,
            DEFAULT_OPS_IN_PROGRESS_HTABLE_SIZE);
    }
    return (s_ops_in_progress_table ? 0 : -PVFS_ENOMEM);
}

static PVFS_error add_op_to_op_in_progress_table(
    vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;

    if (vfs_request)
    {
        qhash_add(s_ops_in_progress_table,
                  (void *)(&vfs_request->info.tag),
                  &vfs_request->hash_link);
        ret = 0;
    }
    return ret;
}

static PVFS_error cancel_op_in_progress(PVFS_id_gen_t tag)
{
    PVFS_error ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    vfs_request_t *vfs_request = NULL;

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "cancel_op_in_progress called\n");

    hash_link = qhash_search(
        s_ops_in_progress_table, (void *)(&tag));
    if (hash_link)
    {
        vfs_request = qhash_entry(
            hash_link, vfs_request_t, hash_link);
        assert(vfs_request);
        assert(vfs_request->info.tag == tag);

        /* for now, cancellation is ONLY supported on I/O operations */
        assert(vfs_request->in_upcall.type == PVFS2_VFS_OP_FILE_IO);

        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "cancelling I/O req %p "
                     "from tag %lld\n", vfs_request, lld(tag));

        ret = PINT_client_io_cancel(vfs_request->op_id);
        if (ret < 0)
        {
            PVFS_perror_gossip("PINT_client_io_cancel failed", ret);
        }
        /*
          set this flag so we can avoid writing the downcall to
          the kernel since it will be ignored anyway
        */
        vfs_request->was_cancelled_io = 1;
    }
    else
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "op in progress cannot "
                     "be found (tag = %lld)\n", lld(tag));
    }
    return ret;
}

static int is_op_in_progress(vfs_request_t *vfs_request)
{
    int op_found = 0;
    struct qlist_head *hash_link = NULL;
    vfs_request_t *tmp_request = NULL;

    assert(vfs_request);

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "is_op_in_progress called on "
                 "tag %lld\n", lld(vfs_request->info.tag));

    hash_link = qhash_search(
        s_ops_in_progress_table, (void *)(&vfs_request->info.tag));
    if (hash_link)
    {
        tmp_request = qhash_entry(
            hash_link, vfs_request_t, hash_link);
        assert(tmp_request);

        op_found = ((tmp_request->info.tag == vfs_request->info.tag) &&
                    (tmp_request->in_upcall.type ==
                     vfs_request->in_upcall.type));
    }
    return op_found;
}

static PVFS_error remove_op_from_op_in_progress_table(
    vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    vfs_request_t *tmp_vfs_request = NULL;

    if (vfs_request)
    {
        hash_link = qhash_search_and_remove(
            s_ops_in_progress_table, (void *)(&vfs_request->info.tag));
        if (hash_link)
        {
            tmp_vfs_request = qhash_entry(
                hash_link, vfs_request_t, hash_link);
            assert(tmp_vfs_request);
            assert(tmp_vfs_request == vfs_request);
            ret = 0;
        }
    }
    return ret;
}

static void finalize_ops_in_progress_table(void)
{
    int i = 0;
    struct qlist_head *hash_link = NULL;

    if (s_ops_in_progress_table)
    {
        for(i = 0; i < s_ops_in_progress_table->table_size; i++)
        {
            do
            {
                hash_link = qhash_search_and_remove_at_index(
                    s_ops_in_progress_table, i);

            } while(hash_link);
        }
        qhash_finalize(s_ops_in_progress_table);
        s_ops_in_progress_table = NULL;
    }
}

static void *exec_remount(void *ptr)
{
    pthread_mutex_lock(&remount_mutex);
    /*
      when the remount mutex is unlocked, tell the kernel to remount
      any file systems that may have been mounted previously, which
      will fill in our dynamic mount information by triggering mount
      upcalls for each fs mounted by the kernel at this point
     */
    if (PINT_dev_remount())
    {
        gossip_err("*** Failed to remount filesystems!\n");
    }

    remount_complete = 1;
    pthread_mutex_unlock(&remount_mutex);

    return NULL;
}

static inline void log_operation_timing(vfs_request_t *vfs_request)
{
#ifdef CLIENT_CORE_OP_TIMING
    double wtime = 0.0f, utime = 0.0f, stime = 0.0f;

    PINT_time_mark(&vfs_request->end);
    PINT_time_diff(vfs_request->start,
                   vfs_request->end,
                   &wtime, &utime, &stime);

    gossip_debug(
        GOSSIP_CLIENTCORE_TIMING_DEBUG, "%s complete (vfs_request "
        "%p)\n\twtime = %f, utime=%f, stime=%f (seconds)\n",
        get_vfs_op_name_str(vfs_request->in_upcall.type),
        vfs_request, wtime, utime, stime);
#else
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "%s complete (vfs_request %p)\n",
        get_vfs_op_name_str(vfs_request->in_upcall.type),
        vfs_request);
#endif
}

static PVFS_error post_lookup_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got a lookup request for %s (fsid %d | parent %llu)\n",
        vfs_request->in_upcall.req.lookup.d_name,
        vfs_request->in_upcall.req.lookup.parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.lookup.parent_refn.handle));

    /* get rank from pid */
    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_ref_lookup(
        vfs_request->in_upcall.req.lookup.parent_refn.fs_id,
        vfs_request->in_upcall.req.lookup.d_name,
        vfs_request->in_upcall.req.lookup.parent_refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.lookup,
        vfs_request->in_upcall.req.lookup.sym_follow,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        gossip_debug(
            GOSSIP_CLIENTCORE_DEBUG,
            "Posting of lookup failed: %s on fsid %d (ret=%d)!\n",
            vfs_request->in_upcall.req.lookup.d_name,
            vfs_request->in_upcall.req.lookup.parent_refn.fs_id, ret);
    }
    return ret;
}

static PVFS_error post_create_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got a create request for %s (fsid %d | parent %llu)\n",
        vfs_request->in_upcall.req.create.d_name,
        vfs_request->in_upcall.req.create.parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.create.parent_refn.handle));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_create(
        vfs_request->in_upcall.req.create.d_name,
        vfs_request->in_upcall.req.create.parent_refn,
        vfs_request->in_upcall.req.create.attributes,
        &vfs_request->in_upcall.credentials, NULL, NULL,
        &vfs_request->response.create,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting file create failed", ret);
    }
    return ret;
}

static PVFS_error post_symlink_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got a symlink request from %s (fsid %d | parent %llu) to %s\n",
        vfs_request->in_upcall.req.sym.entry_name,
        vfs_request->in_upcall.req.sym.parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.sym.parent_refn.handle),
        vfs_request->in_upcall.req.sym.target);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_symlink(
        vfs_request->in_upcall.req.sym.entry_name,
        vfs_request->in_upcall.req.sym.parent_refn,
        vfs_request->in_upcall.req.sym.target,
        vfs_request->in_upcall.req.sym.attributes,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.symlink,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;
    
    if (ret < 0)
    {
        PVFS_perror_gossip("Posting symlink create failed", ret);
    }
    return ret;
}

static PVFS_error post_getattr_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "got a getattr request for fsid %d | handle %llu\n",
        vfs_request->in_upcall.req.getattr.refn.fs_id,
        llu(vfs_request->in_upcall.req.getattr.refn.handle));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_getattr(
        vfs_request->in_upcall.req.getattr.refn,
        vfs_request->in_upcall.req.getattr.mask,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.getattr,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting getattr failed", ret);
    }
    return ret;
}

static PVFS_error post_setattr_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "got a setattr request for fsid %d | handle %llu [mask %d]\n",
        vfs_request->in_upcall.req.setattr.refn.fs_id,
        llu(vfs_request->in_upcall.req.setattr.refn.handle),
        vfs_request->in_upcall.req.setattr.attributes.mask);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_setattr(
        vfs_request->in_upcall.req.setattr.refn,
        vfs_request->in_upcall.req.setattr.attributes,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting setattr failed", ret);
    }
    return ret;
}

static PVFS_error post_remove_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got a remove request for %s under fsid %d and "
        "handle %llu\n", vfs_request->in_upcall.req.remove.d_name,
        vfs_request->in_upcall.req.remove.parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.remove.parent_refn.handle));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_remove(
        vfs_request->in_upcall.req.remove.d_name,
        vfs_request->in_upcall.req.remove.parent_refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting remove failed",ret);
    }
    return ret;
}

static PVFS_error post_mkdir_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got a mkdir request for %s (fsid %d | parent %llu)\n",
        vfs_request->in_upcall.req.mkdir.d_name,
        vfs_request->in_upcall.req.mkdir.parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.mkdir.parent_refn.handle));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_mkdir(
        vfs_request->in_upcall.req.mkdir.d_name,
        vfs_request->in_upcall.req.mkdir.parent_refn,
        vfs_request->in_upcall.req.mkdir.attributes,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.mkdir,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting mkdir failed", ret);
    }
    return ret;
}

static PVFS_error post_readdir_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "Got a readdir request "
                 "for %llu,%d (token %llu)\n",
                 llu(vfs_request->in_upcall.req.readdir.refn.handle),
                 vfs_request->in_upcall.req.readdir.refn.fs_id,
                 llu(vfs_request->in_upcall.req.readdir.token));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_readdir(
        vfs_request->in_upcall.req.readdir.refn,
        vfs_request->in_upcall.req.readdir.token,
        vfs_request->in_upcall.req.readdir.max_dirent_count,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.readdir,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting readdir failed", ret);
    }
    return ret;
}

static PVFS_error post_readdirplus_request(vfs_request_t *vfs_request)
{
    PVFS_hint hints;
    PVFS_error ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "Got a readdirplus request "
                 "for %llu,%d (token %llu)\n",
                 llu(vfs_request->in_upcall.req.readdirplus.refn.handle),
                 vfs_request->in_upcall.req.readdirplus.refn.fs_id,
                 llu(vfs_request->in_upcall.req.readdirplus.token));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_readdirplus(
        vfs_request->in_upcall.req.readdirplus.refn,
        vfs_request->in_upcall.req.readdirplus.token,
        vfs_request->in_upcall.req.readdirplus.max_dirent_count,
        &vfs_request->in_upcall.credentials,
        vfs_request->in_upcall.req.readdirplus.mask,
        &vfs_request->response.readdirplus,
        &vfs_request->op_id, (void *)vfs_request, hints);

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting readdirplus failed", ret);
    }
    return ret;
}

static PVFS_error post_rename_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got a rename request for %s under fsid %d and "
        "handle %llu to be %s under fsid %d and handle %llu\n",
        vfs_request->in_upcall.req.rename.d_old_name,
        vfs_request->in_upcall.req.rename.old_parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.rename.old_parent_refn.handle),
        vfs_request->in_upcall.req.rename.d_new_name,
        vfs_request->in_upcall.req.rename.new_parent_refn.fs_id,
        llu(vfs_request->in_upcall.req.rename.new_parent_refn.handle));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_rename(
        vfs_request->in_upcall.req.rename.d_old_name,
        vfs_request->in_upcall.req.rename.old_parent_refn,
        vfs_request->in_upcall.req.rename.d_new_name,
        vfs_request->in_upcall.req.rename.new_parent_refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting rename failed", ret);
    }
    return ret;
}

static PVFS_error post_truncate_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "Got a truncate request for %llu under "
        "fsid %d to be size %lld\n",
        llu(vfs_request->in_upcall.req.truncate.refn.handle),
        vfs_request->in_upcall.req.truncate.refn.fs_id,
        lld(vfs_request->in_upcall.req.truncate.size));

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_truncate(
        vfs_request->in_upcall.req.truncate.refn,
        vfs_request->in_upcall.req.truncate.size,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting truncate failed", ret);
    }
    return ret;
}

static PVFS_error post_getxattr_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "got a getxattr request for fsid %d | handle %llu\n",
        vfs_request->in_upcall.req.getxattr.refn.fs_id,
        llu(vfs_request->in_upcall.req.getxattr.refn.handle));

    /* We need to fill in the vfs_request->key field here */
    vfs_request->key.buffer = vfs_request->in_upcall.req.getxattr.key;
    vfs_request->key.buffer_sz = vfs_request->in_upcall.req.getxattr.key_sz;
    gossip_debug( GOSSIP_CLIENTCORE_DEBUG, 
            "getxattr key %s keysz %d\n", 
            (char *) vfs_request->key.buffer, vfs_request->key.buffer_sz);

    /* We also need to allocate memory for the vfs_request->response.geteattr */

    vfs_request->response.geteattr.val_array = 
        (PVFS_ds_keyval *) malloc(sizeof(PVFS_ds_keyval));
    if (vfs_request->response.geteattr.val_array == NULL)
    {
        return -PVFS_ENOMEM;
    }
    vfs_request->response.geteattr.err_array = 
        (PVFS_error *) malloc(sizeof(PVFS_error));
    if(vfs_request->response.geteattr.err_array == NULL)
    {
        free(vfs_request->response.geteattr.val_array);
        return -PVFS_ENOMEM;
    }
    vfs_request->response.geteattr.val_array[0].buffer = 
        (void *) malloc(PVFS_REQ_LIMIT_VAL_LEN);
    if (vfs_request->response.geteattr.val_array[0].buffer == NULL)
    {
        free(vfs_request->response.geteattr.val_array);
        free(vfs_request->response.geteattr.err_array);
        return -PVFS_ENOMEM;
    }
    vfs_request->response.geteattr.val_array[0].buffer_sz = 
        PVFS_REQ_LIMIT_VAL_LEN;

    fill_hints(&hints, vfs_request);
    /* Remember to free these up */
    ret = PVFS_isys_geteattr_list(
        vfs_request->in_upcall.req.getxattr.refn,
        &vfs_request->in_upcall.credentials,
        1,
        &vfs_request->key,
        &vfs_request->response.geteattr,
        &vfs_request->op_id, 
        hints, 
        (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting getxattr failed", ret);
    }
    return ret;
}

static PVFS_error post_setxattr_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "got a setxattr request for fsid %d | handle %llu\n",
        vfs_request->in_upcall.req.setxattr.refn.fs_id,
        llu(vfs_request->in_upcall.req.setxattr.refn.handle));

    /* We need to fill in the vfs_request->key field here */
    vfs_request->key.buffer = vfs_request->in_upcall.req.setxattr.keyval.key;
    vfs_request->key.buffer_sz = 
        vfs_request->in_upcall.req.setxattr.keyval.key_sz;
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "setxattr key %s\n", (char *) vfs_request->key.buffer);
    /* We need to fill in the vfs_request->val field here */
    vfs_request->val.buffer = vfs_request->in_upcall.req.setxattr.keyval.val;
    vfs_request->val.buffer_sz = 
        vfs_request->in_upcall.req.setxattr.keyval.val_sz;

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_seteattr_list(
        vfs_request->in_upcall.req.setxattr.refn,
        &vfs_request->in_upcall.credentials,
        1,
        &vfs_request->key,
        &vfs_request->val,
        vfs_request->in_upcall.req.setxattr.flags,
        &vfs_request->op_id, 
        hints, 
        (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting setattr failed", ret);
    }
    return ret;
}

static PVFS_error post_removexattr_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "got a removexattr request for fsid %d | handle %llu\n",
        vfs_request->in_upcall.req.removexattr.refn.fs_id,
        llu(vfs_request->in_upcall.req.removexattr.refn.handle));

    /* We need to fill in the vfs_request->key field here */
    vfs_request->key.buffer = vfs_request->in_upcall.req.removexattr.key;
    vfs_request->key.buffer_sz = vfs_request->in_upcall.req.removexattr.key_sz;
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "removexattr key %s\n", (char *) vfs_request->key.buffer);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_deleattr(
        vfs_request->in_upcall.req.setxattr.refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->key,
        &vfs_request->op_id, 
        hints,
        (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting deleattr failed", ret);
    }
    return ret;
}

static PVFS_error post_listxattr_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    int i = 0, j = 0;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "got a listxattr request for fsid %d | handle %llu\n",
        vfs_request->in_upcall.req.listxattr.refn.fs_id,
        llu(vfs_request->in_upcall.req.listxattr.refn.handle));

    if (vfs_request->in_upcall.req.listxattr.requested_count < 0
            || vfs_request->in_upcall.req.listxattr.requested_count > PVFS_MAX_XATTR_LISTLEN)
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "listxattr invalid requested count %d\n",
                vfs_request->in_upcall.req.listxattr.requested_count);
        return ret;
    }

    /* We also need to allocate memory for the vfs_request->response.listeattr if the user requested */
    vfs_request->response.listeattr.key_array = 
        (PVFS_ds_keyval *) malloc(sizeof(PVFS_ds_keyval) 
                                  * vfs_request->in_upcall.req.listxattr.requested_count);
    if (vfs_request->response.listeattr.key_array == NULL)
    {
        return -PVFS_ENOMEM;
    }
    for (i = 0; i < vfs_request->in_upcall.req.listxattr.requested_count; i++)
    {
        vfs_request->response.listeattr.key_array[i].buffer_sz = PVFS_MAX_XATTR_NAMELEN;
        vfs_request->response.listeattr.key_array[i].buffer =
                    (char *) malloc(sizeof(char) * vfs_request->response.listeattr.key_array[i].buffer_sz);
        if (vfs_request->response.listeattr.key_array[i].buffer == NULL)
        {
            break;
        }
    }
    if (i != vfs_request->in_upcall.req.listxattr.requested_count)
    {
        for (j = 0; j < i; j++)
        {
            free(vfs_request->response.listeattr.key_array[j].buffer);
        }
        free(vfs_request->response.listeattr.key_array);
        return -PVFS_ENOMEM;
    }

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_listeattr(
        vfs_request->in_upcall.req.listxattr.refn,
        vfs_request->in_upcall.req.listxattr.token,
        vfs_request->in_upcall.req.listxattr.requested_count,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.listeattr,
        &vfs_request->op_id, 
        hints,
        (void *)vfs_request);
    vfs_request->hints = hints;
    
    if (ret < 0)
    {
        PVFS_perror_gossip("Posting listxattr failed", ret);
    }
    return ret;
}


static inline int generate_upcall_mntent(struct PVFS_sys_mntent *mntent,
        pvfs2_upcall_t *in_upcall, int mount) 
{
    char *ptr = NULL, *ptrcomma = NULL;
    char buf[PATH_MAX] = {0};
    /*                                                                
      generate a unique dynamic mount point; the id will be passed to
      the kernel via the downcall so we can match it with a proper
      unmount request at unmount time.  if we're unmounting, use the
      passed in id from the upcall
    */
    if (mount)
        snprintf(buf, PATH_MAX, "<DYNAMIC-%d>", dynamic_mount_id);
    else
        snprintf(buf, PATH_MAX, "<DYNAMIC-%d>", in_upcall->req.fs_umount.id);

    mntent->mnt_dir = strdup(buf);
    if (!mntent->mnt_dir)
    {
        return -PVFS_ENOMEM;
    }

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "Using %s Point %s\n",
                 (mount ? "Mount" : "Unmount"), mntent->mnt_dir);

    if (mount) {
        ptr = rindex(in_upcall->req.fs_mount.pvfs2_config_server,
                     (int)'/');
        ptrcomma = strchr(in_upcall->req.fs_mount.pvfs2_config_server,
                     (int)',');
    } else {
        ptr = rindex(in_upcall->req.fs_umount.pvfs2_config_server,
                     (int)'/');
        ptrcomma = strchr(in_upcall->req.fs_umount.pvfs2_config_server,
                     (int)',');
    }

    if (!ptr || ptrcomma)
    {
        gossip_err("Configuration server MUST be of the form "
                   "protocol://address/fs_name\n");
        return -PVFS_EINVAL;
    }
    *ptr = '\0';
    ptr++;
    /* We do not yet support multi-home for kernel module; needs */
    /* same parsing code as in PVFS_util_parse_pvfstab() and a */
    /* loop around BMI_addr_lookup() to pick one that works. */
    mntent->pvfs_config_servers = (char **) calloc(1, sizeof(char *));
    if (!mntent->pvfs_config_servers)
    {
        return -PVFS_ENOMEM;
    }

    if (mount)
        mntent->pvfs_config_servers[0] = strdup(
            in_upcall->req.fs_mount.pvfs2_config_server);
    else
        mntent->pvfs_config_servers[0] = strdup(
            in_upcall->req.fs_umount.pvfs2_config_server);
                                                                     
    if (!mntent->pvfs_config_servers[0])
    {
        return -PVFS_ENOMEM;
    }
    mntent->the_pvfs_config_server = mntent->pvfs_config_servers[0];
    mntent->num_pvfs_config_servers = 1;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "Got Configuration Server: %s "
        "(len=%d)\n", mntent->the_pvfs_config_server,
        (int)strlen(mntent->the_pvfs_config_server));

    mntent->pvfs_fs_name = strdup(ptr);
    if (!mntent->pvfs_fs_name)
    {
        return -PVFS_ENOMEM;
    }
                                                       
    gossip_debug(                                     
        GOSSIP_CLIENTCORE_DEBUG, "Got FS Name: %s (len=%d)\n",
        mntent->pvfs_fs_name, (int)strlen(mntent->pvfs_fs_name));
                                                              
    mntent->encoding = ENCODING_DEFAULT;                      
    mntent->flowproto = FLOWPROTO_DEFAULT;                   
                                                           
    /* also fill in the fs_id for umount */               
    if (!mount)                                           
        mntent->fs_id = in_upcall->req.fs_umount.fs_id;     
    
    /* By default, the VFS does not wish to perform integrity checks */
    mntent->integrity_check = 0;
    return 0;
}

static PVFS_error post_fs_mount_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_ENODEV;
    /*
      since we got a mount request from the vfs, we know that some
      mntent entries are not filled in, so add some defaults here
      if they weren't passed in the options.
    */
    vfs_request->mntent = (struct PVFS_sys_mntent*)malloc(sizeof(struct
        PVFS_sys_mntent));
    if(!vfs_request->mntent)
    {
        return -PVFS_ENOMEM;
    }
    memset(vfs_request->mntent, 0, sizeof(struct PVFS_sys_mntent));
        
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got an fs mount request for host:\n  %s\n",
        vfs_request->in_upcall.req.fs_mount.pvfs2_config_server);

    ret = generate_upcall_mntent(vfs_request->mntent, &vfs_request->in_upcall, 1);
    if (ret < 0)
    {
        goto failed;
    }
    ret = PVFS_isys_fs_add(vfs_request->mntent, &vfs_request->op_id, (void*)vfs_request);

failed:
    if(ret < 0)
    {
        PVFS_perror_gossip("Posting fs_add failed", ret);
    }

    return ret;
}

static PVFS_error service_fs_umount_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_ENODEV;
    struct PVFS_sys_mntent mntent;

    /*
      since we got a umount request from the vfs, we know that
      some mntent entries are not filled in, so add some defaults
      here if they weren't passed in the options.
    */
    memset(&mntent, 0, sizeof(struct PVFS_sys_mntent));

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG,
        "Got an fs umount request via host %s\n",
        vfs_request->in_upcall.req.fs_umount.pvfs2_config_server);

    ret = generate_upcall_mntent(&mntent, &vfs_request->in_upcall, 0);
    if (ret < 0)
    {
        goto fail_downcall;
    }
    ret = PVFS_sys_fs_remove(&mntent);
    if (ret < 0)
    {
        goto fail_downcall;
    }
    else
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "FS umount ok\n");

        reset_acache_timeout();
        reset_ncache_timeout();

        vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_UMOUNT;
        vfs_request->out_downcall.status = 0;
    }
ok:
    PVFS_util_free_mntent(&mntent);

    /* let handle_unexp_vfs_request() function detect completion and handle */
    vfs_request->op_id = -1;

    return 0;
fail_downcall:
    gossip_err(
        "Failed to umount via host %s\n",
        vfs_request->in_upcall.req.fs_umount.pvfs2_config_server);

    PVFS_perror("Umount failed", ret);

    vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_UMOUNT;
    vfs_request->out_downcall.status = ret;
    goto ok;
}

static PVFS_error service_perf_count_request(vfs_request_t *vfs_request)
{
    char* tmp_str;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "Got a perf count request of type %d\n",
        vfs_request->in_upcall.req.perf_count.type);

    vfs_request->out_downcall.type = vfs_request->in_upcall.type;

    switch(vfs_request->in_upcall.req.perf_count.type)
    {
        case PVFS2_PERF_COUNT_REQUEST_ACACHE:
            tmp_str = PINT_perf_generate_text(acache_pc,
                PERF_COUNT_BUF_SIZE);
            if(!tmp_str)
            {
                vfs_request->out_downcall.status = -PVFS_EINVAL;
            }
            else
            {
                memcpy(vfs_request->out_downcall.resp.perf_count.buffer,
                    tmp_str, PERF_COUNT_BUF_SIZE);
                free(tmp_str);
                vfs_request->out_downcall.status = 0;
            }
            break;

        case PVFS2_PERF_COUNT_REQUEST_STATIC_ACACHE:
            tmp_str = PINT_perf_generate_text(static_acache_pc,
                PERF_COUNT_BUF_SIZE);
            if(!tmp_str)
            {
                vfs_request->out_downcall.status = -PVFS_EINVAL;
            }
            else
            {
                memcpy(vfs_request->out_downcall.resp.perf_count.buffer,
                    tmp_str, PERF_COUNT_BUF_SIZE);
                free(tmp_str);
                vfs_request->out_downcall.status = 0;
            }
            break;

        case PVFS2_PERF_COUNT_REQUEST_NCACHE:
            tmp_str = PINT_perf_generate_text(ncache_pc,
                PERF_COUNT_BUF_SIZE);
            if(!tmp_str)
            {
                vfs_request->out_downcall.status = -PVFS_EINVAL;
            }
            else
            {
                memcpy(vfs_request->out_downcall.resp.perf_count.buffer,
                    tmp_str, PERF_COUNT_BUF_SIZE);
                free(tmp_str);
                vfs_request->out_downcall.status = 0;
            }
            break;

        default:
            /* unsupported request, didn't match anything in case statement */
            vfs_request->out_downcall.status = -PVFS_ENOSYS;
            break;
    }

    /* let handle_unexp_vfs_request() function detect completion and handle */
    vfs_request->op_id = -1;
    return 0;
}

#define ACACHE 0
#define NCACHE 1
static PVFS_error service_param_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    unsigned int val;
    int tmp_param = -1;
    int tmp_subsystem = -1;
    unsigned int tmp_perf_val;

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "Got a param request for op %d\n",
        vfs_request->in_upcall.req.param.op);

    vfs_request->out_downcall.type = vfs_request->in_upcall.type;
    vfs_request->op_id = -1;

    switch(vfs_request->in_upcall.req.param.op)
    {
        /* These first case statements fall through to get/set calls */
        case PVFS2_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS:
            tmp_param = ACACHE_TIMEOUT_MSECS;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT:
            tmp_param = ACACHE_HARD_LIMIT;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT:
            tmp_param = ACACHE_SOFT_LIMIT;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE:
            tmp_param = ACACHE_RECLAIM_PERCENTAGE;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_TIMEOUT_MSECS:
            tmp_param = STATIC_ACACHE_TIMEOUT_MSECS;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_HARD_LIMIT:
            tmp_param = STATIC_ACACHE_HARD_LIMIT;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_SOFT_LIMIT:
            tmp_param = STATIC_ACACHE_SOFT_LIMIT;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_RECLAIM_PERCENTAGE:
            tmp_param = STATIC_ACACHE_RECLAIM_PERCENTAGE;
            tmp_subsystem = ACACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS:
            tmp_param = NCACHE_TIMEOUT_MSECS;
            tmp_subsystem = NCACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT:
            tmp_param = NCACHE_HARD_LIMIT;
            tmp_subsystem = NCACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT:
            tmp_param = NCACHE_SOFT_LIMIT;
            tmp_subsystem = NCACHE;
            break;
        case PVFS2_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE:
            tmp_param = NCACHE_RECLAIM_PERCENTAGE;
            tmp_subsystem = NCACHE;
            break;
        /* These next few case statements return without falling through */
        case PVFS2_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS:
            if(vfs_request->in_upcall.req.param.type ==
                PVFS2_PARAM_REQUEST_GET)
            {
                vfs_request->out_downcall.resp.param.value =
                    s_opts.perf_time_interval_secs;
            }
            else
            {
                s_opts.perf_time_interval_secs = 
                    vfs_request->in_upcall.req.param.value;
            }    
            vfs_request->out_downcall.status = 0;
            return(0);
            break;
        case PVFS2_PARAM_REQUEST_OP_PERF_HISTORY_SIZE:
            if(vfs_request->in_upcall.req.param.type ==
                PVFS2_PARAM_REQUEST_GET)
            {
                ret = PINT_perf_get_info(
                    acache_pc, PINT_PERF_HISTORY_SIZE, &tmp_perf_val);
                vfs_request->out_downcall.resp.param.value = tmp_perf_val;
            }
            else
            {
                tmp_perf_val = vfs_request->in_upcall.req.param.value;
                ret = PINT_perf_set_info(
                    acache_pc, PINT_PERF_HISTORY_SIZE, tmp_perf_val);
                ret = PINT_perf_set_info(
                    static_acache_pc, PINT_PERF_HISTORY_SIZE, tmp_perf_val);
                ret = PINT_perf_set_info(
                    ncache_pc, PINT_PERF_HISTORY_SIZE, tmp_perf_val);
            }    
            vfs_request->out_downcall.status = ret;
            return(0);
            break;
        case PVFS2_PARAM_REQUEST_OP_PERF_RESET:
            if(vfs_request->in_upcall.req.param.type ==
                PVFS2_PARAM_REQUEST_SET)
            {
                PINT_perf_reset(acache_pc);
                PINT_perf_reset(static_acache_pc);
                PINT_perf_reset(ncache_pc);
            }    
            vfs_request->out_downcall.resp.param.value = 0;
            vfs_request->out_downcall.status = 0;
            return(0);
            break;
    }

    if(tmp_param == -1)
    {
        /* unsupported request, didn't match anything in case statement */
        vfs_request->out_downcall.status = -PVFS_ENOSYS;
        return 0;
    }

    /* get or set acache/ncache parameters */
    if(vfs_request->in_upcall.req.param.type ==
        PVFS2_PARAM_REQUEST_GET)
    {
        if(tmp_subsystem == ACACHE)
        {
            vfs_request->out_downcall.status = 
                PINT_acache_get_info(tmp_param, &val);
        }
        else
        {
            vfs_request->out_downcall.status = 
                PINT_ncache_get_info(tmp_param, &val);
        }
        vfs_request->out_downcall.resp.param.value = val;
    }
    else
    {
        val = vfs_request->in_upcall.req.param.value;
        vfs_request->out_downcall.resp.param.value = 0;
        if(tmp_subsystem == ACACHE)
        {
            vfs_request->out_downcall.status = 
                PINT_acache_set_info(tmp_param, val);
        }
        else
        {
            vfs_request->out_downcall.status = 
                PINT_ncache_set_info(tmp_param, val);
        }
    }
    return 0;
}
#undef ACACHE 
#undef NCACHE 

static PVFS_error post_statfs_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "Got a statfs request for fsid %d\n",
        vfs_request->in_upcall.req.statfs.fs_id);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_statfs(
        vfs_request->in_upcall.req.statfs.fs_id,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.statfs,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    vfs_request->out_downcall.status = ret;
    vfs_request->out_downcall.type = vfs_request->in_upcall.type;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting statfs failed", ret);
    }
    return ret;
}

static PVFS_error service_fs_key_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = 0;
    int  key_len;
    char *key;
    struct server_configuration_s *sconfig;

    gossip_debug(
            GOSSIP_CLIENTCORE_DEBUG,
            "service_fs_key_request called for fsid %d\n",
            vfs_request->in_upcall.req.fs_key.fsid);
    /* get a pointer to the server configuration */
    sconfig = PINT_get_server_config_struct(
            vfs_request->in_upcall.req.fs_key.fsid);
    if (sconfig == NULL)
    {
        gossip_err("PINT_get_server_config_struct failed:\n");
        ret = -PVFS_ENOENT;
        goto out;
    }
    /* get a secure shared key for this file system */
    PINT_config_get_fs_key(
            sconfig,
            vfs_request->in_upcall.req.fs_key.fsid, 
            &key, &key_len);
    /* drop reference to the server configuration */
    PINT_put_server_config_struct(sconfig);
    if (key_len == 0)
    {
        ret = 0;
        goto out;
    }
    if (key_len < 0 || key == NULL)
    {
        gossip_err("PINT_config_get_fs_key failed:\n");
        ret = -PVFS_EINVAL;
        goto out;
    }
    /* Copy the key length of the FS */
    vfs_request->out_downcall.resp.fs_key.fs_keylen = 
        key_len > FS_KEY_BUF_SIZE ? FS_KEY_BUF_SIZE : key_len;
    /* Copy the secret key of the FS */
    memcpy(vfs_request->out_downcall.resp.fs_key.fs_key, key,
            vfs_request->out_downcall.resp.fs_key.fs_keylen); 
out:
    vfs_request->out_downcall.status = ret;
    vfs_request->out_downcall.type = vfs_request->in_upcall.type;
    vfs_request->op_id = -1;
    return 0;
}

#ifdef USE_MMAP_RA_CACHE
static PVFS_error post_io_readahead_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    gossip_debug(
        GOSSIP_MMAP_RCACHE_DEBUG,
        "post_io_readahead_request called (%lu bytes)\n",
        (unsigned long)vfs_request->in_upcall.req.io.readahead_size);

    assert((vfs_request->in_upcall.req.io.buf_index > -1) &&
           (vfs_request->in_upcall.req.io.buf_index <
            s_desc_params[BM_IO].dev_buffer_count));

    vfs_request->io_tmp_buf = malloc(
        vfs_request->in_upcall.req.io.readahead_size);
    if (!vfs_request->io_tmp_buf)
    {
        return -PVFS_ENOMEM;
    }

    gossip_debug(
        GOSSIP_MMAP_RCACHE_DEBUG, "+++ [1] allocated %lu bytes at %p\n",
        (unsigned long)vfs_request->in_upcall.req.io.readahead_size,
        vfs_request->io_tmp_buf);

    /* make the full-blown readahead sized request */
    ret = PVFS_Request_contiguous(
        (int32_t)vfs_request->in_upcall.req.io.readahead_size,
        PVFS_BYTE, &vfs_request->mem_req);
    assert(ret == 0);

    ret = PVFS_Request_contiguous(
        (int32_t)vfs_request->in_upcall.req.io.readahead_size,
        PVFS_BYTE, &vfs_request->file_req);
    assert(ret == 0);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_io(
        vfs_request->in_upcall.req.io.refn, vfs_request->file_req, 0,
        vfs_request->io_tmp_buf, vfs_request->mem_req,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.io,
        vfs_request->in_upcall.req.io.io_type,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting file I/O failed", ret);
    }

    vfs_request->readahead_posted = 1;
    return 0;
}
#endif /* USE_MMAP_RA_CACHE */

static PVFS_error post_io_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
#ifdef USE_MMAP_RA_CACHE
    int val = 0, amt_returned = 0;
    void *buf = NULL;

    if (vfs_request->in_upcall.req.io.io_type == PVFS_IO_READ)
    {
        /*
          if a non-zero readahead size and count are specified, check
          the readahead cache for the read data being requested --
          this should always be the case during mmap/execution, but
          never the case during normal I/O reads (to avoid this
          overhead in the common case)
        */
        if ((vfs_request->in_upcall.req.io.count > 0) &&
            (vfs_request->in_upcall.req.io.readahead_size > 0))
        {
            vfs_request->io_tmp_buf = (char *)
                ((vfs_request->in_upcall.req.io.count <=
                  MMAP_RA_SMALL_BUF_SIZE) ?
                 vfs_request->io_tmp_small_buf :
                 malloc(vfs_request->in_upcall.req.io.count));

            if (vfs_request->io_tmp_buf)
            {
                if (vfs_request->io_tmp_buf !=
                    vfs_request->io_tmp_small_buf)
                {
                    gossip_debug(
                        GOSSIP_MMAP_RCACHE_DEBUG, "+++ [2] allocated %lu "
                        "bytes at %p\n", (unsigned long)
                        vfs_request->in_upcall.req.io.count,
                        vfs_request->io_tmp_buf);
                }

                gossip_debug(
                    GOSSIP_MMAP_RCACHE_DEBUG, "[%llu,%d] checking"
                    " for %d bytes at offset %lu\n",
                    llu(vfs_request->in_upcall.req.io.refn.handle),
                    vfs_request->in_upcall.req.io.refn.fs_id,
                    (int)vfs_request->in_upcall.req.io.count,
                    (unsigned long)vfs_request->in_upcall.req.io.offset);

                val = pvfs2_mmap_ra_cache_get_block(
                    vfs_request->in_upcall.req.io.refn,
                    vfs_request->in_upcall.req.io.offset,
                    vfs_request->in_upcall.req.io.count,
                    vfs_request->io_tmp_buf,
                    &amt_returned);

                if (val == 0)
                {
                    goto mmap_ra_cache_hit;
                }

                if (vfs_request->io_tmp_buf !=
                    vfs_request->io_tmp_small_buf)
                {
                    gossip_debug(
                        GOSSIP_MMAP_RCACHE_DEBUG, "--- Freeing %p\n",
                        vfs_request->io_tmp_buf);
                    free(vfs_request->io_tmp_buf);
                }
                vfs_request->io_tmp_buf = NULL;
            }
        }

        /* don't post a readahead req if the size is too big or small */
        if ((vfs_request->in_upcall.req.io.readahead_size <
             MMAP_RA_MAX_THRESHOLD) &&
            (vfs_request->in_upcall.req.io.readahead_size >
             MMAP_RA_MIN_THRESHOLD))
        {
            /* otherwise, check if we can post a readahead request here */
            ret = post_io_readahead_request(vfs_request);
            /*
              if the readahead request succeeds, return.  otherwise
              fallback to normal posting/servicing
            */
            if (ret == 0)
            {
                gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG, "readahead io "
                             "posting succeeded!\n");
                return ret;
            }
        }
    }
#endif /* USE_MMAP_RA_CACHE */

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "posted %s: off %ld size %ld tag: %Ld\n",
            vfs_request->in_upcall.req.io.io_type == PVFS_IO_READ ? "read" : "write",
            (unsigned long) vfs_request->in_upcall.req.io.offset,
            (unsigned long) vfs_request->in_upcall.req.io.count,
            lld(vfs_request->info.tag));
    ret = PVFS_Request_contiguous(
        (int32_t)vfs_request->in_upcall.req.io.count,
        PVFS_BYTE, &vfs_request->mem_req);
    assert(ret == 0);

    assert((vfs_request->in_upcall.req.io.buf_index > -1) &&
           (vfs_request->in_upcall.req.io.buf_index <
            s_desc_params[BM_IO].dev_buffer_count));

    /* get a shared kernel/userspace buffer for the I/O transfer */
    vfs_request->io_kernel_mapped_buf = 
        PINT_dev_get_mapped_buffer(BM_IO, s_io_desc, 
            vfs_request->in_upcall.req.io.buf_index);
    assert(vfs_request->io_kernel_mapped_buf);

    ret = PVFS_Request_contiguous(
        (int32_t)vfs_request->in_upcall.req.io.count,
        PVFS_BYTE, &vfs_request->file_req);
    assert(ret == 0);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_io(
        vfs_request->in_upcall.req.io.refn, vfs_request->file_req,
        vfs_request->in_upcall.req.io.offset, 
        vfs_request->io_kernel_mapped_buf, vfs_request->mem_req,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.io,
        vfs_request->in_upcall.req.io.io_type,
        &vfs_request->op_id, hints, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Posting file I/O failed", ret);
    }
    return ret;

#ifdef USE_MMAP_RA_CACHE
    /* on cache hits, we return immediately with the cached data */
  mmap_ra_cache_hit:

    vfs_request->out_downcall.type = PVFS2_VFS_OP_FILE_IO;
    vfs_request->out_downcall.status = 0;
    vfs_request->out_downcall.resp.io.amt_complete = amt_returned;

    /* get a shared kernel/userspace buffer for the I/O transfer */
    buf = PINT_dev_get_mapped_buffer(BM_IO, s_io_desc,
            vfs_request->in_upcall.req.io.buf_index);
    assert(buf);

    /* copy cached data into the shared user/kernel space */
    memcpy(buf, vfs_request->io_tmp_buf,
           vfs_request->in_upcall.req.io.count);

    if (vfs_request->io_tmp_buf != vfs_request->io_tmp_small_buf)
    {
        gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG,
                     "--- Freeing %p\n", vfs_request->io_tmp_buf);
        free(vfs_request->io_tmp_buf);
    }
    vfs_request->io_tmp_buf = NULL;
    vfs_request->op_id = -1;

    return 0;
#endif /* USE_MMAP_RA_CACHE */
}

static PVFS_error post_iox_request(vfs_request_t *vfs_request)
{
    int32_t i, num_ops_posted, iox_count, iox_index;
    int32_t *mem_sizes = NULL;
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;

    struct read_write_x *rwx = (struct read_write_x *) vfs_request->in_upcall.trailer_buf;

    if (vfs_request->in_upcall.trailer_size <= 0 || rwx == NULL)
    {
        gossip_err("post_iox_request: did not receive any offset-length trailers\n");
        goto out;
    }
    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "%s: size %ld\n",
            vfs_request->in_upcall.req.iox.io_type == PVFS_IO_READ ? "readx" : "writex",
            (unsigned long) vfs_request->in_upcall.req.iox.count);

    if ((vfs_request->in_upcall.req.iox.buf_index < 0) ||
           (vfs_request->in_upcall.req.iox.buf_index >= 
            s_desc_params[BM_IO].dev_buffer_count))
    {
        gossip_err("post_iox_request: invalid buffer index %d\n",
                vfs_request->in_upcall.req.iox.buf_index);
        goto out;
    }

    /* get a shared kernel/userspace buffer for the I/O transfer */
    vfs_request->io_kernel_mapped_buf = 
        PINT_dev_get_mapped_buffer(BM_IO, s_io_desc, 
            vfs_request->in_upcall.req.iox.buf_index);
    if (vfs_request->io_kernel_mapped_buf == NULL)
    {
        gossip_err("post_iox_request: PINT_dev_get_mapped_buffer failed\n");
        goto out;
    }

    /* trailer is interpreted as struct read_write_x */
    if (vfs_request->in_upcall.trailer_size % sizeof(struct read_write_x) != 0)
    {
        gossip_err("post_iox_request: trailer size (%Ld) is not a multiple of read_write_x structure (%ld)\n",
            lld(vfs_request->in_upcall.trailer_size),
            (long) sizeof(struct read_write_x));
        goto out;
    }
    vfs_request->iox_count = vfs_request->in_upcall.trailer_size / sizeof(struct read_write_x);
    /* We will split this in units of IOX_HINDEXED_COUNT */
    num_ops_posted = (vfs_request->iox_count / IOX_HINDEXED_COUNT);
    if (vfs_request->iox_count % IOX_HINDEXED_COUNT != 0)
        num_ops_posted++;
    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "iox: iox_count %d, num_ops_posted %d\n",
            vfs_request->iox_count, num_ops_posted);
    vfs_request->num_ops = vfs_request->num_incomplete_ops = num_ops_posted;
    ret = -PVFS_ENOMEM;
    mem_sizes = (int32_t *) calloc(num_ops_posted, sizeof(int32_t));
    if (mem_sizes == NULL)
    {
        gossip_err("post_iox_request: mem_sizes allocation failed\n");
        goto out;
    }
    vfs_request->iox_sizes = (int32_t *) calloc(vfs_request->iox_count, sizeof(int32_t));
    if (vfs_request->iox_sizes == NULL)
    {
        gossip_err("post_iox_request: iox_sizes allocation failed\n");
        goto out;
    }
    vfs_request->iox_offsets = (PVFS_size *) calloc(vfs_request->iox_count, sizeof(PVFS_size));
    if (vfs_request->iox_offsets == NULL)
    {
        gossip_err("post_iox_request: iox_offsets allocation failed\n");
        goto err_sizes;
    }
    for (i = 0; i < vfs_request->iox_count; i++)
    {
        vfs_request->iox_sizes[i] = (int32_t) rwx->len;
        vfs_request->iox_offsets[i] = rwx->off;
        mem_sizes[i/IOX_HINDEXED_COUNT] += (int32_t) rwx->len;
        rwx++;
    }
    vfs_request->op_ids = (PVFS_sys_op_id *) malloc(num_ops_posted * sizeof(PVFS_sys_op_id));
    if (vfs_request->op_ids == NULL)
    {
        gossip_err("post_iox_request: op_ids allocation failed\n");
        goto err_offsets;
    }
    vfs_request->file_req_a = (PVFS_Request *) malloc(num_ops_posted * sizeof(PVFS_Request));
    if (vfs_request->file_req_a == NULL)
    {
        gossip_err("post_iox_request: file_req_a allocation failed\n");
        goto err_opids;
    }
    vfs_request->mem_req_a  = (PVFS_Request *) malloc(num_ops_posted * sizeof(PVFS_Request));
    if (vfs_request->mem_req_a == NULL)
    {
        gossip_err("post_iox_request: mem_req_a allocation failed\n");
        goto err_filereq;
    }
    vfs_request->response.iox = (PVFS_sysresp_io *) malloc(num_ops_posted * sizeof(PVFS_sysresp_io)); 
    if (vfs_request->response.iox == NULL)
    {
        gossip_err("post_iox_request: iox response allocation failed\n");
        goto err_memreq;
    }
    iox_index = 0;
    iox_count = vfs_request->iox_count;
    ret = 0;
    for (i = 0; i < num_ops_posted; i++)
    {
        int32_t iox_stage;

        assert(iox_count >= 0);
        assert(iox_index >= 0 && iox_index < vfs_request->iox_count);
        iox_stage = PVFS_util_min(IOX_HINDEXED_COUNT, iox_count);
        /* Construct a mem request type for this portion */
        ret = PVFS_Request_contiguous(mem_sizes[i], PVFS_BYTE,
                &vfs_request->mem_req_a[i]);
        if (ret != 0)
        {
            gossip_err("post_iox_request: request_contiguous failed mem_sizes[%d] = %d\n",
                    i, mem_sizes[i]);
            break;
        }
        /* file request is now a hindexed request type */
        ret = PVFS_Request_hindexed(iox_stage, 
                &vfs_request->iox_sizes[iox_index],
                &vfs_request->iox_offsets[iox_index],
                PVFS_BYTE, 
                &vfs_request->file_req_a[i]);
        if (ret != 0)
        {
            gossip_err("post_iox_request: request_hindexed failed\n");
            break;
        }

        fill_hints(&hints, vfs_request);
        /* post the I/O */
        ret = PVFS_isys_io(
            vfs_request->in_upcall.req.iox.refn, vfs_request->file_req_a[i],
            0, 
            vfs_request->io_kernel_mapped_buf, vfs_request->mem_req_a[i],
            &vfs_request->in_upcall.credentials,
            &vfs_request->response.iox[i],
            vfs_request->in_upcall.req.iox.io_type,
            &vfs_request->op_ids[i],
            hints,
            (void *)vfs_request);

        if (ret < 0)
        {
            PVFS_perror_gossip("Posting file I/O failed", ret);
            break;
        }
        iox_count -= iox_stage;
        iox_index += iox_stage;
    }
    if (i != num_ops_posted)
    {
        int j;
        for (j = 0; j < i; j++)
        {
            /* cancel previously posted I/O's */
            PINT_client_io_cancel(vfs_request->op_ids[j]);
            PVFS_Request_free(&vfs_request->mem_req_a[j]);
            PVFS_Request_free(&vfs_request->file_req_a[j]);
        }
        free(vfs_request->in_upcall.trailer_buf);
        vfs_request->in_upcall.trailer_buf = NULL;
        goto err_iox;
    }
    vfs_request->op_id = vfs_request->op_ids[0];
    ret = 0;
out:
    free(mem_sizes);
    return ret;
err_iox:
    free(vfs_request->response.iox);
err_memreq:
    free(vfs_request->mem_req_a);
err_filereq:
    free(vfs_request->file_req_a);
err_opids:
    free(vfs_request->op_ids);
err_offsets:
    free(vfs_request->iox_offsets);
err_sizes:
    free(vfs_request->iox_sizes);
    goto out;
}


#ifdef USE_MMAP_RA_CACHE
static PVFS_error service_mmap_ra_flush_request(
    vfs_request_t *vfs_request)
{
    gossip_debug(
        GOSSIP_MMAP_RCACHE_DEBUG, "Flushing mmap-racache elem %llu, %d\n",
        llu(vfs_request->in_upcall.req.ra_cache_flush.refn.handle),
        vfs_request->in_upcall.req.ra_cache_flush.refn.fs_id);

    pvfs2_mmap_ra_cache_flush(
        vfs_request->in_upcall.req.ra_cache_flush.refn);

    /* we need to send a blank success response */
    vfs_request->out_downcall.type = PVFS2_VFS_OP_MMAP_RA_FLUSH;
    vfs_request->out_downcall.status = 0;
    vfs_request->op_id = -1;

    return 0;
}
#endif

static PVFS_error service_operation_cancellation(
    vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;

    /*
      based on the tag specified in the cancellation upcall, find the
      operation currently in progress and issue a cancellation on it
    */
    ret = cancel_op_in_progress(
        (PVFS_id_gen_t)vfs_request->in_upcall.req.cancel.op_tag);

    if (ret == -PVFS_ECANCEL)
    {
        ret = -PVFS_EINTR;
    }

    vfs_request->out_downcall.type = PVFS2_VFS_OP_CANCEL;
    vfs_request->out_downcall.status = ret;
    vfs_request->op_id = -1;

    return 0;
}

static PVFS_error post_fsync_request(vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    PVFS_hint hints;
    
    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "Got a flush request for %llu,%d\n",
        llu(vfs_request->in_upcall.req.fsync.refn.handle),
        vfs_request->in_upcall.req.fsync.refn.fs_id);

    fill_hints(&hints, vfs_request);
    ret = PVFS_isys_flush(
        vfs_request->in_upcall.req.fsync.refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, hints, (void *)vfs_request);
    vfs_request->hints = hints;
    
    if (ret < 0)
    {
        PVFS_perror_gossip("Posting flush failed", ret);
    }
    return ret;
}

static PVFS_object_ref perform_lookup_on_create_error(
    PVFS_object_ref parent,
    char *entry_name,
    PVFS_credentials *credentials,
    int follow_link,
    PVFS_hint hints)
{
    PVFS_error ret = 0;
    PVFS_sysresp_lookup lookup_response;
    PVFS_object_ref refn = { PVFS_HANDLE_NULL, PVFS_FS_ID_NULL };
    ret = PVFS_sys_ref_lookup(
        parent.fs_id, entry_name, parent, credentials,
        &lookup_response, follow_link, hints);

    if (ret)
    {
        char buf[64];
        PVFS_strerror_r(ret, buf, 64);

        gossip_err("*** Lookup failed in %s create failure path: %s\n",
                   (follow_link ? "file" : "symlink"), buf);
    }
    else
    {
        refn = lookup_response.ref;
    }
    return refn;
}

PVFS_error write_device_response(
    void *buffer_list,
    int *size_list,
    int list_size,
    int total_size,
    PVFS_id_gen_t tag,
    job_id_t *job_id,
    job_status_s *jstat,
    job_context_id context)
{
    PVFS_error ret = -1;
    int outcount = 0;

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, 
                 "%s: writing device response.  tag: %llu\n",
                 __func__, llu(tag));

    if (buffer_list && size_list && list_size &&
        total_size && (list_size < MAX_LIST_SIZE))
    {
        ret = job_dev_write_list(buffer_list, size_list, list_size,
                                 total_size, tag, PINT_DEV_EXT_ALLOC,
                                 NULL, 0, jstat, job_id, context);
        if (ret < 0)
        {
            PVFS_perror("job_dev_write_list()", ret);
            return ret;
        }
        else if (ret == 0)
        {
	    ret = job_test(*job_id, &outcount, NULL, jstat, -1, context);
            if (ret < 0)
            {
                PVFS_perror("job_test()", ret);
                return ret;
            }
        }

        if (jstat->error_code != 0)
        {
            PVFS_perror("job_bmi_write_list() error code",
                        jstat->error_code);
            ret = -1;
        }
    }
    return ret;
}

/* encoding needed by client-core to copy readdir entries to the shared page */
static long encode_dirents(pvfs2_readdir_response_t *ptr, PVFS_sysresp_readdir *readdir)
{
    int i; 
    char *buf = (char *) ptr;
    char **pptr = &buf;

    ptr->token = readdir->token;
    ptr->directory_version = readdir->directory_version;
    ptr->pvfs_dirent_outcount = readdir->pvfs_dirent_outcount;

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

    *pptr += offsetof(pvfs2_readdir_response_t, dirent_array);
    for (i = 0; i < readdir->pvfs_dirent_outcount; i++) 
    {
        enc_string(pptr, &readdir->dirent_array[i].d_name);
        *(int64_t *) *pptr = readdir->dirent_array[i].handle;
        *pptr += 8;
    }
    return ((unsigned long) *pptr - (unsigned long) ptr);
}

static int copy_dirents_to_downcall(vfs_request_t *vfs_request)
{
    int ret = 0;
    /* get a buffer for xfer of dirents */
    vfs_request->out_downcall.trailer_buf = 
        PINT_dev_get_mapped_buffer(BM_READDIR, s_io_desc, 
            vfs_request->in_upcall.req.readdir.buf_index);
    if (vfs_request->out_downcall.trailer_buf == NULL)
    {
        ret = -PVFS_EINVAL;
        goto err;
    }

    /* Simply encode the readdir system response into the shared buffer */
    vfs_request->out_downcall.trailer_size = 
        encode_dirents((pvfs2_readdir_response_t *) vfs_request->out_downcall.trailer_buf,
                &vfs_request->response.readdir);

    if (vfs_request->out_downcall.trailer_size <= 0) 
    {
        gossip_err("copy_dirents_to_downcall: invalid trailer size %ld\n",
                (long) vfs_request->out_downcall.trailer_size);
        ret = -PVFS_EINVAL;
    }
err:
    /* free sysresp dirent array */
    free(vfs_request->response.readdir.dirent_array);
    vfs_request->response.readdir.dirent_array = NULL;
    return ret;
}

static long encode_sys_attr(char *ptr, PVFS_sysresp_readdirplus *readdirplus) 
{
    char *buf = ptr;
    char **pptr = &buf;
    int i;

    memcpy(buf, readdirplus->stat_err_array, sizeof(PVFS_error) * readdirplus->pvfs_dirent_outcount);
    *pptr += sizeof(PVFS_error) * readdirplus->pvfs_dirent_outcount;
    if (readdirplus->pvfs_dirent_outcount % 2) 
    {
        *pptr += 4;
    }
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++)
    {
        memcpy(*pptr, &readdirplus->attr_array[i], sizeof(PVFS_sys_attr));
        *pptr += sizeof(PVFS_sys_attr);
        if (readdirplus->attr_array[i].link_target)
        {
            enc_string(pptr, &readdirplus->attr_array[i].link_target);
        }
    }
    return ((unsigned long) *pptr - (unsigned long) ptr);
}

static long encode_readdirplus_to_buffer(char *ptr, PVFS_sysresp_readdirplus *readdirplus)
{
    long amt;
    char *buf = (char *) ptr;

   /* encode the dirent part of the response */
    amt = encode_dirents((pvfs2_readdir_response_t *) buf, (PVFS_sysresp_readdir *) readdirplus);
    if (amt < 0)
        return amt;
    buf += amt;
    /* and then we encode the stat part of the response */
    amt = encode_sys_attr(buf, readdirplus);
    if (amt < 0)
        return amt;
    buf += amt;

    return ((unsigned long) buf - (unsigned long) ptr);
}

static int copy_direntplus_to_downcall(vfs_request_t *vfs_request)
{
    int i, ret = 0;
    /* get a buffer for xfer of direntplus */
    vfs_request->out_downcall.trailer_buf = 
        PINT_dev_get_mapped_buffer(BM_READDIR, s_io_desc, 
        vfs_request->in_upcall.req.readdirplus.buf_index);
    if (vfs_request->out_downcall.trailer_buf == NULL)
    {
        ret = -PVFS_EINVAL;
        goto err;
    }

    /* Simply encode the readdirplus system response into the shared buffer */
    vfs_request->out_downcall.trailer_size = 
        encode_readdirplus_to_buffer(vfs_request->out_downcall.trailer_buf,
                &vfs_request->response.readdirplus);
    if (vfs_request->out_downcall.trailer_size <= 0)
    {
        gossip_err("copy_direntplus_to_downcall: invalid trailer size %ld\n",
                (long) vfs_request->out_downcall.trailer_size);
        ret = -PVFS_EINVAL;
    }
err:
    /* free sysresp dirent array */
    free(vfs_request->response.readdirplus.dirent_array);
    vfs_request->response.readdirplus.dirent_array = NULL;
    /* free sysresp stat error array */
    free(vfs_request->response.readdirplus.stat_err_array);
    vfs_request->response.readdirplus.stat_err_array = NULL;
    /* free sysresp attribute array */
    for (i = 0; i < vfs_request->response.readdirplus.pvfs_dirent_outcount; i++) 
    {
        PVFS_util_release_sys_attr(&vfs_request->response.readdirplus.attr_array[i]);
    }
    free(vfs_request->response.readdirplus.attr_array);
    vfs_request->response.readdirplus.attr_array = NULL;
    return ret;
}

/* 
   this method has the ability to overwrite/scrub the error code
   passed down to the vfs
*/
static inline void package_downcall_members(
    vfs_request_t *vfs_request, int *error_code)
{
    int ret = -PVFS_EINVAL;
    assert(vfs_request);
    assert(error_code);

    switch(vfs_request->in_upcall.type)
    {
        case PVFS2_VFS_OP_LOOKUP:
            if (*error_code)
            {
                vfs_request->out_downcall.resp.lookup.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.lookup.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.lookup.refn =
                    vfs_request->response.lookup.ref;
            }
            break;
        case PVFS2_VFS_OP_CREATE:
            if (*error_code)
            {
                /*
                  unless O_EXCL was specified at open time from the
                  vfs, -PVFS_EEXIST shouldn't be an error, but rather
                  success.  to solve this case, in theory we could do
                  a lookup on a failed create, but there are problems.
                  most are consistency races, but aside from those is
                  that we don't know if the vfs has opened with the
                  O_EXCL flag at this level.  after much
                  investigation, it turns out we don't want to know
                  either.  the vfs (both in 2.4.x and 2.6.x) properly
                  handles the translated error code (which ends up
                  being -EEXIST) in the open path and does the right
                  thing when O_EXCL is specified (i.e. return -EEXIST,
                  otherwise success).  this always works fine for the
                  serial vfs opens, but with enough clients issuing
                  them, this error code is still propagated downward,
                  so as a second line of defense, we're doing the
                  lookup in this case as well.
                */
                if (*error_code == -PVFS_EEXIST)
                {
                    PVFS_hint hints;
                    fill_hints(&hints, vfs_request);
                    vfs_request->out_downcall.resp.create.refn =
                        perform_lookup_on_create_error(
                            vfs_request->in_upcall.req.create.parent_refn,
                            vfs_request->in_upcall.req.create.d_name,
                            &vfs_request->in_upcall.credentials, 1, hints);
                    vfs_request->hints = hints;

                    if (vfs_request->out_downcall.resp.create.refn.handle ==
                        PVFS_HANDLE_NULL)
                    {
                        gossip_debug(
                            GOSSIP_CLIENTCORE_DEBUG, "Overwriting error "
                            "code -PVFS_EEXIST with -PVFS_EACCES "
                            "(create)\n");

                        *error_code = -PVFS_EACCES;
                    }
                    else
                    {
                        gossip_debug(
                            GOSSIP_CLIENTCORE_DEBUG, "Overwriting error "
                            "code -PVFS_EEXIST with 0 (create)\n");

                        *error_code = 0;
                    }
                }
                else
                {
                    vfs_request->out_downcall.resp.create.refn.handle =
                        PVFS_HANDLE_NULL;
                    vfs_request->out_downcall.resp.create.refn.fs_id =
                        PVFS_FS_ID_NULL;
                }
            }
            else
            {
                vfs_request->out_downcall.resp.create.refn =
                    vfs_request->response.create.ref;
            }
            break;
        case PVFS2_VFS_OP_SYMLINK:
            if (*error_code)
            {
                vfs_request->out_downcall.resp.sym.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.sym.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.sym.refn =
                    vfs_request->response.symlink.ref;
            }
            break;
        case PVFS2_VFS_OP_GETATTR:
            if (*error_code == 0)
            {
                PVFS_sys_attr *attr = &vfs_request->response.getattr.attr;

                vfs_request->out_downcall.resp.getattr.attributes =
                    vfs_request->response.getattr.attr;

                gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                        "object type = %d\n", attr->objtype);

                /*
                  free allocated attr memory if required; to avoid
                  copying the embedded link_target string inside the
                  sys_attr object passed down into the vfs, we
                  explicitly copy the link target (if any) into a
                  reserved string space in the getattr downcall object
                */
                if ((attr->objtype == PVFS_TYPE_SYMLINK) &&
                    (attr->mask & PVFS_ATTR_SYS_LNK_TARGET))
                {
                    assert(attr->link_target);

                    snprintf(
                        vfs_request->out_downcall.resp.getattr.link_target,
                        PVFS2_NAME_LEN, "%s", attr->link_target);

                    free(attr->link_target);
                }
            }
            break;
        case PVFS2_VFS_OP_SETATTR:
            break;
        case PVFS2_VFS_OP_REMOVE:
            break;
        case PVFS2_VFS_OP_MKDIR:
            if (*error_code)
            {
                vfs_request->out_downcall.resp.mkdir.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.mkdir.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.mkdir.refn =
                    vfs_request->response.mkdir.ref;
            }
            break;
        case PVFS2_VFS_OP_READDIR:
            if (*error_code)
            {
                vfs_request->out_downcall.status = *error_code;
            }
            else
            {
                *error_code = copy_dirents_to_downcall(vfs_request);
            }
            break;
        case PVFS2_VFS_OP_READDIRPLUS:
            if (*error_code)
            {
                vfs_request->out_downcall.status = *error_code;
            }
            else
            {
                *error_code = copy_direntplus_to_downcall(vfs_request);
            }
            break;
        case PVFS2_VFS_OP_STATFS:
            vfs_request->out_downcall.resp.statfs.block_size =
                s_desc_params[BM_IO].dev_buffer_size;
            vfs_request->out_downcall.resp.statfs.blocks_total = (int64_t)
                (vfs_request->response.statfs.statfs_buf.bytes_total /
                 vfs_request->out_downcall.resp.statfs.block_size);
            vfs_request->out_downcall.resp.statfs.blocks_avail = (int64_t)
                (vfs_request->response.statfs.statfs_buf.bytes_available /
                 vfs_request->out_downcall.resp.statfs.block_size);
            /*
              these values really represent handle/inode counts
              rather than an accurate number of files
            */
            vfs_request->out_downcall.resp.statfs.files_total = (int64_t)
                vfs_request->response.statfs.statfs_buf.handles_total_count;
            vfs_request->out_downcall.resp.statfs.files_avail = (int64_t)
                vfs_request->response.statfs.statfs_buf.handles_available_count;
            break;
        case PVFS2_VFS_OP_FS_MOUNT:
            if (*error_code)
            {
                gossip_err(
                    "Failed to mount via host %s\n",
                    vfs_request->in_upcall.req.fs_mount.pvfs2_config_server);

                PVFS_perror_gossip("Mount failed", *error_code);
            }
            else
            {
                PVFS_handle root_handle;
                /*
                  ungracefully ask bmi to drop connections on cancellation so
                  that the server will immediately know that a cancellation
                  occurred
                */
                PVFS_BMI_addr_t tmp_addr;

                if (BMI_addr_lookup(&tmp_addr, 
                    vfs_request->mntent->the_pvfs_config_server) == 0)
                {
                    if (BMI_set_info(
                            tmp_addr, BMI_FORCEFUL_CANCEL_MODE, NULL) == 0)
                    {
                        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "BMI forceful "
                                     "cancel mode enabled\n");
                    }
                }
                reset_acache_timeout();
                reset_ncache_timeout();

                /*
                  before sending success response we need to resolve the root
                  handle, given the previously resolved fs_id
                */
                ret = PINT_cached_config_get_root_handle(
                    vfs_request->mntent->fs_id, &root_handle);
                if (ret)
                {
                    gossip_err("Failed to retrieve root handle for "
                               "resolved fs_id %d\n", vfs_request->mntent->fs_id);
                    gossip_err(
                        "Failed to mount via host %s\n",
                        vfs_request->in_upcall.req.fs_mount.pvfs2_config_server);
                    PVFS_perror_gossip("Mount failed", ret);
                    PVFS_util_free_mntent(vfs_request->mntent);
                    *error_code = ret;
                    break;
                }
                    
                gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                             "FS mount got root handle %llu on fs id %d\n",
                             llu(root_handle), vfs_request->mntent->fs_id);

                vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_MOUNT;
                vfs_request->out_downcall.status = 0;
                vfs_request->out_downcall.resp.fs_mount.fs_id = 
                    vfs_request->mntent->fs_id;
                vfs_request->out_downcall.resp.fs_mount.root_handle =
                    root_handle;
                vfs_request->out_downcall.resp.fs_mount.id = dynamic_mount_id++;
            }

            PVFS_util_free_mntent(vfs_request->mntent);
            free(vfs_request->mntent);

            break;
        case PVFS2_VFS_OP_RENAME:
            break;
        case PVFS2_VFS_OP_TRUNCATE:
            break;
        case PVFS2_VFS_OP_FSYNC:
            break;
        case PVFS2_VFS_OP_FILE_IO:
            if (*error_code == 0)
            {
#ifdef USE_MMAP_RA_CACHE
                if (vfs_request->readahead_posted)
                {
                    void *buf = NULL;

                    assert(vfs_request->io_tmp_buf);
                    assert(vfs_request->in_upcall.req.io.readahead_size > 0);
                    /*
                      now that we've read the data, insert it into the
                      mmap_ra_cache here
                    */
                    pvfs2_mmap_ra_cache_register(
                        vfs_request->in_upcall.req.io.refn,
                        vfs_request->io_tmp_buf,
                        vfs_request->in_upcall.req.io.readahead_size);

                    /*
                      get a shared kernel/userspace buffer for the I/O
                      transfer
                    */
                    buf = PINT_dev_get_mapped_buffer(BM_IO, s_io_desc, 
                        vfs_request->in_upcall.req.io.buf_index);
                    assert(buf);

                    /* copy cached data into the shared user/kernel space */
                    memcpy(buf, ((char *) vfs_request->io_tmp_buf +
                                 vfs_request->in_upcall.req.io.offset),
                           vfs_request->in_upcall.req.io.count);

                    if (vfs_request->io_tmp_buf !=
                        vfs_request->io_tmp_small_buf)
                    {
                        gossip_debug(
                            GOSSIP_MMAP_RCACHE_DEBUG, "--- Freeing %p\n",
                            vfs_request->io_tmp_buf);
                        free(vfs_request->io_tmp_buf);
                    }
                    vfs_request->io_tmp_buf = NULL;

                    PVFS_Request_free(&vfs_request->mem_req);
                    PVFS_Request_free(&vfs_request->file_req);

                    vfs_request->out_downcall.resp.io.amt_complete =
                        vfs_request->in_upcall.req.io.count;
                }
                else
                {
                    assert(vfs_request->io_tmp_buf == NULL);
                    assert(vfs_request->io_kernel_mapped_buf);

                    PVFS_Request_free(&vfs_request->mem_req);
                    PVFS_Request_free(&vfs_request->file_req);

                    vfs_request->out_downcall.resp.io.amt_complete =
                        (size_t)vfs_request->response.io.total_completed;
                }
#else
                PVFS_Request_free(&vfs_request->mem_req);
                PVFS_Request_free(&vfs_request->file_req);

                vfs_request->out_downcall.resp.io.amt_complete =
                    (size_t)vfs_request->response.io.total_completed;
                gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "completed I/O on tag %Ld\n", lld(vfs_request->info.tag));
#endif
            }
#ifdef USE_MMAP_RA_CACHE
            /* free resources if allocated, even on failed reads */
            if (vfs_request->readahead_posted && vfs_request->io_tmp_buf)
            {
                if (vfs_request->io_tmp_buf !=
                    vfs_request->io_tmp_small_buf)
                {
                    gossip_debug(
                        GOSSIP_MMAP_RCACHE_DEBUG, "--- Freeing %p\n",
                        vfs_request->io_tmp_buf);
                    free(vfs_request->io_tmp_buf);
                }
                vfs_request->io_tmp_buf = NULL;

                PVFS_Request_free(&vfs_request->mem_req);
                PVFS_Request_free(&vfs_request->file_req);
            }
#endif
            /* replace non-errno error code to avoid passing to kernel */
            if (*error_code == -PVFS_ECANCEL)
            {
                /* if an ECANCEL shows up here without going through the 
                 * cancel_op_in_progress() path, then -PVFS_ETIMEDOUT is 
                 * a better errno approximation than -PVFS_EINTR 
                 */
                *error_code = -PVFS_ETIMEDOUT;
            }
            break;
        case PVFS2_VFS_OP_FILE_IOX:
        {
            int j;

            vfs_request->out_downcall.resp.iox.amt_complete = 0;
            for (j = 0; j < vfs_request->num_ops; j++)
            {
                vfs_request->out_downcall.resp.iox.amt_complete +=
                    vfs_request->response.iox[j].total_completed;
            }
            free(vfs_request->response.iox);
            for (j = 0; j < vfs_request->num_ops; j++)
            {
                PVFS_Request_free(&vfs_request->mem_req_a[j]);
                PVFS_Request_free(&vfs_request->file_req_a[j]);
            }
            free(vfs_request->mem_req_a);
            free(vfs_request->file_req_a);
            free(vfs_request->op_ids);
            free(vfs_request->iox_offsets);
            free(vfs_request->iox_sizes);
            free(vfs_request->in_upcall.trailer_buf);
            vfs_request->in_upcall.trailer_buf = NULL;
            
            /* replace non-errno error code to avoid passing to kernel */
            if (*error_code == -PVFS_ECANCEL)
            {
                /* if an ECANCEL shows up here without going through the 
                 * cancel_op_in_progress() path, then -PVFS_ETIMEDOUT is 
                 * a better errno approximation than -PVFS_EINTR 
                 */
                *error_code = -PVFS_ETIMEDOUT;
            }
            break;
        }
        case PVFS2_VFS_OP_GETXATTR:
            if (*error_code == 0)
            {
                int val_sz = 
                    vfs_request->response.geteattr.val_array[0].read_sz;
                gossip_debug(GOSSIP_CLIENTCORE_DEBUG, 
                        "getxattr: val_sz %d, val %s\n",
                        val_sz, 
                        (char *) vfs_request->response.geteattr.val_array[0].buffer);
                /* copy the requested key's value out to the downcall */
                if (val_sz > PVFS_MAX_XATTR_VALUELEN)
                {
                    /* This is really bad. Can it happen? */
                    *error_code = -PVFS_EINVAL;
                }
                else
                {
                    vfs_request->out_downcall.resp.getxattr.val_sz = val_sz;
                    memcpy(vfs_request->out_downcall.resp.getxattr.val,
                            vfs_request->response.geteattr.val_array[0].buffer,
                            val_sz);
                }
            }
            else {
                PVFS_perror("getxattr: ", *error_code);
            }
            /* free up the memory allocate to response.geteattr */
            free(vfs_request->response.geteattr.val_array[0].buffer);
            vfs_request->response.geteattr.val_array[0].buffer = NULL;
            free(vfs_request->response.geteattr.val_array);
            vfs_request->response.geteattr.val_array = NULL;
            free(vfs_request->response.geteattr.err_array);
            vfs_request->response.geteattr.err_array = NULL;
            break;
        case PVFS2_VFS_OP_SETXATTR:
            break;
        case PVFS2_VFS_OP_REMOVEXATTR:
            break;
        case PVFS2_VFS_OP_LISTXATTR:
        {
            int i;
            if (*error_code == 0)
            {
                vfs_request->out_downcall.resp.listxattr.returned_count =
                    vfs_request->response.listeattr.nkey;
                if (vfs_request->in_upcall.req.listxattr.requested_count == 0)
                {
                    vfs_request->out_downcall.resp.listxattr.token = 
                        PVFS_ITERATE_START;
                }
                else 
                {
                    vfs_request->out_downcall.resp.listxattr.token = 
                        vfs_request->response.listeattr.token;
                    vfs_request->out_downcall.resp.listxattr.keylen = 0;
                    for (i = 0; i < vfs_request->response.listeattr.nkey; i++)
                    {
                        memcpy(vfs_request->out_downcall.resp.listxattr.key
                                + vfs_request->out_downcall.resp.listxattr.keylen,
                                vfs_request->response.listeattr.key_array[i].buffer,
                                vfs_request->response.listeattr.key_array[i].read_sz);
                        vfs_request->out_downcall.resp.listxattr.lengths[i] = 
                                vfs_request->response.listeattr.key_array[i].read_sz;
                        vfs_request->out_downcall.resp.listxattr.keylen +=
                                vfs_request->response.listeattr.key_array[i].read_sz;
                    }
                }
                printf("Listxattr obtained: %d ", vfs_request->out_downcall.resp.listxattr.keylen);
                for (i = 0; i < vfs_request->out_downcall.resp.listxattr.keylen; i++)
                {
                    printf("%c", *(char *)((char *)vfs_request->out_downcall.resp.listxattr.key + i));
                }
                printf("\n");
            }
            /* free up the memory allocate to response.listteattr */
            for (i = 0; i < vfs_request->in_upcall.req.listxattr.requested_count; i++)
            {
                free(vfs_request->response.listeattr.key_array[i].buffer);
                vfs_request->response.listeattr.key_array[i].buffer = NULL;
            }
            if (vfs_request->response.listeattr.key_array)
            {
                free(vfs_request->response.listeattr.key_array);
                vfs_request->response.listeattr.key_array = NULL;
            }
            break;
        }
        case PVFS2_VFS_OP_FS_UMOUNT:
        case PVFS2_VFS_OP_PERF_COUNT:
        case PVFS2_VFS_OP_PARAM:
        case PVFS2_VFS_OP_FSKEY:
        case PVFS2_VFS_OP_CANCEL:
            break;
        default:
            gossip_err("Completed upcall of unknown type %x!\n",
                       vfs_request->in_upcall.type);
            break;
    }

    vfs_request->out_downcall.status = *error_code;
    vfs_request->out_downcall.type = vfs_request->in_upcall.type;
}

static inline PVFS_error repost_unexp_vfs_request(
    vfs_request_t *vfs_request, char *completion_handle_desc)
{
    PVFS_error ret = -PVFS_EINVAL;

    assert(vfs_request);

    PINT_dev_release_unexpected(&vfs_request->info);
    PINT_sys_release(vfs_request->op_id);

    PVFS_hint_free(vfs_request->hints);
    memset(vfs_request, 0, sizeof(vfs_request_t));
    vfs_request->is_dev_unexp = 1;

    ret = PINT_sys_dev_unexp(&vfs_request->info, &vfs_request->jstat,
                             &vfs_request->op_id, vfs_request);
    if (ret < 0)
    {
        PVFS_perror_gossip("PINT_sys_dev_unexp()", ret);
    }
    else
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "[-] reposted unexp "
                     "req [%p] due to %s\n", vfs_request,
                     completion_handle_desc);
    }
    return ret;
}

static inline PVFS_error handle_unexp_vfs_request(
    vfs_request_t *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;

    assert(vfs_request);

    if (vfs_request->jstat.error_code)
    {
        PVFS_perror_gossip("job error code",
                           vfs_request->jstat.error_code);
        ret = vfs_request->jstat.error_code;
        goto repost_op;
    }

    gossip_debug(
        GOSSIP_CLIENTCORE_DEBUG, "[+] dev req msg: "
        "sz: %d,tag: %lld,data: %p,type: %d\n",
        vfs_request->info.size, lld(vfs_request->info.tag),
        vfs_request->info.buffer, vfs_request->in_upcall.type);

    if (vfs_request->info.size >= sizeof(pvfs2_upcall_t))
    {
        memcpy(&vfs_request->in_upcall, vfs_request->info.buffer,
               sizeof(pvfs2_upcall_t));
    }
    else
    {
        gossip_err("Error! Short read from device; aborting!\n");
        ret = -PVFS_EIO;
        goto repost_op;
    }

    if (!remount_complete &&
        (vfs_request->in_upcall.type != PVFS2_VFS_OP_FS_MOUNT))
    {
        gossip_debug(
            GOSSIP_CLIENTCORE_DEBUG, "Got an upcall operation of "
            "type %x before mounting. ignoring.\n",
            vfs_request->in_upcall.type);
        /*
          if we don't have any mount information yet, just discard the
          op, causing a kernel timeout/retry
        */
        ret = REMOUNT_PENDING;
        goto repost_op;
    }

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "[*] handling new unexp vfs_request %p\n", vfs_request);

    /*
      make sure the operation is not currently in progress.  if it is,
      ignore it -- this can happen if the vfs issues a retry request
      on an operation that's taking a long time to complete.
      Can this happen any more?
    */
    if (is_op_in_progress(vfs_request))
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, " WARNING: Client-core obtained duplicate upcall of type "
                     "%x that's already in progress (tag=%lld)?\n",
                     vfs_request->in_upcall.type,
                     lld(vfs_request->info.tag));

        ret = OP_IN_PROGRESS;
        goto repost_op;
    }

#ifdef CLIENT_CORE_OP_TIMING
    PINT_time_mark(&vfs_request->start);
#endif

    vfs_request->num_ops = 1;
    vfs_request->num_incomplete_ops = 1;
    vfs_request->op_ids  = NULL;
    switch(vfs_request->in_upcall.type)
    {
        case PVFS2_VFS_OP_LOOKUP:
            ret = post_lookup_request(vfs_request);
            break;
        case PVFS2_VFS_OP_CREATE:
            ret = post_create_request(vfs_request);
            break;
        case PVFS2_VFS_OP_SYMLINK:
            ret = post_symlink_request(vfs_request);
            break;
        case PVFS2_VFS_OP_GETATTR:
            ret = post_getattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_SETATTR:
            ret = post_setattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_REMOVE:
            ret = post_remove_request(vfs_request);
            break;
        case PVFS2_VFS_OP_MKDIR:
            ret = post_mkdir_request(vfs_request);
            break;
        case PVFS2_VFS_OP_READDIR:
            ret = post_readdir_request(vfs_request);
            break;
        case PVFS2_VFS_OP_READDIRPLUS:
            ret = post_readdirplus_request(vfs_request);
            break;
        case PVFS2_VFS_OP_RENAME:
            ret = post_rename_request(vfs_request);
            break;
        case PVFS2_VFS_OP_TRUNCATE:
            ret = post_truncate_request(vfs_request);
            break;
        case PVFS2_VFS_OP_GETXATTR:
            ret = post_getxattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_SETXATTR:
            ret = post_setxattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_REMOVEXATTR:
            ret = post_removexattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_LISTXATTR:
            ret = post_listxattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_STATFS:
            ret = post_statfs_request(vfs_request);
            break;
        case PVFS2_VFS_OP_FS_MOUNT:
            ret = post_fs_mount_request(vfs_request);
            break;
            /*
              NOTE: following operations are blocking
              calls that are serviced inline.
            */
        case PVFS2_VFS_OP_FS_UMOUNT:
            ret = service_fs_umount_request(vfs_request);
            break;
        case PVFS2_VFS_OP_PERF_COUNT:
            ret = service_perf_count_request(vfs_request);
            break;
        case PVFS2_VFS_OP_PARAM:
            ret = service_param_request(vfs_request);
            break;
        case PVFS2_VFS_OP_FSKEY:
            ret = service_fs_key_request(vfs_request);
            break;
            /*
              if the mmap-readahead-cache is enabled and we
              get a cache hit for data, the io call is
              blocking and handled inline
            */
        case PVFS2_VFS_OP_FILE_IO:
            ret = post_io_request(vfs_request);
            break;
        case PVFS2_VFS_OP_FILE_IOX:
            ret = post_iox_request(vfs_request);
            break;
#ifdef USE_MMAP_RA_CACHE
            /*
              if the mmap-readahead-cache is enabled, cache
              flushes are handled inline
            */
        case PVFS2_VFS_OP_MMAP_RA_FLUSH:
            ret = service_mmap_ra_flush_request(vfs_request);
            break;
#endif
        case PVFS2_VFS_OP_CANCEL:
            ret = service_operation_cancellation(vfs_request);
            break;
        case PVFS2_VFS_OP_FSYNC:
            ret = post_fsync_request(vfs_request);
            break;
        case PVFS2_VFS_OP_INVALID:
        default:
            gossip_err(
                "Got an unrecognized/unimplemented vfs operation of "
                "type %x.\n", vfs_request->in_upcall.type);
            ret = -PVFS_ENOSYS;
            break;
    }

    /* if we failed to post the operation, then we should go ahead and write
     * a generic response down with the error code filled in 
     */
    if(ret < 0)
    {
#ifndef GOSSIP_DISABLE_DEBUG
        gossip_err(
            "Post of op: %s failed!\n",
            get_vfs_op_name_str(vfs_request->in_upcall.type));
#else
        gossip_err(
            "Post of op: %d failed!\n", vfs_request->in_upcall.type);
#endif

        vfs_request->out_downcall.status = ret;
        /* this will treat the operation as if it were inlined in the logic
         * to follow, which is what we want -- report a general error and
         * immediately release the request */
        write_inlined_device_response(vfs_request);
    }
  repost_op:
    /*
      check if we need to repost the operation (in case of failure or
      inlined handling/completion)
    */
    switch(ret)
    {
        case 0:
        {
            if(vfs_request->op_id == -1)
            {
                /* This should be set to the return value of the isys_* call */
                int error = ret; /* error code of the SM> */
                vfs_request->num_incomplete_ops--;
                package_downcall_members(vfs_request,  &error);
                write_inlined_device_response(vfs_request); 
                ret = repost_unexp_vfs_request(
                    vfs_request, "inlined completion");
            }
            else
            {

                /*
                   otherwise, we've just properly posted a non-blocking
                   op; mark it as no longer a dev unexp msg and add it
                   to the ops in progress table
                   */
                vfs_request->is_dev_unexp = 0;
                ret = add_op_to_op_in_progress_table(vfs_request);
#if 0
                assert(is_op_in_progress(vfs_request));
#endif
            }
        }
        break;
        case REMOUNT_PENDING:
            ret = repost_unexp_vfs_request(
                vfs_request, "mount pending");
            break;
        case OP_IN_PROGRESS:
            ret = repost_unexp_vfs_request(
                vfs_request, "op already in progress");
            break;
        default:
            PVFS_perror_gossip("Operation failed", ret);
            ret = repost_unexp_vfs_request(
                vfs_request, "failure");
            break;
    }
    return ret;
}

static PVFS_error process_vfs_requests(void)
{
    PVFS_error ret = 0; 
    int op_count = 0, i = 0;
    vfs_request_t *vfs_request = NULL;
    vfs_request_t *vfs_request_array[MAX_NUM_OPS] = {NULL};
    PVFS_sys_op_id op_id_array[MAX_NUM_OPS];
    int error_code_array[MAX_NUM_OPS] = {0};
    void *buffer_list[MAX_LIST_SIZE];
    int size_list[MAX_LIST_SIZE];
    int list_size = 0, total_size = 0;

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "process_vfs_requests called\n");

    /* allocate and post all of our initial unexpected vfs requests */
    for(i = 0; i < MAX_NUM_OPS; i++)
    {
        vfs_request = (vfs_request_t *)malloc(sizeof(vfs_request_t));
        assert(vfs_request);

        s_vfs_request_array[i] = vfs_request;

        memset(vfs_request, 0, sizeof(vfs_request_t));
        vfs_request->is_dev_unexp = 1;

        ret = PINT_sys_dev_unexp(
            &vfs_request->info, &vfs_request->jstat,
            &vfs_request->op_id, vfs_request);

        if (ret < 0)
        {
	    PVFS_perror_gossip("PINT_sys_dev_unexp()", ret);
            return -PVFS_ENOMEM;
        }
    }

    /*
      signal the remount thread to go ahead with the remount attempts
      since we're ready to handle requests now
    */
    pthread_mutex_unlock(&remount_mutex);

    while(s_client_is_processing)
    {
        op_count = MAX_NUM_OPS;
        memset(error_code_array, 0, (MAX_NUM_OPS * sizeof(int)));
        memset(vfs_request_array, 0,
               (MAX_NUM_OPS * sizeof(vfs_request_t *)));

#if 0
        /* generates too much logging, but useful sometimes */
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "Calling PVFS_sys_testsome for new requests\n");
#endif

        ret = PVFS_sys_testsome(
            op_id_array, &op_count, (void *)vfs_request_array,
            error_code_array, PVFS2_CLIENT_DEFAULT_TEST_TIMEOUT_MS);

        for(i = 0; i < op_count; i++)
        {
            vfs_request = vfs_request_array[i];
            assert(vfs_request);
/*             assert(vfs_request->op_id == op_id_array[i]); */
            if (vfs_request->num_ops == 1 &&
                    vfs_request->op_id != op_id_array[i])
            {
                gossip_err("op_id %Ld != completed op id %Ld\n",
                        lld(vfs_request->op_id), lld(op_id_array[i]));
                continue;
            }
            else if (vfs_request->num_ops > 1)
            {
                int j;
                /* assert that completed op is one that we posted earlier */
                for (j = 0; j < vfs_request->num_ops; j++) {
                    if (op_id_array[i] == vfs_request->op_ids[j])
                        break;
                }
                if (j == vfs_request->num_ops)
                {
                    gossip_err("completed op id (%Ld) is weird\n",
                            lld(op_id_array[i]));
                    continue;
                }
            }

            /* check if this is a new dev unexp request */
            if (vfs_request->is_dev_unexp)
            {
                /*
                  NOTE: possible optimization -- if we detect that
                  we're about to handle an inlined/blocking operation,
                  make sure all non-inline ops are posted beforehand
                  so that the sysint test() calls from the blocking
                  operation handling can be making progress on the
                  other ops in progress
                */
                gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "PINT_sys_testsome"
                             " returned unexp vfs_request %p, tag: %llu\n",
                             vfs_request,
                             llu(vfs_request->info.tag));
                ret = handle_unexp_vfs_request(vfs_request);
                assert(ret == 0);

                /* We've handled this unexpected request (posted the
                 * client isys call), we can move
                 * on to the next request in the queue.
                 */
                continue;
            }

            /* We've just completed an (expected) operation on this request, now
             * we must figure out its completion state and act accordingly.
             */
            vfs_request->num_incomplete_ops--;

            /* if operation is not complete, we gotta continue */
            if (vfs_request->num_incomplete_ops != 0)
                continue;
            log_operation_timing(vfs_request);

            gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "PINT_sys_testsome"
                         " returned completed vfs_request %p\n",
                         vfs_request);
            /*
               if this is not a dev unexp msg, it's a non-blocking
               sysint operation that has just completed
               */
            assert(vfs_request->in_upcall.type);

            /*
               even if the op was cancelled, if we get here, we
               will have to remove the op from the in progress
               table.  the error code on cancelled operations is
               already set appropriately
               */
            ret = remove_op_from_op_in_progress_table(vfs_request);
            if (ret)
            {
                PVFS_perror_gossip("Failed to remove op in progress "
                                   "from table", ret);

                /* repost the unexpected request since we're done
                 * with this one.
                 */
                ret = repost_unexp_vfs_request(
                    vfs_request, "normal completion");

                assert(ret == 0);
                continue;
            }

            package_downcall_members(
                vfs_request, &error_code_array[i]);

            /*
               write the downcall if the operation was NOT a
               cancelled I/O operation.  while it's safe to write
               cancelled I/O operations to the kernel, it's a waste
               of time since it will be discarded.  just repost the
               op instead
               */
            if (!vfs_request->was_cancelled_io)
            {
                buffer_list[0] = &vfs_request->out_downcall;
                size_list[0] = sizeof(pvfs2_downcall_t);
                list_size = 1;
                total_size = sizeof(pvfs2_downcall_t);
                if (vfs_request->out_downcall.trailer_size > 0) {
                    buffer_list[1] = vfs_request->out_downcall.trailer_buf;
                    size_list[1] = vfs_request->out_downcall.trailer_size;
                    list_size++;
                    total_size += vfs_request->out_downcall.trailer_size;
                }
                ret = write_device_response(
                    buffer_list,size_list,list_size, total_size,
                    vfs_request->info.tag,
                    &vfs_request->op_id, &vfs_request->jstat,
                    s_client_dev_context);

                gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "downcall "
                             "write returned %d\n", ret);

                if (ret < 0)
                {
                    gossip_err(
                        "write_device_response failed "
                        "(tag=%lld)\n", lld(vfs_request->info.tag));
                }
            }
            else
            {
                gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "skipping "
                             "downcall write due to previous "
                             "cancellation\n");

                ret = repost_unexp_vfs_request(
                    vfs_request, "cancellation");

                assert(ret == 0);
                continue;
            }

            ret = repost_unexp_vfs_request(
                vfs_request, "normal_completion");
            assert(ret == 0);
        }
    }

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "process_vfs_requests returning\n");
    return 0;
}

int main(int argc, char **argv)
{
    int ret = 0, i = 0;
    time_t start_time;
    struct tm *local_time = NULL;
    uint64_t debug_mask = GOSSIP_NO_DEBUG;
    PINT_client_sm *acache_timer_sm_p = NULL;
    PINT_client_sm *static_acache_timer_sm_p = NULL;
    PINT_smcb *smcb = NULL;
    PINT_client_sm *ncache_timer_sm_p = NULL;

#ifdef __PVFS2_SEGV_BACKTRACE__
    struct sigaction segv_action;

    segv_action.sa_sigaction = (void *)client_segfault_handler;
    sigemptyset (&segv_action.sa_mask);
    segv_action.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONESHOT;
    sigaction (SIGSEGV, &segv_action, NULL);
#else

    /* if pvfs2-client-core segfaults, at least log the occurence so
     * pvfs2-client won't repeatedly respawn pvfs2-client-core */
    signal(SIGSEGV, client_segfault_handler);
#endif

    memset(&s_opts, 0, sizeof(options_t));
    parse_args(argc, argv, &s_opts);

    signal(SIGHUP,  client_core_sig_handler);
    signal(SIGINT,  client_core_sig_handler);
    signal(SIGPIPE, client_core_sig_handler);
    signal(SIGILL,  client_core_sig_handler);
    signal(SIGTERM, client_core_sig_handler);

    /* we don't want to write a core file if we're running under
     * the client parent process, because the client-core process
     * could keep segfaulting, and the client would keep restarting it...
     */
    if(s_opts.child)
    {
        struct rlimit lim = {0,0};

        /* set rlimit to prevent core files */
        ret = setrlimit(RLIMIT_CORE, &lim);
        if (ret < 0)
        {
            fprintf(stderr, "setrlimit system call failed (%d); "
                    "continuing", ret);
        }
    }

    /* convert gossip mask if provided on command line */
    if (s_opts.gossip_mask)
    {
        debug_mask = PVFS_debug_eventlog_to_mask(s_opts.gossip_mask);
    }

    if (s_opts.logstamp_type_set)
    {
        gossip_set_logstamp(s_opts.logstamp_type);
    }
    /*
      initialize pvfs system interface

      NOTE: we do not rely on a pvfstab file at all in here, as
      mounting a pvfs2 volume through the kernel interface now
      requires you to specify a server and fs name in the form of:

      protocol://server/fs_name

      At kernel mount time, we dynamically resolve and add the file
      system mount information to the pvfs2 system interface (and also
      (re)configure the acache at that time since it's based on the
      dynamic server configurations)
    */
    ret = PVFS_sys_initialize(debug_mask);
    if (ret < 0)
    {
        return ret;
    }

    if(!strcmp(s_opts.logtype, "file"))
    {
        ret = gossip_enable_file(s_opts.logfile, "a");
        if(ret < 0)
        {
            fprintf(stderr, "Error opening logfile: %s\n", s_opts.logfile);
            return(ret);
        }
    }
    else if(!strcmp(s_opts.logtype, "syslog"))
    {
        ret = gossip_enable_syslog(LOG_INFO);
        if(ret < 0)
        {
            fprintf(stderr, "Error opening syslog\n");
            return(ret);
        }
    }
    else
    {
        fprintf(stderr, "Error: unsupported log type.\n");
        return(-PVFS_EINVAL);
    }

    /* get rid of stdout/stderr/stdin */
    if(!freopen("/dev/null", "r", stdin))
        gossip_err("Error: failed to reopen stdin.\n");
    if(!freopen("/dev/null", "w", stdout))
        gossip_err("Error: failed to reopen stdout.\n");
    if(!freopen("/dev/null", "w", stderr))
        gossip_err("Error: failed to reopen stderr.\n");

    start_time = time(NULL);
    local_time = localtime(&start_time);

    gossip_err("PVFS Client Daemon Started.  Version %s\n", PVFS2_VERSION);
    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,  "***********************"
                 "****************************\n");
    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 " %s starting at %.4d-%.2d-%.2d %.2d:%.2d\n",
                 argv[0], (local_time->tm_year + 1900),
                 local_time->tm_mon+1, local_time->tm_mday,
                 local_time->tm_hour, local_time->tm_min);
    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "***************************************************\n");

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_initialize();
#endif

    ret = set_acache_parameters(&s_opts);
    if(ret < 0)
    {
        PVFS_perror("set_acache_parameters", ret);
        return(ret);
    }
    ret = set_ncache_parameters(&s_opts);
    if(ret < 0)
    {
        PVFS_perror("set_ncache_parameters", ret);
        return(ret);
    }
    set_device_parameters(&s_opts);

    if(s_opts.events)
    {
        PINT_event_enable(s_opts.events);
    }

    /* start performance counters for acache */
    acache_pc = PINT_perf_initialize(acache_keys);
    if(!acache_pc)
    {
        gossip_err("Error: PINT_perf_initialize failure.\n");
        return(-PVFS_ENOMEM);
    }
    ret = PINT_perf_set_info(acache_pc, PINT_PERF_HISTORY_SIZE,
        s_opts.perf_history_size);
    if(ret < 0)
    {
        gossip_err("Error: PINT_perf_set_info (history_size).\n");
        return(ret);
    }

    static_acache_pc = PINT_perf_initialize(acache_keys);
    if(!static_acache_pc)
    {
        gossip_err("Error: PINT_perf_initialize failure.\n");
        return(-PVFS_ENOMEM);
    }
    ret = PINT_perf_set_info(static_acache_pc, PINT_PERF_HISTORY_SIZE,
        s_opts.perf_history_size);
    if(ret < 0)
    {
        gossip_err("Error: PINT_perf_set_info (history_size).\n");
        return(ret);
    }

    PINT_acache_enable_perf_counter(acache_pc, static_acache_pc);

    /* start performance counters for ncache */
    ncache_pc = PINT_perf_initialize(ncache_keys);
    if(!ncache_pc)
    {
        gossip_err("Error: PINT_perf_initialize failure.\n");
        return(-PVFS_ENOMEM);
    }
    ret = PINT_perf_set_info(ncache_pc, PINT_PERF_HISTORY_SIZE,
        s_opts.perf_history_size);
    if(ret < 0)
    {
        gossip_err("Error: PINT_perf_set_info (history_size).\n");
        return(ret);
    }
    PINT_ncache_enable_perf_counter(ncache_pc);

    /* start a timer to roll over performance counters (acache) */
    PINT_smcb_alloc(&smcb, PVFS_CLIENT_PERF_COUNT_TIMER,
            sizeof(struct PINT_client_sm),
            client_op_state_get_machine,
            client_state_machine_terminate,
            s_client_dev_context);
    if (!smcb)
    {
        return(-PVFS_ENOMEM);
    }
    acache_timer_sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    acache_timer_sm_p->u.perf_count_timer.interval_secs = 
        &s_opts.perf_time_interval_secs;
    acache_timer_sm_p->u.perf_count_timer.pc = acache_pc;
    ret = PINT_client_state_machine_post(smcb, NULL, NULL);
    if (ret < 0)
    {
        gossip_lerr("Error posting acache timer.\n");
        return(ret);
    }

    PINT_smcb_alloc(&smcb, PVFS_CLIENT_PERF_COUNT_TIMER,
            sizeof(struct PINT_client_sm),
            client_op_state_get_machine,
            client_state_machine_terminate,
            s_client_dev_context);
    if (!smcb)
    {
        return(-PVFS_ENOMEM);
    }
    static_acache_timer_sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    static_acache_timer_sm_p->u.perf_count_timer.interval_secs = 
        &s_opts.perf_time_interval_secs;
    static_acache_timer_sm_p->u.perf_count_timer.pc = static_acache_pc;
    ret = PINT_client_state_machine_post(smcb, NULL, NULL);
    if (ret < 0)
    {
        gossip_lerr("Error posting acache timer.\n");
        return(ret);
    }

    PINT_smcb_alloc(&smcb, PVFS_CLIENT_PERF_COUNT_TIMER,
            sizeof(struct PINT_client_sm),
            client_op_state_get_machine,
            client_state_machine_terminate,
            s_client_dev_context);
    if (!smcb)
    {
        return(-PVFS_ENOMEM);
    }
    ncache_timer_sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    ncache_timer_sm_p->u.perf_count_timer.interval_secs = 
        &s_opts.perf_time_interval_secs;
    ncache_timer_sm_p->u.perf_count_timer.pc = ncache_pc;
    ret = PINT_client_state_machine_post(smcb, NULL, NULL);
    if (ret < 0)
    {
        gossip_lerr("Error posting ncache timer.\n");
        return(ret);
    }

    ret = initialize_ops_in_progress_table();
    if (ret)
    {
	PVFS_perror("initialize_ops_in_progress_table", ret);
        return ret;
    }   

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if (ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return -PVFS_EDEVINIT;
    }

    /* setup a mapped region for I/O transfers */
    memset(s_io_desc, 0 , NUM_MAP_DESC * sizeof(struct PVFS_dev_map_desc));
    ret = PINT_dev_get_mapped_regions(NUM_MAP_DESC, s_io_desc, s_desc_params);
    if (ret < 0)
    {
	PVFS_perror("PINT_dev_get_mapped_region", ret);
	return ret;
    }

    ret = job_open_context(&s_client_dev_context);
    if (ret < 0)
    {
	PVFS_perror("device job_open_context failed", ret);
	return ret;
    }

    /*
      lock the remount mutex to make sure the remount isn't started
      until we're ready
    */
    pthread_mutex_lock(&remount_mutex);

    if (pthread_create(&remount_thread, NULL, exec_remount, NULL))
    {
	gossip_err("Cannot create remount thread!");
        return -1;
    }

    ret = process_vfs_requests();
    if (ret)
    {
	gossip_err("Failed to process vfs requests!");
    }

    /* join remount thread; should be long done by now */
    if (remount_complete)
    {
        pthread_join(remount_thread, NULL);
    }
    else
    {
        pthread_cancel(remount_thread);
    }

    finalize_ops_in_progress_table();

    /* free all allocated resources */
    for(i = 0; i < MAX_NUM_OPS; i++)
    {
        PINT_dev_release_unexpected(&s_vfs_request_array[i]->info);
        PINT_sys_release(s_vfs_request_array[i]->op_id);
        free(s_vfs_request_array[i]);
    }

    job_close_context(s_client_dev_context);

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_finalize();
#endif

    PINT_dev_finalize();
    PINT_dev_put_mapped_regions(NUM_MAP_DESC, s_io_desc);

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG,
                 "calling PVFS_sys_finalize()\n");
    if (PVFS_sys_finalize())
    {
        gossip_err("Failed to finalize PVFS\n");
        return 1;
    }

    /* forward the signal on to the parent */
    if(s_client_signal)
    {
        kill(0, s_client_signal);
    }

    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "%s terminating\n", argv[0]);
    return 0;
}

static void print_help(char *progname)
{
    assert(progname);

    printf("Usage: %s [OPTION]...[PATH]\n\n",progname);
    printf("-h, --help                    display this help and exit\n");
    printf("-a MS, --acache-timeout=MS    acache timeout in ms "
           "(default is 0 ms)\n");
    printf("--acache-soft-limit=LIMIT     acache soft limit\n");
    printf("--acache-hard-limit=LIMIT     acache hard limit\n");
    printf("--acache-reclaim-percentage=LIMIT acache reclaim percentage\n");
    printf("-n MS, --ncache-timeout=MS    ncache timeout in ms "
           "(default is 0 ms)\n");
    printf("--ncache-soft-limit=LIMIT     ncache soft limit\n");
    printf("--ncache-hard-limit=LIMIT     ncache hard limit\n");
    printf("--ncache-reclaim-percentage=LIMIT ncache reclaim percentage\n");
    printf("--perf-time-interval-secs=SECONDS length of perf counter intervals\n");
    printf("--perf-history-size=VALUE     number of perf counter intervals to maintain\n");
    printf("--logfile=VALUE               override the default log file\n");
    printf("--logtype=file|syslog         specify writing logs to file or syslog\n");
    printf("--logstamp=none|usec|datetime overrides the default log message's time stamp\n");
    printf("--gossip-mask=MASK_LIST       gossip logging mask\n");
    printf("--create-request-id           create a id which is transfered to the server\n");
    printf("--desc-count=VALUE            overrides the default # of kernel buffer descriptors\n");
    printf("--desc-size=VALUE             overrides the default size of each kernel buffer descriptor\n");
    printf("--events=EVENT_LIST           specify the events to enable\n");
}

static void parse_args(int argc, char **argv, options_t *opts)
{
    int ret = 0, option_index = 0;
    char *cur_option = NULL;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"acache-timeout",1,0,0},
        {"acache-reclaim-percentage",1,0,0},
        {"ncache-timeout",1,0,0},
        {"ncache-reclaim-percentage",1,0,0},
        {"perf-time-interval-secs",1,0,0},
        {"perf-history-size",1,0,0},
        {"gossip-mask",1,0,0},
        {"acache-hard-limit",1,0,0},
        {"acache-soft-limit",1,0,0},
        {"ncache-hard-limit",1,0,0},
        {"ncache-soft-limit",1,0,0},
        {"desc-count",1,0,0},
        {"desc-size",1,0,0},
        {"logfile",1,0,0},
        {"logtype",1,0,0},
        {"logstamp",1,0,0},
        {"child",0,0,0},
        {"events",1,0,0},
        {0,0,0,0}
    };

    assert(opts);
    opts->perf_time_interval_secs = PERF_DEFAULT_TIME_INTERVAL_SECS;
    opts->perf_history_size = PERF_DEFAULT_HISTORY_SIZE;

    while((ret = getopt_long(argc, argv, "ha:n:L:",
                             long_opts, &option_index)) != -1)
    {
        switch(ret)
        {
            case 0:
                cur_option = (char *)long_opts[option_index].name;
 
                if (strcmp("help", cur_option) == 0)
                {
                    goto do_help;
                }
                else if (strcmp("acache-timeout", cur_option) == 0)
                {
                    goto do_acache;
                }
                else if (strcmp("ncache-timeout", cur_option) == 0)
                {
                    goto do_ncache;
                }
                else if (strcmp("desc-count", cur_option) == 0) 
                {
                    ret = sscanf(optarg, "%u", &opts->dev_buffer_count);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid descriptor count value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->dev_buffer_count_set = 1;
                }
                else if (strcmp("desc-size", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->dev_buffer_size);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid descriptor size value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->dev_buffer_size_set = 1;
                }
                else if (strcmp("logfile", cur_option) == 0)
                {
                    goto do_logfile;
                }
                else if (strcmp("logtype", cur_option) == 0)
                {
                    opts->logtype = optarg;
                }
                else if (strcmp("logstamp", cur_option) == 0)
                {
                    if(strcmp(optarg, "none") == 0)
                    {
                        opts->logstamp_type = GOSSIP_LOGSTAMP_NONE;
                    }
                    else if(strcmp(optarg, "usec") == 0)
                    {
                        opts->logstamp_type = GOSSIP_LOGSTAMP_USEC;
                    }
                    else if(strcmp(optarg, "datetime") == 0)
                    {
                        opts->logstamp_type = GOSSIP_LOGSTAMP_DATETIME;
                    }
                    else
                    {
                        gossip_err(
                            "Error: invalid logstamp value. "
                            "See usage below\n\n");
                        print_help(argv[0]);
                        exit(EXIT_FAILURE);
                    }
                    opts->logstamp_type_set = 1;
                }
                else if (strcmp("acache-hard-limit", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->acache_hard_limit);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid acache-hard-limit value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->acache_hard_limit_set = 1;
                }
                else if (strcmp("acache-soft-limit", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->acache_soft_limit);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid acache-soft-limit value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->acache_soft_limit_set = 1;
                }
                else if (strcmp("acache-reclaim-percentage", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->acache_reclaim_percentage);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid "
                            "acache-reclaim-percentage value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->acache_reclaim_percentage_set = 1;
                }
                else if (strcmp("ncache-hard-limit", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->ncache_hard_limit);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid ncache-hard-limit value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->ncache_hard_limit_set = 1;
                }
                else if (strcmp("ncache-soft-limit", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->ncache_soft_limit);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid ncache-soft-limit value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->ncache_soft_limit_set = 1;
                }
                else if (strcmp("ncache-reclaim-percentage", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u", &opts->ncache_reclaim_percentage);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid ncache-reclaim-percentage value.\n");
                        exit(EXIT_FAILURE);
                    }
                    opts->ncache_reclaim_percentage_set = 1;
                }
                else if (strcmp("perf-time-interval-secs", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u",
                        &opts->perf_time_interval_secs);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid perf-time-interval-secs value.\n");
                        exit(EXIT_FAILURE);
                    }
                }
                else if (strcmp("perf-history-size", cur_option) == 0)
                {
                    ret = sscanf(optarg, "%u",
                        &opts->perf_history_size);
                    if(ret != 1)
                    {
                        gossip_err(
                            "Error: invalid perf-history-size value.\n");
                        exit(EXIT_FAILURE);
                    }
                }
                else if (strcmp("gossip-mask", cur_option) == 0)
                {
                    opts->gossip_mask = optarg;
                }
                else if (strcmp("child", cur_option) == 0)
                {
                    opts->child = 1;
                }
                else if (strcmp("events", cur_option) == 0)
                {
                    opts->events = optarg;
                }
                break;
            case 'h':
          do_help:
                print_help(argv[0]);
                exit(0);
            case 'L':
          do_logfile:
                opts->logfile = optarg;
                break;
            case 'a':
          do_acache:
                opts->acache_timeout = atoi(optarg);
                if (opts->acache_timeout < 0)
                {
                    gossip_err("Invalid acache timeout value of %d ms,"
                               "disabling the acache.\n",
                               opts->acache_timeout);
                    opts->acache_timeout = 0;
                }
                break;
            case 'n':
          do_ncache:
                opts->ncache_timeout = atoi(optarg);
                if (opts->ncache_timeout < 0)
                {
                    gossip_err("Invalid ncache timeout value of %d ms,"
                               "disabling the ncache.\n",
                               opts->ncache_timeout);
                    opts->ncache_timeout = 0;
                }
                break;
            default:
                gossip_err("Unrecognized option.  "
                        "Try --help for information.\n");
                exit(1);
        }
    }
    if (!opts->logfile)
    {
        opts->logfile = DEFAULT_LOGFILE;
    }
    if (!opts->logtype)
    {
        opts->logtype = "file";
    }
}

static void reset_acache_timeout(void)
{
    int min_stored_timeout = 0, max_acache_timeout_ms = 0;

    min_stored_timeout =
        PINT_server_config_mgr_get_abs_min_handle_recycle_time();

    /*
      if all file systems have been unmounted, this value will be -1,
      so don't do anything in that case
    */
    if (min_stored_timeout != -1)
    {
        /*
          determine the new maximum acache timeout value based on server
          handle recycle times and what the user specified on the command
          line.  if they differ then reset the entire acache to be sure
          there are no entries in the cache that could exceed the new
          timeout.
        */
        max_acache_timeout_ms = PVFS_util_min(
            (min_stored_timeout * 1000), s_opts.acache_timeout);

        if (max_acache_timeout_ms != s_opts.acache_timeout)
        {
            gossip_debug(
                GOSSIP_CLIENTCORE_DEBUG, "Resetting acache timeout to %d"
                " milliseconds\n (based on new dynamic configuration "
                "handle recycle time value)\n", max_acache_timeout_ms);

            PINT_acache_finalize();
            PINT_acache_initialize();
            s_opts.acache_timeout = max_acache_timeout_ms;
            set_acache_parameters(&s_opts);
        }
    }
    else
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "All file systems "
                     "unmounted. Not resetting the acache.\n");
    }
}

static void reset_ncache_timeout(void)
{
    int min_stored_timeout = 0, max_ncache_timeout_ms = 0;

    min_stored_timeout =
        PINT_server_config_mgr_get_abs_min_handle_recycle_time();

    /*
      if all file systems have been unmounted, this value will be -1,
      so don't do anything in that case
    */
    if (min_stored_timeout != -1)
    {
        /*
          determine the new maximum ncache timeout value based on server
          handle recycle times and what the user specified on the command
          line.  if they differ then reset the entire ncache to be sure
          there are no entries in the cache that could exceed the new
          timeout.
        */
        max_ncache_timeout_ms = PVFS_util_min(
            (min_stored_timeout * 1000), s_opts.ncache_timeout);

        if (max_ncache_timeout_ms != s_opts.ncache_timeout)
        {
            gossip_debug(
                GOSSIP_CLIENTCORE_DEBUG, "Resetting ncache timeout to %d"
                " milliseconds\n (based on new dynamic configuration "
                "handle recycle time value)\n", max_ncache_timeout_ms);

            PINT_ncache_finalize();
            PINT_ncache_initialize();
            s_opts.ncache_timeout = max_ncache_timeout_ms;
            set_ncache_parameters(&s_opts);
        }
    }
    else
    {
        gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "All file systems "
                     "unmounted. Not resetting the ncache.\n");
    }
}

#ifndef GOSSIP_DISABLE_DEBUG
static char *get_vfs_op_name_str(int op_type)
{
    typedef struct
    {
        int type;
        char *type_str;
    } __vfs_op_name_info_t;

    static __vfs_op_name_info_t vfs_op_info[] =
    {
        { PVFS2_VFS_OP_INVALID, "PVFS_VFS_OP_INVALID" },
        { PVFS2_VFS_OP_FILE_IO, "PVFS2_VFS_OP_FILE_IO" },
        { PVFS2_VFS_OP_LOOKUP, "PVFS2_VFS_OP_LOOKUP" },
        { PVFS2_VFS_OP_CREATE, "PVFS2_VFS_OP_CREATE" },
        { PVFS2_VFS_OP_GETATTR, "PVFS2_VFS_OP_GETATTR" },
        { PVFS2_VFS_OP_REMOVE, "PVFS2_VFS_OP_REMOVE" },
        { PVFS2_VFS_OP_MKDIR, "PVFS2_VFS_OP_MKDIR" },
        { PVFS2_VFS_OP_READDIR, "PVFS2_VFS_OP_READDIR" },
        { PVFS2_VFS_OP_READDIRPLUS, "PVFS2_VFS_OP_READDIRPLUS" },
        { PVFS2_VFS_OP_SETATTR, "PVFS2_VFS_OP_SETATTR" },
        { PVFS2_VFS_OP_SYMLINK, "PVFS2_VFS_OP_SYMLINK" },
        { PVFS2_VFS_OP_RENAME, "PVFS2_VFS_OP_RENAME" },
        { PVFS2_VFS_OP_STATFS, "PVFS2_VFS_OP_STATFS" },
        { PVFS2_VFS_OP_TRUNCATE, "PVFS2_VFS_OP_TRUNCATE" },
        { PVFS2_VFS_OP_MMAP_RA_FLUSH, "PVFS2_VFS_OP_MMAP_RA_FLUSH" },
        { PVFS2_VFS_OP_FS_MOUNT, "PVFS2_VFS_OP_FS_MOUNT" },
        { PVFS2_VFS_OP_FS_UMOUNT, "PVFS2_VFS_OP_FS_UMOUNT" },
        { PVFS2_VFS_OP_GETXATTR,  "PVFS2_VFS_OP_GETXATTR" },
        { PVFS2_VFS_OP_SETXATTR,  "PVFS2_VFS_OP_SETXATTR" },
        { PVFS2_VFS_OP_LISTXATTR, "PVFS2_VFS_OP_LISTXATTR" },
        { PVFS2_VFS_OP_REMOVEXATTR, "PVFS2_VFS_OP_REMOVEXATTR" },
        { PVFS2_VFS_OP_CANCEL, "PVFS2_VFS_OP_CANCEL" },
        { PVFS2_VFS_OP_FSYNC,  "PVFS2_VFS_OP_FSYNC" },
        { PVFS2_VFS_OP_PARAM,  "PVFS2_VFS_OP_PARAM" },
        { PVFS2_VFS_OP_PERF_COUNT, "PVFS2_VFS_OP_PERF_COUNT" },
        { PVFS2_VFS_OP_FSKEY,  "PVFS2_VFS_OP_FSKEY" },
        { PVFS2_VFS_OP_FILE_IOX, "PVFS2_VFS_OP_FILE_IOX" },
        { 0, "UNKNOWN" }
    };

    int i = 0;
    int limit = (int)(sizeof(vfs_op_info) / sizeof(__vfs_op_name_info_t));
    for(i = 0; i < limit; i++)
    {
        if (vfs_op_info[i].type == op_type)
        {
            return vfs_op_info[i].type_str;
        }
    }
    return vfs_op_info[limit-1].type_str;
}
#endif

static int set_acache_parameters(options_t* s_opts)
{
    int ret = -1;

    /* pass along acache settings if they were specified on command line */
    if(s_opts->acache_reclaim_percentage_set)
    {
        ret = PINT_acache_set_info(ACACHE_RECLAIM_PERCENTAGE, 
            s_opts->acache_reclaim_percentage);
        if(ret < 0)
        {
            PVFS_perror_gossip("PINT_acache_set_info (reclaim-percentage)", ret);
            return(ret);
        }
    }
    if(s_opts->acache_hard_limit_set)
    {
        ret = PINT_acache_set_info(ACACHE_HARD_LIMIT, 
            s_opts->acache_hard_limit);
        if(ret < 0)
        {
            PVFS_perror_gossip("PINT_acache_set_info (hard-limit)", ret);
            return(ret);
        }
    }
    if(s_opts->acache_soft_limit_set)
    {
        ret = PINT_acache_set_info(ACACHE_SOFT_LIMIT, 
            s_opts->acache_soft_limit);
        if(ret < 0)
        {
            PVFS_perror_gossip("PINT_acache_set_info (soft-limit)", ret);
            return(ret);
        }
    }

    /* for timeout we always take the command line argument value */
    ret = PINT_acache_set_info(ACACHE_TIMEOUT_MSECS, s_opts->acache_timeout);
    if(ret < 0)
    {
        PVFS_perror_gossip("PINT_acache_set_info (timout-msecs)", ret);
        return(ret);
    }

    return(0);
}

static int set_ncache_parameters(options_t* s_opts)
{
    int ret = -1;

    /* pass along ncache settings if they were specified on command line */
    if(s_opts->ncache_reclaim_percentage_set)
    {
        ret = PINT_ncache_set_info(NCACHE_RECLAIM_PERCENTAGE, 
            s_opts->ncache_reclaim_percentage);
        if(ret < 0)
        {
            PVFS_perror_gossip("PINT_ncache_set_info (reclaim-percentage)", ret);
            return(ret);
        }
    }
    if(s_opts->ncache_hard_limit_set)
    {
        ret = PINT_ncache_set_info(NCACHE_HARD_LIMIT, 
            s_opts->ncache_hard_limit);
        if(ret < 0)
        {
            PVFS_perror_gossip("PINT_ncache_set_info (hard-limit)", ret);
            return(ret);
        }
    }
    if(s_opts->ncache_soft_limit_set)
    {
        ret = PINT_ncache_set_info(NCACHE_SOFT_LIMIT, 
            s_opts->ncache_soft_limit);
        if(ret < 0)
        {
            PVFS_perror_gossip("PINT_ncache_set_info (soft-limit)", ret);
            return(ret);
        }
    }

    /* for timeout we always take the command line argument value */
    ret = PINT_ncache_set_info(NCACHE_TIMEOUT_MSECS, s_opts->ncache_timeout);
    if(ret < 0)
    {
        PVFS_perror_gossip("PINT_ncache_set_info (timout-msecs)", ret);
        return(ret);
    }

    return(0);
}

static void set_device_parameters(options_t *s_opts)
{
    if (s_opts->dev_buffer_count_set)
    {
        s_desc_params[BM_IO].dev_buffer_count = s_opts->dev_buffer_count;
    }
    else
    {
        s_desc_params[BM_IO].dev_buffer_count = PVFS2_BUFMAP_DEFAULT_DESC_COUNT;
    }
    if (s_opts->dev_buffer_size_set)
    {
        s_desc_params[BM_IO].dev_buffer_size  = s_opts->dev_buffer_size;
    }
    else
    {
        s_desc_params[BM_IO].dev_buffer_size = PVFS2_BUFMAP_DEFAULT_DESC_SIZE;
    }
    /* No command line options accepted for the readdir buffers */
    s_desc_params[BM_READDIR].dev_buffer_count = PVFS2_READDIR_DEFAULT_DESC_COUNT;
    s_desc_params[BM_READDIR].dev_buffer_size  = PVFS2_READDIR_DEFAULT_DESC_SIZE;
    return;
}

static int get_mac(void);

inline static void fill_hints(PVFS_hint *hints, vfs_request_t *req)
{
    int32_t mac;

    *hints = NULL;
    if(!s_opts.events) return;

    mac = get_mac();
    gossip_debug(GOSSIP_CLIENTCORE_DEBUG, "mac: %d\n", mac);
    PVFS_hint_add(hints, PVFS_HINT_CLIENT_ID_NAME, sizeof(mac), &mac);
}

static int get_mac(void)
{
    int sock;
    struct ifreq iface;
    int mac;

    strcpy(iface.ifr_name,"eth0");

    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
        return -1;
    }
    else
    {
        if((ioctl(sock, SIOCGIFHWADDR, &iface)) < 0)
        {
            perror("ioctl SIOCGIFHWADDR");
            return -1;
        }
        else
        {
            mac = iface.ifr_hwaddr.sa_data[0] & 0xff;
            mac |= (iface.ifr_hwaddr.sa_data[1] & 0xff) << 8;
            mac |= (iface.ifr_hwaddr.sa_data[2] & 0xff) << 8;
            mac |= (iface.ifr_hwaddr.sa_data[3] & 0xff) << 8;
            return mac;
        }
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
