/* TODO: Copyright (C) Omnibond, LLC 2010 */

#include <Windows.h>
#include <AccCtrl.h>
#include <AclAPI.h>
#include <stdio.h>
#include <stdlib.h>
#include "dokan.h"
/* TODO: needed? #include <fileinfo.h> */

#include "pvfs2.h"
#include "str-utils.h"
#include "client-service.h"
#include "fs.h"

FILE *g_DebugFile = NULL;
BOOL g_UseStdErr;
BOOL g_DebugMode;

#define DEBUG_FLAG(val, flag) if (val&flag) { DbgPrint("   "#flag"\n"); }

/* TODO */
#define MALLOC_CHECK(ptr)   if (ptr == NULL) \
                                return -ERROR_NOT_ENOUGH_MEMORY
#define MALLOC_CHECK_N(ptr) if (ptr == NULL) \
                                return NULL

#define DEBUG_PATH(path)   DbgPrint("   resolved path: %s\n", path)

static void DbgInit()
{
    char exe_path[MAX_PATH], *p;
    int ret;

    /* create log file in exe directory */
    ret = GetModuleFileName(NULL, exe_path, MAX_PATH);
    if (ret != 0)
    {
        /* get directory */
        p = strrchr(exe_path, '\\');
        if (p)
            *p = '\0';

        strcat(exe_path, "\\pvfs.log");

        g_DebugFile = fopen(exe_path, "w");
    }

}

static void DbgPrint(LPCSTR format, ...)
{
    if (g_DebugMode) 
    {
        char buffer[512];
        SYSTEMTIME sys_time;
    
        va_list argp;
        va_start(argp, format);
        vsprintf_s(buffer, sizeof(buffer), format, argp);
        va_end(argp);
        if (g_UseStdErr)
        {
            fprintf(stderr, buffer);
        } 
        else
        {
            OutputDebugString(buffer);
        }

        /* log to file */
        if (g_DebugFile != NULL)
        {
            GetLocalTime(&sys_time);
            fprintf(g_DebugFile, "[%d-%02d-%02d %02d:%02d:%02d.%03d] (%4u) %s", 
                    sys_time.wYear, sys_time.wMonth, sys_time.wDay, 
                    sys_time.wHour, sys_time.wMinute, sys_time.wSecond, sys_time.wMilliseconds,
                    GetThreadId(GetCurrentThread()),
                    buffer);
            fflush(g_DebugFile);
        }
    }
}

static void DbgClose()
{
    if (g_DebugFile != NULL)
        fclose(g_DebugFile);
}

/* map a file system error code to a Dokan/Windows code 
   -1 is used as a default error */
