#include <httpd.h>
#include <ap_config.h>
#include <ap_provider.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <mod_auth.h>
#include <apr_strings.h>
#include <pvfs2.h>
#include <pint-cached-config.h>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

#include <time.h>

static char *certpath;
static int exp;
static char *mntpt;
static int pvfsinit;

static int is_valid_cert(request_rec *r, char *certfile)
{
    /* XXX: Should we try to use PVFS or just check the expiration
     * date. */
    /* Report not valid for any errors. */
    FILE *f;
    X509 *x509;
    int valid;
    time_t now;
    int i;
    f = fopen(certfile, "r");
    if (f == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_authn_orangefs:is_valid_cert fopen failed");
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_authn_orangefs:is_valid_cert 0");
        return 0;
    }
    x509 = 0;
    PEM_read_X509(f, &x509, 0, 0);
    if (x509 == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_authn_orangefs:is_valid_cert PEM_read_X509 "
                      "failed");
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_authn_orangefs:is_valid_cert 0");
        if (x509 != 0)
            X509_free(x509);
        fclose(f);
        return 0;
    }
    now = time(0);
    i = X509_cmp_time(X509_get_notAfter(x509), &now);
    valid = i >= 0;
    X509_free(x509);
    fclose(f);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_authn_orangefs:is_valid_cert %d", valid);
    return valid;
}

static int store_password(request_rec *r, const char *user,
                          const char *password, const char *hashfile)
{
    int fd;
    unsigned char hashdata[128];
    int i;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_authn_orangefs:store_password");
 
   /* We have decided to grant access. We must store the password (hash)
    * in the filesystem. */

    fd = open(hashfile, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
    if (fd == -1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:store_password "
                      "could not open '%s' for writing", hashfile);
        return 0;
    }
    if (RAND_bytes(hashdata, 64) != 1) {
        close(fd);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:store_password "
                      "could not generate random bytes");
        return 0;
    }
    if (PKCS5_PBKDF2_HMAC(password, strlen(password), hashdata, 64,
                          10000, EVP_sha512(), 64, hashdata+64) != 1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:store_password "
                      "could not hash password");
        close(fd);
        return 0;
    }
    i = write(fd, hashdata, 128);
    if (i < 128) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:store_password "
                      "could not write password hash");
        close(fd);
        return 0;
    }
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_authn_orangefs:store_password "
                  "done");
    close(fd);
    return 1;
}

static int check_password(request_rec *r, const char *user,
                          const char *password, const char *hashfile)
{
    int fd;
    unsigned char hashdata[128];
    unsigned char realhash[64];
    int i;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_authn_orangefs:check_password");

    fd = open(hashfile, O_RDONLY);
    if (fd == -1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:check_password "
                      "could not open '%s' for reading", hashfile);
        return 0;
    }
    i = read(fd, hashdata, 128);
    if (i < 128) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:check_password "
                      "could not read from '%s'", hashfile);
        close(fd);
        return 0;
    }
    if (PKCS5_PBKDF2_HMAC(password, strlen(password), hashdata, 64,
                          10000, EVP_sha512(), 64, realhash) != 1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:check_password "
                      "could not hash password");
        close(fd);
        return 0;
    }
    if (memcmp(realhash, hashdata+64, 64) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:check_password "
                      "wrong password");
        close(fd);
        return 0;
    }
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                  "mod_authn_orangefs:check_password "
                  "good password; done");
    close(fd);
    return 1;
}

