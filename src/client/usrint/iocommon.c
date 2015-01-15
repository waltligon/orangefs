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
#include "pvfs-path.h"
#if PVFS_UCACHE_ENABLE
#include "ucache.h"
#include "string.h"
#endif
#include <errno.h>
#include <pint-cached-config.h>

static int iocommon_parse_serverlist(char *serverlist,
                                     struct PVFS_sys_server_list *slist,
                                     PVFS_fs_id fsid);

/** this is a global analog of errno for pvfs specific
 *  errors errno is set to EIO and this is set to the
 *  original code 
 */
int pvfs_errno;

int iocommon_cred(PVFS_credential **credential)
{
    static PVFS_credential creds_buf;
    static int cred_init = 0;
    int rc;

    if(!cred_init)
    {
        memset(&creds_buf, 0, sizeof(creds_buf));
        rc = PVFS_util_gen_credential_defaults(&creds_buf);
        if (rc < 0)
        {
            errno = rc;
            return -1;
        }
        cred_init = 1;
    }
    else
    {
        rc = PVFS_util_refresh_credential(&creds_buf);
        if (rc < 0)
        {
            errno = rc;
            return -1;
        }
    }

    *credential = &creds_buf;
    return 0;
}

int iocommon_fsync(pvfs_descriptor *pd)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credential *credential;

    PVFS_INIT(pvfs_sys_init);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    rc = iocommon_cred(&credential); 
    if (rc != 0)
    {
        goto errorout;
    }
#if PVFS_UCACHE_ENABLE
    if (ucache_enabled)
    {
        rc = ucache_flush_file(pd->s->fent);
        if(rc != 0)
        {
            goto errorout;
        }
    }
#endif
    errno = 0;
    rc = PVFS_sys_flush(pd->s->pvfs_ref, credential, PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    return rc;
}

/**
 * Find the PVFS handle to an object (file, dir sym) 
 * assumes an absoluate path
 */
