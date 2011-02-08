#include <usrint.h>

int init=0;

/* Perform PVFS initialization if not already finished */
void iocommon_ensure_init()
{

    /* Initialize the file system with mount points */
    int ret;
    if (!init){
        ret = PVFS_util_init_defaults();
        assert(ret==PVFS_FD_SUCCESS);
        init=1;
    }

}

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
    PVFS_credentials *credentials;
    iocommon_cred(&credentials);
    return PVFS_sys_flush(pd->pvfs_ref, credentials, PVFS_HINT_NULL);
}

/*
 * Find the PVFS handle to an object (file, dir sym) 
 * assumes an absoluate path
 */
int iocommon_lookup_absolute( const char *abs_path, PVFS_object_ref *ref)
{
    int rc;
    char pvfs_path[256];
    PVFS_fs_id lookup_fs_id;

    /* Determine the fs_id and pvfs_path */
    rc = PVFS_util_resolve(abs_path, &lookup_fs_id, pvfs_path, 256);

    if (0 == rc)
    {
        PVFS_credentials *credentials;
        PVFS_sysresp_lookup resp_lookup;

        iocommon_cred(&credentials);
        rc = PVFS_sys_lookup(lookup_fs_id, pvfs_path,
                             credentials, &resp_lookup,
                             PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        *ref = resp_lookup.ref;
    }
    else
    {
        fprintf(stderr, "Error: No matching fstab entry for %s\n", abs_path);
    }

    return rc;
}

/*
 * Lookup a file via the PVFS system interface
 */
int iocommon_lookup_relative(const char *rel_path,
                             PVFS_object_ref parent_ref, /* by value */
                             int follow_links,
                             PVFS_object_ref *ref )
{
    int rc;
    PVFS_credentials *credentials;
    PVFS_sysresp_lookup resp_lookup;

    /* Set credentials */
    iocommon_cred(&credentials);

    /* Contact server */
    rc = PVFS_sys_ref_lookup(parent_ref.fs_id,
                             (char*)rel_path,
                             parent_ref,
                             credentials,
                             &resp_lookup,
                             follow_links,
                             PVFS_HINT_NULL);
    *ref = resp_lookup.ref;

    return rc;
}

/*
 * Create a file via the PVFS system interface
 */
int iocommon_create_file( const char *filename,
                             mode_t file_permission,
                             PVFS_hint file_creation_param,
                             PVFS_object_ref parent_ref,
                             PVFS_object_ref *ref )
{
    int rc;
    mode_t mode_mask;
    mode_t user_mode;
    PVFS_sys_attr attributes;
    PVFS_credentials *credentials;
    PVFS_sysresp_create resp_create;

    /* Create distribution var */
    PVFS_sys_dist *dist=NULL;

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

    /* Set attributes */
    memset(&attributes, 0, sizeof(attributes));
    attributes.owner = getuid();
    attributes.group = getgid();
    attributes.atime = time(NULL);
    attributes.mtime = attributes.atime;
    attributes.ctime = attributes.atime;
    attributes.mask = PVFS_ATTR_SYS_ALL_SETABLE;

#if 0
    if (file_creation_param.striping_factor > 0){
        attributes.dfile_count = file_creation_param.striping_factor;
        attributes.mask |= PVFS_ATTR_SYS_DFILE_COUNT;
    }
#endif

    /* Extract the users umask (and restore it to the original value) */
    mode_mask = umask(0);
    umask(mode_mask);
    user_mode = file_permission & ~mode_mask;

    /* Set file permissions */
    if (user_mode & S_IXOTH)
    {
        attributes.perms |= PVFS_O_EXECUTE;
    }
    if (user_mode & S_IWOTH)
    {
        attributes.perms |= PVFS_O_WRITE;
    }
    if (user_mode & S_IROTH)
    {
        attributes.perms |= PVFS_O_READ;
    }
    if (user_mode & S_IXGRP)
    {
        attributes.perms |= PVFS_G_EXECUTE;
    }
    if (user_mode & S_IWGRP)
    {
        attributes.perms |= PVFS_G_WRITE;
    }
    if (user_mode & S_IRGRP)
    {
        attributes.perms |= PVFS_G_READ;
    }
    if (user_mode & S_IXUSR)
    {
        attributes.perms |= PVFS_U_EXECUTE;
    }
    if (user_mode & S_IWUSR)
    {
        attributes.perms |= PVFS_U_WRITE;
    }
    if (user_mode & S_IRUSR)
    {
        attributes.perms |= PVFS_U_READ;
    }

    /* Set credentials */
    iocommon_cred(&credentials);

    /* Contact server */
    rc = PVFS_sys_create((char*)filename,
                         parent_ref,
                         attributes,
                         credentials,
                         dist,
                         &resp_create,
                         NULL,
                         NULL);
    *ref = resp_create.ref;

    if (dist) PINT_dist_free(dist);

    return rc;
}


/* pvfs_open implementation, return file info in fd */
/* assumes path is fully qualified */
/* if pdir is not NULL, it is the parent directory */
pvfs_descriptor *iocommon_open(const char *pathname, int flag,
                               PVFS_hint file_creation_param,
                               mode_t file_permission,
                               PVFS_object_ref *pdir)
{
    int rc;
    int follow_link;
    char *directory;
    char *filename;
    PVFS_object_ref file_ref;
    PVFS_object_ref parent_ref;
    int fs_id = 0;
    pvfs_descriptor *pd = NULL; /* invalid pd until file is opened */

    /* Split the path into a directory and file */
    rc = split_pathname(pathname, &directory, &filename);
    if (0 != rc && (0 == directory || 0 == filename))
    {
        errno = ENOMEM;
        return pd;
    }
    else if (rc != 0)
    {
        fprintf(stderr, "Error: %s is not a legal PVFS path.\n", pathname);
        errno = EACCES;
        return pd;
    }

    /* Check the flag to determine if links are followed */
    if (flag & O_NOFOLLOW)
    {
        follow_link = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    }
    else
    {
        follow_link = PVFS2_LOOKUP_LINK_FOLLOW;
    }

    /* Get reference for the parent directory */
    rc = iocommon_lookup_absolute(directory, &parent_ref);
    if (0 == rc)
    {

        /* An open procedure safe for multiprocessing */

        //Attempt to find file
        rc = iocommon_lookup_relative(filename, parent_ref, follow_link, &file_ref);

        //File was found
        if (rc==0){
            //if EXCLUSIVE, fail
            if ((flag & O_EXCL) && (flag & O_CREAT)){
                return pd;
            }
        }
        //File wasn't found
        else {
            //create file?
            if (flag & O_CREAT){
                rc = iocommon_create_file(filename, file_permission, file_creation_param, parent_ref, &file_ref);
                //create failed, the file must have been created by a different process
                if (rc){
                    //get existing handle
                    rc = iocommon_lookup_relative(filename, parent_ref, follow_link, &file_ref);
                }
            }
        }
    }
    else
    {
        errno = ENOTDIR;
        return pd;
    }

    /* Free directory and filename memory */
    free(directory);
    free(filename);

    /* Translate the pvfs reference into a file descriptor */
    if (0 == rc)
    {
       /* Set the file information */
       /* create fd object */
       pd = pvfs_alloc_descriptor(&pvfs_ops);
       pd->pvfs_ref = file_ref;
       pd->flags = flag;
       pd->is_in_use = 1;    //indicate fd is valid!
    }
    else
    {
        /* Inidicate that an error occurred */
        errno = EACCES;
        return pd;
    }

    /* Truncate the file if neccesary */
    if (flag & O_TRUNC)
    {
        PVFS_credentials *credentials;
        iocommon_cred(&credentials);
        PVFS_sys_truncate(file_ref, 0, credentials, NULL);
    }

    /* Move to the end of file if necessary */
    if (flag & O_APPEND)
        iocommon_lseek(pd, 0, 0, SEEK_END);

    return pd;
}

off64_t iocommon_lseek(pvfs_descriptor *pd, off64_t offset,
            PVFS_size unit_size, int whence)
{

    if (0 == pd)
    {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    switch(whence)
    {
        case SEEK_SET:
        {
            pd->file_pointer = offset*unit_size;
            break;
        }
        case SEEK_CUR:
        {
            pd->file_pointer += offset*unit_size;
            break;
        }
        case SEEK_END:
        {
            PVFS_credentials *credentials;
            PVFS_sysresp_getattr attributes_resp;

            /* Construct credentials*/
            iocommon_cred(&credentials);

            /* Get the file's size in bytes as the ending offset */
            PVFS_sys_getattr(pd->pvfs_ref, PVFS_ATTR_SYS_SIZE,
                             credentials, &attributes_resp, NULL);

            pd->file_pointer = attributes_resp.attr.size + offset*unit_size;
            break;
        }
        default:
        {
            errno = EINVAL;
            return PVFS_FD_FAILURE;
        }
    }
    return pd->file_pointer;
}

/*
 * pvfs_unlink implementation
 * need to verify this is a file or symlink
 * use rmdir for directory
 */
int iocommon_remove (const char *pathname, int dirflag) 
{
    int rc = 0;
    char *parentdir = 0;
    char *file = 0;
    PVFS_object_ref parent_ref;
    PVFS_credentials *credentials;
    PVFS_sys_attr attr;

    /* Initialize the system interface for this process */
    iocommon_ensure_init();
    iocommon_cred(&credentials);

    if (0 == rc)
    {
        rc = split_pathname(pathname, &parentdir, &file);
    }

    if (0 == rc)
    {
        rc = iocommon_lookup_absolute(parentdir, &parent_ref);
    }
    /* need to verify this is a file or symlink */
    /* WBL - What is going on here ??? */
    iocommon_lookup_relative(parentdir, parent_ref,
                PVFS2_LOOKUP_LINK_NO_FOLLOW, &parent_ref);
    iocommon_getattr(parent_ref, &attr);
    if ((attr.objtype & PVFS_TYPE_DIRECTORY) && dirflag)
    {
        errno = EISDIR;
        return -1;
    }
    else if (!(attr.objtype & PVFS_TYPE_DIRECTORY) && !dirflag)
    {
        errno = ENOTDIR;
        return -1;
    }

    if (0 == rc)
    {
        rc = PVFS_sys_remove(file, parent_ref, credentials, PVFS_HINT_NULL);
    }

    free(parentdir);
    free(file);
    if (0 != rc)
    {
        return -1;
    }
    return 0;
}

int iocommon_unlink(const char *pathname)
{
    return iocommon_remove(pathname, 1);
}

int iocommon_rmdir(const char *pathname)
{
    return iocommon_remove(pathname, 0);
}

/* if dir(s) are NULL, assume name is absolute */
int iocommon_rename(pvfs_descriptor *olddir, const char *oldname,
                    pvfs_descriptor *newdir, const char *newname)
{
    int rc;
    char *oldent, *newent, *oldpath, *newpath;
    PVFS_object_ref oldref, newref;
    PVFS_credentials *creds;
    PVFS_hint hints = PVFS_HINT_NULL;

    iocommon_cred(&creds);
    if (olddir)
    {
        /* do relative lookup */
    }
    else
    {
        /* do absolute lookup */
        rc = split_pathname(oldname, &oldpath, &oldent);
        rc = iocommon_lookup_absolute(oldpath, &oldref);
    }
    if (newdir)
    {
        /* do relative lookup */
    }
    else
    {
        /* do absolute lookup */
        rc = split_pathname(newname, &newpath, &newent);
        rc = iocommon_lookup_absolute(newpath, &newref);
    }
    rc = PVFS_sys_rename(oldent, oldref, newent, newref, creds, hints);
    return rc;
}

/* do a blocking read or write
 * extra_offset = extra padding to the pd's offset, independent of the pd's offset */
int iocommon_readorwrite( enum PVFS_io_type which,
        pvfs_descriptor *pd, PVFS_size offset, void *buf,
        PVFS_Request etype_req, PVFS_Request file_req, size_t count)
        //returned by nonblocking operations
{
        int rc;
        PVFS_Request contig_memory_req;
        PVFS_credentials *creds;
        PVFS_sysresp_io read_resp;
        PVFS_size req_size;

        memset(&contig_memory_req, 0, sizeof(PVFS_Request));

        //Ensure descriptor is used for the correct type of access
        if (which==PVFS_IO_READ && (O_WRONLY & pd->flags)){
            errno = EBADF;
            return PVFS_FD_FAILURE;
        }
        else if (which==PVFS_IO_WRITE && (O_RDONLY == (pd->flags & O_ACCMODE)))
        {
            errno = EBADF;
            return PVFS_FD_FAILURE;
        }

        /* Create the memory request of a contiguous region: 'mem_req' x count  */
        rc = PVFS_Request_contiguous(count, etype_req, &contig_memory_req);

        iocommon_cred(&creds);

           rc = PVFS_sys_io(pd->pvfs_ref, file_req, offset, buf,
                            contig_memory_req, creds, &read_resp,
                            which, PVFS_HINT_NULL);

        if (0 != rc)
        {
            errno = EIO;
            return PVFS_FD_FAILURE;
        }

        PVFS_Request_size(contig_memory_req, &req_size);
        pd->file_pointer += req_size;

        PVFS_Request_free(&contig_memory_req);
        return PVFS_FD_SUCCESS;
}

/*
 * [Do a nonblocking read or write]
 * extra_offset = extra padding to the pd's offset,
 * independent of the pd's offset
 * Returns an op_id, response, and ret_mem_request
 * (which represents an etype_req*count region)
 * Note that the none of the PVFS_Requests are freed
 */
int iocommon_ireadorwrite( enum PVFS_io_type which,
        pvfs_descriptor *pd, PVFS_size extra_offset, void *buf,
        PVFS_Request etype_req, PVFS_Request file_req, size_t count,
        PVFS_sys_op_id *ret_op_id, PVFS_sysresp_io *ret_resp,
        PVFS_Request *ret_memory_req)
{
        int rc;
        PVFS_Request contig_memory_req;
        PVFS_credentials *credentials;
        PVFS_size req_size;

        //Ensure descriptor is used for the correct type of access
        if (which==PVFS_IO_READ && (O_WRONLY & pd->flags)){
            errno = EBADF;
            return PVFS_FD_FAILURE;
        }
        else if (which==PVFS_IO_WRITE && (O_RDONLY == (pd->flags & O_ACCMODE)))
        {
            errno = EBADF;
            return PVFS_FD_FAILURE;
        }

        //Create the memory request of a contiguous region: 'mem_req' x count
        rc = PVFS_Request_contiguous(count, etype_req, &contig_memory_req);

        iocommon_cred(&credentials);

        rc = PVFS_isys_io(pd->pvfs_ref, file_req,
                          pd->file_pointer+extra_offset,
                          buf, contig_memory_req,
                          credentials,
                          ret_resp,
                          which,
                          ret_op_id, PVFS_HINT_NULL, NULL);

        assert(*ret_op_id!=-1);//TODO: handle this

        if (rc!=0){
            errno = EIO;
            return PVFS_FD_FAILURE;
        }

        PVFS_Request_size(contig_memory_req, &req_size);
        pd->file_pointer += req_size;

        *ret_memory_req = contig_memory_req;

        return PVFS_FD_SUCCESS;
}

int iocommon_getattr(PVFS_object_ref obj, PVFS_sys_attr *attr)
{
    int                  ret = 0;
    PVFS_credentials     *credentials;
    PVFS_sysresp_getattr getattr_response;

    /* check credentials */
    iocommon_cred(&credentials);

    /* now get attributes */
    ret = PVFS_sys_getattr(obj,
                           PVFS_ATTR_SYS_ALL_NOHINT,
                           credentials,
                           &getattr_response, NULL);

    *attr = getattr_response.attr;

    if(ret < 0)
    {
        errno = EACCES; /* need to get proper return code */
        return -1;
    }

    return 0;
}

/* WBL - question - should attr not be a pointer */
int iocommon_setattr(PVFS_object_ref obj, PVFS_sys_attr *attr)
{
    int                  ret = 0;
    PVFS_credentials     *credentials;

    /* check credentials */
    iocommon_cred(&credentials);

    /* now get attributes */
    ret = PVFS_sys_setattr(obj, *attr, credentials, NULL);

    if(ret < 0)
    {
        errno = EACCES; /* need to get proper return code */
        return -1;
    }

    return 0;
}

int iocommon_stat(pvfs_descriptor *pd, struct stat *buf)
{
    int                  ret = 0;
    PVFS_sys_attr        attr;

    iocommon_getattr(pd->pvfs_ref, &attr);

    /* copy attributes into standard stat struct */
    buf->st_dev = pd->pvfs_ref.fs_id;
    buf->st_ino = pd->pvfs_ref.handle;
    buf->st_mode = attr.perms;
    if (attr.objtype & PVFS_TYPE_METAFILE)
        buf->st_mode |= S_IFREG;
    if (attr.objtype & PVFS_TYPE_DIRECTORY)
        buf->st_mode |= S_IFDIR;
    if (attr.objtype & PVFS_TYPE_SYMLINK)
        buf->st_mode |= S_IFLNK;
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

    return 0;
}

/*
 * The only difference here is that buf is stat64 which
 * means some of its fields are defined as different types
 */
int iocommon_stat64(pvfs_descriptor *pd, struct stat64 *buf)
{
    int                  ret = 0;
    PVFS_sys_attr        attr;

    iocommon_getattr(pd->pvfs_ref, &attr);

    /* copy attributes into standard stat struct */
    buf->st_dev = pd->pvfs_ref.fs_id;
    buf->st_ino = pd->pvfs_ref.handle;
    buf->st_mode = attr.perms;
    if (attr.objtype & PVFS_TYPE_METAFILE)
        buf->st_mode |= S_IFREG;
    if (attr.objtype & PVFS_TYPE_DIRECTORY)
        buf->st_mode |= S_IFDIR;
    if (attr.objtype & PVFS_TYPE_SYMLINK)
        buf->st_mode |= S_IFLNK;
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

    return 0;
}

int iocommon_chown(pvfs_descriptor *pd, uid_t owner, gid_t group)
{
    int                  ret = 0;
    PVFS_sys_attr        attr;

    if (owner != -1)
        attr.owner = owner;
    if (owner != -1)
        attr.group = group;
    attr.mask = PVFS_ATTR_SYS_UID | PVFS_ATTR_SYS_GID;

    ret = iocommon_setattr(pd->pvfs_ref, &attr);

    return ret;
}

int iocommon_chmod(pvfs_descriptor *pd, mode_t mode)
{
    int                  ret = 0;
    PVFS_sys_attr        attr;

    attr.perms = mode & 07777; /* mask off any stray bits */
    attr.mask = PVFS_ATTR_SYS_PERM;

    ret = iocommon_setattr(pd->pvfs_ref, &attr);

    return ret;
}

iocommon_make_directory(const char *pvfs_path, const int mode)
{
    int ret = 0;
    char parent_dir[PVFS_NAME_MAX] = "";
    char base[PVFS_NAME_MAX]  = "";
    char realpath[PVFS_NAME_MAX]  = "";
    char * parentdir_ptr = NULL;
    char * basename_ptr = NULL;
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

    iocommon_cred(&credentials);

    /*
     * Copy the file name into structures to be passed to dirname and basename
     * These calls change the parameter, so we don't want to mess with original
     */
    strcpy(parent_dir, pvfs_path);
    strcpy(base, pvfs_path);

    parentdir_ptr = dirname(parent_dir);
    basename_ptr  = basename(base);

    /* Make sure we don't try and create the root directory */
    if( strcmp(basename_ptr, "/") == 0 )
    {
        errno = EEXIST;
        return(-1);
    }

    /* lookup parent */
    ret = iocommon_lookup_absolute(parentdir_ptr, &parent_ref);
   
    /* Set the attributes for the new directory */
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    attr.perms = mode;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    /* Clear out any info from previous calls */
    memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));

    ret = PVFS_sys_mkdir(basename_ptr,
                         parent_ref,
                         attr,
                         credentials,
                         &resp_mkdir, NULL);

    if (ret != 0)
    {
        errno = ret;
        return(-1);
    }
   
    return(0);
}

