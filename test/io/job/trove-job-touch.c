/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <trove.h>
#include <trove-test.h>
#include <job.h>
#include <job-help.h>

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, i;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle, parent_handle;
    TROVE_keyval_s key, val;
    char *method_name, *file_name;
    char path_name[PATH_SIZE];
	 job_id_t foo_id;
	 job_status_s job_stat;


    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr, "argument parsing failed.\n");
	return -1;
    }

    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr, "initialize failed.\n");
	return -1;
    }

	/* TODO: this is temporary; just pulling in BMI and flow symbols that are
	 * needed for job library */
	ret = BMI_initialize("bogus", NULL, 0);
	if(ret > -1)
	{
		fprintf(stderr, "BMI_initialize() succeeded when it shouldn't have.\n");
		return(-1);
	}
	ret = PINT_flow_initialize("bogus", 0);
	if(ret > -1)
	{
		fprintf(stderr, "flow_initialize() succeeded when it shouldn't have.\n");
		return(-1);
	}

	ret = job_initialize(0);
	if(ret < 0)
	{
		fprintf(stderr, "job_initialize() failure.\n");
		return(-1);
	}

    /* try to look up collection used to store file system */
	ret = job_trove_fs_lookup(file_system, NULL, &job_stat, &foo_id);
	if(ret < 0)
	{
		fprintf(stderr, "fs lookup failed.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret < 0)
		{
			fprintf(stderr, "fs lookup failed (at job_test()).\n");
			return(-1);
		}
	}
	if(job_stat.error_code)
	{
		fprintf(stderr, "bad status after fs_lookup().\n");
		return(-1);
	}

	coll_id = job_stat.coll_id;

    /* find the parent directory name */
    strcpy(path_name, path_to_file);
    for (i=strlen(path_name); i >= 0; i--) {
	if (path_name[i] != '/') path_name[i] = '\0';
	else break;
    }
    file_name = path_to_file + strlen(path_name);
    printf("path is %s\n", path_name);
    printf("file is %s\n", file_name);

    /* find the parent directory handle */
    ret = path_lookup(coll_id, path_name, &parent_handle);
    if (ret < 0) {
	return -1;
    }

    /* TODO: verify that this is in fact a directory! */
    
    /* Q: how do I know what handle to use for the new file? */
    file_handle = requested_file_handle;

    /* create the new dspace */
	ret = job_trove_dspace_create(coll_id,
		file_handle, 
		0xffffffff,
		TROVE_TEST_FILE,
		NULL,
		NULL,
		&job_stat,
		&foo_id);
	if(ret < 0)
	{
		fprintf(stderr, "job_trove_dspace_create() failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret < 0)
		{
			fprintf(stderr, "dspace_create failed (at job_test()).\n");
			return(-1);
		}
	}
	if(job_stat.error_code)
	{
		fprintf(stderr, "bad status after dspace_create().\n");
		return(-1);
	}

	file_handle = job_stat.handle;

    /* TODO: set attributes of file? */

    /* add new file name/handle pair to parent directory */
    key.buffer = file_name;
    key.buffer_sz = strlen(file_name) + 1;
    val.buffer = &file_handle;
    val.buffer_sz = sizeof(file_handle);

	ret = job_trove_keyval_write(coll_id, parent_handle, &key,
		&val, 0, NULL, NULL, &job_stat, &foo_id);
	if(ret < 0)
	{
		fprintf(stderr, "job_trove_keyval_write() failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret < 0)
		{
			fprintf(stderr, "dspace_create failed (at job_test()).\n");
			return(-1);
		}
	}
	if(job_stat.error_code)
	{
		fprintf(stderr, "bad status after keyval_write().\n");
		return(-1);
	}

	job_finalize();
    trove_finalize();

    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:p:h:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
		break;
	    case 'p':
		strncpy(path_to_file, optarg, PATH_SIZE);
	    case 'h':
		requested_file_handle = atoll(optarg);
		break;
	    case '?':
	    default:
		return -1;
	}
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