int iocommon_lookup_absolute(const char *abs_path,
                             int follow_links,
                             PVFS_object_ref *ref,
                             char *error_path,
                             int error_path_size)
{
    int rc = 0;
    int orig_errno = errno;
    int local_path_alloc = 0;
    PVFS_credential *credential;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_path_t *Ppath;

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_lookup_absolute: called with %s\n", abs_path);

    /* Initialize any variables */
    memset(&resp_lookup, 0, sizeof(resp_lookup));

    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    Ppath = PVFS_path_from_expanded((char *)abs_path);
    if (!VALID_PATH_MAGIC(Ppath))
    {
        /* this allocates a PVFS_path which must be
         * freed below since we do not pass it back */
        Ppath = PVFS_new_path(abs_path);
        abs_path = Ppath->expanded_path;
        local_path_alloc = 1;
    }

    if (!(PATH_QUALIFIED(Ppath) || PATH_EXPANDED(Ppath)))
    {
        /* saves original path and returns qualified path */
        /* this does not allocate a PVFS_path because the */
        /* input already is one */
        abs_path = PVFS_qualify_path(abs_path);
    }

    /* Determine the fs_id and pvfs_path */
    errno = 0;
    /**
     * All paths here should be fully resolved and processed
     * to remove extra slashes, dots, and double dots.
     * We could still have symbolic links that change things.
     */
    if (!PATH_RESOLVED(Ppath))
    {
        rc = PVFS_util_resolve_absolute(abs_path);
        if (rc < 0)
        {
            if (rc == -PVFS_ENOENT)
            {
                errno = ESTALE; /* this signals open that resolve failed */
                rc = -1;
                goto errorout;
            }
            if (!PATH_EXPANDED(Ppath))
            {
                char *ret;
                /* try fully expanding the path looking up each segment */
                /* this also should not allocate a PVFS_path */
                ret = PVFS_expand_path(abs_path, !follow_links);
                if (ret && PATH_LOOKEDUP(Ppath))
                {
                    /* the expanding process looks up the final path */
                    /* as a side effect so there is nothing left to do */
                    ref->fs_id = Ppath->fs_id;
                    ref->handle = Ppath->handle;
                }
                else
                {
                    /* there was an error */
                    /* either it failed and we're done */
                    /* or we tried to look up a file we wish to create */
                    /* either way this routine is done it will fall back */
                    rc = -1;
                }
            }
            IOCOMMON_CHECK_ERR(rc);
        }
    }

    /* at this point we should have a resolved path */

    if (PATH_MNTPOINT(Ppath))
    {
        /* looking up the mountpoint, the PVFS root dir */
        /* which is known and stored in configuration */
        ref->fs_id = Ppath->fs_id;
        rc = PINT_cached_config_get_root_handle(Ppath->fs_id,
                                                &ref->handle);
        /* return rc; */
        goto cleanup;
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

    /* let's try to do the lookup */

    errno = 0;
    rc = PVFS_sys_lookup(Ppath->fs_id,
                         Ppath->pvfs_path,
                         credential,
                         &resp_lookup,
                         follow_links,
                         NULL);
    IOCOMMON_CHECK_ERR(rc);
    *ref = resp_lookup.ref;

errorout:
    /* no specific error handling */
cleanup:
    if (local_path_alloc)
    {
        PVFS_free_expanded((char *)abs_path);
    }
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
 *
 * rel_path is relative to PVFS object ref so this shoule
 * all be in PVFS unless a symlink takes us back out of it.
 * Therefore don't need to do anything to the path.
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
    PVFS_credential *credential;
    PVFS_sysresp_lookup resp_lookup;

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_lookup_relative: called with %s\n", rel_path);

    /* Initialize any variables */
    PVFS_INIT(pvfs_sys_init);
    memset(&resp_lookup, 0, sizeof(resp_lookup));

    /* Set credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* if the PVFS lookup hits a symlink out of PVFS space
     * the remaining path will be returned in error_path
     * and it will be handled upstream.
     */

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

    /* a path can be up toPVFS_PATH_MAX long, but a single lookup
     * will only accept up to PVFS_NAME_MAX so this loop takes
     * chunks of the path and if needed uses multiple lookups
     */

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
                                credential,
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
 * Parses a simple string to find the number and select of servers
 * for the LIST layout method
 */
static int iocommon_parse_serverlist(char *serverlist,
                                     struct PVFS_sys_server_list *slist,
                                     PVFS_fs_id fsid)
{
    PVFS_BMI_addr_t *server_array;
    int count;
    char *tok, *save_ptr;
    int i;

    /* expects slist->servers to be NULL */
    if (!slist || slist->servers)
    {
        errno = EINVAL;
        return -1;
    }
    tok = strtok_r(serverlist, ":", &save_ptr);
    if (!tok)
    {
        errno = EINVAL;
        return -1;
    }
    slist->count = atoi(tok);
    PINT_cached_config_count_servers(fsid, PINT_SERVER_TYPE_IO, &count);
    if (slist->count < 1 || slist->count > count)
    {
        errno = EINVAL;
        return -1;
    }
    slist->servers = (PVFS_BMI_addr_t *)malloc(sizeof(PVFS_BMI_addr_t) *
                                                slist->count);
    if (!slist->servers)
    {
        errno = ENOMEM;
        return -1;
    }
    server_array = (PVFS_BMI_addr_t *)malloc(sizeof(PVFS_BMI_addr_t)*count);
    if (!server_array)
    {
        free(slist->servers);
        slist->servers = NULL;
        errno = ENOMEM;
        return -1;
    }
    PINT_cached_config_get_server_array(fsid,
                                        PINT_SERVER_TYPE_IO,
                                        server_array,
                                        &count);
    for (i = 0; i < slist->count; i++)
    {
        tok = strtok_r(NULL, ":", &save_ptr);
        if (!tok || atoi(tok) < 0 || atoi(tok) >= count)
        {
            free(slist->servers);
            slist->servers = NULL;
            free(server_array);
            errno = EINVAL;
            return -1;
        }
        slist->servers[i] = server_array[atoi(tok)];
    }
    free(server_array);
    return 0;
}

/**
 * Create a file via the PVFS system interface
 * parent_ref is a PVFS object so we should be all
 * in PVFS space.
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
    PVFS_credential *credential;
    PVFS_sysresp_create resp_create;
    PVFS_sys_dist *dist = NULL;
    PVFS_sys_layout *layout = NULL;
    PVFS_hint hints = NULL;
#if 0
    PVFS_hint standby_hint; /* We need this if file_creation_param is null. */
#endif /* PVFS_USER_ENV_VARS_ENABLED */

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_create_file: called with %s\n", filename);

    /* Initialize */
    PVFS_INIT(pvfs_sys_init);
    memset(&attr, 0, sizeof(attr));
    memset(&resp_create, 0, sizeof(resp_create));

    attr.owner = geteuid();
    attr.group = getegid();
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;

#if 0
    /* TODO: check for pertinent environment variable hints that should be
     * applied here. Also must check if a regular hint was specified or not.
     * If so then resolve any conflicts (define a policy of which takes
     * precedence) and update the hint.
     * If not, then we must fill in the standby_hint with hint info from the
     * environment variable hints. */
    if(env_vars.env_var_array[ORANGEFS_DIST_NAME]|
       env_vars.env_var_array[ORANGEFS_NUM_DFILES]|... && !file_creation_param)
    {
        printf("pertinent environment variable hint specified"
               " && no hint detected!\n");
    }
#endif /* PVFS_USER_ENV_VARS_ENABLED */
    /* ====================================================================== */

    if (file_creation_param) /* these are hints */
    {
        int length;
        void *value;
        /* check for distribution */
        value = PINT_hint_get_value_by_type(file_creation_param,
                                            PINT_HINT_DISTRIBUTION,
                                            &length);
        if (value)
        {
#if 0
            printf("HINT DETECTED:\n"
                   "\tPINT_HINT_DISTRIBUTION -> value = %s\n", (char *) value);
#endif
            dist = PVFS_sys_dist_lookup((char *)value);
            if (dist)
            {
                value = PINT_hint_get_value_by_type(file_creation_param,
                                                    PINT_HINT_DISTRIBUTION_PV,
                                                    &length);
                /* This should come in as a string here and get tokenized into 
                 * potentially multiple param:value pairs delimited by a '+'.*/
                if(value)
                {
#if 0
                    printf("\tPINT_HINT_DISTRIBUTION_PV -> value = %s\n",
                           (char *) value);
#endif
                    /* Uses inplace iterator */
                    PVFS_dist_pv_pairs_extract_and_add(value, (void *) dist);
                }
            }
            else /* distribution not found */
            {
                rc = EINVAL;
                goto errorout;
            }
        }
        /* check for dfile count */
        value = PINT_hint_get_value_by_type(file_creation_param,
                                            PINT_HINT_DFILE_COUNT,
                                            &length);
        if (value)
        {
            attr.dfile_count = *(int *)value;
            attr.mask |= PVFS_ATTR_SYS_DFILE_COUNT;
        }
        /* check for layout */
        value = PINT_hint_get_value_by_type(file_creation_param,
                                            PINT_HINT_LAYOUT,
                                            &length);
        if (value)
        {
            layout = (PVFS_sys_layout *)malloc(sizeof(PVFS_sys_layout));
            layout->algorithm = *(int *)value;
            layout->server_list.count = 0;
            layout->server_list.servers = NULL;
        }
        /* check for server list */
        value = PINT_hint_get_value_by_type(file_creation_param,
                                            PINT_HINT_SERVERLIST,
                                            &length);
        if (value)
        {
            if(!layout)
            {
                /* serverlist makes no sense without a layout */
                rc = EINVAL;
                goto errorout;
            }
            layout->server_list.count = 0;
            layout->server_list.servers = NULL;
            rc = iocommon_parse_serverlist(value, &layout->server_list,
                                           parent_ref.fs_id);
            if (rc < 0)
            {
                return rc;
            }
        }
        /* check for nocache flag */
        value = PINT_hint_get_value_by_type(file_creation_param,
                                            PINT_HINT_CACHE,
                                            &length);
        if (value)
        {   
            /* this should probably move into the open routine */
        }
        /* look for hints handled on the server */
        if (PVFS_hint_check_transfer(&file_creation_param))
        {
            hints = file_creation_param;
        }
    }

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

    /* Set credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* Contact server */
    errno = 0;
    rc = PVFS_sys_create((char*)filename,
                         parent_ref,
                         attr,
                         credential,
                         dist,
                         &resp_create,
                         layout,
                         hints);
    IOCOMMON_CHECK_ERR(rc);
    *ref = resp_create.ref;

errorout:
    if (dist)
    {
        PVFS_sys_dist_free(dist);
    }
    if (layout)
    {
        free(layout);
    }
    return rc;
}

/**
 * OK we tried to open a file and may have run into a symbolic link that
 * points to NON-PVFS space or something equally weird so we will call
 * PVFS_expand_path which will look up each segment of the path one at a
 * time using either glibc or PVFS as needed.  This is the long slow way
 * but the only way to guarantee it is right.  Most opens should never
 * get to this.
 */
int iocommon_expand_path (PVFS_path_t *Ppath,
                          int follow_flag,
                          int flags,
                          mode_t mode,
                          pvfs_descriptor **pdp)
{
    int rc = 0;
    char *path = NULL;
    pvfs_descriptor *pd = NULL;

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_expand_path: called with %s\n", Ppath->expanded_path);

    path = PVFS_expand_path(Ppath->expanded_path, !follow_flag);
    if (PATH_LOOKEDUP(Ppath) && PATH_RESOLVED(Ppath))
    {
        /* we found a valid PVFS path
         * so we're done - expand path does not set up a descriptor here
         */
        *pdp = NULL;
        goto errorout; /* not really an error, but bailing out */
    }
    if (!PATH_ERROR(Ppath))
    {
        /* glibc file */
        struct stat sbuf;
        /* try to open using glibc */
        rc = (*glibc_ops.open)(path, flags & 01777777, mode);
        IOCOMMON_RETURN_ERR(rc);

        /* create a usrint file descriptor for it */
        gossip_debug(GOSSIP_USRINT_DEBUG,
               "iocommon_expand_path calls pvfs_alloc_descriptor %d\n", rc);
        pd = pvfs_alloc_descriptor(&glibc_ops, rc, NULL, 0);
        pd->is_in_use = PVFS_FS;    /* indicate fd is valid! */
        pd->true_fd = rc;
        pd->s->flags = flags;           /* open flags */

        gen_mutex_unlock(&pd->lock); /* must release before fstat */
        fstat(rc, &sbuf);
        pd->s->mode = sbuf.st_mode;
        if (S_ISDIR(sbuf.st_mode))
        {
            /* we assume path was qualified by PVFS_expand_path() */
            pd->s->dpath = pvfs_dpath_insert(path);
        }
        gen_mutex_unlock(&pd->s->lock); /* this is ok after fstat */

        *pdp = pd;
        goto errorout; /* not really an error, but bailing out */
    }
    rc = Ppath->rc;

errorout:
    /* an error */
    return rc;
}

/**
 * This is done in many of the iocommon calls so this is just a helper
 * function to keep amount of redundant code down.  This routine is used
 * with syscalls other than open and expect the path to be PVFS.
 *
 * pref and fref return object refs to parent and file if the pointer
 * is not NULL.  Some calls only need one or the other.  If filename
 * is not NULL the file name string is returned and must be freed by
 * the caller.  If pdir is not NULL, it is used for a relative lookup
 * otherwise path should be absolute.
 */
int iocommon_lookup(char *path,
                    int followflag,
                    PVFS_object_ref *pref, /* OUT parent ref */
                    PVFS_object_ref *fref, /* OUT file ref */
                    char **filename,       /* OUT text file name */
                    PVFS_object_ref *pdir) /* IN path relative to this */
{
    int rc = 0;
    int internal_follow = PVFS2_LOOKUP_LINK_FOLLOW;
    char *parentdir = NULL;      /* text of parent path */
    char *file = NULL;           /* text of file name */
    char *target = NULL;         /* temp could be parent dir or file name */
    PVFS_object_ref *target_ref; /* temp ref could be dir or file name */
    int skip_file_lookup = 0;    /* case where p and f are req but no p */
    pvfs_descriptor *pd = NULL;
    PVFS_path_t *Ppath = NULL;
    int flags = O_RDONLY;
    int mode = 0644;
    char error_path[PVFS_NAME_MAX];

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_lookup: called with %s\n", path);

    memset(error_path, 0, sizeof(error_path));

    rc = split_pathname(path, &parentdir, &file);
    IOCOMMON_RETURN_ERR(rc);

    /* Four cases here:
     * 1) caller requests pref and fref
     *    look up the parentdir in the first section (via target)
     *    then look up the file in the second section
     * 2) caller requests only fref
     *    look up the file in the first section (via target)
     * 3) caller requests only pref
     *    look up parentdir in the first section (via target)
     * 4) caller requests neither pref nor fref
     *    makes no sense - but can still return filename if requested
     */

    if (!fref && !pref)
    {
        /* fourth case, exit code copies file name if requested */
        goto errorout;
    }
    if (fref && !pref)
    {
        /* second case */
        /* this will use the parent code to do the file lookup */
        /* in one request - unless it fails */
        target = path;
        target_ref = fref;
        internal_follow = followflag;
    }
    else
    {
        /* this is first or third or 4th case */
        /* this is the normal lookup the dir first, file second */
        /* when looking up parent dir, always follow sym links */
        target = parentdir;
        target_ref = pref;
    }
    if (pref || fref)
    {
        /* this is first, second, or third case */
        /* user asked for parent dir so look up dir first */
        if (!pdir)
        {
            /* no relative path was provided */
            errno = 0;
            rc = iocommon_lookup_absolute(target,
                                          internal_follow,
                                          target_ref, /* OUT */
                                          error_path, /* OUT */
                                          sizeof(error_path));
        }
        else
        {
            /* a relative path provided */
            if (!parentdir && pref)
            {
                /* first or third case but no parentdir in path
                 * split_pathname only returns a blank parentdir if
                 * there is only a filename and no slashes.
                 * The parent dir is pdir. Now look up file.
                 */
                target = file;
                target_ref = fref;
                *pref = *pdir;
                internal_follow = followflag;
                skip_file_lookup = 1; /* block the call to lookup_rel below */
            }
            errno = 0;
            rc = iocommon_lookup_relative(target,
                                          *pdir,
                                          internal_follow,
                                          target_ref, /* OUT */
                                          error_path, /* OUT */
                                          sizeof(error_path));
        }
    }
    if (rc == 0 && pref && fref && !skip_file_lookup)
    {
        /* this is first case */
        /* we looked up parent, now look up file */
        errno = 0;
        rc = iocommon_lookup_relative(file,
                                      *pref,
                                      followflag,
                                      fref,       /* OUT */
                                      error_path, /* OUT */
                                      sizeof(error_path));
    }
    /* check to see if we need to expand the path */
    if (rc < 0 && errno == EIO &&
            pvfs_errno == PVFS_ENOTPVFS &&
            !PATH_EXPANDED(Ppath))
    {
        /* last chance to get it open */
#if 0
        if (pdir)
        {
            char *tmp_path;
            int pathsz = strlen(pdir->s->dpath) + strlen(path);
            tmp_path = (char *)malloc(pathsz + 2);
            strcpy(tmp_path, pdir->s->dpath);
            strcat(tmp_path, "/");
            strcat(tmp_path, path);
            Ppath = PVFS_new_path(tmp_path);
        }
        else
#endif
        {
            Ppath = PVFS_path_from_expanded(path);
        }
        if (!VALID_PATH_MAGIC(Ppath))
        {
            /* must be a PVFS_path before we call expand */
            rc = -1;
            goto errorout;
        }
        rc = iocommon_expand_path(Ppath, internal_follow, flags, mode, &pd);
        if (pd)
        {
            /* opened a glibc file - just close it */       
            pvfs_free_descriptor(pd->fd);
            /* need to return some kind of code here */
            return -1;
        }
        pref->fs_id = Ppath->fs_id;
        pref->handle = Ppath->handle;
#if 0
        if (pdir)
#endif
        {
            PVFS_free_path(Ppath);
        }
    }
    IOCOMMON_RETURN_ERR(rc);
errorout:
    if (parentdir)
    {
        free(parentdir);
    }
    if (file)
    {
        if (filename)
        {
            *filename = file;
        }
        else
        {
            free(file);
        }
    }
    return rc;
}

/** pvfs_open implementation, return file info in fd 
 *  if pdir is not NULL, it is the parent directory 
 *
 *  By the time this is called path should be a PVFS_path_t
 *  and the path should be at least qualified, and resolved.
 *  We expect expanded calls to go directly to open_absolute.
 *  
 */
pvfs_descriptor *iocommon_open(const char *path,
                               int flags,
                               PVFS_hint file_creation_param,
                               mode_t mode,
                               pvfs_descriptor *pdir)
{
    int rc = 0;
    int orig_errno = errno;
    int follow_links = 0;
    int cache_flag = 1;
    int length = 0;
    void *value = NULL;
    char *directory = NULL;
    char *filename = NULL;
    char error_path[PVFS_NAME_MAX];
    int defer_bits = 0; /* used when creating a file */
    PVFS_object_ref file_ref;
    PVFS_object_ref parent_ref;
    pvfs_descriptor *pd = NULL; /* invalid pd until file is opened */
    PVFS_sysresp_getattr attributes_resp;
    PVFS_credential *credential;
    PVFS_path_t *Ppath;

    gossip_debug(GOSSIP_USRINT_DEBUG, "iocommon_open: called with %s\n", path);

    /* Initialize */
    memset(&file_ref, 0, sizeof(file_ref));
    memset(&parent_ref, 0, sizeof(parent_ref));
    memset(&attributes_resp, 0, sizeof(attributes_resp));
    memset(error_path, 0, sizeof(error_path));

    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* see if this is a PVFS path and if so check it out completely */
    Ppath = PVFS_path_from_expanded(path);
    if (VALID_PATH_MAGIC(Ppath))
    {
        if (PATH_LOOKEDUP(Ppath))
        {
            if (Ppath->filename && (Ppath->filename[0] == '\0'))
            {
                if (PATH_FOLLOWSYM(Ppath) && (flags & O_NOFOLLOW))
                {
                    /* oops this is an error */
                    rc = -1;
                    errno = ELOOP;
                    goto errorout;
                }
                /* object was looked up completely */
                file_ref.fs_id = Ppath->fs_id;
                file_ref.handle = Ppath->handle;
                goto found;
            }
            /* object was partly looked up */
        }
        if (PATH_RESOLVED(Ppath))
        {
            /* resolution can be skipped ahead */
        }
    }
    /* else this was called with a plain path */

    /* Check the flags to determine if links are followed */
    if (flags & O_NOFOLLOW)
    {
        follow_links = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    }
    else
    {
        follow_links = PVFS2_LOOKUP_LINK_FOLLOW;
    }

    /* now we are ready to try looking things up */
    if (!pdir)
    {
        /* if a simple open try the full path first */
        rc = iocommon_lookup_absolute(path,
                                      follow_links,
                                      &file_ref,
                                      error_path,
                                      sizeof(error_path));
        if (rc >= 0)
        {
            goto found;
        }
        if (errno == EIO && pvfs_errno == PVFS_ENOTPVFS)
        {
            /* nope, so cat the path to pdir's path and go for a
             * full expand that handles links and everything
             */
            int need_to_free = 0;
            if (!VALID_PATH_MAGIC(Ppath))
            {
                Ppath = PVFS_new_path(path);
                need_to_free = 1;
            }

            rc = iocommon_expand_path(Ppath, follow_links, flags, mode, &pd);

            if (PATH_LOOKEDUP(Ppath))
            {
                if (Ppath->filename && Ppath->filename[0] == '\0')
                {
                    /* found pvfs file */
                    file_ref.fs_id = Ppath->fs_id;
                    file_ref.handle = Ppath->handle;
                    if (need_to_free)
                    {
                        PVFS_free_path(Ppath);
                    }
                    goto found;
                }
                else
                {
                    /* found non-pvfs file */
                    if (need_to_free)
                    {
                        PVFS_free_path(Ppath);
                    }
                    return pd;
                }
            }
            if (need_to_free)
            {
                PVFS_free_path(Ppath);
            }
        }
    }
    else
    {
        /* if an openat, try the full path relative to pdir */
        rc = iocommon_lookup_relative(path,
                                      pdir->s->pvfs_ref,
                                      follow_links,
                                      &file_ref,
                                      error_path,
                                      sizeof(error_path));
        if (rc >= 0)
        {
            goto found;
        }
        if (errno == EIO && pvfs_errno == PVFS_ENOTPVFS)
        {
            /* nope, so cat the path to pdir's path and go for a
             * full expand that handles link and everything
             */
            char *tmp_path;
            int dlen = strlen(pdir->s->dpath);
            int plen = strlen(path);
            int mlen = dlen + plen + 2;
            tmp_path = (char *)malloc(mlen);
            strncpy(tmp_path, pdir->s->dpath, dlen + 1);
            strncat(tmp_path, "/", 1);
            strncat(tmp_path, path, plen);
            Ppath = PVFS_new_path(tmp_path);

            rc = iocommon_expand_path(Ppath, follow_links, flags, mode, &pd);

            if (PATH_LOOKEDUP(Ppath) && Ppath->filename &&
                (Ppath->filename[0] == '\0'))
            {
                file_ref.fs_id = Ppath->fs_id;
                file_ref.handle = Ppath->handle;
                PVFS_free_path(Ppath);
                free(tmp_path);
                goto found;
            }
            /* What if we found a glibc path? */
            PVFS_free_path(Ppath);
            free(tmp_path);
        }
    }

    /*
     * if we got here opening the full path failed.  see if the O_CREAT
     * flag was given and file not found, if so try to open the parent
     * dir then create the file
     */
    if (errno != ENOENT || !(flags & O_CREAT))
    {
        /* either file not found and no create flag */
        /* or some other error */
        goto errorout;
    }

    /* Split the path into a directory and file */
    rc = split_pathname(path, &directory, &filename);
    if (rc < 0)
    {
        if (errno != EISDIR)
        {
            goto errorout;
        }
        /* clear error */
        rc = 0;
        errno = 0;
    }

    if (!pdir)
    {
        /* if a simple open try the directory path */
        rc = iocommon_lookup_absolute(directory,
                                      follow_links,
                                      &parent_ref,
                                      error_path,
                                      sizeof(error_path));
        if(rc >= 0)
        {
            goto createfile;
        }
        if (errno == EIO && pvfs_errno == PVFS_ENOTPVFS)
        {
            /* nope, so cat the path to pdir's path and go for a
             * full expand that handles links and everything
             */
            Ppath = PVFS_new_path(directory);

            rc = iocommon_expand_path(Ppath, follow_links, flags, mode, &pd);

            if (PATH_LOOKEDUP(Ppath) && Ppath->filename &&
                (Ppath->filename[0] == '\0'))
            {
                file_ref.fs_id = Ppath->fs_id;
                file_ref.handle = Ppath->handle;
                PVFS_free_path(Ppath);
                goto createfile;
            }
            /* What if we found a glibc path? */
            PVFS_free_path(Ppath);
        }
    }
    else
    {
        /* if an openat, try the directory path relative to pdir */
        rc = iocommon_lookup_relative(directory,
                                      pdir->s->pvfs_ref,
                                      follow_links,
                                      &parent_ref,
                                      error_path,
                                      sizeof(error_path));
        if(rc >= 0)
        {
            goto createfile;
        }
        if (errno == EIO && pvfs_errno == PVFS_ENOTPVFS)
        {
            /* nope, so cat the path to pdir's path and go for a
             * full expand that handles link and everything
             */
            char *tmp_path;
            int dlen = strlen(pdir->s->dpath);
            int plen = strlen(directory);
            int mlen = dlen + plen + 2;
            tmp_path = (char *)malloc(mlen);
            strncpy(tmp_path, pdir->s->dpath, dlen + 1);
            strncat(tmp_path, "/", 1);
            strncat(tmp_path, directory, plen);
            Ppath = PVFS_new_path(tmp_path);

            rc = iocommon_expand_path(Ppath, follow_links, flags, mode, &pd);

            if (PATH_LOOKEDUP(Ppath) && Ppath->filename &&
                (Ppath->filename[0] == '\0'))
            {
                parent_ref.fs_id = Ppath->fs_id;
                parent_ref.handle = Ppath->handle;
                PVFS_free_path(Ppath);
                free(tmp_path);
                goto createfile;
            }
            /* What if we found a glibc path? */
            PVFS_free_path(Ppath);
            free(tmp_path);
        }
    }
    /* Still nothing, must be a file not found */
    errno = ENOENT;
    goto errorout;

createfile:
    /* Now create the file relative to the directory */
    errno = orig_errno;
    errno = 0;
    /* see if the mode provides bits allowing the user to access the
     * file - it might not in some situations and we want to set those
     * bits on until this fd is closed.
     */
    if ((((flags & O_RDONLY) || (flags & O_RDWR))) && !(mode & S_IRUSR))
    {
        defer_bits |= S_IRUSR;
        mode |= S_IRUSR;
    }
    if (((flags & O_WRONLY) || (flags & O_RDWR)) && !(mode & S_IWUSR))
    {
        defer_bits |= S_IWUSR;
        mode |= S_IWUSR;
    }
    rc = iocommon_create_file(filename,
                              mode,
                              file_creation_param,
                              parent_ref,
                              &file_ref);
    if (rc >= 0)
    {
        goto finish;
    }
    /* error on the create */
    if (errno != EEXIST)
    {
        goto errorout;
    }
    /* 
     * The file exists so must have been
     * created by a different process
     * just open it - this should open
     * because it returned the EEXIST above
     */
    errno = 0;
    rc = iocommon_lookup_relative(filename,
                                  parent_ref,
                                  follow_links,
                                  &file_ref,
                                  error_path,
                                  sizeof(error_path));
    if (rc >= 0)
    {
        goto finish;
    }
    goto errorout;

found:
    /* We found the file, make sure we were supposed to */
    if ((rc == 0) && (flags & O_EXCL) && (flags & O_CREAT))
    {
        /* File was found but EXCLUSIVE so fail */
        rc = -1;
        errno = EEXIST;
        goto errorout;
    }

finish:
    /* We successfully looked up the file so complete the open */

    /* If we get here the file was created and/or opened */
    /* Translate the pvfs reference into a file descriptor */
    /* Set the file information */
    /* create fd object */
    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_open calls pvfs_alloc_descriptor %d\n", -1);

    /* check for cache flag */
    /* At the moment the default is to cache */
    cache_flag = 1;
    value = PINT_hint_get_value_by_type(file_creation_param, /* hints */
                                        PINT_HINT_CACHE,
                                        &length);
    if (value)
    {
        cache_flag = *(int *)value;
    }
    /* now allocate file descriptor */
    pd = pvfs_alloc_descriptor(&pvfs_ops, -1, &file_ref, cache_flag);
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
                          credential,
                          &attributes_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);
    pd->s->mode = attributes_resp.attr.perms; /* this may change */
    pd->s->mode_deferred = defer_bits; /* save these until close */

    if (attributes_resp.attr.objtype == PVFS_TYPE_METAFILE)
    {
        pd->s->mode |= S_IFREG;
    }
    if (attributes_resp.attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        pd->s->mode |= S_IFDIR;
        /* set the path of this newly opened direcotry */
        if (pdir)
        {
            /* we opened relative to pdir so need to cat the paths */
            char *tpath;
            int dlen = strlen(pdir->s->dpath);
            int plen = strlen(path);
            int mlen = dlen + plen + 2;
            tpath = (char *)malloc(mlen);
            strncpy(tpath, pdir->s->dpath, dlen + 1);
            strncat(tpath, "/", 1);
            strncat(tpath, path, plen);
            pd->s->dpath = pvfs_dpath_insert(tpath);
            free(tpath);
        }
        else
        {
            /* opened absolute so just use the path */
            pd->s->dpath = pvfs_dpath_insert(path);
        }
    }
    if (attributes_resp.attr.objtype == PVFS_TYPE_SYMLINK)
    {
        pd->s->mode |= S_IFLNK;
    }
    /* ops below will lock if needed */
    gen_mutex_unlock(&pd->s->lock);
    gen_mutex_unlock(&pd->lock);

    /* Truncate the file if neccesary */
    if (flags & O_TRUNC)
    {
        errno = 0;
        rc = PVFS_sys_truncate(file_ref, 0, credential, NULL);
        IOCOMMON_CHECK_ERR(rc);
    }

    /* Move to the end of file if necessary */
    if (flags & O_APPEND)
    {
        rc = iocommon_lseek(pd, 0, 0, SEEK_END);
        IOCOMMON_CHECK_ERR(rc);
    }

    goto cleanup;

errorout:

    /* if file was opened close it freeing up descriptor resources */
    if (pd)
    {
        pvfs_free_descriptor(pd->fd);
        pd = NULL;
    }

cleanup:

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
    PVFS_credential *credential;

    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }
    errno = 0;
    rc =  PVFS_sys_truncate(file_ref, length, credential, NULL);
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
    gen_mutex_lock(&pd->s->lock);
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
            PVFS_credential *credential;
            PVFS_sysresp_getattr attributes_resp;

            memset(&attributes_resp, 0, sizeof(attributes_resp));
            rc = iocommon_cred(&credential);
            if (rc != 0)
            {
                goto errorout;
            }
            /* Get the file's size in bytes as the ending offset */
            errno = 0;
            rc = PVFS_sys_getattr(pd->s->pvfs_ref,
                                  PVFS_ATTR_SYS_SIZE,
                                  credential,
                                  &attributes_resp,
                                  NULL);
            IOCOMMON_CHECK_ERR(rc);
            pd->s->file_pointer = attributes_resp.attr.size +
                                  (offset * unit_size);
            break;
        }
        default:
        {
            errno = EINVAL;
            goto errorout;
        }
    }
    
    /* Sum the individal segment sizes */
    /* if this is a directory adjust token, the hard way */
    if (S_ISDIR(pd->s->mode))
    {
        int dirent_no = 0;
        int dirent_total_count = 0;
        int dirent_read_count = 0;
        PVFS_credential *credential;
        PVFS_sysresp_readdir readdir_resp;

        if ((offset == 0 || unit_size == 0) && whence == SEEK_CUR)
        {
            /* just asking for file position don't change position */
            goto local_exit;
        }

        if ((offset == 0 || unit_size == 0) && whence == SEEK_SET)
        {
            /* just asking for file position don't change position */
            pd->s->token = PVFS_READDIR_START;
            goto local_exit;
        }

        dirent_no = pd->s->file_pointer / sizeof(PVFS_dirent);
        pd->s->file_pointer = dirent_no * sizeof(PVFS_dirent);
        pd->s->token = PVFS_READDIR_START;
        if (dirent_no)
        {
            dirent_read_count = dirent_no;
            if (dirent_read_count > PVFS_REQ_LIMIT_DIRENT_COUNT)
            {
                dirent_read_count = PVFS_REQ_LIMIT_DIRENT_COUNT;
            }
            errno = 0;
            while (dirent_total_count < dirent_no)
            {
                if (dirent_read_count > dirent_no - dirent_total_count)
                {
                    dirent_read_count = dirent_no - dirent_total_count;
                }
                memset(&readdir_resp, 0, sizeof(readdir_resp));
                rc = iocommon_cred(&credential);
                if (rc != 0)
                {
                    goto errorout;
                }
                rc = PVFS_sys_readdir(pd->s->pvfs_ref,
                                      pd->s->token,
                                      dirent_read_count,
                                      credential,
                                      &readdir_resp,
                                      NULL);
                IOCOMMON_CHECK_ERR(rc);
                dirent_total_count += readdir_resp.pvfs_dirent_outcount;
                pd->s->token = readdir_resp.token;
                free(readdir_resp.dirent_array);
            }
        }
    }

