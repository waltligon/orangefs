/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines
 */
#ifndef USRINT_H
#define USRINT_H 1

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#define __USE_MISC 1
#define __USE_ATFILE 1
#define __USE_GNU 1

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <libgen.h>
#include <string.h>
#include <stdarg.h>
#include <memory.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/resource.h>
/* #include <sys/statvfs.h> /* struct statfs on OS X */
#include <sys/vfs.h> /* struct statfs on Linux */
#include <sys/stat.h>
#include <sys/uio.h>
#include <linux/types.h>
/* #include <linux/dirent.h> diff source need diff versions */
#include <time.h>
#include <dlfcn.h>

//#include <mpi.h>

/* PVFS specific includes */
#include <pvfs2.h>
#include <pvfs2-hint.h>


/* magic numbers for PVFS filesystem */
#define PVFS_FS 537068840
#define LINUX_FS 61267

#define PVFS_FD_SUCCESS 0
#define PVFS_FD_FAILURE -1

/* Defines GNU's O_NOFOLLOW flag to be false if its not set */ 
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#define true   1 
#define false  0 
#define O_HINTS 02000000

/* constants for this library */
/* size of stdio default buffer - starting at 1Meg */
#define PVFS_BUFSIZE (1024*1024)

/* extra function prototypes */
int fseek64(FILE *stream, const off64_t offset, int whence);

off64_t ftell64(FILE *stream);

int pvfs_convert_iovec(const struct iovec *vector,
                       int count,
                       PVFS_Request *req,
                       void **buf);

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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

