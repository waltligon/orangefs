/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Create Function Implementation */

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-dcache.h>
#include <pint-servreq.h>
#include <config-manage.h>
#include <pcache.h>

static int get_bmi_addr(char **name,int count,bmi_addr_t *addr);
static int copy_attributes(PVFS_object_attr new,PVFS_object_attr old,\
	PVFS_size *sz, int handle_count, PVFS_handle *handle_array);
static int do_lookup(PVFS_string name,pinode_reference parent,\
		PVFS_bitfield mask,PVFS_credentials cred,pinode_reference *entry);

/* PVFS_sys_create()
 *
 * create a PVFS file with specified attributes 
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_create(PVFS_sysreq_create *req, PVFS_sysresp_create *resp)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1, ret1 = -1, cflags = 0, name_sz = 0, mask = 0;
	int item = 0;
	PVFS_size handle_size = 0;
	pinode *pinode_ptr = NULL, *item_ptr = NULL;
	PVFS_servreq_create req_create;
	PVFS_servreq_createdirent req_crdirent;
	PVFS_servreq_setattr req_setattr;
	PVFS_servreq_rmdirent req_rmdirent;
	PVFS_servreq_remove req_remove;
	bmi_addr_t serv_addr1,serv_addr2,*bmi_addr_list = NULL;
	char *server = NULL,**io_serv_list = NULL,*segment = NULL;
	PVFS_handle *df_handle_array = NULL,new_bkt = 0,handle_mask = 0,\
		*bkt_array = NULL;
	pinode_reference entry;
	int io_serv_cnt = 0, i = 0, j = 0;
	
	/* Allocate the pinode */
	ret = PINT_pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto pinode_alloc_failure;
	}

	/* Check for pinode existence */

	/* Lookup handle(if it exists) in dcache */
	/* TODO: Should I do lookup to get the handle in case I don't
	 * find the entry in the dcache. In this case, it may end up
	 * creating the pinode. Is that what we want?
	 */
	ret = PINT_dcache_lookup(req->entry_name,req->parent_refn,&entry);
	if (ret < 0)
	{
		/* Entry not in dcache */

		/* Set the attribute mask */
		mask = ATTR_BASIC;
		/* Entry not in dcache, do a lookup */
		ret = do_lookup(req->entry_name,req->parent_refn,mask,\
				req->credentials,&entry);
		if (ret < 0)
		{
			entry.handle = -1;
		}
	}
	/* Do only if pinode reference exists */
	if (entry.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		/* Search in pinode cache */
		ret = PINT_pcache_lookup(entry,pinode_ptr);
		if (ret < 0)
		{
			goto pinode_get_failure;
		}
		/* Is pinode present? */
		if (pinode_ptr->pinode_ref.handle != -1)
		{
			/* Pinode is present, remove it */
			ret = PINT_pcache_remove(entry,&item_ptr);
			if (ret < 0)
			{
				goto pinode_remove_failure;
			}
			/* Free pinode removed from the cache */
			PINT_pcache_pinode_dealloc(item_ptr);
		}
		/* Remove dcache entry */
		ret = PINT_dcache_remove(req->entry_name,req->parent_refn,&item);
		if (ret < 0)
		{
			goto pinode_remove_failure;
		}
	}
	
	/* Revalidate the parent handle */
	/* Get the parent pinode */
	cflags = HANDLE_VALIDATE;
	/* Search in pinode cache */
	ret = PINT_pcache_lookup(req->parent_refn,pinode_ptr);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}
	/* Was pinode present? */
	if (pinode_ptr->pinode_ref.handle != -1)
	{
		cflags = HANDLE_VALIDATE;
		mask = req->attrmask + ATTR_BASIC;
		ret = phelper_validate_pinode(pinode_ptr,cflags,mask,req->credentials);
		if (ret < 0)
		{
			goto pinode_remove_failure;
		}
		/* Free the pinode */
		PINT_pcache_pinode_dealloc(pinode_ptr);
	}
	
	/* Determine the initial metaserver for new file */
	ret = config_bt_get_next_meta_bucket(req->parent_refn.fs_id,&server,\
			&new_bkt,&handle_mask);	
	if (ret < 0)
	{
		goto pinode_remove_failure;				
	}
	ret = BMI_addr_lookup(&serv_addr1,server);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}	
	
	/* Fill in the parameters */
	/* TODO: Fill in bucket ID */
	req_create.bucket = new_bkt;
	req_create.handle_mask = handle_mask;
	req_create.fs_id = req->parent_refn.fs_id;
	/* User's responsible for filling this up in the
	 * attrmask before passing it. Should we assume
	 * a default?
	 */
	req_create.object_type = req->attr.objtype;
	
	/* Server request */
	ret = pint_serv_create(&req_job,&ack_job,&req_create,req->credentials,\
			&serv_addr1);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}
	/* New entry pinode reference */
	entry.handle = ack_job->u.create.handle;

	if (server)
		free(server);

	/* Create directory entry server request to parent */
	
	/* Query BTI to get initial meta server */
	ret = config_bt_map_bucket_to_server(&server,req->parent_refn.handle,\
			req->parent_refn.fs_id);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}
	ret = BMI_addr_lookup(&serv_addr2,server);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}
	/* Fill in the parameters */
	name_sz = strlen(req->entry_name);
	req_crdirent.name = (PVFS_string)malloc(name_sz + 1);
	if (!req_crdirent.name)
	{
		ret = -ENOMEM;
		goto crdirent_failure;
	}
	strncpy(req_crdirent.name,req->entry_name,name_sz);
	req_crdirent.name[name_sz] = '\0';
	req_crdirent.new_handle = entry.handle;
	req_crdirent.parent_handle = req->parent_refn.handle;
	req_crdirent.fs_id = req->parent_refn.fs_id;
	
	/* Make server request */
	ret = pint_serv_crdirent(&req_job,&ack_job,&req_crdirent,req->credentials,\
			&serv_addr2);
	if (ret < 0)
	{
		/* Error - EALREADY, file present */
		/* TODO: Handle this */
		goto crdirent_failure;
	}

	/* Determine list of I/O servers */
	/* Get I/O server list and bucket list */
	io_serv_cnt = req->attr.u.meta.nr_datafiles;
	ret = config_bt_get_next_io_bucket_array(req->parent_refn.fs_id,\
			io_serv_cnt,io_serv_list,&bkt_array,&handle_mask);
	if (ret < 0)
	{
		goto get_io_array_failure;
	}

	/* Allocate handle array */
	df_handle_array = (PVFS_handle *)(io_serv_cnt * sizeof(PVFS_handle));
	if (!df_handle_array)
	{
		goto handle_alloc_failure;
	}
	
	/* Allocate BMI address array */
	bmi_addr_list = (PVFS_handle *)malloc(sizeof(PVFS_handle) * io_serv_cnt);
	if (!bmi_addr_list)
	{
		goto bmi_addr_alloc_failure;
	}
	ret = get_bmi_addr(io_serv_list,io_serv_cnt,bmi_addr_list);
	if (ret < 0)
	{
		goto addr_map_failure;
	}

	/* Make create requests to each I/O server */
	for(i = 0;i < io_serv_cnt; i++)
	{
		/* Fill in the parameters */
		/* TODO: Fill in bucket ID */
		req_create.bucket = bkt_array[i];
		req_create.handle_mask = handle_mask;
		req_create.fs_id = req->parent_refn.fs_id;
		/* User's responsible for filling this up in the
	 	 * attrmask before passing it. Should we fill in
	 	 * a default?
	 	 */
		req_create.object_type = req->attr.objtype;
	
		/* Server request */
		ret = pint_serv_create(&req_job,&ack_job,&req_create,req->credentials,\
				&bmi_addr_list[i]);
		if (ret < 0)
		{
			goto create_io_failure;
		}

		/* New entry pinode reference */
		df_handle_array[i] = ack_job->u.create.handle;
		
		/* Free the req,ack jobs */	
		sysjob_free(bmi_addr_list[i],ack_job,ack_job->rsize,BMI_RECV_BUFFER,\
				NULL);
		sysjob_free(bmi_addr_list[i],req_job,req_job->rsize,BMI_SEND_BUFFER,\
				NULL);
	}	

	/* Aggregate all handles and make a setattr request */
	req_setattr.handle = entry.handle;
	req_setattr.fs_id = req->parent_refn.fs_id;
	req_setattr.attrmask = req->attrmask;
	/* Copy the attribute structure */
	ret = copy_attributes(req_setattr.attr,req->attr,&handle_size,io_serv_cnt,\
			df_handle_array);
	if (ret < 0)
	{
		goto create_io_failure;
	}

	/* Make a setattr server request */
	ret = pint_serv_setattr(&req_job,&ack_job,&req_setattr,req->credentials,\
			handle_size,&serv_addr1);
	if (ret < 0)
	{
		goto create_io_failure;
	}

	/* Add entry to dcache */
	segment = (char *)malloc(name_sz + 1);
	strncpy(segment,req->entry_name,name_sz);
	segment[name_sz] = '\0';
	ret = PINT_dcache_insert(segment,entry,req->parent_refn);
	if (ret < 0)
	{
		goto dcache_add_failure;
	}

	/* Allocate the pinode */
	ret = PINT_pcache_pinode_alloc(&pinode_ptr);
	if (ret < 0)
	{
		ret = -ENOMEM;
		goto dcache_add_failure;
	}
	/* Fill up the pinode */
	pinode_ptr->pinode_ref.handle = entry.handle;
	pinode_ptr->pinode_ref.fs_id = req->parent_refn.fs_id;
	pinode_ptr->mask = req->attrmask;
	/* Allocate the handle array */
	pinode_ptr->attr.u.meta.dfh = (PVFS_handle *)malloc(io_serv_cnt\
			* sizeof(PVFS_handle));
	if (!(pinode_ptr->attr.u.meta.dfh))
	{
		ret = -ENOMEM;
		goto dcache_add_failure;
	}
	memcpy(pinode_ptr->attr.u.meta.dfh,df_handle_array,handle_size);
	/* Fill in the timestamps */
	cflags = HANDLE_TSTAMP + ATTR_TSTAMP;
	ret = phelper_fill_timestamps(pinode_ptr);
	if (ret <0)
	{
		goto pinode_fill_failure;
	}
	/* Add pinode to the cache */
	ret = PINT_pcache_insert(pinode_ptr);
	if (ret < 0)
	{
		goto pinode_fill_failure;
	}	

  	return(0); 