local_exit:
    gen_mutex_unlock(&pd->s->lock);
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
    char *file = NULL;
    PVFS_object_ref parent_ref, file_ref;
    PVFS_credential *credential;
    PVFS_sys_attr attr;

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_remove: called with %s\n", path);

    /* Initialize */
    memset(&parent_ref, 0, sizeof(parent_ref));
    memset(&file_ref, 0, sizeof(file_ref));
    memset(&attr, 0, sizeof(attr));

    /* Initialize the system interface for this process */
    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* open path needs to be nofollow */
    rc = iocommon_lookup((char *)path,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW,
                         &parent_ref,
                         &file_ref,
                         &file,
                         pdir);
    IOCOMMON_RETURN_ERR(rc);

    /* need to verify this is a file or symlink */
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
    rc = PVFS_sys_remove(file, parent_ref, credential, PVFS_HINT_NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
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
    char *oldname = NULL, *newname = NULL;
    PVFS_object_ref oldref, newref;
    PVFS_credential *creds;
    PVFS_hint hints = PVFS_HINT_NULL;

    /* Initialize */
    memset(&oldref, 0, sizeof(oldref));
    memset(&newref, 0, sizeof(newref));

    rc = iocommon_cred(&creds);
    if (rc != 0)
    {
        goto errorout;
    }
    /* open oldpath */
    rc = iocommon_lookup((char *)oldpath,
                         PVFS2_LOOKUP_LINK_FOLLOW,
                         &oldref, /* parent */
                         NULL,
                         &oldname, /* filename */
                         oldpdir);
    IOCOMMON_RETURN_ERR(rc);

    /* open newpath */
    rc = iocommon_lookup((char *)newpath,
                         PVFS2_LOOKUP_LINK_FOLLOW,
                         &newref, /* parent */
                         NULL,
                         &newname, /* filename */
                         newpdir);
    IOCOMMON_RETURN_ERR(rc);

    /* check for the trivial case */
    if (newref.fs_id == oldref.fs_id &&
        newref.handle == oldref.handle &&
        !strcmp(oldname, newname))
    {
        return 0;
    }

    errno = 0;
    rc = PVFS_sys_rename(oldname, oldref, newname, newref, creds, hints);
    IOCOMMON_CHECK_ERR(rc);

errorout:
    if (oldname)
    {
        free(oldname);
    }
    if (newname)
    {
        free(newname);
    }
    return rc;
}

