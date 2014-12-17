/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <regex.h>
#include <errno.h>

#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

/* leave pvfs2-config.h first */
#include "pvfs2-config.h"

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
#include "security-util.h"

#ifdef ENABLE_SECURITY_CERT
#include "pint-cert.h"
#include "cert-util.h"
#include "pint-ldap-map.h"
#ifdef ENABLE_CERTCACHE
#include "certcache.h"
#endif
#endif

static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t *openssl_mutexes = NULL;
static int security_init_status = 0;

#ifdef ENABLE_SECURITY_CERT
/* CA public key - used for encryption */
EVP_PKEY *security_pubkey;

/* CA cert */
X509 *ca_cert = NULL;
#endif
/* private key used for signing */
EVP_PKEY *security_privkey = NULL;


struct CRYPTO_dynlock_value
{
    gen_mutex_t mutex;
};


/* thread-safe OpenSSL helper functions */
static int setup_threading(void);
static void cleanup_threading(void);
/* OpenSSL 1.0 allows thread id to be either long or a pointer */
#if OPENSSL_VERSION_NUMBER & 0x10000000
#define PVFS_OPENSSL_USE_THREADID
#define CRYPTO_SET_ID_CALLBACK    CRYPTO_THREADID_set_callback
#define ID_FUNCTION               threadid_function
static void threadid_function(CRYPTO_THREADID *);
#else
#define CRYPTO_SET_ID_CALLBACK    CRYPTO_set_id_callback
#define ID_FUNCTION               id_function
static unsigned long id_function(void);
#endif

static void locking_function(int, int, const char*, int);
static struct CRYPTO_dynlock_value *dyn_create_function(const char*, int);
static void dyn_lock_function(int, struct CRYPTO_dynlock_value*, const char*,
                              int);
static void dyn_destroy_function(struct CRYPTO_dynlock_value*, const char*,
                                 int);

#ifdef ENABLE_SECURITY_KEY
static int load_private_key(const char*);
static int load_public_keys(const char*);
#endif

/*  PINT_security_initialize    
 *
 *  Initializes the security module
 *    
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_EIO if key file is missing or invalid
 *  returns 0 on success
 */
