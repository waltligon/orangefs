/*
 * (C) 2006 Clemson University and The University of Chicago
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
#include <semaphore.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-types.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "synch-server-mapping.h"
#include "quicklist.h"
#include "quickhash.h"
#include "bmi-method-support.h"
#include "pvfs2-synch-client.h"
#include "synch-cache.h"

#include "dlm_prot_client.h" /* DLM client API */
#include "vec_prot_client.h" /* VEC client API */

/* sr_status is either 0 or -ve error code. We use the same field to indicate if op is in progress */
#define  REQ_IN_PROGRESS  1

/* How many version vectors to get on a read? */
#define  MAX_VERSION_GET  1

struct synch_options {
    enum PVFS_synch_method synch_method;
    union {
        struct dlm_options     dlm_options;
        struct vec_options     vec_options;
    } synch;
};

struct synch_req {
    PVFS_fs_id              sr_fsid;
    PVFS_handle             sr_handle;
    PVFS_offset             sr_offset;
    PVFS_size               sr_size;
    int                     sr_stripe_size;
    int                     sr_nservers;
    int                     sr_max_vectors;
    /* <sr_method, sr_rpc> together indicate what method to invoke */
    enum PVFS_synch_method  sr_method;
    int                     sr_rpc;
    enum PVFS_io_type       sr_io_type;
    pthread_mutex_t         sr_lock;
    pthread_cond_t          sr_cv;
    /* Cancellation is indicated by calling thread */
    int                     sr_cancelled;
    /* Normally set and used by the synchronization thread */
    int                     sr_status;
    /* sr_result will be freed by the caller */
    void                    *sr_result;
    struct qlist_head       sr_next; /* chains all such requests */
};

/*
 * the synch thread requires a mutex lock and a condition variable
 * to synchronize with the producer threads (i.e. the main thread)
 * which inserts requests for synch locks into its list
 */
static sem_t           synch_sem;
static pthread_t       synch_thread;
static pthread_mutex_t synch_req_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  synch_req_cv    = PTHREAD_COND_INITIALIZER;
static QLIST_HEAD(synch_req_list);
static int             pint_synch_init = 0;


static void serialize_handle(char **synch_handle, PVFS_object_ref *ref)
{
    encode_PVFS_fs_id(synch_handle, &ref->fs_id);
    encode_PVFS_handle(synch_handle, &ref->handle);
    return;
}

static void deserialize_handle(char **synch_handle, PVFS_object_ref *ref)
{
    decode_PVFS_fs_id(synch_handle, &ref->fs_id);
    decode_PVFS_handle(synch_handle, &ref->handle);
}

/* This function frees memory allocated to req */
static void set_synchreq_status(struct synch_req *req, int status, void *result)
{
    pthread_mutex_lock(&req->sr_lock);
    /* Has this request been cancelled in the meantime */
    if (req->sr_cancelled == 1)
    {
        pthread_mutex_unlock(&req->sr_lock);
        PINT_synch_result_dtor(result);
        free(req);
        return;
    }
    req->sr_status = status;
    req->sr_result = result;
    pthread_cond_signal(&req->sr_cv);
    pthread_mutex_unlock(&req->sr_lock);
    return;
}

/*
 * Returns 0 on success and -ve error on failure 
 */
static int do_dlm_req(struct dlm_options *options, 
        struct synch_req *req, char *encoded_handle, void **presult)
{
    int err = 0;
    struct synch_result *result = NULL;
    PVFS_object_ref ref;

