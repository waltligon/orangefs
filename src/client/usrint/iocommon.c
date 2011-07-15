/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - low level calls to system interface
 */
#include <usrint.h>
#include <linux/dirent.h>
#include <posix-ops.h>
#include <openfile-util.h>
#include <iocommon.h>

/* Functions in this file generally define a label errorout
 * for cleanup before exit and return an int rc which is -1
 * on error with the error code in errno, 0 on success.
 * IOCOMMON_RETURN_ERR checks a return code from a function
 * returns the same protocol and goto errorout: if less than 0
 * IOCOMMON_CHECK_ERR assumes the return code contains the
 * negative of the error code as encoded by PVFS sysint
 * functions and decodes these before jumping to errorout.
 * CHECK_ERR should be called after each sysint call to
 * correctly pass error codes.
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

/* this is a global analog of errno for pvfs specific */
/* errors errno is set to EIO and this si set to the */
/* original code */
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
    PVFS_credentials *credentials;

    pvfs_sys_init();
    iocommon_cred(&credentials);
    rc = PVFS_sys_flush(pd->pvfs_ref, credentials, PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

/*
 * Find the PVFS handle to an object (file, dir sym) 
 * assumes an absoluate path
 */
int iocommon_lookup_absolute(const char *abs_path,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size)
{
    int rc = 0;
    char pvfs_path[256];
    PVFS_fs_id lookup_fs_id;
    PVFS_credentials *credentials;
    PVFS_sysresp_lookup resp_lookup;

    /* Initialize any variables */
    memset(&resp_lookup, 0, sizeof(resp_lookup));

    pvfs_sys_init();
    iocommon_cred(&credentials);

    /* Determine the fs_id and pvfs_path */
    rc = PVFS_util_resolve(abs_path, &lookup_fs_id, pvfs_path, 256);
    IOCOMMON_CHECK_ERR(rc);

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

    rc = PVFS_sys_lookup(lookup_fs_id, pvfs_path,
                         credentials, &resp_lookup,
                         PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    IOCOMMON_CHECK_ERR(rc);
    *ref = resp_lookup.ref;

errorout:
    return rc;
}

/*
 * Lookup a file via the PVFS system interface
 */
int iocommon_lookup_relative(const char *rel_path,
                             PVFS_object_ref parent_ref, /* by value */
                             int follow_links,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size)
{
    int rc = 0;
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

    /* Contact server */
    rc = PVFS_sys_ref_lookup(parent_ref.fs_id,
                             (char*)rel_path,
                             parent_ref,
                             credentials,
                             &resp_lookup,
                             follow_links,
                             PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);
    *ref = resp_lookup.ref;

errorout:
    return rc;
}

/*
 * Create a file via the PVFS system interface
 */
int iocommon_create_file(const char *filename,
                         mode_t mode,
                         PVFS_hint file_creation_param,
                         PVFS_object_ref parent_ref,
                         PVFS_object_ref *ref )
{
    int rc = 0;
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
        PINT_dist_free(dist);
    }
    return rc;
}


/* pvfs_open implementation, return file info in fd */
/* assumes path is fully qualified */
/* if pdir is not NULL, it is the parent directory */
pvfs_descriptor *iocommon_open(const char *pathname, int flags,
                               PVFS_hint file_creation_param,
                               mode_t mode,
                               PVFS_object_ref *pdir)
{
    int rc = 0;
    int follow_link;
    char *directory = NULL;
    char *filename = NULL;
    char error_path[256];
    PVFS_object_ref file_ref;
    PVFS_object_ref parent_ref;
    int fs_id = 0;
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
    rc = split_pathname(pathname, 0, &directory, &filename);
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
        rc = iocommon_lookup_absolute(directory, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (directory)
        {
            rc = iocommon_lookup_relative(directory,
                                          *pdir,
                                          follow_link,
                                          &parent_ref,
                                          NULL,
                                          0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            parent_ref = *pdir;
        }
    }

    /* An open procedure safe for multiprocessing */

    /* Attempt to find file */
    rc = iocommon_lookup_relative(filename,
                                  parent_ref,
                                  follow_link,
                                  &file_ref,
                                  error_path,
                                  sizeof(error_path));
    if ((rc == 0) && (flags & O_EXCL) && (flags & O_CREAT))
    {
        /* File was found but EXCLUSIVE so fail */
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
            pd = pvfs_alloc_descriptor(&pvfs_ops);
            pd->is_in_use = PVFS_FS;    /* indicate fd is valid! */
            pd->true_fd = rc;
            pd->flags = flags;           /* open flags */
            fstat(rc, &sbuf);
            pd->mode = sbuf.st_mode;
            goto errorout; /* not really an error, but bailing out */
        }
        if (errno != ENOENT || !(flags & O_CREAT))
        {
            /* either file not found and no create flag */
            /* or some other error */
            goto errorout;
        }
        /* file not found but create flag */
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
    pd = pvfs_alloc_descriptor(&pvfs_ops);
    pd->pvfs_ref = file_ref;
    pd->flags = flags;           /* open flags */
    pd->is_in_use = PVFS_FS;    /* indicate fd is valid! */

    /* Get the file's type information from its attributes */
    rc = PVFS_sys_getattr(pd->pvfs_ref,
                          PVFS_ATTR_SYS_ALL_NOHINT,
                          credentials,
                          &attributes_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);
    pd->mode = attributes_resp.attr.perms; /* this may change */

    if (attributes_resp.attr.objtype & PVFS_TYPE_METAFILE)
    {
        pd->mode |= S_IFREG;
    }
    if (attributes_resp.attr.objtype & PVFS_TYPE_DIRECTORY)
    {
        pd->mode |= S_IFDIR;
    }
    if (attributes_resp.attr.objtype & PVFS_TYPE_SYMLINK)
    {
        pd->mode |= S_IFLNK;
    }

    /* Truncate the file if neccesary */
    if (flags & O_TRUNC)
    {
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
    PVFS_credentials *credentials;

    pvfs_sys_init();
    iocommon_cred(&credentials);
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

    switch(whence)
    {
        case SEEK_SET:
        {
            pd->file_pointer = (offset * unit_size);
            break;
        }
        case SEEK_CUR:
        {
            pd->file_pointer += (offset * unit_size);
            break;
        }
        case SEEK_END:
        {
            PVFS_credentials *credentials;
            PVFS_sysresp_getattr attributes_resp;

            memset(&attributes_resp, 0, sizeof(attributes_resp));
            iocommon_cred(&credentials);
            /* Get the file's size in bytes as the ending offset */
            rc = PVFS_sys_getattr(pd->pvfs_ref,
                                  PVFS_ATTR_SYS_SIZE,
                                  credentials,
                                  &attributes_resp,
                                  NULL);
            IOCOMMON_CHECK_ERR(rc);
            pd->file_pointer = attributes_resp.attr.size + (offset * unit_size);
            break;
        }
        default:
        {
            errno = EINVAL;
            goto errorout;
        }
    }
    /* if this is a directory adjust token, the hard way */
    if (S_ISDIR(pd->mode))
    {
        int dirent_no;
        PVFS_credentials *credentials;
        PVFS_sysresp_readdir readdir_resp;

        memset(&readdir_resp, 0, sizeof(readdir_resp));
        iocommon_cred(&credentials);
        dirent_no = pd->file_pointer / sizeof(PVFS_dirent);
        pd->file_pointer = dirent_no * sizeof(PVFS_dirent);
        pd->token = PVFS_READDIR_START;
        if(dirent_no)
        {
            rc = PVFS_sys_readdir(pd->pvfs_ref,
                                  pd->token,
                                  dirent_no,
                                  credentials,
                                  &readdir_resp,
                                  NULL);
            IOCOMMON_CHECK_ERR(rc);
            pd->token = readdir_resp.token;
            free(readdir_resp.dirent_array);
        }
    }
    return pd->file_pointer;

errorout:
    return -1;
}

/**
 * implements unlink and rmdir
 * 
 * dirflag indicates trying to remove a dir (rmdir)
 */
int iocommon_remove (const char *pathname,
                     PVFS_object_ref *pdir, 
                     int dirflag)
{
    int rc = 0;
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

    rc = split_pathname(pathname, dirflag, &parentdir, &file);
    IOCOMMON_RETURN_ERR(rc);

    if (!pdir)
    {
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
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
    /* WBL - What is going on here ??? */
    rc = iocommon_lookup_relative(file, parent_ref,
                PVFS2_LOOKUP_LINK_NO_FOLLOW, &file_ref, NULL, 0);
    if (rc < 0)
    {
        goto errorout;
    }
    rc = iocommon_getattr(file_ref, &attr, PVFS_ATTR_SYS_TYPE);
    IOCOMMON_RETURN_ERR(rc);

    if ((attr.objtype & PVFS_TYPE_DIRECTORY) && !dirflag)
    {
        errno = EISDIR;
        goto errorout;
    }
    else if (!(attr.objtype & PVFS_TYPE_DIRECTORY) && dirflag)
    {
        errno = ENOTDIR;
        goto errorout;
    }

    /* should check to see if any process has file open */
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
int iocommon_unlink(const char *pathname,
                    PVFS_object_ref *pdir)
{
    return iocommon_remove(pathname, pdir, 0);
}

/**
 * wrapper for rmdir
 */
int iocommon_rmdir(const char *pathname,
                   PVFS_object_ref *pdir)
{
    return iocommon_remove(pathname, pdir, 1);
}

/* if dir(s) are NULL, assume name is absolute */
int iocommon_rename(PVFS_object_ref *oldpdir, const char *oldpath,
                    PVFS_object_ref *newpdir, const char *newpath)
{
    int rc = 0;
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
        rc = iocommon_lookup_absolute(olddir, &oldref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (olddir)
        {
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
        rc = iocommon_lookup_absolute(newdir, &newref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (newdir)
        {
            rc = iocommon_lookup_relative(newdir, *newpdir,
                            PVFS2_LOOKUP_LINK_FOLLOW, &newref, NULL, 0);
            IOCOMMON_RETURN_ERR(rc);
        }
        else
        {
            newref = *newpdir;
        }
    }
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

/** do a blocking read or write
 * 
 */
int iocommon_readorwrite(enum PVFS_io_type which,
                         pvfs_descriptor *pd,
                         PVFS_size offset,
                         void *buf,
                         PVFS_Request mem_req,
                         PVFS_Request file_req)
        //returned by nonblocking operations
{
    int rc = 0;
    PVFS_credentials *creds;
    PVFS_sysresp_io io_resp;

    /* Initialize */
    memset(&io_resp, 0, sizeof(io_resp));

    if (!pd)
    {
        errno = EBADF;
        return -1;
    }

    //Ensure descriptor is used for the correct type of access
    if (which==PVFS_IO_READ && (O_WRONLY == (pd->flags & O_ACCMODE)) ||
        which==PVFS_IO_WRITE && (O_RDONLY == (pd->flags & O_ACCMODE)))
    {
        errno = EBADF;
        return -1;
    }

    iocommon_cred(&creds);

    rc = PVFS_sys_io(pd->pvfs_ref,
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
    PVFS_Request contig_memory_req;
    PVFS_credentials *credentials;
    PVFS_size req_size;

    //Ensure descriptor is used for the correct type of access
    if (which==PVFS_IO_READ && (O_WRONLY == (pd->flags & O_ACCMODE)) ||
        which==PVFS_IO_WRITE && (O_RDONLY == (pd->flags & O_ACCMODE)))
    {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    //Create the memory request of a contiguous region: 'mem_req' x count
    rc = PVFS_Request_contiguous(count, etype_req, &contig_memory_req);

    iocommon_cred(&credentials);

    rc = PVFS_isys_io(pd->pvfs_ref,
                      file_req,
                      pd->file_pointer+extra_offset,
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
    pd->file_pointer += req_size;
    *ret_memory_req = contig_memory_req;
    return 0;

errorout:
    return rc;
}

int iocommon_getattr(PVFS_object_ref obj, PVFS_sys_attr *attr, uint32_t mask)
{
    int                  rc = 0;
    PVFS_credentials     *credentials;
    PVFS_sysresp_getattr getattr_response;

    /* Initialize */
    memset(&getattr_response, 0, sizeof(getattr_response));

    /* check credentials */
    iocommon_cred(&credentials);

    /* now get attributes */
    rc = PVFS_sys_getattr(obj,
                          mask,
                          credentials,
                          &getattr_response, NULL);
    IOCOMMON_CHECK_ERR(rc);
    *attr = getattr_response.attr;

errorout:
    return rc;
}

int iocommon_setattr(PVFS_object_ref obj, PVFS_sys_attr *attr)
{
    int                  rc = 0;
    PVFS_credentials     *credentials;

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
    int                  rc = 0;
    PVFS_sys_attr        attr;

    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    rc = iocommon_getattr(pd->pvfs_ref, &attr, mask);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    buf->st_dev = pd->pvfs_ref.fs_id;
    buf->st_ino = pd->pvfs_ref.handle;
    buf->st_mode = attr.perms;
    if (attr.objtype & PVFS_TYPE_METAFILE)
    {
        buf->st_mode |= S_IFREG;
    }
    if (attr.objtype & PVFS_TYPE_DIRECTORY)
    {
        buf->st_mode |= S_IFDIR;
    }
    if (attr.objtype & PVFS_TYPE_SYMLINK)
    {
        buf->st_mode |= S_IFLNK;
    }
    buf->st_nlink = 1; /* PVFS does not allow hard links */
    buf->st_uid = attr.owner;
    buf->st_gid = attr.group;
    buf->st_rdev = 0; /* no dev special files */
    buf->st_size = attr.size;
    buf->st_blksize = attr.blksize;
    buf->st_blocks = 0; /* don't have blocks at this time */
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
    int                  rc = 0;
    PVFS_sys_attr        attr;

    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    rc = iocommon_getattr(pd->pvfs_ref, &attr, mask);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    buf->st_dev = pd->pvfs_ref.fs_id;
    buf->st_ino = pd->pvfs_ref.handle;
    buf->st_mode = attr.perms;
    if (attr.objtype & PVFS_TYPE_METAFILE)
    {
        buf->st_mode |= S_IFREG;
    }
    if (attr.objtype & PVFS_TYPE_DIRECTORY)
    {
        buf->st_mode |= S_IFDIR;
    }
    if (attr.objtype & PVFS_TYPE_SYMLINK)
    {
        buf->st_mode |= S_IFLNK;
    }
    buf->st_nlink = 1; /* PVFS does not allow hard links */
    buf->st_uid = attr.owner;
    buf->st_gid = attr.group;
    buf->st_rdev = 0; /* no dev special files */
    buf->st_size = attr.size;
    buf->st_blksize = attr.blksize;
    buf->st_blocks = 0; /* don't have blocks at this time */
    buf->st_atime = attr.atime;
    buf->st_mtime = attr.mtime;
    buf->st_ctime = attr.ctime;

errorout:
    return rc;
}

int iocommon_chown(pvfs_descriptor *pd, uid_t owner, gid_t group)
{
    int                  rc = 0;
    PVFS_sys_attr        attr;

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

    rc = iocommon_setattr(pd->pvfs_ref, &attr);
    return rc;
}

int iocommon_chmod(pvfs_descriptor *pd, mode_t mode)
{
    int                  rc = 0;
    PVFS_sys_attr        attr;

    /* Initialize */
    memset(&attr, 0, sizeof(attr));

    attr.perms = mode & 07777; /* mask off any stray bits */
    attr.mask = PVFS_ATTR_SYS_PERM;

    rc = iocommon_setattr(pd->pvfs_ref, &attr);
    return rc;
}

iocommon_make_directory(const char *pvfs_path,
                        const int mode,
                        PVFS_object_ref *pdir)
{
    int rc = 0;
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
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
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
    int                  rc = 0;
    PVFS_sys_attr        attr;

    /* Initialize any variables */
    memset(&attr, 0, sizeof(attr));

    rc = iocommon_getattr(pd->pvfs_ref, &attr, PVFS_ATTR_SYS_TYPE | 
                                               PVFS_ATTR_SYS_LNK_TARGET);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    if (attr.objtype & PVFS_TYPE_SYMLINK)
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
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
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

int iocommon_getdents(pvfs_descriptor *pd,
                      struct dirent *dirp,
                      unsigned int count)
{
    int rc = 0;
    int name_max;
    PVFS_credentials *credentials;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_ds_position token;
    int bytes = 0, i = 0;

    if (pd->token == PVFS_READDIR_END)
    {
        return -1;  /* EOF */
    }

    if (!S_ISDIR(pd->mode))
    {
        errno = ENOENT;
        return -1;
    }

    /* Initialize */
    memset(&readdir_resp, 0, sizeof(readdir_resp));

    iocommon_cred(&credentials);

    token = pd->token == 0 ? PVFS_READDIR_START : pd->token;

    rc = PVFS_sys_readdir(pd->pvfs_ref,
                          token,
                          count,
                          credentials,
                          &readdir_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

    pd->token = readdir_resp.token;
    name_max = PVFS_util_min(NAME_MAX, PVFS_NAME_MAX);
    while(readdir_resp.pvfs_dirent_outcount--)
    {
        /* copy a PVFS_dirent to a struct dirent */
        dirp->d_ino = (long)readdir_resp.dirent_array[i].handle;
        dirp->d_off = pd->file_pointer;
        dirp->d_reclen = sizeof(PVFS_dirent);
        memcpy(dirp->d_name, readdir_resp.dirent_array[i].d_name, name_max);
        dirp->d_name[name_max] = 0;
        pd->file_pointer += sizeof(PVFS_dirent);
        bytes += sizeof(PVFS_dirent);
    }
    free(readdir_resp.dirent_array);
    return bytes;

errorout:
    return rc;
}

/** Read entries from a directory.
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
/** Read entries from a directory and their associated attributes
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

int iocommon_access(const char *pvfs_path,
                    const int mode,
                    const int flags,
                    PVFS_object_ref *pdir)
{
    int rc = 0;
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
        rc = iocommon_lookup_absolute(parentdir, &parent_ref, NULL, 0);
        IOCOMMON_RETURN_ERR(rc);
    }
    else
    {
        if (parentdir)
        {
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
    rc = iocommon_lookup_relative(file,
                                  parent_ref,
                                  followflag,
                                  &file_ref,
                                  NULL,
                                  0);
    IOCOMMON_CHECK_ERR(rc);
    /* Get file atributes */
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
    int block_size = 2*1024*1024; /* optimal transfer size 2M */
    PVFS_credentials *credentials;
    PVFS_sysresp_statfs statfs_resp;
    
    /* Initialize the system interface for this process */
    pvfs_sys_init();
    iocommon_cred(&credentials);
    memset(&statfs_resp, 0, sizeof(statfs_resp));

    rc = PVFS_sys_statfs(pd->pvfs_ref.fs_id,
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
    int block_size = 2*1024*1024; /* optimal transfer size 2M */
    PVFS_credentials *credentials;
    PVFS_sysresp_statfs statfs_resp;
    
    /* Initialize the system interface for this process */
    pvfs_sys_init();
    iocommon_cred(&credentials);
    memset(&statfs_resp, 0, sizeof(statfs_resp));

    rc = PVFS_sys_statfs(pd->pvfs_ref.fs_id,
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
                      off64_t offset, size_t count)
{
    int rc = 0, bytes_read;
    PVFS_Request mem_req, file_req;
    PVFS_credentials *credentials;
    char *buffer;
    int buffer_size;

    buffer = (char *)malloc(buffer_size);

    PVFS_Request_contiguous(buffer_size, PVFS_BYTE, &mem_req);
    file_req = PVFS_BYTE;

    rc = iocommon_readorwrite(PVFS_IO_READ, pd, offset + bytes_read,
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
        rc = iocommon_readorwrite(PVFS_IO_READ, pd, offset + bytes_read,
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
        return bytes_read;
    }
}

/**
 * This is a routine to read extended attributes
 */
int iocommon_geteattr(PVFS_object_ref obj,
                      char *key_p,
                      void *val_p,
                      int size)
{
    int                  rc = 0;
    PVFS_credentials     *credentials;
    PVFS_ds_keyval       key, val;

    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));

    /* check credentials */
    iocommon_cred(&credentials);

    key.buffer = key_p;
    key.buffer_sz = strlen(key_p);
    val.buffer = val_p;
    val.buffer_sz = size;

    /* now get attributes */
    rc = PVFS_sys_geteattr(obj,
                          credentials,
                          &key,
                          &val,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);
    rc = val.read_sz;

errorout:
    return rc;
}

/**
 * This is a routine to write extended attributes
 */
int iocommon_seteattr(PVFS_object_ref obj,
                      char *key_p,
                      void *val_p,
                      int size,
                      int flag)
{
    int                  rc = 0;
    int                  pvfs_flag = 0;
    PVFS_credentials     *credentials;
    PVFS_ds_keyval       key, val;

    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));

    /* check credentials */
    iocommon_cred(&credentials);

    key.buffer = key_p;
    key.buffer_sz = strlen(key_p);
    val.buffer = val_p;
    val.buffer_sz = size;

    if (flag & XATTR_CREATE)
    {
        pvfs_flag |= PVFS_XATTR_CREATE;
    }
    if (flag & XATTR_REPLACE)
    {
        pvfs_flag |= PVFS_XATTR_REPLACE;
    }

    /* now set attributes */
    rc = PVFS_sys_seteattr(obj,
                          credentials,
                          &key,
                          &val,
                          pvfs_flag,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

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

