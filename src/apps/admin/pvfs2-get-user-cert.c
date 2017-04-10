/*
 * (C) 2013-2014 Clemson University and Omnibond Systems LLC
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
#include <UserEnv.h>
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
    char *fs_userid;
    uint32_t exp;
    char *keypath;
    char *certpath;
#ifdef WIN32
    int system_flag;
#endif
};

#ifndef MAX_PATH
#define MAX_PATH            260
#endif

#define USERID_PWD_LIMIT    256
#define STR_BUF_LEN         320

#define EAT_WS(str)    while (*str && (*str == ' ' || \
                              *str == '\t')) \
                           str++

#ifdef WIN32
#define DEFAULT_KEYFILE          "orangefs-cert-key.pem"
#define DEFAULT_KEYFILE_LEN      21
#define DEFAULT_CERTFILE         "orangefs-cert.pem"
#define DEFAULT_CERTFILE_LEN     17
#endif

int get_current_userid(char *userid, int buflen);

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
    {
        fprintf(stderr, "Error: GetModuleFileName returned %d\n",
            GetLastError());
        return -PVFS_EINVAL;
    }

    /* remove exe file name */
    p = strrchr(module_dir, '\\');  
    if (p)
    {
        *p = '\0';
    }
  
    return 0;
}

/* Open configuration file -- not strictly necessary, as 
   it can use default values */
static FILE *open_config_file(void)
{
    FILE *f = NULL;
    char *file_name = NULL, module_dir[MAX_PATH];
    DWORD ret = 0, malloc_flag = FALSE;

    /* environment variable overrides */
    file_name = getenv("ORANGEFS_CONFIG_FILE");
    if (file_name == NULL)
    {
        file_name = getenv("PVFS2_CONFIG_FILE");
    }
    if (file_name == NULL)
    {
        /* look for file in exe directory */
        ret = get_module_dir(module_dir);
        if (ret == 0)
        {
            file_name = (char *) malloc(MAX_PATH);
            if (file_name == NULL)
            {
                fprintf(stderr, "Error: %s - memory error\n", __func__);
                return NULL;
            }
            malloc_flag = TRUE;
            strncpy(file_name, module_dir, MAX_PATH-14);
            strcat(file_name, "\\orangefs.cfg");            
        }
    }

    if (file_name != NULL)
    {
        f = fopen(file_name, "r");
    }

    if (malloc_flag)
    {
        free(file_name);
    }

    return f;
}
#endif /* WIN32 */

/* return path with filename appended to current user's profile directory */
int get_default_path(const char *filename,
                     char *path)
{
#ifndef WIN32
    /* TODO */
#else
    HANDLE h_token = INVALID_HANDLE_VALUE;
    char profile_dir[MAX_PATH];
    DWORD size = MAX_PATH;    
#endif /* WIN32 */

    if (filename == NULL || path == NULL)
    {
        return -PVFS_EINVAL;
    }

#ifndef WIN32
    /* TODO */
#else

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &h_token))
    {
        fprintf(stderr, "OpenProcessToken error: %d\n", GetLastError());
        return -PVFS_EINVAL;
    }

    if (!GetUserProfileDirectory(h_token, profile_dir, &size))
    {
        fprintf(stderr, "GetUserProfileDirectory error: %d\n", GetLastError());
        CloseHandle(h_token);
        return -PVFS_EINVAL;
    }

    CloseHandle(h_token);
        
    if (strlen(profile_dir) + strlen(filename) + 2 > MAX_PATH)
    {
        fprintf(stderr, "Profile directory path is too long\n");
        return -PVFS_ENAMETOOLONG; /* TODO */
    }

    strcpy(path, profile_dir);
    strcat(path, "\\");
    strcat(path, filename);
#endif /* WIN32 */

    return 0;
}

/* get_file_paths()
 *
 * Utility function to get private key and certificate paths.
 * keypath and certpath should be size MAX_PATH.
 */
