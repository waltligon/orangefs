/*
 * Copyright 2010 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

/* FIXME: obtaining HOST_NAME_MAX is platform specific and should be handled more generally */
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "src/proto/pvfs2-req-proto.h"
#include "src/common/security/getugroups.h"


typedef struct {
    const char *user;
    const char *group;
    int timeout;
    const char *keypath;
#ifdef ENABLE_SECURITY_CERT
    const char *certpath;
#endif
} options_t;

char cert_keypath[PATH_MAX];

static void usage(void)
{
#ifdef ENABLE_SECURITY_CERT
    puts("usage: pvfs2-gencred [-u uid] [-g gid] [-t timeout] "
         "[-k keyfile] [-c certfile]");
#else
    puts("usage: pvfs2-gencred [-u uid] [-g gid] [-t timeout] "
         "[-k keyfile]");
#endif
}

static int safe_write(int fd,
                      const void *buf,
                      size_t nbyte)
{
    const char *cbuf = (const char *)buf;
    ssize_t total;
    ssize_t cnt;

    for (total = 0; total < nbyte; total += cnt)
    {
        do cnt = write(fd, cbuf+total, (nbyte - total));
        while (cnt == -1 && errno == EINTR);
        if (cnt == -1)
        {
            return errno;
        }
    }

    return 0;
}

static int parse_options(int argc,
                         char **argv,
                         options_t *opts)
{
    int ch;
#ifdef ENABLE_SECURITY_CERT
    const char optstr[] = "u:g:t:k:c:";
#else
    const char optstr[] = "u:g:t:k:";
#endif
    
    while ((ch = getopt(argc, argv, optstr)) != -1)
    {
        switch(ch)
        {
            case 'u':
                opts->user = optarg;
                break;
            case 'g':
                opts->group = optarg;
                break;
            case 't':
                opts->timeout = strtol(optarg, NULL, 10);
                if (opts->timeout <= 0)
                {
                    fprintf(stderr, "%s: illegal timeout -- %s\n", argv[0],
                            optarg);
                    usage();
                    return EINVAL;
                }
                break;
            case 'k':
                opts->keypath = optarg;
                break;
#ifdef ENABLE_SECURITY_CERT
            case 'c':
                opts->certpath = optarg;
                break;
#endif
            case '?':
            default:
                usage();
                return EINVAL;
        }
    }
    
    return EXIT_SUCCESS;
}

#ifdef ENABLE_SECURITY_CERT
static char *get_certificate_keypath(const struct passwd *pwd)
{
    const char def_keyfile[] = "/.pvfs2-cert-key.pem";

    /* construct certificate private key file path */
    strncpy(cert_keypath, pwd->pw_dir, PATH_MAX);
    cert_keypath[PATH_MAX-1] = '\0';
    if ((strlen(cert_keypath) + strlen(def_keyfile)) < (PATH_MAX - 1))
    {
        strcat(cert_keypath, def_keyfile);
    }
    else
    {
        return NULL;
    }

    return cert_keypath;
}
#endif

