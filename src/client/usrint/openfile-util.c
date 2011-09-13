/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - routines to manage open files
 */
#define USRINT_SOURCE 1
#include "usrint.h"
#include <sys/syscall.h>
#ifndef SYS_readdir
#define SYS_readdir 89
#endif
#include "posix-ops.h"
#include "openfile-util.h"
#include "posix-pvfs.h"
#include "ucache.h"

static struct glibc_redirect_s
{
    int (*stat)(int ver, const char *path, struct stat *buf);
    int (*stat64)(int ver, const char *path, struct stat64 *buf);
    int (*fstat)(int ver, int fd, struct stat *buf);
    int (*fstat64)(int ver, int fd, struct stat64 *buf);
    int (*fstatat)(int ver, int fd, const char *path, struct stat *buf, int flag);
    int (*fstatat64)(int ver, int fd, const char *path, struct stat64 *buf, int flag);
    int (*lstat)(int ver, const char *path, struct stat *buf);
    int (*lstat64)(int ver, const char *path, struct stat64 *buf);
    int (*mknod)(int ver, const char *path, mode_t mode, dev_t dev);
    int (*mknodat)(int ver, int dirfd, const char *path, mode_t mode, dev_t dev);
} glibc_redirect;

#define PREALLOC 3
static char logfilepath[25];
static int logfile;
static int pvfs_initializing_flag = 0;
static int descriptor_table_count = 0; 
static int descriptor_table_size = 0; 
static pvfs_descriptor **descriptor_table; 
static char rstate[256];  /* used for random number generation */

posix_ops glibc_ops;

pvfs_descriptor pvfs_stdin =
{
    .fd = 0,
    .fsops = &glibc_ops,
    .true_fd = STDIN_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_RDONLY,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .dpath = NULL,
    .is_in_use = PVFS_FS
};

pvfs_descriptor pvfs_stdout =
{
    .fd = 1,
    .fsops = &glibc_ops,
    .true_fd = STDOUT_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .dpath = NULL,
    .is_in_use = PVFS_FS
};

pvfs_descriptor pvfs_stderr =
{
    .fd = 2,
    .fsops = &glibc_ops,
    .true_fd = STDERR_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .dpath = NULL,
    .is_in_use = PVFS_FS
};

static int my_glibc_stat(const char *path, struct stat *buf)
{
    return glibc_redirect.stat(3, path, buf);
}

static int my_glibc_stat64(const char *path, struct stat64 *buf)
{
    return glibc_redirect.stat64(3, path, buf);
}

static int my_glibc_fstat(int fd, struct stat *buf)
{
    return glibc_redirect.fstat(3, fd, buf);
}

static int my_glibc_fstat64(int fd, struct stat64 *buf)
{
    return glibc_redirect.fstat64(3, fd, buf);
}

static int my_glibc_fstatat(int fd, const char *path, struct stat *buf, int flag)
{
    return glibc_redirect.fstatat(3, fd, path, buf, flag);
}

static int my_glibc_fstatat64(int fd, const char *path, struct stat64 *buf, int flag)
{
    return glibc_redirect.fstatat64(3, fd, path, buf, flag);
}

static int my_glibc_lstat(const char *path, struct stat *buf)
{
    return glibc_redirect.lstat(3, path, buf);
}

static int my_glibc_lstat64(const char *path, struct stat64 *buf)
{
    return glibc_redirect.lstat64(3, path, buf);
}

static int my_glibc_mknod(const char *path, mode_t mode, dev_t dev)
{
    return glibc_redirect.mknod(1, path, mode, dev);
}

static int my_glibc_mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    return glibc_redirect.mknodat(1, dirfd, path, mode, dev);
}

static int my_glibc_getdents(u_int fd, struct dirent *dirp, u_int count)
{
    return syscall(SYS_getdents, fd, dirp, count);
}

static int my_glibc_getdents64(u_int fd, struct dirent64 *dirp, u_int count)
{
    return syscall(SYS_getdents64, fd, dirp, count);
}

static int my_glibc_fadvise64(int fd, off64_t offset, off64_t len, int advice)
{
    return syscall(SYS_fadvise64, fd, offset, len, advice);
}

static int my_glibc_fadvise(int fd, off_t offset, off_t len, int advice)
{
    return my_glibc_fadvise64(fd, (off64_t)offset, (off64_t)len, advice);
}

static int my_glibc_flush(int fd)
{
    errno = ENOSYS;
    return -1;
}
 
