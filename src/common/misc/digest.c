/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pvfs2-types.h"
#include "pvfs2-util.h"
#include "pint-util.h"

#ifdef WITH_OPENSSL


#ifdef HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
#endif
#ifdef HAVE_OPENSSL_CRYPTO_H
#include <openssl/crypto.h>
#endif
#include <errno.h>
#include "gen-locks.h"

#ifdef __GEN_POSIX_LOCKING__
#include <pthread.h>
#include <linux/unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>


static gen_mutex_t *mutex = NULL;
static pthread_once_t once_initialize = PTHREAD_ONCE_INIT;
static pthread_once_t once_finalize = PTHREAD_ONCE_INIT;

static void do_lock(int mode, int n, const char *file, int line)
{
    if (mode & CRYPTO_LOCK) {
        gen_mutex_lock(&mutex[n]);
    }
    else {
        gen_mutex_unlock(&mutex[n]);
    }
    return;
}

static unsigned long get_tid(void)
{
    /* If gettid syscall does not exist, fall back to getpid, which I
     * think will do something similar to gettid on non-ntpl
     * implementations */
#if defined(__NR_gettid)
    return syscall(__NR_gettid);
#else
    return getpid();
#endif
}

static void callback_init(void)
{
    int i, num_locks = 0;

    OpenSSL_add_all_digests();
    num_locks = CRYPTO_num_locks();
    mutex = (gen_mutex_t *) calloc(num_locks, sizeof(gen_mutex_t));
    for (i = 0; i < num_locks; i++) {
        gen_mutex_init(&mutex[i]);
    }
    CRYPTO_set_locking_callback(do_lock);
    CRYPTO_set_id_callback(get_tid);
    return;
}

void PINT_util_digest_init(void)
{
    pthread_once(&once_initialize, callback_init);
    return;
}

static void callback_finalize(void)
{
    free(mutex);
    mutex = NULL;
    EVP_cleanup();
}

void PINT_util_digest_finalize(void)
{
    pthread_once(&once_finalize, callback_finalize);
    return;
}

#else

void PINT_util_digest_init(void)
{
    OpenSSL_add_all_digests();
    return;
}

void PINT_util_digest_finalize(void)
{
    EVP_cleanup();
    return;
}

#endif

/* 
 * Given a digest name, a message buffer of a particular length,
 * compute the digest and returns the digested buffer in *digest_output
 * and the length of the digest buffer in *digest_len
 * Also returns errors in case there are any.
 */
static int digest(const char *digest_name, 
                  const void *buf, const size_t buf_len,
                  char **output, size_t *output_len)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    unsigned int digest_len;
    void *digest_value;

    if (!digest_name)
    {
        return -PVFS_EINVAL;
    }
    md = EVP_get_digestbyname(digest_name);
    if (!md)
    {
        return -PVFS_EINVAL;
    }

    digest_value = (void *) malloc(EVP_MAX_MD_SIZE);
    if (!digest_value)
    {
        return -PVFS_ENOMEM;
    }

    /* Instead of checking for different versions of openssl with
     * #ifdefds (nasty nasty ifdefs), we just skip calling 
     * EVP_MD_CTX_init/cleanup (0.9.7 has them, 0.9.6 doesn't), 
     * since in our case (md5 and sha1),
     * the digests don't define cleanup callbacks and init is just
     * a memset.
     */
    memset(&mdctx, 0, sizeof(mdctx));
    EVP_DigestInit(&mdctx, md);
    EVP_DigestUpdate(&mdctx, buf, buf_len);
    EVP_DigestFinal(&mdctx, digest_value, &digest_len);

    if (output)
        *output = digest_value;
    if (output_len)
        *output_len = digest_len;
    return 0;
}

#else /* WITH_OPENSSL */

void PINT_util_digest_init(void)
{
    return;
}

void PINT_util_digest_finalize(void)
{
    return;
}

/* 
 * returns -PVFS_EOPNOTSUPP
 */
static int digest(const char *digest_name, 
                  const void *buf, const size_t buf_len,
                  char **output, size_t *output_len)
{
    return -PVFS_EOPNOTSUPP;
}

#endif

int PINT_util_digest_sha1(const void *input_message, size_t input_length,
                          char **output, size_t *output_length)
{
    return digest("sha1", input_message, input_length,
                  output, output_length);
}

int PINT_util_digest_md5(const void *input_message, size_t input_length,
                         char **output, size_t *output_length)
{
    return digest("md5", input_message, input_length,
                  output, output_length);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
