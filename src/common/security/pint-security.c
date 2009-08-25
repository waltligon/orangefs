/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
/* nlmills: TODO: fix for no security case */

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
#include <assert.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/stack.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

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
#include "getugroups.h"


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t *openssl_mutexes = NULL;
static int security_init_status = 0;

/* private key used for signing */
static EVP_PKEY *security_privkey = NULL;
/* store context used to verify client certificates */
static X509_STORE *security_store = NULL;


struct CRYPTO_dynlock_value
{
    gen_mutex_t mutex;
};

/* thread-safe OpenSSL helper functions */
static int setup_threading(void);
static void cleanup_threading(void);
static unsigned long id_function(void);
static void locking_function(int, int, const char*, int);
static struct CRYPTO_dynlock_value *dyn_create_function(const char*, int);
static void dyn_lock_function(int, struct CRYPTO_dynlock_value*, const char*,
                              int);
static void dyn_destroy_function(struct CRYPTO_dynlock_value*, const char*,
                                 int);

static int load_private_key(const char*);
static int load_public_keys(const char*);
static int load_ca_bundle(const char*);
static int verify_callback(int, X509_STORE_CTX*);
static const char *find_account(const char*, const STACK*);


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
    const struct server_configuration_s *config;
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

    ret = SECURITY_hash_initialize();
    if (ret < 0)
    {
        EVP_cleanup();
        ERR_free_strings();
        cleanup_threading();
        gen_mutex_unlock(&security_init_mutex);
        return ret;
    }

    config = PINT_get_server_config();
    assert(config->serverkey_path);
    assert(config->keystore_path);
    assert(config->cabundle_path);

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

    ret = load_ca_bundle(config->cabundle_path);
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
    X509_STORE_free(security_store);
    EVP_cleanup();
    ERR_free_strings();

    cleanup_threading();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);
    
    return 0;
}

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
    char buf[256];
    const EVP_MD *md = NULL;
    int ret;

    assert(security_privkey);

    config = PINT_get_server_config();
    assert(config->security_timeout);

    cap->issuer = strdup(config->server_alias);
    if (!cap->issuer)
    {
        return -PVFS_ENOMEM;
    }
    cap->timeout = PINT_util_get_current_time() + config->security_timeout;

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);

    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
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
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing capability: "
                         "%s\n", ERR_error_string(ERR_get_error(), buf));
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }

    ret = EVP_SignFinal(&mdctx, 
			cap->signature, &cap->sig_size, 
                        security_privkey);
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
 *  signature matches the contents based on the data within. Verification
 *  always succeeds for the null capability.
 *
 *  returns 1 on success
 *  returns 0 on error or failure to verify
 */