pinode_fill_failure:
	if (pinode_ptr->attr.u.meta.dfh)
		free(pinode_ptr->attr.u.meta.dfh);

dcache_add_failure:
	if (segment)
		free(segment);

create_io_failure:
	/* Free the req,ack jobs */
	sysjob_free(serv_addr2,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr2,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

	/* Rollback all create requests */
	/* Issue: What if there is an error during rollback. we could
	 * be left with inconsistent data on the metaserver and or
	 * io servers 
	 */
	/* Make create requests to each I/O server */
	for(j = 0; j < i ; j++)
	{
		/* Make a remove request */
		/* Fill in the parameters */
		req_remove.handle = df_handle_array[j];
		req_remove.fs_id = req->parent_refn.fs_id;
		req_create.object_type = req->attr.objtype;

		/* Server request */
		ret = pint_serv_remove(&req_job,&ack_job,&req_remove,req->credentials,\
				&bmi_addr_list[j]);
		if (ret < 0)
		{
			/* Free the req,ack jobs */	
			sysjob_free(bmi_addr_list[j],ack_job,ack_job->rsize,BMI_RECV_BUFFER,\
				NULL);
			sysjob_free(bmi_addr_list[j],req_job,req_job->rsize,BMI_SEND_BUFFER,\
				NULL);
			goto addr_map_failure;
		}
	
		/* Free the req,ack jobs */	
		sysjob_free(bmi_addr_list[j],ack_job,ack_job->rsize,BMI_RECV_BUFFER,\
				NULL);
		sysjob_free(bmi_addr_list[j],req_job,req_job->rsize,BMI_SEND_BUFFER,\
				NULL);
	}

addr_map_failure:
	/* Free BMI address array */
	if (bmi_addr_list)
		free(bmi_addr_list);

bmi_addr_alloc_failure:
	/* Free handle array */
	if (df_handle_array)
		free(df_handle_array);

handle_alloc_failure:
	/* Need to free the I/O servers list */
	for(i = 0; i < io_serv_cnt;i++)
	{
		if (io_serv_list[i])
			free(io_serv_list[i]);
	}

get_io_array_failure:
	/* Make a rmdirent request */
	req_rmdirent.entry = (char *)malloc(name_sz + 1);
	if (!req_rmdirent.entry)
	{
		goto crdirent_failure;
	}
	strncpy(req_rmdirent.entry,req->entry_name,name_sz);
	req_rmdirent.entry[name_sz] = '\0';
	req_rmdirent.parent_handle = req->parent_refn.handle;
	req_rmdirent.fs_id = req->parent_refn.fs_id;
	ret1 = pint_serv_rmdirent(&req_job,&ack_job,&req_rmdirent,req->credentials,\
			&serv_addr2);

crdirent_failure:
	/* Handle failure gracefully */
	/* Make a remove request */
	/* Fill in the parameters */
	req_remove.handle = entry.handle;
	req_remove.fs_id = req->parent_refn.fs_id;
	/* Server request */
	ret1 = pint_serv_remove(&req_job,&ack_job,&req_remove,req->credentials,\
			&serv_addr1);

addr_lookup_failure:
	if (server)
		free(server);

pinode_remove_failure:
	/* Free the pinode allocated */
	PINT_pcache_pinode_dealloc(pinode_ptr);

pinode_get_failure:
	sysjob_free(serv_addr1,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);
	sysjob_free(serv_addr1,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);

pinode_alloc_failure:	
	return(ret);
}

