/* we will keep a copy and keep one in the environment */
/* (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - low level calls to system interface
 */
#define USRINT_SOURCE 1
#include "usrint.h"
#include "posix-ops.h"
#include "openfile-util.h"
#include "iocommon.h"
#if PVFS_UCACHE_ENABLE
#include "ucache.h"
#endif
#include <errno.h>

/* Functions in this file generally define a label errorout
 * for cleanup before exit and return an int rc which is -1
 * on error with the error code in errno, 0 on success.
 * IOCOMMON_RETURN_ERR checks a return code from a function
 * returns the same protocol and goto errorout: if less than 0
 * IOCOMMON_CHECK_ERR assumes the return code contains the
 * negative of the error code as encoded by PVFS sysint
 * functions and decodes these before jumping to errorout.
 * PVFS sysint calls always return error codes in the return
 * value, but system calls inside them might set errno to
 * a value that may or may not have meaning for the programmer
 * calling this library.  Steps are taken to ensure errno
 * is not modified unless the code in this lib wants to
 * modify it.  CHECK_ERR should be called after each sysint
 * call to correctly pass error codes.
 */
extern PVFS_error PINT_errno_mapping[];
#define IOCOMMON_RETURN_ERR(rc)                                 \
do {                                                            \
    if ((rc) < 0)                                               \
    {                                                           \
        goto errorout;                                          \
    }                                                           \
} while (0)

#define IOCOMMON_CHECK_ERR(rc)                                  \
do {                                                            \
    errno = orig_errno;                                         \
    if ((rc) < 0)                                               \
    {                                                           \
        if (IS_PVFS_NON_ERRNO_ERROR(-(rc)))                     \
        {                                                       \
            pvfs_errno = -rc;                                   \
            errno = EIO;                                        \
        }                                                       \
        else if (IS_PVFS_ERROR(-(rc)))                          \
        {                                                       \
            errno = PINT_errno_mapping[(-(rc)) & 0x7f];         \
        }                                                       \
        rc = -1;                                                \
        goto errorout;                                          \
    }                                                           \
} while (0)

/** this is a global analog of errno for pvfs specific
 *  errors errno is set to EIO and this si set to the
 *  original code 
 */
int pvfs_errno;

void iocommon_cred(PVFS_credentials **credentials)
{
    static PVFS_credentials creds_buf;
    static int cred_init = 0;

    if(!cred_init)
    {
        memset(&creds_buf, 0, sizeof(creds_buf));
        creds_buf.uid = getuid();
        creds_buf.gid = getgid();
        cred_init = 1;
    }

    *credentials = &creds_buf;
}

int iocommon_fsync(pvfs_descriptor *pd)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;

    pvfs_sys_init();
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    iocommon_cred(&credentials); 
#if PVFS_UCACHE_ENABLE
    if (pvfs_ucache_enabled())
    {
        ucache_flush(pd);
    }
