/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - file descriptors for pvfs
 */

#ifndef POSIX_OPS_H
#define POSIX_OPS_H 1

/* POSIX functions */ 

/** struct of pointers to methods for posix system calls */
typedef struct posix_ops_s
{   
    /* this is not posix but is useful for debugging */
    int (*snprintf)(char *str, int size, const char *format, ...);
    /* begin posix system calls */
    int (*open)(const char *path, int flags, ...);
    int (*open64)(const char *path, int flags, ...);
    int (*openat)(int dirfd, const char *path, int flags, ...);
    int (*openat64)(int dirfd, const char *path, int flags, ...);
#if 0
    int (*creat)(const char *path, mode_t mode, ...);
    int (*creat64)(const char *path, mode_t mode, ...);
#endif
    int (*creat)(const char *path, mode_t mode);
    int (*creat64)(const char *path, mode_t mode);
    int (*unlink)(const char *path);
    int (*unlinkat)(int dirfd, const char *path, int flags);
    int (*rename)(const char *oldpath, const char *newpath);
    int (*renameat)(int olddirfd, const char *oldpath,
                    int newdirfd, const char *newpath);
    ssize_t (*read)( int fd, void *buf, size_t count);
    ssize_t (*pread)( int fd, void *buf, size_t count, off_t offset);
    ssize_t (*readv)(int fd, const struct iovec *vector, int count);
    ssize_t (*pread64)( int fd, void *buf, size_t count, off64_t offset);
    ssize_t (*write)( int fd, const void *buf, size_t count);
    ssize_t (*pwrite)( int fd, const void *buf, size_t count, off_t offset);
    ssize_t (*writev)( int fd, const struct iovec *vector, int count);
    ssize_t (*pwrite64)( int fd, const void *buf, size_t count, off64_t offset);
    off_t (*lseek)(int fd, off_t offset, int whence);
    off64_t (*lseek64)(int fd, off64_t offset, int whence);
    void (*perror)(const char *s);
    int (*truncate)(const char *path, off_t length);
    int (*truncate64)(const char *path, off64_t length);
    int (*ftruncate)(int fd, off_t length);
    int (*ftruncate64)(int fd, off64_t length);
    int (*fallocate)(int fd, off_t offset, off_t length);
    int (*close)( int fd);
    int (*stat)(const char *path, struct stat *buf);
    int (*stat64)(const char *path, struct stat64 *buf);
    int (*fstat)(int fd, struct stat *buf);
    int (*fstat64)(int fd, struct stat64 *buf);
    int (*fstatat)(int fd, const char *path, struct stat *buf, int flag);
    int (*fstatat64)(int fd, const char *path, struct stat64 *buf, int flag);
    int (*lstat)(const char *path, struct stat *buf);
    int (*lstat64)(const char *path, struct stat64 *buf);
    int (*futimesat)(int dirfd, const char *path, const struct timeval times[2]);
    int (*utimes)(const char *path, const struct timeval times[2]);
    int (*utime)(const char *path, const struct utimbuf *buf);
    int (*futimes)(int fd, const struct timeval times[2]);
    int (*dup)(int oldfd);
    int (*dup2)(int oldfd, int newfd);
    int (*dup3)(int oldfd, int newfd, int flags);
    int (*chown)(const char *path, uid_t owner, gid_t group);
    int (*fchown)(int fd, uid_t owner, gid_t group);
    int (*fchownat)(int fd, const char *path, uid_t owner, gid_t group, int flag);
    int (*lchown)(const char *path, uid_t owner, gid_t group);
    int (*chmod)(const char *path, mode_t mode);
    int (*fchmod)(int fd, mode_t mode);
    int (*fchmodat)(int fd, const char *path, mode_t mode, int flag);
    int (*mkdir)(const char *path, mode_t mode);
    int (*mkdirat)(int dirfd, const char *path, mode_t mode);
    int (*rmdir)(const char *path);
    ssize_t (*readlink)(const char *path, char *buf, size_t bufsiz);
    ssize_t (*readlinkat)(int dirfd, const char *path, char *buf, size_t bufsiz);
    int (*symlink)(const char *oldpath, const char *newpath);
    int (*symlinkat)(const char *oldpath, int newdirfd, const char *newpath);
    int (*link)(const char *oldpath, const char *newpath);
    int (*linkat)(int olddirfd, const char *oldpath,
                  int newdirfd, const char *newpath, int flags);
    int (*readdir)(u_int fd, struct dirent *dirp, u_int count);
    int (*getdents)(u_int fd, struct dirent *dirp, u_int count);
    int (*getdents64)(u_int fd, struct dirent64 *dirp, u_int count);
    int (*access)(const char *path, int mode);
    int (*faccessat)(int dirfd, const char *path, int mode, int flags);
    int (*flock)(int fd, int op);
    int (*fcntl)(int fd, int cmd, ...);
    void (*sync)(void);
    int (*fsync)(int fd);
    int (*fdatasync)(int fd);
    int (*fadvise)(int fd, off_t offset, off_t len, int advice);
    int (*fadvise64)(int fd, off64_t offset, off64_t len, int advice);
    int (*statfs)(const char *path, struct statfs *buf);
    int (*statfs64)(const char *path, struct statfs64 *buf);
    int (*fstatfs)(int fd, struct statfs *buf);
    int (*fstatfs64)(int fd, struct statfs64 *buf);
    int (*statvfs)(const char *path, struct statvfs *buf);
    int (*fstatvfs)(int fd, struct statvfs *buf);
    int (*mknod)(const char *path, mode_t mode, dev_t dev);
    int (*mknodat)(int dirfd, const char *path, mode_t mode, dev_t dev);
    ssize_t (*sendfile)(int outfd, int infd, off_t *offset, size_t count);
    ssize_t (*sendfile64)(int outfd, int infd, off64_t *offset, size_t count);
    int (*setxattr)(const char *path, const char *name,
                    const void *value, size_t size, int flags);
    int (*lsetxattr)(const char *path, const char *name,
                     const void *value, size_t size, int flags);
    int (*fsetxattr)(int fd, const char *name,
                     const void *value, size_t size, int flags);
    ssize_t (*getxattr)(const char *path, const char *name,
                        void *value, size_t size);
    ssize_t (*lgetxattr)(const char *path, const char *name,
                         void *value, size_t size);
    ssize_t (*fgetxattr)(int fd, const char *name, void *value, size_t size);
    ssize_t (*listxattr)(const char *path, char *list, size_t size);
    ssize_t (*llistxattr)(const char *path, char *list, size_t size);
    ssize_t (*flistxattr)(int fd, char *list, size_t size);
    int (*removexattr)(const char *path, const char *name);
    int (*lremovexattr)(const char *path, const char *name);
    int (*fremovexattr)(int fd, const char *name);
    mode_t (*umask)(mode_t mask);
    mode_t (*getumask)(void);
    int (*getdtablesize)(void);
    void *(*mmap)(void *start, size_t length, int prot,
                    int flags, int fd, off_t offset);
    int (*munmap)(void *start, size_t length);
    int (*msync)(void *start, size_t length, int flags);
#if 0
    int (*acl_delete_def_file)(const char *path_p);
    acl_t (*acl_get_fd)(int fd);
    acl_t (*acl_get_file)(const char *path_p, acl_type_t type);
    int (*acl_set_fd)(int fd, acl_t acl);
    int (*acl_set_file)(const char *path_p, acl_type_t type, acl_t acl);
#endif

    /* socket operations */
    int (*socket)(int dowmain, int type, int protocol);
    int (*accept)(int sockfd, struct sockaddr *addr, socklen_t *alen);
    int (*bind)(int sockfd, const struct sockaddr *addr, socklen_t alen);
    int (*connect)(int sockfd, const struct sockaddr *addr, socklen_t alen);
    int (*getpeername)(int sockfd, struct sockaddr *addr, socklen_t *alen);
    int (*getsockname)(int sockfd, struct sockaddr *addr, socklen_t *alen);
    int (*getsockopt)(int sockfd, int lvl, int oname,
                      void *oval, socklen_t *olen);
    int (*setsockopt)(int sockfd, int lvl, int oname,
                      const void *oval, socklen_t olen);
    int (*ioctl)(int fd, int request, ...);
    int (*listen)(int sockfd, int backlog);
    int (*recv)(int sockfd, void *buf, size_t len, int flags);
    int (*recvfrom)(int sockfd, void *buf, size_t len, int flags,
                    struct sockaddr *addr, socklen_t *alen);
    int (*recvmsg)(int sockfd, struct msghdr *msg, int flags);
    int (*send)(int sockfd, const void *buf, size_t len, int flags);
    int (*sendto)(int sockfd, const void *buf, size_t len, int flags,
                  const struct sockaddr *addr, socklen_t alen);
    int (*sendmsg)(int sockfd, const struct msghdr *msg, int flags);
    int (*shutdown)(int sockfd, int how);
    int (*socketpair)(int d, int type, int prtocol, int sv[2]);
    int (*pipe)(int filedes[2]);

    /* selinux operations */
    int (*getfscreatecon)(security_context_t *con);
    int (*getfilecon)(const char *path, security_context_t *con);
    int (*lgetfilecon)(const char *path, security_context_t *con);
    int (*fgetfilecon)(int fd, security_context_t *con);
    int (*setfscreatecon)(security_context_t con);
    int (*setfilecon)(const char *path, security_context_t con);
    int (*lsetfilecon)(const char *path, security_context_t con);
    int (*fsetfilecon)(int fd, security_context_t con);
} posix_ops;

