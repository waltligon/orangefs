/* 
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* System Interface Finalize Implementation */

#include <pint-sysint.h>
#include <pcache.h>
#include <pint-dcache.h>
#include <config-manage.h>

extern pcache pvfs_pcache;
extern dcache pvfs_dcache;
extern fsconfig_array server_config;

/* PVFS_finalize
 *
 * shuts down the PVFS system interface
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_finalize()
{
	int ret = 0;
	
	/* Free the dcache */
	dcache_finalize(pvfs_dcache);
	/* free all pinode structures */
	pcache_finalize(pvfs_pcache);
	
	/* Shut down the configuration management interface */
	ret = config_bt_finalize();
	
	/* Close down all flows,endpoints */
	/* leaving this out for now until flows are implemented */
#if 0
	PINT_flow_finalize();
#endif

	/* TODO: finalize BMI and JOB interface */

	/* finalize the job interface */
	job_finalize();

	BMI_finalize();
	
	return(0);
}


