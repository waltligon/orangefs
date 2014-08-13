/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * See COPYING in top-level directory.
 *
 * App to retrieve user certificate for security.
 *
 */


#ifndef WIN32
#include <unistd.h>
#include <pwd.h>
#else
#include <Windows.h>
#include <lmcons.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
#include "security-util.h"

struct options {
    char *userid;
    uint32_t exp;
};

#define USERID_PWD_LIMIT    256
#define STR_BUF_LEN         320

#define EAT_WS(str)    while (*str && (*str == ' ' || \
                              *str == '\t')) \
                           str++

struct user_info {
#ifndef WIN32
    struct passwd *passwd;
#endif
    char userid[USERID_PWD_LIMIT];
};

/* error code */
int last_error = 0;

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
            ERR_error_string_n(sslerr, errstr, sizeof(errstr));
            errstr[sizeof(errstr)-1] = '\0';
            gossip_err("OpenSSL error: %s\n", errstr);
        }
    }
    else if (err != 0)
    {
        errstr[0] = '\0';
        PVFS_strerror_r(err, errstr, sizeof(errstr));
        errstr[sizeof(errstr)-1] = '\0';

        fprintf(stderr, "Error: %s\n", errstr);
    }
}

#ifdef WIN32
/* get the directory where exe resides
   module_dir should be MAX_PATH */
static DWORD get_module_dir(char *module_dir)
{
    char *p;

    /* get exe path */
    if (!GetModuleFileName(NULL, module_dir, MAX_PATH))
        return GetLastError();

    /* remove exe file name */
    p = strrchr(module_dir, '\\');  
    if (p)
        *p = '\0';
  
    return 0;
}

static FILE *open_config_file(void)
{
    FILE *f = NULL;
    char *file_name = NULL, module_dir[MAX_PATH];
    DWORD ret = 0, malloc_flag = FALSE;

    /* environment variable overrides */
    file_name = getenv("ORANGEFS_CONFIG_FILE");
    if (file_name == NULL)
        file_name = getenv("PVFS2_CONFIG_FILE");
    if (file_name == NULL)
    {
        /* look for file in exe directory */
        ret = get_module_dir(module_dir);
        if (ret == 0)
        {
            file_name = (char *) malloc(MAX_PATH);
            malloc_flag = TRUE;
            strncpy(file_name, module_dir, MAX_PATH-14);
            strcat(file_name, "\\orangefs.cfg");            
        }
        else
        {
            fprintf(stderr, "%s: GetModuleFileName failed: %u\n", __func__, ret);
            return NULL;
        }
    }

    f = fopen(file_name, "r");
    if (f == NULL)
    {
        fprintf(stderr, "Could not open configuration file %s (%d)\n", 
            file_name, GetLastError());
    }

    if (malloc_flag)
    {
        free(file_name);
    }

    return f;
}
#endif

/* get_file_paths()
 *
 * Utility function to get private key and certificate paths.
 * keypath and certpath should be size MAX_PATH.
 */