#ifdef BITDEFS
#define stat stat64
#define fstat fstat64
#define fstatat fstatat64
#define lstat lstat64
#define statfs statfs64
#define fstatfs fstatfs64
#define sendfile sendfile64
#endif

extern posix_ops glibc_ops;
extern posix_ops pvfs_ops;

typedef struct pvfs_mmap_s
{
    void *mst;              /**< start of mmap region */
    size_t mlen;            /**< length of mmap region */
    int mprot;              /**< protection of mmap region */
    int mflags;             /**< flags of mmap region */
    int mfd;                /**< file descriptor of mmap region */
    off_t moff;             /**< offset of mmap region */
    struct qlist_head link;
} *pvfs_mmap_t;

/** PVFS-POSIX Descriptor table entry */
/* these items are shared between duped descrptors */
typedef struct pvfs_descriptor_status_s
{
    gen_mutex_t lock;         /**< protect struct from mult threads */
    int dup_cnt;              /**< number of desc using this stat */
    posix_ops *fsops;         /**< syscalls to use for this file */
    PVFS_object_ref pvfs_ref; /**< PVFS fs_id and handle for PVFS file */
    int flags;                /**< the open flags used for this file */
    int clrflags;             /**< modes that must be cleared on close */
    int mode;                 /**< stat mode of the file - may be volatile */
    int mode_deferred;        /**< mode bits requested but not set yet */
    off64_t file_pointer;     /**< offset from the beginning of the file */
    PVFS_ds_position token;   /**< used db Trove to iterate dirents */
    char *dpath;              /**< path of an open directory for fchdir */
    struct file_ent_s *fent;  /**< reference to cached objects */            
                              /**< set to NULL if not caching this file */
} pvfs_descriptor_status;

/* bit flags used only in pvfs_descriptor_status clrflags */
enum
{
    O_CLEAR_NONE = 0,
    O_CLEAR_READ = 1,
    O_CLEAR_WRITE = 2
};

/* these are unique among descriptors */
typedef struct pvfs_descriptor_s
{
    gen_mutex_t lock;         /**< protect struct from mult threads */
    int is_in_use;            /**< PVFS_FS if this descriptor is valid */
    int fd;                   /**< file number in PVFS descriptor_table */
    int true_fd;              /**< the true file number depending on FS */
    int fdflags;              /**< POSIX file descriptor flags */
    int shared_status;        /**< status shared with another desc or process */
    pvfs_descriptor_status *s;
} pvfs_descriptor;

typedef struct pvfs_descriptor_s PFILE; /* these are for posix interface */
typedef struct pvfs_descriptor_s PDIR;

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
