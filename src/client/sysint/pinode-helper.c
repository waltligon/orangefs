/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-servreq.h>
#include <config-manage.h>

static int check_pinode_match(pinode *pnode,pinode *pinode_ptr);
static int check_handle_expiry(struct timeval t1,struct timeval t2);
static int check_size_expiry(struct timeval t1,struct timeval t2);
static int check_attribute_expiry(struct timeval t1,struct timeval t2);
static int update_pinode(pinode *pnode,pinode *pinode_ptr,int tstamp);
static int phelper_fill_attr(pinode *ptr,PVFS_object_attr attr,\
		PVFS_bitfield mask);

/* phelper_get_pinode
 *
 * fetches a pinode 
 *
 * returns 0 on success, -errno on failure
 */
int phelper_get_pinode(pinode_reference pref, pinode **pinode_ptr,
		PVFS_bitfield attrmask,int valid_flags,
		int cache_flags,PVFS_credentials credentials)
{
	int ret = 0;
	
	/* Allocate a pinode */
	ret = PINT_pcache_pinode_alloc(pinode_ptr);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto pinode_alloc_failure;
	}
	/* Does pinode exist? */
	ret = PINT_pcache_lookup(pref,*pinode_ptr);
	if (ret < 0)
	{
		goto pinode_refresh_failure;
	}
	/* No errors */
	/* Also check to see if attributes requested thru mask are in 
	 * pinode else refresh it 
	 */
	if ((*pinode_ptr)->pinode_ref.handle == -1 ||\
		(*pinode_ptr)->mask != attrmask)
	{
		/* Pinode does not exist !!! */
		if (cache_flags & SEARCH_NO_FETCH)
		{
			ret = 2;
			goto pinode_refresh_failure;
		}
		/* Is pinode being retrieved for an object already looked up
	 	 * in which case a handle is available
		 * Does a handle exist
	 	 */
		if (pref.handle == 0 && pref.fs_id == 0)
		{
			/* No handle, perform lookup first */
			ret = -1;
			goto pinode_refresh_failure;
		}

		/* Fill the pinode */
		ret = phelper_refresh_pinode(cache_flags,attrmask,(*pinode_ptr),pref,
				credentials);
		if (ret < 0)
		{
			goto pinode_refresh_failure;	
		}

		/* TODO: Should the object name be filled in?
		 */

		/* Return "1" to indicate that pinode was just filled in */
		//return(1);
	}
	else 
	{
		/* Pinode does exist */

		/* Check if pinode contents are valid using timeout values */
		//valid_flags = HANDLE_REUSE + ATTR_REUSE;
		if (((*pinode_ptr)->mask & attrmask) == attrmask)
		{
			/* All requested values are contained in the pinode */
			ret = phelper_validate_pinode(*pinode_ptr,cache_flags,attrmask,
					credentials);
			if (ret < 0)
			{
				goto pinode_refresh_failure;
			}
		}
		else
		{ 
			/* All the requested values are not contained in the pinode 
			 * hence need to be fetched 
			 */
			memset(*pinode_ptr,0,sizeof(pinode));	
			/* Fill the pinode - already allocated */
			ret = phelper_refresh_pinode(cache_flags,attrmask,(*pinode_ptr),pref,\
					credentials);
			if (ret < 0)
			{
				goto pinode_refresh_failure;
			}
		}
	}

	/* Add/Merge the pinode to the pinode cache */
	ret = PINT_pcache_insert(*pinode_ptr);
	if (ret < 0)
	{
		goto pinode_refresh_failure;
	}

	return(0);
	
pinode_refresh_failure:
	/* Free the allocated pinode */
	PINT_pcache_pinode_dealloc(*pinode_ptr);

pinode_alloc_failure:
	return(ret);
}

/* phelper_refresh_pinode
 *
 * update the contents of the pinode by making a getattr server
 * request
 *
 * returns 0 on success, -errno on failure
 */
