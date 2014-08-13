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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pvfs2-internal.h"
#include "pvfs-path.h"


/* 
 * Takes a path that may be relative to the working dir and
 * expands it all the way to the root, removes extra
 * slashes, single dots and double dots.  Assumes there
 * are no symbolic links.  If there are we assume this
 * will fail lookup later and we will have to run
 * pvfs_expand_path to work out the symbolic links.
 *
 * Sets PATH_QUALIFIED to indicate a successful run of this routine
 * Clears all other flags.
 *
 * code taken from realpath
 *
 * NOTE:  if you modify this function (after 08/08/2012), 
 * remember to make the same modifications to PVFS_expand_path()
 * in pvfs-path.c
 *
 * NOTE:  this function was removed from pvfs-path.c and put
 * into its own file, so it can be called from other functions
 * when --disable-usrint is specified at configure time.
 */
char *PVFS_qualify_path(const char *path)
{
    int ret GCC_UNUSED = 0;
    char *opath = NULL;
    char *npath = NULL;
    PVFS_path_t *Ppath;

    if (!path)
    {
        return NULL;
    }
    /* see if this is a PVFS path if not make one */
    Ppath = PVFS_path_from_expanded((char *)path);
    if (!VALID_PATH_MAGIC(Ppath))
    {
        Ppath = PVFS_new_path(path);
    }
    opath = Ppath->orig_path;
    npath = Ppath->expanded_path;

    /* if we need to start over at qualify clear flags first */
    if (PATH_QUALIFIED(Ppath) || PATH_EXPANDED(Ppath))
    {
        return Ppath->expanded_path;
    }

    CLEAR_ERROR(Ppath);
    CLEAR_MNTPOINT(Ppath);
    CLEAR_RESOLVED(Ppath);
    CLEAR_EXPANDED(Ppath);
    CLEAR_LOOKEDUP(Ppath);
    Ppath->pvfs_path = NULL;
    Ppath->filename = NULL;
    Ppath->fs_id = 0;
    Ppath->handle = 0;
    /* now qualify the path */
    memset(npath, 0, PVFS_PATH_MAX + 1);
    if(*opath != '/')
    {
        if (!getcwd(Ppath->expanded_path, PVFS_PATH_MAX - 2))
        {
            return NULL;
        }
        npath += strnlen(npath, PVFS_PATH_MAX);
        if (npath[-1] != '/')
            *npath++ = '/';
    }       
    else
    {
        *npath++ = '/';
        opath++;
    }
    /* Expand each slash-separated pathname component. */
    while (*opath != '\0')
    {
        /* ignore stray '/' */
        if (*opath == '/')
        {
            opath++;
            continue;
        }
        if (*opath == '.' && (opath[1] == '\0' || opath[1] == '/'))
        {
            /* Ignore "." */
            opath++;
            continue;
        }   
        if (*opath == '.' && opath[1] == '.' &&
            (opath[2] == '\0' || opath[2] == '/'))
        {   
            /* Backup for ".." */
            opath += 2;
            while (npath > Ppath->expanded_path + 1 && (--npath)[-1] != '/')
                ; 
            continue;
        }   
        /* Safely copy the next pathname component. */
        while (*opath != '\0' && *opath != '/')
        {
            if (npath - Ppath->expanded_path > PVFS_PATH_MAX - 2)
            {
                ret = -PVFS_ENAMETOOLONG;
                goto err;
            }
            *npath++ = *opath++;
        }
        /* cap off this segment */
        *npath++ = '/';
    }
    /* Delete trailing slash but don't whomp a lone slash. */
    if (npath != Ppath->expanded_path + 1 && npath[-1] == '/')
        npath--;
    /* Make sure it's null terminated. */
    *npath = '\0';

    SET_QUALIFIED(Ppath);
    /* if this was already a PVFS_path then this is
     * the same path that was passed in
     */
    return Ppath->expanded_path;

err:
    return NULL;
}/*end PVFS_quality_path*/