#if PVFS_UCACHE_ENABLE
/* Returns how many copy operations to/from the ucache will need to be
 * completed.
 */
static int calc_copy_op_cnt(
    off64_t offset,     /* offset into file where transfer should begin */
    size_t req_size,    /* total Request Size */
    int req_blk_cnt,    /* requested Block Count */
    size_t iovec_count, /* number of iovecs in vector */
    const struct iovec *vector /* pointer to array of iovecs */
)
{
    int copy_count = 0; /* The number of memcpy operations to be completed */
    size_t size_left = req_size; /* Bytes left to convert to copy ops */
    size_t iovec_left = vector[0].iov_len; /* bytes left in this iovec */
    int vec_ndx = 0; /* Index into iovec array */

    /* Compute the size of the first block to be transfered */
    size_t block_size_to_transfer = CACHE_BLOCK_SIZE - (offset % CACHE_BLOCK_SIZE);

    int i;
    /* For every block identify source and destination locations in memory and
     * size of transfer between the ucache and memory buffer while maintaining:
     * the size left in the request, the size left in the current iovec
     * segment, the size left in the current block, and which iovec segment is
     * currently being considered.
     */
    for(i = 0; i < req_blk_cnt; i++)
    {
        size_t block_left = block_size_to_transfer;
        while(block_left != 0)
        {
            /* block_left is the limiting factor */
            if(iovec_left > block_left)
            {
                size_left -= block_left;
                iovec_left -= block_left;
                block_left = 0; /* Done with this block */
            }
            /* iovec_left is the limiting factor */
            else if(iovec_left < block_left)
            {
                size_left -= iovec_left;
                block_left -= iovec_left;
                vec_ndx++;  /* Done with this iovec */
                if(vec_ndx < iovec_count)
                {
                    iovec_left = vector[vec_ndx].iov_len;
                }
            }
            /* This transfer operation would complete both */
            else /* They must be equal */
            {
                size_left -= iovec_left;
                block_left = 0;
                vec_ndx++; /* Done with this iovec and block */

                /* Only set the next iovec_left if one is available */
                if(vec_ndx < iovec_count)
                {
                    iovec_left = vector[vec_ndx].iov_len;
                }
            }
            /* Increment the number of the memcpy calls that will need to be
             * performed
             */
            copy_count++;
        }

        /* Break when there are no more bytes to be read/written so that the
         * following if/else code block won't been run unless there is another
         * block of data to be transfered. */
        if(size_left == 0)
        {
            break;
        }
      
        if(size_left >= CACHE_BLOCK_SIZE)
        {
            /* Must transfer full block */
            block_size_to_transfer = CACHE_BLOCK_SIZE;
        }
        else
        {
            /* size_left is less than a full block's size, so size_left is all
             * that needs to be transfered to/from this block
             */
            block_size_to_transfer = size_left;
        }
    }
    /* Finally, return the number of memcpy operations that must be completed
     * to satisfy this request.
     */
    return copy_count;
}

