/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-servreq.h"

static int phelper_refresh_pinode(uint32_t mask, pinode **pinode_ptr,
		PVFS_pinode_reference pref,PVFS_credentials credentials);


/* phelper_get_pinode
 *
 * fetches a pinode 
 *
 * returns 0 on success, -errno on failure
 */
int phelper_get_pinode(PVFS_pinode_reference pref, pinode **pinode_ptr,
		uint32_t attrmask, PVFS_credentials credentials)
{
	int ret = 0;
	
	/* Does pinode exist? */
	ret = PINT_pcache_lookup(pref, pinode_ptr);

	if (ret == PCACHE_LOOKUP_FAILURE)
	{
		/* Pinode does not exist in cache */
		ret = phelper_refresh_pinode(attrmask, pinode_ptr, pref,
				credentials);
		if (ret < 0)
		{
			goto pinode_refresh_failure;	
		}

		ret = PINT_pcache_insert(*pinode_ptr);
		if (ret < 0)
		{
			goto pinode_insert_failure;	
		}
		PINT_pcache_insert_rls(*pinode_ptr);
	}
	else 
	{
		/* Pinode does exist */

		if (((*pinode_ptr)->attr.mask & attrmask) != attrmask)
		{ 
			/* All the requested values are not contained in the pinode 
			 * hence need to be fetched 
			 */
			memset(*pinode_ptr,0,sizeof(pinode));	
			/* Fill the pinode - already allocated */
			ret = phelper_refresh_pinode( attrmask,
						     pinode_ptr ,pref,
						     credentials);
			if (ret < 0)
			{
				/* can't release before we do pinode refresh */
				PINT_pcache_lookup_rls(*pinode_ptr);
				goto pinode_refresh_failure;
			}

			/* we still have one reference to this from refresh */
			PINT_pcache_lookup_rls(*pinode_ptr);

			/*its already in the cache so we don't need to update it*/
		}
	}

	return(0);
	
pinode_insert_failure:
pinode_refresh_failure:
	return(ret);
}

/* phelper_release_pinode
 *
 * let the pcache know that we're done with this pinode, so its safe to be 
 * reused/deallocated/etc if neccessary.
 *
 * returns 0 on success, -errno on failure
 */
int phelper_release_pinode(pinode *pinode_ptr)
{
	return PINT_pcache_lookup_rls(pinode_ptr);
}

/* phelper_refresh_pinode
 *
 * update the contents of the pinode by making a getattr server
 * request
 *
 * returns 0 on success, -errno on failure
 */
static int phelper_refresh_pinode(uint32_t mask, pinode **pinode_ptr,
                           PVFS_pinode_reference pref,
                           PVFS_credentials credentials)
{
	int ret = 0;
	PVFS_object_attr tmp_attr;

	ret = PINT_sys_getattr(pref, mask, credentials, &tmp_attr);
	if (ret < 0)
	{
		return(ret);
	}

	/* we just added this to the cache, do a lookup to get the pointer to
	 * the pinode we just added to the cache
	 */

	ret = PINT_pcache_lookup(pref, pinode_ptr);
	if (ret == PCACHE_LOOKUP_FAILURE)
	{
		/* we just added this, so if we get here maybe caching is off?*/
		ret = PINT_pcache_pinode_alloc(pinode_ptr);
		if (ret < 0)
		{
			ret = -ENOMEM;
			return(ret);
		}
	}
	(*pinode_ptr)->pinode_ref.handle = pref.handle;
	(*pinode_ptr)->pinode_ref.fs_id = pref.fs_id;

	ret = phelper_fill_attr(*pinode_ptr,tmp_attr);
	if (ret < 0)
	{
		return(ret);
	}

	/* Fill the pinode with timestamp info */
	ret = phelper_fill_timestamps(*pinode_ptr);
	if (ret < 0)
	{
		return(ret);
	}

	return(0);
}


/* phelper_fill_timestamps
 *
 * update the pinode's timestamps
 *
 * returns 0 on success, -errno on failure
 */
int phelper_fill_timestamps(pinode *pnode)
{
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
int phelper_fill_attr(pinode *ptr,PVFS_object_attr attr)
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