int phelper_refresh_pinode(int flags,PVFS_bitfield mask,pinode *pinode_ptr,\
		pinode_reference pref, PVFS_credentials credentials)
{
	struct PVFS_server_req_s *req_job;
	struct PVFS_server_resp_s *ack_job;
	PVFS_servreq_getattr req_gattr;
	int ret = 0,tflags = 0;
	bmi_addr_t serv_addr;
	char *server = NULL;

	/* Map handle to server */
	ret = config_bt_map_bucket_to_server(&server,pref.handle,pref.fs_id);
	if (ret < 0)
	{
		return(-1);	
	}
	ret = BMI_addr_lookup(&serv_addr,server);
	if (ret < 0)
	{
		return(-1);
	}
	req_gattr.handle = pref.handle;
	req_gattr.fs_id = pref.fs_id;
	/* Should be based on flag/attrmask */
	/*req_gattr->attrmask = ATTR_UID + ATTR_GID + ATTR_PERM + ATTR_ATIME +\
		ATTR_CTIME + ATTR_MTIME + ATTR_META + ATTR_DATA + ATTR_DIR +\
		ATTR_SYM + ATTR_TYPE;*/
	req_gattr.attrmask = mask;	
	
	/* Setup getattr request to server */
	ret = pint_serv_getattr(&req_job,&ack_job,&req_gattr,credentials,&serv_addr);
	if (ret < 0)
	{
		return(ret);
	}

	/* Fill in the pinode using the server response */
	/* Also take care of filling in the timeouts */
	/* TODO: How to fill in the object name? Does the 
	 * dcache layer pass that name to the pinode helper
	 * layer?
	 */
	pinode_ptr->pinode_ref.handle = pref.handle;
	pinode_ptr->pinode_ref.fs_id = pref.fs_id;
	pinode_ptr->mask = mask;
	
	ret = phelper_fill_attr(pinode_ptr,ack_job->u.getattr.attr,mask);
	if (ret < 0)
	{
		return(ret);
	}

	/* Fill the pinode with timestamps  */
	tflags = HANDLE_TSTAMP + ATTR_TSTAMP;	
	if (req_gattr.attrmask & ATTR_SIZE)
	{
		tflags += SIZE_TSTAMP;	
	}
	ret = phelper_fill_timestamps(pinode_ptr,flags);
	if (ret < 0)
	{
		return(ret);
	}

	return(0);
}

/* phelper_validate_pinode
 *
 * consistency check for the pinode content using timeouts
 *
 * returns 0 on success, -errno on failure
 */
