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

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2-types.h"
#include "src/proto/pvfs2-req-proto.h"


/* nlmills: TODO: move these somewhere sane */
#define DEFAULT_CREDENTIAL_TIMEOUT (15*60)
#define DEFAULT_CREDENTIAL_KEYPATH SYSCONFDIR "/pvfs2credkey.pri"


typedef struct {
    const char *user;
    int timeout;
    const char *keypath;
} options_t;


static void usage(void)
{
    puts("usage: pvfs2-gencred [-u user] [-t timeout] [-k keyfile]"); 
}

static int safe_write(int fd, const void *buf, size_t nbyte)
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
            return -1;
        }
    }

    return 0;
}

static int parse_options(int argc, char **argv, options_t *opts)
{
    int ch;
    
    while ((ch = getopt(argc, argv, "u:t:k:")) != -1)
    {
        switch(ch)
        {
            case 'u':
                opts->user = optarg;
                break;
            case 't':
                opts->timeout = strtol(optarg, NULL, 10);
                if (opts->timeout <= 0)
                {
                    fprintf(stderr, "%s: illegal timeout -- %s\n", argv[0],
                            optarg);
                    usage();
                    return EXIT_FAILURE;
                }
                break;
            case 'k':
                opts->keypath = optarg;
                break;
            case '?':
            default:
                usage();
                return EXIT_FAILURE;
        }
    }
    
    return EXIT_SUCCESS;
}

static int create_credential(const struct passwd *pwd, const gid_t *groups,
    int ngroups, PVFS_credential *cred)
{
    char hostname[HOST_NAME_MAX+1];
    char *issuer;
    int i;

    memset(cred, 0, sizeof(*cred));

    issuer = calloc(PVFS_REQ_LIMIT_ISSUER+1, 1);
    if (issuer == NULL)
    {
        return EXIT_FAILURE;
    }
    gethostname(hostname, HOST_NAME_MAX);
    hostname[sizeof(hostname)-1] = '\0';
    strncpy(issuer, hostname, PVFS_REQ_LIMIT_ISSUER);

    cred->userid = (PVFS_uid)pwd->pw_uid;
    cred->num_groups = (uint32_t)ngroups;
    cred->group_array = calloc(ngroups, sizeof(PVFS_gid));
    if (cred->group_array == NULL)
    {
        free(issuer);
        return EXIT_FAILURE;
    }
    for (i = 0; i < ngroups; i++)
    {
        cred->group_array[i] = (PVFS_gid)groups[i];
    }
    cred->issuer = issuer;

    return EXIT_SUCCESS;
}

static int sign_credential(PVFS_credential *cred, time_t timeout,
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
        perror(keypath);
        return EXIT_FAILURE;
    }

    ret = fstat(fileno(keyfile), &stats);
    if (ret == -1)
    {
        perror("stat");
        fclose(keyfile);
        return EXIT_FAILURE;
    }
    if (stats.st_mode & (S_IROTH | S_IWOTH))
    {
        fprintf(stderr, "warning: insecure permissions on private key file "
                "%s\n", keypath);
    }

    privkey = PEM_read_PrivateKey(keyfile, NULL, NULL, NULL);
    if (privkey == NULL)
    {
        ERR_print_errors_fp(stderr);
        fclose(keyfile);
        return EXIT_FAILURE;
    }

    fclose(keyfile);

    cred->timeout = (PVFS_time)(time(NULL) + timeout);
    cred->signature = malloc(EVP_PKEY_size(privkey));
    if (cred->signature == NULL)
    {
        EVP_PKEY_free(privkey);
        return EXIT_FAILURE;
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
        return EXIT_FAILURE;
    }
    ret = EVP_SignFinal(&mdctx, cred->signature, &cred->sig_size, privkey);
    if (!ret)
    {
        ERR_print_errors_fp(stderr);
        free(cred->signature);
        EVP_MD_CTX_cleanup(&mdctx);
        EVP_PKEY_free(privkey);
        return EXIT_FAILURE;
    }

    EVP_MD_CTX_cleanup(&mdctx);
    EVP_PKEY_free(privkey);

    return EXIT_SUCCESS;
}

static int write_credential(const PVFS_credential *cred, 
    const struct passwd *pwd)
{
    char buf[sizeof(PVFS_credential)+extra_size_PVFS_credential];
    char *pptr = buf;
    int ret;

    if (isatty(STDOUT_FILENO))
    {
        fputs("error: stdout is a tty\n", stderr);
        return EXIT_FAILURE;
    }

    encode_PVFS_credential(&pptr, cred);
    ret = safe_write(STDOUT_FILENO, buf, sizeof(buf));
    if (ret == -1)
    {
        perror("write");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    options_t opts = { NULL, 0, NULL };
    const struct passwd *pwd;
    gid_t groups[PVFS_REQ_LIMIT_GROUPS];
    int ngroups;
    PVFS_credential credential;
    int ret;
    
    ret = parse_options(argc, argv, &opts);
    if (ret != EXIT_SUCCESS)
    {
        return ret;
    }
    
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    
    pwd = opts.user ? getpwnam(opts.user) : getpwuid(getuid());
    if (pwd == NULL)
    {
        fprintf(stderr, "unknown user -- %s\n", opts.user);
        return EXIT_FAILURE;
    }
    
    if (getuid() && pwd->pw_uid != getuid())
    {
        fprintf(stderr, "error: only %s and root can generate a credential "
                "for %s\n", pwd->pw_name, pwd->pw_name);
        return EXIT_FAILURE;
    }
    
    /* nlmills: TODO: fall back to getugroups */
    
    ngroups = sizeof(groups)/sizeof(*groups);
    ret = getgrouplist(pwd->pw_name, pwd->pw_gid, groups, &ngroups);
    if (ret == -1)
    {
        fprintf(stderr, "error: unable to get group list for user %s\n",
                pwd->pw_name);
        return EXIT_FAILURE;
    }
    if (groups[0] != pwd->pw_gid)
    {
        assert(groups[ngroups-1] == pwd->pw_gid);
        groups[ngroups-1] = groups[0];
        groups[0] = pwd->pw_gid;
    }
    
    ret = create_credential(pwd, groups, ngroups, &credential);
    if (ret != EXIT_SUCCESS)
    {
        return ret;
    }

    ret = sign_credential(&credential, (opts.timeout ? (time_t)opts.timeout :
                          DEFAULT_CREDENTIAL_TIMEOUT), (opts.keypath ?
                          opts.keypath : DEFAULT_CREDENTIAL_KEYPATH));
    if (ret != EXIT_SUCCESS)
    {
        return ret;
    }

    ret = write_credential(&credential,  pwd);
    if (ret != EXIT_SUCCESS)
    {
        return ret;
    }
    
    return EXIT_SUCCESS;
}

