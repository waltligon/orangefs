/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <config-manage.h>

/* Configuration Management Data Structure */
fsconfig_array server_config;

/* State Variables */
int *metaserv = NULL; /* Next Meta Server Index */
int *ioserv = NULL;	/* Next I/O Server Index */
int *metabucket = NULL; /* Next Bucket Index */
int *iobucket = NULL;	/* Next I/O server bucket Index */

/* config_bt_initialize
 *
 * initialize the configuration management data structure
 *
 * returns 0 on success, -1 on failure
 */
int config_bt_initialize(pvfs_mntlist mntent_list)
{
	/* Initialize the config management data struct */
	server_config.nr_fs = mntent_list.nr_entry;
	server_config.fs_info = (fsconfig *)malloc(mntent_list.nr_entry\
			* sizeof(fsconfig));
	if (!server_config.fs_info)
	{
		return(-ENOMEM);
	}
	/* Init the mutex lock */
	server_config.mt_lock = gen_mutex_build();

	/* Meta server index */
	/* Note: memset ensures that the first ioserver index for
	 * each file system is zero so don't need to be set later
	 * unless the first ioserver for a file system is not zero
	 */
	metaserv = (int *)malloc(sizeof(int) * server_config.nr_fs);
	memset(metaserv,0,sizeof(int) * server_config.nr_fs);
	/* I/O server index */
	ioserv = (int *)malloc(sizeof(int) * server_config.nr_fs);
	memset(ioserv,0,sizeof(int) * server_config.nr_fs);

	/* Meta/IO server next bucket index */
	/*metabucket = (int *)malloc(sizeof(int) * server_config.nr_fs);
	iobucket = (int *)malloc(sizeof(int) * server_config.nr_fs);
	*/
	/* Set the indexes */
	/*for(i = 0; i < server_config.nr_fs;i++)
	{
		metabucket[i] = server_config.fs_info[i].bucket_array[0].bucket_st;	
		iobucket[i] = server_config.fs_info[i].io_bucket_array[0].bucket_st;	
	}*/
	return(0);
}

/* config_bt_finalize
 *
 * deallocate the configuration management data structure
 *
 * returns 0 on success, -1 on failure
 */
int config_bt_finalize()
{
	int i = 0, j = 0;

	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Iterate thru each file system */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		/* Free the metaservers, buckets */
		for(j = 0; j < server_config.fs_info[i].meta_serv_count; j++)
		{
			free(server_config.fs_info[i].meta_serv_array[j]);
		}
		free(server_config.fs_info[i].meta_serv_array);
		free(server_config.fs_info[i].bucket_array);
		/* Free the I/O servers, buckets */
		for(j = 0; j < server_config.fs_info[i].io_serv_count; j++)
		{
			free(server_config.fs_info[i].io_serv_array[j]);
		}
		free(server_config.fs_info[i].io_serv_array);
		free(server_config.fs_info[i].io_bucket_array);
	}
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	/* Destroy the mutex */
	gen_mutex_destroy(server_config.mt_lock);
	
	/* Free the file system info */
	free(server_config.fs_info);
		
	return(0);
}

/* config_bt_get_next_meta_bucket
 *
 * get bucket,mask for specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_bt_get_next_meta_bucket(PVFS_fs_id fsid, char **meta_name,\
		PVFS_handle *bucket,PVFS_handle *handle_mask)
{
	int i = 0, name_sz = 0, index = 0;
	int fsindex = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);
	
	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid)
		{
			/* Get the next metaserver index */
			index = metaserv[fsindex];
			name_sz = strlen(server_config.fs_info[i].meta_serv_array[index]);
			*meta_name = (PVFS_string)malloc(name_sz + 1);
			if (!(*meta_name))
			{
				/* Release the mutex */
				gen_mutex_unlock(server_config.mt_lock);
				return(-ENOMEM);
			}
			strncpy(*meta_name,server_config.fs_info[i].meta_serv_array[index],\
					name_sz);
			(*meta_name)[name_sz] = '\0';
			/* Get the bucket after a bit shift */
			*bucket = server_config.fs_info[i].bucket_array[index].bucket_st\
				<< (64 - server_config.fs_info[i].maskbits);
			*handle_mask = server_config.fs_info[i].maskbits;	
			/* Update the next metaserver index */
			metaserv[fsindex] = (index + 1) % server_config.fs_info[i].\
				meta_serv_count;

			/* Release the mutex */
			gen_mutex_unlock(server_config.mt_lock);
			return(0);
		}
	}
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);

	return(-EINVAL);
}

