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
#include <usrint.h>
#include <linux/dirent.h>
#include <posix-ops.h>
#include <openfile-util.h>

#define PREALLOC 3
static int descriptor_table_count = 0; 
static int descriptor_table_size = 0; 
static int next_descriptor = 0; 
static pvfs_descriptor **descriptor_table; 
static char rstate[256];  /* used for random number generation */

posix_ops glibc_ops;

pvfs_descriptor pvfs_stdin =
{
    .fd = 0,
    .fsops = &glibc_ops,
    .posix_fd = STDIN_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_RDONLY,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .is_in_use = PVFS_FS
};

pvfs_descriptor pvfs_stdout =
{
    .fd = 1,
    .fsops = &glibc_ops,
    .posix_fd = STDOUT_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .is_in_use = PVFS_FS
};

pvfs_descriptor pvfs_stderr =
{
    .fd = 2,
    .fsops = &glibc_ops,
    .posix_fd = STDERR_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .mode = 0,
    .file_pointer = 0,
    .token = 0,
    .is_in_use = PVFS_FS
};

void load_glibc(void)
{ 
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
/*  glibc_ops.write64 = dlsym(RTLD_NEXT, "write64"); */
    glibc_ops.lseek = dlsym(RTLD_NEXT, "lseek");
    glibc_ops.lseek64 = dlsym(RTLD_NEXT, "lseek64");
    glibc_ops.truncate = dlsym(RTLD_NEXT, "truncate");
    glibc_ops.truncate64 = dlsym(RTLD_NEXT, "truncate64");
    glibc_ops.ftruncate = dlsym(RTLD_NEXT, "ftruncate");
    glibc_ops.ftruncate64 = dlsym(RTLD_NEXT, "ftruncate64");
    glibc_ops.close = dlsym(RTLD_NEXT, "close");
    glibc_ops.flush = dlsym(RTLD_NEXT, "flush");
    glibc_ops.stat = dlsym(RTLD_NEXT, "stat");
    glibc_ops.stat64 = dlsym(RTLD_NEXT, "stat64");
    glibc_ops.fstat = dlsym(RTLD_NEXT, "fstat");
    glibc_ops.fstat64 = dlsym(RTLD_NEXT, "fstat64");
    glibc_ops.fstatat = dlsym(RTLD_NEXT, "fstatat");
    glibc_ops.fstatat64 = dlsym(RTLD_NEXT, "fstatat64");
    glibc_ops.lstat = dlsym(RTLD_NEXT, "lstat");
    glibc_ops.lstat64 = dlsym(RTLD_NEXT, "lstat64");
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
    glibc_ops.readdir = dlsym(RTLD_NEXT, "readdir");
    glibc_ops.getdents = dlsym(RTLD_NEXT, "getdents");
    glibc_ops.access = dlsym(RTLD_NEXT, "access");
    glibc_ops.faccessat = dlsym(RTLD_NEXT, "faccessat");
    glibc_ops.flock = dlsym(RTLD_NEXT, "flock");
    glibc_ops.fcntl = dlsym(RTLD_NEXT, "fcntl");
    glibc_ops.sync = dlsym(RTLD_NEXT, "sync");
    glibc_ops.fsync = dlsym(RTLD_NEXT, "fsync");
    glibc_ops.fdatasync = dlsym(RTLD_NEXT, "fdatasync");
    glibc_ops.umask = dlsym(RTLD_NEXT, "umask");
    glibc_ops.getumask = dlsym(RTLD_NEXT, "getumask");
    glibc_ops.getdtablesize = dlsym(RTLD_NEXT, "getdtablesize");
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

    /* this allows system calls to run */
    load_glibc();

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
	next_descriptor = PREALLOC;

	/* initalize PVFS */ 
	PVFS_util_init_defaults(); 

    /* call other initialization routines */
    PINT_initrand();
    PVFS_perror_gossip_silent();
}

int pvfs_descriptor_table_size(void)
{
    return descriptor_table_size;
}

