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

/* pinode cache */

extern struct server_configuration_s g_server_config;
extern fsconfig_array server_config;

extern gen_mutex_t *g_session_tag_mt_lock;

static int server_get_config(pvfs_mntlist mntent_list);
static int server_parse_config(PVFS_servresp_getconfig *response);

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal data
 * structures.  Must be called before any other system interface function.
 *
 * returns 0 on success, -errno on failure
 */

int PVFS_sys_initialize(pvfs_mntlist mntent_list, PVFS_sysresp_init *resp)
{
    int ret = -1, i;
    gen_mutex_t mt_config;

    enum {
	NONE_INIT_FAIL = 0,
	BMI_INIT_FAIL,
	FLOW_INIT_FAIL,
	JOB_INIT_FAIL,
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

    /* Get configuration parameters from server */
    ret = server_get_config(mntent_list);
    if (ret < 0)
    {
	init_fail = GET_CONFIG_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error in getting server config parameters\n");
	/* Release the mutex */
	gen_mutex_unlock(&mt_config);
	goto return_error;
    }

    /* we need to return the fs_id's to the calling function */
    resp->nr_fsid = server_config.nr_fs;
    resp->fsid_list = malloc(server_config.nr_fs * sizeof(PVFS_handle));
    if (resp->fsid_list == NULL)
    {
	init_fail = GET_CONFIG_INIT_FAIL;
	ret = -ENOMEM;
	goto return_error;
    }

    for(i = 0; i < server_config.nr_fs; i++)
    {
	resp->fsid_list[i] = server_config.fs_info[i].fsid;
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
static int server_get_config(pvfs_mntlist mntent_list)
{
    struct PVFS_server_req_s *req_p = NULL;		/* server request */
    struct PVFS_server_resp_s *ack_p = NULL;	/* server response */
    int ret = -1, i, j;
    bmi_addr_t serv_addr;				 /*PVFS address type structure*/ 
    int len, name_sz, metalen, iolen;
    PVFS_credentials creds;
    char *parse_p;
    struct PINT_decoded_msg decoded;
    void* encoded_resp;
    PVFS_msg_tag_t op_tag = get_next_session_tag();
    PVFS_size max_msg_sz;

    enum {
	NONE_FAIL = 0,
	LOOKUP_FAIL,
	ALLOC_NAME_FAIL,
    } failure = NONE_FAIL;

    req_p = (struct PVFS_server_req_s *) malloc(sizeof(struct PVFS_server_req_s));
    if (req_p == NULL) {
	assert(0);
    }

    /* TODO: Fill up the credentials information */

    /* TODO: IS THIS A REASONABLE MAXIMUM MESSAGE SIZE?  I HAVE NO IDEA */
    max_msg_sz = 
	PINT_get_encoded_generic_ack_sz(0, PVFS_SERV_GETCONFIG) 
	+ (2 * MAX_STRING_SIZE);

    /* Process all entries in pvfstab */
    for (i = 0; i < mntent_list.nr_entry; i++) 
    {
	pvfs_mntent *mntent_p = &mntent_list.ptab_p[i];
	struct fsconfig_s *fsinfo_p;

   	/* Obtain the metaserver to send the request */
	ret = BMI_addr_lookup(&serv_addr, mntent_p->meta_addr);
	if (ret < 0)
	{
	    failure = LOOKUP_FAIL;
	    goto return_error;
	}


	/* Set up the request for getconfig */
	name_sz = strlen(mntent_p->service_name) + 1;

	req_p->op                      = PVFS_SERV_GETCONFIG;
	req_p->rsize                   = sizeof(struct PVFS_server_req_s) + name_sz;
	req_p->credentials             = creds;

	req_p->u.getconfig.fs_name     = mntent_p->service_name;
	req_p->u.getconfig.max_strsize = MAX_STRING_SIZE;

	gossip_ldebug(CLIENT_DEBUG,"asked for fs name = %s\n",
                      req_p->u.getconfig.fs_name);

	/* send the request and receive an acknowledgment */
	ret = PINT_send_req(serv_addr, req_p, max_msg_sz,
	    &decoded, &encoded_resp, op_tag);
	if (ret < 0) {
	    goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	/* Use data from response to update global tables */
	fsinfo_p = &server_config.fs_info[i];

	fsinfo_p->fh_root         = ack_p->u.getconfig.root_handle;
	fsinfo_p->fsid            = ack_p->u.getconfig.fs_id;

        if (server_parse_config(&(ack_p->u.getconfig)))
        {
            gossip_ldebug(CLIENT_DEBUG,"Failed to getconfig from host "
                          "%s\n",mntent_p->meta_addr);
            continue;
        }

/* 	fsinfo_p->meta_serv_count = ack_p->u.getconfig.meta_server_count; */
/* 	fsinfo_p->io_serv_count   = ack_p->u.getconfig.io_server_count; */
/* 	fsinfo_p->maskbits        = ack_p->u.getconfig.maskbits; */


/* 	/\* Copy the client mount point *\/ */
/* 	name_sz = strlen(mntent_p->local_mnt_dir) + 1; */
/* 	fsinfo_p->local_mnt_dir = (PVFS_string) malloc(name_sz); */
/* 	if (fsinfo_p->local_mnt_dir == NULL) { */
/* 	    return -1; */
/* 	} */
/* 	memcpy(fsinfo_p->local_mnt_dir, mntent_p->local_mnt_dir, name_sz); */

/* 	metalen = strlen(ack_p->u.getconfig.meta_server_mapping); */
/* 	iolen   = strlen(ack_p->u.getconfig.io_server_mapping); */

/* 	gossip_ldebug(CLIENT_DEBUG,"server %d config reply:\n\tmeta = %s\n\tio = %s\n", */
/* 	       i, */
/* 	       ack_p->u.getconfig.meta_server_mapping, */
/* 	       ack_p->u.getconfig.io_server_mapping); */

/* 	/\*gossip_ldebug(CLIENT_DEBUG,"maskbits = %ld \nfh_root = %ld\nfsid = %ld\n",fsinfo_p->maskbits, fsinfo_p->fh_root, fsinfo_p->fsid);*\/ */

/* 	/\* How to get the size of metaserver list in ack? *\/ */
/* 	/\* NOTE: PVFS_string == char *, SO I HAVE DOUBTS ABOUT THIS LINE!!! -- ROB *\/ */
/* 	fsinfo_p->meta_serv_array = (PVFS_string *) malloc(fsinfo_p->meta_serv_count * sizeof(PVFS_string)); */
/* 	if (fsinfo_p->meta_serv_array == NULL) */
/* 	{ */
/* 	    ret = -ENOMEM; */
/* 	    goto return_error; */
/* 	} */
/* 	fsinfo_p->bucket_array = (bucket_info *) malloc(fsinfo_p->meta_serv_count * sizeof(bucket_info)); */
/* 	if (fsinfo_p->bucket_array == NULL) */
/* 	{ */
/* 	    ret = -ENOMEM; */
/* 	    goto return_error; */
/* 	} */
/* 	/\* Copy the metaservers from ack to config info *\/ */
/* 	parse_p = ack_p->u.getconfig.meta_server_mapping; */
/* 	gossip_ldebug(CLIENT_DEBUG," = %lld\n", fsinfo_p->fh_root); */
/* 	gossip_ldebug(CLIENT_DEBUG,"meta server count = %d\n", fsinfo_p->meta_serv_count); */
/* 	for (j=0; j < fsinfo_p->meta_serv_count; j++) */
/* 	{ */
/* 	    len = 0; */

/* 	    /\* find next ";" terminator (or end of string) *\/ */
/* 	    while (parse_p[len] != ';' && parse_p[len] != '\0') len++; */

/* #if 0 */
/* 	    /\* skip "pvfs-" *\/ */
/* 	    assert(len > 5); */
/* 	    if (!strncmp(parse_p, "pvfs-", 5)) { */
/* 		parse_p += 5; */
/* 		len -= 5; */
/* 	    } */
/* 	    else { */
/* 		goto return_error; */
/* 	    } */
/* #endif */
/* 	    /\* allocate space for entry *\/ */
/* 	    fsinfo_p->meta_serv_array[j] = (PVFS_string) malloc(len + 1); */
/* 	    if (fsinfo_p->meta_serv_array[j] == NULL) { */
/* 		goto return_error; */
/* 	    } */

/* 	    /\* copy entry *\/ */
/* 	    memcpy(fsinfo_p->meta_serv_array[j], parse_p, len); */
/* 	    fsinfo_p->meta_serv_array[j][len] = '\0'; */
/* 	    gossip_ldebug(CLIENT_DEBUG,"\tmeta server[%d] = %s\n", j, fsinfo_p->meta_serv_array[j]); */

/* 	    if (parse_p[len] == '\0') break; */
/* 	    else parse_p += len + 1; */
/* 	} */

/* 	/\* repeat for i/o servers *\/ */
/* 	fsinfo_p->io_serv_array = (PVFS_string *) malloc(fsinfo_p->io_serv_count * sizeof(PVFS_string)); */
/* 	if (fsinfo_p->io_serv_array == NULL) */
/* 	{ */
/* 	    ret = -ENOMEM; */
/* 	    goto return_error; */
/* 	} */
/* 	fsinfo_p->io_bucket_array = (bucket_info *) malloc(fsinfo_p->io_serv_count * sizeof(bucket_info)); */
/* 	if (fsinfo_p->io_bucket_array == NULL) */
/* 	{ */
/* 	    ret = -ENOMEM; */
/* 	    goto return_error; */
/* 	} */
/* 	parse_p = ack_p->u.getconfig.io_server_mapping; */
/* 	gossip_ldebug(CLIENT_DEBUG,"io server count = %d\n", fsinfo_p->io_serv_count); */
/* 	for (j=0; j < fsinfo_p->io_serv_count; j++) */
/* 	{ */
/* 	    len = 0; */

/* 	    /\* find next ";" terminator (or end of string) *\/ */
/* 	    while (parse_p[len] != ';' && parse_p[len] != '\0') len++; */

/* #if 0 */
/* 	    /\* skip "pvfs-" *\/ */
/* 	    assert(len > 5); */
/* 	    if (!strncmp(parse_p, "pvfs-", 5)) { */
/* 		parse_p += 5; */
/* 		len -= 5; */
/* 	    } */
/* 	    else { */
/* 		goto return_error; */
/* 	    } */
/* #endif */
/* 	    /\* allocate space for entry *\/ */
/* 	    fsinfo_p->io_serv_array[j] = (PVFS_string) malloc(len + 1); */
/* 	    if (fsinfo_p->io_serv_array[j] == NULL) { */
/* 		goto return_error; */
/* 	    } */

/* 	    /\* copy entry *\/ */
/* 	    memcpy(fsinfo_p->io_serv_array[j], parse_p, len); */
/* 	    fsinfo_p->io_serv_array[j][len] = '\0'; */
/* 	    gossip_ldebug(CLIENT_DEBUG,"\tio server[%d] = %s\n", j, fsinfo_p->io_serv_array[j]); */

/* 	    if (parse_p[len] == '\0') break; */
/* 	    else parse_p += len + 1; */
/* 	} */

	/* let go of any resources consumed by PINT_send_req() */
	PINT_release_req(serv_addr, req_p, max_msg_sz, &decoded,
	    &encoded_resp, op_tag);
        break;
    }

    return(0); 


    /* TODO: FIX UP ERROR HANDLING; NEEDS TO BE REVIEWED TOTALLY */
 return_error:
    return -1;
}	  

static int server_parse_config(PVFS_servresp_getconfig *response)
{
    int ret = 1;
    int fs_fd = 0, server_fd = 0;
    char fs_template[] = ".__pvfs_fs_configXXXXXX";
    char server_template[] = ".__pvfs_server_configXXXXXX";
    char *args[3] = { NULL, fs_template, server_template };

    if (response)
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
                ret = PINT_server_config(&g_server_config,3,args);
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