static int error_map(int fs_err)
{
    switch (fs_err)
    {
    case 0:
        return ERROR_SUCCESS;         /* 0 */
    case -PVFS_EPERM:          /* Operation not permitted */
        return -ERROR_ACCESS_DENIED;  /* 5 */
    case -PVFS_ENOENT:         /* No such file or directory */
        return -ERROR_FILE_NOT_FOUND;  /* 2 */
    case -PVFS_EINTR:          /* Interrupted system call */
        return -1;
    case -PVFS_EIO:            /* I/O error */
        return -1;
    case -PVFS_ENXIO:          /* No such device or address */
        return -ERROR_DEV_NOT_EXIST;  /* 110 */
    case -PVFS_EBADF:          /* Bad file number */
        return -ERROR_INVALID_HANDLE; /* 6 */
    case -PVFS_EAGAIN:         /* Try again */
        return -1;
    case -PVFS_ENOMEM:         /* Out of memory */
        return -ERROR_NOT_ENOUGH_MEMORY;  /* 8 */
    case -PVFS_EFAULT:         /* Bad address */
        return -ERROR_INVALID_ADDRESS;  /* 487 */
    case -PVFS_EBUSY:          /* Device or resource busy */
        return -ERROR_BUSY;              /* 170 */
    case -PVFS_EEXIST:         /* File exists */
        return -ERROR_ALREADY_EXISTS;    /* 183 */
    case -PVFS_ENODEV:         /* No such device */
        return -ERROR_DEV_NOT_EXIST;     /* 55 */
    case -PVFS_ENOTDIR:        /* Not a directory */
        return -ERROR_DIRECTORY;         /* 267 */
    case -PVFS_EISDIR:         /* Is a directory */
        return -ERROR_DIRECTORY;         /* 267 */
    case -PVFS_EINVAL:         /* Invalid argument */
        return -ERROR_INVALID_PARAMETER; /* 87 */
    case -PVFS_EMFILE:         /* Too many open files */
        return -ERROR_TOO_MANY_OPEN_FILES;  /* 4 */
    case -PVFS_EFBIG:          /* File too large */
        return -ERROR_FILE_TOO_LARGE;       /* 223 */
    case -PVFS_ENOSPC:         /* No space left on device */
        return -ERROR_HANDLE_DISK_FULL;     /* 39 */
    case -PVFS_EROFS:          /* Read-only file system */
        return -ERROR_NOT_SUPPORTED;        /* 50 */
    case -PVFS_EMLINK:         /* Too many links */
        return -ERROR_TOO_MANY_LINKS;       /* 1142 */
    case -PVFS_EPIPE:          /* Broken pipe */
        return -ERROR_BROKEN_PIPE;          /* 109 */
    case -PVFS_EDEADLK:        /* Resource deadlock would occur */
        return -ERROR_POSSIBLE_DEADLOCK;    /* 1131 */
    case -PVFS_ENAMETOOLONG:   /* File name too long */
        return -ERROR_BUFFER_OVERFLOW;      /* 111 */
    case -PVFS_ENOLCK:         /* No record locks available */
        return -ERROR_LOCK_FAILED;          /* 167 */
    case -PVFS_ENOSYS:         /* Function not implemented */
        return -ERROR_CALL_NOT_IMPLEMENTED; /* 120 */
    case -PVFS_ENOTEMPTY:      /* Directory not empty */
        return -ERROR_DIR_NOT_EMPTY;        /* 145 */
    case -PVFS_ELOOP:          /* Too many symbolic links encountered */
        return -ERROR_TOO_MANY_LINKS;       /* 1142 */
    case -PVFS_EWOULDBLOCK:    /* Operation would block */
        return -WSAEWOULDBLOCK;             /* 10035 */
    case -PVFS_ENOMSG:         /* No message of desired type */
        return -ERROR_INVALID_MESSAGE;      /* 1002 */
    case -PVFS_EUNATCH:        /* Protocol driver not attached */
        return -ERROR_FS_DRIVER_REQUIRED;   /* 588 */
    case -PVFS_EBADR:          /* Invalid request descriptor */
    case -PVFS_EDEADLOCK:      /* Deadlock would result */
        return -ERROR_POSSIBLE_DEADLOCK;    /* 1131 */
    case -PVFS_ENODATA:        /* No data available */
        return -ERROR_NO_DATA;              /* 232 */
    case -PVFS_ETIME:          /* Timer expired */
        return -ERROR_TIMEOUT;              /* 1460 */
    case -PVFS_ENONET:         /* Machine is not on the network */
        return -ERROR_NO_NETWORK;           /* 1222 */
    case -PVFS_EREMOTE:        /* Object is remote */
        return -1;          
    case -PVFS_ECOMM:          /* Communication error on send */
        return -1;
    case -PVFS_EPROTO:         /* Protocol error */
        return -1;
    case -PVFS_EBADMSG:        /* Not a data message */
        return -ERROR_INVALID_MESSAGE;      /* 1002 */      
    case -PVFS_EOVERFLOW:      /* Value too large for defined data type */
        return -ERROR_BUFFER_OVERFLOW;      /* 111 */
    case -PVFS_ERESTART:       /* Interrupted system call should be restarted */
        return -1;
    case -PVFS_EMSGSIZE:       /* Message too long */
        return -WSAEMSGSIZE;                /* 10040 */
    case -PVFS_EPROTOTYPE:     /* Protocol wrong type for socket */
        return -WSAEPROTOTYPE;              /* 10041 */
    case -PVFS_ENOPROTOOPT:    /* Protocol not available */
        return -WSAENOPROTOOPT;             /* 10042 */
    case -PVFS_EPROTONOSUPPORT:/* Protocol not supported */
        return -WSAEPROTONOSUPPORT;         /* 10043 */
    case -PVFS_EOPNOTSUPP:     /* Operation not supported on transport endpoint */
        return -WSAEOPNOTSUPP;              /* 10045 */
    case -PVFS_EADDRINUSE:     /* Address already in use */
        return -WSAEADDRINUSE;              /* 10048 */
    case -PVFS_EADDRNOTAVAIL:  /* Cannot assign requested address */
        return -WSAEADDRNOTAVAIL;           /* 10049 */
    case -PVFS_ENETDOWN:       /* Network is down */
        return -WSAENETDOWN;                /* 10050 */
    case -PVFS_ENETUNREACH:    /* Network is unreachable */
        return -WSAENETUNREACH;             /* 10051 */
    case -PVFS_ENETRESET:      /* Network dropped connection because of reset */
        return -WSAENETRESET;               /* 10052 */
    case -PVFS_ENOBUFS:        /* No buffer space available */
        return -WSAENOBUFS;                 /* 10055 */
    case -PVFS_ETIMEDOUT:      /* Connection timed out */
        return -WSAETIMEDOUT;               /* 10060 */
    case -PVFS_ECONNREFUSED:   /* Connection refused */
        return -WSAECONNREFUSED;            /* 10061 */
    case -PVFS_EHOSTDOWN:      /* Host is down */
        return -WSAEHOSTDOWN;               /* 10064 */
    case -PVFS_EHOSTUNREACH:   /* No route to host */
        return -WSAEHOSTUNREACH;            /* 10065 */
    case -PVFS_EALREADY:       /* Operation already in progress */
        return -WSAEALREADY;                /* 10037 */
    case -PVFS_EACCES:         /* Access not allowed */
        return -WSAEACCES;                  /* 10013 */
    case -PVFS_ECONNRESET:    /* Connection reset by peer */
        return -WSAECONNRESET;              /* 10054 */
    }

    return -1;
}