/* config_bt_get_next_io_bucket_array
 *
 * get array of io_servers,buckets,masks for specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_bt_get_next_io_bucket_io_bucket_array(PVFS_fs_id fsid,\
		int num_buckets,char **io_name_array,PVFS_handle **bucket_array,\
		PVFS_handle *handle_mask)
{
	int fsindex = -1, bkt_index = 0, name_sz = 0;
	int i = 0,j = 0, num_io = 0, ret = 0;
	int *lastbucket_array = NULL;

	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs; i++)
	{
		if (fsid == server_config.fs_info[i].fsid)
		{
			num_io = server_config.fs_info[i].io_serv_count;
			fsindex = i; /* Set the fsindex */
			break;
		}
	}
	
	/* If no matching file system found */
	if (fsindex == -1)
	{
		/* Release the mutex */
		gen_mutex_unlock(server_config.mt_lock);
		return(-EINVAL);
	}

	/* Allocate io array */
	*io_name_array = (char *)malloc(sizeof(char *) * num_io);
	if (!(*io_name_array))
	{
		ret = -ENOMEM;
		goto namearrayalloc_failure;
	}
	/* Allocate io array */
	lastbucket_array = (int *)malloc(sizeof(int) * num_io);
	if (!lastbucket_array)
	{
		ret = -ENOMEM;
		goto lastbktalloc_failure;
	}
	/* Allocate bucket array */
	*bucket_array = (PVFS_handle *)malloc(sizeof(PVFS_handle) * num_buckets);
	if (!(*bucket_array))
	{
		ret = -ENOMEM;
		goto bktarrayalloc_failure;
	}
	
	/* Initialize the last bucket array to start bucket of each I/O
	 * server 
	 */
	for(i = ioserv[fsindex],j = 0;j < num_io;j++)
	{
		name_sz = strlen(server_config.fs_info[fsindex].io_serv_array[i]);
		/* copy the I/O servers */
		io_name_array[j] = (char *)malloc(name_sz + 1);
		if (!io_name_array[j])
		{
			ret = -ENOMEM;
			goto ioserveralloc_failure;
		}
		strncpy(io_name_array[j],server_config.fs_info[fsindex].\
				io_serv_array[i],name_sz);
		io_name_array[name_sz] = '\0';
		/* Increment the I/O server number */
		i = (i + 1) % num_io;

		/* Initialize the last bucket array with first bucket from each
		 * I/O server 
		 */
		lastbucket_array[j] = server_config.fs_info[fsindex].bucket_array[j].\
			bucket_st;
	}
	/* Copy the mask bit value */
	*handle_mask = server_config.fs_info[fsindex].maskbits;

	/* Fill in the bucket array */
	while (bkt_index != num_buckets)
	{
		/* Pick a bucket from each I/O server. We assume this 
		 * would lead to maximum parallelism but if number of 
		 * buckets needed is greater than the no. of I/O servers
		 * then assumption is no longer satisfied
		 */
		for(i = ioserv[fsindex],j = 0; j < num_io; j++)
		{
			/* Check if bucket range for an I/O server is exceeded */	
			if (lastbucket_array[i] ==  server_config.fs_info[fsindex].\
					bucket_array[i].bucket_end)
			{
				lastbucket_array[i] = server_config.fs_info[fsindex].\
					bucket_array[i].bucket_st;
			}
			(*bucket_array)[bkt_index] = lastbucket_array[i] <<\
				(64 - *handle_mask);

			lastbucket_array[i]++;
			bkt_index++;
			if (bkt_index == num_buckets)
				break;
			/* Increment the I/O server number */
			i = (i + 1) % num_io;
		}/* For */

	}/* While */

	/* Update the I/O server for the file system */
	ioserv[fsindex] = (ioserv[fsindex] + 1) % num_io;

	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);

	return(0);