int iocommon_readlink(pvfs_descriptor *pd, char *buf, int size)
{
    int                  ret = 0;
    PVFS_sys_attr        attr;

    iocommon_getattr(pd->pvfs_ref, &attr);

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

    return 0;
}

int iocommon_symlink(const char  *pvfs_path,
                     const char  *link_target)
{
    int ret = 0;
    char parent_dir[PVFS_NAME_MAX] = "";
    char base[PVFS_NAME_MAX]  = "";
    char realpath[PVFS_NAME_MAX]  = "";
    char * parentdir_ptr = NULL;
    char * basename_ptr = NULL;
    PVFS_sys_attr       attr;
    PVFS_object_ref     parent_ref;
    PVFS_sysresp_symlink  resp_symlink;
    PVFS_credentials    *credentials;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_symlink,0, sizeof(resp_symlink));

    iocommon_cred(&credentials);

    /*
     * Copy the file name into structures to be passed to dirname and basename
     * These calls change the parameter, so we don't want to mess with original
     */
    strcpy(parent_dir, pvfs_path);
    strcpy(base,  pvfs_path);

    parentdir_ptr = dirname(parent_dir);
    basename_ptr  = basename(base);

    /* Make sure we don't try and create the root directory */
    if( strcmp(basename_ptr, "/") == 0 )
    {
        errno = EEXIST;
        return(-1);
    }

    /* lookup parent */
    ret = iocommon_lookup_absolute(parentdir_ptr, &parent_ref);
   
    /* Set the attributes for the new directory */
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    attr.perms = 0777;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    /* Clear out any info from previous calls */
    memset(&resp_symlink, 0, sizeof(PVFS_sysresp_symlink));

    ret = PVFS_sys_symlink(basename_ptr,
                           parent_ref,
                           (char *)link_target,
                           attr,
                           credentials,
                           &resp_symlink,
                           NULL);

    if (ret != 0)
    {
        errno = ret;
        return(-1);
    }
   
    return(0);
}

int iocommon_getdents()
{
}

int iocommon_access()
{
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

