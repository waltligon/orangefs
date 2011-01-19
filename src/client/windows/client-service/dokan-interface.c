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
#include "fs.h"

FILE *g_DebugFile = NULL;
BOOL g_UseStdErr;
BOOL g_DebugMode;

#define DEBUG_FLAG(val, flag) if (val&flag) { DbgPrint("   "#flag"\n"); }

/* TODO */
#define MALLOC_CHECK(ptr)   if (ptr == NULL) \
                                return -ERROR_NOT_ENOUGH_MEMORY

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

static char RootDirectory[MAX_PATH] = "C:";

static void
GetFilePath(
    char   *filePath,
    LPCSTR FileName)
{
    RtlZeroMemory(filePath, MAX_PATH);
    strncpy(filePath, RootDirectory, strlen(RootDirectory));
    strncat(filePath, FileName, strlen(FileName));
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


static int __stdcall
PVFS_Dokan_create_file(
    LPCWSTR          FileName,
    DWORD            AccessMode,
    DWORD            ShareMode,
    DWORD            CreationDisposition,
    DWORD            FlagsAndAttributes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
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

    /*
    if (ShareMode == 0 && AccessMode & FILE_WRITE_DATA)
        ShareMode = FILE_SHARE_WRITE;
    else if (ShareMode == 0)
        ShareMode = FILE_SHARE_READ;
    */

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

    // When filePath is a directory, needs to change the flag so that the file can be opened.
    /*
    fileAttr = GetFileAttributes(filePath);
    if (fileAttr && fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
        FlagsAndAttributes |= FILE_FLAG_BACKUP_SEMANTICS;
        //AccessMode = 0;
    }
    */
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

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

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

    switch (ret)
    {
    case 0:
        err = 0;
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
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    case -PVFS_EEXIST:
        err = -ERROR_ALREADY_EXISTS;
        break;
    default: 
        /* TODO: default error */
        err = -1;
    }

    /* assign nonzero context on error so cleanup works */
    /* TODO: use linked list to make sure context is not in use */
    if (err != 0) {        
        /* DokanFileInfo->Context = (ULONG64) ((double) rand() / (double) RAND_MAX) * (double) _UI64_MAX; */
        DokanFileInfo->Context = rand();
        /*if (!strcmp(local_path, "\\testdir3"))
            DebugBreak(); */
    }

    free(local_path);
    free(fs_path);

    DbgPrint("CreateFile exit: %d\n", err);
        
    return err;
}


static int __stdcall
PVFS_Dokan_create_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    int ret, err;
    PVFS_handle handle;

    DbgPrint("CreateDirectory: %S\n", FileName);

    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

    ret = fs_mkdir(fs_path, &handle);

    DbgPrint("   fs_mkdir returns: %d\n", ret);

    switch (ret)
    {
    case 0: 
        err = 0;
        DokanFileInfo->IsDirectory = TRUE;
        DokanFileInfo->Context = handle;
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    case -PVFS_EEXIST:
        err = -ERROR_ALREADY_EXISTS;    
        break;
    default:
        err = -1;
    }

    free(local_path);
    free(fs_path);

    DbgPrint("CreateDirectory exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_open_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    int ret, err;
    PVFS_handle handle;
    PVFS_sys_attr attr;

    DbgPrint("OpenDirectory: %S\n", FileName);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

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

    switch (ret)
    {
    case 0: 
        err = 0;
        DokanFileInfo->Context = handle;    
        DokanFileInfo->IsDirectory = TRUE;
        break;
    case -PVFS_ENOTDIR:
        err = -ERROR_DIRECTORY;
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    default: 
        err = -1;  /* TODO */
    }

    free(local_path);
    free(fs_path);

    DbgPrint("OpenDirectory exit: %d\n", err);
    
    return err;
}


static int __stdcall
PVFS_Dokan_close_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path = NULL, *fs_path = NULL;
    int ret = 0, err;

    DbgPrint("CloseFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* delete the file/dir if DeleteOnClose specified */
    if (DokanFileInfo->DeleteOnClose)
    {
        /* convert from Unicode */
        local_path = convert_wstring(FileName);
        if (local_path == NULL)
        {
            return -ERROR_INVALID_DATA;
        }

        /* resolve the path */
        fs_path = (char *) malloc(MAX_PATH);
        MALLOC_CHECK(fs_path);
        ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
        if (ret != 0)
            goto close_exit;

        DEBUG_PATH(fs_path);

        /* remove the file/dir */
        ret = fs_remove(fs_path);
    }

    /* PVFS doesn't have a close-file semantic.
       Simply clear the handle. */
    DokanFileInfo->Context = 0;

close_exit:

    if (local_path != NULL)
        cleanup_string(local_path);
    if (fs_path != NULL)
        free(fs_path);

    switch (ret)
    {
    case 0:
        err = 0;
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    case -PVFS_ENOTEMPTY:
        err = -ERROR_DIR_NOT_EMPTY;
        break;
    default:
        err = -1;
    }

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

    if (DokanFileInfo->Context) {
        /**** TODO
        if (DokanFileInfo->DeleteOnClose) {
            DbgPrint("DeleteOnClose\n");
            if (DokanFileInfo->IsDirectory) {
                DbgPrint("  DeleteDirectory ");
            } else {
                DbgPrint("  DeleteFile ");
            }
        }
        */ 
    } else {
        DbgPrint("  Cleanup: invalid handle\n");
        return -1;
    }

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
    char *local_path, *fs_path;
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

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);
    
    /* perform the read operation */
    ret = fs_read(fs_path, Buffer, BufferLength, Offset, &len64);
    *ReadLength = (DWORD) len64;
    /* for testing */
    /*
    memset(Buffer, 0, BufferLength);
    *ReadLength = BufferLength;
    ret = 0;
    */

    DbgPrint("   fs_read returns: %d\n", ret);

    switch (ret)
    {
    case 0: 
        /*
        strcpy(sbuf, "   Buffer bytes: ");
        for (i = 0, buf = (char *) Buffer; i < (*ReadLength > 8 ? 8 : *ReadLength); i++)
        {
            sprintf(byte, "%02X ", buf[i]);
            strcat(sbuf, byte);
        }
        DbgPrint("%s\n", sbuf);
        */
        err = 0;
        break;
    default:
        err = -1;
    }

    DbgPrint("ReadFile exit: %d\n", err);

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
    char *local_path, *fs_path;
    PVFS_size len64;
    int ret, err;

    DbgPrint("WriteFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);
    
    /* perform the read operation */
    ret = fs_write(fs_path, (void *) Buffer, NumberOfBytesToWrite, Offset, 
                   &len64);
    *NumberOfBytesWritten = (DWORD) len64;

    DbgPrint("   fs_write returns: %d\n", ret);

    switch (ret)
    {
    case 0: 
        err = 0;
        break;
    default:
        err = -1;
    }

    DbgPrint("WriteFile exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_flush_file_buffers(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    int ret, err;

    DbgPrint("FlushFileBuffers: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

    /* flush the file */
    ret = fs_flush(fs_path);

    DbgPrint("   fs_flush returns: %d\n", ret);

    switch (ret)
    {
    case 0: 
        err = 0;
        break;
    default:
        err = -1;
    }

    free(fs_path);
    free(local_path);

    DbgPrint("FlushFileBuffers exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_get_file_information(
    LPCWSTR                      FileName,
    LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO             DokanFileInfo)
{
    char *local_path, *fs_path, *filename;
    int ret, err;
    PVFS_sys_attr attr;

    DbgPrint("GetFileInfo: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

    /* get file attributes */
    ret = fs_getattr(fs_path, &attr);

    DbgPrint("   fs_getattr returns: %d\n", ret);

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
    
    switch (ret)
    {
    case 0: 
        err = 0;
        break;
    default:
        err = -1;
    }

    cleanup_string(local_path);
    free(fs_path);

    DbgPrint("GetFileInfo exit: %d\n", err);

    return err;
}


static int __stdcall
PVFS_Dokan_set_file_attributes(
    LPCWSTR          FileName,
    DWORD            FileAttributes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    int ret, err;
    PVFS_sys_attr attr;

    DbgPrint("SetFileAttributes: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

    /* convert attributes to PVFS */
    ret = fs_getattr(fs_path, &attr);

    DbgPrint("   fs_getattr returns: %d\n", ret);

    if (ret == 0)
    {
        if (FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            attr.objtype = PVFS_TYPE_DIRECTORY;
        else if (FileAttributes & FILE_ATTRIBUTE_NORMAL)
            attr.objtype = PVFS_TYPE_DATAFILE;
        if (FileAttributes & FILE_ATTRIBUTE_READONLY)
            /* TODO: permissions */ ;

        ret = fs_setattr(fs_path, &attr);

        DbgPrint("   fs_setattr returns: %d\n", ret);
    }

    switch (ret)
    {
    case 0: 
        err = 0;
        break;
    default:
        err = -1;
    }

    free(fs_path);
    cleanup_string(local_path);

    DbgPrint("SetFileAttributes exit: %d\n", err);
    return err;
}


static int __stdcall
PVFS_Dokan_find_files(
    LPCWSTR          FileName,
    PFillFindData    FillFindData, // function pointer
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path,
         filename[PVFS_NAME_MAX], *full_path;
    int ret, err, count = 0;
    PVFS_ds_position token;
    WIN32_FIND_DATAW find_data;
    wchar_t *wpath, *wfilename;
    BY_HANDLE_FILE_INFORMATION hfile_info;
    
    DbgPrint("FindFiles: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
        goto find_files_exit;

    DEBUG_PATH(fs_path);

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

    cleanup_string(local_path);
    free(fs_path);

    switch (ret)
    {
    case 0: 
        err = 0;
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    default: 
        err = -1;
    }

    DbgPrint("FindFiles exit: %d (%d files)\n", err, count);

    return err;
}


static int __stdcall
PVFS_Dokan_delete_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;         
    int ret, err;

    DbgPrint("DeleteFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);
    
    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);

    DEBUG_PATH(fs_path);

    /* Do not actually remove the file here, just return
       success if file is found. Note that directories
       will be removed even if they contain files.
       The file/dir will be deleted in close_file(). */

    switch (ret)
    {
    case 0:
        err = 0;
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    case -PVFS_ENOTEMPTY:
        err = -ERROR_DIR_NOT_EMPTY;
        break;
    default:
        err = -1;
    }

    cleanup_string(local_path);
    free(fs_path);

    DbgPrint("DeleteFile exit: %d (%d)\n", err, ret);

    return err;
}


static int __stdcall
PVFS_Dokan_delete_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret;

    DbgPrint("DeleteDirectory: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /*** TODO ***/
    ret = PVFS_Dokan_delete_file(FileName, DokanFileInfo);

#if 0
    fileLen = wcslen(filePath);
    if (filePath[fileLen-1] != L'\\') {
        filePath[fileLen++] = L'\\';
    }
    filePath[fileLen] = L'*';

    hFind = FindFirstFile(filePath, &findData);
    while (hFind != INVALID_HANDLE_VALUE) {
        if (wcscmp(findData.cFileName, "..") != 0 &&
            wcscmp(findData.cFileName, ".") != 0) {
            FindClose(hFind);
            DbgPrint("  Directory is not empty: %s\n", findData.cFileName);
            return -(int)ERROR_DIR_NOT_EMPTY;
        }
        if (!FindNextFile(hFind, &findData)) {
            break;
        }
    }
    FindClose(hFind);

    if (GetLastError() == ERROR_NO_MORE_FILES) {
        return 0;
    } else {
        return -1;
    }
#endif 

    DbgPrint("DeleteDirectory exit: %d\n", ret);

    return ret;
}


static int __stdcall
PVFS_Dokan_move_file(
    LPCWSTR          FileName, // existing file name
    LPCWSTR          NewFileName,
    BOOL             ReplaceIfExisting,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *old_local_path, *old_fs_path,
         *new_local_path, *new_fs_path;
    int ret, err;

    DbgPrint("MoveFile: %S -> %S\n", FileName, NewFileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    old_local_path = convert_wstring(FileName);
    if (old_local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }
    new_local_path = convert_wstring(NewFileName);
    if (new_local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the paths */
    old_fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(old_fs_path);
    ret = fs_resolve_path(old_local_path, old_fs_path, MAX_PATH);
    if (ret != 0)
        goto move_exit;
    DEBUG_PATH(old_fs_path);
    
    new_fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(new_fs_path);
    ret = fs_resolve_path(new_local_path, new_fs_path, MAX_PATH);
    if (ret != 0)
        goto move_exit;
    DEBUG_PATH(new_fs_path);

    /* rename/move the file */
    ret = fs_rename(old_fs_path, new_fs_path);

    switch (ret)
    {
    case 0:
        err = 0;
        /*
        if (DokanFileInfo->Context) {
           // should close? or rename at closing?
           CloseHandle((HANDLE)DokanFileInfo->Context);
           DokanFileInfo->Context = 0;
        }
        */
        break;
    case -PVFS_ENOENT:
        err = -ERROR_FILE_NOT_FOUND;
        break;
    default:
        err = -1;
    }

move_exit:

    cleanup_string(old_local_path);
    cleanup_string(new_local_path);
    free(old_fs_path);
    free(new_fs_path);

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
    char *local_path, *fs_path;
    int ret, err;
    PVFS_sys_attr attr;

    DbgPrint("SetFileTime: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* convert from Unicode */
    local_path = convert_wstring(FileName);
    if (local_path == NULL)
    {
        return -ERROR_INVALID_DATA;
    }

    /* resolve the path */
    fs_path = (char *) malloc(MAX_PATH);
    MALLOC_CHECK(fs_path);
    ret = fs_resolve_path(local_path, fs_path, MAX_PATH);
    if (ret != 0)
    {
        free(local_path);
        free(fs_path);
        return -1;
    }

    DEBUG_PATH(fs_path);

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

    switch (ret)
    {
    case 0:
        err = 0;
        break;
    default:
        err = -1;
    }

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
    DWORD sd_len;
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
    if (!SetSecurityDescriptorOwner(desc, guest_sid, FALSE))
    {
        DbgPrint("   Could not set descriptor owner\n");
        goto get_file_security_exit;
    }

    /* set primary group to Everyone group */
    if (!SetSecurityDescriptorGroup(desc, everyone_sid, FALSE))
    {
        DbgPrint("   Could not set descriptor group\n");
        goto get_file_security_exit;
    }

    /* add the ACL to the security descriptor */
    if (!SetSecurityDescriptorDacl(desc, TRUE, acl, FALSE))
    {
        DbgPrint("   Could not set descriptor DACL\n");
        goto get_file_security_exit;
    }

    sd_len = GetSecurityDescriptorLength(desc);

    if (BufferLength >= sd_len)
    {
        ZeroMemory(SecurityDescriptor, BufferLength);
        CopyMemory(SecurityDescriptor, desc, sd_len);
    }
    else
    {
        err = -ERROR_INSUFFICIENT_BUFFER;
        *LengthNeeded = sd_len;
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
    PULONGLONG FreeBytesAvailable,
    PULONGLONG TotalNumberOfBytes,
    PULONGLONG TotalNumberOfFreeBytes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    int ret, err;

    DbgPrint("GetDiskFreeSpace\n");
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    ret = fs_get_diskfreespace((PVFS_size *) FreeBytesAvailable, 
                               (PVFS_size *) TotalNumberOfBytes);

    switch (ret)
    {
    case 0: 
        *TotalNumberOfFreeBytes = *FreeBytesAvailable;
        err = 0;
        break;
    default: 
        err = -1;
    }

    /* TODO */
    DbgPrint("GetDiskFreeSpace exit: %d (%d)\n", err, ret);

    return err;
}


int __cdecl dokan_loop()
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

#ifdef _DEBUG
    /* removed in 0.6.0
    DokanSetDebugMode(TRUE); */
#endif

    DbgInit();

    if (g_DebugMode)
        dokanOptions->Options |= DOKAN_OPTION_DEBUG;
    if (g_UseStdErr)
        dokanOptions->Options |= DOKAN_OPTION_STDERR;

    dokanOptions->Options |= DOKAN_OPTION_KEEP_ALIVE;

    dokanOptions->Version = 600;
    /* Hard coded for now */
    dokanOptions->MountPoint = L"Z:";

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
    dokanOperations->GetVolumeInformation = NULL;
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