int PINT_security_initialize(void)
{
    const struct server_configuration_s *config = PINT_get_server_config();
    int ret;
#ifdef ENABLE_SECURITY_KEY
    PINT_llist_p host_aliases;
    host_alias_s *host_alias;
    char buf[HOST_NAME_MAX+2];
#endif

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

    ret = SECURITY_hash_initialize();
    if (ret < 0)
    {
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return ret;
    }    

    /* check for ServerKey */
    PINT_SECURITY_CHECK_NULL(config->serverkey_path, init_error,
                             "ServerKey not defined in configuration file... "
                             "aborting\n");

#ifdef ENABLE_SECURITY_KEY

    PINT_SECURITY_CHECK_NULL(config->keystore_path, init_error,
                             "Keystore not defined in configuration file... "
                             "aborting\n");

    security_privkey = EVP_PKEY_new();
    ret = load_private_key(config->serverkey_path);
    if (ret < 0)
    {
        SECURITY_hash_finalize();
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EIO;
    }

    ret = load_public_keys(config->keystore_path);
    if (ret < 0)
    {
        EVP_PKEY_free(security_privkey);
        SECURITY_hash_finalize();
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EIO;
    }

    host_aliases = config->host_aliases;
    while (host_aliases) {
        host_alias = PINT_llist_head(host_aliases);
        if (host_alias)
        {
            snprintf(buf, HOST_NAME_MAX+2, "S:%s", host_alias->host_alias);
            if (SECURITY_lookup_pubkey(buf) == NULL)
            {
                gossip_err("Could not find public key for alias '%s'\n", buf);
                SECURITY_hash_finalize();
                EVP_cleanup();
                ERR_free_strings();
                cleanup_threading();
                gen_mutex_unlock(&security_init_mutex);
                return -PVFS_ENXIO;
            }
        }
        host_aliases = PINT_llist_next(host_aliases);
    }

#elif ENABLE_SECURITY_CERT

    /* load the CA cert */
    ret = PINT_init_trust_store();
    PINT_SECURITY_CHECK(ret, init_error, "could not initialize trust store\n");

    PINT_SECURITY_CHECK_NULL(config->ca_file, init_error,
                             "CAFile not defined in configuration file... "
                             "aborting\n");

    ret = PINT_load_cert_from_file(config->ca_file, &ca_cert);
    PINT_SECURITY_CHECK(ret, init_error, "could not open cert file %s:\n", 
                        config->ca_file);
    
    ret = PINT_add_trusted_certificate(ca_cert);
    PINT_SECURITY_CHECK(ret, init_error, 
                        "could not add CA cert to trust store\n");

    /* load private key */
    ret = PINT_load_key_from_file(config->serverkey_path, &security_privkey);
    PINT_SECURITY_CHECK(ret, init_error, "could not load private key file %s\n",
                        config->serverkey_path);

    /* get public key */
    security_pubkey = X509_get_pubkey(ca_cert);
    PINT_SECURITY_CHECK_NULL(security_pubkey, init_error, "could not load "
                             "CA cert public key\n");

    /* initialize LDAP */
    ret = PINT_ldap_initialize();
    PINT_SECURITY_CHECK(ret, init_error, "could not initialize LDAP\n");

#endif /* ENABLE_SECURITY_CERT */

    goto init_exit;

init_error:

#ifdef ENABLE_SECURITY_CERT
    PINT_cleanup_trust_store();

    PINT_ldap_finalize();
#endif

    if (security_privkey)
    {
        EVP_PKEY_free(security_privkey);
    }

    SECURITY_hash_finalize();
    EVP_cleanup();
    ERR_free_strings();
    cleanup_threading();
    gen_mutex_unlock(&security_init_mutex);

    return -PVFS_ESECURITY;

init_exit:

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

    SECURITY_hash_finalize();

    EVP_PKEY_free(security_privkey);
    EVP_cleanup();
    ERR_free_strings();


#ifdef ENABLE_SECURITY_CERT
    PINT_cleanup_trust_store();

    PINT_ldap_finalize();
#endif

    cleanup_threading();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);
    
    return 0;
}

#ifdef ENABLE_CERTCACHE
/* PINT_security_cache_ca_cert
 * 
 * Caches CA cert--corresponds to root user.
 * Primarily used when creating a new file system. 
 */
int PINT_security_cache_ca_cert(void)
{
    PVFS_certificate *pcert;
    PVFS_gid group_array[] = {0};
    int ret;

    if (ca_cert == NULL)
    {
        return -PVFS_ESECURITY;
    }

    /* convert X509 CA cert to internal format */
    if (PINT_X509_to_cert(ca_cert, &pcert) != 0)
    {        
        return -PVFS_ESECURITY;
    }

    /* insert cert into cache as root user */
    ret = PINT_certcache_insert(pcert, 0, 1, group_array);

    PINT_cleanup_cert(pcert);
    free(pcert);

    return ret;
}
#endif /* ENABLE_CERTCACHE */

/*  PINT_init_capability
 *
 *  Function to call after creating an initial capability
 *  structure to initialize needed memory space for the signature.
 *  Sets all fields to 0 or NULL to be safe
 *    
 *  returns -PVFS_ENOMEM on error
 *  returns -PVFS_EINVAL if passed an invalid structure
 *  returns 0 on success
 */
int PINT_init_capability(PVFS_capability *cap)
{
    int ret = 0;
    
    if (cap)
    {
        memset(cap, 0, sizeof(PVFS_capability));

        cap->signature = calloc(1, EVP_PKEY_size(security_privkey));
        if (cap->signature == NULL)
        {
            ret = -PVFS_ENOMEM;
        }
    }
    else
    {
        ret = -PVFS_EINVAL;
    }

    return ret;
}