static authn_status authn_check_orangefs(request_rec *r,
                                         const char *user,
                                         const char *password)
{
    char certfile[256];
    char keyfile[256];
    int rc;
    PVFS_fs_id fs_id;
    char path[PVFS_NAME_MAX];
    char err[256];
    int addr_count;
    PVFS_BMI_addr_t *addr_array;
    PVFS_certificate cert;
    PVFS_security_key privkey;
    X509 *xcert;
    RSA *rsa_privkey;
    PVFS_key_data keybuf;
    EVP_PKEY *evpprivkey;
    char hashfile[256];

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_authn_orangefs:authn_check_orangefs %s", user);

    /* Have we already got a cert? */
    snprintf(certfile, 256, "%s/%s-cert.pem", certpath, user);
    snprintf(keyfile, 256, "%s/%s-key.pem", certpath, user);
    snprintf(hashfile, 256, "%s/%s-hash", certpath, user);
    if (access(certfile, F_OK) == 0 && access(keyfile, F_OK) == 0 &&
        access(hashfile, F_OK) == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "have certfile '%s' and keyfile '%s'",
                      certfile, keyfile);
        /* If the cert is valid, we must check its password. */
        if (is_valid_cert(r, certfile)) {
            if (access(hashfile, F_OK) == 0) {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                              "mod_authn_orangefs:authn_check_orangefs "
                              "have hashfile '%s'", hashfile);
                if (check_password(r, user, password, hashfile)) {
                    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                                  "mod_authn_orangefs:"
                                  "authn_check_orangefs granted via "
                                  "hash");
                    return AUTH_GRANTED;
                } else {
                    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                                  "mod_authn_orangefs:"
                                  "authn_check_orangefs no hash; "
                                  "try via new certificate");
                }
            } else {
                ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                              "mod_authn_orangefs:authn_check_orangefs "
                              "have no hashfile '%s'", hashfile);
                return AUTH_GENERAL_ERROR;
            }
        }
    } else {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "have no certfile %s or keyfile %s or hashfile "
                      "%s", certfile, keyfile, hashfile);
        unlink(certfile);
        unlink(keyfile);
        unlink(hashfile);
    }

    /* Do we know which filesystem the user is on? */
    if (mntpt == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs no "
                      "AuthOrangeFSMountPoint directive!");
        return AUTH_GENERAL_ERROR;
    }
    rc = PVFS_util_resolve(mntpt, &fs_id, path, PVFS_NAME_MAX);
    if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PVFS_util_resolve failed %d %s", rc, err);
        return AUTH_GENERAL_ERROR;
    }

    /* Setup preliminary data structures and get the cert from the
     * server. */
    rc = PVFS_mgmt_count_servers(fs_id, PINT_SERVER_TYPE_ALL,
                                 &addr_count);
    if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PVFS_mgmt_count_servers failed %d %s", rc, err);
        return AUTH_GENERAL_ERROR;
    }
    addr_array = apr_pcalloc(r->pool,
                 addr_count*sizeof(PVFS_BMI_addr_t));
    if (addr_array == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "malloc failed");
        return AUTH_GENERAL_ERROR;
    }
    rc = PVFS_mgmt_get_server_array(fs_id, PINT_SERVER_TYPE_ALL,
                                    addr_array, &addr_count);
    if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PVFS_mgmt_get_server_array failed %d %s",
                      rc, err);
        return AUTH_GENERAL_ERROR;
    }
    rc = PVFS_mgmt_get_user_cert(fs_id, user, password, addr_count,
                                 addr_array, &cert, &privkey, exp);
    if (rc == -PVFS_ENOENT) {
        return AUTH_USER_NOT_FOUND;
    } else if (rc == -PVFS_EACCES) {
        return AUTH_DENIED;
    } else if (rc == -PVFS_EINVAL) {
        return AUTH_DENIED;
    } else if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PVFS_mgmt_get_user_cert failed %d %s", rc, err);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }

    /* Save the cert to disk. */
    rc = PINT_cert_to_X509(&cert, &xcert);
    if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PINT_cert_to_X509 failed %d %s", rc, err);
        X509_free(xcert);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }
    rc = PINT_save_cert_to_file(certfile, xcert);
    if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PINT_save_cert_to_file failed %d %s", rc, err);
        X509_free(xcert);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }

    /* Save the key to disk. */
    evpprivkey = 0;
    rsa_privkey = 0;
    keybuf = privkey.buf;
    rsa_privkey = d2i_RSAPrivateKey(0, (const unsigned char **)&keybuf,
                                    privkey.buf_size);
    if (rsa_privkey == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "could not load user private key");
        RSA_free(rsa_privkey);
        X509_free(xcert);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }
    evpprivkey = EVP_PKEY_new();
    if (evpprivkey == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "could not allocate object");
        RSA_free(rsa_privkey);
        X509_free(xcert);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }
    if (!EVP_PKEY_assign_RSA(evpprivkey, rsa_privkey)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "could not allocate object");
        EVP_PKEY_free(evpprivkey);
        X509_free(xcert);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }

    rc = PINT_save_privkey_to_file(keyfile, evpprivkey);
    if (rc != 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "PINT_save_cert_to_file failed %d %s", rc, err);
        EVP_PKEY_free(evpprivkey);
        X509_free(xcert);
        PINT_cleanup_cert(&cert);
        PINT_cleanup_key(&privkey);
        return AUTH_GENERAL_ERROR;
    }
    if (chmod(keyfile, S_IRUSR|S_IWUSR) != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "mod_authn_orangefs:authn_check_orangefs "
                      "chmod 0600 %s failed: %s", keyfile,
                      strerror(errno));
        /* Do not quit here as the file is already on the filesystem.
         * Hopefully the administrator will see the message, and the
         * server is probably running on a dedicated system with no
         * users anyway. */
    }

    EVP_PKEY_free(evpprivkey);
    X509_free(xcert);
    PINT_cleanup_cert(&cert);
    PINT_cleanup_key(&privkey);

    if (store_password(r, user, password, hashfile) != 1) {
        return AUTH_GENERAL_ERROR;
    }

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                  "mod_authn_orangefs:authn_check_orangefs "
                  "granted '%s'", user);
    return AUTH_GRANTED;
}

