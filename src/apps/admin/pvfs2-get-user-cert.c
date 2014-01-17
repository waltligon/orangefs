/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * See COPYING in top-level directory.
 *
 * App to retrieve user certificate for security.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pint-cached-config.h"
#include "str-utils.h"
#include "pvfs2-internal.h"
#include "cert-util.h"

struct options {
    char *userid;
    uint32_t exp;
};

#define USERID_PWD_LIMIT    256

void sec_error(const char *msg, int err)
{
    unsigned long sslerr;
    char errstr[256];

    gossip_err("%s\n", msg);

    if (err == -PVFS_ESECURITY)
    {
        /* debug OpenSSL error queue */
        while ((sslerr = ERR_get_error()) != 0)
        {
            ERR_error_string_n(sslerr, errstr, 256);
            errstr[255] = '\0';
            gossip_err("OpenSSL error: %s\n", errstr);
        }
    }
    else if (err != 0)
    {
        PVFS_perror("Error", err);
    }
}

/* get_key_path()
 * 
 * Utility function to get the path to the user private key file.
 */
char *get_key_path(const struct passwd *passwd, char *keypath)
{
    const char def_keyfile[] = "/.pvfs2-cert-key.pem";
    char *envvar;

    envvar = getenv("PVFS2KEY_FILE");
    if (envvar != NULL)
    {
        strncpy(keypath, envvar, PATH_MAX);
        keypath[PATH_MAX-1] = '\0';
    }
    else
    {    
        /* construct certificate private key file path */
        strncpy(keypath, passwd->pw_dir, PATH_MAX);
        keypath[PATH_MAX-1] = '\0';
        if ((strlen(keypath) + strlen(def_keyfile)) < (PATH_MAX - 1))
        {
            strcat(keypath, def_keyfile);
        }
        else
        {
            return NULL;
        }
    }

    return keypath;
}

/* get_cert_path()
 * 
 * Utility function to get path to the user cert file.
 */
char *get_cert_path(const struct passwd *passwd, char *certpath)
{
    const char def_certfile[] = "/.pvfs2-cert.pem";
    char *envvar;

    envvar = getenv("PVFS2CERT_FILE");
    if (envvar != NULL)
    {
        strncpy(certpath, envvar, PATH_MAX);
        certpath[PATH_MAX-1] = '\0';
    }
    else 
    {
        /* construct certificate private key file path */
        strncpy(certpath, passwd->pw_dir, PATH_MAX);
        certpath[PATH_MAX-1] = '\0';
        if ((strlen(certpath) + strlen(def_certfile)) < (PATH_MAX - 1))
        {
            strcat(certpath, def_certfile);
        }
        else
        {
            return NULL;
        }
    }

    return certpath;
}

/* store_cert_and_key()
 *
 * Write user cert and private key to disk.
 */