#if 0
/* nlmills: temporary function to help gather statistics */
static void hash_capability(const PVFS_capability *cap, char *mdstr)
{
    EVP_MD_CTX mdctx;
    unsigned char md[SHA_DIGEST_LENGTH];
    int i;

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(&mdctx, cap->issuer, strlen(cap->issuer));
    EVP_DigestUpdate(&mdctx, &cap->fsid, sizeof(cap->fsid));
    EVP_DigestUpdate(&mdctx, &cap->sig_size, sizeof(cap->sig_size));
    if (cap->sig_size)
    {
        EVP_DigestUpdate(&mdctx, cap->signature, cap->sig_size);
    }
    EVP_DigestUpdate(&mdctx, &cap->timeout, sizeof(cap->timeout));
    EVP_DigestUpdate(&mdctx, &cap->op_mask, sizeof(cap->op_mask));
    EVP_DigestUpdate(&mdctx, &cap->num_handles, sizeof(cap->num_handles));
    if (cap->num_handles)
    {
        EVP_DigestUpdate(&mdctx, cap->handle_array,
                         cap->num_handles*sizeof(*cap->handle_array));
    }
    EVP_DigestFinal_ex(&mdctx, md, NULL);
    EVP_MD_CTX_cleanup(&mdctx);

    memset(mdstr, 0, 2*SHA_DIGEST_LENGTH+1);
    for (i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        sprintf(mdstr+2*i, "%02x", (unsigned int)md[i]);
    }
}
#endif

/*  PINT_sign_capability
 *
 *  Digitally signs the capability with this server's private key.
 *
 *  returns 0 on success
 *  returns negative on error
 */
int PINT_sign_capability(PVFS_capability *cap)
{
    const struct server_configuration_s *config;
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
#if 0
    char mdstr[2*SHA_DIGEST_LENGTH+1];
#endif
    int ret;

    if (cap == NULL || cap->issuer == NULL || cap->signature == NULL ||
        (cap->num_handles != 0 && cap->handle_array == NULL))
    {
        /* log parameter error */
        PINT_security_error(__func__, -1);

        return -1;
    }

    config = PINT_get_server_config();

    /* cap->issuer is set in get-attr.sm in the server. */

    cap->timeout = PINT_util_get_current_time() + config->capability_timeout;

    if (EVP_PKEY_type(security_privkey->type) == EVP_PKEY_RSA)
    {
        md = EVP_sha1();
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Unsupported key type %u\n",
                     security_privkey->type);
        return -1;
    }

    EVP_MD_CTX_init(&mdctx);

    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignUpdate(&mdctx, 
                         cap->issuer, 
                         strlen(cap->issuer) * sizeof(char));
    ret &= EVP_SignUpdate(&mdctx, &cap->fsid, sizeof(PVFS_fs_id));
    ret &= EVP_SignUpdate(&mdctx, &cap->timeout, sizeof(PVFS_time));
    ret &= EVP_SignUpdate(&mdctx, &cap->op_mask, sizeof(uint32_t));
    ret &= EVP_SignUpdate(&mdctx, &cap->num_handles, sizeof(uint32_t));
    if (cap->num_handles)
    {
        ret &= EVP_SignUpdate(&mdctx, 
                              cap->handle_array, 
                              cap->num_handles * sizeof(PVFS_handle));
    }

    if (!ret)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);

        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignFinal(&mdctx, 
                        cap->signature, &cap->sig_size, 
                        security_privkey);
    if (!ret)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);

        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    EVP_MD_CTX_cleanup(&mdctx);

#if 0
    hash_capability(cap, mdstr);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "CAPSIGN: %s\n", mdstr);
#endif

    return 0;
}

int PINT_server_to_server_capability(PVFS_capability *capability,
                                     PVFS_fs_id fs_id,
                                     int num_handles,
                                     PVFS_handle *handle_array)
{
    int ret = -PVFS_EINVAL;
    server_configuration_s *config = PINT_get_server_config();

    ret = PINT_init_capability(capability);
    if (ret < 0)
    {
        return -PVFS_ENOMEM;
    }

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Generating server-to-server "
                 "capability...\n");
    capability->issuer = (char *) malloc(strlen(config->server_alias) + 3);
    if (capability->issuer == NULL)
    {
        PINT_cleanup_capability(capability);
        return -PVFS_ENOMEM;
    }
    strcpy(capability->issuer, "S:");
    strcat(capability->issuer, config->server_alias);

    capability->fsid = fs_id;
    capability->timeout =
        PINT_util_get_current_time() + config->capability_timeout;
    capability->op_mask = ~((uint32_t)0);
    capability->num_handles = num_handles;
    capability->handle_array = handle_array;

    ret = PINT_sign_capability(capability);
    if (ret < 0)
    {
        PINT_cleanup_capability(capability);
        return -PVFS_EINVAL;
    }
    return 0;
}