    deserialize_handle(&encoded_handle, &ref);
    /* process a dlm request */
    if (req->sr_rpc == DLM_LOCK)
    {
        result = (struct synch_result *) calloc(1, sizeof(struct synch_result));
        if (result == NULL) {
            *presult = NULL;
            gossip_err("DLM_lock: could not allocate memory\n");
            return -EINVAL;
        }
        result->method = PVFS_SYNCH_DLM;
        err = dlm_lock(options, encoded_handle, 
                req->sr_io_type == PVFS_IO_READ 
                    ? DLM_READ_MODE : DLM_WRITE_MODE,
                req->sr_offset, req->sr_size, 
                &result->synch.dlm.dlm_token);
        /* Stubs to allow caching of lock tokens. Currently noops */
        if (err == 0) {
            PINT_synch_cache_insert(PVFS_SYNCH_DLM, &ref, (void *) &result->synch.dlm.dlm_token);
        }
        *presult = (void *) result;
    }
    else if (req->sr_rpc == DLM_UNLOCK)
    {
        *presult = NULL;
        result = (struct synch_result *) req->sr_result;
        if (result == NULL || result->method != PVFS_SYNCH_DLM) {
            gossip_err("DLM_unlock found NULL token or invalid method!\n");
            return -EINVAL;
        }
        /* Stubs to invalidate synch cache. Currently noops */
        PINT_synch_cache_invalidate(PVFS_SYNCH_DLM, &ref);
        err = dlm_unlock(options, result->synch.dlm.dlm_token);
    }
    return err;
}

/*
 * Returns 0 on success and -ve error on failure 
 */
static int do_vec_req(struct vec_options *options,
        struct synch_req *req, char *encoded_handle, void **presult)
{
    int err = 0;
    PVFS_object_ref ref;
    struct synch_result *result = NULL;

    deserialize_handle(&encoded_handle, &ref);
    result = (struct synch_result *) calloc(1, sizeof(struct synch_result));
    if (result == NULL) {
        *presult = NULL;
        gossip_err("DLM_lock: could not allocate memory\n");
        return -EINVAL;
    }
    result->method = PVFS_SYNCH_VECTOR;

    /* process a version server request */
    if (req->sr_rpc == VEC_GET)
    {
        vec_vectors_t *v;
        vec_svectors_t sv;

        /* See if we have any vectors cached locally */
        v = PINT_synch_cache_get(PVFS_SYNCH_VECTOR, &ref);
        err = vec_get(options, encoded_handle,
                    req->sr_io_type == PVFS_IO_READ ? VEC_READ_MODE : VEC_WRITE_MODE,
                    req->sr_offset, req->sr_size, 
                    req->sr_stripe_size, req->sr_nservers, 
                    req->sr_max_vectors, v, &sv);
        /* successful version get */
        if (err == 0) 
        {
            assert(sv.vec_svectors_t_len <= req->sr_max_vectors);
            /* construct result structure for caller (assuming only 1 vector was requested) */
            result->synch.vec.nvector = sv.vec_svectors_t_val[0].vec_vectors_t_len;
            result->synch.vec.vector = sv.vec_svectors_t_val[0].vec_vectors_t_val;
            free(sv.vec_svectors_t_val);
            sv.vec_svectors_t_val = NULL;
            sv.vec_svectors_t_len = 0;
        }
        *presult = (void *) result;
    }
    else if (req->sr_rpc == VEC_PUT)
    {
        vec_vectors_t v;

        err = vec_put(options, encoded_handle,
                    req->sr_io_type == PVFS_IO_READ ? VEC_READ_MODE : VEC_WRITE_MODE,
                    req->sr_offset, req->sr_size,
                    req->sr_stripe_size, req->sr_nservers,
                    &v);
        if (err == 0) 
        {
            /* cache the version vectors so that subsequent get's can obtain this version */
            PINT_synch_cache_insert(PVFS_SYNCH_VECTOR, &ref, (void *) &v);
            /* Construct result structure for caller */
            result->synch.vec.nvector = v.vec_vectors_t_len;
            result->synch.vec.vector  = v.vec_vectors_t_val;
        }
        *presult = (void *) result;
    }
    return err;
}

/*
 * For a given vfs request, try to fetch
 * the lock tokens. 
 * NOTE: We are running without any list locks
 * and req->sr_lock. So we should not do
 * any op_state field manipulation or list
 * insertions in this function.
 * Returns 0 on success and -ve number
 * on timeout or errors!
 */
