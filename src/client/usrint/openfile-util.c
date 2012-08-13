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
#include "iocommon.h"
#include "posix-pvfs.h"
#include "pvfs-path.h"
#ifdef PVFS_AIO_ENABLE
#include "aiocommon.h"
#endif

#if PVFS_UCACHE_ENABLE
#include "ucache.h"
#endif

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
static char logfilepath[30];
static int logfile;
static int descriptor_table_count = 0; 
static int descriptor_table_size = 0; 
static pvfs_descriptor **descriptor_table; 
static char rstate[256];  /* used for random number generation */

posix_ops glibc_ops;

pvfs_descriptor_status pvfs_stdin_status =
{
    .dup_cnt = 1,
    .fsops = &glibc_ops,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_RDONLY,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .dpath = NULL,
    .fent = NULL
};

pvfs_descriptor pvfs_stdin =
{
    .is_in_use = PVFS_FS,
    .fd = 0,
    .true_fd = STDIN_FILENO,
    .fdflags = 0,
    .s = &pvfs_stdin_status
};

pvfs_descriptor_status pvfs_stdout_status =
{
    .dup_cnt = 1,
    .fsops = &glibc_ops,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .dpath = NULL,
    .fent = NULL
};

pvfs_descriptor pvfs_stdout =
{
    .is_in_use = PVFS_FS,
    .fd = 1,
    .true_fd = STDOUT_FILENO,
    .fdflags = 0,
    .s = &pvfs_stdout_status
};

pvfs_descriptor_status pvfs_stderr_status =
{
    .dup_cnt = 1,
    .fsops = &glibc_ops,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .dpath = NULL,
    .fent = NULL
};

pvfs_descriptor pvfs_stderr =
{
    .is_in_use = PVFS_FS,
    .fd = 2,
    .true_fd = STDERR_FILENO,
    .fdflags = 0,
    .s = &pvfs_stderr_status
};

static int my_glibc_stat(const char *path, struct stat *buf)
{
    int rc = glibc_redirect.stat(_STAT_VER, path, buf);
    return rc;
}

static int my_glibc_stat64(const char *path, struct stat64 *buf)
{
    int rc = glibc_redirect.stat64(_STAT_VER, path, buf);
    return rc;
}

static int my_glibc_fstat(int fd, struct stat *buf)
{
    return glibc_redirect.fstat(_STAT_VER, fd, buf);
}

static int my_glibc_fstat64(int fd, struct stat64 *buf)
{
    return glibc_redirect.fstat64(_STAT_VER, fd, buf);
}

static int my_glibc_fstatat(int fd, const char *path, struct stat *buf, int flag)
{
    return glibc_redirect.fstatat(_STAT_VER, fd, path, buf, flag);
}

static int my_glibc_fstatat64(int fd, const char *path, struct stat64 *buf, int flag)
{
    return glibc_redirect.fstatat64(_STAT_VER, fd, path, buf, flag);
}

static int my_glibc_lstat(const char *path, struct stat *buf)
{
    return glibc_redirect.lstat(_STAT_VER, path, buf);
}

static int my_glibc_lstat64(const char *path, struct stat64 *buf)
{
    return glibc_redirect.lstat64(_STAT_VER, path, buf);
}

static int my_glibc_mknod(const char *path, mode_t mode, dev_t dev)
{
    return glibc_redirect.mknod(_MKNOD_VER, path, mode, dev);
}

static int my_glibc_mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    return glibc_redirect.mknodat(_MKNOD_VER, dirfd, path, mode, dev);
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

static int my_glibc_readdir(u_int fd, struct dirent *dirp, u_int count)
{
    return syscall(SYS_readdir, fd, dirp, count);
}

/* moved the need for this to posix-pvfs.c */
#if 0
static int my_glibc_getcwd(char *buf, unsigned long size)
{
    return syscall(SYS_getcwd, buf, size);
}
#endif