/*	get_bmi_addr
 *
 * obtains the BMI addresses of servers
 *
 * returns 0 on success, -errno on failure
 */
static int get_bmi_addr(char **name, int count, bmi_addr_t *addr)
{
	int i = 0, ret = 0;

	for(i = 0;i < count;i++)
	{
		ret = BMI_addr_lookup(&addr[i],name[i]);
		if (ret < 0)
		{
			return(ret);	
		}
	}
	return(0);
}

/* copy_attributes
 *
 * copies the attributes from an attribute to another attribute 
 * structure
 *
 * returns 0 on success, -errno on error
 */
static int copy_attributes(PVFS_object_attr new,PVFS_object_attr old,\
	PVFS_size *sz, int handle_count, PVFS_handle *handle_array)
{
	PVFS_size handle_size = handle_count * sizeof(PVFS_handle);

	/* Copy the generic attributes */	
	new.owner = old.owner;
	new.group = old.group;
	new.perms = old.perms;
	new.objtype = old.objtype;

	/* Fill in the metafile attributes */
	/* TODO: Is this going to work? */
#if 0
	/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
	new.u.meta.dist = old.u.meta.dist;
#endif
	new.u.meta.dfh = (PVFS_handle *)malloc(handle_size);
	if (!new.u.meta.dfh)
	{
		return(-ENOMEM);
	}
	memcpy(new.u.meta.dfh,handle_array,handle_size);	
	new.u.meta.nr_datafiles = handle_count;
	*sz = handle_size;
	
	return(0);
}

