/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/*
 *  Certificate functions - credential are loaded from a 
 *  certificate in the user's profile directory or a configured
 *  directory. The (proxy) certificate contains the OrangeFS UID/GID
 *  in its policy data field. A CA certificate is used to verify
 *  the proxy certificate. 
 */

#include <Windows.h>
#include <Userenv.h>
#include <stdio.h>

#include <crypto/x509.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

#include "cert.h"
#include "user-cache.h"
#include "cred.h"
#include "cert-util.h"
#include "security-util.h"

#define OPENSSL_CERT_ERROR    0xFFFF

extern PORANGEFS_OPTIONS goptions;

static CRYPTO_ONCE once = CRYPTO_ONCE_STATIC_INIT;
static CRYPTO_RWLOCK* rwlock = NULL;

/* initialize OpenSSL */
void openssl_init()
{
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
}

/* cleanup OpenSSL */
void openssl_cleanup()
{
    if (rwlock) {
        CRYPTO_THREAD_lock_free(rwlock);
    }
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
    ERR_remove_state(0);
}

static void lock_init(void)
{
    rwlock = CRYPTO_THREAD_lock_new();
}

static int get_proxy_auth_ex_data_cred()
{
    static volatile int idx = -1;

    if (idx < 0)
    {
        /* set up lock if necessary */
        if (!rwlock)
        {
            if (!CRYPTO_THREAD_run_once(&once, lock_init) || rwlock == NULL) {
                return -1;
            }
        }
        
        if (CRYPTO_THREAD_write_lock(rwlock))
        {
            if (idx < 0)
            {
                idx = X509_STORE_CTX_get_ex_new_index(0, "credential", NULL, NULL,
                    NULL);
            }
            CRYPTO_THREAD_unlock(rwlock);
        }
        else
        {
            report_error("Failed to lock thread in get_proxy_auth_ex_data_cred()", -PVFS_ESECURITY);
        }
    }

    return idx;
}

static int get_proxy_auth_ex_data_user_name()
{
    static volatile int idx = -1;
    if (idx < 0)
    {
        /* set up lock if necessary */
        if (!rwlock)
        {
            if (!CRYPTO_THREAD_run_once(&once, lock_init) || rwlock == NULL) {
                return -1;
            }
        }

        if (CRYPTO_THREAD_write_lock(rwlock))
        {
            if (idx < 0)
            {
                idx = X509_STORE_CTX_get_ex_new_index(0, "user_name",
                    NULL, NULL, NULL);
            }
            CRYPTO_THREAD_unlock(rwlock);
        }
        else
        {
            report_error("Failed to lock thread in get_proxy_auth_ex_data_user_name()", -PVFS_ESECURITY);
        }
    }

    return idx;
}

/* parse the credential string uid/gid from credstr */
static int parse_credential(char *credstr, PVFS_uid *uid, PVFS_gid *gid)
{
    char *p, uidstr[16], gidstr[16];
    int i, ret = 0;

    uidstr[0] = gidstr[0] = '\0';
    i = 0;
    p = credstr;
    while (*p && *p != '/' && i < 15)
    {
        if (isdigit(*p))
        {
            uidstr[i++] = *p++;
        }
        else 
        {
            /* error */
            ret = 1;
            break;
        }
    }
    uidstr[i] = '\0';
    if (ret == 0)
    {
        if (*p == '/')
            p++;
        i = 0;
        while(*p && i < 15)
        {
            if (isdigit(*p))
            {
                gidstr[i++] = *p++;
            }
            else 
            {
                ret = 1;
                break;
            }
        }
        gidstr[i] = '\0';
    }

    if (ret == 0)
    {
        *uid = atoi(uidstr);
        *gid = atoi(gidstr);
    }

    return ret;
}