int store_cert_and_key(PVFS_certificate *cert, PVFS_security_key *key)
{
    X509 *xcert = NULL;
    RSA *rsa_privkey;
    PVFS_key_data keybuf;
    EVP_PKEY *privkey;
    char certpath[PATH_MAX], keypath[PATH_MAX];
    struct passwd *passwd;
    int ret;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: storing user cert and key\n", 
                 __func__);

    /* retrieve passwd information struct */
    errno = 0;
    passwd = getpwuid(getuid());
    if (passwd == NULL)
    {
        gossip_err("Error: could not retrieve passwd user info: %d\n", errno);
        return errno;
    }

    /* convert cert to X509 format */
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: user cert size: %d\n", __func__, 
                 cert->buf_size);
    ret = PINT_cert_to_X509(cert, &xcert);
    if (ret != 0) {        
        sec_error("Error: could not convert cert to X509 format", ret);
        return ret;
    }

    /* get cert file path */
    if (get_cert_path(passwd, certpath) == NULL)
    {
        sec_error("Error: could not load user cert. path", -PVFS_EINVAL);
        ret = -PVFS_EINVAL;
        return ret;
    }

    /* write cert to disk */
    ret = PINT_save_cert_to_file(certpath, xcert);

    X509_free(xcert);

    if (ret != 0)
    {
        sec_error("Error: could not save user cert. to file", ret);
        gossip_err("User cert. path: %s\n", certpath);

        return ret;
    }

    /* set user rw permissions on cert file */
    if (chmod(certpath, S_IRUSR|S_IWUSR) != 0)
    {
        sec_error("Warning: could not chmod user cert file", errno);
    }

    /* convert key to OpenSSL format */
    keybuf = key->buf;
    rsa_privkey = d2i_RSAPrivateKey(NULL, 
        (const unsigned char **) &keybuf,
        key->buf_size);

    if (rsa_privkey == NULL)
    {
        sec_error("Error: could not load user private key", -PVFS_ESECURITY);
        ret = -PVFS_ESECURITY;

        return ret;
    }

    /* get key file path */
    if (get_key_path(passwd, keypath) == NULL)
    {
        sec_error("Error: could not load user private key path", -PVFS_EINVAL);
        ret = -PVFS_EINVAL;
        RSA_free(rsa_privkey);

        return ret;
    }

    /* setup EVP key */
    privkey = EVP_PKEY_new();
    if (privkey == NULL)
    {
        sec_error("Error: could not allocate EVP_PKEY object", -PVFS_ESECURITY);
        ret = -PVFS_ESECURITY;
        RSA_free(rsa_privkey);

        return ret;
    }

    if (!EVP_PKEY_assign_RSA(privkey, rsa_privkey))
    {
        sec_error("Error: could not assign RSA key", -PVFS_ESECURITY);
        ret = -PVFS_ESECURITY;
        EVP_PKEY_free(privkey);
        RSA_free(rsa_privkey);

        return ret;
    }

    /* save key to file */
    ret = PINT_save_privkey_to_file(keypath, privkey);

    /* will also free RSA object */
    EVP_PKEY_free(privkey);

    if (ret != 0)
    {
        sec_error("Error: could not save user private key to file", ret);
        gossip_err("User private key path: %s\n", keypath);

        return ret;
    }

    /* set user rw permissions on key file */
    if (chmod(keypath, S_IRUSR|S_IWUSR) != 0)
    {
        sec_error("Warning: could not chmod user private key file",
                          errno);
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: user cert saved to %s\n",
                 __func__, certpath);
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: user private key saved to %s\n",
                 __func__, keypath);

    return ret;
}