int PINT_verify_capability(const PVFS_capability *cap)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
    EVP_PKEY *pubkey;
    int ret;
    
    if (!cap)
    {
        return 0;
    }

    if (PINT_capability_is_null(cap))
    {
        return 1;
    }

    /* if capability has timed out */
    if (PINT_util_get_current_time() > cap->timeout)
    {
        return 0;
    }
    
    pubkey = SECURITY_lookup_pubkey(cap->issuer);
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found in lookup. Name used: '%s'\n", 
                     cap->issuer);
        return 0;
    }

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
	/* nlmills: TODO: use better error reporting */
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyInit failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }

    ret = EVP_VerifyUpdate(&mdctx, 
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
    else 
    {
	/* nlmills: TODO: use better error reporting */
	gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyUpdate failure.\n");
	EVP_MD_CTX_cleanup(&mdctx);
	return 0;
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
    char buf[256];
    const EVP_MD *md = NULL;
    int ret;
    
    assert(security_privkey);
    
    config = PINT_get_server_config();
    assert(config->server_alias);
    
    cred->issuer = strdup(config->server_alias);
    
    /* nlmills: TODO: time out the credential with the cert */
    cred->timeout = PINT_util_get_current_time() + config->security_timeout;
    
#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);
    
    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    ret &= EVP_SignUpdate(&mdctx, &cred->serial, sizeof(uint32_t));
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
        gossip_debug(GOSSIP_SECURITY_DEBUG, "SignUpdate failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return -1;
    }
    
    ret = EVP_SignFinal(&mdctx, cred->signature, &cred->sig_size,
                        security_privkey);
    EVP_MD_CTX_cleanup(&mdctx);
    if (!ret)
    {
        ERR_error_string_n(ERR_get_error(), buf, 256);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error signing credential: "
		     "%s\n", buf);
        return -1;
    }
    
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
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
    EVP_PKEY *pubkey;
    char buf[256];
    int ret;

    if (!cred)
    {
        return 0;
    }

    if (PINT_util_get_current_time() > cred->timeout)
    {
        return 0;
    }

    /* nlmills: TODO: implement credential revocation */

    pubkey = SECURITY_lookup_pubkey(cred->issuer);
    if (pubkey == NULL)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Public key not found for issuer: %s\n", 
                     cred->issuer);
        return 0;
    }

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    EVP_MD_CTX_init(&mdctx);
    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyInit failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }

    ret = EVP_VerifyUpdate(&mdctx, &cred->serial, sizeof(uint32_t));
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
    if (!ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "VerifyUpdate failure.\n");
        EVP_MD_CTX_cleanup(&mdctx);
        return 0;
    }

    ret = EVP_VerifyFinal(&mdctx, cred->signature, cred->sig_size, pubkey);
    if (ret < 0)
    {
        ERR_error_string_n(ERR_get_error(), buf, 256);
	gossip_debug(GOSSIP_SECURITY_DEBUG, "Error verifying credential: "
		     "%s\n", buf);
    }

    EVP_MD_CTX_cleanup(&mdctx);

    return (ret == 1);
}

/* PINT_verify_certificate
 * 
 * Verifies an X.509 certificate against the local trust store.
 *
 * returns negative on error.
 * returns 0 on success.
 */
int PINT_verify_certificate(const char *certstr,
                            const PVFS_signature signature,
                            uint32_t sig_size)
{
    BIO *certbio;
    X509 *cert;
    X509_STORE_CTX *store_ctx;
    EVP_PKEY *pkey;
    EVP_MD_CTX mdctx;
    const EVP_MD *md = NULL;
    unsigned long err;
    int ret;

    if (!certstr || !signature || (sig_size == 0))
    {
        return -PVFS_EINVAL;
    }

    /******* Part 1 - verify the certificate */

    certbio = BIO_new_mem_buf((char*)certstr, -1);
    if (!certbio)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        return -PVFS_EINVAL;
    }

    cert = PEM_read_bio_X509(certbio, NULL, NULL, NULL);
    BIO_vfree(certbio);
    if (!cert)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        return -PVFS_EINVAL;
    }

    store_ctx = X509_STORE_CTX_new();
    if (!store_ctx)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        X509_free(cert);
        return -PVFS_ENOMEM;
    }
    /* XXX: previous versions did not return a value */
    ret = X509_STORE_CTX_init(store_ctx, security_store, cert, NULL);
    if (!ret)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        X509_STORE_CTX_free(store_ctx);
        X509_free(cert);
        return -PVFS_EINVAL;
    }
    /* nlmills: TODO: set any verification options */

    ret = X509_verify_cert(store_ctx);
    X509_STORE_CTX_free(store_ctx);
    if (ret == 0)
    {
        X509_free(cert);
	return -PVFS_EPERM;
    }
    else if (ret < 0)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        X509_free(cert);
        return -PVFS_EINVAL;
    }

    /* nlmills: TODO: does ref counting keep key from being freed with cert */
    pkey = X509_get_pubkey(cert);
    X509_free(cert);
    if (!pkey)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        return -PVFS_EINVAL;
    }

    /******* Part 2 - verify the signature */

    EVP_MD_CTX_init(&mdctx);

#if defined(SECURITY_ENCRYPTION_RSA)
    md = EVP_sha1();
#elif defined(SECURITY_ENCRYPTION_DSA)
    md = EVP_dss1();
