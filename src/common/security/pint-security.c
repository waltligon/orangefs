/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "pvfs2.h"
#include "pvfs2-types.h"
#include "pint-eattr.h"
#include "pvfs2-req-proto.h"
#include "pvfs2-internal.h"
#include "gossip.h"
#include "gen-locks.h"
#include "server-config.h"
#include "pint-cached-config.h"
#include "pint-util.h"

#include "pint-security.h"
#include "security-hash.h"


/* TODO: move to global configuration */
#define SECURITY_DEFAULT_KEYSTORE "/tmp/keystore"
#define SECURITY_DEFAULT_PRIVKEYFILE  "/tmp/privkey.pem"
#define SECURITY_DEFAULT_TIMEOUT 0


/* the private key used for signing */
static EVP_PKEY *security_privkey = NULL;

static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static int security_init_status = 0;


static int load_private_key(const char*);
static int load_public_keys(const char*);


/*  PINT_security_initialize	
 *
 *  Initializes the security module
 *	
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_EIO if key file is missing or invalid
 *  returns 0 on sucess
 */
int PINT_security_initialize(void)
{
    int ret;

    gen_mutex_lock(&security_init_mutex);
    if (security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    ret = SECURITY_hash_initialize();
    if (ret < 0)
    {
        return ret;
    }

    ret = load_private_key(SECURITY_DEFAULT_PRIVKEYFILE);
    if (ret < 0)
    {
        return -PVFS_EIO;
    }
    
    /* TODO: better error handling */
    ret = load_public_keys(SECURITY_DEFAULT_KEYSTORE);
    if (ret < 0)
    {
        return -PVFS_EIO;
    }

    security_init_status = 1;
    gen_mutex_unlock(&security_init_mutex);
 
    return 0;
}

/*  PINT_security_finalize	
 *
 *  Finalizes the security module
 *	
 *  returns PVFS_EALREADY if already finalized
 *  returns 0 on sucess
 */
int PINT_security_finalize(void)
{
    gen_mutex_lock(&security_init_mutex);
    if (!security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    EVP_PKEY_free(security_privkey);

    EVP_cleanup();
    ERR_free_strings();
    SECURITY_hash_finalize();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);
    
    return 0;
}

/*  PINT_sign_capability
 *
 *  Takes in a PVFS_capability structure and creates a signature
 *  based on the input data
 *
 *  returns 0 on success
 *  returns -1 on error
 */
int PINT_sign_capability(PVFS_capability *cap)
{
    EVP_MD_CTX mdctx;
    unsigned siglen;
    char buf[256];
    int ret;

    assert(security_privkey);

    cap->timeout = PINT_util_get_current_time();
    cap->timeout += SECURITY_DEFAULT_TIMEOUT;

    EVP_MD_CTX_init(&mdctx);

    ret = EVP_SignInit_ex(&mdctx, EVP_sha1(), NULL);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignUpdate(&mdctx, &cap->owner, sizeof(PVFS_handle));
    ret &= EVP_SignUpdate(&mdctx, &cap->fsid, sizeof(PVFS_fs_id));
    ret &= EVP_SignUpdate(&mdctx, &cap->timeout, sizeof(PVFS_time));
    ret &= EVP_SignUpdate(&mdctx, &cap->op_mask, sizeof(uint32_t));
    ret &= EVP_SignUpdate(&mdctx, &cap->num_handles, sizeof(uint32_t));
    ret &= EVP_SignUpdate(&mdctx, cap->handle_array, cap->num_handles * 
                          sizeof(PVFS_handle));

    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignFinal(&mdctx, cap->signature, &siglen, security_privkey);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    EVP_MD_CTX_cleanup(&mdctx);

    return 0;
}

/*  PINT_verify_capability
 *
 *  Takes in a PVFS_capability structure and checks to see if the
 *  signature matches the contents based on the data within
 *
 *  returns 1 on success
 *  returns 0 on error or failure to verify
 */
int PINT_verify_capability(PVFS_capability *data)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    int ret;
    char *buf;
    EVP_PKEY *pubkey;

    if (PINT_util_get_current_time() > data->timeout)
    {
        return 0;
    }
    
    buf = (char *)malloc(sizeof(char) * 1024);
    
    if (buf == NULL)
    {
        return 0;
    }
    
    ret = PINT_cached_config_get_server_name(buf, 1024, data->owner,
                                             data->fsid);
    
    if (ret < 0)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Server name lookup failed.\n");
        free(buf);
        return 0;
    }
    
    pubkey = SECURITY_lookup_pubkey(buf);
        
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found in lookup. Name used: %s\n", buf);
        return 0;
    }
    free(buf);

    md = EVP_sha1();

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    if (ret)
    {
        ret = EVP_VerifyUpdate(&mdctx, &(data->owner), sizeof(PVFS_handle));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->fsid), sizeof(PVFS_fs_id));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->timeout), sizeof(PVFS_time));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->op_mask), sizeof(uint32_t));
        ret &= EVP_VerifyUpdate(&mdctx, &(data->num_handles),
                                sizeof(uint32_t));
        ret &= EVP_VerifyUpdate(&mdctx, data->handle_array,
                                sizeof(PVFS_handle) * data->num_handles);
        if (ret)
        {
            ret = EVP_VerifyFinal(&mdctx, data->signature, 128, pubkey);
        }
        else 
        {
            gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyUpdate failure.\n");
            EVP_MD_CTX_cleanup(&mdctx);
            return 0;
        }
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyInit failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }
    
    EVP_MD_CTX_cleanup(&mdctx);

    return 1;
}