static int process_synch_request(struct synch_req *req)
{
    struct synch_options opt;
    char *ptr = NULL;
    void *result = NULL;
    dlm_handle_t dlm_h;
    vec_handle_t vec_h;
    int ret;
    PVFS_object_ref ref;

    memset(&opt, 0, sizeof(opt));
    opt.synch_method = req->sr_method;
    if (req->sr_method == PVFS_SYNCH_DLM)
    {
        /* For now just use TCP */
        opt.synch.dlm_options.tcp = 1;
        /* Find the server responsible for handling this */
        ret = PINT_synch_server_mapping(req->sr_method, req->sr_handle,
                    req->sr_fsid, (struct sockaddr *) &opt.synch.dlm_options.dlm_addr);
        ptr = dlm_h;
    }
    else if (req->sr_method == PVFS_SYNCH_VECTOR)
    {
        /* For now just use TCP */
        opt.synch.vec_options.tcp = 1;
        /* Find the server responsible for handling this */
        ret = PINT_synch_server_mapping(req->sr_method, req->sr_handle,
                    req->sr_fsid, (struct sockaddr *) &opt.synch.vec_options.vec_addr);
        ptr = vec_h;
    }
    if (ret < 0)
    {
        gossip_err("Could not find server responsible for synchronization\n");
        set_synchreq_status(req, ret, NULL);
        return 0;
    }
    /* Serialize the handle/identifier of the file system object */
    ref.handle = req->sr_handle;
    ref.fs_id  = req->sr_fsid;
    serialize_handle(&ptr, &ref);
    if (req->sr_method == PVFS_SYNCH_DLM)
    {
        ret = do_dlm_req(&opt.synch.dlm_options, req, ptr, &result); 
        set_synchreq_status(req, ret, result);
    }
    else if (req->sr_method == PVFS_SYNCH_VECTOR)
    {
        ret = do_vec_req(&opt.synch.vec_options, req, ptr, &result); 
        set_synchreq_status(req, ret, result);
    }
    return 0;
}

/* This needs to be setup by the synch thread */
static void synch_sig_handler(int signum, siginfo_t *info, void *unused)
{
    pthread_exit(NULL);
}

/*
 * This thread essentially sits in a loop
 * waiting for requests to show up on its 
 * request list and uses RPCs to communicate with
 * either the version vector servers or with the 
 * DLM servers to synchronize data accesses made by
 * multiple clients. Once it satisfies the RPC request, 
 * it wakes up any clients that may have been waiting for
 * it to finish.
 */

static void *exec_synch(void *ptr)
{
    struct sigaction sigact;
    memset(&sigact, 0, sizeof(sigact));

    sigact.sa_flags  |= SA_SIGINFO;
    sigact.sa_handler = (__sighandler_t) synch_sig_handler;
    /* Setup a signal handler SIGUSR1 */
    sigaction(SIGUSR1, &sigact, NULL);
    pthread_mutex_lock(&synch_req_mutex);
    /* wake the main thread */
    sem_post(&synch_sem);

    while (1) 
    {
        struct synch_req *req = NULL;
        /* Empty request list */
        if (qlist_empty(&synch_req_list))
        {
            /* block until something shows up */
            pthread_cond_wait(&synch_req_cv, &synch_req_mutex);
        }
        else /* not empty! */
        {
            req = qlist_entry(synch_req_list.next, struct synch_req, sr_next);
            qlist_del(&req->sr_next);
            /* unlock the global list lock */
            pthread_mutex_unlock(&synch_req_mutex);
            pthread_mutex_lock(&req->sr_lock);
            /* if req has been cancelled, dont bother servicing it */
            if (req->sr_cancelled == 1) {
                pthread_mutex_unlock(&req->sr_lock);
                free(req);
                goto acquire;
            }
            pthread_mutex_unlock(&req->sr_lock);
            /*
             * Technically, there is a small window during which
             * req could have been cancelled, but we would still be
             * processing it because we dont hold req->sr_lock 
             * during processing.
             */
            process_synch_request(req);
acquire:
            /* Reacquire list lock */
            pthread_mutex_lock(&synch_req_mutex);
        }
    }
    pthread_mutex_unlock(&synch_req_mutex);
    return NULL;
}