static int my_glibc_readdir(u_int fd, struct dirent *dirp, u_int count)
{
    return syscall(SYS_readdir, fd, dirp, count);
}

void load_glibc(void)
{ 
    memset((void *)&glibc_ops, 0, sizeof(glibc_ops));
    glibc_ops.open = dlsym(RTLD_NEXT, "open");
    glibc_ops.open64 = dlsym(RTLD_NEXT, "open64");
    glibc_ops.openat = dlsym(RTLD_NEXT, "openat");
    glibc_ops.openat64 = dlsym(RTLD_NEXT, "openat64");
    glibc_ops.creat = dlsym(RTLD_NEXT, "creat");
    glibc_ops.creat64 = dlsym(RTLD_NEXT, "creat64");
    glibc_ops.unlink = dlsym(RTLD_NEXT, "unlink");
    glibc_ops.unlinkat = dlsym(RTLD_NEXT, "unlinkat");
    glibc_ops.rename = dlsym(RTLD_NEXT, "rename");
    glibc_ops.renameat = dlsym(RTLD_NEXT, "renameat");
    glibc_ops.read = dlsym(RTLD_NEXT, "read");
    glibc_ops.pread = dlsym(RTLD_NEXT, "pread");
    glibc_ops.readv = dlsym(RTLD_NEXT, "readv");
    glibc_ops.pread64 = dlsym(RTLD_NEXT, "pread64");
    glibc_ops.write = dlsym(RTLD_NEXT, "write");
    glibc_ops.pwrite = dlsym(RTLD_NEXT, "pwrite");
    glibc_ops.writev = dlsym(RTLD_NEXT, "writev");
    glibc_ops.pwrite64 = dlsym(RTLD_NEXT, "pwrite64");
    glibc_ops.lseek = dlsym(RTLD_NEXT, "lseek");
    glibc_ops.lseek64 = dlsym(RTLD_NEXT, "lseek64");
    glibc_ops.truncate = dlsym(RTLD_NEXT, "truncate");
    glibc_ops.truncate64 = dlsym(RTLD_NEXT, "truncate64");
    glibc_ops.ftruncate = dlsym(RTLD_NEXT, "ftruncate");
    glibc_ops.ftruncate64 = dlsym(RTLD_NEXT, "ftruncate64");
    glibc_ops.fallocate = dlsym(RTLD_NEXT, "posix_fallocate");
    glibc_ops.close = dlsym(RTLD_NEXT, "close");
    glibc_ops.flush = my_glibc_flush;
    glibc_ops.stat = my_glibc_stat;
    glibc_redirect.stat = dlsym(RTLD_NEXT, "__xstat");
    glibc_ops.stat64 = my_glibc_stat64;
    glibc_redirect.stat64 = dlsym(RTLD_NEXT, "__xstat64");
    glibc_ops.fstat = my_glibc_fstat;
    glibc_redirect.fstat = dlsym(RTLD_NEXT, "__fxstat");
    glibc_ops.fstat64 = my_glibc_fstat64;
    glibc_redirect.fstat64 = dlsym(RTLD_NEXT, "__fxstat64");
    glibc_ops.fstatat = my_glibc_fstatat;
    glibc_redirect.fstatat = dlsym(RTLD_NEXT, "__fxstatat");
    glibc_ops.fstatat64 = my_glibc_fstatat64;
    glibc_redirect.fstatat64 = dlsym(RTLD_NEXT, "__fxstatat64");
    glibc_ops.lstat = my_glibc_lstat;
    glibc_redirect.lstat = dlsym(RTLD_NEXT, "__lxstat");
    glibc_ops.lstat64 = my_glibc_lstat64;
    glibc_redirect.lstat64 = dlsym(RTLD_NEXT, "__lxstat64");
    glibc_ops.dup = dlsym(RTLD_NEXT, "dup");
    glibc_ops.dup2 = dlsym(RTLD_NEXT, "dup2");
    glibc_ops.chown = dlsym(RTLD_NEXT, "chown");
    glibc_ops.fchown = dlsym(RTLD_NEXT, "fchown");
    glibc_ops.fchownat = dlsym(RTLD_NEXT, "fchownat");
    glibc_ops.lchown = dlsym(RTLD_NEXT, "lchown");
    glibc_ops.chmod = dlsym(RTLD_NEXT, "chmod");
    glibc_ops.fchmod = dlsym(RTLD_NEXT, "fchmod");
    glibc_ops.fchmodat = dlsym(RTLD_NEXT, "fchmodat");
    glibc_ops.mkdir = dlsym(RTLD_NEXT, "mkdir");
    glibc_ops.mkdirat = dlsym(RTLD_NEXT, "mkdirat");
    glibc_ops.rmdir = dlsym(RTLD_NEXT, "rmdir");
    glibc_ops.readlink = dlsym(RTLD_NEXT, "readlink");
    glibc_ops.readlinkat = dlsym(RTLD_NEXT, "readlinkat");
    glibc_ops.symlink = dlsym(RTLD_NEXT, "symlink");
    glibc_ops.symlinkat = dlsym(RTLD_NEXT, "symlinkat");
    glibc_ops.link = dlsym(RTLD_NEXT, "link");
    glibc_ops.linkat = dlsym(RTLD_NEXT, "linkat");
    glibc_ops.readdir = my_glibc_readdir;
    glibc_ops.getdents = my_glibc_getdents;
    glibc_ops.getdents64 = my_glibc_getdents64;
    glibc_ops.access = dlsym(RTLD_NEXT, "access");
    glibc_ops.faccessat = dlsym(RTLD_NEXT, "faccessat");
    glibc_ops.flock = dlsym(RTLD_NEXT, "flock");
    glibc_ops.fcntl = dlsym(RTLD_NEXT, "fcntl");
    glibc_ops.sync = dlsym(RTLD_NEXT, "sync");
    glibc_ops.fsync = dlsym(RTLD_NEXT, "fsync");
    glibc_ops.fdatasync = dlsym(RTLD_NEXT, "fdatasync");
    glibc_ops.fadvise = my_glibc_fadvise;
    glibc_ops.fadvise64 = my_glibc_fadvise64;
    glibc_ops.statfs = dlsym(RTLD_NEXT, "statfs");
    glibc_ops.statfs64 = dlsym(RTLD_NEXT, "statfs64");
    glibc_ops.fstatfs = dlsym(RTLD_NEXT, "fstatfs");
    glibc_ops.fstatfs64 = dlsym(RTLD_NEXT, "fstatfs64");
    glibc_ops.mknod = my_glibc_mknod;
    glibc_redirect.mknod = dlsym(RTLD_NEXT, "__xmknod");
    glibc_ops.mknodat = my_glibc_mknodat;
    glibc_redirect.mknodat = dlsym(RTLD_NEXT, "__xmknodat");
    glibc_ops.sendfile = dlsym(RTLD_NEXT, "sendfile");
    glibc_ops.sendfile64 = dlsym(RTLD_NEXT, "sendfile64");
#ifdef HAVE_ATTR_XATTR_H
    glibc_ops.setxattr = dlsym(RTLD_NEXT, "setxattr");
    glibc_ops.lsetxattr = dlsym(RTLD_NEXT, "lsetxattr");
    glibc_ops.fsetxattr = dlsym(RTLD_NEXT, "fsetxattr");
    glibc_ops.getxattr = dlsym(RTLD_NEXT, "getxattr");
    glibc_ops.lgetxattr = dlsym(RTLD_NEXT, "lgetxattr");
    glibc_ops.fgetxattr = dlsym(RTLD_NEXT, "fgetxattr");
    glibc_ops.listxattr = dlsym(RTLD_NEXT, "listxattr");
    glibc_ops.llistxattr = dlsym(RTLD_NEXT, "llistxattr");
    glibc_ops.flistxattr = dlsym(RTLD_NEXT, "flistxattr");
    glibc_ops.removexattr = dlsym(RTLD_NEXT, "removexattr");
    glibc_ops.lremovexattr = dlsym(RTLD_NEXT, "lremovexattr");
    glibc_ops.fremovexattr = dlsym(RTLD_NEXT, "fremovexattr");
#endif
    glibc_ops.socket = dlsym(RTLD_NEXT, "socket");
    glibc_ops.accept = dlsym(RTLD_NEXT, "accept");
    glibc_ops.bind = dlsym(RTLD_NEXT, "bind");
    glibc_ops.connect = dlsym(RTLD_NEXT, "connect");
    glibc_ops.getpeername = dlsym(RTLD_NEXT, "getpeername");
    glibc_ops.getsockname = dlsym(RTLD_NEXT, "getsockname");
    glibc_ops.getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    glibc_ops.setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    glibc_ops.ioctl = dlsym(RTLD_NEXT, "ioctl");
    glibc_ops.listen = dlsym(RTLD_NEXT, "listen");
    glibc_ops.recv = dlsym(RTLD_NEXT, "recv");
    glibc_ops.recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    glibc_ops.recvmsg = dlsym(RTLD_NEXT, "recvmsg");
    //glibc_ops.select = dlsym(RTLD_NEXT, "select");
    //glibc_ops.FD_CLR = dlsym(RTLD_NEXT, "FD_CLR");
    //glibc_ops.FD_ISSET = dlsym(RTLD_NEXT, "FD_ISSET");
    //glibc_ops.FD_SET = dlsym(RTLD_NEXT, "FD_SET");
    //glibc_ops.FD_ZERO = dlsym(RTLD_NEXT, "FD_ZERO");
    //glibc_ops.pselect = dlsym(RTLD_NEXT, "pselect");
    glibc_ops.send = dlsym(RTLD_NEXT, "send");
    glibc_ops.sendto = dlsym(RTLD_NEXT, "sendto");
    glibc_ops.sendmsg = dlsym(RTLD_NEXT, "sendmsg");
    glibc_ops.shutdown = dlsym(RTLD_NEXT, "shutdown");
    glibc_ops.socketpair = dlsym(RTLD_NEXT, "socketpair");
    glibc_ops.pipe = dlsym(RTLD_NEXT, "pipe");
    glibc_ops.umask = dlsym(RTLD_NEXT, "umask");
    glibc_ops.getumask = dlsym(RTLD_NEXT, "getumask");
    glibc_ops.getdtablesize = dlsym(RTLD_NEXT, "getdtablesize");

/* PVFS does not implement socket ops */
    pvfs_ops.socket = dlsym(RTLD_NEXT, "socket");
    pvfs_ops.accept = dlsym(RTLD_NEXT, "accept");
    pvfs_ops.bind = dlsym(RTLD_NEXT, "bind");
    pvfs_ops.connect = dlsym(RTLD_NEXT, "connect");
    pvfs_ops.getpeername = dlsym(RTLD_NEXT, "getpeername");
    pvfs_ops.getsockname = dlsym(RTLD_NEXT, "getsockname");
    pvfs_ops.getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    pvfs_ops.setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    pvfs_ops.ioctl = dlsym(RTLD_NEXT, "ioctl");
    pvfs_ops.listen = dlsym(RTLD_NEXT, "listen");
    pvfs_ops.recv = dlsym(RTLD_NEXT, "recv");
    pvfs_ops.recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    pvfs_ops.recvmsg = dlsym(RTLD_NEXT, "recvmsg");
    //pvfs_ops.select = dlsym(RTLD_NEXT, "select");
    //pvfs_ops.FD_CLR = dlsym(RTLD_NEXT, "FD_CLR");
    //pvfs_ops.FD_ISSET = dlsym(RTLD_NEXT, "FD_ISSET");
    //pvfs_ops.FD_SET = dlsym(RTLD_NEXT, "FD_SET");
    //pvfs_ops.FD_ZERO = dlsym(RTLD_NEXT, "FD_ZERO");
    //pvfs_ops.pselect = dlsym(RTLD_NEXT, "pselect");
    pvfs_ops.send = dlsym(RTLD_NEXT, "send");
    pvfs_ops.sendto = dlsym(RTLD_NEXT, "sendto");
    pvfs_ops.sendmsg = dlsym(RTLD_NEXT, "sendmsg");
    pvfs_ops.shutdown = dlsym(RTLD_NEXT, "shutdown");
    pvfs_ops.socketpair = dlsym(RTLD_NEXT, "socketpair");
    pvfs_ops.pipe = dlsym(RTLD_NEXT, "pipe");
}