static int verify_callback(int ok, X509_STORE_CTX *ctx)
{
    char *user_name;
    X509 *xs;
    PROXY_CERT_INFO_EXTENSION *pci;
    char *credstr;
    PVFS_credential *credential;
    PVFS_uid uid;
    PVFS_gid gid;
    char error_msg[256];
    int ret;

    /* prior verifies have succeeded */
    if (ok == 1) 
    {
        /* parse the credential string uid/gid from the policy */
        xs = ctx->current_cert;
        if (xs->ex_flags & EXFLAG_PROXY)
        {
            /* get Windows username for error logging */
            user_name = (char *) X509_STORE_CTX_get_ex_data(ctx, 
                get_proxy_auth_ex_data_user_name());
            
            /* get credential in {UID}/{GID} form from cert policy */
            pci = (PROXY_CERT_INFO_EXTENSION *) 
                    X509_get_ext_d2i(xs, NID_proxyCertInfo, NULL, NULL);

            if (pci->proxyPolicy->policy != NULL && pci->proxyPolicy->policy->length > 0)
            {
                credstr = (char *) pci->proxyPolicy->policy->data;
                credential = (PVFS_credential *) X509_STORE_CTX_get_ex_data(
                    ctx, get_proxy_auth_ex_data_cred());
                ret = parse_credential(credstr, &uid, &gid);
                if (ret == 0)
                {
                    /* initialize and fill in credential */
                    init_credential(uid, &gid, 1, NULL, NULL, credential);
                }
                else
                {
                    _snprintf(error_msg, sizeof(error_msg), "User %s: proxy "
                        "certificate contains invalid credential policy", 
                        user_name);
                    report_error(error_msg, -PVFS_ESECURITY);
                    ok = 0;
                }
            }
            else
            {
                _snprintf(error_msg, sizeof(error_msg), "User %s: proxy "
                          "certificate contains no credential policy", 
                          user_name);
                report_error(error_msg, -PVFS_ESECURITY);
                ok = 0;
            }            

            PROXY_CERT_INFO_EXTENSION_free(pci);
        }
    }
    
    return ok;
}

/* verify certificate */
static unsigned long verify_cert(char *user_name,
                                 X509 *cert, 
                                 X509 *ca_cert,
                                 STACK_OF(X509) *chain,
                                 PVFS_credential *credential)
{
    X509_STORE *trust_store;
    X509_STORE_CTX *ctx = NULL;
    int ret, verify_flag = 0;
    int (*save_verify_cb)(int ok, X509_STORE_CTX *ctx);
    char error_msg[256];

    /* add CA cert to trusted store */
    trust_store = X509_STORE_new();
    if (trust_store == NULL)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    ret = X509_STORE_add_cert(trust_store, ca_cert);
    if (!ret)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    /* setup the context with the certs */
    ctx = X509_STORE_CTX_new();
    if (ctx == NULL)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    ret = X509_STORE_CTX_init(ctx, trust_store, cert, chain);
    if (!ret)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    /* set up verify callback */
    save_verify_cb = ctx->verify_cb;
    X509_STORE_CTX_set_verify_cb(ctx, verify_callback);
    X509_STORE_CTX_set_ex_data(ctx, get_proxy_auth_ex_data_cred(), credential);
    X509_STORE_CTX_set_ex_data(ctx, get_proxy_auth_ex_data_user_name(), user_name);
    X509_STORE_CTX_set_flags(ctx, X509_V_FLAG_ALLOW_PROXY_CERTS);

    /* verify the cert */
    verify_flag = 1;
    ret = (X509_verify_cert(ctx) == 1) ? 0 : OPENSSL_CERT_ERROR;

    X509_STORE_CTX_set_verify_cb(ctx, save_verify_cb);
    
verify_cert_exit:

    /* print error... for non-verify errors, get_cert_credential
       will print errors */
    if (verify_flag && ret == OPENSSL_CERT_ERROR && ctx->error != 0)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: proxy certificate "
            "verification error: %s", user_name, 
            X509_verify_cert_error_string(ctx->error));
        report_error(error_msg, -PVFS_ESECURITY);
    }

    if (ctx != NULL)
    {
        X509_STORE_CTX_cleanup(ctx);
        X509_STORE_CTX_free(ctx);
    }

    if (trust_store != NULL)
    {
        X509_STORE_free(trust_store);
    }

    return ret;
}

/* get user profile directory -- profile_dir should be MAX_PATH bytes */
static unsigned int get_profile_dir(HANDLE huser, 
                                    char *profile_dir)
{
    DWORD profile_len = MAX_PATH;

    if (!GetUserProfileDirectory(huser, profile_dir, &profile_len))
        return GetLastError();

    return 0;
}

