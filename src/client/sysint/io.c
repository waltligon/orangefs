/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* I/O Function Implementation */
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pvfs2-req-proto.h>
#include <pvfs-distribution.h>
#include <pint-servreq.h>
#include <pint-bucket.h>
#include <PINT-reqproto-encode.h>

#define REQ_ENC_FORMAT 0

/* PVFS_sys_io()
 *
 * performs a read or write operation.  PVFS_sys_read() and
 * PVFS_sys_write() are aliases to this function.
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_io(PVFS_sysreq_io *req, PVFS_sysresp_io *resp,
    enum PVFS_sys_io_type type)
{
    pinode* pinode_ptr = NULL;
    PVFS_bitfield attr_mask = 0;
    int ret = -1;
    bmi_addr_t* addr_array = NULL;
    struct PVFS_server_req_s* req_array = NULL;
    void** resp_encoded_array = NULL;
    struct PINT_decoded_msg* resp_decoded_array = NULL;
    int* error_code_array = NULL;
    int i;
    PVFS_Dist* io_dist = NULL;
    int partial_flag = 0;

    /* find a pinode for the target file */

    attr_mask = ATTR_BASIC;
    ret = phelper_get_pinode(req->pinode_refn, &pinode_ptr,
	attr_mask, req->credentials);
    if(ret < 0)
    {
	return(ret);
    }

    /* check permissions */
    ret = check_perms(pinode_ptr->attr, req->credentials.perms,
	req->credentials.uid, req->credentials.gid);
    if(ret < 0)
    {
	return(-EACCES);
    }

    gossip_lerr(
	"WARNING: kludging distribution in PINT_sys_io().\n");
    gossip_lerr(
	"WARNING: kludging datafile handles in PINT_sys_io().\n");
    if(!req->HACK_num_datafiles || !req->HACK_datafile_handles)
    {
	gossip_lerr("Error: didn't set hacked fields.\n");
	return(-EINVAL);
    }

    io_dist = PVFS_Dist_create("default_dist");
    if(!io_dist)
    {
	gossip_lerr("Error: PVFS_Dist_create() failure.\n");
	return(-EINVAL);
    }
    ret = PINT_Dist_lookup(io_dist);
    if(ret < 0)
    {
	goto out;
    }

    /* allocate storage for bookkeeping information */
    /* TODO: try to do something to avoid so many mallocs */
    addr_array = (bmi_addr_t*)malloc(req->HACK_num_datafiles *
	sizeof(bmi_addr_t));
    req_array = (struct PVFS_server_req_s*)
	malloc(req->HACK_num_datafiles * 
	sizeof(struct PVFS_server_req_s));
    resp_encoded_array = (void**)malloc(req->HACK_num_datafiles *
	sizeof(void*));
    resp_decoded_array = (struct PINT_decoded_msg*)
	malloc(req->HACK_num_datafiles * 
	sizeof(struct PINT_decoded_msg));
    error_code_array = (int*)malloc(req->HACK_num_datafiles *
	sizeof(int));
	
    if(!addr_array || !req_array || !resp_encoded_array ||
	!resp_decoded_array || !error_code_array)
    {
	ret = -ENOMEM;
	goto out;
    }

    /* setup the I/O request to each data server */
    for(i=0; i<req->HACK_num_datafiles; i++)
    {
	/* resolve the address of the server */
	ret = PINT_bucket_map_to_server(
	    &(addr_array[i]),
	    req->HACK_datafile_handles[i],
	    req->pinode_refn.fs_id);
	if(ret < 0)
	{
	    goto out;
	}

	/* fill in the I/O request */
	req_array[i].op = PVFS_SERV_IO;
	req_array[i].rsize = sizeof(struct PVFS_server_req_s);
	req_array[i].credentials = req->credentials;
	req_array[i].u.io.handle = req->HACK_datafile_handles[i];
	req_array[i].u.io.fs_id = req->pinode_refn.fs_id;
	req_array[i].u.io.iod_num = i;
	req_array[i].u.io.iod_count = req->HACK_num_datafiles;
	req_array[i].u.io.io_req = req->io_req;
	req_array[i].u.io.io_dist = io_dist;
	if(type == PVFS_SYS_IO_READ)
	    req_array[i].u.io.io_type = PVFS_IO_READ;
	if(type == PVFS_SYS_IO_WRITE)
	    req_array[i].u.io.io_type = PVFS_IO_WRITE;
    }
   
    /* send the I/O requests on their way */
    ret = PINT_send_req_array(
	addr_array,
	req_array,
	PINT_get_encoded_generic_ack_sz(0, PVFS_SERV_IO),
	resp_encoded_array,
	resp_decoded_array,
	error_code_array,
	req->HACK_num_datafiles);

    if(ret < 0 && ret != -EIO)
    {
	goto out;
    }

    /* If EIO, then at least one of the I/O req/resp exchanges
     * failed.  We will continue and carry I/O out with whoever we
     * can, though.
     */
    if(ret == -EIO)
    {
	partial_flag = 1;
	gossip_ldebug(CLIENT_DEBUG, "Warning: one or more I/O requests failed.\n");
    }

    /* run a flow with each I/O server that gave us a positive
     * response
     */

    /* TODO: finish this up */

    ret = -ENOSYS;

out:
    
    if(addr_array)
	free(addr_array);
    if(req_array)
	free(req_array);
    if(resp_encoded_array)
	free(resp_encoded_array);
    if(resp_decoded_array)
	free(resp_decoded_array);
    if(error_code_array)
	free(error_code_array);

    return(ret);

}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
