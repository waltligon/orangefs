/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-servreq.h"

static int phelper_refresh_pinode(
    uint32_t mask,
    PINT_pinode **pinode_p,
    PVFS_pinode_reference pref,
    PVFS_credentials credentials);


int phelper_get_pinode(
    PVFS_pinode_reference pref,
    PINT_pinode **pinode_p,
    uint32_t attrmask,
    PVFS_credentials credentials)
{
    int ret = 0, pinode_valid = 0;

    *pinode_p = PINT_pcache_lookup(pref);
    if (*pinode_p != NULL)
    {
        pinode_valid = (PINT_pcache_pinode_status(*pinode_p) ==
                        PINODE_STATUS_VALID);
        if (!pinode_valid)
        {
            pinode_valid =
                (((*pinode_p)->attr.mask & attrmask) != attrmask);
        }
    }

    if (!pinode_valid)
    {
        ret = phelper_refresh_pinode(
            attrmask, pinode_p, pref, credentials);
        if (ret < 0)
        {
            goto pinode_refresh_failure;	
        }
        PINT_pcache_set_valid(*pinode_p);
    }
    return 0;

  pinode_refresh_failure:
    PINT_pcache_release(*pinode_p);
    return ret;
}

int phelper_release_pinode(PINT_pinode *pinode)
{
    PINT_pcache_release(pinode);
    return 0;
}

/* phelper_refresh_pinode
 *
 * update the contents of the pinode by making a getattr server
 * request
 *
 * returns 0 on success, -errno on failure
 */
static int phelper_refresh_pinode(
    uint32_t mask,
    PINT_pinode **pinode_ptr,
    PVFS_pinode_reference pref,
    PVFS_credentials credentials)
{
    int ret = 0;
    PVFS_object_attr tmp_attr;

    assert(pinode_ptr);

    ret = PINT_sys_getattr(pref, mask, credentials, &tmp_attr);
    if (ret < 0)
    {
        return ret;
    }

    /* getattr placed entry in pcache */
    *pinode_ptr = PINT_pcache_lookup(pref);
    assert(*pinode_ptr);

    ret = phelper_fill_attr(*pinode_ptr, tmp_attr);
    if (ret < 0)
    {
        return ret;
    }
    return 0;
}


/* phelper_fill_timestamps
 *
 * update the pinode's timestamps
 *
 * returns 0 on success, -errno on failure
 */
int phelper_fill_timestamps(PINT_pinode *pnode)
{
#if 0
    int ret = 0;
    struct timeval cur_time;
    long value = 0;

    /* Get the time */
    ret = gettimeofday(&cur_time,NULL);
    if (ret < 0)
    {
        return(-1);	
    }
    /* Initialize the timestamps */
    memset(&(pnode->tstamp),0,sizeof(struct timeval));

    /* Update timestamp */
    /* Check for sum of usecs adding an extra second */
    value = cur_time.tv_usec + handle_to.tv_usec;
    if (value >= 1000000)
    {
        pnode->tstamp.tv_usec = value % 1000000;
        pnode->tstamp.tv_sec = cur_time.tv_sec + handle_to.tv_sec + 1;
    }
    else
    {
        pnode->tstamp.tv_usec = value;
        pnode->tstamp.tv_sec = cur_time.tv_sec + handle_to.tv_sec;
    }
#endif
    return(0);
}


/* phelper_fill_attr
 *
 * fill in the attributes for a pinode - works for either a newly
 * created pinode or an existing one that just needs certain
 * fields modified.
 *
 * returns 0 on success, -errno on error
 */
int phelper_fill_attr(PINT_pinode *ptr, PVFS_object_attr attr)
{
	PVFS_size df_array_size = 0;

	/* set common attributes if needed */
	if(attr.mask & PVFS_ATTR_COMMON_UID)
		ptr->attr.owner = attr.owner;
	if(attr.mask & PVFS_ATTR_COMMON_GID)
		ptr->attr.group = attr.group;
	if(attr.mask & PVFS_ATTR_COMMON_PERM)
		ptr->attr.perms = attr.perms;
	if(attr.mask & PVFS_ATTR_COMMON_ATIME)
		ptr->attr.atime = attr.atime;
	if(attr.mask & PVFS_ATTR_COMMON_CTIME)
		ptr->attr.ctime = attr.ctime;
	if(attr.mask & PVFS_ATTR_COMMON_MTIME)
		ptr->attr.mtime = attr.mtime;
	if(attr.mask & PVFS_ATTR_COMMON_TYPE)
		ptr->attr.objtype = attr.objtype;

	/* set distribution if needed */
	if ((attr.mask & PVFS_ATTR_META_DFILES) &&
            (attr.u.meta.dfile_count > 0))
	{
		df_array_size = attr.u.meta.dfile_count *
                    sizeof(PVFS_handle);

		if (ptr->attr.u.meta.dfile_array)
                    free(ptr->attr.u.meta.dfile_array);
		ptr->attr.u.meta.dfile_array = (PVFS_handle *)
                    malloc(df_array_size);
		if (!(ptr->attr.u.meta.dfile_array))
		{
			return(-ENOMEM);
		}
		memcpy(ptr->attr.u.meta.dfile_array,
                       attr.u.meta.dfile_array, df_array_size);
		ptr->attr.u.meta.dfile_count = attr.u.meta.dfile_count;
	}

	/* set datafile array if needed */
	if ((attr.mask & PVFS_ATTR_META_DIST) &&
            (attr.u.meta.dist_size > 0))
	{
		gossip_lerr("WARNING: packing distribution to memcpy it.\n");
		if(ptr->attr.u.meta.dist)
		{
			gossip_lerr("WARNING: need to free old dist, but I don't know how.\n");
		}
		ptr->attr.u.meta.dist = malloc(attr.u.meta.dist_size);
		if(ptr->attr.u.meta.dist == NULL)
		{
			return(-ENOMEM);
		}
		PINT_Dist_encode(ptr->attr.u.meta.dist, 
			attr.u.meta.dist);
		PINT_Dist_decode(ptr->attr.u.meta.dist, NULL);
	}

	/* add mask to existing values */
	ptr->attr.mask |= attr.mask;

/* 	assert(!(attr.mask & PVFS_ATTR_DATA_ALL)); */
	
	return(0);
}