#endif

    ret = EVP_VerifyInit_ex(&mdctx, md, NULL);
    if (!ret)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
	EVP_PKEY_free(pkey);
        return -PVFS_EINVAL;
    }
   
    ret = EVP_VerifyUpdate(&mdctx, certstr, strlen(certstr) * sizeof(char));
    if (!ret)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
	EVP_PKEY_free(pkey);
        return -PVFS_EINVAL;
    }

    ret = EVP_VerifyFinal(&mdctx, 
			  (unsigned char*)signature, 
			  (unsigned int)sig_size, 
			  pkey);
    EVP_MD_CTX_cleanup(&mdctx);
    EVP_PKEY_free(pkey);
    if (ret == 0)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
            "Certificate verification error: invalid client signature\n");
        return -PVFS_EPERM;
    }
    else if (ret < -1)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        return -PVFS_EINVAL;
    }

    return 0;
}

/* PINT_lookup_account
 *
 * Finds the user account mapped to the given X.509 certificate.
 *
 * returns the account name on success.
 * returns NULL if no mapping exists.
 */
const char *PINT_lookup_account(const char *certstr)
{
    BIO *certbio;
    X509 *cert;
    X509_NAME *subject;
    char *subjectstr;
    STACK *emails;
    const char *account;
    unsigned long err;

    if (!certstr)
    {
        return NULL;
    }

    certbio = BIO_new_mem_buf((char*)certstr, -1);
    if (!certbio)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        return NULL;
    }

    cert = PEM_read_bio_X509(certbio, NULL, NULL, NULL);
    BIO_vfree(certbio);
    if (!cert)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        return NULL;
    }

    subject = X509_get_subject_name(cert);
    if (!subject)
    {
	X509_free(cert);
        return NULL;
    }

    subjectstr = X509_NAME_oneline(subject, NULL, 0);
    if (!subjectstr)
    {
        err = ERR_get_error();
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: %s\n",
                     ERR_func_error_string(err),
                     ERR_reason_error_string(err));
        X509_free(cert);
        return NULL;
    }

    emails = X509_get1_email(cert);

    account = find_account(subjectstr, emails);
    
    X509_email_free(emails);
    CRYPTO_free(subjectstr);
    X509_free(cert);

    return account;
}

/* PINT_lookup_userid
 * 
 * Searches for a userid that matches the given account in the system
 * password database.
 *
 * returns negative on failure.
 * returns zero on success.
 */
int PINT_lookup_userid(const char *account, PVFS_uid *userid)
{
    struct passwd pwbuf;
    struct passwd *pwbufp;
    char *buf;
    long max;
    int ret;

    max = sysconf(_SC_GETPW_R_SIZE_MAX);
    assert(max != -1);
    buf = calloc(max, sizeof(char));
    if (!buf)
    {
        return -PVFS_ENOMEM;
    }

    memset(&pwbuf, 0, sizeof(struct passwd));
    ret = getpwnam_r(account, &pwbuf, buf, max, &pwbufp);
    if ((pwbufp == NULL) || ret)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "User '%s' not found in password database\n",
                     account);
        free(buf);
        return -PVFS_EINVAL;
    }

    *userid = pwbuf.pw_uid;

    free(buf);
    return 0;
}

int PINT_lookup_groups(const char *account, PVFS_gid **group_array,
		       uint32_t *num_groups)
{
    long ngroups_max;
    long buf_max;
    struct passwd pwbuf;
    struct passwd *pwbufp;
    char *buf;
    gid_t pw_gid;
    int ngroups;
    gid_t *groups;
    int ret;
    int i;

    buf_max = sysconf(_SC_GETPW_R_SIZE_MAX);
    assert(buf_max != -1);
    buf = calloc(buf_max, sizeof(char));
    if (!buf)
    {
	*num_groups = 0;
	*group_array = NULL;
	return -PVFS_ENOMEM;
    }

    memset(&pwbuf, 0, sizeof(struct passwd));
    ret = getpwnam_r(account, &pwbuf, buf, buf_max, &pwbufp);
    if ((pwbufp == NULL) || ret)
    {
	gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "User '%s' not found in password database\n",
                     account);
        free(buf);
	*num_groups = 0;
	*group_array = NULL;
        return -PVFS_EINVAL;
    }

    pw_gid = pwbuf.pw_gid;
    free(buf);
    
    /* leave room for euid */
    ngroups_max = sysconf(_SC_NGROUPS_MAX) + 1;
    assert(ngroups_max != -1);
    groups = calloc(ngroups_max, sizeof(gid_t));
    if (!groups)
    {
	*num_groups = 0;
	*group_array = NULL;
	return -PVFS_ENOMEM;
    }

    /* nlmills: TODO: set up autoconf to define HAVE_GETGROUPLIST */