void load_glibc(void)
{ 
    void *libc_handle;
    libc_handle = dlopen("libc.so.6", RTLD_LAZY|RTLD_GLOBAL);
    if (!libc_handle)
    {
        fprintf(stderr,"Failed to open libc.so\n");
        libc_handle = RTLD_NEXT;
    }
    memset((void *)&glibc_ops, 0, sizeof(glibc_ops));
    glibc_ops.open = dlsym(libc_handle, "open");
    glibc_ops.open64 = dlsym(libc_handle, "open64");
    glibc_ops.openat = dlsym(libc_handle, "openat");
    glibc_ops.openat64 = dlsym(libc_handle, "openat64");
    glibc_ops.creat = dlsym(libc_handle, "creat");
    glibc_ops.creat64 = dlsym(libc_handle, "creat64");
    glibc_ops.unlink = dlsym(libc_handle, "unlink");
    glibc_ops.unlinkat = dlsym(libc_handle, "unlinkat");
    glibc_ops.rename = dlsym(libc_handle, "rename");
    glibc_ops.renameat = dlsym(libc_handle, "renameat");
    glibc_ops.read = dlsym(libc_handle, "read");
    glibc_ops.pread = dlsym(libc_handle, "pread");
    glibc_ops.readv = dlsym(libc_handle, "readv");
    glibc_ops.pread64 = dlsym(libc_handle, "pread64");
    glibc_ops.write = dlsym(libc_handle, "write");
    glibc_ops.pwrite = dlsym(libc_handle, "pwrite");
    glibc_ops.writev = dlsym(libc_handle, "writev");
    glibc_ops.pwrite64 = dlsym(libc_handle, "pwrite64");
    glibc_ops.lseek = dlsym(libc_handle, "lseek");
    glibc_ops.lseek64 = dlsym(libc_handle, "lseek64");
    glibc_ops.truncate = dlsym(libc_handle, "truncate");
    glibc_ops.truncate64 = dlsym(libc_handle, "truncate64");
    glibc_ops.ftruncate = dlsym(libc_handle, "ftruncate");
    glibc_ops.ftruncate64 = dlsym(libc_handle, "ftruncate64");
    glibc_ops.fallocate = dlsym(libc_handle, "posix_fallocate");
    glibc_ops.close = dlsym(libc_handle, "close");
    glibc_ops.stat = my_glibc_stat;
    glibc_redirect.stat = dlsym(libc_handle, "__xstat");
    glibc_ops.stat64 = my_glibc_stat64;
    glibc_redirect.stat64 = dlsym(libc_handle, "__xstat64");
    glibc_ops.fstat = my_glibc_fstat;
    glibc_redirect.fstat = dlsym(libc_handle, "__fxstat");
    glibc_ops.fstat64 = my_glibc_fstat64;
    glibc_redirect.fstat64 = dlsym(libc_handle, "__fxstat64");
    glibc_ops.fstatat = my_glibc_fstatat;
    glibc_redirect.fstatat = dlsym(libc_handle, "__fxstatat");
    glibc_ops.fstatat64 = my_glibc_fstatat64;
    glibc_redirect.fstatat64 = dlsym(libc_handle, "__fxstatat64");
    glibc_ops.lstat = my_glibc_lstat;
    glibc_redirect.lstat = dlsym(libc_handle, "__lxstat");
    glibc_ops.lstat64 = my_glibc_lstat64;
    glibc_redirect.lstat64 = dlsym(libc_handle, "__lxstat64");
    glibc_ops.futimesat = dlsym(libc_handle, "futimesat");
    glibc_ops.utimes = dlsym(libc_handle, "utimes");
    glibc_ops.utime = dlsym(libc_handle, "utime");
    glibc_ops.futimes = dlsym(libc_handle, "futimes");
    glibc_ops.dup = dlsym(libc_handle, "dup");
    glibc_ops.dup2 = dlsym(libc_handle, "dup2");
    glibc_ops.chown = dlsym(libc_handle, "chown");
    glibc_ops.fchown = dlsym(libc_handle, "fchown");
    glibc_ops.fchownat = dlsym(libc_handle, "fchownat");
    glibc_ops.lchown = dlsym(libc_handle, "lchown");
    glibc_ops.chmod = dlsym(libc_handle, "chmod");
    glibc_ops.fchmod = dlsym(libc_handle, "fchmod");
    glibc_ops.fchmodat = dlsym(libc_handle, "fchmodat");
    glibc_ops.mkdir = dlsym(libc_handle, "mkdir");
    glibc_ops.mkdirat = dlsym(libc_handle, "mkdirat");
    glibc_ops.rmdir = dlsym(libc_handle, "rmdir");
    glibc_ops.readlink = dlsym(libc_handle, "readlink");
    glibc_ops.readlinkat = dlsym(libc_handle, "readlinkat");
    glibc_ops.symlink = dlsym(libc_handle, "symlink");
    glibc_ops.symlinkat = dlsym(libc_handle, "symlinkat");
    glibc_ops.link = dlsym(libc_handle, "link");
    glibc_ops.linkat = dlsym(libc_handle, "linkat");
    glibc_ops.readdir = my_glibc_readdir;
    glibc_ops.getdents = my_glibc_getdents;
    glibc_ops.getdents64 = my_glibc_getdents64;
    glibc_ops.access = dlsym(libc_handle, "access");
    glibc_ops.faccessat = dlsym(libc_handle, "faccessat");
    glibc_ops.flock = dlsym(libc_handle, "flock");
    glibc_ops.fcntl = dlsym(libc_handle, "fcntl");
    glibc_ops.sync = dlsym(libc_handle, "sync");
    glibc_ops.fsync = dlsym(libc_handle, "fsync");
    glibc_ops.fdatasync = dlsym(libc_handle, "fdatasync");
    glibc_ops.fadvise = my_glibc_fadvise;
    glibc_ops.fadvise64 = my_glibc_fadvise64;
    glibc_ops.statfs = dlsym(libc_handle, "statfs");
    glibc_ops.statfs64 = dlsym(libc_handle, "statfs64");
    glibc_ops.fstatfs = dlsym(libc_handle, "fstatfs");
    glibc_ops.fstatfs64 = dlsym(libc_handle, "fstatfs64");
    glibc_ops.statvfs = dlsym(libc_handle, "statvfs");
    glibc_ops.fstatvfs = dlsym(libc_handle, "fstatvfs");
    glibc_ops.mknod = my_glibc_mknod;
    glibc_redirect.mknod = dlsym(libc_handle, "__xmknod");
    glibc_ops.mknodat = my_glibc_mknodat;
    glibc_redirect.mknodat = dlsym(libc_handle, "__xmknodat");
    glibc_ops.sendfile = dlsym(libc_handle, "sendfile");
    glibc_ops.sendfile64 = dlsym(libc_handle, "sendfile64");
#ifdef HAVE_ATTR_XATTR_H
    glibc_ops.setxattr = dlsym(libc_handle, "setxattr");
    glibc_ops.lsetxattr = dlsym(libc_handle, "lsetxattr");
    glibc_ops.fsetxattr = dlsym(libc_handle, "fsetxattr");
    glibc_ops.getxattr = dlsym(libc_handle, "getxattr");
    glibc_ops.lgetxattr = dlsym(libc_handle, "lgetxattr");
    glibc_ops.fgetxattr = dlsym(libc_handle, "fgetxattr");
    glibc_ops.listxattr = dlsym(libc_handle, "listxattr");
    glibc_ops.llistxattr = dlsym(libc_handle, "llistxattr");
    glibc_ops.flistxattr = dlsym(libc_handle, "flistxattr");
    glibc_ops.removexattr = dlsym(libc_handle, "removexattr");
    glibc_ops.lremovexattr = dlsym(libc_handle, "lremovexattr");
    glibc_ops.fremovexattr = dlsym(libc_handle, "fremovexattr");
#endif
    glibc_ops.socket = dlsym(libc_handle, "socket");
    glibc_ops.accept = dlsym(libc_handle, "accept");
    glibc_ops.bind = dlsym(libc_handle, "bind");
    glibc_ops.connect = dlsym(libc_handle, "connect");
    glibc_ops.getpeername = dlsym(libc_handle, "getpeername");
    glibc_ops.getsockname = dlsym(libc_handle, "getsockname");
    glibc_ops.getsockopt = dlsym(libc_handle, "getsockopt");
    glibc_ops.setsockopt = dlsym(libc_handle, "setsockopt");
    glibc_ops.ioctl = dlsym(libc_handle, "ioctl");
    glibc_ops.listen = dlsym(libc_handle, "listen");
    glibc_ops.recv = dlsym(libc_handle, "recv");
    glibc_ops.recvfrom = dlsym(libc_handle, "recvfrom");
    glibc_ops.recvmsg = dlsym(libc_handle, "recvmsg");
    //glibc_ops.select = dlsym(libc_handle, "select");
    //glibc_ops.FD_CLR = dlsym(libc_handle, "FD_CLR");
    //glibc_ops.FD_ISSET = dlsym(libc_handle, "FD_ISSET");
    //glibc_ops.FD_SET = dlsym(libc_handle, "FD_SET");
    //glibc_ops.FD_ZERO = dlsym(libc_handle, "FD_ZERO");
    //glibc_ops.pselect = dlsym(libc_handle, "pselect");
    glibc_ops.send = dlsym(libc_handle, "send");
    glibc_ops.sendto = dlsym(libc_handle, "sendto");
    glibc_ops.sendmsg = dlsym(libc_handle, "sendmsg");
    glibc_ops.shutdown = dlsym(libc_handle, "shutdown");
    glibc_ops.socketpair = dlsym(libc_handle, "socketpair");
    glibc_ops.pipe = dlsym(libc_handle, "pipe");
    glibc_ops.umask = dlsym(libc_handle, "umask");
    glibc_ops.getumask = dlsym(libc_handle, "getumask");
    glibc_ops.getdtablesize = dlsym(libc_handle, "getdtablesize");
    glibc_ops.mmap = dlsym(libc_handle, "mmap");
    glibc_ops.munmap = dlsym(libc_handle, "munmap");
    glibc_ops.msync = dlsym(libc_handle, "msync");
#if 0
    glibc_ops.acl_delete_def_file = dlsym(libc_handle, "acl_delete_def_file");
    glibc_ops.acl_get_fd = dlsym(libc_handle, "acl_get_fd");
    glibc_ops.acl_get_file = dlsym(libc_handle, "acl_get_file");
    glibc_ops.acl_set_fd = dlsym(libc_handle, "acl_set_fd");
    glibc_ops.acl_set_file = dlsym(libc_handle, "acl_set_file");
#endif

/* PVFS does not implement socket ops */
    pvfs_ops.socket = dlsym(libc_handle, "socket");
    pvfs_ops.accept = dlsym(libc_handle, "accept");
    pvfs_ops.bind = dlsym(libc_handle, "bind");
    pvfs_ops.connect = dlsym(libc_handle, "connect");
    pvfs_ops.getpeername = dlsym(libc_handle, "getpeername");
    pvfs_ops.getsockname = dlsym(libc_handle, "getsockname");
    pvfs_ops.getsockopt = dlsym(libc_handle, "getsockopt");
    pvfs_ops.setsockopt = dlsym(libc_handle, "setsockopt");
    pvfs_ops.ioctl = dlsym(libc_handle, "ioctl");
    pvfs_ops.listen = dlsym(libc_handle, "listen");
    pvfs_ops.recv = dlsym(libc_handle, "recv");
    pvfs_ops.recvfrom = dlsym(libc_handle, "recvfrom");
    pvfs_ops.recvmsg = dlsym(libc_handle, "recvmsg");
    //pvfs_ops.select = dlsym(libc_handle, "select");
    //pvfs_ops.FD_CLR = dlsym(libc_handle, "FD_CLR");
    //pvfs_ops.FD_ISSET = dlsym(libc_handle, "FD_ISSET");
    //pvfs_ops.FD_SET = dlsym(libc_handle, "FD_SET");
    //pvfs_ops.FD_ZERO = dlsym(libc_handle, "FD_ZERO");
    //pvfs_ops.pselect = dlsym(libc_handle, "pselect");
    pvfs_ops.send = dlsym(libc_handle, "send");
    pvfs_ops.sendto = dlsym(libc_handle, "sendto");
    pvfs_ops.sendmsg = dlsym(libc_handle, "sendmsg");
    pvfs_ops.shutdown = dlsym(libc_handle, "shutdown");
    pvfs_ops.socketpair = dlsym(libc_handle, "socketpair");
    pvfs_ops.pipe = dlsym(libc_handle, "pipe");

    /* should have been previously opened */
    /* this decrements the reference count */
    if (libc_handle != RTLD_NEXT)
    {
        dlclose(libc_handle);
    }
}