/* convert string from wide char (Unicode) to multi-byte string */
static char *convert_wstring(const wchar_t *wcstr)
{
    errno_t err;
    size_t ret, mb_size;
    char *mbstr;
    
    /* get size of buffer */
    err = wcstombs_s(&ret, NULL, 0, wcstr, 0);

    if (err != 0)
    {
        DbgPrint("convert_wstring: %d\n", err);
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
        DbgPrint("convert_wstring 2: %d\n", err);
        free(mbstr);

        return NULL;
    }

    return mbstr;

}

static wchar_t *convert_mbstring(const char *mbstr)
{
    errno_t err;
    size_t ret, w_size;
    wchar_t *wstr;

    /* get size of buffer */
    err = mbstowcs_s(&ret, NULL, 0, mbstr, 0);

    if (err != 0)
    {
        DbgPrint("convert_mbstring: %d\n", err);
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
        DbgPrint("convert_mbstring 2: %d\n", err);
        free(wstr);

        return NULL;
    }

    return wstr;
}

#define cleanup_string(str)    free(str)

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
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK_N(fs_path);
    ret = fs_resolve_path(mb_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        DbgPrint("   fs_resolve_path returned %d\n");
        cleanup_string(mb_path);
        free(fs_path);
        return NULL;
    }

    DEBUG_PATH(fs_path);

    cleanup_string(mb_path);

    return fs_path;
}


static int __stdcall
PVFS_Dokan_create_file(
    LPCWSTR          FileName,
    DWORD            AccessMode,
    DWORD            ShareMode,
    DWORD            CreationDisposition,
    DWORD            FlagsAndAttributes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, found, err;
    PVFS_handle handle;
    PVFS_sys_attr attr;

    DbgPrint("CreateFile: %S\n", FileName);
    
    if (CreationDisposition == CREATE_NEW)
        DbgPrint("   CREATE_NEW\n");
    if (CreationDisposition == OPEN_ALWAYS)
        DbgPrint("   OPEN_ALWAYS\n");
    if (CreationDisposition == CREATE_ALWAYS)
        DbgPrint("   CREATE_ALWAYS\n");
    if (CreationDisposition == OPEN_EXISTING)
        DbgPrint("   OPEN_EXISTING\n");
    if (CreationDisposition == TRUNCATE_EXISTING)
        DbgPrint("   TRUNCATE_EXISTING\n");

    DbgPrint("   ShareMode = 0x%x\n", ShareMode);

    DEBUG_FLAG(ShareMode, FILE_SHARE_READ);
    DEBUG_FLAG(ShareMode, FILE_SHARE_WRITE);
    DEBUG_FLAG(ShareMode, FILE_SHARE_DELETE);

    DbgPrint("   AccessMode = 0x%x\n", AccessMode);

    DEBUG_FLAG(AccessMode, GENERIC_READ);
    DEBUG_FLAG(AccessMode, GENERIC_WRITE);
    DEBUG_FLAG(AccessMode, GENERIC_EXECUTE);
    
    DEBUG_FLAG(AccessMode, DELETE);
    DEBUG_FLAG(AccessMode, FILE_READ_DATA);
    DEBUG_FLAG(AccessMode, FILE_READ_ATTRIBUTES);
    DEBUG_FLAG(AccessMode, FILE_READ_EA);
    DEBUG_FLAG(AccessMode, READ_CONTROL);
    DEBUG_FLAG(AccessMode, FILE_WRITE_DATA);
    DEBUG_FLAG(AccessMode, FILE_WRITE_ATTRIBUTES);
    DEBUG_FLAG(AccessMode, FILE_WRITE_EA);
    DEBUG_FLAG(AccessMode, FILE_APPEND_DATA);
    DEBUG_FLAG(AccessMode, WRITE_DAC);
    DEBUG_FLAG(AccessMode, WRITE_OWNER);
    DEBUG_FLAG(AccessMode, SYNCHRONIZE);
    DEBUG_FLAG(AccessMode, FILE_EXECUTE);
    DEBUG_FLAG(AccessMode, STANDARD_RIGHTS_READ);
    DEBUG_FLAG(AccessMode, STANDARD_RIGHTS_WRITE);
    DEBUG_FLAG(AccessMode, STANDARD_RIGHTS_EXECUTE);

    DbgPrint("   FlagsAndAttributes = 0x%x\n", FlagsAndAttributes);

    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_ARCHIVE);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_ENCRYPTED);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_HIDDEN);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_NORMAL);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_OFFLINE);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_READONLY);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_SYSTEM);
    DEBUG_FLAG(FlagsAndAttributes, FILE_ATTRIBUTE_TEMPORARY);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_WRITE_THROUGH);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_OVERLAPPED);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_NO_BUFFERING);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_RANDOM_ACCESS);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_SEQUENTIAL_SCAN);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_DELETE_ON_CLOSE);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_BACKUP_SEMANTICS);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_POSIX_SEMANTICS);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_OPEN_REPARSE_POINT);
    DEBUG_FLAG(FlagsAndAttributes, FILE_FLAG_OPEN_NO_RECALL);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_ANONYMOUS);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_IDENTIFICATION);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_IMPERSONATION);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_DELEGATION);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_CONTEXT_TRACKING);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_EFFECTIVE_ONLY);
    DEBUG_FLAG(FlagsAndAttributes, SECURITY_SQOS_PRESENT);

    DokanFileInfo->Context = 0;

    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* look up the file */
    found = 0;
    ret = fs_lookup(fs_path, &handle);    

    DbgPrint("   fs_lookup returns: %d\n", ret);

    if (ret == -PVFS_ENOENT)
    {
        found = 0;
    }
    else if (ret != 0)
    {
        free(fs_path);
        return -1;
    }
    else
    {
        found = 1;
    }

    switch (CreationDisposition)
    {
    case CREATE_ALWAYS:
        if (found)
        {
            fs_remove(fs_path);
        }
        ret = fs_create(fs_path, &handle);
        break;
    case CREATE_NEW:
        if (found) 
        {
            /* set error */
            ret = -PVFS_EEXIST;
        }
        else
        {
            /* create file */
            ret = fs_create(fs_path, &handle);
        }
        break;
    case OPEN_ALWAYS:
        if (!found)
        {    
            /* create file */
            ret = fs_create(fs_path, &handle);
        }
        break;
    case OPEN_EXISTING:
        if (!found)
        {
            /* return error */;
            ret = -PVFS_ENOENT;
        }
        break;
    case TRUNCATE_EXISTING:
        if (!found)
        {
            ret = -PVFS_ENOENT;
        }
        else
        {   
            ret = fs_truncate(fs_path, 0);
        }
    }

    DbgPrint("   fs_create/fs_truncate returns: %d\n", ret);

    
    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        /* save the file handle in context */
        DokanFileInfo->Context = handle;

        DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

        /* determine whether this is a directory */
        ret = fs_getattr(fs_path, &attr);
        if (ret == 0)
        {
            DokanFileInfo->IsDirectory = attr.objtype & PVFS_TYPE_DIRECTORY;
        }
        else
            /* TODO */ ;
    }

    /* assign nonzero context on error so cleanup works */
    /* TODO: use linked list to make sure context is not in use */
    /* TODO: needed?
    if (err != 0) {        
        DokanFileInfo->Context = rand();
    }
    */

    free(fs_path);

    DbgPrint("CreateFile exit: %d (%d)\n", err, ret);
        
    return err;
}