#ifdef HAVE_GETGROUPLIST

    ngroups = ngroups_max;
    ret = getgrouplist(account, pw_gid, groups, &ngroups);
    if (ret < 0)
    {
	free(groups);
	*num_groups = 0;
	*group_array = NULL;
	return -PVFS_EINVAL;
    }

    /* getgrouplist likes to put pw_gid last */
    if (groups[ngroups-1] == pw_gid)
    {
        groups[ngroups-1] = groups[0];
        groups[0] = pw_gid;
    }

#else /* !HAVE_GETGROUPLIST */

    ngroups = getugroups(ngroups_max, groups, account, pw_gid);
    if (ngroups < 0)
    {
	free(groups);
	*num_groups = 0;
	*group_array = NULL;
	return -PVFS_EINVAL;
    }

#endif /* HAVE_GROUPLIST */

    *group_array = calloc(ngroups, sizeof(PVFS_gid));
    if(!(*group_array))
    {
	free(groups);
	*num_groups = 0;
	return -PVFS_ENOMEM;
    }

    for(i = 0; i < ngroups; i++)
    {
	(*group_array)[i] = (PVFS_gid)groups[i];
    }
    *num_groups = ngroups;

    free(groups);

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

    EVP_PKEY_free(security_privkey);
    security_privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);
    if (security_privkey == NULL)
    {
        ERR_error_string_n(ERR_get_error(), buf, 256);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading private key: "
		     "%s\n", buf);
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

        do
        {
            ch = fgetc(keyfile);
        } while(isspace(ch));

        ungetc(ch, keyfile);

        key = PEM_read_PUBKEY(keyfile, NULL, NULL, NULL);
        if (key == NULL)
        {
            ERR_error_string_n(ERR_get_error(), buf, 4096);
            gossip_debug(GOSSIP_SECURITY_DEBUG, "Error loading public key: "
                         "%s\n", buf);
            fclose(keyfile);
            return -1;
        }

        ret = SECURITY_add_pubkey(buf, key);
        if (ret < 0)
        {
            PVFS_strerror_r(ret, buf, 4096);
            gossip_debug(GOSSIP_SECURITY_DEBUG, "Error inserting public "
                         "key: %s\n", buf);
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

/*  load_ca_bundle
 * 
 *  Initializes the X509_STORE used to verify client credentials
 *  and loads a list of trusted CA's from the filesystem. The path
 *  argument is the location of the file containing trusted CA
 *  certificates in PEM format.
 *
 *  returns -1 on failure
 *  returns 0 on success
 */
static int load_ca_bundle(const char *path)
{
    char buf[512];
    int ret;

    security_store = X509_STORE_new();
    if (!security_store)
    {
        ERR_error_string_n(ERR_get_error(), buf, 512);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "Error creating security store: "
                     "%s\n", buf);
        return -1;
    }

    X509_STORE_set_verify_cb_func(security_store, verify_callback);
    /* nlmills: TODO: set any default verification options */

    ret = X509_STORE_load_locations(security_store, path, NULL);
    if (!ret)
    {
        ERR_error_string_n(ERR_get_error(), buf, 512);
        gossip_err("Error loading CA bundle file: %s\n", buf);
        X509_STORE_free(security_store);
        return -1;
    }

    return 0;
}

