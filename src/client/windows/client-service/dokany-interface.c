/*
 * Copyright (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/* 
 * Dokany is a user-mode file system API like FUSE: https://github.com/dokan-dev/dokany.
 * Most of the following functions are callbacks. dokan_loop starts the Dokany thread. 
 * Functions are called as needed by Dokany (responding to file system requests).
 */

#include "dokan.h"

#include <Windows.h>
#include <AccCtrl.h>
#include <AclAPI.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "pvfs2.h"
#include "gossip.h"
#include "gen-locks.h"
#include "str-utils.h"
#include "security-util.h"

#include "client-service.h"
#include "fs.h"
#include "cert.h"
#include "user-cache.h"
/* #include "ldap-support.h" */
#include "cred.h"
#include "io-cache.h"

BOOL g_UseStdErr;
BOOL g_DebugMode;

/* normally on; disable only for testing */
#define USE_IO_CACHE

extern struct qlist_head user_list;

struct context_entry
{
    struct qhash_head hash_link;
    ULONG64 context;
    DWORD flags;
    PVFS_credential credential;
};

struct qhash_table *context_cache;
gen_mutex_t context_cache_mutex;
extern struct qhash_table *user_cache;
extern PORANGEFS_OPTIONS goptions;

#define DEBUG_FLAG(val, flag) if (val&flag) { client_debug("   "#flag"\n"); }

#define DEBUG_FILE_INFO(p, i) client_debug("   "#i": %u\n", p->i)

#define MALLOC_CHECK(ptr)   if (ptr == NULL) \
                                return -ERROR_NOT_ENOUGH_MEMORY
#define MALLOC_CHECK_N(ptr) if (ptr == NULL) \
                                return NULL

#define CRED_CHECK(func, err)  do { \
                                   if (err != 0) { \
                                       client_debug("%s: bad credential (%d)\n", func, err); \
                                       return err; \
                                   } \
                               } while (0)

#define CRED_CHECK_VOID(func, err)  do { \
                                        if (err != 0) { \
                                           client_debug("%s: bad credential (%d)\n", func, err); \
                                           return; \
                                        } \
                                    } while (0)

#define DEBUG_PATH(path)   client_debug("   resolved path: %s\n", path)

#define DEBUG_BUF_SIZE    8192
void client_debug(char *format, ...)
{
    if (g_DebugMode) 
    {
        char buffer[DEBUG_BUF_SIZE];
        va_list argp;

        va_start(argp, format);
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, argp);
        buffer[DEBUG_BUF_SIZE-1] = '\0';
        va_end(argp);

#ifdef _DEBUG
        /* debug to debugger window */
        OutputDebugString(buffer);
#endif        
        /* use gossip to debug to file or stderr (set in config file) */
        gossip_debug(GOSSIP_WIN_CLIENT_DEBUG, "%s", buffer);
    }
}

/* map a file system error code to a Dokan/Windows code 
   -1 is used as a default error */
static NTSTATUS error_map(int fs_err)
{
    NTSTATUS ret = (NTSTATUS) fs_err;

    switch (fs_err)
    {
    case 0:
    case STATUS_OBJECT_NAME_COLLISION: /* special case */
        ret = STATUS_SUCCESS;         /* 0 */
        break;
    case -PVFS_EPERM:          /* Operation not permitted */
    case -PVFS_EACCES:         /* Access not allowed */
        ret = ERROR_ACCESS_DENIED;  /* 5 */
        break;
    case -PVFS_ENOENT:         /* No such file or directory */
        ret = ERROR_FILE_NOT_FOUND;  /* 2 */
        break;
    case -PVFS_EINTR:          /* Interrupted system call */
    case -PVFS_EIO:            /* I/O error */
        ret = -1;
        break;
    case -PVFS_ENXIO:          /* No such device or address */
        ret = ERROR_DEV_NOT_EXIST;  /* 110 */
        break;
    case -PVFS_EBADF:          /* Bad file number */
        ret = ERROR_INVALID_HANDLE; /* 6 */
        break;
    case -PVFS_EAGAIN:         /* Try again */
        ret = -1;
        break;
    case -PVFS_ENOMEM:         /* Out of memory */
        ret = ERROR_NOT_ENOUGH_MEMORY;  /* 8 */
        break;
    case -PVFS_EFAULT:         /* Bad address */
        ret = ERROR_INVALID_ADDRESS;  /* 487 */
        break;
    case -PVFS_EBUSY:          /* Device or resource busy */
        ret = ERROR_BUSY;              /* 170 */
        break;
    case -PVFS_EEXIST:         /* File exists */
        ret = ERROR_FILE_EXISTS;    /* 80 */
        break;
    case -PVFS_ENODEV:         /* No such device */
        ret = ERROR_DEV_NOT_EXIST;     /* 55 */
        break;
    case -PVFS_ENOTDIR:        /* Not a directory */
        ret = ERROR_DIRECTORY;         /* 267 */
        break;
    case -PVFS_EISDIR:         /* Is a directory */
        ret = ERROR_DIRECTORY;         /* 267 */
        break;
    case -PVFS_EINVAL:         /* Invalid argument */
        ret = ERROR_INVALID_PARAMETER; /* 87 */
        break;
    case -PVFS_EMFILE:         /* Too many open files */
        ret = ERROR_TOO_MANY_OPEN_FILES;  /* 4 */
        break;
    case -PVFS_EFBIG:          /* File too large */
        ret = ERROR_FILE_TOO_LARGE;       /* 223 */
        break;
    case -PVFS_ENOSPC:         /* No space left on device */
        ret = ERROR_HANDLE_DISK_FULL;     /* 39 */
        break;
    case -PVFS_EROFS:          /* Read-only file system */
        ret = ERROR_NOT_SUPPORTED;        /* 50 */
        break;
    case -PVFS_EMLINK:         /* Too many links */
        ret = ERROR_TOO_MANY_LINKS;       /* 1142 */
        break;
    case -PVFS_EPIPE:          /* Broken pipe */
        ret = ERROR_BROKEN_PIPE;          /* 109 */
        break;
    case -PVFS_EDEADLK:        /* Resource deadlock would occur */
        ret = ERROR_POSSIBLE_DEADLOCK;    /* 1131 */
        break;
    case -PVFS_ENAMETOOLONG:   /* File name too long */
        ret = ERROR_BUFFER_OVERFLOW;      /* 111 */
        break;
    case -PVFS_ENOLCK:         /* No record locks available */
        ret = ERROR_LOCK_FAILED;          /* 167 */
        break;
    case -PVFS_ENOSYS:         /* Function not implemented */
        ret = ERROR_CALL_NOT_IMPLEMENTED; /* 120 */
        break;
    case -PVFS_ENOTEMPTY:      /* Directory not empty */
        ret = ERROR_DIR_NOT_EMPTY;        /* 145 */
        break;
    case -PVFS_ELOOP:          /* Too many symbolic links encountered */
        ret = ERROR_TOO_MANY_LINKS;       /* 1142 */
        break;
    case -PVFS_EWOULDBLOCK:    /* Operation would block */
        ret = WSAEWOULDBLOCK;             /* 10035 */
        break;
    case -PVFS_ENOMSG:         /* No message of desired type */
        ret = ERROR_INVALID_MESSAGE;      /* 1002 */
        break;
    case -PVFS_EUNATCH:        /* Protocol driver not attached */
        ret = ERROR_FS_DRIVER_REQUIRED;   /* 588 */
        break;
    case -PVFS_EBADR:          /* Invalid request descriptor */
    case -PVFS_EDEADLOCK:      /* Deadlock would result */
        ret = ERROR_POSSIBLE_DEADLOCK;    /* 1131 */
        break;
    case -PVFS_ENODATA:        /* No data available */
        ret = ERROR_NO_DATA;              /* 232 */
        break;
    case -PVFS_ETIME:          /* Timer expired */
        ret = ERROR_TIMEOUT;              /* 1460 */
        break;
    case -PVFS_ENONET:         /* Machine is not on the network */
        ret = ERROR_NO_NETWORK;           /* 1222 */
        break;
    case -PVFS_EREMOTE:        /* Object is remote */
    case -PVFS_ECOMM:          /* Communication error on send */
    case -PVFS_EPROTO:         /* Protocol error */
        ret = -1;
        break;
    case -PVFS_EBADMSG:        /* Not a data message */
        ret = ERROR_INVALID_MESSAGE;      /* 1002 */
        break;
    case -PVFS_EOVERFLOW:      /* Value too large for defined data type */
        ret = ERROR_BUFFER_OVERFLOW;      /* 111 */
        break;
    case -PVFS_ERESTART:       /* Interrupted system call should be restarted */
        ret = -1;
        break;
    case -PVFS_EMSGSIZE:       /* Message too long */
        ret = WSAEMSGSIZE;                /* 10040 */
        break;
    case -PVFS_EPROTOTYPE:     /* Protocol wrong type for socket */
        ret = WSAEPROTOTYPE;              /* 10041 */
        break;
    case -PVFS_ENOPROTOOPT:    /* Protocol not available */
        ret = WSAENOPROTOOPT;             /* 10042 */
        break;
    case -PVFS_EPROTONOSUPPORT:/* Protocol not supported */
        ret = WSAEPROTONOSUPPORT;         /* 10043 */
        break;
    case -PVFS_EOPNOTSUPP:     /* Operation not supported on transport endpoint */
        ret = WSAEOPNOTSUPP;              /* 10045 */
        break;
    case -PVFS_EADDRINUSE:     /* Address already in use */
        ret = WSAEADDRINUSE;              /* 10048 */
        break;
    case -PVFS_EADDRNOTAVAIL:  /* Cannot assign requested address */
        ret = WSAEADDRNOTAVAIL;           /* 10049 */
        break;
    case -PVFS_ENETDOWN:       /* Network is down */
        ret = WSAENETDOWN;                /* 10050 */
        break;
    case -PVFS_ENETUNREACH:    /* Network is unreachable */
        ret = WSAENETUNREACH;             /* 10051 */
        break;
    case -PVFS_ENETRESET:      /* Network dropped connection because of reset */
        ret = WSAENETRESET;               /* 10052 */
        break;
    case -PVFS_ENOBUFS:        /* No buffer space available */
        ret = WSAENOBUFS;                 /* 10055 */
        break;
    case -PVFS_ETIMEDOUT:      /* Connection timed out */
        ret = WSAETIMEDOUT;               /* 10060 */
        break;
    case -PVFS_ECONNREFUSED:   /* Connection refused */
        ret = WSAECONNREFUSED;            /* 10061 */
        break;
    case -PVFS_EHOSTDOWN:      /* Host is down */
        ret = WSAEHOSTDOWN;               /* 10064 */
        break;
    case -PVFS_EHOSTUNREACH:   /* No route to host */
        ret = WSAEHOSTUNREACH;            /* 10065 */
        break;
    case -PVFS_EALREADY:       /* Operation already in progress */
        ret = WSAEALREADY;                /* 10037 */
        break;
    case -PVFS_ECONNRESET:    /* Connection reset by peer */
        ret = WSAECONNRESET;              /* 10054 */
        break;
    }

    return DokanNtStatusFromWin32(ret);
}