/*
 * Allocate a new pvfs_descriptor
 * initialize fsops to the given set
 */
 pvfs_descriptor *pvfs_alloc_descriptor(posix_ops *fsops)
 {
 	int i; 
    if (fsops == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    pvfs_sys_init();

    /* table should be initialized now - check for available slot */
	if (descriptor_table_count == (descriptor_table_size - PREALLOC))
	{
        errno = ENOMEM;
		return NULL;
	}

   /* find next empty slot in table */
	for (i = next_descriptor; descriptor_table[i];
			i = (i == descriptor_table_size - 1) ? PREALLOC : i + 1);

   /* found a slot */
	descriptor_table[i] = malloc(sizeof(pvfs_descriptor));
	if (descriptor_table[i] == NULL)
	{
		return NULL;
	}
	next_descriptor = ((i == descriptor_table_size - 1) ? PREALLOC : i + 1);
	descriptor_table_count++;

	/* fill in descriptor */
	descriptor_table[i]->fd = i;
	descriptor_table[i]->dup_cnt = 1;
	descriptor_table[i]->fsops = fsops;
	descriptor_table[i]->posix_fd = i;
	descriptor_table[i]->pvfs_ref.fs_id = 0;
	descriptor_table[i]->pvfs_ref.handle = 0;
	descriptor_table[i]->flags = 0;
	descriptor_table[i]->mode = 0;
	descriptor_table[i]->file_pointer = 0;
	descriptor_table[i]->token = 0;
	descriptor_table[i]->is_in_use = PVFS_FS;

    return descriptor_table[i];
}

/*
 * Function for dupliating a descriptor - used in dup and dup2 calls
 */
int pvfs_dup_descriptor(int oldfd, int newfd)
{
    if (oldfd < 0 || oldfd >= descriptor_table_size)
    {
        errno = EBADF;
        return -1;
    }
    if (newfd == -1)
    {
        /* find next empty slot in table */
        for (newfd = next_descriptor; descriptor_table[newfd];
            newfd = (newfd == descriptor_table_size-1) ? PREALLOC : newfd++);
    }
    else
    {
        if (descriptor_table[newfd] != NULL)
        {
            /* close old file in new slot */
            pvfs_close(newfd);
        }
    }
    descriptor_table[newfd] = descriptor_table[oldfd];
    descriptor_table[newfd]->dup_cnt++;
	descriptor_table_count++;
}

/*
 * Return a pointer to the pvfs_descriptor for the file descriptor or null
 * if there is no entry for the given file descriptor
 * should probably be inline if we can get at static table that way
 */
pvfs_descriptor *pvfs_find_descriptor(int fd)
{
    if (fd < 0 || fd >= descriptor_table_size)
    {
        errno = EBADF;
        return NULL;
    }
	return descriptor_table[fd];
}

int pvfs_free_descriptor(int fd)
{
    pvfs_descriptor *pd;

    if (fd < 0 || fd >= descriptor_table_size)
    {
        errno = EBADF;
        return -1;
    }
    pd = descriptor_table[fd];

	/* clear out table entry */
	descriptor_table[fd] = NULL;

	/* keep up with used descriptors */
	descriptor_table_count--;

    /* check if last copy */
    if (--(pd->dup_cnt) <= 0)
    {
	    /* free descriptor - wipe memory first */
	    memset(pd, 0, sizeof(pvfs_descriptor));
	    free(pd);
    }

	return 0;
}

/* 
 * takes a path that is relative to the working dir and
 * expands it all the way to the root
 */
char * pvfs_qualify_path(const char *path)
{
    char *rc = NULL;
    int i = 1;
    int cdsz;
    int psz;
    char *curdir;
    char *newpath;

    if(path[0] != '/')
    {
        /* loop until our temp buffer is big enough for the */
        /* current directory */
        do
        {
            if (i > 1)
            {
                free(curdir);
            }
            curdir = (char *)malloc(i * 256);
            if (curdir == NULL)
            {
                return NULL;
            }
            rc = getcwd(curdir, i * 256);
            i++;
        } while ((rc == NULL) && (errno == ERANGE));
        if (rc == NULL)
        {
            /* some other error, bail out */
            free(curdir);
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
        newpath = (char *)path;
    return newpath;
}

/**
 * Determines if a path is part of a PVFS Filesystem 
 *
 * returns 1 if PVFS 0 otherwise
 */

int is_pvfs_path(const char *path)
{
    struct statfs file_system;
    char *directory = NULL ;
    char *str;

    pvfs_sys_init();
    memset(&file_system, 0, sizeof(file_system));
    /* add current working dir to front of relative path */
    /* and copy to temp string working area */
    if(path[0] != '/')
    {
        directory = getcwd(NULL, 0);
        str = malloc((strlen(directory) + 1) * sizeof(char));
        if(!str)
        {
            return -1;
        }
        strcpy(str, directory);
        free(directory);
    }
    else
    {
        str = malloc((strlen(path) + 1) * sizeof(char));
        if(!str)
        {
            return -1;
        }
        strcpy(str, path);
    }

    /* lop off the last segment of the path */
    int count;
    for(count = strlen(str) -2; count > 0; count--)
    {
        if(str[count] == '/')
        {
            str[count] = '\0';
            break;
        }
    }
    /* this must call standard glibc statfs */
    glibc_ops.statfs(str, &file_system);
    free(str);
    if(file_system.f_type == PVFS_FS)
    {
        return 1;
    }
    else
    {
        /* not PVFS assume the kernel can handle it */
        return 0;
    }
}

void pvfs_debug(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
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
    length = strnlen(path, PVFS_NAME_MAX);
    if (length == PVFS_NAME_MAX)
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
