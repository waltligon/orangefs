/* Performs GetFileInfo call on file specified on command-line and displays 
   the info. */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    const char* filename;
    HANDLE hFile;
    BY_HANDLE_FILE_INFORMATION fileInfo;

    if (argc != 2) {
        fprintf(stderr, "USAGE: %s file_path\n", argv[0]);
        return 1;
    }

    filename = argv[1];

    hFile = CreateFile(filename,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile error: %u\n", GetLastError());
        return 1;
    }

    ZeroMemory(&fileInfo, sizeof(fileInfo));
    if (GetFileInformationByHandle(hFile, &fileInfo)) {
        printf("dwFileAttributes: %lx\n", fileInfo.dwFileAttributes);
        printf("ftCreationTime.dwLowDateTime: %lx\n", fileInfo.ftCreationTime.dwLowDateTime);
        printf("ftCreationTime.dwHighDateTime: %lx\n", fileInfo.ftCreationTime.dwHighDateTime);
        printf("ftLastAccessTime.dwLowDateTime: %lx\n", fileInfo.ftLastAccessTime.dwLowDateTime);
        printf("ftLastAccessTime.dwHighDateTime: %lx\n", fileInfo.ftLastAccessTime.dwHighDateTime);
        printf("ftLastWriteTime.dwLowDateTime: %lx\n", fileInfo.ftLastWriteTime.dwLowDateTime);
        printf("ftCreationTime.dwHighDateTime: %lx\n", fileInfo.ftLastWriteTime.dwHighDateTime);
        printf("dwVolumeSerialNumber: %lx\n", fileInfo.dwVolumeSerialNumber);
        printf("nFileSizeHigh: %lx\n", fileInfo.nFileSizeHigh);
        printf("nFileSizeLow: %lx\n", fileInfo.nFileSizeLow);
        printf("nNumberOfLinks: %lu\n", fileInfo.nNumberOfLinks);
        printf("nFileIndexHigh: %lx\n", fileInfo.nFileIndexHigh);
        printf("nFileIndexLow: %lx\n", fileInfo.nFileIndexLow);
    }
    else {
        fprintf(stderr, "GetFileInformationByHandle error: %u\n", GetLastError());
        return 1;
    }

    return 0;
}