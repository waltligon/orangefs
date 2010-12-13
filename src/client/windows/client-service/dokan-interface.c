/* TODO: Copyright (C) Omnibond, LLC 2010 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "dokan.h"
/* TODO: needed? #include <fileinfo.h> */

#include "pvfs2.h"
#include "fs.h"

BOOL g_UseStdErr;
BOOL g_DebugMode;

#define PVFS2_DOKAN_CHECKFLAG(val, flag) if (val&flag) { DbgPrint("\t" #flag "\n"); }

/* TODO */
#define MALLOC_CHECK(ptr)

static void DbgPrint(LPCSTR format, ...)
{
    if (g_DebugMode) 
    {
        char buffer[512];
    
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
    }
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
static char *convert_string(const wchar_t *wcstr)
{
    errno_t err;
    size_t ret, mb_size;
    char *mbstr;
    
    /* get size of buffer */
    err = wcstombs_s(&ret, NULL, 0, wcstr, 0);

    if (err != 0)
    {
        DbgPrint("convert_string: %d\n", err);
        return NULL;
    }

    /* allocate buffer */
    mb_size = ret;
    mbstr = (char *) malloc(mb_size);
    if (mbstr == NULL)
    {
        DbgPrint("convert_string: no memory\n");
    }

    /* convert string */
    err = wcstombs_s(&ret, mbstr, mb_size, wcstr, wcslen(wcstr));

    if (err != 0)
    {
        DbgPrint("convert_string 2: %d\n", err);
        free(mbstr);

        return NULL;
    }

    return mbstr;

}

#define cleanup_string(str)    free(str)

static int
PVFS2_Dokan_create_file(
    LPCWSTR          FileName,
    DWORD            AccessMode,
    DWORD            ShareMode,
    DWORD            CreationDisposition,
    DWORD            FlagsAndAttributes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    DWORD fileAttr;
    int ret, found, err;
    PVFS_handle handle;
    PVFS_sys_attr attr;


    DbgPrint("CreateFile : %S\n", FileName);
    
    if (CreationDisposition == CREATE_NEW)
        DbgPrint("\tCREATE_NEW\n");
    if (CreationDisposition == OPEN_ALWAYS)
        DbgPrint("\tOPEN_ALWAYS\n");
    if (CreationDisposition == CREATE_ALWAYS)
        DbgPrint("\tCREATE_ALWAYS\n");
    if (CreationDisposition == OPEN_EXISTING)
        DbgPrint("\tOPEN_EXISTING\n");
    if (CreationDisposition == TRUNCATE_EXISTING)
        DbgPrint("\tTRUNCATE_EXISTING\n");

    /*
    if (ShareMode == 0 && AccessMode & FILE_WRITE_DATA)
        ShareMode = FILE_SHARE_WRITE;
    else if (ShareMode == 0)
        ShareMode = FILE_SHARE_READ;
    */

    DbgPrint("\tShareMode = 0x%x\n", ShareMode);

    PVFS2_DOKAN_CHECKFLAG(ShareMode, FILE_SHARE_READ);
    PVFS2_DOKAN_CHECKFLAG(ShareMode, FILE_SHARE_WRITE);
    PVFS2_DOKAN_CHECKFLAG(ShareMode, FILE_SHARE_DELETE);

    DbgPrint("\tAccessMode = 0x%x\n", AccessMode);

    PVFS2_DOKAN_CHECKFLAG(AccessMode, GENERIC_READ);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, GENERIC_WRITE);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, GENERIC_EXECUTE);
    
    PVFS2_DOKAN_CHECKFLAG(AccessMode, DELETE);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_READ_DATA);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_READ_ATTRIBUTES);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_READ_EA);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, READ_CONTROL);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_WRITE_DATA);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_WRITE_ATTRIBUTES);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_WRITE_EA);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_APPEND_DATA);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, WRITE_DAC);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, WRITE_OWNER);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, SYNCHRONIZE);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, FILE_EXECUTE);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, STANDARD_RIGHTS_READ);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, STANDARD_RIGHTS_WRITE);
    PVFS2_DOKAN_CHECKFLAG(AccessMode, STANDARD_RIGHTS_EXECUTE);

    // When filePath is a directory, needs to change the flag so that the file can be opened.
    /*
    fileAttr = GetFileAttributes(filePath);
    if (fileAttr && fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
        FlagsAndAttributes |= FILE_FLAG_BACKUP_SEMANTICS;
        //AccessMode = 0;
    }
    */
    DbgPrint("\tFlagsAndAttributes = 0x%x\n", FlagsAndAttributes);

    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_ARCHIVE);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_ENCRYPTED);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_HIDDEN);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_NORMAL);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_OFFLINE);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_READONLY);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_SYSTEM);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_ATTRIBUTE_TEMPORARY);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_WRITE_THROUGH);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_OVERLAPPED);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_NO_BUFFERING);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_RANDOM_ACCESS);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_SEQUENTIAL_SCAN);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_DELETE_ON_CLOSE);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_BACKUP_SEMANTICS);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_POSIX_SEMANTICS);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_OPEN_REPARSE_POINT);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, FILE_FLAG_OPEN_NO_RECALL);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_ANONYMOUS);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_IDENTIFICATION);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_IMPERSONATION);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_DELEGATION);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_CONTEXT_TRACKING);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_EFFECTIVE_ONLY);
    PVFS2_DOKAN_CHECKFLAG(FlagsAndAttributes, SECURITY_SQOS_PRESENT);

    DbgPrint("\n");

    local_path = convert_string(FileName);
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

    /* look up the file */
    found = 0;
    ret = fs_lookup(fs_path, &handle);    
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

    DbgPrint("Return code: %d\n", ret);

    switch (ret)
    {
    case 0:
        err = 0;
        /* save the file handle in context */
        DokanFileInfo->Context = handle;

        DbgPrint("Context: %llx\n", DokanFileInfo->Context);

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

    free(local_path);
    free(fs_path);
        
    return err;
}