/* 
 * Service a synchronization request.
 * Timeout is used to indicate whether
 * a) we should just post (timeout < 0)
 * b) we should issue a blocking rpc call (no timeout)
 * c) we should issue a blocking timeout rpc call (specified timeout)
 */
static PVFS_error service_synch_req(struct synch_req *req, int timeout, void **user_ptr)
{
    int err;

    /* lock the request */
    pthread_mutex_lock(&req->sr_lock);
    req->sr_cancelled = 0;
    req->sr_status = REQ_IN_PROGRESS;
    /* Insert request into the thread's list atomically and wake the thread */
    pthread_mutex_lock(&synch_req_mutex);
    qlist_add_tail(&req->sr_next, &synch_req_list);
    pthread_cond_signal(&synch_req_cv);
    pthread_mutex_unlock(&synch_req_mutex);
    if (timeout < 0) {
        if (user_ptr)
            *user_ptr = req;
        /* Queued the request, now unlock the request and return success */
        pthread_mutex_unlock(&req->sr_lock);
        return 0;
    }

    if (timeout > 0) {
        struct timeval now;
        struct timespec wait_time;

        gettimeofday(&now, NULL);
        wait_time.tv_sec = now.tv_sec + (timeout / 1000);
        wait_time.tv_nsec = now.tv_usec * 1000 + ((timeout % 1000) * 1000000);
        if (wait_time.tv_nsec > 1000000000)
        {
            wait_time.tv_nsec -= 1000000000;
            wait_time.tv_sec++;
        }
        /* timed wait until the RPC request is serviced */
        err = pthread_cond_timedwait(&req->sr_cv, &req->sr_lock, &wait_time);
    }
    else {
        /* infinite wait */
        err = pthread_cond_wait(&req->sr_cv, &req->sr_lock);
    }
    if (err == ETIMEDOUT) {
        assert(req->sr_status == REQ_IN_PROGRESS);
        /* mark it as a cancelled request.
         * NOTE: Cancelled requests need to be handled specially by the thread.
         * In case of a succesful DLM lock request that has been cancelled 
         * it must issue a unlock as well!
         */
        req->sr_cancelled = 1;
        /* dont free the req since it will be freed by the thread */
        pthread_mutex_unlock(&req->sr_lock);
        if (user_ptr)
            *user_ptr = NULL;
        return err;
    }
    else {
        assert(req->sr_status != REQ_IN_PROGRESS);
        /* query the status of the operation */
        err = req->sr_status;
        /* return the result */
        if (user_ptr)
            *user_ptr = req;
        pthread_mutex_unlock(&req->sr_lock);
        /* req will be freed in post_synch_req only */
        return err;
    }
}

/*
 * This function attempts to issue a RPC call specified
 * according to <method, rpc_method>.
 * This will block until atleast timeout milli-seconds
 * have elapsed.
 *
 * Returns a errno style error for error situations and 0 for success.
 *
 * if timeout < 0  ==> post a synchronization operation that will prevent this 
 *                     sm progress but allows other sm's to progress.
 * if timeout == 0 ==> invoke a infinite blocking synchronization operation that 
 *                     will prevent any sm from progressing.
 * if timeout > 0  ==> invoke a finite blocking synchronization operation that will
 *                     prevent any sm from progressing upto timeout mseconds.
 */