static int post_config(apr_pool_t *pconf, apr_pool_t *plog,
                       apr_pool_t *ptemp, server_rec *s)
{
    int rc;
    char err[256];
    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, plog,
                  "mod_authn_orangefs:post_config");
    if (pvfsinit) {
        rc = PVFS_util_init_defaults();
        if (rc != 0) {
            PVFS_strerror_r(rc, err, 256);
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, plog,
                          "mod_authn_orangefs:post_config "
                          "PVFS_util_init_defaults failed (%d) %s",
                          rc, err);
            return !OK;
        }
    }
    ap_add_version_component(pconf, "mod_authn_orangefs/1.0");
    return OK;
}

static const authn_provider authn_orangefs_provider = {
    authn_check_orangefs,
    NULL
};

static void register_hooks(apr_pool_t *pool)
{
    exp = 60;
    ap_hook_post_config(post_config, NULL, NULL, APR_HOOK_LAST);
    ap_register_provider(pool, AUTHN_PROVIDER_GROUP, "orangefs", "0",
                         &authn_orangefs_provider);
}

static const char *set_certpath(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, cmd->server,
                 "mod_authn_orangefs:set_certpath %s",
                 arg1);
    certpath = apr_pstrdup(cmd->pool, arg1);
    return 0;
}

static const char *set_exp(cmd_parms *cmd, void *config,
                           const char *arg1)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, cmd->server,
                 "mod_authn_orangefs:set_pvfsinit %s",
                 arg1);
    exp = strtol(arg1, 0, 10);
    return 0;
}

static const char *set_mntpt(cmd_parms *cmd, void *config,
                             const char *arg1)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, cmd->server,
                 "mod_authn_orangefs:set_mntpt %s",
                 arg1);
    mntpt = apr_pstrdup(cmd->pool, arg1);
    return 0;
}

static const char *set_pvfsinit(cmd_parms *cmd, void *config,
                                const char *arg1)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, cmd->server,
                 "mod_authn_orangefs:set_pvfsinit %s",
                 arg1);
    pvfsinit = strcmp(arg1, "orangefs_authn_module") == 0;
    return 0;
}

static const command_rec directives[] = {
    AP_INIT_TAKE1("AuthOrangeFSCertPath", set_certpath, 0, ACCESS_CONF,
                  "The path to store user keys and certificates in. The"
                  " web server must be able to write here."),
    AP_INIT_TAKE1("AuthOrangeFSCertValidity", set_exp, 0, ACCESS_CONF,
                  "Set this to the desired certificate validity in "
                  "minutes. Defaults to 1 hour."),
    AP_INIT_TAKE1("AuthOrangeFSMountPoint", set_mntpt, 0, ACCESS_CONF,
                  "The mount point of the filesystem that will be "
                  "consulted for user information."),
    AP_INIT_TAKE1("PVFSInit", set_pvfsinit, 0, RSRC_CONF,
                  "Set to this module's name to specify that this "
                  "module should initialize PVFS."),
    { NULL }
};

module AP_MODULE_DECLARE_DATA authn_orangefs_module = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
    NULL,
    NULL,
    directives,
    register_hooks
};
