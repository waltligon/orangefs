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
	int cflags = 0,vflags = 0;
	PVFS_count32 count = 0;
	struct timeval cur_time;
	PVFS_size *size_array = 0, logical_size = 0;
	pinode *entry_pinode = NULL;
	PVFS_bitfield attr_mask = req->attrmask;
	pinode_reference entry;
	struct PINT_decoded_msg decoded;
	int max_msg_sz = 0;
	PVFS_servreq_getattr req_args;

	/* Let's check if size is to be fetched here, If so
	 * somehow ensure that dist is returned as part of 
	 * attributes - is this the way we want this to be done?
	 */
	if (req->attrmask & ATTR_SIZE)
		attr_mask |= ATTR_META;

	/* Fill in pinode reference */ 
	entry.handle = req->pinode_refn.handle;
	entry.fs_id = req->pinode_refn.fs_id;

	/* Check for presence of pinode */ 
	/*cflags = HANDLE_TSTAMP + ATTR_TSTAMP;*/
	cflags = HANDLE_VALIDATE + ATTR_VALIDATE;
	if (cflags & ATTR_SIZE)
		cflags += SIZE_VALIDATE;
	ret = phelper_get_pinode(entry,&entry_pinode,attr_mask,
			vflags,cflags,req->credentials);
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
#if 0
		ret = dist_p->logical_size(dparm,size_array,count,&logical_size);

		if (ret < 0)
		{
			goto map_server_failure;
		}
#endif

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

	/* Add to cache  */
	ret = PINT_pcache_insert(entry_pinode);
	if (ret < 0)
	{
		goto map_server_failure;
	}

	/* After fetching all values, pass on only the values of interest */
	/* Fill in the response structure */
	resp->attr = entry_pinode->attr; 
	
	/* Free memory allocated for name */
	if (size_array)
		free(size_array);

	/* Free the jobs */	
	if (ack_p)
		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	if (req_p)
		free(req_p);

	return(0);
	
map_server_failure:
	if (ack_p)
		PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
	if (req_p)
		free(req_p);

	if (server)
		free(server);
	/* Free memory allocated for name */
	if (size_array)
		free(size_array);

pinode_get_failure:

	return(ret);
}