/*  PINT_verify_capability
 *
 *  Takes in a PVFS_capability structure and checks to see if the
 *  signature matches the contents based on the data within. Verification
 *  always succeeds for the null capability.
 *
 *  returns 1 on success
 *  returns 0 on error or failure to verify
 */
int PINT_verify_capability(const PVFS_capability *cap)
{
#if 0
    char mdstr[2*SHA_DIGEST_LENGTH+1];
#endif
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
    EVP_PKEY *pubkey;
    int ret;
    
    if (cap == NULL || cap->issuer == NULL || cap->signature == NULL ||
        (cap->num_handles != 0 && cap->handle_array == NULL))
    {
        /* log parameter error */
        PINT_security_error(__func__, -1);

        return 0;
    }

    if (PINT_capability_is_null(cap))
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Verified null capability\n");
        return 1;
    }

    PINT_debug_capability(cap, "Verifying");

    /* if capability has timed out */
    if (PINT_util_get_current_time() > cap->timeout)
    {
        char buf[16];

        gossip_debug(GOSSIP_SECURITY_DEBUG, "Capability (%s) expired (timeout "
                     "%llu)\n", PINT_util_bytes2str(cap->signature, buf, 4),
                     llu(cap->timeout));
        return 0;
    }

#if 0
    hash_capability(cap, mdstr);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "CAPVRFY: %s\n", mdstr);
#endif

#ifdef ENABLE_SECURITY_CERT
    /* get CA certificate public key */
    pubkey = X509_get_pubkey(ca_cert);
    if (pubkey == NULL)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);
        return 0;
    }
#else
    pubkey = SECURITY_lookup_pubkey(cap->issuer);
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found in lookup. Name used: '%s'\n", 
                     cap->issuer);
        return 0;
    }
#endif
    if (EVP_PKEY_type(pubkey->type) == EVP_PKEY_RSA)
    {
        md = EVP_sha1();
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Unsupported key type %u\n",
                     pubkey->type);
        return 0;
    }

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    ret &= EVP_VerifyUpdate(&mdctx, 
                            cap->issuer,
                            strlen(cap->issuer) * sizeof(char));
    ret &= EVP_VerifyUpdate(&mdctx, &cap->fsid, sizeof(PVFS_fs_id));
    ret &= EVP_VerifyUpdate(&mdctx, &cap->timeout, sizeof(PVFS_time));
    ret &= EVP_VerifyUpdate(&mdctx, &cap->op_mask, sizeof(uint32_t));
    ret &= EVP_VerifyUpdate(&mdctx, &cap->num_handles,
                            sizeof(uint32_t));
    if (cap->num_handles)
    {
        ret &= EVP_VerifyUpdate(&mdctx, 
                                cap->handle_array,
                                cap->num_handles * sizeof(PVFS_handle));
    }
    if (ret)
    {
        ret = EVP_VerifyFinal(&mdctx, cap->signature, cap->sig_size, 
                              pubkey);
    }

    if (ret != 1)
    {
        PINT_security_error("Capability verify", -PVFS_ESECURITY);
    }
    
    EVP_MD_CTX_cleanup(&mdctx);

    return (ret == 1);
}

/* PINT_init_credential
 *
 * Initializes a credential and allocates memory for its internal members.
 *
 * returns negative on error.
 * returns zero on success.
 */
int PINT_init_credential(PVFS_credential *cred)
{
    int ret = 0;

    if (cred)
    {
        memset(cred, 0, sizeof(PVFS_credential));

        cred->signature = calloc(1, EVP_PKEY_size(security_privkey));
        if (!cred->signature)
        {
            ret = -PVFS_ENOMEM;
        }
    }
    else
    {
        ret = -PVFS_EINVAL;
    }

    return ret;
}