/* retrieve OrangeFS credential from proxy cert */
int get_proxy_cert_credential(HANDLE huser,
                              char *user_name,
                              PVFS_credential *credential,
                              ASN1_UTCTIME **expires)
{
    char cert_dir[MAX_PATH], cert_path[MAX_PATH],
         cert_pattern[MAX_PATH];
    HANDLE h_find;
    WIN32_FIND_DATA find_data;
    X509 *cert = NULL, *chain_cert = NULL, *ca_cert = NULL;
    STACK_OF(X509) *chain = NULL;
    int ret;
    unsigned long err_flag = FALSE;
    char error_msg[1024];

    client_debug("   get_proxy_cert_credential: enter\n");
    
    if (user_name == NULL || credential == NULL || expires == NULL)
    {
        client_debug("   get_proxy_cert_credential: invalid parameter\n");
        return -1;
    }

    /* system user -- return root credential */
    if (!stricmp(user_name, "SYSTEM"))
    {
        *expires = NULL;
        return get_system_credential(credential);
    }

    /* locate the certificates and CA */
    if (strlen(goptions->cert_dir_prefix) > 0)
    {
        if ((strlen(goptions->cert_dir_prefix) + strlen(user_name) + 8) > MAX_PATH)
        {
            _snprintf(error_msg, sizeof(error_msg), "User %s: path to certificate "
                "too long", user_name);
            report_error(error_msg, -PVFS_EOVERFLOW);
            return -1;
        }

        /* cert dir is cert_dir_prefix\user_name */
        strcpy(cert_dir, goptions->cert_dir_prefix);
        strcat(cert_dir, user_name);
        strcat(cert_dir, "\\");
    }
    else
    {
        /* get profile directory */
        ret = get_profile_dir(huser, cert_dir);
        if (ret == 0)
        {
            if (strlen(cert_dir) > 0 && cert_dir[strlen(cert_dir)-1] != '\\')
                strcat(cert_dir, "\\");
        }
        else
        {
            _snprintf(error_msg, sizeof(error_msg), "User %s: could not locate "
                "profile directory: %d", user_name, ret);
            report_error(error_msg, -PVFS_ENOENT);
            return ret;
        }
        
        if (strlen(cert_dir) + 7 > MAX_PATH)
        {
            _snprintf(error_msg, sizeof(error_msg), "User %s: profile directory too "
                "long", user_name);
            report_error(error_msg, -PVFS_EOVERFLOW);
            return -1;
        }
    }
    
    /* load certs */
    chain = sk_X509_new_null();

    strcpy(cert_pattern, cert_dir);
    strcat(cert_pattern, "cert.*");
    h_find = FindFirstFile(cert_pattern, &find_data);
    if (h_find == INVALID_HANDLE_VALUE)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: no certificates in %s", 
            user_name, cert_dir);
        report_error(error_msg, -PVFS_ENOENT);
        ret = -1;
        goto get_proxy_cert_credential_exit;
    }

    do
    {
        strcpy(cert_path, cert_dir);        
        strcat(cert_path, find_data.cFileName);
        /* load proxy cert */
        if (!stricmp(find_data.cFileName, "cert.0"))
        {
            ret = PINT_load_cert_from_file(cert_path, &cert);
        }
        else
        {
            /* load intermediate certs (including user cert) */
            ret = PINT_load_cert_from_file(cert_path, &chain_cert);
            if (ret == 0) 
                sk_X509_push(chain, chain_cert);
        }

        if (ret != 0)
        {
            _snprintf(error_msg, sizeof(error_msg), "Error loading cert %s. See "
                "below for details", cert_path);
            report_error(error_msg, ret);
        }
    } while (ret == 0 && FindNextFile(h_find, &find_data));

    FindClose(h_find);

    /* no proxy cert */
    if (cert == NULL)
    {
        _snprintf(error_msg, sizeof(error_msg), "Missing or invalid %scert.0. See "
            "below for details", cert_dir);
        report_error(error_msg, -PVFS_ESECURITY);
        ret = OPENSSL_CERT_ERROR;
    }
    
    if (ret != 0)
        goto get_proxy_cert_credential_exit;

    /* load CA cert */
    ret = PINT_load_cert_from_file(goptions->ca_file, &ca_cert);
    if (ret != 0)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: error loading CA "
            "certificate %s. See subsequent log messages for details", 
            user_name, goptions->ca_file);
        report_error(error_msg, ret);
        goto get_proxy_cert_credential_exit;
    }

    /* read and cache credential from certificate */
    ret = verify_cert(user_name, cert, ca_cert, chain, credential);

    if (ret == 0)
    {        
        *expires = ASN1_STRING_dup(X509_get_notAfter(cert));
        /* TODO: cache revision */
        credential->timeout = time(NULL) + PVFS2_SECURITY_TIMEOUT_MAX;
    }

get_proxy_cert_credential_exit:

    /* error handling */
    if (ret == OPENSSL_CERT_ERROR)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: certificate "
            "errors:\n", user_name);
        report_error(error_msg, -PVFS_ESECURITY);
        ret = -PVFS_ESECURITY;
    }

    /* free chain */
    if (chain != NULL)
        sk_X509_pop_free(chain, X509_free);

    if (cert != NULL)
        X509_free(cert);
    if (ca_cert != NULL)
        X509_free(ca_cert);

    client_debug("   get_proxy_cert_credential: exit\n");

    return ret;
}

static int get_module_dir(char *module_dir)
{
    int ret;
    char *p;
    
    ret = GetModuleFileName(NULL, module_dir, MAX_PATH);
    if (ret != 0)
    {
        /* get directory */
        p = strrchr(module_dir, '\\');
        if (p)
            *p = '\0';
    }

    return ret ? 0 : GetLastError();
}