int phelper_validate_pinode(pinode *pnode,int flags,PVFS_bitfield mask,\
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
			ret = phelper_refresh_pinode(flags,mask,pinode_ptr,pnode->pinode_ref,
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
			ret = phelper_refresh_pinode(flags,mask,pinode_ptr,pnode->pinode_ref,
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
static int update_pinode(pinode *pnode,pinode *pinode_ptr,int tstamp)
{
	int ret = 0;

	/* Update the pinode */
	pnode->attr = pinode_ptr->attr;

	/* Fill in pinode with timestamps */
	ret = phelper_fill_timestamps(pnode,tstamp);
	if (ret < 0)
	{	
		return(-1);
	}
	
	return(0);
}

/* phelper_fill_timestamps
 *
 * update the pinode's timestamps
 *
 * returns 0 on success, -errno on failure
 */
int phelper_fill_timestamps(pinode *pnode,int tstamp)
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
	memset(&(pnode->tstamp_handle),0,sizeof(struct timeval));
	memset(&(pnode->tstamp_attr),0,sizeof(struct timeval));

	/* Update handle timestamp */
	if (tstamp | HANDLE_TSTAMP)
	{
		/* Check for sum of usecs adding an extra second */
		value = cur_time.tv_usec + handle_to.tv_usec;
		if (value >= 1000000)
		{
			pnode->tstamp_handle.tv_usec = value % 1000000;
			pnode->tstamp_handle.tv_sec = cur_time.tv_sec + handle_to.tv_sec\
				+ 1;
		}
		else
		{
			pnode->tstamp_handle.tv_usec = value;
			pnode->tstamp_handle.tv_sec = cur_time.tv_sec + handle_to.tv_sec;
		}
	}

	/* Update attribute timestamp */
	if (tstamp | ATTR_TSTAMP)
	{
		/* Check for sum of usecs adding an extra second */
		value = cur_time.tv_usec + attr_to.tv_usec;
		if (value >= 1000000)
		{
			pnode->tstamp_attr.tv_usec = value % 1000000;
			pnode->tstamp_attr.tv_sec = cur_time.tv_sec + attr_to.tv_sec\
				+ 1;
		}
		else
		{
			pnode->tstamp_attr.tv_usec = value;
			pnode->tstamp_attr.tv_sec = cur_time.tv_sec + attr_to.tv_sec;
		}
	}

	/* Update attribute timestamp */
	if (tstamp | SIZE_TSTAMP)
	{
		/* Check for sum of usecs adding an extra second */
		value = cur_time.tv_usec + size_to.tv_usec;
		if (value >= 1000000)
		{
			pnode->tstamp_size.tv_usec = value % 1000000;
			pnode->tstamp_size.tv_sec = cur_time.tv_sec + size_to.tv_sec\
				+ 1;
		}
		else
		{
			pnode->tstamp_size.tv_usec = value;
			pnode->tstamp_size.tv_sec = cur_time.tv_sec + size_to.tv_sec;
		}
	}

	return(0);
}

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

/* modify_pinode
 *
 * modifies a pinode selectively based on a mask provided an
 * attribute structure 
 *
 * returns 0 on success, -errno on failure
 */
int modify_pinode(pinode *node,PVFS_object_attr attr,PVFS_bitfield mask)
{
	PVFS_size dfh_size = 0;
	/* Check mask and accordingly update the pinode */
	if (ATTR_BASIC & mask)
	{
		node->attr.owner = attr.owner;
		node->attr.group = attr.group;
		node->attr.perms = attr.perms;
		node->attr.atime = attr.atime;
		node->attr.mtime = attr.mtime;
		node->attr.ctime = attr.ctime;
		node->attr.objtype = attr.objtype;
	}
	if (ATTR_META & mask)
	{
#if 0
		/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
		node->attr.u.meta.dist = attr.u.meta.dist;
#endif
		dfh_size = sizeof(PVFS_handle) * attr.u.meta.nr_datafiles;
		/* Allocate datafile array */
		free(node->attr.u.meta.dfh);
		node->attr.u.meta.dfh = (PVFS_handle *)malloc(dfh_size);
		if (!node->attr.u.meta.dfh)
			return(-ENOMEM);
		memcpy(node->attr.u.meta.dfh,attr.u.meta.dfh,dfh_size);
		node->attr.u.meta.nr_datafiles = attr.u.meta.nr_datafiles;
	}
	if (ATTR_DIR & mask)
	{

	}
	if (ATTR_SYM & mask)
	{

	}

	/* Finally, copy the attribute mask */
	node->mask = mask;

	return(0);
}

/* phelper_fill_attr
 *
 * fill in the attributes for a pinode 
 *
 * returns 0 on success, -errno on error
 */
static int phelper_fill_attr(pinode *ptr,PVFS_object_attr attr,\
		PVFS_bitfield mask)
{
	PVFS_count32 num_files = attr.u.meta.nr_datafiles;
	PVFS_size size = num_files * sizeof(PVFS_handle);

	ptr->attr = attr;
	if ((mask & ATTR_META) == ATTR_META)
	{
		ptr->attr.u.meta.dfh = (PVFS_handle *)malloc(size);
		if (!(ptr->attr.u.meta.dfh))
		{
			return(-ENOMEM);
		}
		memcpy(ptr->attr.u.meta.dfh,attr.u.meta.dfh,size);
		ptr->attr.u.meta.nr_datafiles = num_files;
#if 0
		/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
		ptr->attr.u.meta.dist = attr.u.meta.dist;
#endif
	}
	if ((mask & ATTR_DATA) == ATTR_DATA)
	{

	}
	
	return(0);

}