static int verify_callback(int ok, X509_STORE_CTX *ctx)
{
    if (!ok)
    {
        int err;
        err = X509_STORE_CTX_get_error(ctx);
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "Certificate verification error: %s\n",
                     X509_verify_cert_error_string(err));
    }

    return ok;
}

/* find_account
 *
 * Internal function to find matches in the mappings configuration for the
 * given subject strings and email addresses.
 */
/* nlmills: TODO: log matches for debugging configs */
/* nlmills: TODO: consider case-insensitve compare */
/* nlmills: TODO: refactor into separate functions for each mapping type */
static const char *find_account(const char *subject, const STACK *emails)
{
    const struct server_configuration_s *config;
    PINT_llist_p mappings;
    struct security_mapping_s *mapping;
    regex_t regex;
    char *errbuf;
    int sz;
    const char *account = NULL;
    int ret;

    config = PINT_get_server_config();

    mappings = config->security_mappings;
    if (!mappings)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "No security mappings defined!\n");
        return NULL;
    }

    /* exits after the first match */
    for (mappings = mappings->next; mappings; mappings = mappings->next)
    {
        mapping = (struct security_mapping_s*)mappings->item;
        if (mapping->keyword == SECURITY_KEYWORD_EMAIL)
        {
            ret = sk_find((STACK*)emails, mapping->pattern);
            if (ret != -1)
            {
                gossip_debug(GOSSIP_SECURITY_DEBUG,
                             "Matched email '%s' to account '%s'\n",
                             sk_value(emails, ret),
                             mapping->account);
                account = mapping->account;
            }
        }
        else if (mapping->keyword == SECURITY_KEYWORD_EMAIL_REGEX)
        {
            int i;

            ret = regcomp(&regex, mapping->pattern, REG_EXTENDED|REG_NOSUB);
            if (ret)
            {
                sz = regerror(ret, &regex, NULL, 0);
                errbuf = calloc(sz, sizeof(char));
                if (errbuf)
                {
                    regerror(ret, &regex, errbuf, sz);
                    gossip_err("Error compiling regular expression '%s': %s\n",
                               mapping->pattern,
                               errbuf);
                    free(errbuf);
                }
                continue;
            }

            for (i = 0; i < sk_num(emails); i++)
            {
                ret = regexec(&regex, sk_value(emails, i), 0, NULL, 0);
                if (!ret)
                {
                    gossip_debug(GOSSIP_SECURITY_DEBUG,
                                 "Matched pattern '%s' to email '%s' " 
                                 "and account '%s'\n",
                                 mapping->pattern,
                                 sk_value(emails, i),
                                 mapping->account);
                    account = mapping->account;
                    break;
                }
            }
            
            regfree(&regex);
        }
        else if (mapping->keyword == SECURITY_KEYWORD_SUBJECT)
        {
            if (!strcmp(subject, mapping->pattern))
            {
                gossip_debug(GOSSIP_SECURITY_DEBUG,
                             "Matched subject '%s' to account '%s'\n",
                             subject,
                             mapping->account);
                account = mapping->account;
            }
        }
        else if (mapping->keyword == SECURITY_KEYWORD_SUBJECT_REGEX)
        {
            ret = regcomp(&regex, mapping->pattern, REG_EXTENDED|REG_NOSUB);
            if (ret)
            {
                sz = regerror(ret, &regex, NULL, 0);
                errbuf = calloc(sz, sizeof(char));
                if (errbuf)
                {
                    regerror(ret, &regex, errbuf, sz);
                    gossip_err("Error compiling regular expression '%s': %s\n",
                               mapping->pattern,
                               errbuf);
                    free(errbuf);
                }
                continue;
            }
            ret = regexec(&regex, subject, 0, NULL, 0);
            regfree(&regex);
            if (!ret)
            {
                gossip_debug(GOSSIP_SECURITY_DEBUG,
                                 "Matched pattern '%s' to subject '%s' " 
                                 "and account '%s'\n",
                                 mapping->pattern,
                                 subject,
                                 mapping->account);
                account = mapping->account;
            }
        }

        /* match found */
        if (account)
        {
            break;
        }
    }

    return account;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
