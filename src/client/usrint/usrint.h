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
#ifdef __OPTIMIZE__
#undef __OPTIMIZE__
#endif
#define __NO_INLINE__ 1
#endif

/*
 * this defines __USE_LARGEFILE, __USE_LARGEFILE64, and
 * __USE_FILE_OFFSET64 which control many of the other includes
 */
#ifdef HAVE_FEATURES_H
#include <features.h>
#endif

/*
 * force this stuff off if the source requests
 * the stuff controlling inlining and def'ing of
 * functions in stdio is really mixed up and varies from
 * one generation of the headers to another.
 * I hate to whack all inlining and related stuff
 * but it seems the only reliable way to turn it off.
 * USRINT code can get this or not with the var below
 * I question that header files should be doing
 * optimization in the first place.  WBL
 */
#ifdef USRINT_SOURCE
#ifdef __USE_FILE_OFFSET64
#undef __USE_FILE_OFFSET64
#endif
/* This seems to reappear on some systems, so whack it again */
#ifdef __OPTIMIZE__
#undef __OPTIMIZE__
#endif
#ifdef __REDIRECT
#undef __REDIRECT
#endif
#ifdef __USE_EXTERN_INLINES
#undef __USE_EXTERN_INLINES
#endif
#ifdef __USE_FORTIFY_LEVEL
#undef __FORTIFY_LEVEL
#define __USE_FORTIFY_LEVEL 0
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
#ifdef HAVE_SYS_SENDFILE_H
#include <sys/sendfile.h>
#endif
/* #include <sys/statvfs.h> */ /* struct statfs on OS X */
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h> /* struct statfs on Linux */
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <sys/statvfs.h>
#include <sys/uio.h>
#ifdef PVFS_HAVE_ACL_INCLUDES
#include <sys/acl.h>
#include <acl/libacl.h>
#endif
#include <sys/mman.h>
#include <sys/time.h>

#ifdef HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif

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
extern ssize_t getxattr(const char *path, const char *name,
                    void *value, size_t size);
extern ssize_t lgetxattr(const char *path, const char *name,
                     void *value, size_t size);
extern ssize_t fgetxattr(int fd, const char *name, void *value, size_t size);
extern ssize_t listxattr(const char *path, char *list, size_t size);
extern ssize_t llistxattr(const char *path, char *list, size_t size);
extern ssize_t flistxattr(int fd, char *list, size_t size);
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
#include <gen-locks.h>

/* magic numbers for PVFS filesystem */
#define PVFS_FS 537068840
#define LINUX_FS 61267

#define PVFS_FD_SUCCESS 0
#define PVFS_FD_FAILURE -1

/* Defines GNU's O_NOFOLLOW flag to be false if its not set */ 
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* Define AT_FDCWD and related flags on older systems */
#ifndef AT_FDCWD
# define AT_FDCWD		-100	/* Special value used to indicate
					   the *at functions should use the
					   current working directory. */
#endif
#ifndef AT_SYMLINK_NOFOLLOW
# define AT_SYMLINK_NOFOLLOW	0x100	/* Do not follow symbolic links.  */
#endif
#ifndef AT_REMOVDIR
# define AT_REMOVEDIR		0x200	/* Remove directory instead of
					   unlinking file.  */
#endif
#ifndef AT_SYMLINK_FOLLOW
# define AT_SYMLINK_FOLLOW	0x400	/* Follow symbolic links.  */
#endif
#ifndef AT_EACCESS
# define AT_EACCESS		0x200	/* Test access permitted for
					   effective IDs, not real IDs.  */
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
#ifdef  PVFS_USRINT_DEBUG
#define debug(s,v) fprintf(stderr,s,v)
#else
#define debug(s,v)
#endif

/* USRINT Configuration Defines - Defaults */
/* These should be defined in pvfs2-config.h */

#ifndef PVFS_USRINT_BUILD
#define PVFS_USRINT_BUILD 1
#endif

#ifndef PVFS_USRINT_CWD
#define PVFS_USRINT_CWD 1
#endif

#ifndef PVFS_USRINT_KMOUNT
#define PVFS_USRINT_KMOUNT 0
#endif

#ifndef PVFS_UCACHE_ENABLE
#define PVFS_UCACHE_ENABLE 1
#endif


/* FD sets */
#if 0
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
#endif

#endif /* USRINT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

