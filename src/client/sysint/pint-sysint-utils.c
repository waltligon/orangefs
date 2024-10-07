/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef WIN32
#include <unistd.h>
#endif
#include <sys/types.h>

#include "pvfs2-sysint.h"
#include "pvfs2-req-proto.h"
#include "pint-sysint-utils.h"
#include "pint-cached-config.h"
#include "acache.h"
#include "PINT-reqproto-encode.h"
#include "trove.h"
#include "server-config-mgr.h"
#include "str-utils.h"
#include "pvfs2-util.h"
#include "client-state-machine.h"
#include "gen-locks.h"


#ifdef HAVE_OPENSSL

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t *openssl_mutexes = NULL;
static int security_init_status = 0;


struct CRYPTO_dynlock_value
{
    gen_mutex_t mutex;
};

static int setup_threading(void);
static void cleanup_threading(void);
/* OpenSSL 1.0 allows thread id to be either long or a pointer  */
/* OpenSSl 1.1 does not use threadid callback functions anymore */
#if OPENSSL_VERSION_NUMBER & 0x10000000L
#   ifndef CRYPTO_THREADID_set_callback
#          define PVFS_OPENSSL_USE_THREADID
#          define CRYPTO_SET_ID_CALLBACK    CRYPTO_THREADID_set_callback
#          define ID_FUNCTION               threadid_function
           static void threadid_function(CRYPTO_THREADID *);
#   endif
#else
#   define CRYPTO_SET_ID_CALLBACK    CRYPTO_set_id_callback
#   define ID_FUNCTION               id_function
    static unsigned long id_function(void);
#endif

#ifndef CRYPTO_set_locking_callback
static void locking_function(int, int, const char*, int);
#endif

#ifndef CRYPTO_set_dynlock_create_callback
static struct CRYPTO_dynlock_value *dyn_create_function(const char*, int);
#endif

#ifndef CRYPTO_set_dynlock_lock_callback
static void dyn_lock_function(int,struct CRYPTO_dynlock_value*,const char*,int);
#endif

#ifndef CRYPTO_set_dynlock_destroy_callback
static void dyn_destroy_function(struct CRYPTO_dynlock_value*,const char*,int);
#endif

#endif /* HAVE_OPENSSL */


/*
  analogous to 'PINT_get_server_config' in config-utils.c -- only an
  fs_id is required since any client may know about different server
  configurations during run-time
*/
struct server_configuration_s *PINT_get_server_config_struct(
                                                PVFS_fs_id fs_id)
{
    return PINT_server_config_mgr_get_config(fs_id);
}

void PINT_put_server_config_struct(struct server_configuration_s *config)
{
    PINT_server_config_mgr_put_config(config);
}

/* PINT_lookup_parent()
 *
 * given a pathname and an fsid, looks up the handle of the parent
 * directory
 *
 * returns 0 on success, -PVFS_errno on failure
 */
int PINT_lookup_parent(char *filename,
                       PVFS_fs_id fs_id,
                       PVFS_credential *credential,
                       PVFS_object_ref *parent_ref)
{
    int ret = -PVFS_EINVAL;
    char buf[PVFS_SEGMENT_MAX] = {0};
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look, 0, sizeof(PVFS_sysresp_lookup));
    memset(parent_ref, 0, sizeof(PVFS_object_ref));

    if (PINT_get_base_dir(filename, buf, PVFS_SEGMENT_MAX))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n", filename);
        memset(parent_ref, 0, sizeof(*parent_ref));
        return ret;
    }

    ret = PVFS_sys_lookup(fs_id, buf,
                          credential,
                          &resp_look,
                          PVFS2_LOOKUP_LINK_FOLLOW,
                          NULL);
    if (ret < 0)
    {
        gossip_err("Lookup failed on %s\n", buf);
        memset(parent_ref, 0, sizeof(*parent_ref));
        return ret;
    }

    PVFS_object_ref_copy(parent_ref, &resp_look.ref);
    return 0;
}

/* Certain functions outside of the security code use OpenSSL
 * (e.g. src/common/misc/digest.c)
 */
#ifdef HAVE_OPENSSL

/* PINT_client_security_initialize
 *
 * Initializes OpenSSL libraries used by security code.
 *
 * returns 0 on success
 * returns negative PVFS error on failure
 */