static int
PVFS2_Dokan_create_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    int ret, err;
    PVFS_handle handle;

    DbgPrint("CreateDirectory : %S\n", FileName);

    local_path = convert_string(FileName);
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

    ret = fs_mkdir(fs_path, &handle);

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

    return err;
}


static int
PVFS2_Dokan_open_directory(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    char *local_path, *fs_path;
    int ret, err;
    PVFS_handle handle;
    PVFS_sys_attr attr;

    DbgPrint("OpenDirectory : %S\n", FileName);

    /* convert from Unicode */
    local_path = convert_string(FileName);
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

    /* lookup the file */
    ret = fs_lookup(fs_path, &handle);    

    if (ret == 0)
    {
        ret = fs_getattr(fs_path, &attr);
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
    
    return err;
}


static int
PVFS2_Dokan_close_file(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    DbgPrint("CloseFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", DokanFileInfo->Context);

    /* PVFS doesn't have a close-file semantic.
       Simply clear the handle and return success */
    DokanFileInfo->Context = 0;

    return 0;
}


static int
PVFS2_Dokan_cleanup(
    LPCWSTR          FileName,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    DbgPrint("Cleanup: %S\n", FileName);
    DbgPrint("  Context: %llx\n", DokanFileInfo->Context);

    if (DokanFileInfo->Context) {
        /**** TODO
        if (DokanFileInfo->DeleteOnClose) {
            DbgPrint("\tDeleteOnClose\n");
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

    return 0;
}


static int
PVFS2_Dokan_read_file(
    LPCWSTR          FileName,
    LPVOID           Buffer,
    DWORD            BufferLength,
    LPDWORD          ReadLength,
    LONGLONG         Offset,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    PVFS_handle handle = DokanFileInfo->Context;

    DbgPrint("ReadFile: %S\n", FileName);
    DbgPrint("   Context: %llx\n", handle);

    



    return 0;
}


static int
PVFS2_Dokan_write_file(
    LPCWSTR        FileName,
    LPCVOID        Buffer,
    DWORD        NumberOfBytesToWrite,
    LPDWORD        NumberOfBytesWritten,
    LONGLONG            Offset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle = (HANDLE)DokanFileInfo->Context;
    ULONG    offset = (ULONG)Offset;
    BOOL    opened = FALSE;

    GetFilePath(filePath, FileName);

    DbgPrint("WriteFile : %s, offset %I64d, length %d\n", filePath, Offset, NumberOfBytesToWrite);

    // reopen the file
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle, cleanuped?\n");
        handle = CreateFile(
            filePath,
            GENERIC_WRITE,
            FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DbgPrint("\tCreateFile error : %d\n\n", GetLastError());
            return -1;
        }
        opened = TRUE;
    }

    if (DokanFileInfo->WriteToEndOfFile) {
        if (SetFilePointer(handle, 0, NULL, FILE_END) == INVALID_SET_FILE_POINTER) {
            DbgPrint("\tseek error, offset = EOF, error = %d\n", GetLastError());
            return -1;
        }
    } else if (SetFilePointer(handle, offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        DbgPrint("\tseek error, offset = %d, error = %d\n", offset, GetLastError());
        return -1;
    }

        
    if (!WriteFile(handle, Buffer, NumberOfBytesToWrite, NumberOfBytesWritten, NULL)) {
        DbgPrint("\twrite error = %u, buffer length = %d, write length = %d\n",
            GetLastError(), NumberOfBytesToWrite, *NumberOfBytesWritten);
        return -1;

    } else {
        DbgPrint("\twrite %d, offset %d\n\n", *NumberOfBytesWritten, offset);
    }

    // close the file when it is reopened
    if (opened)
        CloseHandle(handle);

    return 0;
}


static int
PVFS2_Dokan_flush_file_buffers(
    LPCWSTR        FileName,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle = (HANDLE)DokanFileInfo->Context;

    GetFilePath(filePath, FileName);

    DbgPrint("FlushFileBuffers : %s\n", filePath);

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");
        return 0;
    }

    if (FlushFileBuffers(handle)) {
        return 0;
    } else {
        DbgPrint("\tflush error code = %d\n", GetLastError());
        return -1;
    }

}


static int
PVFS2_Dokan_get_file_information(
    LPCWSTR                            FileName,
    LPBY_HANDLE_FILE_INFORMATION    HandleFileInformation,
    PDOKAN_FILE_INFO                DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle = (HANDLE)DokanFileInfo->Context;
    BOOL    opened = FALSE;

    GetFilePath(filePath, FileName);

    DbgPrint("GetFileInfo : %s\n", filePath);

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");

        // If CreateDirectory returned FILE_ALREADY_EXISTS and 
        // it is called with FILE_OPEN_IF, that handle must be opened.
        handle = CreateFile(filePath, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return -1;
        opened = TRUE;
    }

    if (!GetFileInformationByHandle(handle,HandleFileInformation)) {
        DbgPrint("\terror code = %d\n", GetLastError());

        // FileName is a root directory
        // in this case, FindFirstFile can't get directory information
        if (wcslen(FileName) == 1) {
            DbgPrint("  root dir\n");
            HandleFileInformation->dwFileAttributes = GetFileAttributes(filePath);

        } else {
            WIN32_FIND_DATAW find;
            ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
            handle = FindFirstFile(filePath, &find);
            if (handle == INVALID_HANDLE_VALUE) {
                DbgPrint("\tFindFirstFile error code = %d\n\n", GetLastError());
                return -1;
            }
            HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
            HandleFileInformation->ftCreationTime = find.ftCreationTime;
            HandleFileInformation->ftLastAccessTime = find.ftLastAccessTime;
            HandleFileInformation->ftLastWriteTime = find.ftLastWriteTime;
            HandleFileInformation->nFileSizeHigh = find.nFileSizeHigh;
            HandleFileInformation->nFileSizeLow = find.nFileSizeLow;
            DbgPrint("\tFindFiles OK, file size = %d\n", find.nFileSizeLow);
            FindClose(handle);
        }
    } else {
        DbgPrint("\tGetFileInformationByHandle success, file size = %d\n",
            HandleFileInformation->nFileSizeLow);
    }

    DbgPrint("\n");

    if (opened) {
        CloseHandle(handle);
    }

    return 0;
}


static int
PVFS2_Dokan_find_files(
    LPCWSTR                FileName,
    PFillFindData        FillFindData, // function pointer
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char                filePath[MAX_PATH];
    HANDLE                hFind;
    WIN32_FIND_DATAW    findData;
    DWORD                error;
    char                yenStar = "\\*";
    int count = 0;

    GetFilePath(filePath, FileName);

    wcscat(filePath, yenStar);
    DbgPrint("FindFiles :%s\n", filePath);

    hFind = FindFirstFile(filePath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid file handle. Error is %u\n\n", GetLastError());
        return -1;
    }

    FillFindData(&findData, DokanFileInfo);
    count++;

    while (FindNextFile(hFind, &findData) != 0) {
         FillFindData(&findData, DokanFileInfo);
        count++;
    }
    
    error = GetLastError();
    FindClose(hFind);

    if (error != ERROR_NO_MORE_FILES) {
        DbgPrint("\tFindNextFile error. Error is %u\n\n", error);
        return -1;
    }

    DbgPrint("\tFindFiles return %d entries in %s\n\n", count, filePath);

    return 0;
}


static int
PVFS2_Dokan_delete_file(
    LPCWSTR                FileName,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle = (HANDLE)DokanFileInfo->Context;

    GetFilePath(filePath, FileName);

    DbgPrint("DeleteFile %s\n", filePath);

    return 0;
}


static int
PVFS2_Dokan_delete_directory(
    LPCWSTR                FileName,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle = (HANDLE)DokanFileInfo->Context;
    HANDLE    hFind;
    WIN32_FIND_DATAW    findData;
    ULONG    fileLen;

    ZeroMemory(filePath, sizeof(filePath));
    GetFilePath(filePath, FileName);

    DbgPrint("DeleteDirectory %s\n", filePath);

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
}


static int
PVFS2_Dokan_move_file(
    LPCWSTR                FileName, // existing file name
    LPCWSTR                NewFileName,
    BOOL                ReplaceIfExisting,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char            filePath[MAX_PATH];
    char            newFilePath[MAX_PATH];
    BOOL            status;

    GetFilePath(filePath, FileName);
    GetFilePath(newFilePath, NewFileName);

    DbgPrint("MoveFile %s -> %s\n\n", filePath, newFilePath);

    if (DokanFileInfo->Context) {
        // should close? or rename at closing?
        CloseHandle((HANDLE)DokanFileInfo->Context);
        DokanFileInfo->Context = 0;
    }

    if (ReplaceIfExisting)
        status = MoveFileEx(filePath, newFilePath, MOVEFILE_REPLACE_EXISTING);
    else
        status = MoveFile(filePath, newFilePath);

    if (status == FALSE) {
        DWORD error = GetLastError();
        DbgPrint("\tMoveFile failed status = %d, code = %d\n", status, error);
        return -(int)error;
    } else {
        return 0;
    }
}

static int
PVFS2_Dokan_lock_file(
    LPCWSTR                FileName,
    LONGLONG            ByteOffset,
    LONGLONG            Length,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle;
    LARGE_INTEGER offset;
    LARGE_INTEGER length;

    GetFilePath(filePath, FileName);

    DbgPrint("LockFile %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");
        return -1;
    }

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (LockFile(handle, offset.HighPart, offset.LowPart, length.HighPart, length.LowPart)) {
        DbgPrint("\tsuccess\n\n");
        return 0;
    } else {
        DbgPrint("\tfail\n\n");
        return -1;
    }
}


static int
PVFS2_Dokan_set_end_of_file(
    LPCWSTR                FileName,
    LONGLONG            ByteOffset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char            filePath[MAX_PATH];
    HANDLE            handle;
    LARGE_INTEGER    offset;

    GetFilePath(filePath, FileName);

    DbgPrint("SetEndOfFile %s, %I64d\n", filePath, ByteOffset);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");
        return -1;
    }

    offset.QuadPart = ByteOffset;
    if (!SetFilePointerEx(handle, offset, NULL, FILE_BEGIN)) {
        DbgPrint("\tSetFilePointer error: %d, offset = %I64d\n\n",
                GetLastError(), ByteOffset);
        return GetLastError() * -1;
    }

    if (!SetEndOfFile(handle)) {
        DWORD error = GetLastError();
        DbgPrint("\terror code = %d\n\n", error);
        return error * -1;
    }

    return 0;
}


static int
PVFS2_Dokan_set_allocation_size(
    LPCWSTR                FileName,
    LONGLONG            AllocSize,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char            filePath[MAX_PATH];
    HANDLE            handle;
    LARGE_INTEGER    fileSize;

    GetFilePath(filePath, FileName);

    DbgPrint("SetAllocationSize %s, %I64d\n", filePath, AllocSize);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");
        return -1;
    }

    if (GetFileSizeEx(handle, &fileSize)) {
        if (AllocSize < fileSize.QuadPart) {
            fileSize.QuadPart = AllocSize;
            if (!SetFilePointerEx(handle, fileSize, NULL, FILE_BEGIN)) {
                DbgPrint("\tSetAllocationSize: SetFilePointer eror: %d, "
                    "offset = %I64d\n\n", GetLastError(), AllocSize);
                return GetLastError() * -1;
            }
            if (!SetEndOfFile(handle)) {
                DWORD error = GetLastError();
                DbgPrint("\terror code = %d\n\n", error);
                return error * -1;
            }
        }
    } else {
        DWORD error = GetLastError();
        DbgPrint("\terror code = %d\n\n", error);
        return error * -1;
    }
    return 0;
}


static int
PVFS2_Dokan_set_file_attributes(
    LPCWSTR                FileName,
    DWORD                FileAttributes,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    
    GetFilePath(filePath, FileName);

    DbgPrint("SetFileAttributes %s\n", filePath);

    if (!SetFileAttributes(filePath, FileAttributes)) {
        DWORD error = GetLastError();
        DbgPrint("\terror code = %d\n\n", error);
        return error * -1;
    }

    DbgPrint("\n");
    return 0;
}


static int
PVFS2_Dokan_set_file_time(
    LPCWSTR                FileName,
    CONST FILETIME*        CreationTime,
    CONST FILETIME*        LastAccessTime,
    CONST FILETIME*        LastWriteTime,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle;

    GetFilePath(filePath, FileName);

    DbgPrint("SetFileTime %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");
        return -1;
    }

    if (!SetFileTime(handle, CreationTime, LastAccessTime, LastWriteTime)) {
        DWORD error = GetLastError();
        DbgPrint("\terror code = %d\n\n", error);
        return error * -1;
    }

    DbgPrint("\n");
    return 0;
}

static int
PVFS2_Dokan_unlock_file(
    LPCWSTR                FileName,
    LONGLONG            ByteOffset,
    LONGLONG            Length,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char    filePath[MAX_PATH];
    HANDLE    handle;
    LARGE_INTEGER    length;
    LARGE_INTEGER    offset;

    GetFilePath(filePath, FileName);

    DbgPrint("UnlockFile %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        DbgPrint("\tinvalid handle\n\n");
        return -1;
    }

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (UnlockFile(handle, offset.HighPart, offset.LowPart, length.HighPart, length.LowPart)) {
        DbgPrint("\tsuccess\n\n");
        return 0;
    } else {
        DbgPrint("\tfail\n\n");
        return -1;
    }
}


static int
PVFS2_Dokan_unmount(
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    DbgPrint("Unmount\n");
    return 0;
}

int __cdecl dokan_loop()
{

    int status;
    PDOKAN_OPERATIONS dokanOperations =
            (PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS));
    PDOKAN_OPTIONS dokanOptions =
            (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));

#ifdef _DEBUG
    g_DebugMode = g_UseStdErr = TRUE;
#else
    g_DebugMode = g_UseStdErr = FALSE;
#endif

    ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
    dokanOptions->ThreadCount = 0; /* use default */

    if (g_DebugMode)
        dokanOptions->Options |= DOKAN_OPTION_DEBUG;
    if (g_UseStdErr)
        dokanOptions->Options |= DOKAN_OPTION_STDERR;

    dokanOptions->Options |= DOKAN_OPTION_KEEP_ALIVE;

    /* Hard coded M: for now */
    dokanOptions->DriveLetter = L'M';

    /* assign file operations */
    ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));
    dokanOperations->CreateFile = PVFS2_Dokan_create_file;
    dokanOperations->OpenDirectory = PVFS2_Dokan_open_directory;
    dokanOperations->CreateDirectory = PVFS2_Dokan_create_directory;
    dokanOperations->Cleanup = PVFS2_Dokan_cleanup;
    dokanOperations->CloseFile = PVFS2_Dokan_close_file;
    dokanOperations->ReadFile = PVFS2_Dokan_read_file;
    dokanOperations->WriteFile = PVFS2_Dokan_write_file;
    dokanOperations->FlushFileBuffers = PVFS2_Dokan_flush_file_buffers;
    dokanOperations->GetFileInformation = PVFS2_Dokan_get_file_information;
    dokanOperations->FindFiles = PVFS2_Dokan_find_files;
    dokanOperations->FindFilesWithPattern = NULL;
    dokanOperations->SetFileAttributes = PVFS2_Dokan_set_file_attributes;
    dokanOperations->SetFileTime = PVFS2_Dokan_set_file_time;
    dokanOperations->DeleteFile = PVFS2_Dokan_delete_file;
    dokanOperations->DeleteDirectory = PVFS2_Dokan_delete_directory;
    dokanOperations->MoveFile = PVFS2_Dokan_move_file;
    dokanOperations->SetEndOfFile = PVFS2_Dokan_set_end_of_file;
    dokanOperations->SetAllocationSize = PVFS2_Dokan_set_allocation_size;
    dokanOperations->LockFile = PVFS2_Dokan_lock_file;
    dokanOperations->UnlockFile = PVFS2_Dokan_unlock_file;
    dokanOperations->GetDiskFreeSpace = NULL;
    dokanOperations->GetVolumeInformation = NULL;
    dokanOperations->Unmount = PVFS2_Dokan_unmount;

    /* TODO: dokan loops until termination */
    status = DokanMain(dokanOptions, dokanOperations);

    switch (status) {
        case DOKAN_SUCCESS:
            fprintf(stderr, "Success\n");
            break;
        case DOKAN_ERROR:
            fprintf(stderr, "Error\n");
            break;
        case DOKAN_DRIVE_LETTER_ERROR:
            fprintf(stderr, "Bad Drive letter\n");
            break;
        case DOKAN_DRIVER_INSTALL_ERROR:
            fprintf(stderr, "Can't install driver\n");
            break;
        case DOKAN_START_ERROR:
            fprintf(stderr, "Driver something wrong\n");
            break;
        case DOKAN_MOUNT_ERROR:
            fprintf(stderr, "Can't assign a drive letter\n");
            break;
        default:
            fprintf(stderr, "Unknown error: %d\n", status);
            break;
    }

    free(dokanOptions);
    free(dokanOperations);

    return 0;

}