static int __stdcall
PVFS_Dokan_create_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_handle handle;

    DbgPrint("CreateDirectory: %S\n", FileName);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    ret = fs_mkdir(fs_path, &handle);

    DbgPrint("   fs_mkdir returns: %d\n", ret);

    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        DokanFileInfo->IsDirectory = TRUE;
        DokanFileInfo->Context = handle;
    }

    free(fs_path);

    DbgPrint("CreateDirectory exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_open_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_handle handle;
    PVFS_sys_attr attr;

    DbgPrint("OpenDirectory: %S\n", FileName);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* lookup the file */
    ret = fs_lookup(fs_path, &handle);    

    DbgPrint("   fs_lookup returns: %d\n", ret);

    if (ret == 0)
    {
        ret = fs_getattr(fs_path, &attr);
        DbgPrint("   fs_getattr returns: %d\n", ret);
        if (ret == 0)
        {
            if (!(attr.objtype & PVFS_TYPE_DIRECTORY))
            {
                ret = -PVFS_ENOTDIR;
            }
        }
    }

    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        DokanFileInfo->Context = handle;    
        DokanFileInfo->IsDirectory = TRUE;
    }

    free(fs_path);

    DbgPrint("OpenDirectory exit: %d (%d)\n", err, ret);
    
    return err;
}