PVFS_error PINT_pre_synch(enum PVFS_synch_method method,
                    PVFS_object_ref *ref, 
                    enum PVFS_io_type io_type,
                    PVFS_offset  offset,
                    PVFS_size  count,
                    int    stripe_size,
                    int    nservers,
                    int    timeout,
                    void **user_ptr)
{
    struct synch_req *req;

    if (pint_synch_init == 0 || method == PVFS_SYNCH_NONE)
        return -EINVAL;

    if (io_type != PVFS_IO_READ && io_type != PVFS_IO_WRITE)
        return -EINVAL;

    if (method != PVFS_SYNCH_DLM && method != PVFS_SYNCH_VECTOR)
        return -EINVAL;

    req = (struct synch_req *) calloc(1, sizeof(struct synch_req));
    if (!req) 
        return -ENOMEM;

    req->sr_fsid      = ref->fs_id;
    req->sr_handle    = ref->handle;
    req->sr_offset    = offset;
    req->sr_size      = count;
    req->sr_method    = method;
    req->sr_stripe_size = stripe_size;
    req->sr_nservers  = nservers;
    req->sr_max_vectors = MAX_VERSION_GET;

    if (method == PVFS_SYNCH_DLM) {
        req->sr_rpc = DLM_LOCK;
    }
    else if (method == PVFS_SYNCH_VECTOR) {
        if (io_type == PVFS_IO_READ) {
            req->sr_rpc = VEC_GET;
        }
        else {
            req->sr_rpc = VEC_PUT;
        }
    }
    req->sr_io_type   = io_type;
    pthread_mutex_init(&req->sr_lock, NULL);
    pthread_cond_init(&req->sr_cv, NULL);
    return service_synch_req(req, timeout, user_ptr);
}

/*
 * Cleans up everything
 * a) in case method is set to PVFS_SYNCH_DLM, we need to try and unlock
 * b) nothing to for PVFS_SYNCH_VECTOR
 * Same semantics for timeout
 */
PVFS_error PINT_post_synch(void *user_ptr, int timeout)
{
    struct synch_req *req;

    req = (struct synch_req *) user_ptr;
    if (pint_synch_init == 0 || req == NULL)
        return -EINVAL;

    if (req->sr_method == PVFS_SYNCH_VECTOR) {
        free(req);
        return 0;
    }
    req->sr_rpc = DLM_UNLOCK;
    service_synch_req(req, timeout, NULL);
    free(req);
    return 0;
}

PVFS_error PINT_synch_cancel(void **user_ptr)
{
    struct synch_req *req;
    PVFS_error ret;

    if (pint_synch_init == 0 || user_ptr == NULL || *user_ptr == NULL)
        return -EINVAL;
    req = (struct synch_req *) *user_ptr;
    pthread_mutex_lock(&req->sr_lock);
    if (req->sr_status == REQ_IN_PROGRESS) {
        /* mark it as a cancelled request, dont free it though */
        req->sr_cancelled = 1;
        ret = 0;
    }
    else {
        ret = req->sr_status;
        /* Free the result, since we are not interested in it anyway */
        PINT_synch_result_dtor(req->sr_result);
        req->sr_result = NULL;
        free(req);
    }
    pthread_mutex_unlock(&req->sr_lock);
    *user_ptr = NULL;
    return ret;
}

/*
 * Waits until a previously posted synchronization operation
 * has finished.
 * Returns the error status of the posted synch. operation.
 */
PVFS_error PINT_synch_wait(void *user_ptr)
{
    struct synch_req *req = (struct synch_req *) user_ptr;
    PVFS_error ret;

    if (pint_synch_init == 0 || req == NULL)
        return -EINVAL;

    pthread_mutex_lock(&req->sr_lock);
    ret = (req->sr_status == REQ_IN_PROGRESS) ? 0 : 1;
    if (!ret) {
        pthread_cond_wait(&req->sr_cv, &req->sr_lock);
        ret = req->sr_status;
    }
    pthread_mutex_unlock(&req->sr_lock);
    /* All freeing is done by PINT_post_synch() */
    return ret;
}

/* Make sure that callers of this routine call it prior to calling PINT_post_synch() */
struct synch_result *PINT_synch_result(void *user_ptr)
{
    struct synch_req *req = (struct synch_req *) user_ptr;

    if (pint_synch_init == 0 || req == NULL)
        return NULL;
    return (struct synch_result *) req->sr_result;
}