/**
 * Provided two ucache related structures ureq and ucop, determine the
 * reads/writes to be completed between the ucache and user memory (vector).
 */
void calc_copy_ops(
    off64_t offset,
    size_t req_size,
    struct ucache_req_s *ureq,
    struct ucache_copy_s *ucop,
    int copy_count,
    const struct iovec *vector
)
{
    int ureq_ndx = 0;
    int vec_ndx = 0;
    size_t size_left = req_size;
    size_t blk_tfer_size = CACHE_BLOCK_SIZE - (offset % CACHE_BLOCK_SIZE);
    size_t blk_left = blk_tfer_size;
    size_t vec_left = vector[0].iov_len;
    int i;
    for(i = 0; i < copy_count; i++)
    {
        /* Record necessary info for the future memcpy operation */
        if(i == 0)
        {
            ucop[i].cache_pos = (char *)ureq[ureq_ndx].ublk_ptr +
            (offset % CACHE_BLOCK_SIZE);
        }
        else
        {
            ucop[i].cache_pos = (char *)ureq[ureq_ndx].ublk_ptr +
                    (blk_tfer_size - blk_left);
        }
        ucop[i].buff_pos = (char *)vector[vec_ndx].iov_base +
                (vector[vec_ndx].iov_len - vec_left);
        ucop[i].blk_index = ureq[ureq_ndx].ublk_index;

        if(vec_left > blk_left) /* Finish block */
        {
            if(size_left < blk_left)
            {
                ucop[i].size = size_left;
                break;
            }
            ucop[i].size = blk_left;
            vec_left -= blk_left;
            size_left -= blk_left;
            if(size_left >= CACHE_BLOCK_SIZE)
            {
                blk_tfer_size = CACHE_BLOCK_SIZE;
                blk_left = blk_tfer_size;
            }
            else
            {
                blk_tfer_size = size_left;
                blk_left = blk_tfer_size;
            }
            ureq_ndx++;
        }
        else if(vec_left < blk_left) /* Finish iovec */
        {
            if(size_left < vec_left)
            {
                ucop[i].size = size_left;
                break;
            }
            ucop[i].size = vec_left;
            blk_left -= vec_left;
            size_left -= vec_left;
            vec_ndx++;
            vec_left = vector[vec_ndx].iov_len;
        }
        else /* They must be equal - finish both */
        {
            if(size_left < blk_left)
            {
                ucop[i].size = size_left;
                break;
            }
            ucop[i].size = blk_left;
            size_left -= blk_left;
            if(size_left >= CACHE_BLOCK_SIZE)
            {
                blk_tfer_size = CACHE_BLOCK_SIZE;
                blk_left = blk_tfer_size;
            }
            else
            {
                blk_tfer_size = size_left;
                blk_left = blk_tfer_size;
            }
            vec_ndx++;
            vec_left = vector[vec_ndx].iov_len;
            ureq_ndx++;
        }
    }
}

static int cache_readorwrite(
    enum PVFS_io_type which,
    struct ucache_copy_s * ucop
)
{
    int rc = 0;
    if(which == PVFS_IO_READ)
    {
        /* Copy from cache to user mem */
        memcpy(ucop->buff_pos, ucop->cache_pos, ucop->size);
        rc = (int)ucop->size;
    }
    else
    {
        /* Copy from user mem to cache */
        memcpy(ucop->cache_pos, ucop->buff_pos, ucop->size);
        rc = (int)ucop->size;
    }
    return rc;
}

int calc_req_blk_cnt(uint64_t offset, size_t req_size)
{
    /* Check for zero sized request */
    if(req_size == 0)
    {
        return 0;
    }
    /* Check to see if request is less than a full block */
    if(req_size < CACHE_BLOCK_SIZE && (offset % CACHE_BLOCK_SIZE) == 0)
    {
        return 1;
    }
    /* Count next blocks */
    size_t req_left = req_size - (CACHE_BLOCK_SIZE -
            (offset % CACHE_BLOCK_SIZE));
    int blk_cnt = req_left / CACHE_BLOCK_SIZE;

    /* Account for last block if necessary */
    if((req_left - (blk_cnt * CACHE_BLOCK_SIZE)) != 0)
    {
        blk_cnt++;
    }

    return (blk_cnt + 1); /* Add one to account for first block */
}


/* Return the sum of iov_len of iovecs in the iovec array pointed to by vector. */
size_t sum_iovec_lengths(size_t iovec_count, const struct iovec *vector)
{
    size_t size = 0;
    int i = 0;
    for (i = 0; i < iovec_count; i++)
    {
        size += vector[i].iov_len;
    }
    return size;
}

/** Attempt to read a full CACHE_BLOCK_SIZE into the ucache block.
 *
 * Also adjust req_size and req_blk_cnt used in 
 * iocommon_readorwrite to account for the scenarios where less 
 * data was read from the file system than was requested.
 *
 * Also, fent_size is updated to inform the ucache of the largest file size
 * seen by the ucache related to this file.
 * 
 * Upon ucache block removal, we use the file entry size to determine if the
 * last block held in ucache isn't a full CACHE_BLOCK_SIZE.
 * This ensures that the correct number of bytes are written to the
 * file system on block removal.
 */
unsigned char read_full_block_into_ucache(
    pvfs_descriptor *pd, /** Ultimately let's us id the file */
    PVFS_size offset, /** The original request offset */
    struct ucache_req_s *req, /** The ucache_req_s describing this block */
    int req_index, /** The index of this ucache_req_s in the overall array */
    uint64_t * fent_size, /** Pointer to this file entry size seen by ucache */
    size_t * req_size, /** Pointer to this iocommon_readorwrite request's size */
    int * req_blk_cnt /** Pointer to the number of ucache blocks determined by req_size and offset */
)
{
    /* Return Boolean indicating we read a full ucache block */
    unsigned char rfb= 1;

    /* The byte count read by iocommon_vreadorwrite */
    int vread_count = 0;

    /* Attempt Read of Full Block From file system into user cache */
    struct iovec cache_vec = {req->ublk_ptr, CACHE_BLOCK_SIZE};
    lock_lock(get_lock(req->ublk_index));
    vread_count = iocommon_vreadorwrite(PVFS_IO_READ,
                                        &pd->s->pvfs_ref,
                                        req->ublk_tag,
                                        1,
                                        &cache_vec);
    
    /* After reading, attempt update of *fent_size */
    if((req->ublk_tag + vread_count) > *fent_size)
    {
        *fent_size = req->ublk_tag + vread_count;
    }

    /* Were we able to completely read this block? */
    if(vread_count != CACHE_BLOCK_SIZE)
    {
        /* This is the index of the last valid cache block in the ureq array */
        /* so add 1 to the index to get the correct block count. */
        *req_blk_cnt = req_index + 1;

        /* We need to recompute the req_size if less data was
         * read than was requested. */
        size_t new_req_size = 0;
        /* Did we fail to read a complete block on the first block of
         * the request?
         */
        if(offset >= req->ublk_tag)
        {
            new_req_size = vread_count - (offset - req->ublk_tag);
        }
        else
        {
            new_req_size = (req->ublk_tag - offset) + vread_count;
        }
        if(new_req_size < *req_size)
        {
            /* printf("Request expected:%Zu\tbut only read:%Zu\n", *req_size, new_req_size); */
            *req_size = new_req_size;
        }
        /* Unlock block */
        lock_unlock(get_lock(req->ublk_index));
        rfb = 0;
    }
    /* Unlock block */
    lock_unlock(get_lock(req->ublk_index));
    return rfb;
}
#endif /* PVFS_UCACHE_ENABLE */

/** Do a blocking read or write, possibly utilizing the user cache.
 * Returns -1 on error, some positive value on success;
 */