ioserveralloc_failure:
	for(i = 0;i < j;i++) {
		if (io_name_array[i])
			free(io_name_array[i]);
	}

bktarrayalloc_failure:
	if (lastbucket_array)
		free(lastbucket_array);

lastbktalloc_failure:
	if (io_name_array)
		free(io_name_array);

namearrayalloc_failure:
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(ret);
}

/* config_bt_map_bucket_to_server
 *
 * get the server associated with the bucket 
 *
 * returns 0 on success, -errno on failure
 */
int config_bt_map_bucket_to_server(char **server_name, PVFS_handle bucket,\
		PVFS_fs_id fsid)
{
	int i = 0,j = 0,name_sz = 0;
	PVFS_handle bkt = 0;
	PVFS_handle bucket_start = 0, bucket_end = 0;
	PVFS_string *meta_serv = NULL;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid) 
		{
			/* HACK: come back and figure out the right way to map a bucket to a
			 * server
			 */
			if (server_config.fs_info[i].maskbits == 0)
			{
				bkt = 0;
			} else {
				bkt = bucket >> ( 63 - (server_config.fs_info[i].maskbits));
			}

			for(j = 0; j < server_config.fs_info[i].meta_serv_count; j++)
			{
				bucket_start = server_config.fs_info[i].bucket_array[j].bucket_st;
				bucket_end = server_config.fs_info[i].bucket_array[j].bucket_end;
				meta_serv = server_config.fs_info[i].meta_serv_array[j];
				if ((bkt >= bucket_start) && (bkt <= bucket_end))
				{
					name_sz = strlen(meta_serv) + 1;
					*server_name = malloc(name_sz);
					memcpy(*server_name, meta_serv, name_sz);
					/* Release the mutex */
					gen_mutex_unlock(server_config.mt_lock);
					return(0);
				}
			}
		}
	}
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	printf("unable to map server to bucket/server combo\n");
	return(-EINVAL);
}

/* config_bt_map_server_to_bucket_array
 *
 * get the number of metaservers in specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_bt_map_server_to_bucket_array(char **server_name,\
		PVFS_handle **bucket_array, PVFS_handle *handle_mask)
{
	/* NOT IMPLEMENTED */
	return(0);
}


/* config_bt_get_num_meta
 *
 * get the number of metaservers in specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_bt_get_num_meta(PVFS_fs_id fsid, int *num_meta)
{
	int i = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid) 
		{
			*num_meta = server_config.fs_info[i].meta_serv_count;
			/* Release the mutex */
			gen_mutex_unlock(server_config.mt_lock);
			return(0);
		}

	}
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(-EINVAL);
}

/* config_bt_get_num_io
 *
 * get the number of ioservers in specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_bt_get_num_io(PVFS_fs_id fsid, int *num_io)
{
	int i = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid) 
		{
			*num_io = server_config.fs_info[i].io_serv_count;
			/* Release the mutex */
			gen_mutex_unlock(server_config.mt_lock);
			return(0);
		}
	}

	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(-EINVAL);
}

/* config_fsi_get_root_handle
 *
 * get root handle of a specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_fsi_get_root_handle(PVFS_fs_id fsid,PVFS_handle *fh_root)
{
	int i = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid) 
		{
			*fh_root = server_config.fs_info[i].fh_root;
			/* Release the mutex */
			gen_mutex_unlock(server_config.mt_lock);
			return(0);
		}
	}
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(-EINVAL);
}