/* convert string from wide char (Unicode) to multi-byte string */
char *convert_wstring(const wchar_t *wcstr)
{
    errno_t err;
    size_t ret, mb_size;
    char *mbstr;
    
    /* get size of buffer */
    err = wcstombs_s(&ret, NULL, 0, wcstr, 0);

    if (err != 0)
    {
        client_debug("convert_wstring: %d\n", err);
        return NULL;
    }

    /* allocate buffer */
    mb_size = ret;
    mbstr = (char *) malloc(mb_size);
    if (mbstr == NULL)
        return NULL;

    /* convert string */
    err = wcstombs_s(&ret, mbstr, mb_size, wcstr, wcslen(wcstr));

    if (err != 0)
    {
        client_debug("convert_wstring 2: %d\n", err);
        free(mbstr);

        return NULL;
    }

    return mbstr;

}

wchar_t *convert_mbstring(const char *mbstr)
{
    errno_t err;
    size_t ret, w_size;
    wchar_t *wstr;

    /* get size of buffer */
    err = mbstowcs_s(&ret, NULL, 0, mbstr, 0);

    if (err != 0)
    {
        client_debug("convert_mbstring: %d\n", err);
        return NULL;
    }

    w_size = ret;
    wstr = (wchar_t *) malloc(w_size * sizeof(wchar_t));
    if (wstr == NULL)
        return NULL;

    /* convert string */
    err = mbstowcs_s(&ret, wstr, w_size, mbstr, strlen(mbstr));

    if (err != 0)
    {
        client_debug("convert_mbstring 2: %d\n", err);
        free(wstr);

        return NULL;
    }

    return wstr;
}

#define cleanup_string(str)    free(str)

/* get process name with Process ID */
DWORD get_process_name(DWORD dwProcessId, LPSTR lpProcessName, DWORD nSize)
{
    HANDLE hProcess;
    char* process_name, *base_name, *temp;

    if (!(process_name = (char*)malloc(sizeof(char) * nSize)))
    {
        return ERROR_OUTOFMEMORY;
    }

    if (!(base_name = (char*)malloc(sizeof(char) * nSize)))
    {
        return ERROR_OUTOFMEMORY;
    }

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwProcessId);
    if (!hProcess)
    {
        free(base_name);
        free(process_name);
        return GetLastError();
    }

    if (!GetProcessImageFileName(hProcess, process_name, nSize))
    {
        free(base_name);
        free(process_name);
        CloseHandle(hProcess);
        return GetLastError();
    }

    strncpy(lpProcessName, process_name, strlen(process_name) + 1);
    if ((temp = strrchr(process_name, '\\')))
    {
        strncpy(base_name, temp + 1, strlen(temp + 1) + 1);
        if (strlen(base_name))
            strncpy(lpProcessName, base_name, strlen(base_name) + 1);
    }

    free(base_name);
    free(process_name);
    CloseHandle(hProcess);

    return STATUS_SUCCESS;
}

/* convert PVFS time to Windows FILETIME 
   (from MSDN Knowledgebase) */
static void convert_pvfstime(time_t t, LPFILETIME pft)
{
    LONGLONG ll;

    ll = Int32x32To64(t, 10000000) + 116444736000000000;
    pft->dwLowDateTime = (DWORD) ll;
    pft->dwHighDateTime = ll >> 32;
}


static void convert_filetime(CONST LPFILETIME pft, PVFS_time *t)
{
    LONGLONG ll;

    ll = (LONGLONG) pft->dwHighDateTime << 32;
    ll |= pft->dwLowDateTime;
    ll -= 116444736000000000;
    *t = ll / 10000000LL;
}

static DWORD get_volume_serial_num(PVFS_fs_id fs_id) {
    DWORD serial_num = (DWORD) fs_id;

    if (serial_num < 0x10000000) {
        serial_num += 0x10000000;
    }

    return serial_num;
}

/* Return resolved file system path.
   Caller must free returned string. */
static char *get_fs_path(const wchar_t *local_path)
{
    char *mb_path, *fs_path = NULL;
    int ret;

    /* convert from Unicode */
    mb_path = convert_wstring(local_path);
    if (mb_path == NULL)
    {
        return NULL;
    }

    /* resolve the path */
    fs_path = (char *) malloc(PVFS_PATH_MAX + 4);
    MALLOC_CHECK_N(fs_path);
    ret = fs_resolve_path(mb_path, fs_path, PVFS_PATH_MAX);
    if (ret != 0)
    {
        client_debug("   fs_resolve_path returned %d\n", ret);
        cleanup_string(mb_path);
        free(fs_path);
        return NULL;
    }

    DEBUG_PATH(fs_path);

    cleanup_string(mb_path);

    return fs_path;
}

int cred_compare(const void *key, 
                 struct qhash_head *link)
{
    struct context_entry *entry = qhash_entry(link, struct context_entry, hash_link);

    return (entry->context == *((ULONG64 *) key));
}

/* Get credential for requestor.
   Assumes credential is allocated but fields are not. */
static int get_requestor_credential(PDOKAN_FILE_INFO file_info,                                    
                                    PVFS_credential *credential)
{
    HANDLE htoken;
    PTOKEN_USER token_user;
    char buffer[1024], user_name[256], domain_name[256];
    DWORD user_len = 256, domain_len = 256, return_len, err;
    SID_NAME_USE snu;
    ASN1_UTCTIME *expires = NULL;
    int cache_hit, ret = 0;

    client_debug("   get_requestor_credential: enter\n");

    /* get requesting user information */
    htoken = DokanOpenRequestorToken(file_info);
    if (htoken != INVALID_HANDLE_VALUE)
    {
        if (!GetTokenInformation(htoken, TokenUser, buffer, sizeof(buffer), &return_len))
        {
            err = GetLastError();
            client_debug("   get_requestor_credential: GetTokenInformation failed: %d\n", err);
            CloseHandle(htoken);
            return err * -1;
        }   

        token_user = (PTOKEN_USER) buffer;

        if (!LookupAccountSid(NULL, token_user->User.Sid, user_name, &user_len,
                              domain_name, &domain_len, &snu))
        {
            err = GetLastError();
            client_debug("   get_requestor_credential: LookupAccountSid failed: %u\n", err);
            CloseHandle(htoken);
            return err * -1;
        }        
    }
    else
    {
        /* not all operations have a requestor */
        client_debug("   get_requestor_credential: no requestor\n");

        if (goptions->user_mode == USER_MODE_SERVER)
        {
            /* this will cause the code below to use the certificate for the
               SYSTEM user */
            strcpy(user_name, "SYSTEM");
        }
        else
        {
            ret = get_system_credential(credential);
            if (ret != 0)
            {
                report_error("Error: no system credential", ret);
            }
            return (ret == 0) ? 0 : -ERROR_ACCESS_DENIED;
        }
    }

    client_debug("   get_requestor_credential: requestor: %s\n", user_name);
    
    /* search user list for credential */
    cache_hit = get_cache_user(user_name, credential);
    if (cache_hit == USER_CACHE_MISS)
    {
        /* cache miss */
        if (goptions->user_mode == USER_MODE_LIST)
        {
            /* get system user credential */
            if (!stricmp(user_name, "SYSTEM"))
            {
                ret = get_system_credential(credential);
            }
            else
            {
                client_debug("   get_requestor_credential:  user %s not found\n", user_name);
                ret = -ERROR_USER_PROFILE_LOAD;
            }
        }
        else if (goptions->user_mode == USER_MODE_CERT)
        {
            /* load credential from certificate */
            ret = get_proxy_cert_credential(htoken, user_name, credential, &expires);
        }
        /* else if (goptions->user_mode == USER_MODE_LDAP)
        {
            ret = get_ldap_credential(user_name, credential);
        } */
        else if (goptions->user_mode == USER_MODE_SERVER)
        {            
            /* TODO - key mode */
            ret = get_user_cert_credential(htoken, user_name, credential, &expires);
        }

        /* cache user if credential created */
        if (ret == 0)
        {
            add_cache_user(user_name, credential, expires);
        }
    }

    CloseHandle(htoken);

    client_debug("   get_requestor_credential: exit\n");

    /* if credential can't be created return access denied */
    return (ret == 0) ? 0 : -ERROR_ACCESS_DENIED;
}

/* Get FS credential from cache or mapping system (based on requestor) */
static int get_credential(PDOKAN_FILE_INFO file_info, 
                           PVFS_credential *credential)
{
    struct qhash_head *item;
    struct context_entry *entry;
    int ret = 0;

    if (file_info == NULL || credential == NULL)
        return -ERROR_INVALID_PARAMETER;

    client_debug("   get_credential:  context: %llx\n", file_info->Context);

    if (file_info->Context != 0)
    {
        /* check cache for existing credential 
           associated with the context */    
        gen_mutex_lock(&context_cache_mutex);
        item = qhash_search(context_cache, &file_info->Context);
        if (item != NULL)
        {
            /* if cache hit -- return credential */
            entry = qhash_entry(item, struct context_entry, hash_link);
            PINT_copy_credential(&(entry->credential), credential);
            if (goptions->user_mode != USER_MODE_SERVER)
            {
                client_debug("   get_credential:  found (%d:%d)\n", 
                    credential->userid, credential->group_array[0]);
            }
            else
            {
                client_debug("   get_credential:  found\n");
            }
        }
        else
        {
            client_debug("   get_credential:  not found\n");
            ret = -1;
        }
        gen_mutex_unlock(&context_cache_mutex);
    }
    else
    {
        /* retrieve credential for the requestor */
        ret = get_requestor_credential(file_info, credential);
        if (ret == 0)
        {
            if (goptions->user_mode != USER_MODE_SERVER)
            {
                client_debug("   get_credential:  requestor credential (%d:%d)\n", 
                    credential->userid, credential->group_array[0]);
            }
            else
            {
                client_debug("   get_credential:  requestor credential OK\n");
            }
        }
    }

    client_debug("   get_credential:  exit\n");

    return ret;
}

