
#ifndef USRINT_H
#define USRINT_H 1

#define _LARGEFILE64_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <libgen.h>
#include <string.h>
#include <stdarg.h>
#include <memory.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/resource.h>
/* #include <sys/statvfs.h> /* struct statfs on OS X */
#include <sys/vfs.h> /* struct statfs on Linux */
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/dirent.h>
#define __USE_ATFILE 1
#include <fcntl.h>
#include <time.h>
#include <sys/uio.h>
#define __USE_GNU 1
#include <dlfcn.h>

//#include <mpi.h>
#include <pvfs2.h>
#include <pvfs2-hint.h>

#include <posix-pvfs.h>

#define _GNU_SOURCE

/* magic numbers for PVFS filesystem */
#define PVFS_FS 537068840
#define LINUX_FS 61267

#define PVFS_FD_SUCCESS 0
#define PVFS_FD_FAILURE -1

/* Defines GNU's O_NOFOLLOW flag to be false if its not set */ 
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define true   1 
#define false  0 
#define O_HINTS 02000000

/* POSIX functions */ 

/** struct of pointers to methods for posix system calls */
typedef struct posix_ops_s
{   
    int (*statfs)(const char *path, struct statfs *buf);
    int (*open)(const char *path, int flags, ...);
    int (*open64)(const char *path, int flags, ...);
    int (*openat)(int dirfd, const char *path, int flags, ...);
    int (*openat64)(int dirfd, const char *path, int flags, ...);
    int (*creat)(const char *path, mode_t mode, ...);
    int (*creat64)(const char *path, mode_t mode, ...);
    int (*unlink)(const char *path);
    int (*unlinkat)(int dirfd, const char *path);
    int (*rename)(const char *oldpath, const char *newpath);
    int (*renameat)(int olddirfd, const char *oldpath,
                    int newdirfd, const char *newpath);
    ssize_t (*read)( int fd, void *buf, size_t count);
    ssize_t (*pread)( int fd, void *buf, size_t count, off_t offset);
    ssize_t (*readv)(int fd, const struct iovec *vector, int count);
    ssize_t (*pread64)( int fd, void *buf, size_t count, off64_t offset);
    ssize_t (*write)( int fd, void *buf, size_t count);
    ssize_t (*pwrite)( int fd, void *buf, size_t count, off_t offset);
    ssize_t (*writev)( int fd, const struct iovec *vector, int count);
    ssize_t (*write64)( int fd, void *buf, size_t count, off64_t offset);
    off_t (*lseek)(int fd, off_t offset, int whence);
    off64_t (*lseek64)(int fd, off64_t offset, int whence);
    int (*truncate)(const char *path, off_t length);
    int (*truncate64)(const char *path, off64_t length);
    int (*ftruncate)(int fd, off_t length);
    int (*ftruncate64)(int fd, off64_t length);
    int (*close)( int fd);
    int (*flush)(int fd);
    int (*stat)(const char *path, struct stat *buf);
    int (*stat64)(const char *path, struct stat64 *buf);
    int (*fstat)(int fd, struct stat *buf);
    int (*fstat64)(int fd, struct stat64 *buf);
    int (*fstatat)(int fd, char *path, struct stat *buf, int flag);
    int (*fstatat64)(int fd, char *path, struct stat64 *buf, int flag);
    int (*lstat)(const char *path, struct stat *buf);
    int (*lstat64)(const char *path, struct stat64 *buf);
    int (*dup)(int oldfd);
    int (*dup2)(int oldfd, int newfd);
    int (*chown)(const char *path, uid_t owner, gid_t group);
    int (*fchown)(int fd, uid_t owner, gid_t group);
    int (*fchownat)(int fd, char *path, uid_t owner, gid_t group, int flag);
    int (*lchown)(const char *path, uid_t owner, gid_t group);
    int (*chmod)(const char *path, mode_t mode);
    int (*fchmod)(int fd, mode_t mode);
    int (*fchmodat)(int fd, char *path, mode_t mode, int flag);
    int (*lchmod)(const char *path, mode_t mode);
    int (*mkdir)(const char *path, mode_t mode);
    int (*mkdirat)(int dirfd, const char *path, mode_t mode);
    int (*rmdir)(const char *path);
    ssize_t (*readlink)(const char *path, char *buf, size_t bufsiz);
    int (*readlinkat)(int dirfd, const char *path, char *buf, size_t bufsiz);
    ssize_t (*symlink)(const char *oldpath, const char *newpath);
    int (*symlinkat)(const char *oldpath, int newdirfd, const char *newpath);
    ssize_t (*link)(const char *oldpath, const char *newpath);
    int (*linkat)(const char *oldpath, int newdirfd,
                  const char *newpath, int flags);
    int (*readdir)(unsigned int fd, struct dirent *dirp, unsigned int count);
    int (*getdents)(unsigned int fd, struct dirent *dirp, unsigned int count);
    int (*access)(const char * path, int mode);
    int (*faccessat)(int dirfd, const char * path, int mode, int flags);
    int (*flock)(int fd, int op);
    int (*fcntl)(int fd, int cmd, ...);
    void (*sync)(void);
    int (*fsync)(int fd);
    int (*fdatasync)(int fd);
    mode_t (*umask)(mode_t mask);
    mode_t (*getumask)(void);
    int (*getdtablesize)(void);
} posix_ops;