/* config_fsi_get_io_server
 *
 * get I/O servers for a specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_fsi_get_io_server(PVFS_fs_id fsid,char **io_server_array,
		int *num_ioserv)
{
	int i = 0, num_io = 0, fsindex = 0, name_sz = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid) 
		{
			fsindex = fsid;
			break;
		}
	}
	/* Allocate memory for the I/O server array */	
	num_io = server_config.fs_info[fsindex].io_serv_count;
	*io_server_array = (char *)malloc(num_io * sizeof(char *)); 
	if (!(*io_server_array))
	{
		/* Release the mutex */
		gen_mutex_unlock(server_config.mt_lock);
		return(-ENOMEM);
	}
	/* Copy each I/O server */
	for(i = 0;i < num_io;i++)
	{
		name_sz = strlen(server_config.fs_info[fsindex].io_serv_array[i]);
		io_server_array[i] = (char *)malloc(name_sz + 1);
		if (!io_server_array[i])
		{
			while(i >= 0)
			{
				if (io_server_array[i])
					free(io_server_array[i]);
				i--;
			}	
			/* Release the mutex */
			gen_mutex_unlock(server_config.mt_lock);
			return(-ENOMEM);
		}
		strncpy(io_server_array[i],server_config.fs_info[fsindex].\
				io_serv_array[i],name_sz);
		io_server_array[name_sz] = '\0';
	} 
	*num_ioserv = num_io;

	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(0);
}

/* config_fsi_get_meta_server
 *
 * get meta servers for a specified file system
 *
 * returns 0 on success, -errno on failure
 */
int config_fsi_get_meta_server(PVFS_fs_id fsid,char **meta_server_array,\
		int *num_metaserv)
{
	int i = 0, num_meta = 0,fsindex = 0, name_sz = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (fsid == server_config.fs_info[i].fsid) 
		{
			fsindex = fsid;
			break;
		}
	}
	/* Allocate memory for the meta server array */	
	num_meta = server_config.fs_info[fsindex].meta_serv_count;
	*meta_server_array = (char *)malloc(num_meta * sizeof(char *)); 
	if (!(*meta_server_array))
	{	
		/* Release the mutex */
		gen_mutex_unlock(server_config.mt_lock);
		return(-ENOMEM);
	}
	/* Copy each metaserver */
	for(i = 0;i < num_meta;i++)
	{
		name_sz = strlen(server_config.fs_info[fsindex].meta_serv_array[i]);
		meta_server_array[i] = (char *)malloc(name_sz + 1);
		if (!meta_server_array[i])
		{
			while(i >= 0)
			{
				if (meta_server_array[i])
					free(meta_server_array[i]);
				i--;
			}
			/* Release the mutex */
			gen_mutex_unlock(server_config.mt_lock);
			return(-ENOMEM);
		}
		strncpy(meta_server_array[i],server_config.fs_info[fsindex].\
				meta_serv_array[i],name_sz);
		meta_server_array[name_sz] = '\0';
	}
	*num_metaserv = num_meta;

	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(0);
}

/* config_fsi_get_fsid
 *
 * get the file system ID given the client side mount point
 *
 * returns 0 on success, -errno on failure
 */
int config_fsi_get_fsid(PVFS_fs_id *fsid,char *mnt_dir)
{
	int i = 0, mnt_len = strlen(mnt_dir);
	
	/* Grab the mutex */
	gen_mutex_lock(server_config.mt_lock);

	/* Look for matching fsid */
	for(i = 0; i < server_config.nr_fs;i++)
	{
		if (!strncmp(server_config.fs_info[i].local_mnt_dir,mnt_dir,mnt_len)) 
		{
			*fsid = server_config.fs_info[i].fsid;
			break;
		}
	}
	/* Release the mutex */
	gen_mutex_unlock(server_config.mt_lock);
	return(0);
}
