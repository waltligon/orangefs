/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>

#include "pvfs2-sysint.h"
#include "pvfs2-req-proto.h"
#include "pint-sysint.h"
#include "pint-servreq.h"
#include "pint-sysint.h"
#include "pint-bucket.h"
#include "pcache.h"
#include "pinode-helper.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

#define REQ_ENC_FORMAT 0

#define PVFS_MAX_CONFIG_SIZE 1000

int g_session_tag;
gen_mutex_t *g_session_tag_mt_lock;
struct server_configuration_s g_server_config;

static int server_parse_config(
    struct server_configuration_s *config,
    PVFS_servresp_getconfig *response);

/*
 * PINT_do_lookup looks up one dirent in a given parent directory
 * create and remove (possibly others) have to check for a dirent's presence
 *
 * returns 0 on success (with pinode_ref filled in), -ERRNO on failure
 */

int PINT_do_lookup (char* name,PVFS_pinode_reference parent,
		PVFS_credentials cred,PVFS_pinode_reference *entry)
{
	struct PVFS_server_req_s req_p;             /* server request */
        struct PVFS_server_resp_s *ack_p = NULL;    /* server response */
        int ret = -1, name_sz = 0;
        struct PINT_decoded_msg decoded;
        bmi_addr_t serv_addr;
        pinode *pinode_ptr = NULL;
	void* encoded_resp;
	PVFS_msg_tag_t op_tag;
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
        req_p.u.lookup_path.attrmask = PVFS_ATTR_COMMON_ALL;

	/*expecting exactly one segment to come back (maybe attribs)*/
	max_msg_sz = PINT_get_encoded_generic_ack_sz(0, req_p.op) + (sizeof(PVFS_handle) + sizeof(PVFS_object_attr));

        ret = PINT_bucket_map_to_server(&serv_addr, parent.handle, parent.fs_id);
        if (ret < 0)
        {
            failure = MAP_SERVER_FAILURE;
            goto return_error;
        }

        /* Make a lookup_path server request to get the handle and
         * attributes
         */

	op_tag = get_next_session_tag();

	ret = PINT_send_req(serv_addr, &req_p, max_msg_sz,
	    &decoded, &encoded_resp, op_tag);
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
	/* filter to make sure we set a reasonable mask here */
	pinode_ptr->attr.mask &= PVFS_ATTR_COMMON_ALL;

        /* Add to the pinode list */
        ret = PINT_pcache_insert(pinode_ptr);
        if (ret < 0)
        {
            failure = ADD_PCACHE_FAILURE;
            goto return_error;
        }
        PINT_pcache_insert_rls(pinode_ptr);

	PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
	    &encoded_resp, op_tag);
        return (0);

