/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-sysint.h"
#include "pvfs2-req-proto.h"
#include "pint-sysint.h"
#include "pint-servreq.h"
#include "pint-sysint.h"
#include "pint-bucket.h"
#include "pcache.h"
#include "pinode-helper.h"
#include "PINT-reqproto-encode.h"

#define REQ_ENC_FORMAT 0

int g_session_tag;
gen_mutex_t *g_session_tag_mt_lock;

/*
 * PINT_do_lookup looks up one dirent in a given parent directory
 * create and remove (possibly others) have to check for a dirent's presence
 *
 * returns 0 on success (with pinode_ref filled in), -ERRNO on failure
 */

int PINT_do_lookup (PVFS_string name,pinode_reference parent,
		PVFS_bitfield mask,PVFS_credentials cred,pinode_reference *entry)
{
	struct PVFS_server_req_s req_p;             /* server request */
        struct PVFS_server_resp_s *ack_p = NULL;    /* server response */
        int ret = -1, name_sz = 0;
        struct PINT_decoded_msg decoded;
        bmi_addr_t serv_addr;
        pinode *pinode_ptr = NULL;
        bmi_size_t max_msg_sz = 0;

        /*Q: should I combine these into one since there's not much
         * cleanup going on for each case?
         */

        enum {
            NONE_FAILURE = 0,
            MAP_SERVER_FAILURE,
            SEND_REQ_FAILURE,
            INVAL_LOOKUP_FAILURE,
            ADD_PCACHE_FAILURE,
        } failure = NONE_FAILURE;

	if (name == NULL)  /* how do we look up a null name? */
	    return -ENOENT;

        name_sz = strlen(name) + 1; /*include the null terminator*/

        req_p.op = PVFS_SERV_LOOKUP_PATH;
        req_p.credentials = cred;
        req_p.rsize = name_sz + sizeof(struct PVFS_server_req_s);
        req_p.u.lookup_path.path = name;
        req_p.u.lookup_path.fs_id = parent.fs_id;
        req_p.u.lookup_path.starting_handle = parent.handle;
        req_p.u.lookup_path.attrmask = mask;

	/*expecting exactly one segment to come back (maybe attribs)*/
	max_msg_sz = sizeof(struct PVFS_server_resp_s) + (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));

        ret = PINT_bucket_map_to_server(&serv_addr, parent.handle, parent.fs_id);
        if (ret < 0)
        {
            failure = MAP_SERVER_FAILURE;
            goto return_error;
        }

        /* Make a lookup_path server request to get the handle and
         * attributes
         */

        ret = PINT_server_send_req(serv_addr, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
        {
            failure = SEND_REQ_FAILURE;
            goto return_error;
        }

        ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	/* make sure the operation didn't fail*/
	if (ack_p->status < 0 )
	{
		ret = ack_p->status;
		failure = SEND_REQ_FAILURE;
		goto return_error;
	}

        /* we should never get multiple handles back for the meta file*/
        if (ack_p->u.lookup_path.count != 1)
        {
	    ret = -EINVAL;
            failure = INVAL_LOOKUP_FAILURE;
            goto return_error;
        }

        entry->handle = ack_p->u.lookup_path.handle_array[0];
        entry->fs_id = parent.fs_id;

        /*in the event of a successful lookup, we need to add this to the pcache too*/

        ret = PINT_pcache_pinode_alloc(&pinode_ptr);
        if (ret < 0)
        {
            ret = -ENOMEM;
            failure = INVAL_LOOKUP_FAILURE;
            goto return_error;
        }

        /* Fill in the timestamps */
        ret = phelper_fill_timestamps(pinode_ptr);
        if (ret < 0)
        {
            failure = ADD_PCACHE_FAILURE;
            goto return_error;
        }

        /* Set the size timestamp - size was not fetched */
        pinode_ptr->size_flag = SIZE_INVALID;
        pinode_ptr->pinode_ref.handle = ack_p->u.lookup_path.handle_array[0];
        pinode_ptr->pinode_ref.fs_id = parent.fs_id;
        pinode_ptr->attr = ack_p->u.lookup_path.attr_array[0];
	pinode_ptr->mask = req_p.u.lookup_path.attrmask;

        /* Add to the pinode list */
        ret = PINT_pcache_insert(pinode_ptr);
        if (ret < 0)
        {
            failure = ADD_PCACHE_FAILURE;
            goto return_error;
        }

        PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
        return (0);

return_error:

    switch(failure)
    {
        case ADD_PCACHE_FAILURE:
            PINT_pcache_pinode_dealloc(pinode_ptr);
        case INVAL_LOOKUP_FAILURE:
        case SEND_REQ_FAILURE:
            PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);
        case MAP_SERVER_FAILURE:
        case NONE_FAILURE:
	    break;
    }
    return (ret);
}

/*
 * type: if 0 for requests, 1 for reponses.
 */
