/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file includes definitions of common internal utility functions */

#include <string.h>
#include <assert.h>
#include "pvfs2-types.h"
#include "pint-util.h"
#include "gen-locks.h"

static int current_tag = 1;
static gen_mutex_t current_tag_lock = GEN_MUTEX_INITIALIZER;

PVFS_msg_tag_t PINT_util_get_next_tag(void)
{
    PVFS_msg_tag_t ret;

    gen_mutex_lock(&current_tag_lock);
    ret = current_tag;

    /* increment the tag, don't use zero */
    if (current_tag + 1 == PINT_MSG_TAG_INVALID)
    {
	current_tag = 1;
    }
    else
    {
	current_tag++;
    }
    gen_mutex_unlock(&current_tag_lock);

    return ret;
}

int PINT_copy_object_attr(PVFS_object_attr *dest, PVFS_object_attr *src)
{
    int ret = -PVFS_ENOMEM;

    if (dest && src)
    {
	if (src->mask & PVFS_ATTR_COMMON_UID)
        {
            dest->owner = src->owner;
        }
	if (src->mask & PVFS_ATTR_COMMON_GID)
        {
            dest->group = src->group;
        }
	if (src->mask & PVFS_ATTR_COMMON_PERM)
        {
            dest->perms = src->perms;
        }
	if (src->mask & PVFS_ATTR_COMMON_ATIME)
        {
            dest->atime = src->atime;
        }
	if (src->mask & PVFS_ATTR_COMMON_CTIME)
        {
            dest->ctime = src->ctime;
        }
        if (src->mask & PVFS_ATTR_COMMON_MTIME)
        {
            dest->mtime = src->mtime;
        }
	if (src->mask & PVFS_ATTR_COMMON_TYPE)
        {
            dest->objtype = src->objtype;
        }

        /*
          NOTE:
          we only copy the size out if we're actually a
          datafile object.  sometimes the size field is
          valid when the objtype is a metafile because
          of different uses of the acache.  In this case
          (namely, getattr), the size is stored in the
          acache before this deep copy, so it's okay
          that we're not copying here even though the
          size mask bit is set.

          if we don't do this trick, the metafile that
          caches the size will have it's union data
          overwritten with a bunk size.
        */
        if ((src->mask & PVFS_ATTR_DATA_SIZE) &&
            (src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_DATAFILE))
        {
            dest->u.data.size = src->u.data.size;
        }

	if ((src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_METAFILE) &&
            (src->mask & PVFS_ATTR_META_DFILES))
	{
	    PVFS_size df_array_size = src->u.meta.dfile_count *
                sizeof(PVFS_handle);

            if (df_array_size)
            {
		if ((dest->mask & PVFS_ATTR_META_DFILES) &&
		    dest->u.meta.dfile_count > 0)
                {
                    if (dest->u.meta.dfile_array)
                    {
                        free(dest->u.meta.dfile_array);
                    }
                }
		dest->u.meta.dfile_array = malloc(df_array_size);
		if (!dest->u.meta.dfile_array)
		{
                    return ret;
		}
		memcpy(dest->u.meta.dfile_array,
                       src->u.meta.dfile_array, df_array_size);
	    }
            else
            {
		dest->u.meta.dfile_array = NULL;
	    }
	    dest->u.meta.dfile_count = src->u.meta.dfile_count;
	}

	if ((src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_METAFILE) &&
            (src->mask & PVFS_ATTR_META_DIST))
	{
            assert(src->u.meta.dist_size > 0);

	    if ((dest->mask & PVFS_ATTR_META_DIST))
            {
                PINT_dist_free(dest->u.meta.dist);
            }
            dest->u.meta.dist = PINT_dist_copy(src->u.meta.dist);
            if (dest->u.meta.dist == NULL)
            {
                return ret;
            }
            dest->u.meta.dist_size = src->u.meta.dist_size;
        }

        if (src->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            dest->u.sym.target_path_len = src->u.sym.target_path_len;
            dest->u.sym.target_path = strdup(src->u.sym.target_path);
            if (dest->u.sym.target_path == NULL)
            {
                return ret;
            }
        }

	dest->mask = src->mask;
        ret = 0;
    }
    return ret;
}

void PINT_free_object_attr(PVFS_object_attr *attr)
{
    if (attr)
    {
        if (attr->objtype == PVFS_TYPE_METAFILE)
        {
            if (attr->mask & PVFS_ATTR_META_DFILES)
            {
                if (attr->u.meta.dfile_array)
                {
                    free(attr->u.meta.dfile_array);
                }
            }
            if (attr->mask & PVFS_ATTR_META_DIST)
            {
                if (attr->u.meta.dist)
                {
                    PINT_dist_free(attr->u.meta.dist);
                }
            }
        }
        else if (attr->objtype == PVFS_TYPE_SYMLINK)
        {
            if (attr->mask & PVFS_ATTR_SYMLNK_TARGET)
            {
                if ((attr->u.sym.target_path_len > 0) &&
                    attr->u.sym.target_path)
                {
                    free(attr->u.sym.target_path);
                }
            }
        }
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
