/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <malloc.h>
#include <assert.h>
#include <string.h>

#include "pcache.h"
#include "pinode-helper.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "pint-dcache.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "PINT-reqproto-encode.h"
#include "str-utils.h"

/* PVFS_sys_ref_lookup()
 *
 * steps in lookup
 * --------------
 * 1. First some terminology:
 * /parl/fshorte/some_dir/foobar.txt = path
 * /fshorte			 = segment
 *
 * 2. First we try to shorten the path by looking in the dcache for recently
 * used segments (IE: try to shorten "/parl/fshorte/some_dir/foobar.txt" to
 * "/some_dir/foobar.txt")
 *
 * 3. Once we get to a point where we can't resolve the segment in the dcache, 
 * we send lookup the parent handle of the last segment of the path we were 
 * able to lookup, or the just use whatever parent was passed in if nothing
 * is in the dcache. (IE: send "/fshorte/some_dir/foobar.txt" to the server who has
 * the dirent for the "/parl" segment)
 * 
 * 4. We're sending the entire path that we have available to the server. We
 * have no idea how many segments (if any) the server will be able to resolve,
 * so we look at the count variables in the response structure from the server
 * here there are 3 possibilities:
 *	a) the server can completely resolve the path; it returns N handles and
 *	    N PVFS_object_attr structures
 *	b) the server can resolve more than one segment in the path, but can't
 *	    go all the way, so it returns M handles and M-1 PVFS_object_attr
 *	    structures.  (IE: it knew about "/fshorte/some_dir", but the metadata
 *	    server holding the some_dir directory is different from the server that
 *	    holds the /fshorte segment [heh].  so we get back handles for both
 *	    "/fshorte" and "/some_dir" segments, but we only get a PVFS_object_attr
 *	    structure for "/fshorte".)
 *	c) the server figures out that the path doesn't resolve and returns an
 *	    error.
 *
 * 5. In the above cases (a, c), we know the result immediately, but if we
 * didn't get back the response for the entire path, we'll need to send more
 * requests to walk the entire path before we can return.
 *
 * returns 0 on success, -errno on failure
 * 
 * SIDE EFFECTS:
 * 1). All [p|d]cache entries that we resolve are added to the [p|d]cache
 *
 */