#endif
    errno = 0;
    rc = PVFS_sys_flush(pd->s->pvfs_ref, credentials, PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

/**
 * Find the PVFS handle to an object (file, dir sym) 
 * assumes an absoluate path
 */
int iocommon_lookup_absolute(const char *abs_path,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size)
{
    int rc = 0;
    int orig_errno = errno;
    char pvfs_path[PVFS_PATH_MAX];
    PVFS_fs_id lookup_fs_id;
    PVFS_credentials *credentials;
    PVFS_sysresp_lookup resp_lookup;

    /* Initialize any variables */
    memset(&resp_lookup, 0, sizeof(resp_lookup));

    pvfs_sys_init();
    iocommon_cred(&credentials);

    /* Determine the fs_id and pvfs_path */
    errno = 0;
    rc = PVFS_util_resolve(abs_path, &lookup_fs_id, pvfs_path, PVFS_PATH_MAX);
    if (rc < 0)
    {
        if (rc == -PVFS_ENOENT)
        {
            errno = ESTALE; /* this signals open that resolve failed */
            rc = -1;
            goto errorout;
        }
        IOCOMMON_CHECK_ERR(rc);
    }

    /* set up buffer to return partially looked up path */
    /* in failure.  This is most likely a non-PVFS path */

    /* Set up error path */
    if (error_path)
    {
        memset(error_path, 0, error_path_size);
        resp_lookup.error_path = error_path;
        resp_lookup.error_path_size = error_path_size;
    }
    else
    {
        resp_lookup.error_path = NULL;
        resp_lookup.error_path_size = 0;
    }

    errno = 0;
    rc = PVFS_sys_lookup(lookup_fs_id, pvfs_path,
                         credentials, &resp_lookup,
                         PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    IOCOMMON_CHECK_ERR(rc);
    *ref = resp_lookup.ref;

errorout:
    return rc;
}

/**
 * Lookup a file via the PVFS system interface
 *
 * Assumes we have already looked up part of the path
 * POSIX assumes we can handle at least 1024 char paths
 * and potentially 4096 char paths (depending on which
 * include file you look at).  PVFS cannot deal with more
 * than 255 chars at a time so we must break long paths
 * into pieces and do multiple relative lookups
 */
int iocommon_lookup_relative(const char *rel_path,
                             PVFS_object_ref parent_ref, /* by value */
                             int follow_links,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_object_ref current_seg_ref;
    char current_seg_path[PVFS_NAME_MAX];
    char *cur, *last, *start;
    PVFS_credentials *credentials;
    PVFS_sysresp_lookup resp_lookup;

    /* Initialize any variables */
    pvfs_sys_init();
    memset(&resp_lookup, 0, sizeof(resp_lookup));

    /* Set credentials */
    iocommon_cred(&credentials);

    /* Set up error path */
    if (error_path)
    {
        memset(error_path, 0, error_path_size);
        resp_lookup.error_path = error_path;
        resp_lookup.error_path_size = error_path_size;
    }
    else
    {
        resp_lookup.error_path = NULL;
        resp_lookup.error_path_size = 0;
    }

    current_seg_ref = parent_ref;
    cur = (char *)rel_path;
    last = (char *)rel_path;
    start = (char *)rel_path;

    /* loop over segments of the path with max PVFS_NAME_MAX chars */
    while(*cur)
    {
        /* loop over chars to find a complete path segment */
        /* that is no longer than PVFS_NAME_MAX chars */
        while(*cur)
        {
            /* find next path seperator / */
            /* cur either points to a slash */
            /* or the first char of the path */
            /* there must be at least one */
            /* so n either case increment it first */
            for(cur++; *cur && *cur != '/'; cur++);
            if (cur - start > PVFS_NAME_MAX-1)
            {
                /* we over-shot the limit go back to last */
                cur = last;
                if (cur == start)
                {
                    /* single segment larger than PVFS_NAME_MAX */
                    errno = ENAMETOOLONG;
                    rc = -1;
                    goto errorout;
                }
                break;
            }
            else
            {
                /* set up to add the next path segment */
                last = cur;
            }
        }
        memset(current_seg_path, 0, PVFS_NAME_MAX);
        strncpy(current_seg_path, start, (cur - start) + 1);
        start = cur;
        last = cur;

        /* Contact server */
        errno = 0;
        rc = PVFS_sys_ref_lookup(parent_ref.fs_id,
                                current_seg_path,
                                current_seg_ref,
                                credentials,
                                &resp_lookup,
                                follow_links,
                                PVFS_HINT_NULL);
        IOCOMMON_CHECK_ERR(rc);
        if (*cur)
        {
            current_seg_ref = resp_lookup.ref;
        }
        else
        {
            *ref = resp_lookup.ref;
        }
    }

errorout:
    return rc;
}

/**
 * Create a file via the PVFS system interface
 */
int iocommon_create_file(const char *filename,
                         mode_t mode,
                         PVFS_hint file_creation_param,
                         PVFS_object_ref parent_ref,
                         PVFS_object_ref *ref )
{
    int rc = 0;
    int orig_errno = errno;
    mode_t mode_mask;
    mode_t user_mode;
    PVFS_sys_attr attr;
    PVFS_credentials *credentials;
    PVFS_sysresp_create resp_create;
    PVFS_sys_dist *dist = NULL;
    PVFS_sys_layout *layout = NULL;

    /* Initialize */
    pvfs_sys_init();
    memset(&attr, 0, sizeof(attr));
    memset(&resp_create, 0, sizeof(resp_create));

    /* this is not right - need to pull parameters out of hints */
    /* investigate PVFS hint mechanism */
#if 0
    if (file_creation_param.striping_unit > 0)
    {
        dist = PVFS_sys_dist_lookup("simple_stripe");
        if (PVFS_sys_dist_setparam(dist, "strip_size",
                                  &(file_creation_param.striping_unit)) < 0)
        {
            fprintf(stderr, "Error: failed to set striping_factor\n");
        }
    }
#endif

    attr.owner = geteuid();
    attr.group = getegid();
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;

#if 0
    if (file_creation_param.striping_factor > 0){
        attributes.dfile_count = file_creation_param.striping_factor;
        attributes.mask |= PVFS_ATTR_SYS_DFILE_COUNT;
    }
#endif

    /* Extract the users umask (and restore it to the original value) */
    mode_mask = umask(0);
    umask(mode_mask);
    user_mode = mode & ~mode_mask;

    /* Set file permissions */
    if (user_mode & S_IXOTH)
    {
        attr.perms |= PVFS_O_EXECUTE;
    }
    if (user_mode & S_IWOTH)
    {
        attr.perms |= PVFS_O_WRITE;
    }
    if (user_mode & S_IROTH)
    {
        attr.perms |= PVFS_O_READ;
    }
    if (user_mode & S_IXGRP)
    {
        attr.perms |= PVFS_G_EXECUTE;
    }
    if (user_mode & S_IWGRP)
    {
        attr.perms |= PVFS_G_WRITE;
    }
    if (user_mode & S_IRGRP)
    {
        attr.perms |= PVFS_G_READ;
    }
    if (user_mode & S_IXUSR)
    {
        attr.perms |= PVFS_U_EXECUTE;
    }
    if (user_mode & S_IWUSR)
    {
        attr.perms |= PVFS_U_WRITE;
    }
    if (user_mode & S_IRUSR)
    {
        attr.perms |= PVFS_U_READ;
    }

    /* Set credentials */
    iocommon_cred(&credentials);

    /* Contact server */
    errno = 0;
    rc = PVFS_sys_create((char*)filename,
                         parent_ref,
                         attr,
                         credentials,
                         dist,
                         &resp_create,
                         layout,
                         NULL);
    IOCOMMON_CHECK_ERR(rc);
    *ref = resp_create.ref;

errorout:
    if (dist)
    {
        PVFS_sys_dist_free(dist);
    }
    return rc;
}


/** pvfs_open implementation, return file info in fd 
 *  assumes path is fully qualified 
 *  if pdir is not NULL, it is the parent directory 
 */
pvfs_descriptor *iocommon_open(const char *path,
                               int flags,
                               PVFS_hint file_creation_param,
                               mode_t mode,
                               pvfs_descriptor *pdir)
{
    int rc = 0;
    int orig_errno = errno;
    int follow_link;
    char *directory = NULL;
    char *filename = NULL;
    char error_path[256];
    PVFS_object_ref file_ref;
    PVFS_object_ref parent_ref;
    pvfs_descriptor *pd = NULL; /* invalid pd until file is opened */
    PVFS_sysresp_getattr attributes_resp;
    PVFS_credentials *credentials;

    /* Initialize */
    memset(&file_ref, 0, sizeof(file_ref));
    memset(&parent_ref, 0, sizeof(parent_ref));
    memset(&attributes_resp, 0, sizeof(attributes_resp));
    memset(error_path, 0, sizeof(error_path));

    pvfs_sys_init();
    iocommon_cred(&credentials);

    /* Split the path into a directory and file */
    rc = split_pathname(path, 0, &directory, &filename);
    IOCOMMON_RETURN_ERR(rc);

    /* Check the flags to determine if links are followed */
    if (flags & O_NOFOLLOW)
    {
        follow_link = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    }
    else
    {
        follow_link = PVFS2_LOOKUP_LINK_FOLLOW;
    }

    /* Get reference for the parent directory */
    if (pdir == NULL)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(directory, &parent_ref, NULL, 0);
        if (rc < 0)
        {
            if (errno == ESTALE)
            {
                /* special case we are opening the root dir of PVFS */
                errno = 0;
                rc = iocommon_lookup_absolute(path, &file_ref, NULL, 0);
                /* in this case we don't need to look up anything else */
                /* jump right to found the file code */
                goto foundfile;
            }
            IOCOMMON_RETURN_ERR(rc);
        }
    }
    else
    {
        if (directory)
        {
            errno = 0;
            rc = iocommon_lookup_relative(directory,
                                          pdir->s->pvfs_ref,
                                          follow_link,
                                          &parent_ref,
                                          NULL,
                                          0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            parent_ref = pdir->s->pvfs_ref;
        }
    }

    /* An open procedure safe for multiprocessing */

    /* Attempt to find file */
    errno = 0;
    rc = iocommon_lookup_relative(filename,
                                  parent_ref,
                                  follow_link,
                                  &file_ref,
                                  error_path,
                                  sizeof(error_path));
foundfile:
    if ((rc == 0) && (flags & O_EXCL) && (flags & O_CREAT))
    {
        /* File was found but EXCLUSIVE so fail */
        rc = -1;
        errno = EEXIST;
        goto errorout;
    }
    if (rc < 0)
    {
        /* if an error code was returned */
        if (errno == EIO && pvfs_errno == PVFS_ENOTPVFS &&
            flags & O_NOTPVFS)
        {
            struct stat sbuf;
            /* try to open using glibc */
            rc = (*glibc_ops.open)(error_path, flags & 01777777, mode);
            IOCOMMON_RETURN_ERR(rc);
            pd = pvfs_alloc_descriptor(&glibc_ops, -1, NULL, 0);
            pd->is_in_use = PVFS_FS;    /* indicate fd is valid! */
            pd->true_fd = rc;
            pd->s->flags = flags;           /* open flags */
            fstat(rc, &sbuf);
            pd->s->mode = sbuf.st_mode;
            goto errorout; /* not really an error, but bailing out */
        }
        if (errno != ENOENT || !(flags & O_CREAT))
        {
            /* either file not found and no create flag */
            /* or some other error */
            goto errorout;
        }
        /* file not found but create flag */
        /* clear errno, it was not an error */
        errno = orig_errno;
        errno = 0;
        rc = iocommon_create_file(filename,
                                  mode,
                                  file_creation_param,
                                  parent_ref,
                                  &file_ref);
        if (rc < 0)
        {
            /* error on the create */
            if (errno != EEXIST)
            {
                goto errorout;
            }
            /* the file exists so must have been
             * created by a different process
             * just open it
             */
            errno = 0;
            rc = iocommon_lookup_relative(filename,
                                          parent_ref,
                                          follow_link,
                                          &file_ref,
                                          NULL,
                                          0);
            IOCOMMON_RETURN_ERR(rc);
        }
    }

    /* If we get here the file was created and/or opened */
    /* Translate the pvfs reference into a file descriptor */
    /* Set the file information */
    /* create fd object */
    pd = pvfs_alloc_descriptor(&pvfs_ops, -1, &file_ref, 0);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    pd->s->flags = flags;           /* open flags */
    pd->is_in_use = PVFS_FS;    /* indicate fd is valid! */

    /* Get the file's type information from its attributes */
    errno = 0;
    rc = PVFS_sys_getattr(pd->s->pvfs_ref,
                          PVFS_ATTR_SYS_ALL_NOHINT,
                          credentials,
                          &attributes_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);
    pd->s->mode = attributes_resp.attr.perms; /* this may change */

    if (attributes_resp.attr.objtype == PVFS_TYPE_METAFILE)
    {
        pd->s->mode |= S_IFREG;
    }
    if (attributes_resp.attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        pd->s->mode |= S_IFDIR;
        if (pdir)
        {
            pd->s->dpath = (char *)malloc(strlen(pdir->s->dpath) + strlen(path) + 2);
            strcpy(pd->s->dpath, pdir->s->dpath);
            strcat(pd->s->dpath, "/");
            strcat(pd->s->dpath, path);
        }
        else
        {
            pd->s->dpath = (char *)malloc(strlen(path) + 1);
            strcpy(pd->s->dpath, path);
        }
    }
    if (attributes_resp.attr.objtype == PVFS_TYPE_SYMLINK)
    {
        pd->s->mode |= S_IFLNK;
    }

    /* Truncate the file if neccesary */
    if (flags & O_TRUNC)
    {
        errno = 0;
        rc = PVFS_sys_truncate(file_ref, 0, credentials, NULL);
        IOCOMMON_CHECK_ERR(rc);
    }

    /* Move to the end of file if necessary */
    if (flags & O_APPEND)
    {
        rc = iocommon_lseek(pd, 0, 0, SEEK_END);
    }

errorout:

    /* Free directory and filename memory */
    if (directory)
    {
        free(directory);
    }
    if (filename)
    {
        free(filename);
    }
    if (rc < 0)
    {
        return NULL;
    }
    else
    {
        return pd;
    }
}

/**
 * Implementation of truncate via PVFS
 *
 */
int iocommon_truncate(PVFS_object_ref file_ref, off64_t length)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;

    pvfs_sys_init();
    iocommon_cred(&credentials);
    errno = 0;
    rc =  PVFS_sys_truncate(file_ref, length, credentials, NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

/**
 * Implementation of lseek for file and directory via PVFS
 *
 */
off64_t iocommon_lseek(pvfs_descriptor *pd, off64_t offset,
            PVFS_size unit_size, int whence)
{
    int rc = 0;
    int orig_errno = errno;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    switch(whence)
    {
        case SEEK_SET:
        {
            pd->s->file_pointer = (offset * unit_size);
            break;
        }
        case SEEK_CUR:
        {
            pd->s->file_pointer += (offset * unit_size);
            break;
        }
        case SEEK_END:
        {
            PVFS_credentials *credentials;
            PVFS_sysresp_getattr attributes_resp;

            memset(&attributes_resp, 0, sizeof(attributes_resp));
            iocommon_cred(&credentials);
            /* Get the file's size in bytes as the ending offset */
            errno = 0;
            rc = PVFS_sys_getattr(pd->s->pvfs_ref,
                                  PVFS_ATTR_SYS_SIZE,
                                  credentials,
                                  &attributes_resp,
                                  NULL);
            IOCOMMON_CHECK_ERR(rc);
            pd->s->file_pointer = attributes_resp.attr.size + (offset * unit_size);
            break;
        }
        default:
        {
            errno = EINVAL;
            goto errorout;
        }
    }
    /* if this is a directory adjust token, the hard way */
    if (S_ISDIR(pd->s->mode))
    {
        int dirent_no;
        PVFS_credentials *credentials;
        PVFS_sysresp_readdir readdir_resp;

        memset(&readdir_resp, 0, sizeof(readdir_resp));
        iocommon_cred(&credentials);
        dirent_no = pd->s->file_pointer / sizeof(PVFS_dirent);
        pd->s->file_pointer = dirent_no * sizeof(PVFS_dirent);
        pd->s->token = PVFS_READDIR_START;
        if(dirent_no)
        {
            errno = 0;
            rc = PVFS_sys_readdir(pd->s->pvfs_ref,
                                  pd->s->token,
                                  dirent_no,
                                  credentials,
                                  &readdir_resp,
                                  NULL);
            IOCOMMON_CHECK_ERR(rc);
            pd->s->token = readdir_resp.token;
            free(readdir_resp.dirent_array);
        }
    }
    return pd->s->file_pointer;

errorout:
    return -1;
}

/**
 * implements unlink and rmdir
 * 
 * dirflag indicates trying to remove a dir (rmdir)
 */
int iocommon_remove (const char *path,
                     PVFS_object_ref *pdir, 
                     int dirflag)
{
    int rc = 0;
    int orig_errno = errno;
    char *parentdir = NULL;
    char *file = NULL;
    PVFS_object_ref parent_ref, file_ref;
    PVFS_credentials *credentials;
    PVFS_sys_attr attr;

    /* Initialize */
    memset(&parent_ref, 0, sizeof(parent_ref));
    memset(&file_ref, 0, sizeof(file_ref));
    memset(&attr, 0, sizeof(attr));

    /* Initialize the system interface for this process */
    pvfs_sys_init();
    iocommon_cred(&credentials);

    rc = split_pathname(path, dirflag, &parentdir, &file);
    IOCOMMON_RETURN_ERR(rc);

    if (!pdir)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
            errno = 0;
            rc = iocommon_lookup_relative(parentdir, *pdir,
                            PVFS2_LOOKUP_LINK_FOLLOW, &parent_ref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        { 
            parent_ref = *pdir;
        }
    }

    /* need to verify this is a file or symlink */
    errno = 0;
    rc = iocommon_lookup_relative(file, parent_ref,
                PVFS2_LOOKUP_LINK_NO_FOLLOW, &file_ref, NULL, 0);
    IOCOMMON_RETURN_ERR(rc);

    errno = 0;
    rc = iocommon_getattr(file_ref, &attr, PVFS_ATTR_SYS_TYPE);
    IOCOMMON_RETURN_ERR(rc);

    if ((attr.objtype == PVFS_TYPE_DIRECTORY) && !dirflag)
    {
        errno = EISDIR;
        goto errorout;
    }
    else if ((attr.objtype != PVFS_TYPE_DIRECTORY) && dirflag)
    {
        errno = ENOTDIR;
        goto errorout;
    }

    /* should check to see if any process has file open */
    /* but at themoment we don't have a way to do that */
    errno = 0;
    rc = PVFS_sys_remove(file, parent_ref, credentials, PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    if (parentdir)
    {
        free(parentdir);
    }
    if (file)
    {
        free(file);
    }
    if (rc < 0)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

/**
 * wrapper for unlink
 */
int iocommon_unlink(const char *path,
                    PVFS_object_ref *pdir)
{
    return iocommon_remove(path, pdir, 0);
}

/**
 * wrapper for rmdir
 */
int iocommon_rmdir(const char *path,
                   PVFS_object_ref *pdir)
{
    return iocommon_remove(path, pdir, 1);
}

/** if dir(s) are NULL, assume name is absolute */
int iocommon_rename(PVFS_object_ref *oldpdir, const char *oldpath,
                    PVFS_object_ref *newpdir, const char *newpath)
{
    int rc = 0;
    int orig_errno = errno;
    char *olddir = NULL, *newdir = NULL, *oldname = NULL, *newname = NULL;
    PVFS_object_ref oldref, newref;
    PVFS_credentials *creds;
    PVFS_hint hints = PVFS_HINT_NULL;

    /* Initialize */
    memset(&oldref, 0, sizeof(oldref));
    memset(&newref, 0, sizeof(newref));

    iocommon_cred(&creds);
    rc = split_pathname(oldpath, 0, &olddir, &oldname);
    IOCOMMON_RETURN_ERR(rc);

    if (!oldpdir)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(olddir, &oldref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (olddir)
        {
            errno = 0;
            rc = iocommon_lookup_relative(olddir, *oldpdir, 
                                PVFS2_LOOKUP_LINK_FOLLOW, &oldref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            oldref = *oldpdir;
        }
    }
    rc = split_pathname(newpath, 0, &newdir, &newname);
    IOCOMMON_RETURN_ERR(rc);
    if (!newpdir)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(newdir, &newref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (newdir)
        {
            errno = 0;
            rc = iocommon_lookup_relative(newdir, *newpdir,
                            PVFS2_LOOKUP_LINK_FOLLOW, &newref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            newref = *newpdir;
        }
    }
    errno = 0;
    rc = PVFS_sys_rename(oldname, oldref, newname, newref, creds, hints);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    if (olddir)
    {
        free(olddir);
    }
    if (oldname)
    {
        free(oldname);
    }
    if (newdir)
    {
        free(newdir);
    }
    if (newname)
    {
        free(newname);
    }
    return rc;
}

#if PVFS_UCACHE_ENABLE
/** TODO
 * 1: read, read from user cache and write to user mem
 * 2: write, read from user mem and write to user cache
 */
static uint32_t place_data(enum PVFS_io_type which,
                           const uint64_t block, 
                           const struct iovec *vector, 
                           int *iovec_ndx,
                           unsigned char *scratch, 
                           void **scratch_ptr,
                           uint64_t *scratch_left)
{
    const uint64_t block_size = CACHE_BLOCK_SIZE_K * 1024;
    /* Bytes of block remaining to be read/written */
    uint64_t left = CACHE_BLOCK_SIZE_K * 1024;    
    void *user_mem = 0; /* Where to read/write */
    uint64_t user_mem_size = 0; /* How much to read/write */

    /* Continue read/writing strips of data until the whole block completed */
    while(left != 0)
    {
        /* Do we need to use the scratch_ptr or a fresh segment */
        if(*scratch)
        {
            /* Use a previously used buffer that wasn't quite filled by the 
             * previous cache block.
             */
            user_mem = *scratch_ptr;
            user_mem_size = *scratch_left;
            *scratch = 0;
        }
        else
        {
            user_mem = vector[*iovec_ndx].iov_base;
            user_mem_size = vector[*iovec_ndx].iov_len;
        }

        /* Will this transfer complete the block but not the user mem segment */
        if(user_mem_size > left)
        {
            /* Save a reference to where we left off with this segment*/
            *scratch_ptr = (void *)(user_mem + left);
            *scratch_left = user_mem_size - left;
            *scratch = 1;
        }
        else
        {
            /* We're done with this user mem segment */
            (*iovec_ndx)++;
        }

        /* More Data! */
        if(which == 1)
        {
            /* Read */
            memcpy(user_mem,
                     (void *)(voidp_t)(block + (block_size - left)),
                     (size_t)user_mem_size);
        }
        else
        {
            /* Write */           
            memcpy((void*)(voidp_t)(block + (block_size - left)),
                     user_mem,
                     (size_t)user_mem_size);
        }

        left -= user_mem_size;
    }
    return 1;
}
#endif /* PVFS_UCACHE_ENABLE */

/*The Wrapper Fuction calls to the "nocache" version of 
 *  io_common_readorwrite (below)   
 */
/** do a blocking read or write, possibly utilizing the user cache.
 * 
 */
int iocommon_readorwrite(enum PVFS_io_type which,
                         pvfs_descriptor *pd,
                         PVFS_size offset,
                         void *buf,
                         PVFS_Request mem_req,
                         PVFS_Request file_req,
                         size_t count,
                         const struct iovec *vector)
{
    int rc = 0;
#if PVFS_UCACHE_ENABLE
    int i;
    int tag_cnt = 0; /* how many tags/blocks to access */
    int size; /* how many bytes request spans */
    uint64_t remainder = 0;
    uint32_t blk_size = CACHE_BLOCK_SIZE_K * 1024;
    uint64_t next_tag = 0;
    /* Array of the tags we need to read/write to */
    uint64_t tags[tag_cnt];
    PVFS_fs_id *fs_id = &(pd->s->pvfs_ref.fs_id);
    PVFS_handle *handle = &(pd->s->pvfs_ref.handle);
    /* Used to determine if we finished writing a
     * block without filling up the 
     * current io segment 
     */
    unsigned char scratch = 0;  
    /* The offset into the last io semgment that
     * was partially used (so use 
     * this ptr then move on to the next io segment) .
     */
    void  *scratch_ptr = 0; 
    uint64_t scratch_left = 0;
    int iovec_ndx = 0;
    uint16_t block_ndx = 0;

    /* stores hits and misses of blocks */
    unsigned char *hits = 0;

    if(!pvfs_ucache_enabled() || !pd->s->mtbl)
    {
        ucache_pseudo_misses++; /* could overflow, reset periodically */
#endif /* PVFS_UCACHE_ENABLE */
        /* Bypass the ucache */
        errno = 0;
        rc = iocommon_readorwrite_nocache(which,
                                          pd,
                                          offset,
                                          buf,
                                          mem_req,
                                          file_req);
        if (rc < 0)
        {
            goto errorout;
        }
#if PVFS_UCACHE_ENABLE
    }

    /* How many bytes does request span */
    /* these will be contiguous in file starting at offset */
    /* may be spread in out memory */
    for (i = 0; i < count; i++)
    {
        size += vector[i].iov_len;
    }

    /* How many tags? */
    tag_cnt = size / (CACHE_BLOCK_SIZE_K * 1024);

    /* Add 2 to be sure we have enough tags (may not need them all) */
    tag_cnt += 2;
   
    /* Block Aligned */
    remainder = offset % blk_size;
    tags[0] = offset - remainder;

    /* Loop over positions storing tags (ment identifiers) */
    next_tag = tags[0] + blk_size;
    for(i = 1; i < tag_cnt; i++)
    {
        tags[i] = next_tag;
        next_tag += blk_size;
        if (next_tag > (offset + size))
            break;
    }
    /* This should represent the number of blks */
    tag_cnt = i + 1;

    /* Now that tags are set build array of lookup responses*/
    hits = (unsigned char *)malloc(sizeof(unsigned char) * tag_cnt);
    for(i = 0; i < tag_cnt; i++)
    {
        /* if lookup returns nil set char to 0, otherwise 1 */
        if(ucache_lookup(fs_id, handle, tags[i], NULL) == (void *)NIL) 
        {
            ucache_misses++; /* could overflow, reset periodically */
            hits[tag_cnt] = 0; /* miss */
        }
        else{
            ucache_hits++;  /* could overflow, reset periodically */
            hits[tag_cnt] = 1; /* hit */
        }
    }

    uint64_t block_loc = 0;
    for(i = 0; i < tag_cnt; i++)
    {
        if(which == 1) /* Read */
        {
            if(hits[i] == 1) /* Hit */
            {
                /* Read from Cache */
                //instead of looking up again, save lookup somehow
                block_loc = (voidp_t)ucache_lookup(fs_id, handle, tags[i], &block_ndx);
                lock_lock(ucache_lock);
                lock_lock(get_lock(block_ndx)); 
                rc = place_data(1, block_loc, vector, &iovec_ndx, 
                                   &scratch, &scratch_ptr, &scratch_left);
                lock_unlock(get_lock(block_ndx));
                lock_unlock(ucache_lock);
            }
            else /* Miss */
            {
                /* read from fs into user mem */
                rc = iocommon_readorwrite_nocache(which, pd, offset, 
                                                  buf, mem_req, file_req);
                if(rc > 0)
                {
                    block_loc = (voidp_t)ucache_insert(fs_id, handle, tags[i], 
                                                         &block_ndx);
                }
                /* Copy into cache if possible */
                if(block_loc != (uint64_t)NIL)
                {
                    lock_lock(ucache_lock);
                    lock_lock(get_lock(block_ndx)); 
                    rc = place_data(2, block_loc, vector, &iovec_ndx, 
                                       &scratch, &scratch_ptr, &scratch_left);
                    lock_unlock(get_lock(block_ndx));
                    lock_unlock(ucache_lock);
                }
            }
        }
        else if(which == 2) /* Write */
        {
            if(hits[i] == 1) /* Hit */
            {
                /* Overwrite block in cache */
                /* //or use previous return value */
                block_loc = (voidp_t)ucache_lookup(fs_id, handle, tags[i], &block_ndx); 
                lock_lock(ucache_lock);
                lock_lock(get_lock(block_ndx));  
                rc = place_data(2, block_loc, vector, &iovec_ndx, 
                                   &scratch, &scratch_ptr, &scratch_left);
                lock_unlock(get_lock(block_ndx));
                lock_unlock(ucache_lock);
            }
            else /* Miss */
            {
                /* Attempt ucache insert, on fail, send to file system */
                block_loc = (voidp_t)ucache_insert(fs_id, handle, tags[i], &block_ndx);
                if(block_loc != (uint64_t)NIL)
                {
                    lock_lock(ucache_lock);
                    lock_lock(get_lock(block_ndx)); 
                    rc = place_data(2, block_loc, vector, &iovec_ndx,
                                       &scratch, &scratch_ptr, &scratch_left);
                    lock_unlock(get_lock(block_ndx));
                    lock_unlock(ucache_lock);
                }
                else
                {
                    rc = iocommon_readorwrite_nocache(which,
                                                      pd,
                                                      offset, 
                                                      buf,
                                                      mem_req,
                                                      file_req);
                }
            }
        }
    }
    free(hits);
#endif /* PVFS_UCACHE_ENABLE */
errorout:
    return rc;
}

/** do a blocking read or write
 * 
 */
int iocommon_readorwrite_nocache(enum PVFS_io_type which,
                         pvfs_descriptor *pd,
                         PVFS_size offset,
                         void *buf,
                         PVFS_Request mem_req,
                         PVFS_Request file_req)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *creds;
    PVFS_sysresp_io io_resp;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&io_resp, 0, sizeof(io_resp));

    /* Ensure descriptor is used for the correct type of access */
    if ((which == PVFS_IO_READ &&
            (O_WRONLY == (pd->s->flags & O_ACCMODE))) ||
        (which == PVFS_IO_WRITE &&
            (O_RDONLY == (pd->s->flags & O_ACCMODE))))
    {
        errno = EBADF;
        return -1;
    }

    iocommon_cred(&creds);

    errno = 0;
    rc = PVFS_sys_io(pd->s->pvfs_ref,
                     file_req,
                     offset,
                     buf,
                     mem_req,
                     creds,
                     &io_resp,
                     which,
                     PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);
    return io_resp.total_completed;

errorout:
    return rc;
}

/** Do a nonblocking read or write, possibly utilizing the user cache.
 *
 * extra_offset = extra padding to the pd's offset,
 * independent of the pd's offset
 * Returns an op_id, response, and ret_mem_request
 * (which represents an etype_req*count region)
 * Note that the none of the PVFS_Requests are freed
 *
int iocommon_ireadorwrite(enum PVFS_io_type which,
                          pvfs_descriptor *pd,
                          PVFS_size extra_offset,
                          void *buf,
                          PVFS_Request etype_req,
                          PVFS_Request file_req,
                          size_t count,
                          PVFS_sys_op_id *ret_op_id,
                          PVFS_sysresp_io *ret_resp,
                          PVFS_Request *ret_memory_req)
{
    #ifndef UCACHE_ENABLED
    // No cache 
    return iocommon_ireadorwrite_nocache(which, pd, extra_offset, buf, 
        etype_req, file_req, count, ret_op_id, ret_resp, ret_memory_req);
    #endif // UCACHE_ENABLED 

    //if read then check cache..if not there..then read from i/o node and store into correct location
    //Possibly Data Transfer
    //Possibly More Cache Routines
}
*/

/** Do a nonblocking read or write
 *
 * extra_offset = extra padding to the pd's offset,
 * independent of the pd's offset
 * Returns an op_id, response, and ret_mem_request
 * (which represents an etype_req*count region)
 * Note that the none of the PVFS_Requests are freed
 */
int iocommon_ireadorwrite(enum PVFS_io_type which,
                          pvfs_descriptor *pd,
                          PVFS_size extra_offset,
                          void *buf,
                          PVFS_Request etype_req,
                          PVFS_Request file_req,
                          size_t count,
                          PVFS_sys_op_id *ret_op_id,
                          PVFS_sysresp_io *ret_resp,
                          PVFS_Request *ret_memory_req)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_Request contig_memory_req;
    PVFS_credentials *credentials;
    PVFS_size req_size;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    //Ensure descriptor is used for the correct type of access
    if ((which==PVFS_IO_READ && (O_WRONLY == (pd->s->flags & O_ACCMODE))) ||
        (which==PVFS_IO_WRITE && (O_RDONLY == (pd->s->flags & O_ACCMODE))))
    {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    //Create the memory request of a contiguous region: 'mem_req' x count
    rc = PVFS_Request_contiguous(count, etype_req, &contig_memory_req);

    iocommon_cred(&credentials);

    errno = 0;
    rc = PVFS_isys_io(pd->s->pvfs_ref,
                      file_req,
                      pd->s->file_pointer+extra_offset,
                      buf,
                      contig_memory_req,
                      credentials,
                      ret_resp,
                      which,
                      ret_op_id,
                      PVFS_HINT_NULL,
                      NULL);
    IOCOMMON_CHECK_ERR(rc);

    assert(*ret_op_id!=-1);//TODO: handle this

    PVFS_Request_size(contig_memory_req, &req_size);
    pd->s->file_pointer += req_size;
    *ret_memory_req = contig_memory_req;
    return 0;

errorout:
    return rc;
}

/** Implelments an object attribute get or read
 *
 */
int iocommon_getattr(PVFS_object_ref obj, PVFS_sys_attr *attr, uint32_t mask)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;
    PVFS_sysresp_getattr getattr_response;

    /* Initialize */
    memset(&getattr_response, 0, sizeof(getattr_response));

    /* check credentials */
    iocommon_cred(&credentials);

    /* now get attributes */
    errno = 0;
    rc = PVFS_sys_getattr(obj,
                          mask,
                          credentials,
                          &getattr_response, NULL);
    IOCOMMON_CHECK_ERR(rc);
    *attr = getattr_response.attr;

errorout:
    return rc;
}

/** Implelments an object attribute set or write
 *
 */
int iocommon_setattr(PVFS_object_ref obj, PVFS_sys_attr *attr)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;

    /* check credentials */
    iocommon_cred(&credentials);

    /* now get attributes */
    rc = PVFS_sys_setattr(obj, *attr, credentials, NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

int iocommon_stat(pvfs_descriptor *pd, struct stat *buf, uint32_t mask)
{
    int rc = 0;
    PVFS_sys_attr attr;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    errno = 0;
    rc = iocommon_getattr(pd->s->pvfs_ref, &attr, mask);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    buf->st_dev = pd->s->pvfs_ref.fs_id;
    buf->st_ino = pd->s->pvfs_ref.handle;
    buf->st_mode = attr.perms;
    if (attr.objtype == PVFS_TYPE_METAFILE)
    {
        buf->st_mode |= S_IFREG;
    }
    if (attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        buf->st_mode |= S_IFDIR;
    }
    if (attr.objtype == PVFS_TYPE_SYMLINK)
    {
        buf->st_mode |= S_IFLNK;
    }
    buf->st_nlink = 1; /* PVFS does not allow hard links */
    buf->st_uid = attr.owner;
    buf->st_gid = attr.group;
    buf->st_rdev = 0; /* no dev special files */
    buf->st_size = attr.size;
    buf->st_blksize = attr.blksize;
    if (attr.blksize)
    {
        buf->st_blocks = (attr.size + (attr.blksize - 1)) / attr.blksize;
    }
    buf->st_atime = attr.atime;
    buf->st_mtime = attr.mtime;
    buf->st_ctime = attr.ctime;

errorout:
    return rc;
}

/*
 * The only difference here is that buf is stat64 which
 * means some of its fields are defined as different types
 */
int iocommon_stat64(pvfs_descriptor *pd, struct stat64 *buf, uint32_t mask)
{
    int rc = 0;
    PVFS_sys_attr attr;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    errno = 0;
    rc = iocommon_getattr(pd->s->pvfs_ref, &attr, mask);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    buf->st_dev = pd->s->pvfs_ref.fs_id;
    buf->st_ino = pd->s->pvfs_ref.handle;
    buf->st_mode = attr.perms;
    if (attr.objtype == PVFS_TYPE_METAFILE)
    {
        buf->st_mode |= S_IFREG;
    }
    if (attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        buf->st_mode |= S_IFDIR;
    }
    if (attr.objtype == PVFS_TYPE_SYMLINK)
    {
        buf->st_mode |= S_IFLNK;
    }
    buf->st_nlink = 1; /* PVFS does not allow hard links */
    buf->st_uid = attr.owner;
    buf->st_gid = attr.group;
    buf->st_rdev = 0; /* no dev special files */
    buf->st_size = attr.size;
    buf->st_blksize = attr.blksize;
    if (attr.blksize)
    {
        buf->st_blocks = (attr.size + (attr.blksize - 1)) / attr.blksize;
    }
    buf->st_atime = attr.atime;
    buf->st_mtime = attr.mtime;
    buf->st_ctime = attr.ctime;

errorout:
    return rc;
}

int iocommon_chown(pvfs_descriptor *pd, uid_t owner, gid_t group)
{
    int rc = 0;
    PVFS_sys_attr attr;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    if (owner != -1)
    {
        attr.owner = owner;
        attr.mask |= PVFS_ATTR_SYS_UID;
    }
    if (group != -1)
    {
        attr.group = group;
        attr.mask |= PVFS_ATTR_SYS_GID;
    }

    errno = 0;
    rc = iocommon_setattr(pd->s->pvfs_ref, &attr);
    return rc;
}

int iocommon_chmod(pvfs_descriptor *pd, mode_t mode)
{
    int rc = 0;
    PVFS_sys_attr attr;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    attr.perms = mode & 07777; /* mask off any stray bits */
    attr.mask = PVFS_ATTR_SYS_PERM;

    errno = 0;
    rc = iocommon_setattr(pd->s->pvfs_ref, &attr);
    return rc;
}

int iocommon_make_directory(const char *pvfs_path,
                        const int mode,
                        PVFS_object_ref *pdir)
{
    int rc = 0;
    int orig_errno = errno;
    char *parentdir = NULL;
    char *filename = NULL;
    PVFS_sys_attr       attr;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref     parent_ref;
    PVFS_sysresp_mkdir  resp_mkdir;
    PVFS_credentials    *credentials;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&resp_lookup, 0, sizeof(resp_lookup));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_mkdir,  0, sizeof(resp_mkdir));

    pvfs_sys_init();
    iocommon_cred(&credentials);

    rc = split_pathname(pvfs_path, 1, &parentdir, &filename);
    IOCOMMON_RETURN_ERR(rc);

    /* Make sure we don't try and create the root or current directory */

    /* lookup parent */
    if (!pdir)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
            errno = 0;
            rc = iocommon_lookup_relative(parentdir, *pdir,
                            PVFS2_LOOKUP_LINK_FOLLOW, &parent_ref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            parent_ref = *pdir;
        }
    }
   
    /* Set the attributes for the new directory */
    attr.owner = geteuid();
    attr.group = getegid();
    attr.perms = mode & 07777; /* mask off stray bits */
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    errno = 0;
    rc = PVFS_sys_mkdir(filename,
                        parent_ref,
                        attr,
                        credentials,
                        &resp_mkdir, NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    if (parentdir)
    {
        free(parentdir);
    }
    if (filename)
    {
        free(filename);
    }   
    return(rc);
}

int iocommon_readlink(pvfs_descriptor *pd, char *buf, int size)
{
    int rc = 0;
    PVFS_sys_attr attr;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize any variables */
    memset(&attr, 0, sizeof(attr));
 
    errno = 0;
    rc = iocommon_getattr(pd->s->pvfs_ref, &attr, PVFS_ATTR_SYS_TYPE | 
                                               PVFS_ATTR_SYS_LNK_TARGET);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    if (attr.objtype == PVFS_TYPE_SYMLINK)
    {
        strncpy(buf, attr.link_target, size);
    }
    else
    {
        errno = EINVAL;
        return -1;
    }

errorout:
    return rc;
}

int iocommon_symlink(const char *pvfs_path,   /* where new linkis created */
                     const char *link_target, /* contents of the link */
                     PVFS_object_ref *pdir)   /* suports symlinkat */
{
    int rc = 0;
    int orig_errno = errno;
    char *parentdir = NULL;
    char *filename = NULL;
    PVFS_sys_attr       attr;
    PVFS_object_ref     parent_ref;
    PVFS_sysresp_symlink  resp_symlink;
    PVFS_credentials    *credentials;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_symlink,0, sizeof(resp_symlink));

    pvfs_sys_init();
    iocommon_cred(&credentials);


    rc = split_pathname(pvfs_path, 0, &parentdir, &filename);
    IOCOMMON_RETURN_ERR(rc);

    /* Make sure we don't try and create the root or current directory */

    /* lookup parent */
    if (!pdir)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
            errno = 0;
            rc = iocommon_lookup_relative(parentdir, *pdir,
                            PVFS2_LOOKUP_LINK_FOLLOW, &parent_ref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            parent_ref = *pdir;
        }
    }
   
    /* Set the attributes for the new directory */
    attr.owner = getuid();
    attr.group = getgid();
    attr.perms = 0777;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    errno = 0;
    rc = PVFS_sys_symlink(filename,
                          parent_ref,
                          (char *)link_target,
                          attr,
                          credentials,
                          &resp_symlink,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    if (parentdir)
    {
        free(parentdir);
    }
    if (filename)
    {
        free(filename);
    }   
    return(rc);
}

int iocommon_getdents(pvfs_descriptor *pd, /**< pvfs fiel descriptor */
                      struct dirent *dirp, /**< pointer to buffer */
                      unsigned int size)   /**< number of bytes in buffer */
{
    int rc = 0;
    int orig_errno = errno;
    int name_max;
    int count;  /* number of records to read */
    PVFS_credentials *credentials;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_ds_position token;
    int bytes = 0, i = 0;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    if (pd->s->token == PVFS_READDIR_END)
    {
        return 0;  /* EOF */
    }

    if (!S_ISDIR(pd->s->mode))
    {
        errno = ENOENT;
        return -1;
    }

    /* Initialize */
    memset(&readdir_resp, 0, sizeof(readdir_resp));

    iocommon_cred(&credentials);

    token = pd->s->token == 0 ? PVFS_READDIR_START : pd->s->token;

    /* posix deals in bytes in buffer and bytes read */
    /* PVFS deals in number of records to read or were read */
    count = size / sizeof(struct dirent);
    if (count > PVFS_REQ_LIMIT_DIRENT_COUNT)
    {
        count = PVFS_REQ_LIMIT_DIRENT_COUNT;
    }
    errno = 0;
    rc = PVFS_sys_readdir(pd->s->pvfs_ref,
                          token,
                          count,
                          credentials,
                          &readdir_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

    pd->s->token = readdir_resp.token;
    name_max = PVFS_util_min(NAME_MAX, PVFS_NAME_MAX);
    for(i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
    {
        /* copy a PVFS_dirent to a struct dirent */
        dirp->d_ino = (long)readdir_resp.dirent_array[i].handle;
        dirp->d_off = pd->s->file_pointer;
        dirp->d_reclen = sizeof(PVFS_dirent);
        memcpy(dirp->d_name, readdir_resp.dirent_array[i].d_name, name_max);
        dirp->d_name[name_max] = 0;
        pd->s->file_pointer += sizeof(struct dirent);
        bytes += sizeof(struct dirent);
        dirp++;
    }
    free(readdir_resp.dirent_array);
    return bytes;

errorout:
    return rc;
}

int iocommon_getdents64(pvfs_descriptor *pd,
                      struct dirent64 *dirp,
                      unsigned int size)
{
    int rc = 0;
    int orig_errno = errno;
    int name_max;
    int count;
    PVFS_credentials *credentials;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_ds_position token;
    int bytes = 0, i = 0;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    if (pd->s->token == PVFS_READDIR_END)
    {
        return 0;  /* EOF */
    }

    if (!S_ISDIR(pd->s->mode))
    {
        errno = ENOENT;
        return -1;
    }

    /* Initialize */
    memset(&readdir_resp, 0, sizeof(readdir_resp));

    iocommon_cred(&credentials);

    token = pd->s->token == 0 ? PVFS_READDIR_START : pd->s->token;

    count = size / sizeof(struct dirent64);
    if (count > PVFS_REQ_LIMIT_DIRENT_COUNT)
    {
        count = PVFS_REQ_LIMIT_DIRENT_COUNT;
    }
    errno = 0;
    rc = PVFS_sys_readdir(pd->s->pvfs_ref,
                          token,
                          count,
                          credentials,
                          &readdir_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

    pd->s->token = readdir_resp.token;
    name_max = PVFS_util_min(NAME_MAX, PVFS_NAME_MAX);
    for(i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
    {
        /* copy a PVFS_dirent to a struct dirent64 */
        dirp->d_ino = (uint64_t)readdir_resp.dirent_array[i].handle;
        dirp->d_off = (off64_t)pd->s->file_pointer;
        dirp->d_reclen = sizeof(struct dirent64);
        memcpy(dirp->d_name, readdir_resp.dirent_array[i].d_name, name_max);
        dirp->d_name[name_max] = 0;
        pd->s->file_pointer += sizeof(struct dirent64);
        bytes += sizeof(struct dirent64);
        dirp++;
    }
    free(readdir_resp.dirent_array);
    return bytes;

errorout:
    return rc;
}

/* Read entries from a directory.
 *                  
 *  \param token opaque value used to track position in directory
 *         when more than one read is required.
 *  \param pvfs_dirent_incount maximum number of entries to read, if
 *         available, starting from token.
PVFS_error PVFS_sys_readdir(
    PVFS_object_ref ref,
    PVFS_ds_position token, 
    int32_t pvfs_dirent_incount,
    const PVFS_credentials *credentials,
    PVFS_sysresp_readdir *resp,
    PVFS_hint hints)
 */          
/* Read entries from a directory and their associated attributes
 *  in an efficient manner.
 *  
 *  \param token opaque value used to track position in directory
 *         when more than one read is required.
 *  \param pvfs_dirent_incount maximum number of entries to read, if
 *         available, starting from token.
PVFS_error PVFS_sys_readdirplus(
    PVFS_object_ref ref,
    PVFS_ds_position token,
    int32_t pvfs_dirent_incount,
    const PVFS_credentials *credentials,
    uint32_t attrmask,
    PVFS_sysresp_readdirplus *resp,
    PVFS_hint hints)
 */

/** Checks to see if caller has the requested permissions
 *
 */
int iocommon_access(const char *pvfs_path,
                    const int mode,
                    const int flags,
                    PVFS_object_ref *pdir)
{
    int rc = 0;
    int orig_errno = errno;
    char *parentdir = NULL;
    char *file = NULL;
    int followflag = PVFS2_LOOKUP_LINK_FOLLOW;
    int uid = -1, gid = -1;
    PVFS_object_ref parent_ref, file_ref;
    PVFS_credentials *credentials;
    PVFS_sys_attr attr;

    /* Initialize */
    memset(&parent_ref, 0, sizeof(parent_ref));
    memset(&file_ref, 0, sizeof(file_ref));
    memset(&attr, 0, sizeof(attr));

    /* Initialize the system interface for this process */
    pvfs_sys_init();
    iocommon_cred(&credentials);

    rc = split_pathname(pvfs_path, 0, &parentdir, &file);
    IOCOMMON_RETURN_ERR(rc);

    if (!pdir)
    {
        errno = 0;
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
            errno = 0;
            rc = iocommon_lookup_relative(parentdir, *pdir,
                            PVFS2_LOOKUP_LINK_FOLLOW, &parent_ref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        { 
            parent_ref = *pdir;
        }
    }
    /* Attempt to find file */
    if (flags & AT_SYMLINK_NOFOLLOW)
    {
        followflag = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    }
    errno = 0;
    rc = iocommon_lookup_relative(file,
                                  parent_ref,
                                  followflag,
                                  &file_ref,
                                  NULL,
                                  0);
    IOCOMMON_CHECK_ERR(rc);
    /* Get file atributes */
    errno = 0;
    rc = iocommon_getattr(file_ref, &attr, PVFS_ATTR_SYS_COMMON_ALL);
    IOCOMMON_RETURN_ERR(rc);

    if (flags & AT_EACCESS)
    {
        uid = getuid();
        gid = getgid();
    }
    else
    {
        uid = geteuid();
        gid = getegid();
    }
    if (uid == attr.owner)
    {
        if (((R_OK & mode) && !(S_IRUSR & attr.perms)) ||
           ((W_OK & mode) && !(S_IWUSR & attr.perms)) ||
           ((X_OK & mode) && !(S_IXUSR & attr.perms)))
        {
            errno = EACCES;
            rc = -1;
            goto errorout;
        }
    }
    else if (gid == attr.group)
    {
        if (((R_OK & mode) && !(S_IRGRP & attr.perms)) ||
           ((W_OK & mode) && !(S_IWGRP & attr.perms)) ||
           ((X_OK & mode) && !(S_IXGRP & attr.perms)))
        {
            errno = EACCES;
            rc = -1;
            goto errorout;
        }
    }
    else
    {
        if (((R_OK & mode) && !(S_IROTH & attr.perms)) ||
           ((W_OK & mode) && !(S_IWOTH & attr.perms)) ||
           ((X_OK & mode) && !(S_IXOTH & attr.perms)))
        {
            errno = EACCES;
            rc = -1;
            goto errorout;
        }
    }
    /* Access is allowed, rc should be 0 */  

errorout:
    if (parentdir)
    {
        free(parentdir);
    }
    if (file)
    {
        free(file);
    }
    return rc;
}

int iocommon_statfs(pvfs_descriptor *pd, struct statfs *buf)
{
    int rc = 0;
    int orig_errno = errno;
    int block_size = 2*1024*1024; /* optimal transfer size 2M */
    PVFS_credentials *credentials;
    PVFS_sysresp_statfs statfs_resp;
    
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize the system interface for this process */
    pvfs_sys_init();
    iocommon_cred(&credentials);
    memset(&statfs_resp, 0, sizeof(statfs_resp));

    errno = 0;
    rc = PVFS_sys_statfs(pd->s->pvfs_ref.fs_id,
                         credentials,
                         &statfs_resp,
                         NULL);
    IOCOMMON_CHECK_ERR(rc);
    /* assign fields for statfs struct */
    /* this is a fudge because they don't line up */
    buf->f_type = PVFS2_SUPER_MAGIC;
    buf->f_bsize = block_size; 
    buf->f_blocks = statfs_resp.statfs_buf.bytes_total/1024;
    buf->f_bfree = statfs_resp.statfs_buf.bytes_available/1024;
    buf->f_bavail = statfs_resp.statfs_buf.bytes_available/1024;
    buf->f_files = statfs_resp.statfs_buf.handles_total_count;
    buf->f_ffree = statfs_resp.statfs_buf.handles_available_count;
    buf->f_fsid.__val[0] = statfs_resp.statfs_buf.fs_id;
    buf->f_fsid.__val[1] = 0;
    buf->f_namelen = PVFS_NAME_MAX;

errorout:
    return rc;
}

int iocommon_statfs64(pvfs_descriptor *pd, struct statfs64 *buf)
{
    int rc = 0;
    int orig_errno = errno;
    int block_size = 2*1024*1024; /* optimal transfer size 2M */
    PVFS_credentials *credentials;
    PVFS_sysresp_statfs statfs_resp;
    
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize the system interface for this process */
    pvfs_sys_init();
    iocommon_cred(&credentials);
    memset(&statfs_resp, 0, sizeof(statfs_resp));

    errno = 0;
    rc = PVFS_sys_statfs(pd->s->pvfs_ref.fs_id,
                         credentials,
                         &statfs_resp,
                         NULL);
    IOCOMMON_CHECK_ERR(rc);
    /* assign fields for statfs struct */
    /* this is a fudge because they don't line up */
    buf->f_type = PVFS2_SUPER_MAGIC;
    buf->f_bsize = block_size; 
    buf->f_blocks = statfs_resp.statfs_buf.bytes_total/1024;
    buf->f_bfree = statfs_resp.statfs_buf.bytes_available/1024;
    buf->f_bavail = statfs_resp.statfs_buf.bytes_available/1024;
    buf->f_files = statfs_resp.statfs_buf.handles_total_count;
    buf->f_ffree = statfs_resp.statfs_buf.handles_available_count;
    buf->f_fsid.__val[0] = statfs_resp.statfs_buf.fs_id;
    buf->f_fsid.__val[1] = 0;
    buf->f_namelen = PVFS_NAME_MAX;

errorout:
    return rc;
}

int iocommon_sendfile(int sockfd, pvfs_descriptor *pd,
                      off64_t *offset, size_t count)
{
    int rc = 0, bytes_read = 0;
    PVFS_Request mem_req, file_req;
    char *buffer;
    int buffer_size = (8*1024*1024);

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    buffer = (char *)malloc(buffer_size);

    PVFS_Request_contiguous(buffer_size, PVFS_BYTE, &mem_req);
    file_req = PVFS_BYTE;

    errno = 0;
    rc = iocommon_readorwrite_nocache(PVFS_IO_READ, pd, *offset + bytes_read,
                               buffer, mem_req, file_req);
    while(rc > 0)
    {
        int flags = 0;
        bytes_read += rc;
        if (bytes_read + buffer_size < count)
        {
            flags = MSG_MORE;
        }
        rc = glibc_ops.send(sockfd, buffer, rc, flags);
        if (rc < 0)
        {
            break;
        }
        errno = 0;
        rc = iocommon_readorwrite_nocache(PVFS_IO_READ, pd, *offset + bytes_read,
                                   buffer, mem_req, file_req);
    }  
    PVFS_Request_free(&mem_req);
    free(buffer);
    if (rc < 0)
    {
        return -1;
    }
    else
    {
        *offset += bytes_read;
        return bytes_read;
    }
}

/** Implelments an extended attribute get or read
 *
 *  The PVFS server enforces namespaces as prefixes on the
 *  attribute keys.  Thus they are not checked here.
 *  Probably would be more efficient to do so.
 */
int iocommon_geteattr(pvfs_descriptor *pd,
                      const char *key_p,
                      void *val_p,
                      int size)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;
    PVFS_ds_keyval key, val;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));

    /* check credentials */
    iocommon_cred(&credentials);

    key.buffer = (char *)key_p;
    key.buffer_sz = strlen(key_p) + 1;
    val.buffer = val_p;
    val.buffer_sz = size;

    /* now get attributes */
    errno = 0;
    rc = PVFS_sys_geteattr(pd->s->pvfs_ref,
                          credentials,
                          &key,
                          &val,
                          NULL);
    switch (rc)
    {
    case -PVFS_ENOENT:
        /* file exists if we have a pd */
        /* either attr does not exist or */
        /* we do not have access to it */
        rc = -PVFS_ENODATA;
        break;
    case -PVFS_EMSGSIZE:
        /* buffer was too small for the attribute value */
        rc = -PVFS_ERANGE;
    }
    IOCOMMON_CHECK_ERR(rc);
    rc = val.read_sz;

errorout:
    return rc;
}

/** Implelments an extended attribute set or write
 *
 *  The flag can be used to control whether to overwrite
 *  or create a new attribute.  The default is to create
 *  if needed and overwrite if a previus value exists.
 *  The PVFS server enforces namespaces as prefixes on the
 *  attribute keys.  Thus they are not checked here.
 *  Probably would be more efficient to do so.
 */
int iocommon_seteattr(pvfs_descriptor *pd,
                      const char *key_p,
                      const void *val_p,
                      int size,
                      int flag)
{
    int rc = 0;
    int pvfs_flag = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;
    PVFS_ds_keyval key, val;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));

    /* check credentials */
    iocommon_cred(&credentials);

    key.buffer = (char *)key_p;
    key.buffer_sz = strlen(key_p) + 1;
    val.buffer = (void *)val_p;
    val.buffer_sz = size;

    if (flag & XATTR_CREATE)//TODO
    {
        pvfs_flag |= PVFS_XATTR_CREATE;
    }
    if (flag & XATTR_REPLACE)//TODO
    {
        pvfs_flag |= PVFS_XATTR_REPLACE;
    }

    /* now set attributes */
    errno = 0;
    rc = PVFS_sys_seteattr(pd->s->pvfs_ref,
                          credentials,
                          &key,
                          &val,
                          pvfs_flag,
                          NULL);
    if (rc == -PVFS_ENOENT)
    {
        /* file exists if we have a pd */
        /* either attr does not exist or */
        /* we do not have access to it */
        rc = -PVFS_ENODATA;
    }
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

/** Implements an extended attribute delete or remove
 *
 *  The PVFS server enforces namespaces as prefixes on the
 *  attribute keys.  Thus they are not checked here.
 *  Probably would be more efficient to do so.
 */
int iocommon_deleattr(pvfs_descriptor *pd,
                      const char *key_p)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credentials *credentials;
    PVFS_ds_keyval key;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));

    /* check credentials */
    iocommon_cred(&credentials);

    key.buffer = (char *)key_p;
    key.buffer_sz = strlen(key_p) + 1;

    /* now set attributes */
    errno = 0;
    rc = PVFS_sys_deleattr(pd->s->pvfs_ref,
                           credentials,
                           &key,
                           NULL);
    if (rc == -PVFS_ENOENT)
    {
        /* file exists if we have a pd */
        /* either attr does not exist or */
        /* we do not have access to it */
        rc = -PVFS_ENODATA;
    }
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

/** Implelments an extended attribute key list
 *
 *  All of the keys for athe specified object are returned
 *  in the specified buffer, NULL delimited.  The number
 *  of keys is returned.  If the size passed in is 0, then
 *  only the number of keys available is returned.
 *  The PVFS server enforces namespaces as prefixes on the
 *  attribute keys.  Thus they are not checked here.
 *  Probably would be more efficient to do so.
 */
int iocommon_listeattr(pvfs_descriptor *pd,
                       char *list,
                       int size,
                       int *retsize)
{
    int rc = 0;
    int orig_errno = errno;
    int k, total_size, total_keys, max_keys;
    int32_t nkey;
    PVFS_ds_position token;
    PVFS_credentials *credentials;
    PVFS_sysresp_listeattr listeattr_resp;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&listeattr_resp, 0, sizeof(listeattr_resp));
    token = PVFS_ITERATE_START;
    total_size = 0;
    total_keys = 0;
    nkey = 0;

    /* check credentials */
    iocommon_cred(&credentials);

    /* find number of attributes */
    errno = 0;
    rc = PVFS_sys_listeattr(pd->s->pvfs_ref,
                            token,
                            nkey,
                            credentials,
                            &listeattr_resp,
                            NULL);
    if (rc == -PVFS_ENOENT)
    {
        /* file exists if we have a pd */
        /* either attr does not exist or */
        /* we do not have access to it */
        rc = -PVFS_ENODATA;
    }
    IOCOMMON_CHECK_ERR(rc);

    /* get available keys */
    nkey = listeattr_resp.nkey;

    /* allocate key_array */
    if (nkey > PVFS_MAX_XATTR_LISTLEN)
    {
        max_keys = PVFS_MAX_XATTR_LISTLEN;
    }
    else
    {
        max_keys = nkey;
    }
    listeattr_resp.key_array = (PVFS_ds_keyval *)malloc(max_keys *
                                                    sizeof(PVFS_ds_keyval));
    for (k = 0; k < max_keys; k++)
    {
        listeattr_resp.key_array[k].buffer_sz = PVFS_MAX_XATTR_NAMELEN;
        listeattr_resp.key_array[k].buffer =
                            (char *)malloc(PVFS_MAX_XATTR_NAMELEN);
    }
    
    /* now list attributes */
    do
    {
        token = listeattr_resp.token;
        listeattr_resp.nkey = max_keys;
        errno = 0;
        rc = PVFS_sys_listeattr(pd->s->pvfs_ref,
                                token,
                                nkey,
                                credentials,
                                &listeattr_resp,
                                NULL);
        if (rc == -PVFS_ENOENT)
        {
            /* file exists if we have a pd */
            /* either attr does not exist or */
            /* we do not have access to it */
            rc = -PVFS_ENODATA;
        }
        IOCOMMON_CHECK_ERR(rc);

        /* copy keys out to caller */
        for (k = 0; k < listeattr_resp.nkey; k++)
        {
            if (size > 0)
            {
                if (total_size + listeattr_resp.key_array[k].read_sz > size)
                {
                    total_size = size;
                    errno = ERANGE;
                    rc = -1;
                    break; /* ran out of buffer space */
                }
                strncpy(list, listeattr_resp.key_array[k].buffer,
                        listeattr_resp.key_array[k].read_sz);
                list += listeattr_resp.key_array[k].read_sz;
            }
            total_size += listeattr_resp.key_array[k].read_sz;
        }
        total_keys += listeattr_resp.nkey;
    } while (total_keys < nkey && listeattr_resp.nkey > 0 &&
             total_size < size);
    *retsize = total_size;
    /* free key_array */
    for (k = 0; k < max_keys; k++)
    {
        free(listeattr_resp.key_array[k].buffer);
    }
    free(listeattr_resp.key_array);

errorout:
    return rc;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