void debug_print_type(void* thing, int type)
{
	if (type ==0)
	{
		struct PVFS_server_req_s * req = thing;
		switch( req->op )
		{
			case PVFS_SERV_CREATE:
				gossip_ldebug(CLIENT_DEBUG,"create request\n");
				break;
			case PVFS_SERV_CREATEDIRENT:
				gossip_ldebug(CLIENT_DEBUG,"create dirent request\n");
				break;
			case PVFS_SERV_REMOVE:
				gossip_ldebug(CLIENT_DEBUG,"remove request\n");
				break;
			case PVFS_SERV_LOOKUP_PATH:
				gossip_ldebug(CLIENT_DEBUG,"lookup path request\n");
				break;
			case PVFS_SERV_SETATTR:
				gossip_ldebug(CLIENT_DEBUG,"setattr request\n");
				break;
			case PVFS_SERV_GETCONFIG:
				gossip_ldebug(CLIENT_DEBUG,"getconfig request\n");
				break;
			case PVFS_SERV_GETATTR:
				gossip_ldebug(CLIENT_DEBUG,"getattr request\n");
				break;
			case PVFS_SERV_READDIR:
				gossip_ldebug(CLIENT_DEBUG,"readdir request\n");
				break;
			case PVFS_SERV_MKDIR:
				gossip_ldebug(CLIENT_DEBUG,"mkdir request\n");
				break;
			case PVFS_SERV_RMDIR:
				gossip_ldebug(CLIENT_DEBUG,"rmdir request\n");
				break;
			case PVFS_SERV_RMDIRENT:
				gossip_ldebug(CLIENT_DEBUG,"rmdirent request\n");
				break;
			default:
				gossip_ldebug(CLIENT_DEBUG,"unknown request = %d\n", req->op);
				break;
		}
	}
	else
	{
		struct PVFS_server_resp_s * resp = thing;
		switch( resp->op )
		{
			case PVFS_SERV_RMDIRENT:
				gossip_ldebug(CLIENT_DEBUG,"rmdirent response\n");
				break;
			case PVFS_SERV_MKDIR:
				gossip_ldebug(CLIENT_DEBUG,"mkdir response\n");
				break;
			case PVFS_SERV_RMDIR:
				gossip_ldebug(CLIENT_DEBUG,"rmdir response\n");
				break;
			case PVFS_SERV_READDIR:
				gossip_ldebug(CLIENT_DEBUG,"readdir response\n");
				break;
			case PVFS_SERV_CREATE:
				gossip_ldebug(CLIENT_DEBUG,"create response\n");
				break;
			case PVFS_SERV_CREATEDIRENT:
				gossip_ldebug(CLIENT_DEBUG,"create dirent response\n");
				break;
			case PVFS_SERV_REMOVE:
				gossip_ldebug(CLIENT_DEBUG,"remove response\n");
				break;
			case PVFS_SERV_LOOKUP_PATH:
				gossip_ldebug(CLIENT_DEBUG,"lookup path response\n");
				break;
			case PVFS_SERV_SETATTR:
				gossip_ldebug(CLIENT_DEBUG,"setattr request\n");
				break;
			case PVFS_SERV_GETCONFIG:
				gossip_ldebug(CLIENT_DEBUG,"getconfig reply\n");
				break;
			case PVFS_SERV_GETATTR:
				gossip_ldebug(CLIENT_DEBUG,"getattr reply\n");
				break;
			default:
				gossip_ldebug(CLIENT_DEBUG,"unknown reply = %d\n", resp->op);
				break;
		}
	}
}

int get_next_session_tag(void)
{
    int ret = 0;
    /* grab a lock for this variable */
    gen_mutex_lock(g_session_tag_mt_lock);

    ret = g_session_tag;

    /* increment the tag, don't use zero */
    if (g_session_tag + 1 == 0)
    {
	g_session_tag = 1;
    }
    else
    {
	g_session_tag++;
    }

    /* release the lock */
    gen_mutex_unlock(g_session_tag_mt_lock);
    return ret;
}


/* get_no_of_segments
 *
 * calculates the number of segments in the path
 *
 * returns nothing
 */
void get_no_of_segments(char *path,int *num)
{
    char *s = path;
    int len = strlen(path);
    int pos = 0;
    *num = 0;

    for(pos = 0;pos < len;pos++) 
    {
	if (s[pos] == '/' && (pos + 1) < len) 
	    (*num)++;
    }
}

/* get_path_element
 *
 * gets the specified segment in the provided path.
 * IE:	path = "/home/fshorte/foo.txt"
 *	element = 0
 *  *segment would be pointed to a new buffer containing "home"
 *
 * this function is intended to aid path parsing for use with the dcache
 *
 * returns 0 on success, -errno on error
 */