/*
 * runs on exit to do any cleanup
 */
static void usrint_cleanup(void)
{
    /* later check for an error that might want us */
    /* to keep this - for now it is empty */
    glibc_ops.unlink(logfilepath);
    /* cache cleanup? */
#if 0
    if (ucache_enabled)
    {
        ucache_finalize();
    }
#endif
    PVFS_sys_finalize();
}

#if PVFS_UCACHE_ENABLE
/*
 * access function to see if cache is currently enabled
 * only used by code ouside of this module
 */
int pvfs_ucache_enabled(void)
{
    return ucache_enabled;
}
#endif

void pvfs_sys_init_doit(void);

int pvfs_sys_init(void)
{
    static int pvfs_initializing_flag = 0;
    static int pvfs_lib_lock_initialized = 0; /* recursive lock init flag */
    static int pvfs_lib_init_flag = 0;

    int rc = 0;

    /* Mutex protecting initialization of recursive mutex */
    static gen_mutex_t mutex_mutex = GEN_MUTEX_INITIALIZER;
    /* The recursive mutex */
    static pthread_mutex_t rec_mutex;

    if(pvfs_lib_init_flag)
        return 0;

    if(!pvfs_lib_lock_initialized)
    {
        rc = gen_mutex_lock(&mutex_mutex);
        if(!pvfs_lib_lock_initialized)
        {
            //init recursive mutex
            pthread_mutexattr_t rec_attr;
            rc = pthread_mutexattr_init(&rec_attr);
            rc = pthread_mutexattr_settype(&rec_attr, PTHREAD_MUTEX_RECURSIVE);
            rc = pthread_mutex_init(&rec_mutex, &rec_attr);
            rc = pthread_mutexattr_destroy(&rec_attr);
            pvfs_lib_lock_initialized = 1;
        }
        rc = gen_mutex_unlock(&mutex_mutex);
    }

    rc = pthread_mutex_lock(&rec_mutex);
    if(pvfs_lib_init_flag || pvfs_initializing_flag)
    {
        rc = pthread_mutex_unlock(&rec_mutex);
        return 1;
    }

    /* set this to prevent pvfs_sys_init from running recursively (indirect) */
    pvfs_initializing_flag = 1;

    //Perform Init
    pvfs_sys_init_doit();
    pvfs_initializing_flag = 0;
    pvfs_lib_init_flag = 1;
    rc = pthread_mutex_unlock(&rec_mutex);
    return 0;
}