static void usrint_cleanup(void)
{
    /* later check for an error that might want us */
    /* to keep this - for now it is empty */
    glibc_ops.unlink(logfilepath);
}

/* 
 * Perform PVFS initialization tasks
 */ 

void pvfs_sys_init(void) { 
	struct rlimit rl; 
	int rc; 
    static int pvfs_lib_init_flag = 0; 

    if (pvfs_lib_init_flag)
    {
        return;
    }
    pvfs_lib_init_flag = 1; /* should only run this once */
    pvfs_initializing_flag = 1;

    /* this allows system calls to run */
    load_glibc();
    PINT_initrand();

    /* if this fails no much we can do about it */
    atexit(usrint_cleanup);

	rc = getrlimit(RLIMIT_NOFILE, &rl); 
	/* need to check for "INFINITY" */

    /* set up descriptor table */
	descriptor_table_size = rl.rlim_max;
	descriptor_table =
			(pvfs_descriptor **)malloc(sizeof(pvfs_descriptor *) *
			descriptor_table_size);
	memset(descriptor_table, 0,
			(sizeof(pvfs_descriptor *) * descriptor_table_size));
    descriptor_table[0] = &pvfs_stdin;
    descriptor_table[1] = &pvfs_stdout;
    descriptor_table[2] = &pvfs_stderr;
    descriptor_table_count = PREALLOC;

    sprintf(logfilepath, "/tmp/pvfsuid-%05d.log", (int)(getuid()));
    logfile = glibc_ops.open(logfilepath, O_RDWR|O_CREAT, 0600);
    if (logfile < 0)
    {
        perror("failed in pvfs_sys_init");
        exit(-1);
    }

	/* initalize PVFS */ 
	PVFS_util_init_defaults(); 
    if (errno == EINPROGRESS)
    {
        errno = 0;
    }

    /* call other initialization routines */

    /* Initialize User Cache */
    if(ucache && ucache_locks)
    {
        ucache_initialize();
    }

    //PVFS_perror_gossip_silent(); 
    pvfs_initializing_flag = 0;
}