#if 0
/* nlmills: temporary function to help gather statistics */
static void hash_credential(const PVFS_credential *cred, char *mdstr)
{
    EVP_MD_CTX mdctx;
    unsigned char md[SHA_DIGEST_LENGTH];
    int i;

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(&mdctx, &cred->userid, sizeof(cred->userid));
    EVP_DigestUpdate(&mdctx, &cred->num_groups, sizeof(cred->num_groups));
    if (cred->num_groups)
    {
        EVP_DigestUpdate(&mdctx, cred->group_array,
                         cred->num_groups*sizeof(*cred->group_array));
    }
    EVP_DigestUpdate(&mdctx, cred->issuer, strlen(cred->issuer));
    EVP_DigestUpdate(&mdctx, &cred->timeout, sizeof(cred->timeout));
    EVP_DigestUpdate(&mdctx, &cred->sig_size, sizeof(cred->sig_size));
    if (cred->sig_size)
    {
        EVP_DigestUpdate(&mdctx, cred->signature, cred->sig_size);
    }    
    EVP_DigestFinal_ex(&mdctx, md, NULL);
    EVP_MD_CTX_cleanup(&mdctx);

    memset(mdstr, 0, 2*SHA_DIGEST_LENGTH+1);
    for (i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        sprintf(mdstr+2*i, "%02x", (unsigned int)md[i]);
    }
}
#endif

/* PINT_sign_credential
 *
 * Digitally signs a credential with the server private key.
 *
 * returns -1 on error.
 * returns 0 on success.
 */
int PINT_sign_credential(PVFS_credential *cred)
{
    const struct server_configuration_s *config;
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
#if 0
    char mdstr[2*SHA_DIGEST_LENGTH+1];
#endif
    int ret;

    if (cred == NULL || (cred->num_groups != 0 && cred->group_array == NULL))
    {
        /* log parameter error */
        PINT_security_error(__func__, -1);

        return -1;
    }

    config = PINT_get_server_config();
    
    cred->issuer = (char *) malloc(strlen(config->server_alias) + 3);
    if (cred->issuer == NULL)
    {
        return -PVFS_ENOMEM;
    }
    strcpy(cred->issuer, "S:");
    strcat(cred->issuer, config->server_alias);
    
    cred->timeout = PINT_util_get_current_time() + config->credential_timeout;

    /* If squashing is enabled, server will re-sign a credential with 
       translated uid/gid. In this case the signature must be reallocated. 
       -- see prelude_validate */
    if (cred->signature == NULL)
    {
        cred->signature = calloc(1, EVP_PKEY_size(security_privkey));
        if (cred->signature == NULL)
        {
            free(cred->issuer);
            return -1;
        }
    }

    if (EVP_PKEY_type(security_privkey->type) == EVP_PKEY_RSA)
    {
        md = EVP_sha1();
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Unsupported key type %u\n",
                     security_privkey->type);
        return -1;
    }
    
    EVP_MD_CTX_init(&mdctx);
    
    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    ret &= EVP_SignUpdate(&mdctx, &cred->userid, sizeof(PVFS_uid));
    ret &= EVP_SignUpdate(&mdctx, &cred->num_groups, sizeof(uint32_t));
    if (cred->num_groups)
    {
        ret &= EVP_SignUpdate(&mdctx, cred->group_array, 
                              cred->num_groups * sizeof(PVFS_gid));
    }
    if (cred->issuer)
    {
        ret &= EVP_SignUpdate(&mdctx, cred->issuer, 
                              strlen(cred->issuer) * sizeof(char));
    }
    ret &= EVP_SignUpdate(&mdctx, &cred->timeout, sizeof(PVFS_time));
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "SignUpdate failure\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }
    
    ret = EVP_SignFinal(&mdctx, cred->signature, &cred->sig_size,
                        security_privkey);
    EVP_MD_CTX_cleanup(&mdctx);
    if (!ret)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);
        return -1;
    }