int get_path_element(char *path, char** segment, int element)
{
    int pathlen = 0;
    int i = 0, num_slashes_seen = 0;
    int delimiter1 = 0 , delimiter2 = 0;
    /*make sure we asked for something sane*/
    if ((element < 0) || (path == NULL))
	return (-EINVAL);

    pathlen = strlen(path) + 1;

    /* find the start of the element that we're looking for */
    for(i =0; i < pathlen; i++)
    {
	if (path[i] == '/')
	{
	    num_slashes_seen++;
	    if (num_slashes_seen > element)
	    {
		break;
	    }
	}
    }
    delimiter1 = i + 1;

    for(i = delimiter1; i < pathlen; i++)
    {
	if (path[i] == '/')
	    break;
    }

    delimiter2 = i;

    if (delimiter2 - delimiter1 < 1)
    {
	return (-EINVAL);
    }

    *segment = malloc(delimiter2 - delimiter1 + 1);
    if (*segment == NULL)
    {
	return (-ENOMEM);
    }
    memcpy(*segment, &path[delimiter1], delimiter2 - delimiter1 );
    (*segment)[delimiter2 - delimiter1] = '\0';
    return (0);
}

/* get_next_path
 *
 * gets remaining path given number of path segments to skip
 *
 * returns 0 on success, -errno on failure
 */
int get_next_path(char *path, char **newpath, int skip)
{
    int pathlen=0, i=0, num_slashes_seen=0;
    int delimiter1=0;

    pathlen = strlen(path) + 1;

    /* find our starting point in the old path, it could be past multiple 
     * segments*/
    for(i =0; i < pathlen; i++)
    {
	if (path[i] == '/')
	{
	    num_slashes_seen++;
	    if (num_slashes_seen > skip)
	    {
		break;
	    }
	}
    }

    delimiter1 = i;
    if (pathlen - delimiter1 < 1)
    {
        return (-EINVAL);
    }

    *newpath = malloc(pathlen - delimiter1);
    if (*newpath == NULL)
    {
        return (-ENOMEM);
    }
    memcpy(*newpath, &path[delimiter1], pathlen - delimiter1 );
    /* *newpath[pathlen - delimiter1 -1 ] = '\0';*/
    return(0);

#if 0
    char *ptr = fname;
    int pos = *start; /* Initialize the position to current */
    int len = strlen(fname);
    int cnt = 0; /* */

	/* NULL value passed */
	if (!fname)
	{
		*start = *end = 0;
		return(-1);
	}

	/* Skip through to the segment you want */
	while(pos < len)
	{
		if (ptr[pos] == '/')
			cnt++;
		if(cnt == (num + 1))
			break;
			
		/* Start of remaining path */
		if (cnt == num)
		{
			/* Record start position */
			*start = pos + 1;
		}
		pos++;
	}
	/* Record end position */
	*end = pos - 1;

	return(0);
#endif
}

#if 0
/* get_next_segment
 *
 * gets next path segment given full pathname
 *
 * returns 0 on success, -errno on failure
 */
int get_next_segment(char *input, char **output, int *start)
{
    char *p1 = NULL,*p2 = NULL;
    char *s = (char *)&input[*start];
    int size = 0;

    /* Search for start of segment */
    p1 = index(s,'/');
    if (p1 != NULL)
    {
	/* Search for end of segment */
	p2 = index(p1 + 1,'/');
	if (p2 != NULL)
	    size = p2 - p1;
	else
	    size = &(input[strlen(input)]) - p1;
    }
    else
    {
	/* no '/' characters, either we only have '/' or have something in the
	 * root directory
	 */
	size = strlen(s) + 1;
	p1 = input;
    }

    if (size - 1)
    {
	/* Set up the segment to be returned */
	*output = (char *)malloc(size);
	strncpy(*output, p1 + 1, size - 1);
	(*output)[size - 1] = '\0';
	*start += size;
    }
    else
    {
	*output = NULL;
	*start = -1;
    }
    return(0);
}
#endif

/* check_perms
 *
 * check permissions of a PVFS object against the access mode
 *
 * returns 0 on success, -1 on error
 */
int check_perms(PVFS_object_attr attr,PVFS_permissions mode,int uid,int gid)
{
	int ret = 0;

	if ((attr.perms & mode) == mode)
		ret = 0;
	else if (attr.group == gid && ((attr.perms & mode) == mode))
		ret = 0;
	else if (attr.owner == uid)
		ret = 0;
	else
		ret = -1;

	return(ret);
}

/* sysjob_free
 *  
 * frees job associated data structures
 *
 * returns 0 on success, -errno on failure
 * 
 */
int sysjob_free(bmi_addr_t server,void *tmp_job,bmi_size_t size, const int op,
	int (*func)(void *,int))
{
	/* Call the respective free function */
	if (func)
		(*func)(tmp_job,0);
	if (tmp_job)
		BMI_memfree(server,tmp_job,size,op);

	return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