int get_file_paths(const struct user_info *user_info, 
                   char *keypath,
                   char *certpath)
{
#ifndef WIN32
    const char def_keyfile[] = "/.pvfs2-cert-key.pem";
    const char def_certfile[] = "/.pvfs2-cert.pem";
    char *envvar;

    envvar = getenv("PVFS2KEY_FILE");
    if (envvar != NULL)
    {
        strncpy(keypath, envvar, MAX_PATH);
        keypath[MAX_PATH-1] = '\0';
    }
    else
    {    
        /* construct certificate private key file path */
        strncpy(keypath, user_info->passwd->pw_dir, MAX_PATH);
        keypath[MAX_PATH-1] = '\0';
        if ((strlen(keypath) + strlen(def_keyfile)) < (MAX_PATH - 1))
        {
            strcat(keypath, def_keyfile);
        }
        else
        {
            return NULL;
        }
    }

    envvar = getenv("PVFS2CERT_FILE");
    if (envvar != NULL)
    {
        strncpy(certpath, envvar, MAX_PATH);
        certpath[MAX_PATH-1] = '\0';
    }
    else 
    {
        /* construct certificate file path */
        strncpy(certpath, user_info->passwd->pw_dir, MAX_PATH);
        certpath[MAX_PATH-1] = '\0';
        if ((strlen(certpath) + strlen(def_certfile)) < (MAX_PATH - 1))
        {
            strcat(certpath, def_certfile);
        }
        else
        {
            return -PVFS_EOVERFLOW;
        }
    }
#else
    FILE *f;
    char line[STR_BUF_LEN], *pline, keyword[STR_BUF_LEN],
        args[STR_BUF_LEN], conf_keypath[MAX_PATH], conf_certpath[MAX_PATH];
    int i, ret;

    /* open OrangeFS configuration file */
    f = open_config_file();
    if (f == NULL)
    {
        /* error message already logged */
        return -PVFS_ENOENT;
    }

    conf_keypath[0] = conf_certpath[0] = '\0';
    while (!feof(f))
    {
        line[0] = '\0';
        fgets(line, sizeof(line), f);
        line[sizeof(line)-1] = '\0';

        /* remove \n */        
        if (strlen(line) > 0 && line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = '\0';
        
        /* skip whitespace and comments */
        pline = line;
        EAT_WS(pline);
        if (!(*pline) || *pline == '#')
            continue;

        /* get keyword */
        i = 0;
        while (*pline && (*pline != ' ' || *pline == '\t') &&
               (i < sizeof(keyword)-1))
        {
            keyword[i++] = *pline++;
        }
        keyword[i] = '\0';

        /* only look for specific keywords */
        if (stricmp(keyword, "key-file") && stricmp(keyword, "cert-file"))
            continue;

        /* get arguments */
        EAT_WS(pline);
        i = 0;
        while (*pline && (i < sizeof(args)-1))
        {
            args[i++] = *pline++;
        }
        args[i] = '\0';

        /* copy keyword values */
        if (strlen(args)+1 > MAX_PATH)
        {
            fprintf(stderr, "%s too long\n", keyword);
            fclose(f);
            return -PVFS_EOVERFLOW;
        }

        if (!stricmp(keyword, "key-file"))
        {
            strcpy(conf_keypath, args);
        }
        else if (!stricmp(keyword, "cert-file"))
        {
            strcpy(conf_certpath, args);
        }
    }

    fclose(f);

    if (strlen(conf_keypath) == 0)
    {
        /* TODO: default--use profile dir? */
    }

    if (strlen(conf_certpath) == 0)
    {
        /* TODO: default? */
    }

    /* get security paths */
    ret = PINT_get_security_path(conf_keypath, user_info->userid, 
        keypath, MAX_PATH);
    if (ret != 0)
    {
        /* TODO: use pvfs_perror? */
        fprintf(stderr, "Could not process path: %s (%d)\n", conf_keypath,
            ret);
        return ret;
    }

    ret = PINT_get_security_path(conf_certpath, user_info->userid, 
        certpath, MAX_PATH);
    if (ret != 0)
    {
        /* TODO: use pvfs_perror? */
        fprintf(stderr, "Could not process path: %s (%d)\n", conf_keypath,
            ret);
        return ret;
    }
    
#endif
    
    return 0;
}

/* fill in current user id plus passwd info on *NIX */
int get_user_info(struct user_info *user_info)
{
#ifdef WIN32
    DWORD buflen = USERID_PWD_LIMIT;
#endif

    if (user_info == NULL)
    {
        return -PVFS_EINVAL;
    }

#ifndef WIN32
    errno = 0;
    user_info->passwd = getpwuid(getuid());
    if (user_info->passwd == NULL)
    {
        return errno;
    }

    strncpy(user_info->userid, user_info->passwd->pw_name, USERID_PWD_LIMIT);
#else
    if (!GetUserName(user_info->userid, &buflen))
    {
        return (int) last_error;
    }
#endif

    return 0;
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
    char keypath[MAX_PATH], certpath[MAX_PATH];
    struct user_info user_info;
    int ret;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: storing user cert and key\n", 
                 __func__);

    /* retrieve user information struct */
    ret = get_user_info(&user_info);
    if (ret != 0)
    {
        gossip_err("Error: could not retrieve user info: %d\n", ret);
        return ret;
    }

    /* get paths */
    ret = get_file_paths(&user_info, keypath, certpath);
    if (ret != 0)
    {
        fprintf(stderr, "Could not get key/certificate file path (%d)\n", ret);
        return ret;
    }

    /* convert cert to X509 format */
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: user cert size: %d\n", __func__, 
                 cert->buf_size);
    ret = PINT_cert_to_X509(cert, &xcert);
    if (ret != 0) {        
        sec_error("Error: could not convert cert to X509 format", ret);
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

#ifndef WIN32
    /* set user rw permissions on cert file */
    if (chmod(certpath, S_IRUSR|S_IWUSR) != 0)
    {
        sec_error("Warning: could not chmod user cert file", errno);
    }
#endif

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

#ifndef WIN32
    /* set user rw permissions on key file */
    if (chmod(keypath, S_IRUSR|S_IWUSR) != 0)
    {
        sec_error("Warning: could not chmod user private key file",
                          errno);
    }
#endif

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

#ifdef WIN32
/* get path to pvfs2 tabfile */
int get_tab_file(char *tabfile)
{
    char exe_path[MAX_PATH], *p;
    DWORD ret = 0;
    FILE *f;

    /* locate tabfile -- env. variable overrides */
    if (getenv("PVFS2TAB_FILE"))
    {
        strncpy(tabfile, getenv("PVFS2TAB_FILE"), MAX_PATH);
        tabfile[MAX_PATH-1] = '\0';
    }
    else
    {
        if (GetModuleFileName(NULL, exe_path, MAX_PATH))
        {
            /* get directory */
            p = strrchr(exe_path, '\\');
            if (p)
                *p = '\0';

            if (strlen(exe_path) + strlen("\\orangefstab") + 1 > MAX_PATH)
            {
                return ERROR_BUFFER_OVERFLOW;
            }
            
            strcpy(tabfile, exe_path);
            strcat(tabfile, "\\orangefstab");
            
            /* attempt to open file */
            f = fopen(tabfile, "r");
            if (f)
                fclose(f);
            else 
            {
                /* switch to pvfs2tab -- PVFS_sys_initialize will fail if not valid */
                strcpy(tabfile, exe_path);
                strcat(tabfile, "\\pvfs2tab");
            }
        }
        else
        {
            ret = GetLastError();
        }
    }

    return (int) ret;
}
#endif

int main(int argc, char **argv)
{
    struct options options;
    char userid[USERID_PWD_LIMIT], pwd[USERID_PWD_LIMIT], input[8];
    const PVFS_util_tab* tab;
    int ret, valid, quit, fs_num, init_flag = 0, addr_count;
    PVFS_BMI_addr_t *addr_array;
    struct user_info user_info;
    PVFS_certificate cert;
    PVFS_security_key privkey;
#ifndef WIN32
    int i;
#else
    char tabfile[MAX_PATH];
#endif

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
        ret = get_user_info(&user_info);
        if (ret != 0)
        {
            gossip_err("Error: could not retrieve passwd user info: %d\n", 
                       last_error);
            goto exit_main;
        }

        userid[0] = '\0';
        printf("Enter username [%s]: ", user_info.userid);
        scanf("%[^\r\n]%*c", userid);

        /* use default */
        if (strlen(userid) == 0)
        {
            strcpy(userid, user_info.userid);
        }
    }

    if (strlen(userid) == 0)
    {
        fprintf(stderr, "No username specified... exiting\n");
        goto exit_main;
    }

    printf("Using username %s...\n", userid);

    /* prompt for password */
    EVP_read_pw_string(pwd, USERID_PWD_LIMIT, "Enter file system password: ", 0);

    /* init OrangeFS - open tabfile */
#ifndef WIN32
    tab = PVFS_util_parse_pvfstab(NULL);
#else
    ret = get_tab_file(tabfile);
    if (ret != 0)
    {
        fprintf(stderr, "Could not get tabfile path (%d)\n", ret);
        goto exit_main;
    }
    
    tab = PVFS_util_parse_pvfstab(tabfile);
#endif

    if (tab == NULL)
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
        return 1;
    }

    /* initialize sysint interface */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_initialize", ret);
        return 1;
    }

    init_flag = 1;

    /* prompt if multiple file systems */
    valid = quit = fs_num = 0;    
    if (tab->mntent_count > 1)
    {
#ifndef WIN32
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
#else
        fprintf(stderr, "Multiple file systems detected - not supported\n");
        goto exit_main;
#endif
    }

    if (quit)
    {
        goto exit_main;
    }

    /* init selected file system */
    printf("Using %s...\n", tab->mntent_array[fs_num].pvfs_config_servers[0]);

    /* init file system */
    ret = PVFS_sys_fs_add(&tab->mntent_array[fs_num]);
    if (ret != 0 && ret != -PVFS_EEXIST)
    {
        PVFS_perror("PVFS_sys_fs_add", ret);
        goto exit_main;
    }

    /* get server addresses */
    ret = PVFS_mgmt_count_servers(tab->mntent_array[fs_num].fs_id,
                                  PINT_SERVER_TYPE_ALL,
                                  &addr_count);
    if (ret != 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers", ret);
        goto exit_main;
    }

    if (addr_count == 0)
    {
        fprintf(stderr, "Unable to load any server addresses\n");
        goto exit_main;
    }

    addr_array = (PVFS_BMI_addr_t *) 
                 malloc(addr_count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
        PVFS_perror("Memory error", -PVFS_ENOMEM);
        goto exit_main;
    }

    ret = PVFS_mgmt_get_server_array(tab->mntent_array[fs_num].fs_id, 
                                     PINT_SERVER_TYPE_ALL, 
                                     addr_array,
                                     &addr_count);
    if (ret != 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array", ret);
        goto exit_main;
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

exit_main:

    /* finalize PVFS */
    if (init_flag)
    {
        PVFS_sys_finalize();
    }

    /* finalize OpenSSL */
    EVP_cleanup();
    ERR_free_strings();

    /* keep Windows console open to view error messages */
#ifdef WIN32
    EVP_read_pw_string(input, 8, "Press ENTER to exit...", 0);
#endif

    return (ret != 0) ? 1 : 0;
}