/* 
 * Perform PVFS initialization tasks
 */
void pvfs_sys_init_doit(void) {
    struct rlimit rl; 
	int rc;

    /* this allows system calls to run */
    load_glibc();
    PINT_initrand();

    /* if this fails not much we can do about it */
    atexit(usrint_cleanup);

    /* set up current working dir */
    pvfs_cwd_init();

	rc = getrlimit(RLIMIT_NOFILE, &rl); 
	/* need to check for "INFINITY" */

    /* set up descriptor table */
	descriptor_table_size = rl.rlim_max;
	descriptor_table =
			(pvfs_descriptor **)malloc(sizeof(pvfs_descriptor *) *
			descriptor_table_size);
    if (!descriptor_table)
    {
        perror("failed to malloc descriptor table");
        exit(-1);
    }
	memset(descriptor_table, 0,
			(sizeof(pvfs_descriptor *) * descriptor_table_size));
    descriptor_table[0] = &pvfs_stdin;
    gen_mutex_init(&pvfs_stdin.lock);
    gen_mutex_init(&pvfs_stdin.s->lock);
    descriptor_table[1] = &pvfs_stdout;
    gen_mutex_init(&pvfs_stdout.lock);
    gen_mutex_init(&pvfs_stdin.s->lock);
    descriptor_table[2] = &pvfs_stderr;
    gen_mutex_init(&pvfs_stderr.lock);
    gen_mutex_init(&pvfs_stdin.s->lock);
    descriptor_table_count = PREALLOC;

    /* open log file */
    /* we dupe this FD to get FD's for pvfs files */
    memset(logfilepath, 0, sizeof(logfilepath));
    snprintf(logfilepath, 25, "/tmp/pvfsuid-%05d.log", (int)(getuid()));
    logfile = glibc_ops.open(logfilepath, O_RDWR|O_CREAT, 0600);
    if (logfile < 0)
    {
        perror("failed in pvfs_sys_init");
        exit(-1);
    }

	/* initalize PVFS */ 
    /* this is very complex so most stuff needs to work
     * before we do this
     */
	PVFS_util_init_defaults(); 
    if (errno == EINPROGRESS)
    {
        errno = 0;
    }

    /* call other initialization routines */

    /* lib calls should never print user error messages */
    /* user can do that with return codes */
    PVFS_perror_gossip_silent();

#if PVFS_UCACHE_ENABLE
    //gossip_enable_file(UCACHE_LOG_FILE, "a");
    //gossip_enable_stderr();

    /* ucache initialization - assumes shared memory previously 
     * aquired (using ucache daemon) 
     */
    rc = ucache_initialize();
    if (rc < 0)
    {
        /* ucache failed to initialize, so continue without it */
        /* Write a warning message in the ucache.log letting programmer know */
        ucache_enabled = 0;

        /* Enable the writing of the error message and write the message to file. */
        //gossip_set_debug_mask(1, GOSSIP_UCACHE_DEBUG);
        //gossip_debug(GOSSIP_UCACHE_DEBUG, 
        //    "WARNING: client caching configured enabled but couldn't inizialize\n");
    }
#endif

#ifdef PVFS_AIO_ENABLE
   /* initialize aio interface */
   aiocommon_init();
#endif
}