int PINT_client_security_initialize(void)
{
    int ret;

    gen_mutex_lock(&security_init_mutex);
    if (security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    ret = setup_threading();
    if (ret < 0)
    {
        gen_mutex_unlock(&security_init_mutex);
        return ret;
    }

    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    security_init_status = 1;
    gen_mutex_unlock(&security_init_mutex);

    return 0;
}

/* PINT_client_security_finalize
 *
 * Cleans up after the OpenSSL library.
 *
 * returns 0 on success
 * returns negative PVFS error on failure
 */
int PINT_client_security_finalize(void)
{
    gen_mutex_lock(&security_init_mutex);
    if (!security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    EVP_cleanup();
    ERR_free_strings();

    cleanup_threading();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);

    return 0;
}

/* setup_threading
 * 
 * Sets up the data structures and callbacks required by the OpenSSL library
 * for multithreaded operation.
 *
 * returns -PVFS_ENOMEM if a mutex could not be initialized
 * returns 0 on success
 */
static int setup_threading(void)
{
    int i;

    openssl_mutexes = calloc(CRYPTO_num_locks(), sizeof(gen_mutex_t));
    if (!openssl_mutexes)
    {
        return -PVFS_ENOMEM;
    }

    for (i = 0; i < CRYPTO_num_locks(); i++)
    {
        if (gen_mutex_init(&openssl_mutexes[i]) != 0)
        {
            for (i = i - 1; i >= 0; i--)
            {
                gen_mutex_destroy(&openssl_mutexes[i]);
            }
            free(openssl_mutexes);
            openssl_mutexes = NULL;
            return -PVFS_ENOMEM;
        }
    }

#ifndef CRYPTO_THREADID_set_callback
    CRYPTO_SET_ID_CALLBACK(ID_FUNCTION);
#endif
    CRYPTO_set_locking_callback(locking_function);
    CRYPTO_set_dynlock_create_callback(dyn_create_function);
    CRYPTO_set_dynlock_lock_callback(dyn_lock_function);
    CRYPTO_set_dynlock_destroy_callback(dyn_destroy_function);

    return 0;
}
/* cleanup_threading
 *
 * Frees the data structures used to provide multithreading support to
 * the OpenSSL library.
 */
static void cleanup_threading(void)
{
    int i;

#ifndef CRYPTO_THREADID_set_callback
    CRYPTO_SET_ID_CALLBACK(NULL);
#endif
    CRYPTO_set_locking_callback(NULL);
    CRYPTO_set_dynlock_create_callback(NULL);
    CRYPTO_set_dynlock_lock_callback(NULL);
    CRYPTO_set_dynlock_destroy_callback(NULL);
    
    if (openssl_mutexes)
    {
        for (i = 0; i < CRYPTO_num_locks(); i++)
        {
            gen_mutex_destroy(&openssl_mutexes[i]);
        }
    }

    free(openssl_mutexes);
    openssl_mutexes = NULL;
}


/* If PVFS_OPENSSL_USE_THREADID is defined, then OpenSSL is at major version #1
 * and CRYPTO_THREADID_set_callback is NOT defined.
 */
#ifdef PVFS_OPENSSL_USE_THREADID
    /* threadid_function
     * 
     * The OpenSSL thread id callback for OpenSSL v1.0.0. 
    */
   static void threadid_function(CRYPTO_THREADID *id)
   {
   /* NOTE: PVFS_OPENSSL_USE_THREAD_PTR is not currently defined in 
      the source. If you wish to use a thread pointer, you must implement 
      a gen_thread_self (etc.) that uses thread pointers. Then define 
      this macro in this file or through the configure script. 
    */
#   ifdef PVFS_OPENSSL_USE_THREAD_PTR
          CRYPTO_THREADID_set_pointer(id, gen_thread_self());
#   else
          CRYPTO_THREADID_set_numeric(id, (unsigned long) gen_thread_self());
#   endif
   }
#else 
#   if !(OPENSSL_VERSION_NUMBER & 0x10000000L)
    /* id_function
     *
     * The OpenSSL thread id callback for OpenSSL v0.9.8.
     */
    static unsigned long id_function(void)
    {
        return (unsigned long) gen_thread_self();
    }
#   endif
#endif

#ifndef CRYPTO_set_locking_callback
/* locking_function
 *
 * The OpenSSL locking_function callback.
 */
static void locking_function(int mode, int n, const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        gen_mutex_lock(&openssl_mutexes[n]);
    }
    else
    {
        gen_mutex_unlock(&openssl_mutexes[n]);
    }
}
#endif

#ifndef CRYPTO_set_dynlock_create_callback
/* dyn_create_function
 *
 * The OpenSSL dyn_create_function callback.
 */
static struct CRYPTO_dynlock_value *dyn_create_function(const char *file, 
                                                        int line)
{
    struct CRYPTO_dynlock_value *ret;

    ret = malloc(sizeof(struct CRYPTO_dynlock_value));
    if (ret)
    {
        gen_mutex_init(&ret->mutex);
    }

    return ret;
}
#endif

#ifndef CRYPTO_set_dynlock_lock_callback
/* dyn_lock_function
 *
 * The OpenSSL dyn_lock_function callback.
 */
static void dyn_lock_function(int mode, struct CRYPTO_dynlock_value *l,
                              const char *file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        gen_mutex_lock(&l->mutex);
    }
    else
    {
        gen_mutex_unlock(&l->mutex);
    }
}
#endif


#ifndef CRYPTO_set_dynlock_destroy_callback
/* dyn_destroy_function
 *
 * The OpenSSL dyn_destroy_function callback.
 */
static void dyn_destroy_function(struct CRYPTO_dynlock_value *l,
                                 const char *file, int line)
{
    gen_mutex_destroy(&l->mutex);
    free(l);
}
#endif

#else /* !HAVE_OPENSSL */

int PINT_client_security_initialize(void)
{
    return 0;
}

int PINT_client_security_finalize(void)
{
    return 0;
}

#endif /* HAVE_OPENSSL */
 
/* client only routine to start a timer for perf counters */
/* should add an smcb pointer to the perf_counter and put
 * its timer there - makes shutting down easier
 */
int client_perf_start_rollover(struct PINT_perf_counter *pc,
                               struct PINT_perf_counter *tpc)
{
    int ret = 0;
    PINT_client_sm *sm_p;
    

    PINT_smcb_alloc(&(pc->smcb),
                    PVFS_CLIENT_PERF_COUNT_TIMER,
                    sizeof(struct PINT_client_sm),
                    client_op_state_get_machine,
                    client_state_machine_terminate,
                    pint_client_sm_context);

    if (!pc->smcb)
    {
        return(-PVFS_ENOMEM);
    }

    PINT_state_machine_locate(pc->smcb, 1);

    sm_p = PINT_sm_frame(pc->smcb, PINT_FRAME_CURRENT);
    sm_p->u.perf_count_timer.pc = pc;
    sm_p->u.perf_count_timer.tpc = tpc;
    pc->running = 1;

    ret = PINT_client_state_machine_post(pc->smcb, NULL, NULL);
    if (ret < 0)
    {
        gossip_lerr("Error posting cache timer.\n");
        return(ret);
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
