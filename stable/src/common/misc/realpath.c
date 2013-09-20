/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * realpath.c -- canonicalize pathname by removing symlinks
 * Copyright (C) 1993 Rick Sladkey <jrs@world.std.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library Public License for more details.
 */

#define resolve_symlinks

/*
 * This routine is part of libc.  We include it nevertheless,
 * since the libc version has some security flaws.
 */

#include <limits.h>     /* for PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 8192
#endif
#ifndef WIN32
#include <unistd.h>
#include <sys/syscall.h>
#endif
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "pvfs2-internal.h"
#include "realpath.h"
#include "pvfs2-types.h"
#include "pvfs2-util.h"

#define MAX_READLINKS 32

#ifdef WIN32
/* PINT_realpath()
 *
 * canonicalizes path and places the result into resolved_path.  Includes
 * cleaning of symbolic links, trailing slashes, and .. or . components.
 * maxreslth is the maximum length allowed in resolved_path.
 *
 * returns 0 on success, -PVFS_error on failure.
 */
int PINT_realpath(
    const char *path,
    char *resolved_path,
    int maxreslth)
{
    char *ret_path;

    if (resolved_path == NULL || path == NULL)
        return -PVFS_EINVAL;

    /* just use CRT version for now */
    ret_path = _fullpath(resolved_path, path, maxreslth);

    if (ret_path == NULL)
        return -PVFS_EINVAL;
    
    return 0;
}
#else
/* PINT_realpath()
 *
 * canonicalizes path and places the result into resolved_path.  Includes
 * cleaning of symbolic links, trailing slashes, and .. or . components.
 * maxreslth is the maximum length allowed in resolved_path.
 *
 * returns 0 on succes, -PVFS_error on failure.
 */
int PINT_realpath(
    const char *path,
    char *resolved_path,
    int maxreslth)
{
    int readlinks = 0;
    char *npath;
    char link_path[PATH_MAX + 1];
    int n;
    char *buf = NULL;
    int ret;

    npath = resolved_path;

    /* If it's a relative pathname use getcwd for starters. */
    if (*path != '/')
    {
        if (!getcwd(npath, maxreslth - 2))
        {
            return(-PVFS_EINVAL);
        }
        npath += strlen(npath);
        if (npath[-1] != '/')
            *npath++ = '/';
    }
    else
    {
        *npath++ = '/';
        path++;
    }

    /* Expand each slash-separated pathname component. */
    while (*path != '\0')
    {
        /* Ignore stray "/" */
        if (*path == '/')
        {
            path++;
            continue;
        }
        if (*path == '.' && (path[1] == '\0' || path[1] == '/'))
        {
            /* Ignore "." */
            path++;
            continue;
        }
        if (*path == '.' && path[1] == '.' &&
            (path[2] == '\0' || path[2] == '/'))
        {
            /* Backup for ".." */
            path += 2;
            while (npath > resolved_path + 1 && (--npath)[-1] != '/')
                ;
            continue;
        }
        /* Safely copy the next pathname component. */
        while (*path != '\0' && *path != '/')
        {
            if (npath - resolved_path > maxreslth - 2)
            {
                ret = -PVFS_ENAMETOOLONG;
                goto err;
            }
            *npath++ = *path++;
        }

        /* Protect against infinite loops. */
        if (readlinks++ > MAX_READLINKS)
        {
            ret = -PVFS_ELOOP;
            goto err;
        }

        /* See if last pathname component is a symlink. */
        *npath = '\0';

#ifndef BUILD_USRINT
        /* see if this part of the path has a PVFS mount point */
        ret = PVFS_util_resolve_absolute(resolved_path);
        /* we don't care about the output of resolve */
        /* link_path was just a placeholder */
        memset(link_path, 0, PATH_MAX);
        if (ret == 0)
        {
            n = readlink(resolved_path, link_path, PATH_MAX);
        }
        else
        {
            n = syscall(SYS_readlink, resolved_path, link_path, PATH_MAX);
#if 0
            /* this doesn't work, a syscall should certainly work */
            n = glibc_ops.readlink(resolved_path, link_path, PATH_MAX);
#endif
        }
#else
        n = readlink(resolved_path, link_path, PATH_MAX);
#endif /* BUILD_USRINT */
        if (n < 0)
        {
            /* EINVAL means the file exists but isn't a symlink. */
            if (errno != EINVAL)
            {
                ret = -PVFS_EINVAL;
                goto err;
            }
        }
        else
        {
#ifdef resolve_symlinks /* Richard Gooch dislikes sl resolution */
            int m;

            /* Note: readlink doesn't add the null byte. */
            link_path[n] = '\0';
            if (*link_path == '/')
                /* Start over for an absolute symlink. */
                npath = resolved_path;
            else
                /* Otherwise back up over this component. */
                while (*(--npath) != '/')
                    ;

            /* Insert symlink contents into path. */
            m = strlen(path);
            if (buf)
                free(buf);
            buf = malloc(m + n + 1);
            if(!buf)
            {
                ret = -PVFS_ENOMEM;
                goto err;
            }
            memcpy(buf, link_path, n);
            memcpy(buf + n, path, m + 1);
            path = buf;
#endif
        }
        *npath++ = '/';
    }
    /* Delete trailing slash but don't whomp a lone slash. */
    if (npath != resolved_path + 1 && npath[-1] == '/')
        npath--;
    /* Make sure it's null terminated. */
    *npath = '\0';

    if (buf)
        free(buf);
    return 0;

  err:
    if (buf)
        free(buf);
    return ret;
}
#endif /* WIN32 */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
