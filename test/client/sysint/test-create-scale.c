/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include "client.h"
#include "pvfs2-util.h"
#include "str-utils.h"

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *basename = (char *)0;
    PVFS_fs_id cur_fs;
    PVFS_sysresp_create resp_create;
    char entry_name[256];
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    int max_dfiles = 0;
    int iters = 0;
    int i,j;
    double start_time;
    double end_time;
    double* measurements;

    if (argc != 4)
    {
        fprintf(stderr,"Usage: %s <filename (base)> <max dfiles> <iters>\n",argv[0]);
        return ret;
    }
    basename = argv[1];
    ret = sscanf(argv[2], "%d", &max_dfiles);
    if(ret != 1)
    {
	fprintf(stderr, "Error: could not parse args.\n");
	return(-1);
    }
    ret = sscanf(argv[3], "%d", &iters);
    if(ret != 1)
    {
	fprintf(stderr, "Error: could not parse args.\n");
	return(-1);
    }

    measurements = (double*)malloc(max_dfiles*sizeof(double));
    assert(measurements);
    memset(measurements, 0, max_dfiles*sizeof(double));

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }
    ret = PVFS_util_get_default_fsid(&cur_fs);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
    }

    if (PVFS_util_remove_base_dir(basename,str_buf,256))
    {
        if (basename[0] != '/')
        {
            printf("You forgot the leading '/'\n");
        }
        printf("Cannot retrieve entry name for creation on %s\n",
               basename);
        return(-1);
    }

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));
    PVFS_util_gen_credentials(&credentials);

    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime = 
	time(NULL);
    attr.dfile_count = max_dfiles;
    attr.mask |= PVFS_ATTR_SYS_DFILE_COUNT;

    ret = PVFS_util_lookup_parent(basename, cur_fs, credentials, 
                                  &parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    parent_refn.fs_id = cur_fs;

    /* do one big one to prime connections if needed */
    sprintf(entry_name, "%s%d", str_buf, 0);
    ret = PVFS_sys_create(entry_name, parent_refn, attr,
			  credentials, NULL, &resp_create);
    if (ret < 0)
    {
	PVFS_perror("PVFS_sys_create", ret);
	return(-1);
    }
    ret = PVFS_sys_remove(entry_name, parent_refn, credentials);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_remove", ret);
	return(-1);
    }

    for(i=0; i<max_dfiles; i++)
    {
	start_time = Wtime();
	for(j=0; j<iters; j++)
	{
	    sprintf(entry_name, "%s%d", str_buf, j);
	    attr.dfile_count = i+1;
	    ret = PVFS_sys_create(entry_name, parent_refn, attr,
				  credentials, NULL, &resp_create);
	    if (ret < 0)
	    {
		PVFS_perror("PVFS_sys_create", ret);
		return(-1);
	    }
	}
	end_time = Wtime();
	measurements[i] = end_time-start_time;

	for(j=0; j<iters; j++)
	{
	    sprintf(entry_name, "%s%d", str_buf, j);
	    ret = PVFS_sys_remove(entry_name, parent_refn, credentials);
	    if(ret < 0)
	    {
		PVFS_perror("PVFS_sys_remove", ret);
		return(-1);
	    }
	}
    }

    /* print out some results */
    printf("<num dfiles> <ave. time each> <iterations>\n");
    for(i=0; i<max_dfiles; i++)
    {
	printf("%d\t%f\t%d\n", (i+1), (measurements[i]/iters), iters);
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