void PINT_synch_result_dtor(struct synch_result *result)
{
    if (pint_synch_init == 0 || !result)
        return;
    if (result->method == PVFS_SYNCH_VECTOR) {
        free(result->synch.vec.vector);
    }
    free(result);
    return;
}

/* Spawn the thread that does the RPC communication and initialize the synchronization subsystem */

PVFS_error PINT_synch_init(void)
{
    int ret;
    if (pint_synch_init == 1)
    {
        gossip_err("PINT_synch_init: already initialized?\n");
        return -EINVAL;
    }
    if ((ret = PINT_initialize_synch_server_mapping_table()) < 0)
    {
        gossip_err("Could not initialize synch server mapping table\n");
        return ret;
    }
    if ((ret = PINT_synch_cache_init()) < 0) 
    {
        gossip_err("Could not initialize synch cache\n");
        PINT_finalize_synch_server_mapping_table();
        return ret;
    }
    sem_init(&synch_sem, 0, 0);
    /*
    * spawn a thread that will handle the synch. communication
    * for posix I/O consistency semantics.
    */
    if (pthread_create(&synch_thread, NULL, exec_synch, NULL))
    {
        PINT_synch_cache_finalize();
        PINT_finalize_synch_server_mapping_table();
        gossip_err("Cannot create synchronizer thread!\n");
        return -1;
    }
    /* Wait until the thread sets itself */
    sem_wait(&synch_sem);
    pint_synch_init = 1;
    return 0;
}

/* Cleans up the thread that does the RPC communication */
void PINT_synch_cleanup(void)
{
    pthread_kill(synch_thread, SIGUSR1);
    pthread_join(synch_thread, NULL);
    PINT_synch_cache_finalize();
    PINT_finalize_synch_server_mapping_table();
    pint_synch_init = 0;
    return;
}

/*
 * ping a given file system + a specific machine's synchronization servers.
 */
int PINT_synch_ping(PVFS_fs_id fs_id, const char *string_server_address,
        enum PVFS_synch_method method)
{
    char *tcp_string = NULL, *ptr;
    struct sockaddr_in synch_addr;
	struct hostent *hent;
    int dlm_port = -1, vec_port = -1, port = -1;

    if (method == PVFS_SYNCH_NONE)
    {
        return 0;
    }
    PVFS_mgmt_map_synch_ports(
            fs_id, (char *) string_server_address,
            &dlm_port, &vec_port);
    if (method == PVFS_SYNCH_DLM)
        port = (dlm_port <= 0) ? DLM_REQ_PORT : dlm_port;
    else
        port = (vec_port <= 0) ? VEC_REQ_PORT : vec_port;

    /* Currently, we only support tcp server addresses */
    tcp_string = string_key("tcp", string_server_address);
    if (tcp_string == NULL)
    {
        gossip_err("synch-client: Unsupported server addresses\n");
        return -EINVAL;
    }
    /* tcp_string has the port numbers. zero that out */
    ptr = strchr(tcp_string, ':');
    if (ptr)
    {
        *ptr = '\0';
    }
    memset(&synch_addr, 0, sizeof(synch_addr));
    synch_addr.sin_family = AF_INET;
    synch_addr.sin_port   = htons((short) port);
    hent = gethostbyname(tcp_string);
    if (hent == NULL)
    {
        gossip_err("synch-client: gethostbyname failed\n");
        return -EINVAL;
    }
    memcpy(&synch_addr.sin_addr.s_addr, hent->h_addr_list[0], hent->h_length);
    free(tcp_string);

    if (method == PVFS_SYNCH_DLM)
    {
        struct dlm_options dlmopt;
        dlmopt.tcp = 1;
        dlmopt.dlm_addr = (struct sockaddr *) &synch_addr;

        return dlm_ping(&dlmopt);
    }
    else if (method == PVFS_SYNCH_VECTOR)
    {
        struct vec_options vecopt;
        vecopt.tcp = 1;
        vecopt.vec_addr = (struct sockaddr *) &synch_addr;
        return vec_ping(&vecopt);
    }
    return -EINVAL;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