#if 0
    hash_credential(cred, mdstr);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "CREDSIGN: %s\n", mdstr);
#endif
    
    return 0;
}

/*  PINT_verify_credential
 *
 *  Takes in a PVFS_credential structure and checks to see if the
 *  signature matches the contents based on the data within
 *
 *  returns 1 on success
 *  returns 0 on error or failure to verify
 */
int PINT_verify_credential(const PVFS_credential *cred)
{
#if 0
    char mdstr[2*SHA_DIGEST_LENGTH+1];
#endif
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
    EVP_PKEY *pubkey;
    char sigbuf[16];
    int ret;
#ifdef ENABLE_SECURITY_CERT
    X509 *cert;
    int certcache_hit;
#endif

    if (cred == NULL || (cred->sig_size != 0 && cred->signature == NULL) ||
        (cred->num_groups != 0 && cred->group_array == NULL))
    {
        /* log parameter error */
        PINT_security_error(__func__, -1);

        return 0;
    }

#ifdef ENABLE_SECURITY_CERT
    /* check if this is an unsigned credential... in this case 
       it will be verified, but it does not provide any rights other 
       than basic ops like statfs */
    if (IS_UNSIGNED_CRED(cred))
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Unsigned credential from %s "
                     "received\n", cred->issuer);
        return 1;
    }
#endif

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Verifying credential: %s\n",
                 PINT_util_bytes2str(cred->signature, sigbuf, 4));

    if (PINT_util_get_current_time() > cred->timeout)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Credential (%s) expired "
                     "(timeout %llu)\n", 
                     PINT_util_bytes2str(cred->signature, sigbuf, 4),
                     llu(cred->timeout));
        return 0;
    }

#if 0
    hash_credential(cred, mdstr);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "CREDVRFY: %s\n", mdstr);
#endif

#ifdef ENABLE_SECURITY_CERT
    /* get X509 cert from certificate buffer */
    ret = PINT_cert_to_X509(&cred->certificate, &cert);
    if (ret != 0)
    {
        PINT_security_error(__func__, ret);
        return 0;
    }

#ifdef ENABLE_CERTCACHE
    /* check cert cache for cert */
    certcache_hit = 
        (PINT_certcache_lookup(
            (PVFS_certificate *) &cred->certificate) != NULL);
#else
    certcache_hit = 0;
#endif

    if (!certcache_hit)
    {
        /* verify the certificate (using the trust store)
         * note: we don't cache a verified cert at this stage 
         */
        ret = PINT_verify_certificate(cert);
        if (ret != 0)
        {
            /* Note: errors already logged */
            X509_free(cert);
            return 0;
        }
    }
    /* get certificate public key */
    pubkey = X509_get_pubkey(cert);
    if (pubkey == NULL)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);
        X509_free(cert);
        return 0;
    }

#else /* !ENABLE_SECURITY_CERT */
    pubkey = SECURITY_lookup_pubkey(cred->issuer);
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found for issuer: %s\n", 
                     cred->issuer);
        return 0;
    }
#endif /* ENABLE_SECURITY_CERT */

    if (EVP_PKEY_type(pubkey->type) == EVP_PKEY_RSA)
    {
        md = EVP_sha1();
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Unsupported key type %u\n",
                     pubkey->type);
        return 0;
    }

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    ret &= EVP_VerifyUpdate(&mdctx, &cred->userid, sizeof(PVFS_uid));
    ret &= EVP_VerifyUpdate(&mdctx, &cred->num_groups, sizeof(uint32_t));
    if (cred->num_groups)
    {
        ret &= EVP_VerifyUpdate(&mdctx, cred->group_array,
                                cred->num_groups * sizeof(PVFS_gid));
    }
    if (cred->issuer)
    {
        ret &= EVP_VerifyUpdate(&mdctx, cred->issuer,
                                strlen(cred->issuer) * sizeof(char));
    }
    ret &= EVP_VerifyUpdate(&mdctx, &cred->timeout, sizeof(PVFS_time));
    if (ret)
    {
        ret = EVP_VerifyFinal(&mdctx, cred->signature, cred->sig_size, pubkey);
    }

    if (ret != 1)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);
    }

    EVP_MD_CTX_cleanup(&mdctx);