int PVFS_sys_ref_lookup(
    PVFS_fs_id fs_id,
    char* relative_pathname,
    PVFS_pinode_reference parent,
    PVFS_credentials credentials,
    PVFS_sysresp_lookup *resp)
{
    struct PVFS_server_req req_p;
    struct PVFS_server_resp *ack_p = NULL;
    int ret = -1, i = 0;
    int max_msg_sz, name_sz;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag;
    struct PINT_decoded_msg decoded;

    pinode *pinode_ptr = NULL;
    char *path = NULL;
    char segment[PVFS_SEGMENT_MAX] = {0};
    bmi_addr_t serv_addr;
    int total_segments = 0, num_segments_remaining = 0;
    PVFS_pinode_reference entry;

    enum {
	NONE_FAILURE = 0,
	GET_PARENT_FAILURE,
	MAP_TO_SERVER_FAILURE,
	SEND_REQ_FAILURE,
	RECV_REQ_FAILURE,
	DCACHE_INSERT_FAILURE,
	PCACHE_ALLOC_FAILURE,
	CHECK_PERMS_FAILURE,
	GET_NEXT_PATHSEG_FAILURE,
    } failure = NONE_FAILURE;

    /* make sure lookup was given sane arguments */
    if(relative_pathname == NULL)
    {
	ret = -EINVAL;
	failure = NONE_FAILURE;
	goto return_error;
    }

    /* check length of path and number of segments */
    if((strlen(relative_pathname) +1) > PVFS_REQ_LIMIT_PATH_NAME_BYTES ||
	(PINT_string_count_segments(relative_pathname) > 
	PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT))
    {
	ret = -ENAMETOOLONG;
	failure = NONE_FAILURE;
	goto return_error;
    }

    /*print args to make sure we're sane*/
    gossip_ldebug(CLIENT_DEBUG,"req->\n\tname: %s\n\tfs_id: %d\n\tcredentials:\n\t\tuid: %d\n\t\tgid: %d\n",relative_pathname,fs_id, credentials.uid, credentials.gid);

    /* Get  the total number of segments */
    total_segments = num_segments_remaining =
        PINT_string_count_segments(relative_pathname);

    /* make sure we're asking for something reasonable */
    if (num_segments_remaining < 1)
    {
	failure = GET_PARENT_FAILURE;
	ret = (-EINVAL);
	goto return_error;
    }

    /* do dcache lookups here to shorten the path as much as possible */

    name_sz = strlen(relative_pathname) + 1;
    path = (char *)malloc(name_sz);
    if (path == NULL)
    {
	failure = GET_PARENT_FAILURE;
        ret = -ENOMEM;
        goto return_error;
    }
    memcpy(path, relative_pathname, name_sz);

    /* traverse the path as much as we can via the dcache */

    /* send server messages here */
    while(num_segments_remaining > 0)
    {
	name_sz = strlen(path) + 1;
	req_p.op     = PVFS_SERV_LOOKUP_PATH;
	req_p.credentials = credentials;
	max_msg_sz = PINT_encode_calc_max_size(PINT_ENCODE_RESP, req_p.op,
	    PINT_CLIENT_ENC_TYPE);
	gossip_debug(CLIENT_DEBUG,
		      "  expecting ack of %d bytes (%d segment(s))\n",
		      max_msg_sz,
		      num_segments_remaining);

	/* update the pointer to the copy we already have */
	req_p.u.lookup_path.path = path;
	req_p.u.lookup_path.fs_id = parent.fs_id;
	req_p.u.lookup_path.starting_handle = parent.handle;
	req_p.u.lookup_path.attrmask = PVFS_ATTR_COMMON_ALL;

        /* Get Metaserver in BMI URL format using the bucket table 
         * interface */
	ret = PINT_bucket_map_to_server(&serv_addr, parent.handle,
						 parent.fs_id);
	if (ret < 0)
	{
	    failure = MAP_TO_SERVER_FAILURE;
	    goto return_error;
	}

	op_tag = get_next_session_tag();

	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
                            &decoded, &encoded_resp, op_tag);
	if (ret < 0)
	{
	    failure = SEND_REQ_FAILURE;
	    goto return_error;
	}
	ack_p = (struct PVFS_server_resp *) decoded.buffer;

	if (ack_p->status < 0 )
	{
	    ret = ack_p->status;
	    failure = RECV_REQ_FAILURE;
	    goto return_error;
	}

	if (ack_p->u.lookup_path.handle_count < 1)
	{
	    /*
	     * TODO: EINVAL is obviously the wrong errocode to return, are we
	     * going to set these errors to some other value that we setup in 
	     * another file or something?  pvfs_errno.h maybe?
	     */
	    failure = RECV_REQ_FAILURE;
	    ret = (-EINVAL);
	    gossip_ldebug(CLIENT_DEBUG,"server returned 0 entries\n");
	    goto return_error;
	}

	num_segments_remaining -= ack_p->u.lookup_path.handle_count;

	for(i = 0; i < ack_p->u.lookup_path.handle_count; i++)
	{
	    entry.handle = ack_p->u.lookup_path.handle_array[i];
	    entry.fs_id = fs_id;

            ret = PINT_get_path_element(path, i, segment, PVFS_SEGMENT_MAX);
	    if (ret < 0)
	    {
		failure = RECV_REQ_FAILURE;
		goto return_error;
	    }

	    /* Add entry to dcache */
	    ret = PINT_dcache_insert(segment, entry, parent);
	    if (ret < 0)
	    {
		failure = DCACHE_INSERT_FAILURE;
		goto return_error;
	    }
	    /* Add to pinode cache */
	    ret = PINT_pcache_pinode_alloc(&pinode_ptr); 	
	    if (ret < 0)
	    {
		failure = PCACHE_ALLOC_FAILURE;
		ret = -ENOMEM;
		goto return_error;
	    }
	    /* Fill the pinode */
	    pinode_ptr->pinode_ref.handle = entry.handle;
	    pinode_ptr->pinode_ref.fs_id = entry.fs_id;

	    /* TODO: this logic is kinda busted... -PHIL */
	    if (i >= ack_p->u.lookup_path.attr_count)
	    {
		/* the attributes on the last item may not be valid,  so if
		 * we're on the last path segment, and we didn't get an attr
		 * structure for set everything to zero.
		 */
		memset(&pinode_ptr->attr, 0, sizeof(PVFS_object_attr));
	    }
	    else
	    {
		pinode_ptr->attr = ack_p->u.lookup_path.attr_array[i];
		/* filter to make sure the server didn't return 
		 * attributes that we aren't handling here 
		 */
		pinode_ptr->attr.mask &= PVFS_ATTR_COMMON_ALL;
	    }

	    /* Check permissions for path */
	    ret = check_perms(pinode_ptr->attr, pinode_ptr->attr.perms,
                              credentials.uid, credentials.gid);
	    if (ret < 0)
	    {
		failure = CHECK_PERMS_FAILURE;
		goto return_error;
	    }

	    /* Fill in the timestamps */
	    ret = phelper_fill_timestamps(pinode_ptr);
	    if (ret < 0)
	    {
		failure = CHECK_PERMS_FAILURE;
		goto return_error;
	    }
					
	    /* Set the size timestamp - size was not fetched */
	    pinode_ptr->size_flag = SIZE_INVALID;

	    /* Add to the pinode list */
	    ret = PINT_pcache_insert(pinode_ptr);
	    if (ret < 0)
	    {
		failure = CHECK_PERMS_FAILURE;
		goto return_error;
	    }
	    PINT_pcache_insert_rls(pinode_ptr);
	}

	PINT_release_req(serv_addr, &req_p, max_msg_sz,
                         &decoded, &encoded_resp, op_tag);
	if (path)
        {
	    free(path);
            path = NULL;
        }

	if (num_segments_remaining == 0)
	{
	    break;
	}

	/*get rid of the old path*/

	/* get the next chunk of the path to send */
	ret = get_next_path(relative_pathname,&path,total_segments - num_segments_remaining);
	if (ret < 0)
	{
	    failure = GET_NEXT_PATHSEG_FAILURE;
	    goto return_error;
	}

	/* Update the parent handle with the handle of last segment
	 * that has been looked up
	 */
	parent.handle = entry.handle;
    }

    resp->pinode_refn.handle = entry.handle;
    resp->pinode_refn.fs_id = entry.fs_id;

    return(0);
    