int parse_args(int argc, char **argv, struct options *options)
{
    int argi;

    if (options == NULL || argv == NULL)
    {
        return -1;
    }

    options->userid = NULL;
    options->exp = 0;

    for (argi = 1; argi < argc; argi++)
    {
        if (!strcmp(argv[argi], "-h") || !(strcmp(argv[argi], "--help")))
        {
            return 1;
        }
        if (!strcmp(argv[argi], "-d"))
        {
            options->exp = strtol(argv[++argi], NULL, 10);
            if (errno == ERANGE)
                return 1;
        }
        else if (argv[argi][0] == '-')
        {
            fprintf(stderr, "Invalid option: %s\n", argv[argi]);
            return -1;
        }
        else
        {
            options->userid = argv[argi];
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct options options;
    char userid[USERID_PWD_LIMIT], pwd[USERID_PWD_LIMIT], input[8];
    const PVFS_util_tab* tab;
    int ret, i, valid, quit, fs_num, addr_count;
    PVFS_BMI_addr_t *addr_array;
    struct passwd *passwd = NULL;
    PVFS_certificate cert;
    PVFS_security_key privkey;

    if (parse_args(argc, argv, &options) != 0)
    {
        fprintf(stderr, "USAGE: %s [-h|--help] [-d expiration time] [username]\n", argv[0]);
        fprintf(stderr, "   Requests certificate and private key from OrangeFS file system.\n"
                        "   Expiration time is in minutes.\n"
                        "   Files are stored in user's home directory.\n");
        return 1;
    }    

    /* init OpenSSL */
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    /* prompt for userid if necessary */
    if (options.userid != NULL)
    {
        strncpy(userid, options.userid, USERID_PWD_LIMIT);
        userid[USERID_PWD_LIMIT-1] = '\0';
    }
    else
    {
        /* get current userid as default */
        passwd = getpwuid(getuid());
        if (passwd == NULL)
        {
            gossip_err("Error: could not retrieve passwd user info: %d\n", 
                       errno);
            return 1;
        }

        userid[0] = '\0';
        printf("Enter username [%s]: ", passwd->pw_name);        
        scanf("%[^\r\n]%*c", userid);

        /* use default */
        if (strlen(userid) == 0)
        {
            strcpy(userid, passwd->pw_name);
        }
    }

    if (strlen(userid) == 0)
    {
        fprintf(stderr, "No username specified... exiting\n");
        return 1;
    }

    printf("Using username %s...\n", userid);

    /* prompt for password */
    EVP_read_pw_string(pwd, USERID_PWD_LIMIT, "Enter file system password: ", 0);

    /* init PVFS - setup addr list */
    tab = PVFS_util_parse_pvfstab(NULL);
    if (tab == NULL)
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
        return 1;
    }

    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_initialize", ret);
        return 1;
    }

    /* prompt if multiple file systems */
    valid = quit = fs_num = 0;    
    if (tab->mntent_count > 1)
    {
        do
        {
            printf("\n");        
            for (i = 0; i < tab->mntent_count; i++)
            {
                printf("[%d] %s/%s (%s)\n", 
                       i+1,
                       tab->mntent_array[i].pvfs_config_servers[0],
                       tab->mntent_array[i].pvfs_fs_name,
                       tab->mntent_array[i].mnt_dir);
            }
            printf("Select file system [1-%d] or [q]uit: ",
                   tab->mntent_count);
            scanf("%7s", input);

            fs_num = atoi(input) - 1;
            quit = input[0] == 'q' || input[0] == 'Q';
            valid = (strlen(input) > 0) &&
                    (quit ||
                     (fs_num >= 0 && fs_num < tab->mntent_count));
            if (!valid)
            {
                fprintf(stderr, "Invalid input\n");
            }
        } while (!valid);
    }

    if (quit)
    {
        PVFS_sys_finalize();
        return 1;
    }

    printf("Using %s...\n", tab->mntent_array[fs_num].pvfs_config_servers[0]);

    /* init file system */
    ret = PVFS_sys_fs_add(&tab->mntent_array[fs_num]);
    if (ret != 0 && ret != -PVFS_EEXIST)
    {
        PVFS_perror("PVFS_sys_fs_add", ret);
        PVFS_sys_finalize();
        return 1;
    }

    /* get server addresses */
    ret = PVFS_mgmt_count_servers(tab->mntent_array[fs_num].fs_id,
                                  PINT_SERVER_TYPE_ALL,
                                  &addr_count);
    if (ret != 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers", ret);
        PVFS_sys_finalize();
        return 1;
    }

    if (addr_count == 0)
    {
        fprintf(stderr, "Unable to load any server addresses\n");
        PVFS_sys_finalize();
        return 1;
    }

    addr_array = (PVFS_BMI_addr_t *) 
                 malloc(addr_count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
        PVFS_perror("Memory error", -PVFS_ENOMEM);
        PVFS_sys_finalize();
        return 1;
    }

    ret = PVFS_mgmt_get_server_array(tab->mntent_array[fs_num].fs_id, 
                                     PINT_SERVER_TYPE_ALL, 
                                     addr_array,
                                     &addr_count);
    if (ret != 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array", ret);
        PVFS_sys_finalize();
        return 1;
    }

    /* send get-user-cert request */
    ret = PVFS_mgmt_get_user_cert(tab->mntent_array[fs_num].fs_id,
                                  userid, pwd, (uint32_t) addr_count,
                                  addr_array, &cert, &privkey, options.exp);
    if (ret == 0)
    {
        ret = store_cert_and_key(&cert, &privkey);
    }
    else
    {
        
        fprintf(stderr, "Error retrieving certificate and key:\n");
                
        if (ret == -PVFS_EACCES || ret == -PVFS_ENOENT)
        {
            fprintf(stderr, "   Invalid username or password\n");
            ret = 1;
        }
        else
        {
            PVFS_perror("   Error", ret);
            ret = 1;                        
        }
        fprintf(stderr, "Contact your system administrator for assistance.\n");
    }

    if (ret == 0)
    {
        printf("User certificate and private key stored.\n");
    }

    PINT_cleanup_cert(&cert);

    PINT_cleanup_key(&privkey);

    free(addr_array);

    /* finalize PVFS */
    PVFS_sys_finalize();

    /* finalize OpenSSL */
    EVP_cleanup();
    ERR_free_strings();

    return (ret != 0) ? 1 : 0;
}