static int create_credential(const struct passwd *pwd,
                             const gid_t *groups,
                             int ngroups,
#ifdef ENABLE_SECURITY_CERT
                             const char *certpath,
#endif
                             PVFS_credential *cred)
{
    char hostname[HOST_NAME_MAX+1];
    char *issuer;
#ifdef ENABLE_SECURITY_CERT
    const char def_certfile[] = "/.pvfs2-cert.pem";
    char def_certpath[PATH_MAX];
    FILE *f;
    X509 *cert;
    BIO *bio_mem;
    char *cert_buf;
#else
    int i;
#endif

    memset(cred, 0, sizeof(*cred));

    issuer = calloc(PVFS_REQ_LIMIT_ISSUER+1, 1);
    if (issuer == NULL)
    {
        return ENOMEM;
    }
    /* issuer field for clients is prefixed with "C:" */
    issuer[0] = 'C';
    issuer[1] = ':';
    gethostname(hostname, HOST_NAME_MAX);
    hostname[sizeof(hostname)-1] = '\0';
    strncpy(issuer+2, hostname, PVFS_REQ_LIMIT_ISSUER-2);

#ifdef ENABLE_SECURITY_CERT
    /* in cert mode, the uid/gids must be determined by
       server mapping */
    cred->userid = PVFS_UID_MAX;
    cred->num_groups = 1;
    cred->group_array = calloc(1, sizeof(PVFS_gid));
    if (cred->group_array == NULL)
    {
        free(issuer);
        return ENOMEM;
    }
    cred->group_array[0] = PVFS_GID_MAX;
#else
    cred->userid = (PVFS_uid)pwd->pw_uid;
    cred->num_groups = (uint32_t)ngroups;
    cred->group_array = calloc(ngroups, sizeof(PVFS_gid));
    if (cred->group_array == NULL)
    {
        free(issuer);
        return ENOMEM;
    }
    for (i = 0; i < ngroups; i++)
    {
        cred->group_array[i] = (PVFS_gid)groups[i];
    }
#endif /* ENABLE_SECURITY_CERT */

    cred->issuer = issuer;

#ifdef ENABLE_SECURITY_CERT
    /* use certpath or look for cert in home dir */
    if (certpath == NULL)
    {
        strncpy(def_certpath, pwd->pw_dir, PATH_MAX);
        def_certpath[PATH_MAX-1] = '\0';
        if ((strlen(def_certpath) + strlen(def_certfile)) < (PATH_MAX - 1))
        {
            strncat(def_certpath, def_certfile, sizeof(def_certfile));
        }
        else
        {            
            fprintf(stderr, "Path to certificate too long\n");
            return ERANGE;
        }
        certpath = def_certpath;
    }

    /* open certificate path */
    f = fopen(certpath, "r");
    if (f == NULL)
    {
        int err = errno;
        perror(certpath);
        return err;
    }

    /* read certificate */
    cert = PEM_read_X509(f, NULL, NULL, NULL);

    fclose(f);

    /* check cert */
    if (cert == NULL)
    {
        ERR_print_errors_fp(stderr);
        return ENODATA;
    }

    /* write cert to memory */
    bio_mem = BIO_new(BIO_s_mem());
    if (bio_mem == NULL)
    {
        ERR_print_errors_fp(stderr);
        return ENOMEM;
    }

    /* write cert to mem BIO */
    if (i2d_X509_bio(bio_mem, cert) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BIO_free(bio_mem);
        X509_free(cert);
        return EINVAL;
    }

    /* check size */
    cred->certificate.buf_size = BIO_get_mem_data(bio_mem, &cert_buf);
    if (cert_buf == NULL)
    {
        ERR_print_errors_fp(stderr);
        BIO_free(bio_mem);
        X509_free(cert);
        return EINVAL;
    }

    cred->certificate.buf = (PVFS_cert_data) malloc(cred->certificate.buf_size);
    memcpy(cred->certificate.buf, cert_buf, cred->certificate.buf_size);

    BIO_free(bio_mem);
#else  /* !ENABLE_SECURITY_CERT */
    cred->certificate.buf_size = 0;
    cred->certificate.buf = NULL;
#endif

    return EXIT_SUCCESS;
}

static int sign_credential(PVFS_credential *cred,
                           time_t timeout,
                           const char *keypath)
{
    FILE *keyfile;
    struct stat stats;
    EVP_PKEY *privkey;
    const EVP_MD *md;
    EVP_MD_CTX mdctx;
    int ret;

    keyfile = fopen(keypath, "rb");
    if (keyfile == NULL)
    {
        int err = errno;
        perror(keypath);
        return err;
    }

    ret = fstat(fileno(keyfile), &stats);
    if (ret == -1)
    {
        int err = errno;
        perror("stat");
        fclose(keyfile);
        return err;
    }
    if (stats.st_mode & (S_IROTH | S_IWOTH))
    {
        fprintf(stderr, "warning: insecure permissions on private key file "
                "%s\n", keypath);
    }

    privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);

    fclose(keyfile);

    if (privkey == NULL)
    {
        ERR_print_errors_fp(stderr);
        return ENODATA;
    }

    cred->timeout = (PVFS_time)(time(NULL) + timeout);
    cred->signature = malloc(EVP_PKEY_size(privkey));
    if (cred->signature == NULL)
    {
        EVP_PKEY_free(privkey);
        return ENOMEM;
    }

    md = EVP_PKEY_type(privkey->type) == EVP_PKEY_DSA ? EVP_dss1() : 
         EVP_sha1();
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
        ERR_print_errors_fp(stderr);
        free(cred->signature);
        EVP_MD_CTX_cleanup(&mdctx);
        EVP_PKEY_free(privkey);
        return ENODATA;
    }
    ret = EVP_SignFinal(&mdctx, cred->signature, &cred->sig_size, privkey);
    if (!ret)
    {
        ERR_print_errors_fp(stderr);
        free(cred->signature);
        EVP_MD_CTX_cleanup(&mdctx);
        EVP_PKEY_free(privkey);
        return ENODATA;
    }

    EVP_MD_CTX_cleanup(&mdctx);
    EVP_PKEY_free(privkey);

    return EXIT_SUCCESS;
}