return_error:

    switch(failure)
    {
	case CHECK_PERMS_FAILURE:
	    if (pinode_ptr != NULL)
		PINT_pcache_pinode_alloc(&pinode_ptr); 	
	case PCACHE_ALLOC_FAILURE:
	case DCACHE_INSERT_FAILURE:
	case RECV_REQ_FAILURE:
	    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
		&encoded_resp, op_tag);
	case GET_NEXT_PATHSEG_FAILURE:
            /* request has already been freed at this point */
	case SEND_REQ_FAILURE:
	case MAP_TO_SERVER_FAILURE:
	    if(path)
		free(path);
	case GET_PARENT_FAILURE:
	case NONE_FAILURE:
	    break;
    }
    return(ret);
}

/* PVFS_sys_lookup()
 *
 * See documentation for PVFS_sys_ref_lookup.
 *
 * SPECIAL CASES:
 * 1). If the user passes in "/" we return the root handle.
 *
 */
int PVFS_sys_lookup(
    PVFS_fs_id fs_id, char* name,
    PVFS_credentials credentials,
    PVFS_sysresp_lookup *resp)
{
    int ret = -EINVAL;
    PVFS_pinode_reference parent;

    if (name && resp)
    {
        parent.fs_id = fs_id;

        ret = PINT_bucket_get_root_handle(fs_id,&parent.handle);
        if (ret < 0)
        {
            return ret;
        }

        /* NOTE: special case is that we're doing a lookup of the root
         * handle (which we got during the getconfig) so we want to check
         * to see if we're looking up "/"; if so, then get the root handle
         * from the bucket table interface and return
         */
        if (!strcmp(name, "/"))
        {
            resp->pinode_refn.handle = parent.handle;
            resp->pinode_refn.fs_id = fs_id;
            ret = 0;
        }
        else
        {
            /*
              strip off leading slash (if any) and lookup the rest
              of the path.  NOTE: If it's always an error to not
              have a leading slash, we should check that above
              without doing this function call.
            */
            char *path = ((name[0] == '/') ? &name[1] : name);
            ret = PVFS_sys_ref_lookup(fs_id, path, parent,
                                      credentials, resp);
        }
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
