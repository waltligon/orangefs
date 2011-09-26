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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _ATFILE_SOURCE
#define _ATFILE_SOURCE 1
#endif
#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE 1
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE 1
#endif

/*
 * This seems to control redirect of 32-bit IO to 64-bit IO
 * We want to avoid this in our source
 */
#ifdef USRINT_SOURCE
#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS 
#endif
#else
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

/*
 * this defines __USE_LARGEFILE, __USE_LARGEFILE64, and
 * __USE_FILE_OFFSET64 which control many of the other includes
 */
#include <features.h>
/*
 * force this stuff off if the source requests
 */
#ifdef USRINT_SOURCE
#ifdef __USE_FILE_OFFSET64
#undef __USE_FILE_OFFSET64
#endif
#ifdef __OPTIMIZE__
#undef __OPTIMIZE__
#endif
#endif

#include <pvfs2-config.h>
#include <gossip.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <utime.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <assert.h>
#include <libgen.h>
#include <dirent.h>
#include <string.h>
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#include <memory.h>
#include <limits.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <sys/select.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <sys/resource.h>
#include <sys/sendfile.h>
/* #include <sys/statvfs.h> */ /* struct statfs on OS X */
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h> /* struct statfs on Linux */
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/acl.h>
#include <acl/libacl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <linux/types.h>
#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#else
#ifdef HAVE_SYS_ATTR_H
#include <sys/xattr.h>
#else
#define XATTR_CREATE 0x1
#define XATTR_REPLACE 0x2
extern int setxattr(const char *path, const char *name,
                    const void *value, size_t size, int flags);
extern int lsetxattr(const char *path, const char *name,
                     const void *value, size_t size, int flags);
extern int fsetxattr(int fd, const char *name,
                     const void *value, size_t size, int flags);
extern int getxattr(const char *path, const char *name,
                    void *value, size_t size);
extern int lgetxattr(const char *path, const char *name,
                     void *value, size_t size);
extern int fgetxattr(int fd, const char *name, void *value, size_t size);
extern int listxattr(const char *path, char *list, size_t size);
extern int llistxattr(const char *path, char *list, size_t size);
extern int flistxattr(int fd, char *list, size_t size);
extern int removexattr(const char *path, const char *name);
extern int lremovexattr(const char *path, const char *name);
extern int fremovexattr(int fd, const char *name);
#endif
#endif

/* #include <linux/dirent.h> diff source need diff versions */
#include <time.h>
#include <dlfcn.h>

//#include <mpi.h>

/* PVFS specific includes */
#include <pvfs2.h>
#include <pvfs2-hint.h>
#include <pvfs2-debug.h>
#include <pvfs2-types.h>
#include <pvfs2-req-proto.h>

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
#define O_HINTS     02000000  /* PVFS hints are present */
#define O_NOTPVFS   04000000  /* Open non-PVFS files if possible */

/* constants for this library */
/* size of stdio default buffer - starting at 1Meg */
#define PVFS_BUFSIZE (1024*1024)

/* extra function prototypes */

extern int posix_readdir(unsigned int fd, struct dirent *dirp,
                         unsigned int count);

extern int fseek64(FILE *stream, const off64_t offset, int whence);

extern off64_t ftell64(FILE *stream);

extern int pvfs_convert_iovec(const struct iovec *vector,
                              int count,
                              PVFS_Request *req,
                              void **buf);

/* MPI functions */ 
//int MPI_File_open(MPI_Comm comm, char *filename,
//                    int amode, MPI_Info info, MPI_File *mpi_fh); 
//int MPI_File_write(MPI_File mpi_fh, void *buf,
//                    int count, MPI_Datatype datatype, MPI_Status *status); 

/* Macros */

/* debugging */

//#define USRINT_DEBUG
#ifdef  USRINT_DEBUG
#define debug(s,v) fprintf(stderr,s,v)
#else
#define debug(s,v)
#endif

/* FD sets */

#ifdef FD_SET
#undef FD_SET
#endif
#define FD_SET(d,fdset)                 \
do {                                    \
    pvfs_descriptor *pd;                \
    pd = pvfs_find_descriptor(d);       \
    if (pd)                             \
    {                                   \
        __FD_SET(pd->true_fd,(fdset));  \
    }                                   \
} while(0)

#ifdef FD_CLR
#undef FD_CLR
#endif
#define FD_CLR(d,fdset)                 \
do {                                    \
    pvfs_descriptor *pd;                \
    pd = pvfs_find_descriptor(d);       \
    if (pd)                             \
    {                                   \
        __FD_CLR(pd->true_fd,(fdset));  \
    }                                   \
} while(0)

#ifdef FD_ISSET
#undef FD_ISSET
#endif
#define FD_ISSET(d,fdset)               \
do {                                    \
    pvfs_descriptor *pd;                \
    pd = pvfs_find_descriptor(d);       \
    if (pd)                             \
    {                                   \
        __FD_ISSET(pd->true_fd,(fdset));\
    }                                   \
} while(0)


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

