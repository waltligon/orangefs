/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "pcache.h"
#include "pint-bucket.h"
#include "pint-dcache.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "gen-locks.h"
#include "pint-servreq.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"

#define BKT_STR_SIZE 7

#define REQ_ENC_FORMAT 0

job_context_id PVFS_sys_job_context = -1;

/* pinode cache */

extern struct server_configuration_s g_server_config;
extern fsconfig_array server_config;

extern gen_mutex_t *g_session_tag_mt_lock;

static int server_get_config(
    struct server_configuration_s *config,
    pvfs_mntlist mntent_list);

static int server_parse_config(
    struct server_configuration_s *config,
    PVFS_servresp_getconfig *response);

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal data
 * structures.  Must be called before any other system interface function.
 *
 * returns 0 on success, -errno on failure
 */

int PVFS_sys_initialize(pvfs_mntlist mntent_list, PVFS_sysresp_init *resp)
{
    int ret = -1, i = 0;
    int num_file_systems = 0;
    gen_mutex_t mt_config;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    enum {
	NONE_INIT_FAIL = 0,
	BMI_INIT_FAIL,
	FLOW_INIT_FAIL,
	JOB_INIT_FAIL,
	JOB_CONTEXT_FAIL,
	PCACHE_INIT_FAIL,
	DCACHE_INIT_FAIL,
	BUCKET_INIT_FAIL,
	GET_CONFIG_INIT_FAIL
    } init_fail = NONE_INIT_FAIL; /* used for cleanup in the event of failures */

    /* Initialize BMI */
    /*TODO: change this so it parses the bmi module from the pvfstab file*/
    ret = BMI_initialize("bmi_tcp",NULL,0);
    if (ret < 0)
    {
	init_fail = BMI_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"BMI initialize failure\n");
	goto return_error;
    }

    /* initialize bmi session identifier, TODO: DOCUMENT THIS */
    g_session_tag_mt_lock = gen_mutex_build();

    /* Initialize flow */
    ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
    if (ret < 0)
    {
	init_fail = FLOW_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Flow initialize failure.\n");
	goto return_error;
    }

    /* Initialize the job interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
	init_fail = JOB_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error initializing job interface: %s\n",strerror(-ret));
	goto return_error;
    }

    ret = job_open_context(&PVFS_sys_job_context);
    if(ret < 0)
    {
	init_fail = JOB_CONTEXT_FAIL;
	gossip_ldebug(CLIENT_DEBUG, "job_open_context() failure.\n");
	goto return_error;
    }
	
    /* Initialize the pinode cache */
    ret = PINT_pcache_initialize();
    if (ret < 0)
    {
	init_fail = PCACHE_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error initializing pinode cache\n");
	goto return_error;	
    }

    /* Initialize the directory cache */
    ret = PINT_dcache_initialize();
    if (ret < 0)
    {
	init_fail = DCACHE_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error initializing directory cache\n");
	goto return_error;	
    }	

    server_config.nr_fs = mntent_list.nr_entry;
    server_config.fs_info = (fsconfig *)malloc(mntent_list.nr_entry * sizeof(fsconfig));
    if (server_config.fs_info == NULL)
    {
	assert(0);
	gossip_ldebug(CLIENT_DEBUG,"Error in allocating configuration parameters\n");
	ret = -ENOMEM;
	goto return_error;
    }
    memset(server_config.fs_info, 0, mntent_list.nr_entry * sizeof(fsconfig));

    /* Get configuration parameters from server */
    ret = server_get_config(&g_server_config,mntent_list);
    if (ret < 0)
    {
	init_fail = GET_CONFIG_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error in getting server config parameters\n");
	/* Release the mutex */
	gen_mutex_unlock(&mt_config);
	goto return_error;
    }
    num_file_systems = llist_count(g_server_config.file_systems);
    assert(num_file_systems);

    /* Grab the mutex - serialize all writes to server_config */
    gen_mutex_lock(&mt_config);	

    /* Initialize the configuration management interface */
    ret = PINT_bucket_initialize();
    if (ret < 0)
    {
	init_fail = BUCKET_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error in initializing configuration management interface\n");
	/* Release the mutex */
	gen_mutex_unlock(&mt_config);
	goto return_error;
    }

    /* we need to return the fs_id's to the calling function */
    resp->nr_fsid = num_file_systems;
    resp->fsid_list = malloc(num_file_systems * sizeof(PVFS_handle));
    if (resp->fsid_list == NULL)
    {
	init_fail = GET_CONFIG_INIT_FAIL;
	ret = -ENOMEM;
	goto return_error;
    }

    /*
      iterate over each fs for two reasons:
      1) load mappings into bucket interface
      2) store fs ids into resp object
    */
    cur = g_server_config.file_systems;
    while(cur && (i < num_file_systems))
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        assert(cur_fs->coll_id);
        if (PINT_handle_load_mapping(&g_server_config,cur_fs))
        {
            init_fail = GET_CONFIG_INIT_FAIL;
            gossip_ldebug(CLIENT_DEBUG,"Failed to load fs info into the "
                          "PINT_handle interface.\n");
            gen_mutex_unlock(&mt_config);
            goto return_error;
        }
        resp->fsid_list[i++] = cur_fs->coll_id;
        cur = llist_next(cur);
    }

    /* Release the mutex */
    gen_mutex_unlock(&mt_config);
	
    /* load the server info from config file to table */
    return(0);

 return_error:
    free(server_config.fs_info);

    switch(init_fail) {
	case GET_CONFIG_INIT_FAIL:
	    PINT_bucket_finalize();
	case BUCKET_INIT_FAIL:
	    PINT_dcache_finalize();
	case DCACHE_INIT_FAIL:
	    PINT_pcache_finalize();
	case PCACHE_INIT_FAIL:
	case JOB_CONTEXT_FAIL:
	    job_finalize();
	case JOB_INIT_FAIL:
	    PINT_flow_finalize();
	case FLOW_INIT_FAIL:
	    BMI_finalize();
	case BMI_INIT_FAIL:
	case NONE_INIT_FAIL:
	    /* nothing to do for either of these */
	    break;
    }
    return(ret);
}

/* server_get_config()
 *
 */
static int server_get_config(struct server_configuration_s *config,
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
	+ (2 * MAX_STRING_SIZE);

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
            return 1;
	}

	/* Set up the request for getconfig */
        memset(&serv_req,0,sizeof(struct PVFS_server_req_s));
	serv_req.op = PVFS_SERV_GETCONFIG;
	serv_req.rsize = sizeof(struct PVFS_server_req_s);
	serv_req.credentials = creds;
	serv_req.u.getconfig.max_strsize = MAX_STRING_SIZE;

	gossip_ldebug(CLIENT_DEBUG,"asked for fs name = %s\n",
                      mntent_p->service_name);

	/* send the request and receive an acknowledgment */
	ret = PINT_send_req(serv_addr, &serv_req, max_msg_sz,
                            &decoded, &encoded_resp, op_tag);
	if (ret < 0)
        {
            gossip_ldebug(CLIENT_DEBUG,"PINT_send_req failed\n");
            return 1;
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
            return 1;
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
    char *args[3] = { NULL, fs_template, server_template };

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
                ret = PINT_server_config(config,3,args);
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
