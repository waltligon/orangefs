/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <sys/types.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>

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
#include "security-types.h"


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t *openssl_mutexes = NULL;
static int security_init_status = 0;


struct CRYPTO_dynlock_value
{
    gen_mutex_t mutex;
};

static int setup_threading(void);
static void cleanup_threading(void);
static unsigned long id_function(void);
static void locking_function(int, int, const char*, int);
static struct CRYPTO_dynlock_value *dyn_create_function(const char*, int);
static void dyn_lock_function(int, struct CRYPTO_dynlock_value*, const char*,
                              int);
static void dyn_destroy_function(struct CRYPTO_dynlock_value*, const char*,
                                 int);


/*
  analogous to 'get_server_config_struct' in pvfs2-server.c -- only an
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
int PINT_lookup_parent(
    char *filename,
    PVFS_fs_id fs_id,
    PVFS_credential *credential,
    PVFS_handle * handle)
{
    int ret = -PVFS_EINVAL;
    char buf[PVFS_SEGMENT_MAX] = {0};
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look, 0, sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename, buf, PVFS_SEGMENT_MAX))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n", filename);
        *handle = PVFS_HANDLE_NULL;
        return ret;
    }

    ret = PVFS_sys_lookup(fs_id, buf, credential,
                          &resp_look, PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret < 0)
    {
        gossip_err("Lookup failed on %s\n", buf);
        *handle = PVFS_HANDLE_NULL;
        return ret;
    }

    *handle = resp_look.ref.handle;
    return 0;
}

/* nlmills: TODO: document me */
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

/* nlmills: TODO: document me */
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

    CRYPTO_set_id_callback(id_function);
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

    CRYPTO_set_id_callback(NULL);
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

/* id_function
 *
 * The OpenSSL id_function callback.
 */
static unsigned long id_function(void)
{
    /* nlmills: TODO: find a more portable way to do this */
    return (unsigned long)gen_thread_self();
}

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
   

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