int pvfs_descriptor_table_size(void)
{
    return descriptor_table_size;
}

/*
 * Allocate a new pvfs_descriptor
 * initialize fsops to the given set
 */
 pvfs_descriptor *pvfs_alloc_descriptor(posix_ops *fsops,
                                        int fd, 
                                        PVFS_object_ref *file_ref,
                                        int use_cache)
 {
    int newfd, flags = 0; 
    pvfs_descriptor *pd;
    /* insure one thread at a time is in */
    /* fd setup section */
    static gen_mutex_t lock = GEN_MUTEX_INITIALIZER;

    pvfs_sys_init();
    debug("pvfs_alloc_descriptor called with %d\n", fd);
    if (fsops == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
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

    gen_mutex_lock(&lock);
    {
        if (descriptor_table[newfd] != NULL)
        {
            errno = EINVAL;
            gen_mutex_unlock(&lock);
            return NULL;
        }

        /* allocate new descriptor */
	    descriptor_table_count++;
        pd = (pvfs_descriptor *)malloc(sizeof(pvfs_descriptor));
    
        if (!pd)
        {
            gen_mutex_unlock(&lock);
            return NULL;
        }
        memset(pd, 0, sizeof(pvfs_descriptor));
    
        gen_mutex_init(&pd->lock);
        gen_mutex_lock(&pd->lock);
        descriptor_table[newfd] = pd;
    }
    gen_mutex_unlock(&lock);

    pd->s = (pvfs_descriptor_status *)malloc(sizeof(pvfs_descriptor_status));
    if (!pd->s)
    {
        free(pd);
        return NULL;
    }
    memset(pd->s, 0, sizeof(pvfs_descriptor_status));

    gen_mutex_init(&pd->s->lock);
    gen_mutex_lock(&pd->s->lock);

	/* fill in descriptor */
	pd->is_in_use = PVFS_FS;
	pd->fd = newfd;
	pd->true_fd = newfd;
	pd->fdflags = 0;

    /*
    if (!use_cache)
    {
	    pd->fdflags |= PVFS_FD_NOCACHE;
    }
    */
	pd->s->dup_cnt = 1;
	pd->s->fsops = fsops;
    if (file_ref)
    {
	    pd->s->pvfs_ref.fs_id = file_ref->fs_id;
	    pd->s->pvfs_ref.handle = file_ref->handle;
    }
    else
    {
        /* if this is not a PVFS file then the file_ref will be NULL */
	    pd->s->pvfs_ref.fs_id = 0;
	    pd->s->pvfs_ref.handle = 0LL;
    }

    pd->s->flags = flags;
    pd->s->mode = 0; /* this should be filled in by caller */
    pd->s->file_pointer = 0;
    pd->s->token = 0;
    pd->s->dpath = NULL;
    pd->s->fent = NULL; /* not caching if left NULL */

#if PVFS_UCACHE_ENABLE
    if (ucache_enabled /* && use_cache*/ )
    {
        /* File reference won't always be passed in */
        if(file_ref != NULL)
        {
            /* We have the file identifiers
             * so insert file info into ucache
             * this fills in mtbl
             */
            ucache_open_file(&(file_ref->fs_id),
                             &(file_ref->handle), 
                             &(pd->s->fent));
        }
    }
#endif /* PVFS_UCACHE_ENABLE */

    /* NEW PD IS STILL LOCKED */
    debug("\tpvfs_alloc_descriptor returns with %d\n", pd->fd);
    return pd;
}

/*
 * Function for duplicating a descriptor - used in dup and dup2 calls
 */
int pvfs_dup_descriptor(int oldfd, int newfd)
{
    int rc = 0;
    pvfs_descriptor *pd;

    debug("pvfs_dup_descriptor: called with %d\n", oldfd);
    pvfs_sys_init();
    if (oldfd < 0 || oldfd >= descriptor_table_size)
    {
        errno = EBADF;
        return -1;
    }
    if (newfd == -1) /* dup */
    {
        newfd = glibc_ops.dup(logfile);
        if (newfd < 0)
        {
            debug("\npvfs_dup_descriptor: returns with %d\n", newfd);
            return newfd;
        }
    }
    else /* dup2 */
    {
        /* see if requested fd is in use */
        if (descriptor_table[newfd] != NULL)
        {
            /* check for special case */
            if (newfd == oldfd)
            {
                debug("\tpvfs_dup_descriptor: returns with %d\n", oldfd);
                return oldfd;
            }
            /* close old file in new slot */
            rc = pvfs_free_descriptor(newfd);
            if (rc < 0)
            {
                debug("\tpvfs_dup_descriptor: returns with %d\n", rc);
                return rc;
            }
        }
        /* continuing with dup2 */
        rc = glibc_ops.dup2(oldfd, newfd);
        if (rc < 0)
        {
            debug("\tpvfs_dup_descriptor: returns with %d\n", rc);
            return rc;
        }
    }
    /* new set up new pvfs_descfriptor */
	descriptor_table_count++;
    pd = (pvfs_descriptor *)malloc(sizeof(pvfs_descriptor));
    if (!pd)
    {
        debug("\tpvfs_dup_descriptor: returns with %d\n", -1);
        return -1;
    }
    memset(pd, 0, sizeof(pvfs_descriptor));
    gen_mutex_init(&pd->lock);
    gen_mutex_lock(&pd->lock);
    descriptor_table[newfd] = pd;

	pd->is_in_use = PVFS_FS;
	pd->fd = newfd;
	pd->true_fd = newfd;
	pd->fdflags = 0;
    /* share the pvfs_desdriptor_status info */
    pd->s = descriptor_table[oldfd]->s;
    gen_mutex_lock(&pd->s->lock);
    pd->s->dup_cnt++;
    gen_mutex_unlock(&pd->s->lock);
    gen_mutex_unlock(&pd->lock);
    debug("\tpvfs_dup_descriptor: returns with %d\n", newfd);
    return newfd;
}

/*
 * Return a pointer to the pvfs_descriptor for the file descriptor or null
 * if there is no entry for the given file descriptor
 * should probably be inline if we can get at static table that way
 */
pvfs_descriptor *pvfs_find_descriptor(int fd)
{
    pvfs_descriptor *pd = NULL;

    pvfs_sys_init();
    if (fd < 0 || fd >= descriptor_table_size)
    {
        errno = EBADF;
        return NULL;
    }
    pd = descriptor_table[fd];
    if (!pd)
    {
        int flags = 0;
        /* see if glibc opened this file without our knowing */
        flags = glibc_ops.fcntl(fd, F_GETFL);
        if (flags == -1)
        {
            /* apparently not */
            return NULL;
        }
        /* allocate a descriptor */
	    descriptor_table_count++;
        pd = (pvfs_descriptor *)malloc(sizeof(pvfs_descriptor));
        if (!pd)
        {
            return NULL;
        }
        memset(pd, 0, sizeof(pvfs_descriptor));
        gen_mutex_init(&pd->lock);
        gen_mutex_lock(&pd->lock);
        descriptor_table[fd] = pd;
        debug("pvfs_find_descriptor: implicit alloc of descriptor %d\n", fd);

        pd->s =
             (pvfs_descriptor_status *)malloc(sizeof(pvfs_descriptor_status));
        if (!pd->s)
        {
            free(pd);
            return NULL;
        }
        memset(pd->s, 0, sizeof(pvfs_descriptor_status));
        gen_mutex_init(&pd->s->lock);
    
	    /* fill in descriptor */
	    pd->is_in_use = PVFS_FS;
	    pd->fd = fd;
	    pd->true_fd = fd;
	    pd->fdflags = 0;
	    pd->s->dup_cnt = 1;
	    pd->s->fsops = &glibc_ops;
	    pd->s->pvfs_ref.fs_id = 0;
	    pd->s->pvfs_ref.handle = 0LL;
	    pd->s->flags = flags;
	    pd->s->mode = 0;
	    pd->s->file_pointer = 0;
	    pd->s->token = 0;
	    pd->s->dpath = NULL;
        pd->s->fent = NULL; /* not caching if left NULL */
    }
    else
    {
        /* locks here prevent a thread from getting */
        /* a pd that is not finish being allocated yet */
        gen_mutex_lock(&pd->lock);
        if (pd->is_in_use != PVFS_FS)
        {
            errno = EBADF;
            gen_mutex_unlock(&pd->lock);
            return NULL;
        }
    }
    gen_mutex_unlock(&pd->lock);
	return pd;
}

int pvfs_free_descriptor(int fd)
{
    int dup_cnt;
    pvfs_descriptor *pd = NULL;
    debug("pvfs_free_descriptor called with %d\n", fd);

    pd = pvfs_find_descriptor(fd);
    if (pd == NULL)
    {
        debug("\tpvfs_free_descriptor returns %d\n", -1);
        return -1;
    }

	/* clear out table entry */
	descriptor_table[fd] = NULL;
    glibc_ops.close(fd);

	/* keep up with used descriptors */
	descriptor_table_count--;

    /* check if last copy */
    gen_mutex_lock(&pd->s->lock);
    dup_cnt = --(pd->s->dup_cnt);
    gen_mutex_unlock(&pd->s->lock);
    if (dup_cnt <= 0)
    {
        if (pd->s->dpath)
        {
            free(pd->s->dpath);
        }

#if PVFS_UCACHE_ENABLE
        if (pd->s->fent)
        {
            int rc = 0;
            rc = ucache_close_file(pd->s->fent);
            if(rc == -1)
            {
                debug("\tpvfs_free_descriptor returns %d\n", rc);
                return rc;
            }
        }
#endif /* PVFS_UCACHE_ENABLE */

	    /* free descriptor status - wipe memory first */
	    memset(pd->s, 0, sizeof(pvfs_descriptor_status));

        /* first 3 descriptors not malloc'd */
        if (fd > 2)
        {
	        free(pd->s);
        }
    }
	/* free descriptor - wipe memory first */
	memset(pd, 0, sizeof(pvfs_descriptor));
    /* first 3 descriptors not malloc'd */
    if (fd > 2)
    {
	    free(pd);
    }

    debug("\tpvfs_free_descriptor returns %d\n", 0);
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