static int write_credential(const PVFS_credential *cred, 
                            const struct passwd *pwd)
{
    char buf[sizeof(PVFS_credential)+extra_size_PVFS_credential] = { 0 };
    char *pptr = buf;
    int ret;

    if (isatty(STDOUT_FILENO))
    {
        fputs("error: stdout is a tty\n", stderr);
        return EIO;
    }

    encode_PVFS_credential(&pptr, cred);
    ret = safe_write(STDOUT_FILENO, buf, sizeof(buf));
    if (ret)
    {        
        perror("write");
        return ret;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    options_t opts;
    const struct passwd *pwd;
    const struct group *grp;
    uid_t euid;
    gid_t groups[PVFS_REQ_LIMIT_GROUPS];
    int ngroups;
    PVFS_credential credential;
    int ret = EXIT_SUCCESS;

    memset(&opts, 0, sizeof(opts));
    ret = parse_options(argc, argv, &opts);
    if (ret != EXIT_SUCCESS)
    {
        return ret;
    }
    
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    
    if (opts.user)
    {
        unsigned long val;
        char *endptr;

        val = strtoul(opts.user, &endptr, 10);
        if (*endptr == '\0' && *opts.user != '\0')
        {
            if (val > PVFS_UID_MAX)
            {
                pwd = NULL;
            }
            else
            {
                pwd = getpwuid((uid_t)val);
            }
        }
        else
        {
            pwd = getpwnam(opts.user);
        }
    }
    else
    {
        pwd = getpwuid(getuid());
    }
    if (pwd == NULL)
    {
        if (opts.user)
        {        
            fprintf(stderr, "unknown user -- %s\n", opts.user);
        }
        return EINVAL;
    }

    if (opts.group)
    {
        unsigned long val;
        char *endptr;

        val = strtoul(opts.group, &endptr, 10);
        if (*endptr == '\0' && *opts.group != '\0')
        {
            if (val > PVFS_GID_MAX)
            {
                grp = NULL;
            }
            else
            {
                grp = getgrgid((gid_t)val);
            }
        }
        else
        {
            grp = getgrnam(opts.group);
        }
    }
    else
    {
        grp = getgrgid(getgid());
    }
    if (grp == NULL)
    {
        if (opts.group)
        {
            fprintf(stderr, "unknown group -- %s\n", opts.group);
        }
        return EINVAL;
    }

    euid = getuid();
    if (euid && pwd->pw_uid != euid)
    {
        fprintf(stderr, "error: only %s and root can generate a credential "
                "for %s\n", pwd->pw_name, pwd->pw_name);
        return EPERM;
    }

    if (euid && grp->gr_gid != getgid())
    {
        fprintf(stderr, "error: cannot generate a credential for group %s: "
                "Permission denied\n", grp->gr_name);
        return EPERM;
    }

#ifdef HAVE_GETGROUPLIST

    ngroups = sizeof(groups)/sizeof(*groups);
    ret = getgrouplist(pwd->pw_name, grp->gr_gid, groups, &ngroups);
    if (ret == -1)
    {
        fprintf(stderr, "error: unable to get group list for user %s\n",
                pwd->pw_name);
        return ENOENT;
    }
    if (groups[0] != grp->gr_gid)
    {
        assert(groups[ngroups-1] == grp->gr_gid);
        groups[ngroups-1] = groups[0];
        groups[0] = grp->gr_gid;
    }

#else /* !HAVE_GETGROUPLIST */

    ngroups = sizeof(groups)/sizeof(*groups);
    ngroups = getugroups(ngroups, groups, pwd->pw_name, grp->gr_gid);
    if (ngroups == -1)
    {
        int err = errno;
        fprintf(stderr, "error: unable to get group list for user %s: %s\n",
                pwd->pw_name, strerror(errno));
        return err;
    }

#endif /* HAVE_GETGROUPLIST */
    
    ret = create_credential(pwd, 
                            groups, 
                            ngroups, 
#ifdef ENABLE_SECURITY_CERT
                            opts.certpath,
#endif
                            &credential);
    if (ret != EXIT_SUCCESS)
        goto main_exit;

#ifdef ENABLE_SECURITY_CERT
    if (opts.keypath == NULL)
    {
        opts.keypath = get_certificate_keypath(pwd);
    }
#endif

    ret = sign_credential(&credential, (opts.timeout ? (time_t)opts.timeout :
                          PVFS2_DEFAULT_CREDENTIAL_TIMEOUT), (opts.keypath ?
                          opts.keypath : PVFS2_DEFAULT_CREDENTIAL_KEYPATH));
    if (ret != EXIT_SUCCESS)
        goto main_exit;

    ret = write_credential(&credential, pwd);
    if (ret != EXIT_SUCCESS)
        goto main_exit;

main_exit:

    free(credential.issuer);
    free(credential.group_array);
#ifdef ENABLE_SECURITY_CERT
    free(credential.certificate.buf);
#endif

    return ret;
}

