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

#include "acache.h"
#include "pint-bucket.h"
#include "pint-dcache.h"
#include "pvfs2-sysint.h"
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "pint-servreq.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "client-state-machine.h"

job_context_id PVFS_sys_job_context = -1;

extern gen_mutex_t *g_session_tag_mt_lock;

static char* build_flow_module_list(pvfs_mntlist* mntlist);

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal data
 * structures.  Must be called before any other system interface function.
 *
 * the default_debug_mask is used if not overridden by the PVFS2_DEBUGMASK
 * environment variable at run-time.  allowable string formats of the
 * env variable are the same as the EventLogging line in the server
 * configuration file.
 *
 * returns 0 on success, -errno on failure
 */

int PVFS_sys_initialize(
    pvfs_mntlist mntent_list,
    int default_debug_mask,
    PVFS_sysresp_init *resp)
{
    int ret = -1, i, j;
    int num_file_systems = 0;
    gen_mutex_t *mt_config = NULL;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;
    const char **method_ptr_list;
    int num_method_ptr_list, max_method_ptr_list;
    char *method_list = 0;
    char *flowproto_list = NULL;
    char *debug_mask_str = NULL;
    int debug_mask = 0;

    enum {
	NONE_INIT_FAIL = 0,
	ENCODER_INIT_FAIL,
	BMI_INIT_FAIL,
	FLOW_INIT_FAIL,
	JOB_INIT_FAIL,
	JOB_CONTEXT_FAIL,
	ACACHE_INIT_FAIL,
	DCACHE_INIT_FAIL,
	BUCKET_INIT_FAIL,
	GET_CONFIG_INIT_FAIL
    } init_fail = NONE_INIT_FAIL;

    gossip_enable_stderr();

    debug_mask_str = getenv("PVFS2_DEBUGMASK");
    debug_mask = (debug_mask_str ?
                  PVFS_debug_eventlog_to_mask(debug_mask_str) :
                  default_debug_mask);

    gossip_set_debug_mask(1,debug_mask);

    /* make sure we were given sane arguments */
    if ((mntent_list.ptab_array == NULL) || (resp == NULL))
    {
	ret = -EINVAL;
	init_fail = NONE_INIT_FAIL;
	goto return_error;
    }

    /* initialize protocol encoder */
    ret = PINT_encode_initialize();
    if(ret < 0)
    {
	init_fail = ENCODER_INIT_FAIL;
	goto return_error;
    }

    /* Parse the method types from the mntent_list */
    num_method_ptr_list = 0;
    max_method_ptr_list = 0;
    method_ptr_list = 0;
    for (i=0; i<mntent_list.ptab_count; i++) {
	const char *meth_name = BMI_method_from_scheme(
	  mntent_list.ptab_array[i].pvfs_config_server);
	for (j=0; j<num_method_ptr_list; j++) {
	    if (method_ptr_list[j] == meth_name)
		break;
	}
	if (j == num_method_ptr_list && meth_name) {  /* ignore unknown ones */
	    if (num_method_ptr_list == max_method_ptr_list) {
		const char **x = method_ptr_list;
		max_method_ptr_list += 2;
		method_ptr_list = malloc(
		  max_method_ptr_list * sizeof(*method_ptr_list));
		if (!method_ptr_list) {
		    init_fail = BMI_INIT_FAIL;
		    ret = -ENOMEM;
		    goto return_error;
		}
		if (x) {
		    memcpy(method_ptr_list, x,
		      num_method_ptr_list * sizeof(*method_ptr_list));
		    free(x);
		}
	    }
	    method_ptr_list[num_method_ptr_list] = meth_name;
	    ++num_method_ptr_list;
	}
    }
    if (num_method_ptr_list) {
	j = num_method_ptr_list;  /* intervening , and ending \0 */
	for (i=0; i<num_method_ptr_list; i++)
	    j += strlen(method_ptr_list[i]);
	method_list = malloc(j * sizeof(char));
	method_list[0] = 0;
	for (i=0; i<num_method_ptr_list; i++) {
	    if (i > 0)
		strcat(method_list, ",");
	    strcat(method_list, method_ptr_list[i]);
	}
	free(method_ptr_list);
    }

    if(method_list == NULL)
    {
	gossip_err("Error: failed to parse BMI method names from tab file entries.\n");
	ret = -EINVAL;
	init_fail = BMI_INIT_FAIL;
	goto return_error;
    }

    /* parse flowprotocol list as well */
    flowproto_list = build_flow_module_list(&mntent_list);
    if(!flowproto_list)
    {
	gossip_err("Error: failed to parse flow protocols from tab file entries.\n");
	ret = -EINVAL;
	init_fail = BMI_INIT_FAIL;
	goto return_error;
    }
    
    /* Initialize BMI */
    ret = BMI_initialize(method_list,NULL,0);
    if (ret < 0)
    {
	init_fail = BMI_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"BMI initialize failure\n");
	goto return_error;
    }
    if (method_list)
	free(method_list);

    /* initialize bmi session identifier, TODO: DOCUMENT THIS */
    g_session_tag_mt_lock = gen_mutex_build();

    /* Initialize flow */
    ret = PINT_flow_initialize(flowproto_list, 0);
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
    ret = PINT_acache_initialize();
    if (ret < 0)
    {
	init_fail = ACACHE_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error initializing pinode cache\n");
	goto return_error;	
    }
    PINT_acache_set_timeout(PINT_ACACHE_TIMEOUT * 1000);

    /* Initialize the directory cache */
    ret = PINT_dcache_initialize();
    if (ret < 0)
    {
	init_fail = DCACHE_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error initializing directory cache\n");
	goto return_error;	
    }	
    PINT_dcache_set_timeout(PINT_DCACHE_TIMEOUT * 1000);

    /* Get configuration parameters from server */
    ret = PINT_server_get_config(PINT_get_server_config_struct(),mntent_list);
    if (ret < 0)
    {
	init_fail = DCACHE_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error in getting server config parameters\n");
	goto return_error;
    }
    num_file_systems = PINT_llist_count(
      PINT_get_server_config_struct()->file_systems);
    assert(num_file_systems);

    /* Grab the mutex - serialize all writes to server_config */
    mt_config = gen_mutex_build();
    if (!mt_config)
    {
	init_fail = DCACHE_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,
                      "Failed to initialize mutex\n");
	goto return_error;
    }

    gen_mutex_lock(mt_config);	

    /* Initialize the configuration management interface */
    ret = PINT_bucket_initialize();
    if (ret < 0)
    {
	init_fail = BUCKET_INIT_FAIL;
	gossip_ldebug(CLIENT_DEBUG,"Error in initializing configuration management interface\n");
	/* Release the mutex */
	gen_mutex_unlock(mt_config);
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
    cur = PINT_get_server_config_struct()->file_systems;
    i = 0;
    while(cur && (i < num_file_systems))
    {
        cur_fs = PINT_llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        assert(cur_fs->coll_id);
        if (PINT_handle_load_mapping(PINT_get_server_config_struct(), cur_fs))
        {
            init_fail = GET_CONFIG_INIT_FAIL;
            gossip_ldebug(CLIENT_DEBUG,"Failed to load fs info into the "
                          "PINT_handle interface.\n");
            gen_mutex_unlock(mt_config);
            goto return_error;
        }
        resp->fsid_list[i++] = cur_fs->coll_id;
        cur = PINT_llist_next(cur);
    }

    /* Release the mutex */
    gen_mutex_unlock(mt_config);
    gen_mutex_destroy(mt_config);

    assert(i == num_file_systems);
    return(0);

 return_error:
    switch(init_fail) {
	case GET_CONFIG_INIT_FAIL:
	    PINT_bucket_finalize();
	case BUCKET_INIT_FAIL:
	    PINT_dcache_finalize();
	case DCACHE_INIT_FAIL:
	    PINT_acache_finalize();
	case ACACHE_INIT_FAIL:
	case JOB_CONTEXT_FAIL:
	    job_finalize();
	case JOB_INIT_FAIL:
	    PINT_flow_finalize();
	case FLOW_INIT_FAIL:
	    BMI_finalize();
	case BMI_INIT_FAIL:
	    PINT_encode_finalize();
	case ENCODER_INIT_FAIL:
	case NONE_INIT_FAIL:
	    /* nothing to do for either of these */
	    break;
    }
    gossip_disable();

    return(ret);
}