/* add entry for file to the context cache */
static void add_context(PDOKAN_FILE_INFO file_info, DWORD flags, PVFS_credential *credential)
{
    struct context_entry *entry;

    if (file_info == NULL) {
        client_debug("   add_context: NULL file_info\n");
        return;
    }

    /* create new entry */
    entry = (struct context_entry *) calloc(1, sizeof(struct context_entry));
    if (entry == NULL)
    {
        client_debug("   add_context: out of memory\n");
        return;
    }

    entry->context = file_info->Context;
    entry->flags = flags;
    PINT_copy_credential(credential, &(entry->credential));

    gen_mutex_lock(&context_cache_mutex);
    qhash_add(context_cache, &entry->context, &entry->hash_link);
    gen_mutex_unlock(&context_cache_mutex);
}

static struct context_entry *get_context_entry(ULONG64 context)
{
    struct qhash_head *item = NULL;
    struct context_entry *entry = NULL;
    
    gen_mutex_lock(&context_cache_mutex);
    item = qhash_search(context_cache, &context);
    if (item != NULL)
    {
        /* get entry on cache hit */
        entry = qhash_entry(item, struct context_entry, hash_link);		
    }
    gen_mutex_unlock(&context_cache_mutex);
    
    return entry;
}

/* remove credential from cache */
static void remove_context(ULONG64 context)
{
    struct qhash_head *link; 
    struct context_entry *entry;
    
    gen_mutex_lock(&context_cache_mutex);
    link = qhash_search_and_remove(context_cache, &context);
    if (link != NULL)
    {
        entry = qhash_entry(link, struct context_entry, hash_link);
        PINT_cleanup_credential(&(entry->credential));
        free(entry);
    }
    gen_mutex_unlock(&context_cache_mutex);
}

/* Permission constants */
#define PERM_READ    4
#define PERM_WRITE   2
#define PERM_EXECUTE 1

/* Return true if user with credential has permission (given attributes) */
static int check_perm(PVFS_sys_attr *attr, PVFS_credential *credential, int perm)
{
    int mask;

    if (goptions->user_mode != USER_MODE_SERVER)
    {
        /* root user (uid 0 or gid 0) always has rights */
        if (credential->userid == 0 || credential_in_group(credential, 0))
            return 1;
    
        if (attr->owner == credential->userid)
            /* use owner mask */
            mask = (attr->perms >> 6) & 7;
        else if (credential_in_group(credential, attr->group))
            /* use group mask */
            mask = (attr->perms >> 3) & 7;
        else
            /* use other mask */
            mask = attr->perms & 7;

        if (mask & perm)
        {
            return 1;
        }
    }
    else
    {
        /* in server-side user mode, user is listed as having permission if 
           any users have permission; server will handle insufficent perms. 
           FUTURE: request rights mask from server (need new server request)
         */        
        if (((attr->perms & 7) & perm) || (((attr->perms >> 3) & 7) & perm) ||
            (((attr->perms >> 6) & 7) & perm))
        {
            return 1;
        }    
    }

    return 0;
}

/* Check permissions for create_file call */
static int check_create_perm(PVFS_sys_attr *attr, PVFS_credential *credential, DWORD access_mode)
{
    int ret = 0, write_flag = 0;

    /* read attributes access */
    if (access_mode & FILE_READ_ATTRIBUTES ||
        access_mode & FILE_READ_EA ||
        access_mode & READ_CONTROL ||
        access_mode & SYNCHRONIZE)
    {
        /* On PVFS2, all users have these rights */
        ret = 1;
    }

    /* read data access */
    if (access_mode & GENERIC_READ ||
        access_mode & GENERIC_ALL ||
        access_mode & FILE_READ_DATA)
    {
        ret = check_perm(attr, credential, PERM_READ);
        
        if (!ret)
            return ret;
    }

    /* write attributes access */
    if (access_mode & FILE_WRITE_ATTRIBUTES ||
        access_mode & FILE_WRITE_EA ||
        access_mode & WRITE_DAC ||
        access_mode & WRITE_OWNER ||
        access_mode & DELETE)
    {
        /* owner always has these permissions */
        ret = attr->owner == credential->userid;
        if (!ret)
        {
            /* otherwise write permissions are needed */
            ret = check_perm(attr, credential, PERM_WRITE);
            if (!ret)
                return ret;
            write_flag = 1;
        }
    }

    /* write access */
    if (access_mode & GENERIC_WRITE ||
        access_mode & GENERIC_ALL ||
        access_mode & FILE_WRITE_DATA)
    {
        /* Either user is owner, or has write permissions checked already. 
           Note that if owner doesn't have write data, the file will be  
           marked read-only */
        ret = write_flag || check_perm(attr, credential, PERM_WRITE);

        if (!ret)
            return ret;
    }

    /* execute access */
    if (access_mode & GENERIC_EXECUTE ||
        access_mode & GENERIC_ALL)
    {
        ret = check_perm(attr, credential, PERM_EXECUTE);
    }

    return ret;
}

/* convert OrangeFS attributes to Windows info */
static int PVFS_sys_attr_to_file_info(char *filename,
                                      PVFS_credential *credential,
                                      PVFS_sys_attr *attr, 
                                      LPBY_HANDLE_FILE_INFORMATION phFileInfo)
{

    if (filename == NULL || credential == NULL || attr == NULL || 
        phFileInfo == NULL)
    {
        return -PVFS_EINVAL;
    }

    ZeroMemory(phFileInfo, sizeof(BY_HANDLE_FILE_INFORMATION));
    
    if (attr->objtype & PVFS_TYPE_DIRECTORY) {
        phFileInfo->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    }

    /* check for hidden file */
    if (strcmp(filename, ".") != 0 &&
        strcmp(filename, "..") != 0 &&
        filename[0] == '.')
    {
        phFileInfo->dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    }
        
    /* Check perms for READONLY */
    if (!check_perm(attr, credential, PERM_WRITE))
    {
        phFileInfo->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;        
    }

    /* check for temporary file */
    /*
    if (DokanFileInfo->DeleteOnClose)
    {
        phFileInfo->dwFileAttributes |= FILE_ATTRIBUTE_TEMPORARY;
        strcat(info, "TEMP ");
    }
    */

    /* normal file */
    if (phFileInfo->dwFileAttributes == 0)
    {
        phFileInfo->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;        
    }
        
    /* links */
    phFileInfo->nNumberOfLinks = 1;

    /* file times */
    convert_pvfstime(attr->ctime, &phFileInfo->ftCreationTime);
    convert_pvfstime(attr->atime, &phFileInfo->ftLastAccessTime);
    convert_pvfstime(attr->mtime, &phFileInfo->ftLastWriteTime);

    /* file size */
    phFileInfo->nFileSizeHigh = (attr->size & 0x7FFFFFFF00000000LL) >> 32;
    phFileInfo->nFileSizeLow = (attr->size & 0xFFFFFFFFLL);

    return 0;
}

static ULONG64 gen_context()
{
    LARGE_INTEGER counter;

    QueryPerformanceCounter(&counter);

    return (ULONG64) counter.QuadPart;
}