/* load_private_key
 *
 * Reads the private key from a file in PEM format.
 *
 * returns -1 on error
 * returns 0 on success
 */
static int load_private_key(const char *path)
{
    FILE *keyfile;
    char buf[256];

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        gossip_err("%s: %s\n", path, strerror(errno));
        return -1;
    }

    security_privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);
    if (security_privkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading private key: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        fclose(keyfile);
    }

    fclose(keyfile);

    return 0;
}

/*  load_public_keys
 *
 *  Internal function to load keys from a file.
 *  File path includes the filename
 *  When finished without error, hash table will be filled
 *  with all host ID / public key pairs.
 *	
 *  returns -1 on error
 *  returns 0 on success
 */
static int load_public_keys(const char *path)
{
    FILE *keyfile;
    int ch, ptr;
    static char buf[1024];
    EVP_PKEY *key;
    char *host;
    int ret;

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        gossip_err("%s: %s\n", path, strerror(errno));
        return -1;
    }

    ch = fgetc(keyfile);

    while (ch != EOF)
    {
        while (isspace(ch))
        {
            ch = fgetc(keyfile);
        }

        if (ch == EOF)
        {
            break;
        }

        if (!isalnum(ch))
        {
            fclose(keyfile);
            return -1;
        }

        for (ptr = 0; (ptr < 1023) && isalnum(ch); ptr++)
        {
            buf[ptr] = (char)ch;
            ch = fgetc(keyfile);
            if (ch == EOF)
            {
                fclose(keyfile);
                return -1;
            }
        }
        buf[ptr] = '\0';

        do
        {
            ch = fgetc(keyfile);
        } while(isspace(ch));

        ungetc(ch, keyfile);

        key = PEM_read_PUBKEY(keyfile, NULL, NULL, NULL);
        if (key == NULL)
        {
            gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading public key: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
            fclose(keyfile);
            return -1;
        }

        host = PINT_config_get_host_addr_ptr(PINT_get_server_config(), buf);
        if (host == NULL)
        {
            gossip_debug(GOSSIP_SECURITY_DEBUG, "Alias '%s' not found "
                         "in configuration\n", buf);
        }
        else
        {
            ret = SECURITY_add_pubkey(host, key);
            if (ret < 0)
            {
                PVFS_strerror_r(ret, buf, 1024);
                gossip_debug(GOSSIP_SECURITY_DEBUG, "Error inserting public "
                             "key: %s\n", buf);
                fclose(keyfile);
                return -1;
            }
        }

        ch = fgetc(keyfile);
    }

    fclose(keyfile);

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