/* build_flow_module_list()
 *
 * builds a string specifying a list of flow protocols suitable for use
 * as an argument to PINT_flow_initialize(), based on flow protocols
 * found in mntlist
 *
 * returns pointer to string on success, NULL on failure
 * NOTE: caller must free string returned by this function
 */
static char* build_flow_module_list(pvfs_mntlist* mntlist)
{
    int i,j;
    int found = 0;
    int new_len = 0;
    /* we always load up at least the default module */
    char* ret_str = NULL;
    char* old_ret_str = NULL;
    char* next_mod = NULL;

    /* iterate through array */
    for(i=0; i<mntlist->ptab_count; i++)
    {
	switch(mntlist->ptab_array[i].flowproto)
	{
	    case FLOWPROTO_BMI_TROVE:
		next_mod = "flowproto_bmi_trove";
		break;
	    case FLOWPROTO_DUMP_OFFSETS:
		next_mod = "flowproto_dump_offsets";
		break;
	    case FLOWPROTO_BMI_CACHE:
		next_mod = "flowproto_bmi_cache";
		break;
	    case FLOWPROTO_MULTIQUEUE:
		next_mod = "flowproto_multiqueue";
		break;
	}

	/* see if we have already found this module */
	found = 0;
	for(j=0; j<i; j++)
	{
	    if(mntlist->ptab_array[i].flowproto == 
		mntlist->ptab_array[j].flowproto)
	    {
		found = 1;
		break;
	    }
	}

	/* if we don't already have this module in our list, add it in */
	if(!found)
	{
	    old_ret_str = ret_str;
	    new_len = strlen(next_mod) + 2;
	    if(old_ret_str)
		new_len += strlen(old_ret_str);
	    ret_str = (char*)malloc(new_len);
	    if(!ret_str)
	    {
		if(old_ret_str)
		    free(old_ret_str);
		return(NULL);
	    }
	    memset(ret_str, 0, new_len);
	    if(old_ret_str)
	    {
		strcpy(ret_str, old_ret_str);
		strcat(ret_str, ",");
	    }
	    strcat(ret_str, next_mod);
	    if(old_ret_str)
		free(old_ret_str);
	}
    }

    return(ret_str);
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