int get_file_paths(struct options *options,
                   char *keypath,
                   char *certpath)
{
#ifndef WIN32
    const char def_keyfile[] = "/.pvfs2-cert-key.pem";
    const char def_certfile[] = "/.pvfs2-cert.pem";
    char *envvar;
    struct passwd *passwd;
#else
    FILE *f;
    char line[STR_BUF_LEN], *pline, keyword[STR_BUF_LEN],
        args[STR_BUF_LEN], conf_keypath[MAX_PATH], conf_certpath[MAX_PATH],
        local_userid[USERID_PWD_LIMIT];
    int i, ret;
    DWORD size = MAX_PATH;
#endif /* WIN32 */

#ifndef WIN32
    keypath[0] = certpath[0] = '\0';

    if (options->keypath != NULL)
    {
        strncpy(keypath, options->keypath, MAX_PATH);
    }
    else
    {
        envvar = getenv("PVFS2KEY_FILE");
        if (envvar != NULL)
        {
            strncpy(keypath, envvar, MAX_PATH);
            keypath[MAX_PATH-1] = '\0';
        }
    }

    if (options->certpath != NULL)
    {
        strncpy(certpath, options->certpath, MAX_PATH);
    }
    else
    {
        envvar = getenv("PVFS2CERT_FILE");
        if (envvar != NULL)
        {
            strncpy(certpath, envvar, MAX_PATH);
            certpath[MAX_PATH-1] = '\0';
        }
    }

    if (strlen(keypath) == 0 || strlen(certpath) == 0)
    {
        /* store file(s) in current user's home directory */
        passwd = getpwuid(getuid());
        if (passwd == NULL)
        {
            fprintf(stderr, "Error: Could not retrieve passwd info for "
                    "current user\n");
            return -PVFS_ENOENT;
        }

        if (strlen(keypath) == 0)
        {    
            /* construct certificate private key file path */
            strncpy(keypath, passwd->pw_dir, MAX_PATH);
            keypath[MAX_PATH-1] = '\0';
            if ((strlen(keypath) + strlen(def_keyfile)) < (MAX_PATH - 1))
            {
                strcat(keypath, def_keyfile);
            }
            else
            {
                return -PVFS_EOVERFLOW;
            }
        }
    
        if (strlen(certpath) == 0) 
        {
            /* construct certificate file path */
            strncpy(certpath, passwd->pw_dir, MAX_PATH);
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
    }
#else
    /* if specified as options, copy */
    keypath[0] = certpath[0] = '\0';

    if (options->system_flag)
    {
        /* return paths for SYSTEM user, to be stored in the module dir */
        if ((ret = get_module_dir(keypath)) != 0)
        {
            /* error already reported */
            return ret;
        }
        strcpy(certpath, keypath);

        strcat(keypath, "\\");
        strcat(keypath, DEFAULT_KEYFILE);

        strcat(certpath, "\\");
        strcat(certpath, DEFAULT_CERTFILE);

        return 0;
    }

    if (options->keypath != NULL)
    {
        strncpy(keypath, options->keypath, MAX_PATH);
    }
    if (options->certpath != NULL)
    {
        strncpy(certpath, options->certpath, MAX_PATH);
    }

    /* we're done if both paths defined */
    if (strlen(keypath) > 0 && strlen(certpath) > 0)
    {
        return 0;
    }

    /* otherwise use OrangeFS configuration file (usually 
       C:\OrangeFS\Client\orangefs.cfg) */
    conf_keypath[0] = conf_certpath[0] = '\0';
    f = open_config_file();
    while (f && !feof(f))
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
            fprintf(stderr, "Error: %s too long\n", keyword);
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

    if (f != NULL)
    {
        fclose(f);
    }

    /* keypath needs definition */
    if (strlen(keypath) == 0)
    {
        if (strlen(conf_keypath) > 0)
        {
            /* get security paths - this replaces %USERNAME% strings in 
               the paths with the *LOCAL* userid */
            ret = get_current_userid(local_userid, USERID_PWD_LIMIT);
            if (ret != 0)
            {
                fprintf(stderr, "Error: cannot retrieve current user\n");
                return -PVFS_EINVAL;
            } 

            ret = PINT_get_security_path(conf_keypath, local_userid, 
                keypath, MAX_PATH);
            if (ret != 0)
            {        
               fprintf(stderr, "Error: could not process path: %s (%d)\n",
                   conf_keypath, ret);
               return ret;
            }
        }
        else
        {
            /* use executing user's profile directory */
            ret = get_default_path(DEFAULT_KEYFILE, keypath);
            if (ret != 0)
            {
                return ret;
            }
        }
    }

    /* certpath needs definition */
    if (strlen(certpath) == 0)
    {
        if (strlen(conf_certpath) > 0)
        {
            /* get security paths - this replaces %USERNAME% strings in 
               the paths with the *LOCAL* userid */
            ret = get_current_userid(local_userid, USERID_PWD_LIMIT);
            if (ret != 0)
            {
                fprintf(stderr, "Error: cannot retrieve current user\n");
                return -PVFS_EINVAL;
            } 

            ret = PINT_get_security_path(conf_certpath, local_userid, 
                certpath, MAX_PATH);
            if (ret != 0)
            {        
               fprintf(stderr, "Error: could not process path: %s (%d)\n",
                   conf_certpath, ret);
               return ret;
            }
        }
        else
        {
            /* use executing user's profile directory */
            ret = get_default_path(DEFAULT_CERTFILE, certpath);
            if (ret != 0)
            {
                return ret;
            }
        }
    }    
#endif /* WIN32 */
    
    return 0;
}

/* get current user name */
int get_current_userid(char *userid, int buflen)
{
#ifndef WIN32
    struct passwd *passwd;

    errno = 0;
    passwd = getpwuid(getuid());
    if (passwd == NULL)
    {
        return errno;
    }

    strncpy(userid, passwd->pw_name, buflen);
    userid[buflen-1] = '\0';
#else
    if (!GetUserName(userid, (LPDWORD) &buflen))
    {
        return (int) GetLastError();
    }
#endif

    return 0;
}

/* store_cert_and_key()
 *
 * Write user cert and private key to disk.
 */
int store_cert_and_key(struct options *options,
                       PVFS_certificate *cert,
                       PVFS_security_key *key)
{
    X509 *xcert = NULL;
    RSA *rsa_privkey;
    PVFS_key_data keybuf;
    EVP_PKEY *privkey;
    char keypath[MAX_PATH], certpath[MAX_PATH];
    int ret;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: storing user cert and key\n", 
                 __func__);

    /* get paths for key and cert. files */
    ret = get_file_paths(options, keypath, certpath);
    if (ret != 0)
    {
        fprintf(stderr, "Error: Could not get key/certificate file path (%d)\n", ret);
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
        gossip_err("Error: User cert. path: %s\n", certpath);

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

/* get_option_value - return value from long or short option
   Returns NULL on error. If not NULL, strdup the returned value -
   do not operate on the returned pointer. */
char *get_option_value(int argc, char **argv, int *argi)
{
    char *popt;

    if (argv == NULL || *argi >= argc)
    {
        return NULL;
    }

    popt = argv[*argi];

    /* long option in "--opt=val" form */
    if (!strncmp(popt, "--", 2))
    {
        (*argi)++;
        return (strchr(popt, '=') != NULL) ? strchr(popt, '=') + 1 : NULL;
    }
    else 
    {
        /* short option in "-opt val" form */        
        if (*argi+1 >= argc)
        {
            return NULL;
        }
        else
        {
            (*argi) += 2;
            return argv[*argi-1];
        }
    }

    /* unreachable */
    return NULL;
}

int parse_args(int argc, char **argv, struct options *options)
{
    int argi;
    char *optval = NULL;

    if (options == NULL || argv == NULL)
    {
        return -1;
    }

    options->fs_userid = NULL;

    argi = 1;
    while (argi < argc)
    {
        if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help"))
        {
            return 1;
        }
        else if (!strcmp(argv[argi], "-k") ||
                 !strncmp(argv[argi], "--keyfile=", 10))
        {
            optval = get_option_value(argc, argv, &argi);
            if (optval == NULL)
            {
                fprintf(stderr, "Error: bad option %s\n", argv[argi]);
                return -1;
            }
            options->keypath = strdup(optval);
        }
        else if (!strcmp(argv[argi], "-c") ||
                 !strncmp(argv[argi], "--certfile=", 11))
        {
            optval = get_option_value(argc, argv, &argi);
            if (optval == NULL)
            {
                fprintf(stderr, "Error: bad option %s\n", argv[argi]);
                return -1;
            }
            options->certpath = strdup(optval);
        }
#ifdef WIN32
        else if (!strcmp(argv[argi], "-s") ||
                 !strcmp(argv[argi], "--system"))
        {
            options->system_flag = 1;
            argi++;
        }
#endif
        else if (!strcmp(argv[argi], "-x") ||
                 !strncmp(argv[argi], "--expiration=", 12))
        {
            optval = get_option_value(argc, argv, &argi);
            if (optval == NULL)
            {
                fprintf(stderr, "Error: bad option %s\n", argv[argi]);
                return -1;
            }
            options->exp = strtol(optval, NULL, 10);
            if (errno == ERANGE)
            {
                fprintf(stderr, "Error bad option %s\n", optval);
                return 1;
            }
        }
        else if (argv[argi][0] == '-')
        {
            fprintf(stderr, "Error: Invalid option: %s\n", argv[argi]);
            return -1;
        }
        else
        {
            options->fs_userid = argv[argi];
            break;
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
    char fs_userid[USERID_PWD_LIMIT], input_userid[USERID_PWD_LIMIT], pwd[USERID_PWD_LIMIT], 
        input[8];
    const PVFS_util_tab* tab;
    int ret, valid, quit, fs_num, init_flag = 0, addr_count;
    PVFS_BMI_addr_t *addr_array;
    PVFS_certificate cert;
    PVFS_security_key privkey;
#ifndef WIN32
    int i;
#else
    char tabfile[MAX_PATH];
#endif

    memset(&options, 0, sizeof(options));

    if (parse_args(argc, argv, &options) != 0)
    {
        fprintf(stderr, "USAGE: %s [-h|--help] [options...] [username]\n", argv[0]);
        fprintf(stderr, "   Requests certificate and private key from OrangeFS file system\n"
            "   Options:\n"
            "       -c {val}|--certfile={val} - full path for storage of certificate file\n"
            "       -k {val}|--keyfile={val} - full path for storage of cert. key file\n"
#ifdef WIN32
            "       -s|--system - generate files for the SYSTEM user\n"
#endif
            "       -x {val}|--expiration={val} - expiration time (in days; default\n"
            "                                     set on server)\n"
            "   By default, files are stored in executing user's home directory.\n");
        return 1;
    }    

    /* init OpenSSL */
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    /* prompt for userid if necessary */
    if (options.fs_userid != NULL)
    {
        strncpy(fs_userid, options.fs_userid, USERID_PWD_LIMIT);
        fs_userid[USERID_PWD_LIMIT-1] = '\0';
    }
    else
    {
        /* get current userid as default */
        ret = get_current_userid(fs_userid, USERID_PWD_LIMIT);
        if (ret != 0)
        {
            gossip_err("Error: could not retrieve current user: %d\n", 
                       ret);
            goto exit_main;
        }

        input_userid[0] = '\0';
        printf("Enter username [%s]: ", fs_userid);
        scanf("%[^\r\n]%*c", input_userid);

        /* override default */
        if (strlen(input_userid) != 0)
        {
            strcpy(fs_userid, input_userid);
        }
    }

    if (strlen(fs_userid) == 0)
    {
        fprintf(stderr, "Error: No username specified... exiting\n");
        goto exit_main;
    }

    printf("Using username %s...\n", fs_userid);

    /* prompt for password */
    EVP_read_pw_string(pwd, USERID_PWD_LIMIT, "Enter file system password: ", 0);

    /* init OrangeFS - open tabfile */
#ifndef WIN32
    tab = PVFS_util_parse_pvfstab(NULL);
#else
    ret = get_tab_file(tabfile);
    if (ret != 0)
    {
        fprintf(stderr, "Error: Could not get tabfile path (%d)\n", ret);
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
        fprintf(stderr, "Error: Multiple file systems detected - not supported\n");
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
        fprintf(stderr, "Error: Unable to load any server addresses\n");
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
                                  fs_userid, pwd, (uint32_t) addr_count,
                                  addr_array, &cert, &privkey, options.exp);
    if (ret == 0)
    {
        ret = store_cert_and_key(&options, &cert, &privkey);
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