#ifdef ENABLE_SECURITY_CERT
    EVP_PKEY_free(pubkey);
    X509_free(cert);
#endif

    return (ret == 1);
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

    CRYPTO_SET_ID_CALLBACK(ID_FUNCTION);
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

    CRYPTO_SET_ID_CALLBACK(NULL);
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
#ifdef PVFS_OPENSSL_USE_THREAD_PTR
    CRYPTO_THREADID_set_pointer(id, gen_thread_self());
#else
    CRYPTO_THREADID_set_numeric(id, (unsigned long) gen_thread_self());
#endif
}
#else
/* id_function
 *
 * The OpenSSL thread id callback for OpenSSL v0.9.8.
 */
static unsigned long id_function(void)
{
    return (unsigned long) gen_thread_self();
}
#endif /* PVFS_OPENSSL_USE_THREADID */

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

#ifdef ENABLE_SECURITY_KEY
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
        gossip_err("Error loading private key: %s: %s\n", path, 
                   strerror(errno));
        return -1;
    }

    EVP_PKEY_free(security_privkey);
    security_privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);
    if (security_privkey == NULL)
    {
        ERR_error_string_n(ERR_get_error(), buf, 256);
        gossip_err("Error loading private key: %s: %s\n", path, buf);
        fclose(keyfile);
        return -1;
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
 *  Uses static storage.
 *    
 *  returns -1 on error
 *  returns 0 on success
 */
static int load_public_keys(const char *path)
{
    FILE *keyfile;
    int ch, ptr;
    static char buf[4096];
    EVP_PKEY *key;
    int ret;

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        gossip_err("Error loading keystore: %s: %s\n", path, 
                   strerror(errno));
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

        for (ptr = 0; (ptr < 4095) && !isspace(ch); ptr++)
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

        if (buf[1] != ':' || (buf[0] != 'C' && buf[0] != 'S')) {
            gossip_err("Error loading keystore: Issuer must start with "
                       "'C:' or 'C:' but is '%s'\n", buf);
            fclose(keyfile);
            return -1;
        }

        do
        {
            ch = fgetc(keyfile);
        } while(isspace(ch));

        ungetc(ch, keyfile);

        key = PEM_read_PUBKEY(keyfile, NULL, NULL, NULL);
        if (key == NULL)
        {
            ERR_error_string_n(ERR_get_error(), buf, 4096);
            gossip_err("Error loading public key: %s %s\n", path, buf);
            fclose(keyfile);
            return -1;
        }

        ret = SECURITY_add_pubkey(buf, key);
        if (ret < 0)
        {
            PVFS_strerror_r(ret, buf, 4096);
            gossip_err("Error inserting public key: %s\n", buf);
            fclose(keyfile);
            return -1;
        }
        gossip_debug(GOSSIP_SECURITY_DEBUG, 
                     "Added public key for '%s'\n", 
                     buf);
        
        ch = fgetc(keyfile);
    }

    fclose(keyfile);

    return 0;
}
#endif

/* PINT_security_error 
 * Log security errors to gossip, (usually OpenSSL errors)
 */
void PINT_security_error(const char *prefix, int err)
{
    unsigned long sslerr;
    char errstr[256];

    switch (err) {    
    case 0:
        break;
    case -1:
        /* usually a parameter error */
        gossip_err("%s: parameter error\n", prefix);
        break;
    case -PVFS_ESECURITY:
        /* debug OpenSSL error queue */
        while ((sslerr = ERR_get_error()) != 0)
        {
            ERR_error_string_n(sslerr, errstr, 256);
            errstr[255] = '\0';
            gossip_err("%s: OpenSSL error: %s\n", 
                         prefix, errstr);
        }
        break;
    default:
        /* debug PVFS/errno error */
        PVFS_strerror_r(err, errstr, 256);
        errstr[255] = '\0';
        gossip_err("%s: %s\n", prefix, errstr);
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
