/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Get attribute Function Implementation */
#include <malloc.h>
#include <assert.h>
#include <string.h>

#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pvfs2-req-proto.h"
#include "pvfs-distribution.h"
#include "pint-servreq.h"
#include "config-manage.h"
#include "PINT-reqproto-encode.h"

#define REQ_ENC_FORMAT 0

/* PVFS_sys_getattr()
 *
 * obtain the attributes of a PVFS file
 * 
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_getattr(PVFS_sysreq_getattr *req, PVFS_sysresp_getattr *resp)
{
   /* Initialization */   
	struct PVFS_server_req_s *req_p = NULL;	 /* server request */
	struct PVFS_server_resp_s *ack_p = NULL; /* server response */
   int ret = -1,i = 0;
   bmi_addr_t serv_addr;	            /* PVFS address type structure */ 
	char *server = NULL;
	int vflags = 0;
	//PVFS_count32 count = 0;
	struct timeval cur_time;
	PVFS_size *size_array = 0, logical_size = 0;
	pinode *entry_pinode = NULL;
	PVFS_bitfield attr_mask = req->attrmask;
	pinode_reference entry;
	struct PINT_decoded_msg decoded;
	int max_msg_sz = 0, num_data_servers = 0;
	//PVFS_servreq_getattr req_args;

	/* Let's check if size is to be fetched here, If so
	 * somehow ensure that dist is returned as part of 
	 * attributes - is this the way we want this to be done?
	 */
	if (req->attrmask & ATTR_SIZE)
		attr_mask |= ATTR_META;

	/* Fill in pinode reference */ 
	entry.handle = req->pinode_refn.handle;
	entry.fs_id = req->pinode_refn.fs_id;

	/* can't use phelper_get_pinode here because pinode helper calls 
	 * getattr();
	 */

	/* do we have a valid copy? 
	 * if any of the attributes are stale, or absent then we need to 
	 * retrive a fresh copy.
	 */
	ret = PINT_pcache_lookup(entry, entry_pinode);
	if (ret  == PCACHE_LOOKUP_SUCCESS)
        {
		resp->attr = entry_pinode->attr;
		return (0);
        }

	ret = config_bt_map_bucket_to_server(&server,entry.handle,entry.fs_id);
        if (ret < 0)
        {
		goto map_server_failure;
        }

        ret = BMI_addr_lookup(&serv_addr,server);
        if (ret < 0)
        {
		goto map_server_failure;
        }

	req_p = (struct PVFS_server_req_s *) malloc(sizeof(struct PVFS_server_req_s));
        if (req_p == NULL) {
                assert(0);
        }

	req_p->op = PVFS_SERV_GETATTR;
        req_p->credentials = req->credentials;
	req_p->rsize = sizeof(struct PVFS_server_req_s);
	req_p->u.getattr.handle = entry.handle;
	req_p->u.getattr.fs_id = entry.fs_id;
	req_p->u.getattr.attrmask = req->attrmask;

	/* Q: how much stuff are we getting back? */
	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* Make a server getattr request */
	ret = PINT_server_send_req(serv_addr, req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
            goto send_req_failure;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->status < 0 )
        {
            goto send_req_failure;
            ret = ack_p->status;
        }

	resp->attr = ack_p->u.getattr.attr;
	/* TODO: uncomment when extended attributes are defined */
	/* resp->eattr = ack_p.u.getattr.eattr; */

        /* the request isn't needed anymore, free it */
        free(req_p);

	/* do size calculations here? */

	if ((req->attrmask & ATTR_SIZE) == ATTR_SIZE)
	{
		/*only do this if you want the size*/
		num_data_servers = entry_pinode->attr.u.meta.nr_datafiles;
	}

#if 0
	ret = phelper_get_pinode(entry,&entry_pinode,attr_mask,
			vflags,req->credentials);
	if (ret < 0)
	{
		goto pinode_get_failure;
	}

	/* If size is to be fetched, individual requests to each I/O 
	 * server having a datafile handle need to be sent
	 */

	if (req->attrmask & ATTR_SIZE)
	{
		/* How many I/O servers? */
		count = entry_pinode->attr.u.meta.nr_datafiles;

		/* Allocate the datafile size array */
		size_array = (PVFS_size *)malloc(count * sizeof(PVFS_size));
		if (!size_array)
		{
			ret = -ENOMEM;
			goto pinode_get_failure;
		}

		/* For each I/O server */
		for(i = 0;i < count;i++)
		{
			/* Set up a server getattr request */

			/* Get server using the datafile handle thru the BTI */
			ret = config_bt_map_bucket_to_server(&server,\
				entry_pinode->attr.u.meta.dfh[i], req->pinode_refn.fs_id);
			if (ret < 0)
			{
				goto map_server_failure;
			}
			ret = BMI_addr_lookup(&serv_addr,server);
			if (ret < 0)
			{
				goto map_server_failure;
			}
			/* Fill in the parameters */
			req_args.handle = entry_pinode->attr.u.meta.dfh[i];
			req_args.fs_id = req->pinode_refn.fs_id;
			/* Obtain the datafile attributes as it contains the size */
			req_args.attrmask = req->attrmask | ATTR_DATA | ATTR_SIZE;

			/* Make a server getattr request */
			ret = PINT_server_send_req(serv_addr, req_p, max_msg_sz, &decoded);
			if (ret < 0)
			{
				goto map_server_failure;
			}

			ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

			/* store the file size reported by the I/O server */
			size_array[i] = ack_p->u.getattr.attr.u.data.size;

			/* Free the jobs */
			PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
			free(req_p);
		}

		/* Now, calculate the logical size using the relevant distribution
	 	* function
	 	*/
		/* TODO: Figure this thing out!!! */
#if 0
		dist_p = &(entry_pinode->attr.u.meta.dist);
#endif

		/* TODO: Check with Walt if this is the prototype of the logical
	 	* size calculation function. It may differ slightly. 
	 	*/
		ret = dist_p->logical_size(dparm,size_array,count,&logical_size);

		if (ret < 0)
		{
			goto map_server_failure;
		}

		/* Need to update the pinode with size and cache it */
		/* TODO: Need to figure out an optimum way of doing it */
		entry_pinode->size = logical_size;
		/* Get current time */
		ret = gettimeofday(&cur_time,NULL);
		if (ret < 0)
		{
			goto map_server_failure;
		}
		/* Set the size timestamp */
		entry_pinode->tstamp_size.tv_sec = cur_time.tv_sec + size_to.tv_sec;
		entry_pinode->tstamp_size.tv_usec = cur_time.tv_usec + size_to.tv_usec;
	}/* if size is required */

#endif

	ret = gettimeofday(&cur_time,NULL);
	if (ret < 0)
	{
		goto map_server_failure;
	}
	/* Set the size timestamp */
	phelper_fill_timestamps(entry_pinode);

	/* Add to cache  */
	ret = PINT_pcache_insert(entry_pinode);
	if (ret < 0)
	{
		goto map_server_failure;
	}

	/* Free memory allocated for name */
	if (size_array)
		free(size_array);

	/* Free the jobs */	
	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	return(0);
	
send_req_failure:
printf("send_req_failure\n");

map_server_failure:
printf("map_server_failure\n");
	if (ack_p)
		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	if (req_p)
		free(req_p);

	if (server)
		free(server);
	/* Free memory allocated for name */
	if (size_array)
		free(size_array);

	return(ret);
}