int iocommon_readorwrite(enum PVFS_io_type which,
                         pvfs_descriptor *pd,
                         PVFS_size offset,
                         size_t iovec_count,
                         const struct iovec *vector)
{
    int rc = 0;
#if PVFS_UCACHE_ENABLE
    if(ucache_enabled)
    {
        if(!pd->s->fent)
        {
            lock_lock(ucache_lock);
            ucache_stats->pseudo_misses++; /* could overflow */
            these_stats.pseudo_misses++;
            lock_unlock(ucache_lock);
        }
    }

    if(!ucache_enabled || !pd->s->fent)
    {
#endif /* PVFS_UCACHE_ENABLE */
        /* Bypass the ucache */
        errno = 0;
        rc = iocommon_vreadorwrite(which,
                                   &pd->s->pvfs_ref,
                                   offset,
                                   iovec_count,
                                   vector);
        return rc;
#if PVFS_UCACHE_ENABLE
    }

    /* How many bytes is the request? */
    /* Request is contiguous in file starting at offset. */
    /* Also, the iovec segments may be non-contiguous in memory */
    /* Sum the individal segment sizes */
    /* size in bytes the R/W request might encompass */
    size_t req_size = sum_iovec_lengths(iovec_count, vector);
    /* Return zero here if we know there's nothing to be done. */
    if(req_size == 0)
    {
        return 0;
    }
    
#if 0
    printf("iocommon_readorwrite: offset = %lu\treq_size = %lu\n",
           offset,
           req_size);
    if(which == PVFS_IO_READ)
    {
        printf("attempting to read from ucache...\n");
    }
    else
    {
        printf("attempting to write to ucache...\n");
    }
#endif

    /* Now, we know this isn't zero sized request */
    struct file_ent_s *fent = pd->s->fent;
    uint64_t new_file_size = fent->size;
    struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent);
    /* how many blocks the R/W request may encompass */
    int req_blk_cnt = calc_req_blk_cnt(offset, req_size);
    int transfered = 0; /* count of the bytes transfered */

    /* If the ucache per file blk request threshold is exceeded, flush and
     * evict file, then peform nocache version of readorwrite. */
    if((req_blk_cnt + mtbl->num_blocks) > UCACHE_MAX_BLK_REQ)
    {
        /*
         * printf("flushing file from ucache, since it's grown too large and "
         *         "continuing to service request without involving the ucache\n");
         */
        /* Flush dirty blocks */
        rc = ucache_flush_file(pd->s->fent);
        if(rc != 0)
        {
            /* TODO: alert user there was an error when flushing the ucache */
        }

        /* Bypass the ucache */
        rc = iocommon_vreadorwrite(which,
                                   &pd->s->pvfs_ref,
                                   offset,
                                   iovec_count,
                                   vector);
        return rc;
    }

    /* Now that we know the req_blk_cnt, allocate the required
     * space for tags, hits boolean, and ptr to block in ucache shared memory.
     */
    struct ucache_req_s ureq[req_blk_cnt];
    memset(ureq, 0, sizeof(struct ucache_req_s) * req_blk_cnt);
    ureq[0].ublk_tag = offset - (offset % CACHE_BLOCK_SIZE); /* first tag */

    int i; /* index used for 'for loops' */
    /* Loop over positions storing tags (ment identifiers) */
    for(i = 1; i < req_blk_cnt; i++)
    {
        ureq[i].ublk_tag = ureq[ (i - 1) ].ublk_tag + CACHE_BLOCK_SIZE;
    }

    /* Now that tags are set fill in array of lookup responses */
    for(i = 0; i < req_blk_cnt; i++)
    {
        struct ucache_req_s *this = &ureq[i];
        this->ublk_ptr = ucache_lookup(pd->s->fent,
                                       this->ublk_tag,
                                       &(this->ublk_index));
        if(this->ublk_ptr == (void *)NIL)
        {
            lock_lock(ucache_lock);
            ucache_stats->misses++; /* could overflow */
            these_stats.misses++;
            lock_unlock(ucache_lock);
        }
        else
        {
            lock_lock(ucache_lock);
            ucache_stats->hits++;  /* could overflow */
            these_stats.hits++;
            lock_unlock(ucache_lock);
        }
    }
    if(which == PVFS_IO_READ)
    {
        /* Loop over ureq structure and perform reads on misses */
        /* Keep track of how much has been read so we can know if the last
         * block successfully read is incomplete.
		 */
        /* Re-read last block if partial */
        for(i = 0; i < req_blk_cnt; i++)
        {
            struct ucache_req_s *this = &ureq[i];
            if(this->ublk_ptr == (void *) NILP) /* ucache miss on block*/
            {
                this->ublk_ptr = ucache_insert(pd->s->fent,
                                               this->ublk_tag,
                                               &(this->ublk_index));
                /* ucache_insert fail */
                if(this->ublk_ptr == (void *) NILP)
                {
                    /* Cannot cache the rest of this file */
                    /* either try some other sort of eviction or perform no cache */

                    /** Alert the user that we are no longer caching this file,
                     * since we couldn't obtain a free cache block.
                     */
                    /*TODO make this a gossip statement */
                    /* printf("Flushing file from cache. Couldn't obtain block.\n"); */

                    /* Flush dirty blocks */
                    rc = ucache_flush_file(pd->s->fent);
                    if(rc != 0)
                    {
                        /* TODO: alert user there was an error when flushing
                         * the ucache
                         */
                        printf("warning: error detected when flushing file"
                               " from ucache.\n");
                    }
                    /* Bypass the ucache */
                    rc = iocommon_vreadorwrite(which,
                                               &pd->s->pvfs_ref,
                                               offset,
                                               iovec_count,
                                               vector);
                    return rc;
                }

                /* Attempt read of full block from fs into ucache.
                 * Remember this locks, reads into, then unlocks the specifed
                 * block provided by this->ublk_ptr
                 */
                if(!read_full_block_into_ucache(
                    pd, offset, this, i, &new_file_size, &req_size, &req_blk_cnt)
                )
                {
                    /* Stop trying to read if we couldn't read a full bock
                     * this time
                     */
                    break;
                }
            }
        }
    }

    /* Read beginning and end blks into cache before writing if
     * either end of the request are unalligned.
     */
    if(which == PVFS_IO_WRITE) /* Write */
    {
        unsigned char first_block_hit = 0;
        unsigned char last_block_hit = 0;

        /* Attempt insertion of blocks reported missed during lookup */
        for(i = 0; i < req_blk_cnt; i++)
        {
            struct ucache_req_s *this = &ureq[i];
            if(this->ublk_ptr == (void *) NILP) /* ucache miss on block*/
            {
                /* Attempt to make room for missed blocks */
                this->ublk_ptr = ucache_insert(pd->s->fent,
                                               this->ublk_tag,
                                               &(this->ublk_index));
                /* ucache_insert fail */
                if(this->ublk_ptr == (void *) NILP)
                {
                    /* Cannot cache the rest of this file */
                    /* either try some other sort of eviction or perform no cache */

                    /** Alert the user that we are no longer caching this file,
                     * since we couldn't obtain a free cache block.
                     */
                    /*TODO make this a gossip statement */
                    printf("Flushing file from cache. Couldn't obtain block.\n");

                    /* Flush dirty blocks */
                    rc = ucache_flush_file(pd->s->fent);
                    if(rc != 0)
                    {
                        /* TODO: alert user there was an error when flushing
                         * the ucache
                         */
                        printf("warning: error detected when flushing file"
                               " from ucache.\n");
                    }
                    /* Bypass the ucache */
                    rc = iocommon_vreadorwrite(which,
                                               &pd->s->pvfs_ref,
                                               offset,
                                               iovec_count,
                                               vector);
                    return rc;
                }
            }
            else /* Block was hit */
            {
                if(i == 0)
                {
                    first_block_hit = 1;
                }
                if(i == (req_blk_cnt - 1))
                {
                    last_block_hit = 1;
                }
            }
        }

        /* We aren't concerned about ucache block on the interior of this
         * request, so we are only concerned about first and the last ucache
         * blocks of this request. */

        /* Write would leave the first block incomplete if the offset starts
         * after the first block's tag or the request is less than a full sized
         * block */
        if((ureq[0].ublk_tag != offset) || (req_size < CACHE_BLOCK_SIZE))
        {
            /* If the first block was missed */
            if(first_block_hit == 0)
            {
                /* We create copies of the following two variables so that
                 * read_full_block_into_ucache won't adjust the originals
                 * like we intend on PVFS_IO_READ.
                 * Note that new_file_size may still be modified.
                 */
                size_t copy_of_req_size = req_size;
                int copy_of_req_blk_cnt = req_blk_cnt;
                read_full_block_into_ucache(pd,
                                            offset,
                                            &ureq[0],
                                            0,
                                            &new_file_size,
                                            &copy_of_req_size,
                                            &copy_of_req_blk_cnt);
            }
        }
        /* Last block if there is one and the write won't complete the block */
        if((req_blk_cnt > 1) && ((offset + req_size) % CACHE_BLOCK_SIZE != 0))
        {
            /* If the last block was missed or if it was hit and the ucache
             * block is currently incomplete from a ucache perspective. */
            if(last_block_hit == 0)
            {
                /* We create copies of the following two variables so that
                 * read_full_block_into_ucache won't adjust the originals
                 * like we intend on PVFS_IO_READ.
                 * Note that new_file_size may still be modified.
                 */
                size_t copy_of_req_size = req_size;
                int copy_of_req_blk_cnt = req_blk_cnt;
                read_full_block_into_ucache(pd,
                                            offset,
                                            &ureq[req_blk_cnt -1],
                                            req_blk_cnt - 1,
                                            &new_file_size,
                                            &copy_of_req_size,
                                            &copy_of_req_blk_cnt);
            }
        }

        /* After reading, attempt update of new_file_size */
        if(offset + req_size > new_file_size)
        {
            new_file_size = offset + req_size;
        }

        /* Now that we're sure of what the new file size will be,
         * lock the global ucache lock and adjust the file entry's size
         * as perceived by the ucache, then unlock the ucache.
         */
        lock_lock(ucache_lock);
        fent->size = new_file_size;
        /* printf("fent->size = %lu KB\n", fent->size / 1024); */
        lock_unlock(ucache_lock);
    }

    /* At this point we know how many blocks the request will cover, the tags
     * (indexes into file) of the blocks, and the ptr to the corresponding
     * blk in memory, and the new file size max seen by ucache.
     */

    /* If only one iovec then we can assume there will be req_blk_cnt
     * memcpy operations, otherwise we need to determine how many
     * memcpy operations will be required so we can create the ucache_copy_s
     * struct array of proper length.
     */
    int copy_count = 0;
    if(iovec_count == 1)
    {
        copy_count = req_blk_cnt;
    }
    else
    {
        copy_count = calc_copy_op_cnt(offset,
                                      req_size,
                                      req_blk_cnt,
                                      iovec_count,vector);
    }

    /* Create copy structure and fill with appropriate values */
    struct ucache_copy_s ucop[copy_count];
    calc_copy_ops(offset, req_size, &ureq[0], &ucop[0], copy_count, vector);

    /* The ucache copy structure should now be filled and we can procede with
     * the necessary memcpy operations.
     */
    int ureq_index = 0;
    for(i = 0; i < copy_count; i++)
    {
        /* perform copy operation */
        lock_lock(get_lock(ureq[ureq_index].ublk_index));
        transfered += cache_readorwrite(which, &ucop[i]);
        /* Unlock the block */
        lock_unlock(get_lock(ureq[ureq_index].ublk_index));
        /* Check if this ucop completed this block, so we can adjust the
         * ureq_index accordingly */
        if((offset + transfered) >=
            (ureq[ureq_index].ublk_tag + CACHE_BLOCK_SIZE))
        {
            ureq_index++;
        }
    }
    return transfered;