/* retrieve OrangeFS credential from user cert */
int get_user_cert_credential(HANDLE huser,
                             char *user_name,
                             PVFS_credential *cred,
                             ASN1_UTCTIME **expires)
{
    int ret = 0;
    char key_file[MAX_PATH], cert_file[MAX_PATH],
         errmsg[1024];
    X509 *xcert = NULL;
    PVFS_certificate *cert = NULL;
    PVFS_gid group_array[1] = { PVFS_GID_MAX };

    /* 1. build certificate/keyfile path--profile or cert-dir
       2. fill in fields (append certificate data to credential)
       3. sign credential 
     */
    
    client_debug("   get_user_cert_credential: enter\n");

    if (user_name == NULL || strlen(user_name) == 0 || cred == NULL ||
        expires == NULL)
    {
        report_error("get_user_cert_credential:", -PVFS_EINVAL);
        return -PVFS_EINVAL;
    }

    /* expand the key file path */
    if (strlen(goptions->key_file) != 0)
    {
        ret = PINT_get_security_path(goptions->key_file, user_name, key_file, 
                                     MAX_PATH);
    }
    else
    {
        /* use module dir as the default for the system user */
        if (!stricmp(user_name, "SYSTEM"))
        {
            ret = get_module_dir(key_file);
        }
        else 
        {
            /* default: profile_dir\orangefs-cert-key.pem */
            ret = get_profile_dir(huser, key_file);
        }
        if (ret == 0)
        {
            if (strlen(key_file) + 24 > MAX_PATH)
            {
                _snprintf(errmsg, sizeof(errmsg), "User %s: profile dir: ",
                    user_name);
                report_error(errmsg, -PVFS_EOVERFLOW);
                return -PVFS_EOVERFLOW;
            }

            /* append filename */
            strcat(key_file, "\\orangefs-cert-key.pem");
        }
    }

    if (ret != 0)
    {
        _snprintf(errmsg, sizeof(errmsg), "User %s: key file path: ",
                  user_name);
        report_error(errmsg, ret);
        return ret;
    }

    /* expand the cert file path */
    if (strlen(goptions->cert_dir_prefix) != 0)
    {
        ret = PINT_get_security_path(goptions->cert_dir_prefix, user_name, cert_file, 
                                     MAX_PATH);
    }
    else
    {
        /* use module dir as the default for the system user */
        if (!stricmp(user_name, "SYSTEM"))
        {            
            ret = get_module_dir(cert_file);
        }
        else
        {
            /* default: profile_dir\orangefs-cert-key.pem */
            ret = get_profile_dir(huser, cert_file);
        }

        if (ret == 0)
        {
            if (strlen(cert_file) + 20 > MAX_PATH)
            {
                _snprintf(errmsg, sizeof(errmsg), "User %s: profile dir: ",
                    user_name);
                report_error(errmsg, -PVFS_EOVERFLOW);
                return -PVFS_EOVERFLOW;
            }

            /* append filename */
            strcat(cert_file, "\\orangefs-cert.pem");
        }
    }

    if (ret != 0)
    {
        _snprintf(errmsg, sizeof(errmsg), "User %s: cert file path: ",
            user_name);
        report_error(errmsg, ret);        
        return ret;
    }

    /* read the certificate file */
    ret = PINT_load_cert_from_file(cert_file, &xcert);
    if (ret != 0)
    {
        _snprintf(errmsg, sizeof(errmsg), "User %s: certificate load: ", user_name);
        report_error(errmsg, ret);
        return ret; 
    }

    /* convert X509 struct to PVFS_certificate */
    ret = PINT_X509_to_cert(xcert, &cert);
    if (ret != 0)
    {
        _snprintf(errmsg, sizeof(errmsg), "User %s: convert cert: ", user_name);
        report_error(errmsg, ret);        
        X509_free(xcert);
        return ret;
    }

    /* get the expiration time for caching */
    *expires = ASN1_STRING_dup(X509_get_notAfter(xcert));

    /* free X509 cert */
    X509_free(xcert);

    client_debug("   get_user_cert_credential: user: %s\tkey_file: %s\tcert_file: %s\n",
        user_name, key_file, cert_file);

    /* initialize the credential */
    ret = init_credential(PVFS_UID_MAX, group_array, 1, key_file, cert, cred);
    if (ret != 0)
    {
        _snprintf(errmsg, sizeof(errmsg), "User %s: credential error: %d", user_name, ret);
        report_error(errmsg, ret);
    }

    PINT_cleanup_cert(cert);

    client_debug("   get_user_cert_credential: exit\n");

    return ret;
}