/* do_lookup()
 *
 * perform a server lookup path request
 *
 * returns 0 on success, -errno on failure
 */
static int do_lookup(PVFS_string name,pinode_reference parent,\
		PVFS_bitfield mask,PVFS_credentials cred,pinode_reference *entry)
{
	struct PVFS_server_req_s *req_job;
	struct PVFS_server_resp_s *ack_job;
	PVFS_servreq_lookup_path req_args;
	char *server = NULL;
	bmi_addr_t serv_addr;
	int ret = 0,count = 0;

	/* Get Metaserver in BMI URL format using the bucket table 
	 * interface */
	/* TODO: Do this...I am assuming that the handle contains the
	 * bucket ID which is used to perform the mapping
	 */
	ret = config_bt_map_bucket_to_server(&server,parent.handle,parent.fs_id);
	if (ret < 0)
	{
		goto map_to_server_failure;
	}
	ret = BMI_addr_lookup(&serv_addr,server);
	if (ret < 0)
	{
		goto addr_lookup_failure;
	}

	/* Fill in the arguments */
	req_args.path = (PVFS_string)malloc(strlen(name) + 1);
	if (!req_args.path)
	{
		ret = -ENOMEM;
		goto addr_lookup_failure;
	}
	strncpy(req_args.path,name,strlen(name));
	req_args.path[strlen(name)] = '\0';
	req_args.starting_handle = parent.handle;
	req_args.fs_id = parent.fs_id;
	req_args.attrmask = mask;

	/* Make a lookup_path server request to get the handle and
	 * attributes of segment
	 */
	ret = pint_serv_lookup_path(&req_job,&ack_job,&req_args,\
			cred,&serv_addr);
	if (ret < 0)
	{
		goto lookup_path_failure;
	}
	count = ack_job->u.lookup_path.count;
	if (count != 1)
	{
		ret = -EINVAL;
		goto lookup_path_failure;
	}

	/* Fill up pinode reference for entry */
	entry->handle = ack_job->u.lookup_path.handle_array[1];

	/* Free the jobs */
	sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

	return(0);

lookup_path_failure:
	if (req_args.path)
		free(req_args.path);
addr_lookup_failure:
	if (server)
		free(server);

map_to_server_failure:
	sysjob_free(serv_addr,ack_job,ack_job->rsize,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr,req_job,req_job->rsize,BMI_SEND_BUFFER,NULL);

	return(ret);
}