#endif /* PVFS_UCACHE_ENABLE */
}

/** do a blocking read or write from an iovec
 *  this just converts to PVFS Request notation
 *  other interfaces can still do direct reads to
 *  RorW_nocache below
 */
int iocommon_vreadorwrite(enum PVFS_io_type which,
                         PVFS_object_ref *por,
                         PVFS_size offset,
                         size_t count,
                         const struct iovec *vector)
{
    int rc = 0;
    int i, size = 0;
    void *buf;
    PVFS_Request mem_req;
    PVFS_Request file_req;

    for(i = 0; i < count; i++)
    {   
        size += vector[i].iov_len;
    }

    if(size == 0)
    {
        return 0;
    }

    rc = PVFS_Request_contiguous(size, PVFS_BYTE, &file_req);
    rc = pvfs_convert_iovec(vector, count, &mem_req, &buf);
    rc = iocommon_readorwrite_nocache(which,
                                      por,
                                      offset, 
                                      buf,
                                      mem_req,
                                      file_req);
    PVFS_Request_free(&mem_req);
    PVFS_Request_free(&file_req);

    return rc;
}

/** do a blocking read or write
 *  all sync reads or writes to disk come here
 */
int iocommon_readorwrite_nocache(enum PVFS_io_type which,
                                 PVFS_object_ref *por,
                                 PVFS_size offset,
                                 void *buf,
                                 PVFS_Request mem_req,
                                 PVFS_Request file_req)
{
    int rc = 0;
    int orig_errno = errno;
    PVFS_credential *creds;
    PVFS_sysresp_io io_resp;

    gossip_debug(GOSSIP_USRINT_DEBUG,
           "iocommon_readorwrite_nocache: called with %d\n", (int)por->handle);

    if (!por)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&io_resp, 0, sizeof(io_resp));

    rc = iocommon_cred(&creds);
    if (rc != 0)
    {
        goto errorout;
    }

    errno = 0;
    rc = PVFS_sys_io(*por,
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
    PVFS_credential *credential;
    PVFS_size req_size;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Ensure descriptor is used for the correct type of access */
    if ((which==PVFS_IO_READ && (O_WRONLY == (pd->s->flags & O_ACCMODE))) ||
        (which==PVFS_IO_WRITE && (O_RDONLY == (pd->s->flags & O_ACCMODE))))
    {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* Create the memory request of a contiguous region: 'mem_req' x count */
    rc = PVFS_Request_contiguous(count, etype_req, &contig_memory_req);

    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    errno = 0;
    rc = PVFS_isys_io(pd->s->pvfs_ref,
                      file_req,
                      pd->s->file_pointer+extra_offset,
                      buf,
                      contig_memory_req,
                      credential,
                      ret_resp,
                      which,
                      ret_op_id,
                      PVFS_HINT_NULL,
                      NULL);
    IOCOMMON_CHECK_ERR(rc);

    /* TODO: handle this */
    assert(*ret_op_id!=-1);

    PVFS_Request_size(contig_memory_req, &req_size);
    gen_mutex_lock(&pd->s->lock);
    pd->s->file_pointer += req_size;
    gen_mutex_unlock(&pd->s->lock);
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
    PVFS_credential *credential;
    PVFS_sysresp_getattr getattr_response;

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_getattr: called with %d\n", (int)obj.handle);

    /* Initialize */
    memset(&getattr_response, 0, sizeof(getattr_response));

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* now get attributes */
    errno = 0;
    rc = PVFS_sys_getattr(obj,
                          mask,
                          credential,
                          &getattr_response, NULL);
    IOCOMMON_CHECK_ERR(rc);
    *attr = getattr_response.attr;
    if (attr->objtype == PVFS_TYPE_DIRECTORY)
    {
        attr->blksize = 1024; /* dir block sizes not maintained in PVFS */
    }
    if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        attr->blksize = 1024; /* dir block sizes not maintained in PVFS */
        attr->size = strnlen(attr->link_target, PVFS_PATH_MAX);
    }

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
    PVFS_credential *credential;

    gossip_debug(GOSSIP_USRINT_DEBUG,
                 "iocommon_setattr: called with %d\n", (int)obj.handle);

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* Should we automatically set CTIME here?
     * Seems like a good place to do so, all updates of the "inode" must
     * go through here, for all potential user level interfaces, so this
     * might be a good idea.  Should only update if we change owner,
     * group, permissions, more something else like that.
     */

    /* now set attributes */
    rc = PVFS_sys_setattr(obj, *attr, credential, NULL);
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
    memset(buf, 0, sizeof(struct stat));

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
        buf->st_nlink = 1; /* PVFS does not allow hard links */
    }
    if (attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        buf->st_mode |= S_IFDIR;
        buf->st_nlink = attr.dirent_count + 2;
    }
    if (attr.objtype == PVFS_TYPE_SYMLINK)
    {
        buf->st_mode |= S_IFLNK;
        buf->st_nlink = 1; /* PVFS does not allow hard links */
    }
    buf->st_uid = attr.owner;
    buf->st_gid = attr.group;
    buf->st_rdev = 0; /* no dev special files */
    buf->st_size = attr.size;
#if PVFS_USER_ENV_VARS_ENABLED
    if(env_vars.env_var_array[ORANGEFS_STRIP_SIZE_AS_BLKSIZE].env_var_value &&
       strcmp(env_vars.env_var_array[ORANGEFS_STRIP_SIZE_AS_BLKSIZE].env_var_value, "true") == 0)
    {
        if(attr.dfile_count > 0)
        {
            buf->st_blksize = attr.blksize / attr.dfile_count;
        }
        else
        {
            buf->st_blksize = attr.blksize;
        }
        /*
        printf("ORANGEFS_STRIP_SIZE_AS_BLKSIZE=%s\n",
               env_vars.env_var_array[ORANGEFS_STRIP_SIZE_AS_BLKSIZE].env_var_value);
        printf("attr.blksize=%llu\n", (long long unsigned int) attr.blksize);
        printf("dfile_count=%d\n", attr.dfile_count);
        printf("buf->st_blksize=%llu\n", (long long unsigned int) buf->st_blksize);
        */
    }
    else
    {
        buf->st_blksize = attr.blksize;
    }
#else
    buf->st_blksize = attr.blksize;
#endif
    buf->st_blocks = (attr.size + (S_BLKSIZE - 1)) / S_BLKSIZE;
    /* we don't have nsec so we left the memset zero them */
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
    memset(buf, 0, sizeof(struct stat64));

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
        buf->st_nlink = 1; /* PVFS does not allow hard links */
    }
    if (attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        buf->st_mode |= S_IFDIR;
        buf->st_nlink = attr.dirent_count + 2;
    }
    if (attr.objtype == PVFS_TYPE_SYMLINK)
    {
        buf->st_mode |= S_IFLNK;
        buf->st_nlink = 1; /* PVFS does not allow hard links */
    }
    buf->st_uid = attr.owner;
    buf->st_gid = attr.group;
    buf->st_rdev = 0; /* no dev special files */
    buf->st_size = attr.size;
#if PVFS_USER_ENV_VARS_ENABLED
    if(env_vars.env_var_array[ORANGEFS_STRIP_SIZE_AS_BLKSIZE].env_var_value &&
       strcmp(env_vars.env_var_array[ORANGEFS_STRIP_SIZE_AS_BLKSIZE].env_var_value, "true") == 0)
    {
        if(attr.dfile_count > 0)
        {
            buf->st_blksize = attr.blksize / attr.dfile_count;
        }
        else
        {
            buf->st_blksize = attr.blksize;
        }
        /*
        printf("ORANGEFS_STRIP_SIZE_AS_BLKSIZE=%s\n",
               env_vars.env_var_array[ORANGEFS_STRIP_SIZE_AS_BLKSIZE].env_var_value);
        printf("attr.blksize=%llu\n", (long long unsigned int) attr.blksize);
        printf("dfile_count=%d\n", attr.dfile_count);
        printf("buf->st_blksize=%llu\n", (long long unsigned int) buf->st_blksize);
        */
    }
    else
    {
        buf->st_blksize = attr.blksize;
    }
#else
    buf->st_blksize = attr.blksize;
#endif
    buf->st_blocks = (attr.size + (S_BLKSIZE - 1)) / S_BLKSIZE;
    /* we don't have nsec so we left the memset zero them */
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