int pvfs_descriptor_table_size(void)
{
    return descriptor_table_size;
}

/*
 * Allocate a new pvfs_descriptor
 * initialize fsops to the given set
 */
 pvfs_descriptor *pvfs_alloc_descriptor(posix_ops *fsops, int fd, 
                                        PVFS_object_ref *file_ref
 )
 {
 	int newfd, flags = 0; 
    if (fsops == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    pvfs_sys_init();

    if (fd == -1)
    {
        /* PVFS file allocate a real descriptor for it */
        newfd = glibc_ops.dup(logfile);
    }
    else
    {
        /* opened by glibc, make sure this is a valid fd */
        newfd = fd;
        flags = glibc_ops.fcntl(newfd, F_GETFL);
        if (flags < 0)
        {
            return NULL;
        }
    }
	descriptor_table_count++;
    descriptor_table[newfd] =
                (pvfs_descriptor *)malloc(sizeof(pvfs_descriptor));
    if (!descriptor_table[newfd])
    {
        return NULL;
    }

	/* fill in descriptor */
    /* add reference to chache objects here */
	descriptor_table[newfd]->fd = newfd;
	descriptor_table[newfd]->dup_cnt = 1;
	descriptor_table[newfd]->fsops = fsops;
	descriptor_table[newfd]->true_fd = newfd;
	descriptor_table[newfd]->pvfs_ref.fs_id = file_ref->fs_id;
	descriptor_table[newfd]->pvfs_ref.handle = file_ref->handle;
	descriptor_table[newfd]->flags = flags;
	descriptor_table[newfd]->mode = 0;
	descriptor_table[newfd]->file_pointer = 0;
	descriptor_table[newfd]->token = 0;
	descriptor_table[newfd]->dpath = NULL;
	descriptor_table[newfd]->is_in_use = PVFS_FS;
    descriptor_table[newfd]->mtbl = NULL;

#if 0
    /* File reference won't always be passed in */
    if(file_ref != NULL)
    {
        /* We have the file identifiers, so insert file info into ucache */
        rc = ucache_open_file(&(file_ref->fs_id), &(file_ref->handle), 
                                       descriptor_table[newfd]->mtbl);
        /* Unique Entry */
        if(rc > 0)
        {   
            descriptor_table[newfd]->mtbl->ref_cnt = 1;
        }
        /* File already in Cache */
        else if(rc == 0){
            descriptor_table[newfd]->mtbl->ref_cnt++;
        }
        /* Could not insert */
        else
        {
            /* TODO: probably need modify this */  
        }
    }
#endif

    return descriptor_table[newfd];
}

/*
 * Function for duplicating a descriptor - used in dup and dup2 calls
 */
int pvfs_dup_descriptor(int oldfd, int newfd)
{
    int rc = 0;
    if (oldfd < 0 || oldfd >= descriptor_table_size)
    {
        errno = EBADF;
        return -1;
    }
    if (newfd == -1)
    {
        newfd = glibc_ops.dup(logfile);
        if (newfd < 0)
        {
            return newfd;
        }
    }
    else
    {
        if (descriptor_table[newfd] != NULL)
        {
            /* close old file in new slot */
            rc = pvfs_free_descriptor(newfd);
            if (rc < 0)
            {
                return rc;
            }
        }
    }
    rc = glibc_ops.dup2(oldfd, newfd);
    if (rc < 0)
    {
        return rc;
    }
    descriptor_table[newfd] = descriptor_table[oldfd];
    descriptor_table[newfd]->dup_cnt++;
	descriptor_table_count++;
    return 0;
}

/*
 * Return a pointer to the pvfs_descriptor for the file descriptor or null
 * if there is no entry for the given file descriptor
 * should probably be inline if we can get at static table that way
 */
pvfs_descriptor *pvfs_find_descriptor(int fd)
{
    pvfs_descriptor *pd;
    if (fd < 0 || fd >= descriptor_table_size)
    {
        errno = EBADF;
        return NULL;
    }
    pd = descriptor_table[fd];
    if (!pd)
    {
        int flags;
        /* see if glibc opened this file without our knowing */
        flags = glibc_ops.fcntl(fd, F_GETFL);
        if (flags < 0)
        {
            errno = EBADF;
            return NULL;
        }
        pd = (pvfs_descriptor *)malloc(sizeof(pvfs_descriptor));
	    /* fill in descriptor */
	    pd->fd = fd;
	    pd->dup_cnt = 1;
	    pd->fsops = &glibc_ops;
	    pd->true_fd = fd;
	    pd->pvfs_ref.fs_id = 0;
	    pd->pvfs_ref.handle = 0;
	    pd->flags = flags;
	    pd->mode = 0;
	    pd->file_pointer = 0;
	    pd->token = 0;
	    pd->dpath = NULL;
	    pd->is_in_use = PVFS_FS;
        descriptor_table[fd] = pd;
    }
    else if (pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return NULL;
    }
	return pd;
}

int pvfs_free_descriptor(int fd)
{
    pvfs_descriptor *pd;
    debug("pvfs_free_descriptor called with %d\n", fd);

    pd = pvfs_find_descriptor(fd);
    if (pd == NULL)
    {
        return -1;
    }

	/* clear out table entry */
	descriptor_table[fd] = NULL;
    glibc_ops.close(fd);

	/* keep up with used descriptors */
	descriptor_table_count--;

    /* check if last copy */
    if (--(pd->dup_cnt) <= 0)
    {
        if (pd->dpath)
        {
            free(pd->dpath);
        }

#if 0
        /* release cache objects here */
        pd->mtbl->ref_cnt--;
        if(pd->mtbl->ref_cnt == 0)
        {
            /* Flush dirty blocks before file removal from cache */
            ucache_flush(&(pd->pvfs_ref.fs_id), &(pd->pvfs_ref.handle));
            /* remove all of this file's associated data from cache */
            ucache_close_file(&(pd->pvfs_ref.fs_id), &(pd->pvfs_ref.handle));
        }
#endif

	    /* free descriptor - wipe memory first */
	    memset(pd, 0, sizeof(pvfs_descriptor));
	    free(pd);
    }

    debug("pvfs_free_descriptor returns %d\n", 0);
	return 0;
}

/* 
 * takes a path that is relative to the working dir and
 * expands it all the way to the root
 */
char *pvfs_qualify_path(const char *path)
{
    int cdsz;
    int psz;
    char *curdir = NULL;
    char *newpath = NULL;

    if(path[0] != '/')
    {
        curdir = get_current_dir_name();
        if (curdir == NULL)
        {
            /* error, bail out */
            return NULL;
        }
        cdsz = strlen(curdir);
        psz = strlen(path);
        /* allocate buffer for whole path and copy */
        newpath = (char *)malloc(cdsz + psz + 2);
        strncpy(newpath, curdir, cdsz);
        free(curdir);
        strncat(newpath, "/", 1);
        strncat(newpath, path, psz);
    }
    else
    {
        newpath = (char *)path;
    }
    return newpath;
}

/**
 * Determines if a path is part of a PVFS Filesystem 
 *
 * returns 1 if PVFS 0 otherwise
 */

int is_pvfs_path(const char *path)
{
#ifdef PVFS_ASSUME_MOUNT
    struct statfs file_system;
#endif
    int rc = 0;
    PVFS_fs_id fs_id;
    char pvfs_path[256];
    char *newpath = NULL ;

    if (pvfs_initializing_flag)
    {
        /* we cannot open a PVFS file while
         * initializing PVFS
         */
        return 0;
    }

    pvfs_sys_init();
#ifdef PVFS_ASSUME_MOUNT
    memset(&file_system, 0, sizeof(file_system));
    
    newpath = pvfs_qualify_path(path);
    /* lop off the last segment of the path */
    int count;
    for(count = strlen(newpath) -2; count > 0; count--)
    {
        if(newpath[count] == '/')
        {
            newpath[count] = '\0';
            break;
        }
    }
    /* this must call standard glibc statfs */
    glibc_ops.statfs(newpath, &file_system);
    if (newpath != path)
    {
        free(newpath);
    }
    if(file_system.f_type == PVFS_FS)
    {
        return 1; /* PVFS */
    }
    else
    {
        return 0; /* not PVFS assume the kernel can handle it */
    }
/***************************************************************/
#else
/***************************************************************/
    newpath = pvfs_qualify_path(path);
    rc = PVFS_util_resolve(newpath, &fs_id, pvfs_path, 256);
    if (newpath != path)
    {
        free(newpath);
    }
    if (rc < 0)
    {
        if (rc == -PVFS_ENOENT)
        {
            return 0; /* not a PVFS path */
        }
        errno = rc;
        return 0; /* an error returned - let glibc deal with it */
        // return -1; /* an error returned */
    }
    return 1; /* a PVFS path */
#endif
}

/**
 * Split a pathname into a directory and a filename.
 * If non-null is passed as the directory or filename,
 * the field will be allocated and filled with the correct value
 *
 * A slash at the end of the path is interpreted as no filename
 * and is an error.  To parse the last dir in a path, remove this
 * trailing slash.  No filename with no directory is OK.
 */
int split_pathname( const char *path,
                    int dirflag,
                    char **directory,
                    char **filename)
{
    int i, fnlen, slashes = 0;
    int length = strlen("pvfs2");

    if (!path || !directory || !filename)
    {
        errno = EINVAL;
        return -1;
    }
	/* chop off pvfs2 prefix */
	if (strncmp(path, "pvfs2:", length) == 0)
    {
		path = &path[length];
    }
    /* Split path into a directory and filename */
    length = strnlen(path, PVFS_PATH_MAX);
    if (length == PVFS_PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    i = length - 1;
    if (dirflag)
    {
        /* skip any trailing slashes */
        for(; i >= 0 && path[i] == '/'; i--)
        {
            slashes++;
        }
    }
    for (; i >= 0; i--)
    {
        if (path[i] == '/')
        {
            /* parse the directory */
            *directory = malloc(i + 1);
            if (!*directory)
            {
                return -1;
            }
            strncpy(*directory, path, i);
            (*directory)[i] = '\0';
            break;
        }
    }
    if (i == -1)
    {
        /* found no '/' path is all filename */
        *directory = NULL;
    }
    i++;
    /* copy the filename */
    fnlen = length - i - slashes;
    if (fnlen == 0)
    {
        filename = NULL;
        if (!directory)
        {
            errno = EISDIR;
        }
        else
        {
            errno = ENOENT;
        }
        return -1;
    }
    /* check flag to see if there are slashes to skip */
    *filename = malloc(fnlen + 1);
    if (!*filename)
    {
        if (*directory)
        {
            free(*directory);
        }
        *directory = NULL;
        *filename = NULL;
        return -1;
    }
    strncpy(*filename, path + i, length - i);
    (*filename)[length - i] = '\0';
    return 0;
}

void PINT_initrand(void)
{
    static int init_called = 0;
    pid_t pid;
    uid_t uid;
    gid_t gid;
    struct timeval time;
    char *oldstate;
    unsigned int seed;

    if (init_called)
    {
        return;
    }
    init_called = 1;
    pid = getpid();
    uid = getuid();
    gid = getgid();
    gettimeofday(&time, NULL);
    seed = (((pid << 16) ^ uid) ^ (gid << 8)) ^ time.tv_usec;
    oldstate = initstate(seed, rstate, 256);
    setstate(oldstate);
}

long int PINT_random(void)
{
    char *oldstate;
    long int rndval;

    PINT_initrand();
    oldstate = setstate(rstate);
    rndval = random();
    setstate(oldstate);
    return rndval;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