extern posix_ops glibc_ops;
extern posix_ops pvfs_ops;

/** PVFS Descriptor table entry */
typedef struct pvfs_descriptor_s
{
    int fd;                   /**< file number in PVFS descriptor_table */
    int dup_cnt;              /**< number of table slots with this des */
    posix_ops *fsops;         /**< syscalls to use for this file */
    int posix_fd;             /**< non-PVFS files, the true file number */
    PVFS_object_ref pvfs_ref; /**< PVFS fs_id and handle for PVFS file */
    int flags;                /**< the open flags used for this file */
    off64_t file_pointer;     /**< offset from the beginning of the file */
    char is_in_use;           /**< true if this descriptor is valid */
} pvfs_descriptor;

typedef struct pvfs_descriptor_s PFILE; /* these are for posix interface */
typedef struct pvfs_descriptor_s PDIR;

#include <openfile-util.h>
#include <iocommon.h>

/* These are the standard function we are overloading in this lib */
#if 0
int open(const char *path, int flags, ...);
int open64(const char *path, int flags, ...); 
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count); 
off_t lseek(int fd, off_t offset, int whence); 
off64_t lseek64(int fd, off64_t offset, int whence); 
ssize_t pread(int fd, void *buf, size_t nbytes, off_t offset); 
ssize_t pwrite(int fd, const void *buf, size_t nbytes, off_t offset); 
ssize_t readv(int fd, const struct iovec *iov, int iovcnt); 
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)  ; 
int unlink(const char *path);
int close(int fd);

FILE * fopen(const char * path, const char * mode) ;
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream); 
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream); 
int fprintf(FILE *stream, const char *format, ...); 
int fputs(const char *s, FILE *stream); 
int fputc(int c, FILE *stream); 
int fgetc(FILE *stream); 
char *fgets(char *s, int size, FILE *stream); 
void rewind(FILE *stream); 
int fflush(FILE *stream); 
int getc(FILE *stream); 
int ungetc(int c, FILE *stream); 
int fclose(FILE *stream); 

/* MPI functions */ 
int MPI_File_open(MPI_Comm comm, char *filename, int amode, MPI_Info info, MPI_File *mpi_fh); 
int MPI_File_write(MPI_File mpi_fh, void *buf, int count, MPI_Datatype datatype, MPI_Status *status); 
#endif


//typedef uint64_t off64_t;

#endif
