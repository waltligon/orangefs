/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Getconfig Function Implementation */

#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pint-servreq.h>

/* Bucket string size is the max number of digits required to
 * represent the entire range of buckets */
#define BKT_STR_SIZE 7

/* Declarations */
extern fsconfig_array server_config;

/* server_getconfig()
 *
 * gets configuration parameters from a PVFS server 
 *
 * returns 0 on success, -errno on failure
 */
int server_getconfig(pvfs_mntlist mntent_list)
{
	struct PVFS_server_req_s *req_job = NULL;		/* server request */
	struct PVFS_server_resp_s *ack_job = NULL;	/* server response */
	int ret = -1,i = 0,req_size = 0,j = 0, ack_size = 0;
   bmi_addr_t serv_addr;				 /*PVFS address type structure*/ 
	int start = 0,k = 0,name_sz = 0,metalen= 0,iolen = 0, mnt_len = 0;
	PVFS_credentials cred;
	PVFS_servreq_getconfig req_getconfig;
	char bktstr[BKT_STR_SIZE];

	/* Fill up the credentials information */

	/* Process all entries in pvfstab */
	for(i = 0; i < mntent_list.nr_entry;i++) 
	{
   	/* Obtain the metaserver to send the request */
		ret = BMI_addr_lookup(&serv_addr,mntent_list.ptab_p[i].meta_addr);   
		if (ret < 0)
		{
			return(ret);
		}
#define fsmnt_dir mntent_list.ptab_p[i].serv_mnt_dir 
		name_sz = strlen(fsmnt_dir);
		/* Make a server getconfig request */
		req_getconfig.fs_name = (PVFS_string)malloc(name_sz + 1);
		if (!req_getconfig.fs_name)
		{	
			ret = -ENOMEM;
			goto namealloc_failure;
		}
		strncpy(req_getconfig.fs_name,fsmnt_dir,name_sz);	
		req_getconfig.fs_name[name_sz] = '\0';	
		req_getconfig.max_strsize = MAX_STRING_SIZE;
#undef fsmnt_dir
		ret = pint_serv_getconfig(&req_job,&ack_job,&req_getconfig,\
				cred, &serv_addr);
		if (ret < 0)
		{
			ret = -EINVAL;
			goto getconfig_failure;
		}

		/* Use data from response to update global tables */
		server_config.fs_info[i].fh_root = ack_job->u.getconfig.root_handle;
		server_config.fs_info[i].fsid = ack_job->u.getconfig.fs_id;
		server_config.fs_info[i].meta_serv_count = ack_job->u.getconfig.\
			meta_server_count;
		server_config.fs_info[i].io_serv_count = ack_job->u.getconfig.\
			io_server_count;
		server_config.fs_info[i].maskbits = ack_job->u.getconfig.\
			maskbits;
		/* Copy the client mount point */
		mnt_len = strlen(mntent_list.ptab_p[i].local_mnt_dir);
		server_config.fs_info[i].local_mnt_dir = (PVFS_string)malloc(mnt_len
				+ 1);
		strncpy(server_config.fs_info[i].local_mnt_dir,mntent_list.ptab_p[i].\
				local_mnt_dir, mnt_len);
		server_config.fs_info[i].local_mnt_dir[mnt_len] = '\0';

		metalen = strlen(ack_job->u.getconfig.meta_server_mapping);
		iolen = strlen(ack_job->u.getconfig.io_server_mapping);
		/* How to get the size of metaserver list in ack? */
		server_config.fs_info[i].meta_serv_array = (PVFS_string *)\
			malloc(server_config.fs_info[i].meta_serv_count\
			* sizeof(PVFS_string));
		if (!server_config.fs_info[i].meta_serv_array)
		{
			ret = -ENOMEM;
			goto getconfig_failure;
		}
		server_config.fs_info[i].bucket_array = (bucket_info *)\
			malloc(server_config.fs_info[i].meta_serv_count\
			* sizeof(bucket_info));
		if (!server_config.fs_info[i].bucket_array)
		{
			ret = -ENOMEM;
			goto metabucketalloc_failure;
		}
		/* Copy the metaservers from ack to config info */
		for(j = 0;j < server_config.fs_info[i].meta_serv_count;j++) 
		{
			k = start;
			while (ack_job->u.getconfig.meta_server_mapping[k] != ' ' &&
					k < metalen)
				k++;
			server_config.fs_info[i].meta_serv_array[j]\
				= (PVFS_string)malloc(k - start + 1);
			if (!server_config.fs_info[i].meta_serv_array[j])
			{
				ret = -ENOMEM;
				goto metaserver_alloc_failure;
			}
			memcpy(server_config.fs_info[i].meta_serv_array[j],
					&(ack_job->u.getconfig.meta_server_mapping[start]),k-start);
			server_config.fs_info[i].meta_serv_array[j][k-start] = '\0';
			/* Get the bucket info */
			/* Get the start bucket */
			start = k + 1;
			while (ack_job->u.getconfig.meta_server_mapping[k] != '-' &&
					k < metalen)
				k++;
			memcpy(bktstr,&(ack_job->u.getconfig.meta_server_mapping[start]),\
					k - start);
			bktstr[k-start] = '\0';
			server_config.fs_info[i].bucket_array[j].bucket_st = atoi(bktstr);
			/* Get the end bucket */
			start = k + 1;
			/*while (ack_job->u.getconfig.meta_server_mapping[k] != ';' &&
					k < req_job->u.getconfig.max_strsize)*/
			while (ack_job->u.getconfig.meta_server_mapping[k] != ';' &&
					k < metalen)
				k++;
			memcpy(bktstr,&(ack_job->u.getconfig.meta_server_mapping[start]),\
					k - start);
			bktstr[k-start] = '\0';
			server_config.fs_info[i].bucket_array[j].bucket_end = atoi(bktstr);

			start = k + 1;
		}
		start = k = 0;

		/* How to get the size of ioserver list in ack? */
		server_config.fs_info[i].io_serv_array = (PVFS_string *)\
			malloc(server_config.fs_info[i].io_serv_count\
			* sizeof(PVFS_string));
		if (!server_config.fs_info[i].io_serv_array)
		{
			ret = -ENOMEM;
			goto metaserver_alloc_failure;
		}
		server_config.fs_info[i].io_bucket_array = (bucket_info *)\
			malloc(server_config.fs_info[i].io_serv_count\
			* sizeof(bucket_info));
		if (!server_config.fs_info[i].io_bucket_array)
		{
			ret = -ENOMEM;
			goto iobucketalloc_failure;
		}
		/* Copy the ioservers from ack to config info */
		for(j = 0;j < server_config.fs_info[i].io_serv_count;j++) 
		{
			k = start;
			while (ack_job->u.getconfig.io_server_mapping[k] != ' ' &&
					k < iolen)
				k++;
			server_config.fs_info[i].io_serv_array[j]\
				= (PVFS_string)malloc(k - start + 1);
			if (!server_config.fs_info[i].io_serv_array[j])
			{
				ret = -ENOMEM;
				goto ioserver_alloc_failure;
			}
			memcpy(server_config.fs_info[i].io_serv_array[j],
					&(ack_job->u.getconfig.io_server_mapping[start]),k-start);
			server_config.fs_info[i].io_serv_array[j][k-start] = '\0';
			/* Get the bucket info */
			/* Get the start bucket */
			start = k + 1;
			while (ack_job->u.getconfig.io_server_mapping[k] != '-' &&
					k < iolen)
				k++;
			memcpy(bktstr,&(ack_job->u.getconfig.io_server_mapping[start]),\
					k - start);
			bktstr[k-start] = '\0';
			server_config.fs_info[i].io_bucket_array[j].bucket_st = atoi(bktstr);
			/* Get the end bucket */
			start = k + 1;
			while (ack_job->u.getconfig.io_server_mapping[k] != ';' &&
					k < iolen)
				k++;
			memcpy(bktstr,&(ack_job->u.getconfig.io_server_mapping[start]),\
					k - start);
			bktstr[k-start] = '\0';
			server_config.fs_info[i].io_bucket_array[j].bucket_end = atoi(bktstr);

			start = k + 1;
		}
		start = k = 0;

		sysjob_free(serv_addr,req_job,req_size,BMI_SEND_BUFFER,NULL);
		sysjob_free(serv_addr,ack_job,ack_size,BMI_RECV_BUFFER,NULL);
		if (req_getconfig.fs_name)
			free(req_getconfig.fs_name);
	}
	return(0); 

ioserver_alloc_failure:
	for(i = 0;i < mntent_list.nr_entry;i++)
	{
		for(j = 0;j < server_config.fs_info[i].io_serv_count;j++)
		{
			if (server_config.fs_info[i].io_serv_array[j])
				free(server_config.fs_info[i].io_serv_array[j]);
		}

		if (server_config.fs_info[i].io_bucket_array)
			free(server_config.fs_info[i].io_bucket_array);
	}

iobucketalloc_failure:
	for(i = 0;i < mntent_list.nr_entry;i++)
	{
		if (server_config.fs_info[i].io_serv_array)
			free(server_config.fs_info[i].io_serv_array);
	}

metaserver_alloc_failure:
	for(i = 0;i < mntent_list.nr_entry;i++)
	{
		for(j = 0;j < server_config.fs_info[i].meta_serv_count;j++)
		{
			if (server_config.fs_info[i].meta_serv_array[j])
				free(server_config.fs_info[i].meta_serv_array[j]);
		}

		if (server_config.fs_info[i].bucket_array)
			free(server_config.fs_info[i].bucket_array);
	}

metabucketalloc_failure:
	for(i = 0;i < mntent_list.nr_entry;i++)
	{
		if (server_config.fs_info[i].meta_serv_array)
			free(server_config.fs_info[i].meta_serv_array);
	}

getconfig_failure:
	sysjob_free(serv_addr,ack_job,ack_size,BMI_RECV_BUFFER,NULL);
	sysjob_free(serv_addr,req_job,req_size,BMI_SEND_BUFFER,NULL);
	if (req_getconfig.fs_name)
		free(req_getconfig.fs_name);

namealloc_failure:
	/* Shutdown all structures that have been initialized */
	/*PVFS_sys_finalize();*/
	return(ret);

}	  

