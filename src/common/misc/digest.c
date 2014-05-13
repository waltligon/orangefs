/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>

#include "pvfs2-types.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <errno.h>

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

    if (digest_name == NULL)
    {
        return -PVFS_EINVAL;
    }
    md = EVP_get_digestbyname(digest_name);
    if (md == NULL)
    {
        return -PVFS_EINVAL;
    }
    digest_value = malloc(EVP_MAX_MD_SIZE);
    if (digest_value == NULL)
    {
        return -PVFS_ENOMEM;
    }

    EVP_MD_CTX_init(&mdctx);
    memset(&mdctx, 0, sizeof(mdctx));
    EVP_DigestInit(&mdctx, md);
    EVP_DigestUpdate(&mdctx, buf, buf_len);
    EVP_DigestFinal(&mdctx, digest_value, &digest_len);
    EVP_MD_CTX_cleanup(&mdctx);

    if (output)
        *output = digest_value;
    if (output_len)
        *output_len = digest_len;
    return 0;
}

int PINT_util_digest_sha1(const void *input_message, size_t input_length,
                          char **output, size_t *output_length)
{
    return digest("sha1", input_message, input_length,
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