static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_create_file(
    LPCWSTR FileName,
    PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
    ACCESS_MASK DesiredAccess,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    ACCESS_MASK desired_access;
    DWORD file_attrs_and_flags;
    DWORD create_disposition;
    char process_name[PATH_MAX];
    char *fs_path;
    int ret = -1, ret_attr = -1, ret_perm = -1, err = -1;
    int found = 0, attr_flag = 0, new_flag = 0;
    PVFS_handle handle;
    PVFS_sys_attr attr;
    PVFS_credential credential;

    client_debug("CreateFile: %S\n", FileName);

    /* change kernel flags to usermode flags */
    DokanMapKernelToUserCreateFileFlags(
        DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
        &desired_access, &file_attrs_and_flags, &create_disposition);

    client_debug("  Create Disposition = 0x%x\n", create_disposition);
    
    if (create_disposition == CREATE_NEW)
    {
        client_debug("   CREATE_NEW\n");
    }
    else if (create_disposition == OPEN_ALWAYS)
    {
        client_debug("   OPEN_ALWAYS\n");
    }
    else if (create_disposition == CREATE_ALWAYS)
    {
        client_debug("   CREATE_ALWAYS\n");
    }
    if (create_disposition == OPEN_EXISTING)
    {
        client_debug("   OPEN_EXISTING\n");
    }
    if (create_disposition == TRUNCATE_EXISTING)
    {
        client_debug("   TRUNCATE_EXISTING\n");
    }

    client_debug("  ShareAccess = 0x%x\n", ShareAccess);

    DEBUG_FLAG(ShareAccess, FILE_SHARE_READ);
    DEBUG_FLAG(ShareAccess, FILE_SHARE_WRITE);
    DEBUG_FLAG(ShareAccess, FILE_SHARE_DELETE);

    client_debug("  Desired Access = 0x%x\n", desired_access);

    DEBUG_FLAG(desired_access, GENERIC_READ);
    DEBUG_FLAG(desired_access, GENERIC_WRITE);
    DEBUG_FLAG(desired_access, GENERIC_EXECUTE);
    
    DEBUG_FLAG(desired_access, DELETE);
    DEBUG_FLAG(desired_access, FILE_READ_DATA);
    DEBUG_FLAG(desired_access, FILE_READ_ATTRIBUTES);
    DEBUG_FLAG(desired_access, FILE_READ_EA);
    DEBUG_FLAG(desired_access, READ_CONTROL);
    DEBUG_FLAG(desired_access, FILE_WRITE_DATA);
    DEBUG_FLAG(desired_access, FILE_WRITE_ATTRIBUTES);
    DEBUG_FLAG(desired_access, FILE_WRITE_EA);
    DEBUG_FLAG(desired_access, FILE_APPEND_DATA);
    DEBUG_FLAG(desired_access, WRITE_DAC);
    DEBUG_FLAG(desired_access, WRITE_OWNER);
    DEBUG_FLAG(desired_access, SYNCHRONIZE);
    DEBUG_FLAG(desired_access, FILE_EXECUTE);
    DEBUG_FLAG(desired_access, STANDARD_RIGHTS_READ);
    DEBUG_FLAG(desired_access, STANDARD_RIGHTS_WRITE);
    DEBUG_FLAG(desired_access, STANDARD_RIGHTS_EXECUTE);

    client_debug("  File Attributes / Flags = 0x%x\n", file_attrs_and_flags);

    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_ARCHIVE);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_ENCRYPTED);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_HIDDEN);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_NORMAL);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_OFFLINE);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_READONLY);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_SYSTEM);
    DEBUG_FLAG(file_attrs_and_flags, FILE_ATTRIBUTE_TEMPORARY);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_WRITE_THROUGH);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_OVERLAPPED);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_NO_BUFFERING);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_RANDOM_ACCESS);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_SEQUENTIAL_SCAN);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_DELETE_ON_CLOSE);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_BACKUP_SEMANTICS);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_POSIX_SEMANTICS);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_OPEN_REPARSE_POINT);
    DEBUG_FLAG(file_attrs_and_flags, FILE_FLAG_OPEN_NO_RECALL);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_ANONYMOUS);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_IDENTIFICATION);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_IMPERSONATION);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_DELEGATION);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_CONTEXT_TRACKING);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_EFFECTIVE_ONLY);
    DEBUG_FLAG(file_attrs_and_flags, SECURITY_SQOS_PRESENT);

    client_debug("  DokanFileInfo:\n");

    strcpy(process_name, "Unknown");
    get_process_name(DokanFileInfo->ProcessId, process_name, PATH_MAX);    
    DEBUG_FILE_INFO(DokanFileInfo, ProcessId);
    client_debug("   ProcessName: %s\n", process_name);
    DEBUG_FILE_INFO(DokanFileInfo, IsDirectory);
    DEBUG_FILE_INFO(DokanFileInfo, DeleteOnClose);
    DEBUG_FILE_INFO(DokanFileInfo, PagingIo);
    DEBUG_FILE_INFO(DokanFileInfo, SynchronousIo);
    DEBUG_FILE_INFO(DokanFileInfo, Nocache);
    DEBUG_FILE_INFO(DokanFileInfo, WriteToEndOfFile);
    
    DokanFileInfo->Context = 0;

    /* load credential (of requestor) */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("CreateFile", err);

    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* look up the file */
    found = 0;
    ret = fs_lookup(fs_path, &credential, &handle);

    client_debug("   fs_lookup returns: %d\n", ret);

    if (ret == -PVFS_ENOENT)
    {
        found = 0;
    }
    else if (ret != 0)
    {
        free(fs_path);
        return error_map(ret);
    }
    else
    {
        found = 1;
    }

    /* check permissions for existing file */
    if (found)
    {
        ret_attr = fs_getattr(fs_path, &credential, &attr);
        if (ret_attr == 0)
        {
            ret_perm = check_create_perm(&attr, &credential, desired_access);
            if (!ret_perm)
            {
                client_debug("CreateFile exit: access denied\n");
                free(fs_path);
                return ERROR_ACCESS_DENIED;
            }
            attr_flag = 1;
        }
        else
        {
            client_debug("CreateFile exit: fs_getattr (1) failed with code: %d\n", ret_attr);
            free(fs_path);
            return error_map(ret_attr);
        }
    }

    ret = 0;

    switch (create_disposition)
    {
    case CREATE_ALWAYS:
        if (found)
        {
            fs_remove(fs_path, &credential);
        }
        if (DokanFileInfo->IsDirectory)
        {
            ret = fs_mkdir(fs_path, &credential, &handle, goptions->new_dir_perms);
        }
        else {
            ret = fs_create(fs_path, &credential, &handle, goptions->new_file_perms);
        }
        if (found && ret == 0) {
            err = STATUS_OBJECT_NAME_COLLISION;
        }
        break;
    case CREATE_NEW:
        if (found)
        {
            if (strcmp(fs_path, "/") == 0) {
                /* set directory flag on for the root directory */
                DokanFileInfo->IsDirectory = 1;
            }
            else {
                ret = -PVFS_EEXIST;
            }
        }
        else
        {
            /* create file or directory */
            if (DokanFileInfo->IsDirectory)
            {
                ret = fs_mkdir(fs_path, &credential, &handle, goptions->new_dir_perms);
            }
            else {
                ret = fs_create(fs_path, &credential, &handle, goptions->new_file_perms);
            }
        }
        break;
    case OPEN_ALWAYS:
        if (!found)
        {    
            /* create file */
            ret = fs_create(fs_path, &credential, &handle, goptions->new_file_perms);
        }
        else {
            err = STATUS_OBJECT_NAME_COLLISION;
        }
        break;
    case OPEN_EXISTING:
        if (!found)
        {
            /* return error */;
            ret = -PVFS_ENOENT;
        }
        else {
            if (DokanFileInfo->IsDirectory && !(attr.objtype & PVFS_TYPE_DIRECTORY)) {
                ret = -PVFS_ENOTDIR;
            }
        }
        break;
    case TRUNCATE_EXISTING:
        if (!found)
        {
            ret = -PVFS_ENOENT;
        }
        else
        {   
            ret = fs_truncate(fs_path, 0, &credential);
        }
    }

    client_debug("   fs_create/fs_truncate returns: %d\n", ret);

    if (err != STATUS_OBJECT_NAME_COLLISION) {
        err = error_map(ret);
    }
    if (err == ERROR_SUCCESS || err == STATUS_OBJECT_NAME_COLLISION)
    {
        /* generate unique context */
        DokanFileInfo->Context = gen_context();

        client_debug("   Context: %llx\n", DokanFileInfo->Context);
        add_context(DokanFileInfo, file_attrs_and_flags, &credential);
        /* determine whether this is a directory */
        if (!attr_flag)
        {
            ret_attr = fs_getattr(fs_path, &credential, &attr);
        }
        if (ret_attr == 0)
        {
            DokanFileInfo->IsDirectory = (attr.objtype & PVFS_TYPE_DIRECTORY) ? TRUE : FALSE;
            client_debug("   Directory: %s\n", DokanFileInfo->IsDirectory ? "TRUE" : "FALSE");
        }
        else
        {
            client_debug("   fs_getattr (2) failed with code: %d\n", ret_attr);
        }
    }

    free(fs_path);
    PINT_cleanup_credential(&credential);

    client_debug("CreateFile exit: %d (%d)\n", err, ret);

    return err;
}


/* Not used for Dokany */
#if 0
static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_create_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_handle handle;
    PVFS_credential credential;

    client_debug("CreateDirectory: %S\n", FileName);

    DokanFileInfo->Context = 0;

    /* load credential (of requestor) */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("CreateDirectory", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    ret = fs_mkdir(fs_path, &credential, &handle, goptions->new_dir_perms);

    client_debug("   fs_mkdir returns: %d\n", ret);

    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        DokanFileInfo->IsDirectory = TRUE;
        DokanFileInfo->Context = gen_context();
        add_context(DokanFileInfo, 0, &credential);
    }

    free(fs_path);
    PINT_cleanup_credential(&credential);

    client_debug("CreateDirectory exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_open_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_sys_attr attr;
    PVFS_credential credential;

    client_debug("OpenDirectory: %S\n", FileName);

    DokanFileInfo->Context = 0;

    /* load credential (of requestor) */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("OpenDirectory", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* verify file is a directory */
    ret = fs_getattr(fs_path, &credential, &attr);
    client_debug("   fs_getattr returns: %d\n", ret);
    if (ret == 0)
    {
        if (!(attr.objtype & PVFS_TYPE_DIRECTORY))
        {
            ret = -PVFS_ENOTDIR;
        }
    }

    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        DokanFileInfo->IsDirectory = TRUE;
        DokanFileInfo->Context = gen_context();
        add_context(DokanFileInfo, 0, &credential);
    }

    free(fs_path);
    PINT_cleanup_credential(&credential);

    client_debug("OpenDirectory exit: %d (%d)\n", err, ret);
    
    return err;
}
#endif


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_close_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path = NULL;
    int ret = 0, err = 0;    

    client_debug("CloseFile: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* No-op: PVFS doesn't have an open-file/close-file semantic */ 

    /* remove credential from cache */
    if (DokanFileInfo->Context != 0)
    {
        remove_context(DokanFileInfo->Context);
    }

    client_debug("CloseFile exit: %d (%d)\n", err, ret);

    return err;
}