int iocommon_getmod(pvfs_descriptor *pd, mode_t *mode)
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
    rc = iocommon_getattr(pd->s->pvfs_ref, &attr, PVFS_ATTR_SYS_PERM);
    if (!rc)
    {
        *mode = attr.perms & 07777;
    }
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
    PVFS_sys_attr       attr;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref     parent_ref;
    PVFS_sysresp_mkdir  resp_mkdir;
    PVFS_credential    *credential;
    char *filename = NULL;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&resp_lookup, 0, sizeof(resp_lookup));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_mkdir,  0, sizeof(resp_mkdir));

    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* Make sure we don't try and create the root or current directory */

    /* lookup parent */
    rc = iocommon_lookup((char *)pvfs_path,
                         PVFS2_LOOKUP_LINK_FOLLOW,
                         &parent_ref,
                         NULL,
                         &filename,
                         pdir);
    IOCOMMON_RETURN_ERR(rc);

    attr.owner = geteuid();
    attr.group = getegid();
    attr.perms = mode & 07777; /* mask of stray bits */
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    errno = 0;
    rc = PVFS_sys_mkdir(filename,
                        parent_ref,
                        attr,
                        credential,
                        &resp_mkdir, NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
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
                                                PVFS_ATTR_SYS_SIZE |
                                                PVFS_ATTR_SYS_LNK_TARGET);
    IOCOMMON_RETURN_ERR(rc);

    /* copy attributes into standard stat struct */
    if (attr.objtype == PVFS_TYPE_SYMLINK)
    {
        strncpy(buf, attr.link_target, size);
        rc = attr.size;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
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
    PVFS_sys_attr       attr;
    PVFS_object_ref     parent_ref;
    PVFS_sysresp_symlink  resp_symlink;
    PVFS_credential    *credential;
    char *filename = NULL;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_symlink,0, sizeof(resp_symlink));

    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* Make sure we don't try and create the root or current directory */

    /* lookup parent */
    rc = iocommon_lookup((char *)pvfs_path,
                         PVFS2_LOOKUP_LINK_FOLLOW,
                         &parent_ref,
                         NULL,
                         &filename,
                         pdir);
    IOCOMMON_RETURN_ERR(rc);

    /* Set the attributes for the new directory */
    attr.owner = getuid();
    attr.group = getgid();
    attr.perms = 0777;
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    errno = 0;
    rc = PVFS_sys_symlink(filename,
                          parent_ref,
                          (char *)link_target,
                          attr,
                          credential,
                          &resp_symlink,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

errorout:
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
    PVFS_credential *credential;
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

    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    gen_mutex_lock(&pd->s->lock);
    token = pd->s->token == 0 ? PVFS_READDIR_START : pd->s->token;

    /* clear the output buffer */
    memset(dirp, 0, size);
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
                          credential,
                          &readdir_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

    pd->s->token = readdir_resp.token;
    name_max = PVFS_util_min(NAME_MAX, PVFS_NAME_MAX);
    for(i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
    {
        /* copy a PVFS_dirent to a struct dirent */
        dirp->d_ino = (long)readdir_resp.dirent_array[i].handle;
#ifdef _DIRENT_HAVE_D_OFF
        dirp->d_off = pd->s->file_pointer;
#endif
#ifdef _DIRENT_HAVE_D_RECLEN
        dirp->d_reclen = sizeof(PVFS_dirent);
#endif
#ifdef _DIRENT_HAVE_D_TYPE
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
        dirp->d_type = DT_UNKNOWN;
#endif
        strncpy(dirp->d_name, readdir_resp.dirent_array[i].d_name, name_max);
        dirp->d_name[name_max] = 0;
        pd->s->file_pointer += sizeof(struct dirent);
        bytes += sizeof(struct dirent);
        dirp++;
    }
    gen_mutex_unlock(&pd->s->lock);
    free(readdir_resp.dirent_array);
    return bytes;

errorout:
    gen_mutex_unlock(&pd->s->lock);
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
    PVFS_credential *credential;
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

    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    gen_mutex_lock(&pd->s->lock);
    token = pd->s->token == 0 ? PVFS_READDIR_START : pd->s->token;

    /* clear the output buffer */
    memset(dirp, 0, size);
    count = size / sizeof(struct dirent64);
    if (count > PVFS_REQ_LIMIT_DIRENT_COUNT)
    {
        count = PVFS_REQ_LIMIT_DIRENT_COUNT;
    }
    errno = 0;
    rc = PVFS_sys_readdir(pd->s->pvfs_ref,
                          token,
                          count,
                          credential,
                          &readdir_resp,
                          NULL);
    IOCOMMON_CHECK_ERR(rc);

    pd->s->token = readdir_resp.token;
    name_max = PVFS_util_min(NAME_MAX, PVFS_NAME_MAX);
    for(i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
    {
        /* copy a PVFS_dirent to a struct dirent64 */
        dirp->d_ino = (uint64_t)readdir_resp.dirent_array[i].handle;
#ifdef _DIRENT_HAVE_D_OFF
        dirp->d_off = (off64_t)pd->s->file_pointer;
#endif
#ifdef _DIRENT_HAVE_D_RECLEN
        dirp->d_reclen = sizeof(struct dirent64);
#endif
#ifdef _DIRENT_HAVE_D_TYPE
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
        dirp->d_type = DT_UNKNOWN;
#endif
        strncpy(dirp->d_name, readdir_resp.dirent_array[i].d_name, name_max);
        dirp->d_name[name_max] = 0;
        pd->s->file_pointer += sizeof(struct dirent64);
        bytes += sizeof(struct dirent64);
        dirp++;
    }
    gen_mutex_unlock(&pd->s->lock);
    free(readdir_resp.dirent_array);
    return bytes;

errorout:
    gen_mutex_unlock(&pd->s->lock);
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
    const PVFS_credential *credential,
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
    const PVFS_credential *credential,
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
    int followflag = PVFS2_LOOKUP_LINK_FOLLOW;
    int uid = -1, gid = -1;
    PVFS_object_ref file_ref;
    PVFS_credential *credential;
    PVFS_sys_attr attr;

    /* Initialize */
    memset(&file_ref, 0, sizeof(file_ref));
    memset(&attr, 0, sizeof(attr));

    /* Initialize the system interface for this process */
    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* Attempt to find file */
    if (flags & AT_SYMLINK_NOFOLLOW)
    {
        followflag = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    }

    /* lookup parent */
    rc = iocommon_lookup((char *)pvfs_path,
                         followflag,
                         NULL,
                         &file_ref,
                         NULL,
                         pdir);
    IOCOMMON_RETURN_ERR(rc);

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
    return rc;
}

int iocommon_statfs(pvfs_descriptor *pd, struct statfs *buf)
{
    int rc = 0;
    int orig_errno = errno;
    int block_size = 2*1024*1024; /* optimal transfer size 2M */
    PVFS_credential *credential;
    PVFS_sysresp_statfs statfs_resp;
    
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize the system interface for this process */
    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }
    memset(&statfs_resp, 0, sizeof(statfs_resp));

    errno = 0;
    rc = PVFS_sys_statfs(pd->s->pvfs_ref.fs_id,
                         credential,
                         &statfs_resp,
                         NULL);
    IOCOMMON_CHECK_ERR(rc);
    /* assign fields for statfs struct */
    /* this is a fudge because they don't line up */
    buf->f_type = PVFS2_SUPER_MAGIC;
    buf->f_bsize = block_size; 
    buf->f_blocks = statfs_resp.statfs_buf.bytes_total / 1024;
    buf->f_bfree = statfs_resp.statfs_buf.bytes_available / 1024;
    buf->f_bavail = statfs_resp.statfs_buf.bytes_available / 1024;
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
    PVFS_credential *credential;
    PVFS_sysresp_statfs statfs_resp;
    
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize the system interface for this process */
    PVFS_INIT(pvfs_sys_init);
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }
    memset(&statfs_resp, 0, sizeof(statfs_resp));

    errno = 0;
    rc = PVFS_sys_statfs(pd->s->pvfs_ref.fs_id,
                         credential,
                         &statfs_resp,
                         NULL);
    IOCOMMON_CHECK_ERR(rc);
    /* assign fields for statfs struct */
    /* this is a fudge because they don't line up */
    buf->f_type = PVFS2_SUPER_MAGIC;
    buf->f_bsize = block_size; 
    buf->f_blocks = statfs_resp.statfs_buf.bytes_total / 1024;
    buf->f_bfree = statfs_resp.statfs_buf.bytes_available / 1024;
    buf->f_bavail = statfs_resp.statfs_buf.bytes_available / 1024;
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
    rc = iocommon_readorwrite_nocache(PVFS_IO_READ,
                                      &pd->s->pvfs_ref,
                                      *offset + bytes_read,
                                      buffer,
                                      mem_req,
                                      file_req);
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
        rc = iocommon_readorwrite_nocache(PVFS_IO_READ,
                                          &pd->s->pvfs_ref,
                                          *offset + bytes_read,
                                          buffer,
                                          mem_req,
                                          file_req);
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
    PVFS_credential *credential;
    PVFS_ds_keyval key, val;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    key.buffer = (char *)key_p;
    key.buffer_sz = strlen(key_p) + 1;
    val.buffer = val_p;
    val.buffer_sz = size;

    /* now get attributes */
    errno = 0;
    rc = PVFS_sys_geteattr(pd->s->pvfs_ref,
                          credential,
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
    PVFS_credential *credential;
    PVFS_ds_keyval key, val;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

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
                          credential,
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

/** Implelments atomic operations on an extended attribute
 *
 *  The flag parameter is currently not implemented.
 *  The PVFS server enforces namespaces as prefixes on the
 *  attribute keys.  Thus they are not checked here.
 *  Probably would be more efficient to do so.
 */
int iocommon_atomiceattr(pvfs_descriptor *pd,
                         const char *key_p,
                         void *val_p,
                         int valsize,
                         void *response,
                         int respsize,
                         int flag,
                         int opcode)
{
    int rc = 0;
    int pvfs_flag = 0;
    int orig_errno = errno;
    PVFS_credential *credential;
    PVFS_ds_keyval key, val, resp;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));
    memset(&resp, 0, sizeof(resp));

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    key.buffer = (char *)key_p;
    key.buffer_sz = strlen(key_p) + 1;
    val.buffer = (void *)val_p;
    val.buffer_sz = valsize;
    resp.buffer = (void *)response;
    resp.buffer_sz = respsize;
    

    /* now perform atomic operation on attributes */
    errno = 0;
    rc = PVFS_sys_atomiceattr(pd->s->pvfs_ref,
                              credential,
                              &key,
                              &val,
                              &resp,
                              pvfs_flag,
                              opcode,
                              NULL);
    
    /* NOTE: Are these really the only values we need to translate? */
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
    PVFS_credential *credential;
    PVFS_ds_keyval key;

    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }
    /* Initialize */
    memset(&key, 0, sizeof(key));

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    key.buffer = (char *)key_p;
    key.buffer_sz = strlen(key_p) + 1;

    /* now set attributes */
    errno = 0;
    rc = PVFS_sys_deleattr(pd->s->pvfs_ref,
                           credential,
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
    PVFS_credential *credential;
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

    /* check credential */
    rc = iocommon_cred(&credential);
    if (rc != 0)
    {
        goto errorout;
    }

    /* find number of attributes */
    errno = 0;
    rc = PVFS_sys_listeattr(pd->s->pvfs_ref,
                            token,
                            nkey,
                            credential,
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
                                credential,
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

