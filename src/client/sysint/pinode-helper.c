/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-servreq.h"

#if 0
static int check_pinode_match(pinode *pnode,pinode *pinode_ptr);
static int update_pinode(pinode *pnode,pinode *pinode_ptr);
#endif

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
int phelper_refresh_pinode(uint32_t mask, pinode **pinode_ptr,
                           PVFS_pinode_reference pref,
                           PVFS_credentials credentials)
{
	int ret = 0;
	PVFS_sysresp_getattr resp;

	ret = PVFS_sys_getattr(pref, mask, credentials, &resp);
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

	ret = phelper_fill_attr(*pinode_ptr,resp.attr);
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

#if 0

/* phelper_validate_pinode
 *
 * consistency check for the pinode content using timeouts
 *
 * returns 0 on success, -errno on failure
 */
int phelper_validate_pinode(pinode *pnode,int flags,uint32_t mask,\
		PVFS_credentials credentials)
{
	struct timeval cur_time; /* Current time */
	int ret = 0;
	pinode *pinode_ptr = NULL;
	int tstamp = 0;

	/* Get current time */
	ret = gettimeofday(&cur_time,NULL);
	if (ret < 0)
	{
		return(ret);
	}

	/* Does the handle need to be validated? */
	if (flags & HANDLE_VALIDATE)
	{
		/* If size to be revalidated, fetch distribution */
		if (flags & SIZE_VALIDATE)
			mask |= ATTR_META;

		/* Check for handle expiry using the handle timestamp value */
		ret = check_handle_expiry(cur_time,pnode->tstamp_handle);
		if (ret < 0)
		{
			/* Handle could have changed, need to verify this */
			/* Server getattr request */
			/* Allocate a pinode */
			ret = PINT_pcache_pinode_alloc(&pinode_ptr);
			if (ret < 0)
			{
				ret = -ENOMEM;
				return(ret);
			}
			ret = phelper_refresh_pinode(mask,pinode_ptr,pnode->pinode_ref,
					credentials);
			if (ret < 0)
			{
				/* Free the memory allocated for pinode */
				PINT_pcache_pinode_dealloc(pinode_ptr);
				return(ret);
			}

			/* Check if most attributes(maybe a few select attributes)
			 * match with those earlier for the PVFS object 
			 */
			ret = check_pinode_match(pnode,pinode_ptr);
			if (ret < 0)
			{
				/* No match */
				/* Handle-file relationship has probably changed, need
				 * to redo lookup
				 */
				/* Free the memory allocated for pinode */
				PINT_pcache_pinode_dealloc(pinode_ptr);
				return(ret); /* handle info has changed */
			}
			else 
			{
				/* Match */
				/* Update the original pinode with new values 
				 * and also update both the timestamps.
				 */
				tstamp = HANDLE_TSTAMP + ATTR_TSTAMP;
				ret = update_pinode(pnode,pinode_ptr,tstamp);
				if (ret < 0)
				{
					return(ret);
				}
				/* Free the memory allocated for pinode */
				PINT_pcache_pinode_dealloc(pinode_ptr);
				/* Safe to use the pinode values */
				return(0); 
			}

		}
	}

	/* Do the attributes need to be validated? */
	if (flags & ATTR_VALIDATE)
	{
		/* If size to be revalidated, fetch distribution */
		if (flags & SIZE_VALIDATE)
			mask |= ATTR_META;

		/* Check for attribute expiry using timestamp value */
		ret = check_attribute_expiry(cur_time,pnode->tstamp_attr);
		if (ret < 0)
		{
			/* Attributes may have become inconsistent, so fill up
			 * pinode again
			 */
			/* Allocate a pinode */
			ret = PINT_pcache_pinode_alloc(&pinode_ptr);
			if (ret < 0)
			{
				ret = -ENOMEM;
				return(ret);
			}
			/* makes a server getattr request */
			ret = phelper_refresh_pinode(mask,pinode_ptr,pnode->pinode_ref,
					credentials);
			if (ret < 0)
			{
				/* Free the memory allocated for pinode */
				PINT_pcache_pinode_dealloc(pinode_ptr);
				return(-1);
			}
			else
			{
				/* Update the original pinode with new values 
				 * and also update the attribute timestamp.
				 */
				tstamp = ATTR_TSTAMP;
				ret = update_pinode(pnode,pinode_ptr,tstamp);
				if (ret < 0)
				{
					return(ret);
				}
				/* Free the memory allocated for pinode */
				PINT_pcache_pinode_dealloc(pinode_ptr);
				return(0);
			}
		}
	}

	/* Does the size need to be revalidated? */
	if (flags & SIZE_VALIDATE)
	{
		/* Check for size expiry using timestamp value */
		ret = check_size_expiry(cur_time,pnode->tstamp_size);
		if (ret < 0)
		{
			/* Use the distribution to determine the I/O servers */

			/* Send a getattr request to each I/O server requesting
		 	 *  size
		 	 */

			/* Pass all the sizes to the distribution function that
		 	 * will calculate logical size 
		 	 */

			/* Update the pinode and its timestamp*/

		}
	}

	/* Pinode need not be updated. So return success */
	return(0);
}

#endif

#if 0
/* check_handle_expiry
 *
 * check to determine if handle is stale based on timeout value
 *
 * returns 0 on success, -1 on failure
 */
static int check_handle_expiry(struct timeval t1,struct timeval t2)
{
	/* Does handle timestamp exceed the current time?
	 * If yes, handle is valid. If no, handle is stale.
	 */
	if (t2.tv_sec > t1.tv_sec || (t2.tv_sec == t1.tv_sec &&\
				t2.tv_usec > t1.tv_usec))
			return(0);

	/* Handle is stale */
	return(-1);
}

/* check_attribute_expiry
 *
 * check to determine if attributes are stale based on timeout value
 *
 * returns 0 on success, -1 on failure
 */
static int check_attribute_expiry(struct timeval t1,struct timeval t2)
{
	/* Does attribute timestamp exceed the current time?
	 * If yes, attributes are valid. If no, attributes are stale.
	 */
	if (t2.tv_sec > t1.tv_sec || (t2.tv_sec == t1.tv_sec &&\
				t2.tv_usec > t1.tv_usec))
			return(0);

	/* Attributes are stale */
	return(-1);
}

/* check_size_expiry
 *
 * check to determine if cached size is stale based on timeout value
 *
 * returns 0 on success, -1 on failure
 */
static int check_size_expiry(struct timeval t1,struct timeval t2)
{
	/* Does size timestamp exceed the current time?
	 * If yes, size is valid. If no, size is stale.
	 */
	if (t2.tv_sec > t1.tv_sec || (t2.tv_sec == t1.tv_sec &&\
				t2.tv_usec > t1.tv_usec))
			return(0);

	/* Size is stale */
	return(-1);
}

/* update_pinode
 *
 * updates a pinode with values from another pinode
 *
 * returns 0 on success, -1 on failure
 */
static int update_pinode(pinode *pnode,pinode *pinode_ptr)
{
	int ret = 0;

	/* Update the pinode */
	pnode->attr = pinode_ptr->attr;

	/* Fill in pinode with timestamps */
	ret = phelper_fill_timestamps(pnode);
	if (ret < 0)
	{	
		return(-1);
	}
	
	return(0);
}
#endif

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

#if 0
/* check_pinode_match
 *
 * compares two pinodes to check if they are the same
 *
 * returns 0 on success, -errno on failure
 */
static int check_pinode_match(pinode *pnode,pinode *pinode_ptr)
{
	/* TODO: Should the object name also be compared? */
	/*if (!memcmp(&pnode->attr,&pinode_ptr->attr,sizeof(PVFS_object_attr)))
	 */
	if (pnode->attr.owner == pinode_ptr->attr.owner &&\
		 pnode->attr.group == pinode_ptr->attr.group &&\
		 pnode->attr.objtype == pinode_ptr->attr.objtype &&\
		 pnode->attr.perms == pinode_ptr->attr.perms)
		return(0);
	else 
		return(-1);
}
#endif

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
	PVFS_size df_array_size = attr.u.meta.dfile_count * sizeof(PVFS_handle);

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
	if ((attr.mask & PVFS_ATTR_META_DIST) &&
            (attr.u.meta.dfile_count > 0))
	{
		if(ptr->attr.u.meta.dfile_array)
			free(ptr->attr.u.meta.dfile_array);
		ptr->attr.u.meta.dfile_array = (PVFS_handle *)malloc(df_array_size);
		if (!(ptr->attr.u.meta.dfile_array))
		{
			return(-ENOMEM);
		}
		memcpy(ptr->attr.u.meta.dfile_array, attr.u.meta.dfile_array, df_array_size);
		ptr->attr.u.meta.dfile_count = attr.u.meta.dfile_count;
	}

	/* set datafile array if needed */
	if ((attr.mask & PVFS_ATTR_META_DFILES) &&
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

	assert(!(attr.mask & PVFS_ATTR_DATA_ALL));
	
	return(0);
}