static void __stdcall
PVFS_Dokan_cleanup(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char* fs_path = NULL;
    int ret = 0, err;
    PVFS_credential credential;
    int del_flag = 0;
    /* struct context_entry* entry; */

    client_debug("Cleanup: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* determine whether file should be deleted */
    del_flag = DokanFileInfo->DeleteOnClose;
    /* TODO: determine if DeleteOnClose is always set correctly--otherwise 
       the following will be needed. */
#if 0
    if (!del_flag)
    {
        /* get cached entry */
        entry = get_context_entry(DokanFileInfo->Context);
        del_flag = entry && (entry->flags & FILE_FLAG_DELETE_ON_CLOSE);
    }
#endif

    /* delete the file/dir if DeleteOnClose specified */
    if (del_flag)
    {
        client_debug("   Deleting file\n");

        /* load credential */
        err = get_credential(DokanFileInfo, &credential);
        CRED_CHECK_VOID("CleanupFile", err);

        /* get file system path */
        fs_path = get_fs_path(FileName);
        if (fs_path == NULL)
        {
            return;
        }

        /* remove the file/dir */
        ret = fs_remove(fs_path, &credential);

        PINT_cleanup_credential(&credential);
    }

    client_debug("Cleanup exit: %d\n", 0);

    return;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_read_file(
    LPCWSTR          FileName,
    LPVOID           Buffer,
    DWORD            BufferLength,
    LPDWORD          ReadLength,
    LONGLONG         Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path = NULL;
    PVFS_size len64;
#ifdef USE_IO_CACHE
    PVFS_object_ref object_ref;
    enum PVFS_io_type io_type;
    int update_flag;
#else
    PVFS_sys_attr attr;
#endif
    PVFS_credential credential;
    int ret = -1, cache_ret, err;
    
    client_debug("ReadFile: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);
    client_debug("   BufferLength: %lu\n", BufferLength);
    client_debug("   Offset: %llu\n", Offset);

    if (FileName == NULL || wcslen(FileName) == 0 ||
        Buffer == NULL || BufferLength == 0 ||
        ReadLength == 0)
    {
        return -1;
    }

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("ReadFile", err);

    /* check IO cache */
#ifdef USE_IO_CACHE
    cache_ret = io_cache_get(DokanFileInfo->Context, &object_ref, &io_type, 
        &update_flag);
    if (cache_ret == IO_CACHE_HIT)
    {

        ret = fs_read2(object_ref, Buffer, BufferLength, Offset, 
                        &len64, &credential);

    }
    else if (cache_ret != IO_CACHE_MISS)
    {
        /* error */
        report_error("Read file: cache error: ", cache_ret);

        goto read_file_exit;
    }
#endif


#ifdef USE_IO_CACHE
    if (cache_ret == IO_CACHE_MISS)
    {
        /* get file system path */
        fs_path = get_fs_path(FileName);
        if (fs_path == NULL)
        {
            ret = -1;
            
            goto read_file_exit;
        }

        ret = fs_lookup(fs_path, &credential, &object_ref.handle);
        if (ret != 0)
        {
            report_error("Read file: lookup error: ", ret);

            goto read_file_exit;
        }

        object_ref.fs_id = fs_get_id(0);

        ret = fs_read2(object_ref, Buffer, BufferLength, Offset,
                       &len64, &credential);
        if (ret == 0)
        {
            io_type = PVFS_IO_READ;
            cache_ret = io_cache_add(DokanFileInfo->Context, &object_ref,
                io_type, IO_CACHE_UPDATE);
            if (cache_ret != 0)
            {
                report_error("Read file: error adding context to IO cache: ", cache_ret);
            }
        }
    }
#else
    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
    {
        ret = -1;
            
        goto read_file_exit;
    }

    /* perform the read operation */
    ret = fs_read(fs_path, Buffer, BufferLength, Offset, &len64, &credential);

#endif

    *ReadLength = (DWORD) len64;

    client_debug("   ReadLength: %u\n", *ReadLength);

read_file_exit:

    if (fs_path != NULL)
    {
        free(fs_path);
    }
    
    PINT_cleanup_credential(&credential);

    err = error_map(ret);
    
    client_debug("ReadFile exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_write_file(
    LPCWSTR          FileName,
    LPCVOID          Buffer,
    DWORD            NumberOfBytesToWrite,
    LPDWORD          NumberOfBytesWritten,
    LONGLONG         Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path = NULL;
    PVFS_size len64;
    PVFS_credential credential;
#ifdef USE_IO_CACHE
    PVFS_object_ref object_ref;
    enum PVFS_io_type io_type;
    int update_flag;
#endif    
    int ret = -1, ret2, cache_ret, err;
    PVFS_sys_attr attr = {0};

    client_debug("WriteFile: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);
    client_debug("   NumberOfBytesToWrite: %u\n", NumberOfBytesToWrite);
    client_debug("   Offset: %llu\n", Offset);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("WriteFile", err);

    /* check IO cache */
#ifdef USE_IO_CACHE
    cache_ret = io_cache_get(DokanFileInfo->Context, &object_ref, &io_type,
        &update_flag);
    if (cache_ret == IO_CACHE_HIT)
    {
        ret = fs_write2(object_ref, (void *) Buffer, NumberOfBytesToWrite,
                        Offset, &len64, &credential);
    }
    else if (cache_ret != IO_CACHE_MISS)
    {
        /* error */
        report_error("Write File: cache error: ", cache_ret);

        goto write_file_exit;
    }
#endif

#ifdef USE_IO_CACHE
    if (cache_ret == IO_CACHE_MISS)
    {
        /* get file system path */
        fs_path = get_fs_path(FileName);
        if (fs_path == NULL)
        {
            ret = -1;
            
            goto write_file_exit;
        }

        ret = fs_lookup(fs_path, &credential, &object_ref.handle);
        if (ret != 0)
        {
            report_error("Write File: lookup error: ", ret);

            goto write_file_exit;
        }

        object_ref.fs_id = fs_get_id(0);

        ret = fs_write2(object_ref, (void *) Buffer, NumberOfBytesToWrite,
                        Offset, &len64, &credential);
        if (ret == 0)
        {
            io_type = PVFS_IO_WRITE;
            cache_ret = io_cache_add(DokanFileInfo->Context, &object_ref, 
                io_type, IO_CACHE_UPDATE);
            if (cache_ret != 0)
            {
                report_error("Read File: error adding context to IO cache: ", cache_ret);
            }
        }
    }
#else
    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
    {
        ret = -1;
            
        goto write_file_exit;
    }

    /* perform the write operation */
    ret = fs_write(fs_path, (void *) Buffer, NumberOfBytesToWrite, Offset, 
                   &len64, &credential);

#endif

    *NumberOfBytesWritten = (DWORD) len64;

    client_debug("   NumberOfBytesWritten: %u\n", *NumberOfBytesWritten);

write_file_exit:
    /* set the modify and access times */
    if (ret == 0 && !goptions->disable_update_write_time)
    {
        if (fs_path == NULL)
        {
            fs_path = get_fs_path(FileName);
        }
        if (fs_path != NULL)
        {
            attr.mask = PVFS_ATTR_SYS_ATIME|PVFS_ATTR_SYS_ATIME_SET|
                PVFS_ATTR_SYS_MTIME|PVFS_ATTR_SYS_MTIME_SET;
            attr.atime = attr.mtime = time(NULL);
            if((ret2 = fs_setattr(fs_path, &attr, &credential)) != 0)
            {
                client_debug("   fs_setattr (atime/mtime) returned %d\n", ret2);
            }
        }
    }

    if (fs_path != NULL)
    {
        free(fs_path);
    }
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("WriteFile exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_flush_file_buffers(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_credential credential;

    client_debug("FlushFileBuffers: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("FlushFileBuffers", err);
    
    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
    {
        return -1;
    }

    /* flush the file */
    ret = fs_flush(fs_path, &credential);

    err = error_map(ret);

    free(fs_path);
    PINT_cleanup_credential(&credential);

    client_debug("FlushFileBuffers exit: %d (%d)\n", err, ret);

    return err;
}

/* free attribute buffers that are allocated with fs_getattr */
#define FREE_ATTR_BUFS(attr)    do { \
                                    if (attr.dist_name != NULL) \
                                        free(attr.dist_name); \
                                    if (attr.dist_params != NULL) \
                                        free(attr.dist_params); \
                                    if (attr.link_target != NULL) \
                                        free(attr.link_target); \
                                } while (0)


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_get_file_information(
    LPCWSTR                      FileName,
    LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO             DokanFileInfo)
{
    char *fs_path, *filename;
    int ret, err;
    PVFS_sys_attr attr;
    PVFS_credential credential;
    PVFS_handle handle;
    char info[32];
    ULONG64 index;
    

    client_debug("GetFileInfo: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    DEBUG_FILE_INFO(DokanFileInfo, ProcessId);
    DEBUG_FILE_INFO(DokanFileInfo, IsDirectory);
    DEBUG_FILE_INFO(DokanFileInfo, DeleteOnClose);
    DEBUG_FILE_INFO(DokanFileInfo, PagingIo);
    DEBUG_FILE_INFO(DokanFileInfo, SynchronousIo);
    DEBUG_FILE_INFO(DokanFileInfo, Nocache);
    DEBUG_FILE_INFO(DokanFileInfo, WriteToEndOfFile);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("GetFileInfo", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
    {
        return -1;
    }

    /* lookup for handle */
    ret = fs_lookup(fs_path, &credential, &handle);
    if (ret == 0) {

        /* get file attributes */
        ret = fs_getattr(fs_path, &credential, &attr);

        if (ret == 0)
        {
            filename = (char*)malloc(strlen(fs_path) + 1);
            MALLOC_CHECK(filename);
            PINT_remove_base_dir(fs_path, filename, (int)strlen(fs_path) + 1);

            ret = PVFS_sys_attr_to_file_info(filename, &credential, &attr,
                HandleFileInformation);

            /* set serial number */
            HandleFileInformation->dwVolumeSerialNumber = get_volume_serial_num(fs_get_id(0));

            /* set index */
            HandleFileInformation->nFileIndexHigh = (DWORD)(handle >> 32);
            HandleFileInformation->nFileIndexLow = (DWORD)(handle & 0xFFFFFFFF);

            free(filename);

            if (ret == 0)
            {
                strcpy(info, "   ");
                /* temporary file */
                if (DokanFileInfo->DeleteOnClose)
                {
                    HandleFileInformation->dwFileAttributes |= FILE_ATTRIBUTE_TEMPORARY;
                    strcat(info, "TEMP ");
                }

                /* debugging */
                if (HandleFileInformation->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    strcat(info, "DIR ");
                }

                if (HandleFileInformation->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
                {
                    strcat(info, "HIDDEN ");
                }

                if (HandleFileInformation->dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                {
                    strcat(info, "READONLY ");
                }

                /* normal file */
                if (HandleFileInformation->dwFileAttributes & FILE_ATTRIBUTE_NORMAL)
                {
                    strcat(info, "NORMAL");
                }

                client_debug("%s (%lx)\n", info, HandleFileInformation->dwFileAttributes);

                /* debug volume serial no. and index */
                client_debug("   Volume Serial Number: %x\n", HandleFileInformation->dwVolumeSerialNumber);
                index = HandleFileInformation->nFileIndexHigh;
                index <<= 32;
                index |= HandleFileInformation->nFileIndexLow;
                client_debug("   File Index: %llx\n", index);
            }

            FREE_ATTR_BUFS(attr);
        }
    }

    err = error_map(ret);

    free(fs_path);
    PINT_cleanup_credential(&credential);

    client_debug("GetFileInfo exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_set_file_attributes(
    LPCWSTR          FileName,
    DWORD            FileAttributes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err, change_flag = 0;
    PVFS_sys_attr attr;
    PVFS_credential credential;

    client_debug("SetFileAttributes: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("SetFileAttributes", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
    {
        return -1;
    }

    /* convert attributes to PVFS */
    ret = fs_getattr(fs_path, &credential, &attr);

    if (ret == 0)
    {
        attr.mask = PVFS_ATTR_SYS_PERM;
        /* write permission is on and request to make
           file readonly */
        if (((attr.perms & 0200) ||
             (attr.perms & 0020) ||
             (attr.perms & 0002)) &&
            (FileAttributes & FILE_ATTRIBUTE_READONLY))
        {
            attr.perms &= ~0222;
            change_flag = 1;
        }
        else if ((!(attr.perms & 0200) ||
                  !(attr.perms & 0020) ||
                  !(attr.perms & 0002)) &&
                 !(FileAttributes & FILE_ATTRIBUTE_READONLY))
        {
            /* write permission is off and request to make
               file writable */
            attr.perms |= 0222;
            change_flag = 1;
        }

        if (change_flag)
        {
            ret = fs_setattr(fs_path, &attr, &credential);
        }
    }

    free(fs_path);
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("SetFileAttributes exit: %d (%d)\n", err, ret);

    return err;
}


/* add . and .. entries to directory listing */
static int add_dir_entries(
    char *fs_path,
    PVFS_credential *credential,
    PFillFindData FillFindData,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret;
    PVFS_sys_attr attr1, attr2;
    char parent_path[PVFS_PATH_MAX+8];
    WIN32_FIND_DATAW find_data;
    BY_HANDLE_FILE_INFORMATION hfile_info;

    /* get attributes of current directory */
    ret = fs_getattr(fs_path, credential, &attr1);
    if (ret != 0)
    {
        client_debug("   add_dir_entries: fs_getattr (1) returned %d\n", ret);
        return ret;
    }

    /* determine parent path */
    memset(parent_path, 0, sizeof(parent_path));
    if (strcmp(fs_path, "/") == 0)
    {
        /* just use current path for root */
        memcpy(&attr2, &attr1, sizeof(PVFS_sys_attr));
    }
    else
    {
        /* get attrs of parent for .. entry */
        ret = PINT_get_base_dir(fs_path, parent_path, PVFS_PATH_MAX);
        if (ret == -1)
        {
            return -PVFS_EINVAL;
        }
        
        ret = fs_getattr(parent_path, credential, &attr2);
        if (ret != 0)
        {
            client_debug("   add_dir_entries: fs_getattr (2) returned %d\n", ret);
            return ret;
        }
    }

    /* convert attributes of . entry */
    memset(&find_data, 0, sizeof(WIN32_FIND_DATAW));
    ret = PVFS_sys_attr_to_file_info(".", credential, &attr1, &hfile_info);
    if (ret != 0)
    {
        client_debug("   add_dir_entries: PVFS_sys_attr_to_file_info returned %d\n", ret);        
        return -PVFS_EINVAL;
    }
        
    /* attributes */
    find_data.dwFileAttributes = hfile_info.dwFileAttributes;
    memcpy(&find_data.ftCreationTime, &hfile_info.ftCreationTime, 
            sizeof(FILETIME));
    memcpy(&find_data.ftLastAccessTime, &hfile_info.ftLastAccessTime,
            sizeof(FILETIME));
    memcpy(&find_data.ftLastWriteTime, &hfile_info.ftLastWriteTime,
            sizeof(FILETIME));
    find_data.nFileSizeHigh = hfile_info.nFileSizeHigh;
    find_data.nFileSizeLow = hfile_info.nFileSizeLow;
        
    /* filename */        
    wcscpy(find_data.cFileName, L".");

    /* Dokan callback function */
    FillFindData(&find_data, DokanFileInfo);

    /* convert attributes of .. entry */
    memset(&find_data, 0, sizeof(WIN32_FIND_DATAW));
    ret = PVFS_sys_attr_to_file_info("..", credential, &attr2, &hfile_info);
    if (ret != 0)
    {
        client_debug("   add_dir_entries: PVFS_sys_attr_to_file_info returned %d\n", ret);        
        return -PVFS_EINVAL;
    }
        
    /* attributes */
    find_data.dwFileAttributes = hfile_info.dwFileAttributes;
    memcpy(&find_data.ftCreationTime, &hfile_info.ftCreationTime, 
            sizeof(FILETIME));
    memcpy(&find_data.ftLastAccessTime, &hfile_info.ftLastAccessTime,
            sizeof(FILETIME));
    memcpy(&find_data.ftLastWriteTime, &hfile_info.ftLastWriteTime,
            sizeof(FILETIME));
    find_data.nFileSizeHigh = hfile_info.nFileSizeHigh;
    find_data.nFileSizeLow = hfile_info.nFileSizeLow;
        
    /* filename */        
    wcscpy(find_data.cFileName, L"..");

    /* Dokan callback function */
    FillFindData(&find_data, DokanFileInfo);

    return 0;
}

/* max files per request - based on PVFS_REQ_LIMIT_DIRENT_COUNT_READDIRPLUS in 
   pvfs2-req-proto.h */
#define PVFS2_FIND_FILES_MAX    60

static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_find_files_with_pattern(
    LPCWSTR          PathName,
    LPCWSTR          SearchPattern,
    PFillFindData    FillFindData, // function pointer
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path, **filename_array;
    int ret, err, count = 0, i, incount, outcount;
    PVFS_ds_position token;
    PVFS_credential credential;
    PVFS_sys_attr *attr_array;
    WIN32_FIND_DATAW find_data;
    wchar_t *wfilename = NULL;
    BY_HANDLE_FILE_INFORMATION hfile_info;
    int match_flag;
    
    client_debug("FindFilesWithPattern: %S\n", PathName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);
    client_debug("   Pattern: %S\n", SearchPattern);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("FindFiles", err);

    /* get file system path */
    fs_path = get_fs_path(PathName);
    if (fs_path == NULL)
    {
        return -1;
    }

    /* max files per request */
    incount = PVFS2_FIND_FILES_MAX;

    /* allocate filename buffers */
    filename_array = (char **) malloc(incount * sizeof(char *));
    MALLOC_CHECK(filename_array);
    for (i = 0; i < incount; i++)
    {
        filename_array[i] = (char *) malloc(PVFS_NAME_MAX + 8);
        MALLOC_CHECK(filename_array[i]);
    }

    /* allocate attr buffer */
    attr_array = (PVFS_sys_attr *) malloc(incount * sizeof(PVFS_sys_attr));
    MALLOC_CHECK(attr_array);

    /* no need to match if pattern is "*" (all files) */
    match_flag = wcscmp(SearchPattern, L"*");

    /* if we have a * (all files) pattern, add . and .. entries */
    if (!match_flag)
    {
        ret = add_dir_entries(fs_path, &credential, FillFindData, DokanFileInfo);
        if (ret != 0)
        {
            goto find_files_exit;
        }
    }

    token = PVFS_READDIR_START;

    /* loop until all files are returned */
    do
    {   
        /* Dokan timeout */
        DokanResetTimeout(30000, DokanFileInfo);

        /* request up to incount files from file system */
        ret = fs_find_files(fs_path, &credential, &token, incount, &outcount, 
                            filename_array, attr_array);
        if (ret != 0)
        {
            client_debug("   fs_find_files returned %d\n", ret);
            goto find_files_exit;
        }

        /* loop through files */
        for (i = 0; i < outcount; i++)
        {
            client_debug("   File found: %s\n", filename_array[i]);

            wfilename = convert_mbstring(filename_array[i]);
            if (wfilename == NULL) {
                ret = -1; /* TODO */
                goto find_files_exit;
            }
            
            /* match file against search pattern */
            if (match_flag)
            {                
                if (!DokanIsNameInExpression(SearchPattern, wfilename, FALSE))
                {
                    client_debug("   File doesn't match\n");
                    goto find_files_no_match;
                }
            }
            
            count++;
            
            /* convert file information */
            memset(&find_data, 0, sizeof(WIN32_FIND_DATAW));
            ret = PVFS_sys_attr_to_file_info(filename_array[i], &credential, 
                &attr_array[i], &hfile_info);
            if (ret != 0)
            {
                client_debug("   PVFS_sys_attr_to_file_info returned %d\n", ret);
                cleanup_string(wfilename);
                goto find_files_exit;
            }
            

            find_data.dwFileAttributes = hfile_info.dwFileAttributes;
            client_debug("   Matching file attrs: %lx\n", find_data.dwFileAttributes);
            memcpy(&find_data.ftCreationTime, &hfile_info.ftCreationTime, 
                   sizeof(FILETIME));
            memcpy(&find_data.ftLastAccessTime, &hfile_info.ftLastAccessTime,
                   sizeof(FILETIME));
            memcpy(&find_data.ftLastWriteTime, &hfile_info.ftLastWriteTime,
                   sizeof(FILETIME));
            find_data.nFileSizeHigh = hfile_info.nFileSizeHigh;
            find_data.nFileSizeLow = hfile_info.nFileSizeLow;
        
            /* copy filename */        
            wcscpy(find_data.cFileName, wfilename);

            /* Dokan callback function */
            FillFindData(&find_data, DokanFileInfo);

find_files_no_match:
            if (wfilename)
            {
                cleanup_string(wfilename);
                wfilename = NULL;
            }

        } /* for */         

    } while (outcount && (incount == outcount));

find_files_exit:    

    /* free filenames */
    for (i = 0; i < incount; i++)
    {
        free(filename_array[i]);
    }
    free(filename_array);

    free(attr_array);

    free(fs_path);
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("FindFiles exit: %d (%d) (%d files)\n", err, ret, count);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_delete_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;         
    PVFS_handle handle;
    PVFS_credential credential;
    int ret, err;

    client_debug("DeleteFile: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("DeleteFile", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* Do not actually remove the file here, just return
       success if file is found. 
       The file/dir will be deleted in PVFS_Dokan_cleanup(). */
    ret = fs_lookup(fs_path, &credential, &handle);

    free(fs_path);
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("DeleteFile exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_delete_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret, err;
    PVFS_credential credential;
    char *fs_path, **filename_array;
    int incount, outcount;
    PVFS_ds_position token;
    PVFS_sys_attr *attr_array;

    client_debug("DeleteDirectory: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential (for add_context */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("DeleteDirectory", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
    {
        return -1;
    }

    /* just look for 1 file/directory */
    incount = 1;

    /* allocate filename buffer */
    filename_array = (char**)malloc(sizeof(char*));
    MALLOC_CHECK(filename_array);
    filename_array[0] = (char*)malloc(PVFS_NAME_MAX + 8);
    MALLOC_CHECK(filename_array[0]);

    /* allocate attr buffer */
    attr_array = (PVFS_sys_attr*)malloc(sizeof(PVFS_sys_attr));
    MALLOC_CHECK(attr_array);

    token = PVFS_READDIR_START;

    /* check whether directory is empty */
    ret = fs_find_files(fs_path, &credential, &token, incount, &outcount,
        filename_array, attr_array);

    if (ret != 0)
    {
        client_debug("   fs_find_files returned %d\n", ret);
        goto delete_directory_exit;
    }

    /* do not delete non-empty directories - return error */
    if (outcount) {
        client_debug("   nonempty directory\n");
        ret = -PVFS_ENOTEMPTY;
        goto delete_directory_exit;
    }

    /* store context with DeleteOnClose flag */
    /* TODO: not needed? */
#if 0
    add_context(DokanFileInfo, FILE_FLAG_DELETE_ON_CLOSE, &credential);
#endif

    /* use same process as a file deletion */
    err = PVFS_Dokan_delete_file(FileName, DokanFileInfo);

delete_directory_exit:

    /* free filename array */
    free(filename_array[0]);
    free(filename_array);

    free(attr_array);

    free(fs_path);

    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("DeleteDirectory exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_move_file(
    LPCWSTR          FileName, // existing file name
    LPCWSTR          NewFileName,
    BOOL             ReplaceIfExisting,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *old_fs_path, *new_fs_path;
    int ret, err;
    PVFS_credential credential;

    client_debug("MoveFile: %S -> %S\n", FileName, NewFileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("MoveFile", err);

    /* get file system path */
    old_fs_path = get_fs_path(FileName);
    if (old_fs_path == NULL)
        return -1;

    new_fs_path = get_fs_path(NewFileName);
    if (new_fs_path == NULL)
    {
        free(old_fs_path);
        return -1;
    }

    /* rename/move the file */
    ret = fs_rename(old_fs_path, new_fs_path, &credential);

    free(old_fs_path);
    free(new_fs_path);
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("MoveFile exit: %d (%d)\n", err, ret);

    return err;
}

static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_lock_file(
    LPCWSTR          FileName,
    LONGLONG         ByteOffset,
    LONGLONG         Length,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    client_debug("LockFile: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS does not currently have a locking mechanism */

    client_debug("LockFile exit: %d\n", 0);

    return 0;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_set_end_of_file(
    LPCWSTR             FileName,
    LONGLONG            ByteOffset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    client_debug("SetEndOfFile %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS doesn't open file handles, so this function is not needed (?) */

    client_debug("SetEndOfFile exit: %d\n", 0);

    return 0;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_set_allocation_size(
    LPCWSTR          FileName,
    LONGLONG         AllocSize,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret, err;
    PVFS_credential credential;
    char *fs_path;

    client_debug("SetAllocationSize %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("SetAllocationSize", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;
    
    /* truncate file */
    ret = fs_truncate(fs_path, AllocSize, &credential);

    free(fs_path);
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("SetAllocationSize exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_set_file_time(
    LPCWSTR          FileName,
    CONST FILETIME*  CreationTime,
    CONST FILETIME*  LastAccessTime,
    CONST FILETIME*  LastWriteTime,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret = 0, err;
    PVFS_credential credential;
    PVFS_sys_attr attr;

    client_debug("SetFileTime: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* load credential */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("SetFileTime", err);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;
        
    /* convert and set the file times */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    if (CreationTime != NULL && !(CreationTime->dwLowDateTime == 0 &&
        CreationTime->dwHighDateTime == 0))
    {
        convert_filetime((LPFILETIME) CreationTime, &attr.ctime);
        client_debug("   Setting CreationTime to %llu\n", attr.ctime);
        attr.mask |= PVFS_ATTR_SYS_CTIME;
    }
    if (LastAccessTime != NULL && !(LastAccessTime->dwLowDateTime == 0 &&
        LastAccessTime->dwHighDateTime == 0))
    {
        convert_filetime((LPFILETIME) LastAccessTime, &attr.atime);
        client_debug("   Setting LastAccessTime to %llu\n", attr.atime);
        attr.mask |= PVFS_ATTR_SYS_ATIME|PVFS_ATTR_SYS_ATIME_SET;
    }
    if (LastWriteTime != NULL && !(LastWriteTime->dwLowDateTime == 0 &&
        LastWriteTime->dwHighDateTime == 0))
    {
        convert_filetime((LPFILETIME) LastWriteTime, &attr.mtime);
        client_debug("   Setting LastWriteTime to %llu\n", attr.mtime);
        attr.mask |= PVFS_ATTR_SYS_MTIME|PVFS_ATTR_SYS_MTIME_SET;
    }
    
    if (attr.mask != 0)
        ret = fs_setattr(fs_path, &attr, &credential);

    free(fs_path);
    PINT_cleanup_credential(&credential);

    err = error_map(ret);

    client_debug("SetFileTime exit: %d (%d)\n", err, ret);

    return err;
}

/* TODO: Not currently in use. Causes Windows Explorer to crash. */
#if 0
static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_get_file_security(
    LPCWSTR               FileName,
    PSECURITY_INFORMATION SecurityInformation, 
    PSECURITY_DESCRIPTOR  SecurityDescriptor,
    ULONG                 BufferLength,
    PULONG                LengthNeeded,
    PDOKAN_FILE_INFO      DokanFileInfo)
{
    SID_IDENTIFIER_AUTHORITY sid_auth_world = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyone_sid = NULL, self_sid = NULL /*guest_sid = NULL*/;
    DWORD self_sid_size = SECURITY_MAX_SID_SIZE;
    EXPLICIT_ACCESS ea;
    PACL acl = NULL;
    PSECURITY_DESCRIPTOR desc = NULL;
    int err = 1;

    client_debug("GetFileSecurity: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);
    client_debug("   BufferLength: %u\n", BufferLength);

    /* debug flags */
    client_debug("   Flags:\n");
    if (*SecurityInformation & DACL_SECURITY_INFORMATION)
        client_debug("      DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & GROUP_SECURITY_INFORMATION)
        client_debug("      GROUP_SECURITY_INFORMATION\n");
    if (*SecurityInformation & LABEL_SECURITY_INFORMATION)
        client_debug("      LABEL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & OWNER_SECURITY_INFORMATION)
        client_debug("      OWNER_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_DACL_SECURITY_INFORMATION)
        client_debug("      PROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_SACL_SECURITY_INFORMATION)
        client_debug("      PROTECTED_SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & SACL_SECURITY_INFORMATION)
        client_debug("      SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_DACL_SECURITY_INFORMATION)
        client_debug("      UNPROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_SACL_SECURITY_INFORMATION)
        client_debug("      UNPROTECTED_SACL_SECURITY_INFORMATION\n");
    
    /* TODO: return all access rights for everyone for now */
    
    /* get SID for Everyone group */
    if (!AllocateAndInitializeSid(&sid_auth_world, 1, SECURITY_WORLD_RID,
               0, 0, 0, 0, 0, 0, 0, &everyone_sid))
    {   
        client_debug("   Could not allocate SID for Everyone\n");
        goto get_file_security_exit;
    }

    /* get SID for Guest account */
    /*
    if (!AllocateAndInitializeSid(&sid_auth_world, 1, DOMAIN_USER_RID_GUEST,
               0, 0, 0, 0, 0, 0, 0, &guest_sid))
    {
        client_debug("   Could not allocate SID for Guest\n");
        goto get_file_security_exit;
    }
    */

    self_sid = LocalAlloc(LMEM_FIXED, self_sid_size);
    if (self_sid == NULL)
    {
        client_debug("   Could not allocate SID for self\n");
        goto get_file_security_exit;
    }

    /* get SID for current account */
    if (!CreateWellKnownSid(WinSelfSid, NULL, self_sid, &self_sid_size))
    {
        client_debug("   Could not create SID for self\n");
        goto get_file_security_exit;
    }

    /* Specify ACE with all rights for everyone */
    ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));
    ea.grfAccessPermissions = KEY_ALL_ACCESS;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = (LPTSTR) everyone_sid;

    /* add entry to the ACL */
    if (SetEntriesInAcl(1, &ea, NULL, &acl) != ERROR_SUCCESS)
    {
        client_debug("   Could not add ACE to ACL\n");
        goto get_file_security_exit;
    }

    /* initialize the descriptor */
    desc = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    MALLOC_CHECK(desc);
    if (!InitializeSecurityDescriptor(desc, 
                SECURITY_DESCRIPTOR_REVISION))
    {
        client_debug("   Could not initialize descriptor\n");
        goto get_file_security_exit;
    }

    /* set primary owner to Guest */
    if (*SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        if (!SetSecurityDescriptorOwner(desc, self_sid, FALSE))
        {
            client_debug("   Could not set descriptor owner\n");
            goto get_file_security_exit;
        }
    }

    /* set primary group to Everyone group */
    if (*SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        if (!SetSecurityDescriptorGroup(desc, everyone_sid, FALSE))
        {
            client_debug("   Could not set descriptor group\n");
            goto get_file_security_exit;
        }
    }

    /* add the ACL to the security descriptor */
    if (*SecurityInformation & DACL_SECURITY_INFORMATION)
    {
       if (!SetSecurityDescriptorDacl(desc, TRUE, acl, FALSE))
       {
           client_debug("   Could not set descriptor DACL\n");
           goto get_file_security_exit;
       }
    }

    *LengthNeeded = GetSecurityDescriptorLength(desc);

    if (BufferLength >= *LengthNeeded)
    {
        ZeroMemory(SecurityDescriptor, BufferLength);
        CopyMemory(SecurityDescriptor, desc, *LengthNeeded);
    }
    else
    {
        client_debug("   Length Needed: %u\n", *LengthNeeded);
        err = -ERROR_INSUFFICIENT_BUFFER;        
    }

get_file_security_exit:
    
    if (desc)
        LocalFree(desc);
    if (acl)
        LocalFree(acl);
    /*
    if (guest_sid)
        FreeSid(guest_sid);
    */
    if (self_sid)
        FreeSid(self_sid);
    if (everyone_sid)
        FreeSid(everyone_sid);

    if (err == 1)
        err = GetLastError() * -1;

    client_debug("GetFileSecurity exit: %d\n", err);

    return err;
}
#endif 

static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_get_file_security(
    LPCWSTR               FileName,
    PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR  SecurityDescriptor,
    ULONG                 BufferLength,
    PULONG                LengthNeeded,
    PDOKAN_FILE_INFO      DokanFileInfo)
{
    client_debug("GetFileSecurity: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    client_debug("GetFileSecurity exit: %d\n", STATUS_NOT_IMPLEMENTED);

    /* allow Dokan to build the Security Descriptor */
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_set_file_security(
    LPCWSTR               FileName,
    PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR  SecurityDescriptor,
    ULONG                 BufferLength, // SecurityDescriptor length
    PDOKAN_FILE_INFO      DokanFileInfo)
{
    int err;

    client_debug("SetFileSecurity: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* debug flags */
    client_debug("   Flags:\n");
    if (*SecurityInformation & DACL_SECURITY_INFORMATION)
        client_debug("      DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & GROUP_SECURITY_INFORMATION)
        client_debug("      GROUP_SECURITY_INFORMATION\n");
    if (*SecurityInformation & LABEL_SECURITY_INFORMATION)
        client_debug("      LABEL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & OWNER_SECURITY_INFORMATION)
        client_debug("      OWNER_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_DACL_SECURITY_INFORMATION)
        client_debug("      PROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_SACL_SECURITY_INFORMATION)
        client_debug("      PROTECTED_SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & SACL_SECURITY_INFORMATION)
        client_debug("      SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_DACL_SECURITY_INFORMATION)
        client_debug("      UNPROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_SACL_SECURITY_INFORMATION)
        client_debug("      UNPROTECTED_SACL_SECURITY_INFORMATION\n");

    /* TODO: no effect for now */

    err = 0;

    client_debug("SetFileSecurity exit: %d\n", err);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_unlock_file(
    LPCWSTR          FileName,
    LONGLONG         ByteOffset,
    LONGLONG         Length,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    client_debug("UnLockFile: %S\n", FileName);
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS does not currently have a locking mechanism */

    client_debug("UnLockFile exit: %d\n", 0);

    return 0;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_unmount(
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    client_debug("Unmount\n");
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    client_debug("Unmount exit: %d\n", 0);

    return 0;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_get_disk_free_space(
    PULONGLONG       FreeBytesAvailable,
    PULONGLONG       TotalNumberOfBytes,
    PULONGLONG       TotalNumberOfFreeBytes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret, err;
    PVFS_gid gid = 0;
    PVFS_credential credential;

    client_debug("GetDiskFreeSpace\n");
    client_debug("   Context: %llx\n", DokanFileInfo->Context);

    /* use root credential for this function */
    err = get_credential(DokanFileInfo, &credential);
    CRED_CHECK("GetDiskFreeSpace", err);

    ret = fs_get_diskfreespace(&credential,
                               (PVFS_size *) FreeBytesAvailable, 
                               (PVFS_size *) TotalNumberOfBytes);

    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        *TotalNumberOfFreeBytes = *FreeBytesAvailable;
        client_debug("   FreeBytesAvailable: %llu\n", *FreeBytesAvailable);
        client_debug("   TotalNumberofBytes: %llu\n", *TotalNumberOfBytes);
    }

    PINT_cleanup_credential(&credential);

    client_debug("GetDiskFreeSpace exit: %d (%d)\n", err, ret);

    return err;
}


static NTSTATUS DOKAN_CALLBACK
PVFS_Dokan_get_volume_information(
    LPWSTR           VolumeNameBuffer,
    DWORD            VolumeNameSize,
    LPDWORD          VolumeSerialNumber,
    LPDWORD          MaximumComponentLength,
    LPDWORD          FileSystemFlags,
    LPWSTR           FileSystemNameBuffer,
    DWORD            FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *vol_name;
    wchar_t *wvol_name;
    size_t vol_len;

    client_debug("GetVolumeInformation\n");
    client_debug("   Context: %llx\n", DokanFileInfo->Context);
    client_debug("   VolumeNameSize: %u\n", VolumeNameSize);
    client_debug("   FileSystemNameSize: %u\n", FileSystemNameSize);

    /* get OrangeFS FS name and convert to Unicode for volume name */
    vol_name = fs_get_name(0);
    client_debug("  FS name: %s\n", vol_name);

    wvol_name = convert_mbstring(vol_name);
    ZeroMemory(VolumeNameBuffer, VolumeNameSize * sizeof(wchar_t));
    vol_len = wcslen(wvol_name);
    if (vol_len > VolumeNameSize - 1)
    {
        vol_len = VolumeNameSize - 1;
    }

    wcsncpy(VolumeNameBuffer, wvol_name, vol_len);
    free(wvol_name);

    /* serial number, comp. length and flags */
    *VolumeSerialNumber = get_volume_serial_num(fs_get_id(0));
    client_debug("  VolumeSerialNumber: %x\n", *VolumeSerialNumber);
    *MaximumComponentLength = PVFS_NAME_MAX;
    *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | 
                       FILE_CASE_PRESERVED_NAMES /* |
                       FILE_NAMED_STREAMS  |
                       FILE_PERSISTENT_ACLS*/ ;

    /* File System Name - report as NTFS for compatibility */
    ZeroMemory(FileSystemNameBuffer, FileSystemNameSize * sizeof(wchar_t));
    /* wcsncpy(FileSystemNameBuffer, L"OrangeFS", 8); */
    wcsncpy(FileSystemNameBuffer, L"NTFS", 4);

    client_debug("   VolumeNameBuffer: %S\n", VolumeNameBuffer);
    client_debug("   VolumeSerialNumber: %08X\n", *VolumeSerialNumber);
    client_debug("   MaximumComponentLength: %u\n", *MaximumComponentLength);
    client_debug("   FileSystemFlags: %08X\n", *FileSystemFlags);
    client_debug("   FileSystemNameBuffer: %S\n", FileSystemNameBuffer);
    client_debug("GetVolumeInformation exit: 0\n");

    return 0;
}


int __cdecl dokan_loop(PORANGEFS_OPTIONS options)
{

    int status;    
    PDOKAN_OPERATIONS dokanOperations = NULL;
    PDOKAN_OPTIONS dokanOptions = NULL;

    if ((dokanOperations = (PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS))) == NULL) {
      return -1;
    }

    if ((dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS))) == NULL) {
      return -1;
    }

    /* init credential cache */
    context_cache = qhash_init(cred_compare, quickhash_64bit_hash, 257);
    gen_mutex_init(&context_cache_mutex);

    g_DebugMode = options->debug;
    g_UseStdErr = options->debug_stderr;

    ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
    dokanOptions->ThreadCount = options->threads;

    if (g_DebugMode)
        dokanOptions->Options |= DOKAN_OPTION_DEBUG;
    
    if (g_UseStdErr)
        dokanOptions->Options |= DOKAN_OPTION_STDERR;
    
    dokanOptions->Options |= DOKAN_OPTION_REMOVABLE;

    dokanOptions->Version = DOKAN_VERSION;

    dokanOptions->MountPoint = convert_mbstring(options->mount_point);

    /* assign file operations */
    ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));
    dokanOperations->ZwCreateFile = PVFS_Dokan_create_file;
    dokanOperations->Cleanup = PVFS_Dokan_cleanup;
    dokanOperations->CloseFile = PVFS_Dokan_close_file;
    dokanOperations->ReadFile = PVFS_Dokan_read_file;
    dokanOperations->WriteFile = PVFS_Dokan_write_file;
    dokanOperations->FlushFileBuffers = PVFS_Dokan_flush_file_buffers;
    dokanOperations->GetFileInformation = PVFS_Dokan_get_file_information;
    dokanOperations->FindFilesWithPattern = PVFS_Dokan_find_files_with_pattern;
    dokanOperations->SetFileAttributes = PVFS_Dokan_set_file_attributes;
    dokanOperations->SetFileTime = PVFS_Dokan_set_file_time;
    dokanOperations->DeleteFile = PVFS_Dokan_delete_file;
    dokanOperations->DeleteDirectory = PVFS_Dokan_delete_directory;
    dokanOperations->MoveFile = PVFS_Dokan_move_file;
    dokanOperations->SetEndOfFile = PVFS_Dokan_set_end_of_file;
    dokanOperations->SetAllocationSize = PVFS_Dokan_set_allocation_size;
    dokanOperations->LockFile = PVFS_Dokan_lock_file;
    dokanOperations->UnlockFile = PVFS_Dokan_unlock_file;
    dokanOperations->GetDiskFreeSpace = PVFS_Dokan_get_disk_free_space;
    dokanOperations->GetVolumeInformation = PVFS_Dokan_get_volume_information;
    dokanOperations->GetFileSecurityA = PVFS_Dokan_get_file_security;
    dokanOperations->SetFileSecurityA = PVFS_Dokan_set_file_security;
    dokanOperations->Unmounted = PVFS_Dokan_unmount;

    /* Attempt to start listening for Dokan messages. Will retry indefinitely.
       If service is stopped or CTRL-C is used, thread will terminate.
       Retry is primarily for waiting for services to be available on system 
       startup. */
    do {
        client_debug("Entering DokanMain\n");

        /* blocks until unmounted */
        status = DokanMain(dokanOptions, dokanOperations);

        client_debug("Exited DokanMain\n");

        switch (status) {
            case DOKAN_SUCCESS:
                client_debug("Success\n");
                break;
            case DOKAN_ERROR:
                client_debug("Error\n");
                break;
            case DOKAN_DRIVE_LETTER_ERROR:
                client_debug("Bad Drive letter\n");
                break;
            case DOKAN_DRIVER_INSTALL_ERROR:
                client_debug("Can't install driver\n");
                break;
            case DOKAN_START_ERROR:
                client_debug("Driver something wrong\n");
                break;
            case DOKAN_MOUNT_ERROR:
                client_debug("Can't assign a drive letter\n");
                break;
            case DOKAN_MOUNT_POINT_ERROR:
                client_debug("Can't assign mount point\n");
                break;
            default:
                client_debug("Unknown error: %d\n", status);
                break;
        }

        if (status != DOKAN_SUCCESS) {
            client_debug("Retrying in 30 seconds...\n");
            Sleep(30000);
        }
    } while (status != DOKAN_SUCCESS);

    cleanup_string((void *) dokanOptions->MountPoint);

    qhash_destroy_and_finalize(context_cache, struct context_entry, hash_link, free);
    gen_mutex_destroy(&context_cache_mutex);

    free(dokanOptions);
    free(dokanOperations);

    ExitThread((DWORD)status);

    return status;
}
