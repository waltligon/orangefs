/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* System Interface Initialize Implementation */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>

#include "pcache.h"
#include "pint-dcache.h"
#include "config-manage.h"
#include "pvfs2-sysint.h"
#include "pint-sysint.h"
#include "gen-locks.h"

/* pinode cache */
extern pcache pvfs_pcache; 
/* PVFS directory cache */
extern struct dcache pvfs_dcache;
extern fsconfig_array server_config;

extern gen_mutex_t *g_session_tag_mt_lock;

/* PVFS_sys_initialize()
 *
 * Initializes the PVFS system interface and any necessary internal data
 * structures.  Must be called before any other system interface function.
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_initialize(pvfs_mntlist mntent_list)
{
	int ret = -1;
	gen_mutex_t mt_config;
	
	/* Initialize BMI */
	/*TODO: change this so it parses the bmi module from the pvfstab file*/
	ret = BMI_initialize("bmi_tcp",NULL,0);
	if (ret < 0)
	{
		printf("BMI initialize failure\n");
		goto BMI_init_failure;
	}

	/* initialize bmi session identifier */
	g_session_tag_mt_lock = gen_mutex_build();

	/* Initialize flow */
	/* Leaving out for now until flows are implemented */
#if 0
	ret = PINT_flow_initialize(flag);
	if (ret < 0)
	{
		printf("Flow initialize failure.\n");
		goto getconfig_failure;
	}
#endif

	/* Initialize the job interface */
	ret = job_initialize(0);
	if (ret < 0)
	{
		printf("Error initializing job interface: %s\n",strerror(-ret));
		goto job_init_failure;
	}
	
	/* Initialize the pinode cache */
	ret = pcache_initialize(&pvfs_pcache);
	if (ret < 0)
	{
		printf("Error initializing pinode cache\n");
		goto pcache_init_failure;	
	}	
	/* Initialize the directory cache */
	ret = dcache_initialize(&pvfs_dcache);
	if (ret < 0)
	{
		printf("Error initializing directory cache\n");
		goto dcache_init_failure;	
	}	

	/* TODO: Uncomment both these functions */
	/* Allocate memory for the global configuration structures */
	/*server_config.nr_fs = mntent_list.nr_entry;
	server_config.fs_info = (fsconfig *)malloc(mntent_list.nr_entry\
			* sizeof(fsconfig));
	if (!server_config.fs_info)
	{
		printf("Error in allocating configuration parameters\n");
		ret = -ENOMEM;
		goto config_alloc_failure;
	}*/

	/* Grab the mutex - serialize all writes to server_config */
	gen_mutex_lock(&mt_config);	
	
	/* Initialize the configuration management interface */
	ret = config_bt_initialize(mntent_list);
	if (ret < 0)
	{
		printf("Error in initializing configuration management interface\n");
		/* Release the mutex */
		gen_mutex_unlock(&mt_config);
		goto config_bt_failure;
	}

	/* Get configuration parameters from server */
	ret = server_getconfig(mntent_list);
	if (ret < 0)
	{
		printf("Error in getting server config parameters\n");
		/* Release the mutex */
		gen_mutex_unlock(&mt_config);
		goto config_bt_failure;
	}	

	/* Release the mutex */
	gen_mutex_unlock(&mt_config);
	
	/* load the server info from config file to table */
	return(0);

config_bt_failure:
	/* Free the server config */
	free(server_config.fs_info);
	dcache_finalize(&pvfs_dcache);

dcache_init_failure:
	pcache_finalize(pvfs_pcache);

pcache_init_failure:
	job_finalize();

job_init_failure:
	BMI_finalize();

BMI_init_failure:
	return(ret);
}