static int __stdcall
PVFS_Dokan_close_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path = NULL;
    int ret = 0, err;

    DbgPrint("CloseFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* delete the file/dir if DeleteOnClose specified */
    if (DokanFileInfo->DeleteOnClose)
    {
        /* get file system path */
        fs_path = get_fs_path(FileName);
        if (fs_path == NULL)
            return -1;

        /* remove the file/dir */
        ret = fs_remove(fs_path);
    }

    /* PVFS doesn't have a close-file semantic.
       Simply clear the handle. */
    DokanFileInfo->Context = 0;

    if (fs_path != NULL)
        free(fs_path);

    err = error_map(ret);

    DbgPrint("CloseFile exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_cleanup(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    DbgPrint("Cleanup: %S\n", FileName);
    DbgPrint("  Context: %llx\n", DokanFileInfo->Context);

    /* needed?
    if (DokanFileInfo->Context == 0) {
    }
    else {
        DbgPrint("  Cleanup: invalid handle\n");
        return -1;
    }
    */

    DbgPrint("Cleanup exit: %d\n", 0);

    return 0;
}


static int __stdcall
PVFS_Dokan_read_file(
    LPCWSTR          FileName,
    LPVOID           Buffer,
    DWORD            BufferLength,
    LPDWORD          ReadLength,
    LONGLONG         Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    PVFS_size len64;
    int ret, err;
    
    DbgPrint("ReadFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);
    DbgPrint("   BufferLength: %lu\n", BufferLength);
    DbgPrint("   Offset: %llu\n", Offset);

    if (FileName == NULL || wcslen(FileName) == 0 ||
        Buffer == NULL || BufferLength == 0 || 
        ReadLength == 0)
        return -1;

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;
    
    /* perform the read operation */
    ret = fs_read(fs_path, Buffer, BufferLength, Offset, &len64);
    *ReadLength = (DWORD) len64;

    free(fs_path);    

    err = error_map(ret);
    
    DbgPrint("ReadFile exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_write_file(
    LPCWSTR          FileName,
    LPCVOID          Buffer,
    DWORD            NumberOfBytesToWrite,
    LPDWORD          NumberOfBytesWritten,
    LONGLONG         Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    PVFS_size len64;
    int ret, err;

    DbgPrint("WriteFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    DEBUG_PATH(fs_path);
    
    /* perform the read operation */
    ret = fs_write(fs_path, (void *) Buffer, NumberOfBytesToWrite, Offset, 
                   &len64);
    *NumberOfBytesWritten = (DWORD) len64;

    free(fs_path);

    err = error_map(ret);

    DbgPrint("WriteFile exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_flush_file_buffers(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;

    DbgPrint("FlushFileBuffers: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* flush the file */
    ret = fs_flush(fs_path);

    err = error_map(ret);

    free(fs_path);

    DbgPrint("FlushFileBuffers exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_get_file_information(
    LPCWSTR                      FileName,
    LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO             DokanFileInfo)
{
    char *fs_path, *filename;
    int ret, err;
    PVFS_sys_attr attr;

    DbgPrint("GetFileInfo: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* get file attributes */
    ret = fs_getattr(fs_path, &attr);

    if (ret == 0)
    {
        /* convert to Windows attributes */
        HandleFileInformation->dwFileAttributes = 0;
        if (attr.objtype & PVFS_TYPE_DIRECTORY) 
            HandleFileInformation->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

        /* check for hidden file */
        filename = (char *) malloc(strlen(fs_path) + 1);
        MALLOC_CHECK(filename);
        ret = PINT_remove_base_dir(fs_path, filename, strlen(fs_path) + 1);
        if (ret == 0 && filename[0] == '.')
            HandleFileInformation->dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
        free(filename);
        ret = 0;
        
        /* TODO
           Check perms for READONLY */
        
        /* check for temporary file */
        if (DokanFileInfo->DeleteOnClose)
            HandleFileInformation->dwFileAttributes |= FILE_ATTRIBUTE_TEMPORARY;

        /* normal file */
        if (HandleFileInformation->dwFileAttributes == 0)
            HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        
        /* file times */
        convert_pvfstime(attr.ctime, &HandleFileInformation->ftCreationTime);
        convert_pvfstime(attr.atime, &HandleFileInformation->ftLastAccessTime);
        convert_pvfstime(attr.mtime, &HandleFileInformation->ftLastWriteTime);

        /* file size */
        HandleFileInformation->nFileSizeHigh = (attr.size & 0x7FFFFFFF00000000LL) >> 32;
        HandleFileInformation->nFileSizeLow = (attr.size & 0xFFFFFFFFLL);

    }    
    
    err = error_map(ret);

    free(fs_path);

    DbgPrint("GetFileInfo exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_set_file_attributes(
    LPCWSTR          FileName,
    DWORD            FileAttributes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_sys_attr attr;

    DbgPrint("SetFileAttributes: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* convert attributes to PVFS */
    ret = fs_getattr(fs_path, &attr);

    if (ret == 0)
    {
        if (FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            attr.objtype = PVFS_TYPE_DIRECTORY;
        else if (FileAttributes & FILE_ATTRIBUTE_NORMAL)
            attr.objtype = PVFS_TYPE_DATAFILE;
        if (FileAttributes & FILE_ATTRIBUTE_READONLY)
            /* TODO: permissions */ ;

        ret = fs_setattr(fs_path, &attr);
    }

    free(fs_path);

    err = error_map(ret);

    DbgPrint("SetFileAttributes exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_find_files(
    LPCWSTR          FileName,
    PFillFindData    FillFindData, // function pointer
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path,
         filename[PVFS_NAME_MAX], *full_path;
    int ret, err, count = 0;
    PVFS_ds_position token;
    WIN32_FIND_DATAW find_data;
    wchar_t *wpath, *wfilename;
    BY_HANDLE_FILE_INFORMATION hfile_info;
    
    DbgPrint("FindFiles: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* find the first file */
    ret = fs_find_first_file(fs_path, &token, filename, PVFS_NAME_MAX);
    if (ret != 0)
        goto find_files_exit;

    while (strlen(filename) > 0)
    {        
        DbgPrint("   File found: %s\n", filename);
        count++;
        /* append the filename to the dir path */
        full_path = (char *) malloc(strlen(fs_path) + strlen(filename) + 2);
        MALLOC_CHECK(full_path);
        strcpy(full_path, fs_path);
        if (full_path[strlen(full_path)-1] != '/') 
            strcat(full_path, "/");
        strcat(full_path, filename);
        DbgPrint("   Full path: %s\n", full_path);

        /* get file information */
        memset(&find_data, 0, sizeof(WIN32_FIND_DATAW));
        wpath = convert_mbstring(full_path);
        wfilename = convert_mbstring(filename);
        ret = PVFS_Dokan_get_file_information(wpath, &hfile_info, DokanFileInfo);
        if (ret != 0) 
        {
            free(full_path);
            cleanup_string(wpath);
            cleanup_string(wfilename);
            goto find_files_exit;
        }
        find_data.dwFileAttributes = hfile_info.dwFileAttributes;
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

        free(full_path);
        cleanup_string(wpath);
        cleanup_string(wfilename);

        /* find next file */
        ret = fs_find_next_file(fs_path, &token, filename, PVFS_NAME_MAX);

        if (ret != 0)
            goto find_files_exit;
    }

find_files_exit:    

    DbgPrint("   fs_find_xxx_file returns: %d\n", ret);

    free(fs_path);

    err = error_map(ret);

    DbgPrint("FindFiles exit: %d (%d) (%d files)\n", err, ret, count);

    return err;
}


static int __stdcall
PVFS_Dokan_delete_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;         
    PVFS_handle handle;
    int ret, err;

    DbgPrint("DeleteFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);
    
    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* Do not actually remove the file here, just return
       success if file is found. 
       The file/dir will be deleted in close_file(). */
    ret = fs_lookup(fs_path, &handle);

    free(fs_path);

    err = error_map(ret);

    DbgPrint("DeleteFile exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_delete_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int err;

    DbgPrint("DeleteDirectory: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    err = PVFS_Dokan_delete_file(FileName, DokanFileInfo);

    DbgPrint("DeleteDirectory exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_move_file(
    LPCWSTR          FileName, // existing file name
    LPCWSTR          NewFileName,
    BOOL             ReplaceIfExisting,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *old_fs_path, *new_fs_path;
    int ret, err;

    DbgPrint("MoveFile: %S -> %S\n", FileName, NewFileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

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
    ret = fs_rename(old_fs_path, new_fs_path);

    free(old_fs_path);
    free(new_fs_path);

    err = error_map(ret);

    DbgPrint("MoveFile exit: %d (%d)\n", err, ret);

    return err;
}

static int __stdcall
PVFS_Dokan_lock_file(
    LPCWSTR          FileName,
    LONGLONG         ByteOffset,
    LONGLONG         Length,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    DbgPrint("LockFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS does not currently have a locking mechanism */

    DbgPrint("LockFile exit: %d\n", 0);

    return 0;
}


static int __stdcall
PVFS_Dokan_set_end_of_file(
    LPCWSTR                FileName,
    LONGLONG            ByteOffset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    DbgPrint("SetEndOfFile %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS doesn't open file handles, so this function is not needed (?) */

    DbgPrint("SetEndOfFile exit: %d\n", 0);

    return 0;
}


static int __stdcall
PVFS_Dokan_set_allocation_size(
    LPCWSTR          FileName,
    LONGLONG         AllocSize,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    DbgPrint("SetAllocationSize %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* function not needed (?) */

    DbgPrint("SetAllocationSize exit: %d\n", 0);

    return 0;
}


static int __stdcall
PVFS_Dokan_set_file_time(
    LPCWSTR          FileName,
    CONST FILETIME*  CreationTime,
    CONST FILETIME*  LastAccessTime,
    CONST FILETIME*  LastWriteTime,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *fs_path;
    int ret, err;
    PVFS_sys_attr attr;

    DbgPrint("SetFileTime: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* get file system path */
    fs_path = get_fs_path(FileName);
    if (fs_path == NULL)
        return -1;

    /* convert times to PVFS */
    ret = fs_getattr(fs_path, &attr);

    DbgPrint("   fs_getattr returns: %d\n", ret);

    if (ret == 0)
    {
        convert_filetime((LPFILETIME) CreationTime, &attr.ctime);
        convert_filetime((LPFILETIME) LastAccessTime, &attr.atime);
        convert_filetime((LPFILETIME) LastWriteTime, &attr.mtime);

        ret = fs_setattr(fs_path, &attr);        

        DbgPrint("   fs_setattr returns: %d\n", ret);
    }

    free(fs_path);

    err = error_map(ret);

    DbgPrint("SetFileTime exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_get_file_security(
    LPCWSTR               FileName,
    PSECURITY_INFORMATION SecurityInformation, 
    PSECURITY_DESCRIPTOR  SecurityDescriptor,
    ULONG                 BufferLength,
    PULONG                LengthNeeded,
    PDOKAN_FILE_INFO      DokanFileInfo)
{
    SID_IDENTIFIER_AUTHORITY sid_auth_world = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyone_sid = NULL, guest_sid = NULL;
    EXPLICIT_ACCESS ea;
    PACL acl = NULL;
    PSECURITY_DESCRIPTOR desc;
    int err = 1;

    DbgPrint("GetFileSecurity: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);
    DbgPrint("   BufferLength: %u\n", BufferLength);

    /* debug flags */
    DbgPrint("   Flags:\n");
    if (*SecurityInformation & DACL_SECURITY_INFORMATION)
        DbgPrint("      DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & GROUP_SECURITY_INFORMATION)
        DbgPrint("      GROUP_SECURITY_INFORMATION\n");
    if (*SecurityInformation & LABEL_SECURITY_INFORMATION)
        DbgPrint("      LABEL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & OWNER_SECURITY_INFORMATION)
        DbgPrint("      OWNER_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_DACL_SECURITY_INFORMATION)
        DbgPrint("      PROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_SACL_SECURITY_INFORMATION)
        DbgPrint("      PROTECTED_SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & SACL_SECURITY_INFORMATION)
        DbgPrint("      SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_DACL_SECURITY_INFORMATION)
        DbgPrint("      UNPROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_SACL_SECURITY_INFORMATION)
        DbgPrint("      UNPROTECTED_SACL_SECURITY_INFORMATION\n");
    
    /* TODO: return all access rights for everyone for now */
    
    /* get SID for Everyone group */
    if (!AllocateAndInitializeSid(&sid_auth_world, 1, SECURITY_WORLD_RID,
               0, 0, 0, 0, 0, 0, 0, &everyone_sid))
    {   
        DbgPrint("   Could not allocate SID for Everyone\n");
        goto get_file_security_exit;
    }

    /* get SID for Guest account */
    if (!AllocateAndInitializeSid(&sid_auth_world, 1, DOMAIN_USER_RID_GUEST,
               0, 0, 0, 0, 0, 0, 0, &guest_sid))
    {
        DbgPrint("   Could not allocate SID for Guest\n");
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
        DbgPrint("   Could not add ACE to ACL\n");
        goto get_file_security_exit;
    }

    /* initialize the descriptor */
    desc = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    MALLOC_CHECK(desc);
    if (!InitializeSecurityDescriptor(desc, 
                SECURITY_DESCRIPTOR_REVISION))
    {
        DbgPrint("   Could not initialize descriptor\n");
        goto get_file_security_exit;
    }

    /* set primary owner to Guest */
    if (*SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        if (!SetSecurityDescriptorOwner(desc, guest_sid, FALSE))
        {
            DbgPrint("   Could not set descriptor owner\n");
            goto get_file_security_exit;
        }
    }

    /* set primary group to Everyone group */
    if (*SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        if (!SetSecurityDescriptorGroup(desc, everyone_sid, FALSE))
        {
            DbgPrint("   Could not set descriptor group\n");
            goto get_file_security_exit;
        }
    }

    /* add the ACL to the security descriptor */
    if (*SecurityInformation & DACL_SECURITY_INFORMATION)
    {
       if (!SetSecurityDescriptorDacl(desc, TRUE, acl, FALSE))
       {
           DbgPrint("   Could not set descriptor DACL\n");
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
        DbgPrint("   Length Needed: %u\n", *LengthNeeded);
        err = -ERROR_INSUFFICIENT_BUFFER;        
    }

get_file_security_exit:
    
    if (desc)
        LocalFree(desc);
    if (acl)
        LocalFree(acl);
    if (guest_sid)
        FreeSid(guest_sid);
    if (everyone_sid)
        FreeSid(everyone_sid);

    if (err == 1)
        err = GetLastError() * -1;

    DbgPrint("GetFileSecurity exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_set_file_security(
    LPCWSTR               FileName,
    PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR  SecurityDescriptor,
    ULONG                 BufferLength, // SecurityDescriptor length
    PDOKAN_FILE_INFO      DokanFileInfo)
{
    int err;

    DbgPrint("SetFileSecurity: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* debug flags */
    DbgPrint("   Flags:\n");
    if (*SecurityInformation & DACL_SECURITY_INFORMATION)
        DbgPrint("      DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & GROUP_SECURITY_INFORMATION)
        DbgPrint("      GROUP_SECURITY_INFORMATION\n");
    if (*SecurityInformation & LABEL_SECURITY_INFORMATION)
        DbgPrint("      LABEL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & OWNER_SECURITY_INFORMATION)
        DbgPrint("      OWNER_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_DACL_SECURITY_INFORMATION)
        DbgPrint("      PROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & PROTECTED_SACL_SECURITY_INFORMATION)
        DbgPrint("      PROTECTED_SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & SACL_SECURITY_INFORMATION)
        DbgPrint("      SACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_DACL_SECURITY_INFORMATION)
        DbgPrint("      UNPROTECTED_DACL_SECURITY_INFORMATION\n");
    if (*SecurityInformation & UNPROTECTED_SACL_SECURITY_INFORMATION)
        DbgPrint("      UNPROTECTED_SACL_SECURITY_INFORMATION\n");

    /* TODO: no effect for now */

    err = 0;

    DbgPrint("SetFileSecurity exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_unlock_file(
    LPCWSTR          FileName,
    LONGLONG         ByteOffset,
    LONGLONG         Length,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    DbgPrint("UnLockFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS does not currently have a locking mechanism */

    DbgPrint("UnLockFile exit: %d\n", 0);

    return 0;
}


static int __stdcall
PVFS_Dokan_unmount(
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    DbgPrint("Unmount\n");
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);
    DbgPrint("Unmount exit: %d\n", 0);

    return 0;
}


static int __stdcall
PVFS_Dokan_get_disk_free_space(
    PULONGLONG       FreeBytesAvailable,
    PULONGLONG       TotalNumberOfBytes,
    PULONGLONG       TotalNumberOfFreeBytes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret, err;

    DbgPrint("GetDiskFreeSpace\n");
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    ret = fs_get_diskfreespace((PVFS_size *) FreeBytesAvailable, 
                               (PVFS_size *) TotalNumberOfBytes);

    err = error_map(ret);
    if (err == ERROR_SUCCESS)
    {
        *TotalNumberOfFreeBytes = *FreeBytesAvailable;
    }

    DbgPrint("GetDiskFreeSpace exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
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

    DbgPrint("GetVolumeInformation\n");
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* volume name */
    vol_name = fs_get_name(0);
    wvol_name = convert_mbstring(vol_name);
    /* bug in volume.c -- use length of wvol_name */
    wcsncpy(VolumeNameBuffer, wvol_name, wcslen(wvol_name));
    free(wvol_name);

    /* serial number, comp. length and flags */
    *VolumeSerialNumber = fs_get_id(0);
    *MaximumComponentLength = PVFS_NAME_MAX;
    *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | 
                       FILE_CASE_PRESERVED_NAMES;
                       /*
                       FILE_SUPPORTS_REMOTE_STORAGE |
                       FILE_PERSISTENT_ACLS;
                       */

    /* File System Name */
    /* bug in volume.c -- see above */
    wcsncpy(FileSystemNameBuffer, L"OrangeFS", 8);

    DbgPrint("GetVolumeInformation exit: 0\n");

    return 0;
}


int __cdecl dokan_loop(PORANGEFS_OPTIONS options)
{

    int status;
    PDOKAN_OPERATIONS dokanOperations =
            (PDOKAN_OPERATIONS) malloc(sizeof(DOKAN_OPERATIONS));
    PDOKAN_OPTIONS dokanOptions =
            (PDOKAN_OPTIONS) malloc(sizeof(DOKAN_OPTIONS));

#ifdef _DEBUG
    g_DebugMode = g_UseStdErr = TRUE;
#else
    g_DebugMode = g_UseStdErr = FALSE;
#endif

    ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
    dokanOptions->ThreadCount = 0; /* use default */

    DbgInit();

    if (g_DebugMode)
        dokanOptions->Options |= DOKAN_OPTION_DEBUG;
    if (g_UseStdErr)
        dokanOptions->Options |= DOKAN_OPTION_STDERR;

    dokanOptions->Options |= DOKAN_OPTION_KEEP_ALIVE;

    dokanOptions->Version = 600;
    /* Hard coded for now */
    dokanOptions->MountPoint = convert_mbstring(options->mount_point);

    /* assign file operations */
    ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));
    dokanOperations->CreateFile = PVFS_Dokan_create_file;
    dokanOperations->OpenDirectory = PVFS_Dokan_open_directory;
    dokanOperations->CreateDirectory = PVFS_Dokan_create_directory;
    dokanOperations->Cleanup = PVFS_Dokan_cleanup;
    dokanOperations->CloseFile = PVFS_Dokan_close_file;
    dokanOperations->ReadFile = PVFS_Dokan_read_file;
    dokanOperations->WriteFile = PVFS_Dokan_write_file;
    dokanOperations->FlushFileBuffers = PVFS_Dokan_flush_file_buffers;
    dokanOperations->GetFileInformation = PVFS_Dokan_get_file_information;
    dokanOperations->FindFiles = PVFS_Dokan_find_files;
    dokanOperations->FindFilesWithPattern = NULL;
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
    dokanOperations->Unmount = PVFS_Dokan_unmount;

    DbgPrint("Entering DokanMain\n");

    /* TODO: dokan loops until termination */
    status = DokanMain(dokanOptions, dokanOperations);

    DbgPrint("Exited DokanMain\n");

    switch (status) {
        case DOKAN_SUCCESS:
            DbgPrint("Success\n");
            break;
        case DOKAN_ERROR:
            DbgPrint("Error\n");
            break;
        case DOKAN_DRIVE_LETTER_ERROR:
            DbgPrint("Bad Drive letter\n");
            break;
        case DOKAN_DRIVER_INSTALL_ERROR:
            DbgPrint("Can't install driver\n");
            break;
        case DOKAN_START_ERROR:
            DbgPrint("Driver something wrong\n");
            break;
        case DOKAN_MOUNT_ERROR:
            DbgPrint("Can't assign a drive letter\n");
            break;
        default:
            DbgPrint("Unknown error: %d\n", status);
            break;
    }

    free(dokanOptions);
    free(dokanOperations);

    DbgClose();

    return status;

}