return_error:

    switch(failure)
    {
        case ADD_PCACHE_FAILURE:
            PINT_pcache_pinode_dealloc(pinode_ptr);
        case INVAL_LOOKUP_FAILURE:
        case SEND_REQ_FAILURE:
	    PINT_release_req(serv_addr, &req_p, max_msg_sz, &decoded,
		&encoded_resp, op_tag);
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

/* PINT_server_get_config()
 *
 */
int PINT_server_get_config(struct server_configuration_s *config,
                           pvfs_mntlist mntent_list)
{
    int ret = -1, i = 0;
    bmi_addr_t serv_addr;
    struct PVFS_server_req_s serv_req;
    struct PVFS_server_resp_s *serv_resp = NULL;
    PVFS_credentials creds;
    struct PINT_decoded_msg decoded;
    void* encoded_resp;
    PVFS_size max_msg_sz;
    pvfs_mntent *mntent_p = NULL;
    PVFS_msg_tag_t op_tag = get_next_session_tag();

    /* TODO: Fill up the credentials information */

    /* TODO: IS THIS A REASONABLE MAXIMUM MESSAGE SIZE?  I HAVE NO IDEA */
    max_msg_sz = 
	PINT_get_encoded_generic_ack_sz(0, PVFS_SERV_GETCONFIG) 
	+ (2 * PVFS_MAX_CONFIG_SIZE);

    /*
      for each entry in the pvfstab, attempt to query the server for
      getconfig information.  discontinue loop when we have info.
    */
    for (i = 0; i < mntent_list.nr_entry; i++)
    {
	mntent_p = &mntent_list.ptab_p[i];

   	/* Obtain the metaserver to send the request */
	ret = BMI_addr_lookup(&serv_addr, mntent_p->meta_addr);
	if (ret < 0)
	{
            gossip_ldebug(CLIENT_DEBUG,"Failed to resolve BMI "
                          "address %s\n",mntent_p->meta_addr);
            return -1;
	}

	/* Set up the request for getconfig */
        memset(&serv_req,0,sizeof(struct PVFS_server_req_s));
	serv_req.op = PVFS_SERV_GETCONFIG;
	serv_req.rsize = sizeof(struct PVFS_server_req_s);
	serv_req.credentials = creds;
	serv_req.u.getconfig.max_strsize = PVFS_MAX_CONFIG_SIZE;

	gossip_ldebug(CLIENT_DEBUG,"asked for fs name = %s\n",
                      mntent_p->service_name);

	/* send the request and receive an acknowledgment */
	ret = PINT_send_req(serv_addr, &serv_req, max_msg_sz,
                            &decoded, &encoded_resp, op_tag);
	if (ret < 0)
        {
            gossip_ldebug(CLIENT_DEBUG,"PINT_send_req failed\n");
            return -1;
	}
	serv_resp = (struct PVFS_server_resp_s *) decoded.buffer;

        if (server_parse_config(config,&(serv_resp->u.getconfig)))
        {
            gossip_ldebug(CLIENT_DEBUG,"Failed to getconfig from host "
                          "%s\n",mntent_p->meta_addr);

            /* let go of any resources consumed by PINT_send_req() */
            PINT_release_req(serv_addr, &serv_req, max_msg_sz, &decoded,
                             &encoded_resp, op_tag);
            continue;
        }

	/* let go of any resources consumed by PINT_send_req() */
	PINT_release_req(serv_addr, &serv_req, max_msg_sz, &decoded,
                         &encoded_resp, op_tag);
        break;
    }

    /* verify that each pvfstab entry is valid according to the server */
    for (i = 0; i < mntent_list.nr_entry; i++)
    {
	mntent_p = &mntent_list.ptab_p[i];

        /* make sure we valid information about this fs */
        if (PINT_server_config_has_fs_config_info(
                config,mntent_p->service_name) == 0)
        {
            gossip_ldebug(CLIENT_DEBUG,"Error:  Cannot retrieve "
                          "information about pvfstab entry %s\n",
                          mntent_p->meta_addr);
            return -1;
        }
    }
    return(0); 
}

static int server_parse_config(struct server_configuration_s *config,
                               PVFS_servresp_getconfig *response)
{
    int ret = 1;
    int fs_fd = 0, server_fd = 0;
    char fs_template[] = ".__pvfs_fs_configXXXXXX";
    char server_template[] = ".__pvfs_server_configXXXXXX";

    if (config && response)
    {
        assert(response->fs_config_buf);
        assert(response->server_config_buf);

        fs_fd = mkstemp(fs_template);
        if (fs_fd == -1)
        {
            return ret;
        }

        server_fd = mkstemp(server_template);
        if (server_fd == -1)
        {
            close(fs_fd);
            return ret;
        }

        assert(!response->fs_config_buf[response->fs_config_buflen - 1]);
        assert(!response->server_config_buf[response->server_config_buflen - 1]);

        if (write(fs_fd,response->fs_config_buf,
                  (response->fs_config_buflen - 1)) ==
            (response->fs_config_buflen - 1))
        {
            if (write(server_fd,response->server_config_buf,
                      (response->server_config_buflen - 1)) ==
                (response->server_config_buflen - 1))
            {
                ret = PINT_server_config(config, fs_template, server_template);
            }
        }
        close(fs_fd);
        close(server_fd);

        remove(fs_template);
        remove(server_template);
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
