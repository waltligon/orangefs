/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gossip.h"
#include "pint-dev.h"
#include "job.h"
#include "job-help.h"

int main(int argc, char **argv)	
{
    int ret = 1;
    struct PINT_dev_unexp_info info;
    char buf1[] = "Hello ";
    char buf2[] = "World.";
    void* buffer_list[2];
    int size_list[2];
    job_context_id context;
    job_status_s jstat;
    job_id_t tmp_id;

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if(ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return(-1);
    }

    /* start the BMI interface */
    ret = BMI_initialize("bmi_tcp", NULL, 0);
    if(ret < 0)
    {
	    fprintf(stderr, "BMI_initialize failure.\n");
	    return(-1);
    }

    /* start the flow interface */
    ret = PINT_flow_initialize("flowproto_bmi_trove", 0);
    if(ret < 0)
    {
	    fprintf(stderr, "flow init failure.\n");
	    return(-1);
    }

    ret = job_initialize(0);
    if(ret < 0)
    {
	PVFS_perror("job_initialize", ret);
	return(-1);
    }

    ret = job_open_context(&context);
    if(ret < 0)
    {
	PVFS_perror("job_open_context", ret);
	return(-1);
    }
	
    /* post job for unexpected device messages */
    ret = job_dev_unexp(&info, NULL, 0, &jstat, &tmp_id, 0, context);
    if(ret < 0)
    {
	PVFS_perror("job_dev_unexp()", ret);
	return(-1);
    }
    if(ret == 0)
    {	
	printf("no immed completion.\n");
	ret = block_on_job(tmp_id, NULL, &jstat, context);
	if(ret < 0)
	{
	    PVFS_perror("block_on_job()", ret);
	    return(-1);
	}
    }
    else
    {
	printf("immed completion.\n");
    }

    if(jstat.error_code != 0)
    {
	PVFS_perror("job_bmi_unexp() error code", jstat.error_code);
	return(-1);
    }

    printf("Got message: size: %d, tag: %d, payload: %s\n", 
	info.size, (int)info.tag, (char*)info.buffer);

    PINT_dev_release_unexpected(&info);

    /* try writing a list message */
    buffer_list[0] = buf1;
    buffer_list[1] = buf2;
    size_list[0] = strlen(buf1);
    size_list[1] = strlen(buf1) + 1;

    ret = job_dev_write_list(buffer_list, size_list, 2, (strlen(buf1) +
	strlen(buf2) + 1), 7, PINT_DEV_EXT_ALLOC, NULL, 0, &jstat, &tmp_id,
	context);
    if(ret < 0)
    {
	PVFS_perror("job_dev_write_list()", ret);
	return(-1);
    }
    if(ret == 0)
    {	
	printf("no immed completion.\n");
	ret = block_on_job(tmp_id, NULL, &jstat, context);
	if(ret < 0)
	{
	    PVFS_perror("block_on_job()", ret);
	    return(-1);
	}
    }
    else
    {
	printf("immed completion.\n");
    }

    if(jstat.error_code != 0)
    {
	PVFS_perror("job_bmi_write_list() error code", jstat.error_code);
	return(-1);
    }

    job_close_context(context);
    job_finalize();
    PINT_dev_finalize();
    PINT_flow_finalize();
    BMI_finalize();

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
