/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <trove.h>
#include <trove-test.h>
#include <job.h>
#include <job-help.h>

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";
char path_to_file[PATH_SIZE] = "/bar";

extern char *optarg;

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret, i;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle;
    char *method_name;
	 job_id_t foo_id;
	 job_status_s job_stat;
	job_context_id context;


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

	ret = job_open_context(&context);
	if(ret < 0)
	{
		fprintf(stderr, "job_open_context() failure.\n");
		return(-1);
	}


    /* try to look up collection used to store file system */
	ret = job_trove_fs_lookup(file_system, NULL, &job_stat, &foo_id,
	context);
	if(ret < 0)
	{
		fprintf(stderr, "fs lookup failed.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat, context);
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

	for(i=0; i<20; i++)
	{
		/* lets assume bucket 1, 8 bit handle mask */
		 file_handle = 0x01000000;

		 /* create the new dspace */
		ret = job_trove_dspace_create(coll_id,
			file_handle, 
			TROVE_TEST_FILE,
			NULL,
			NULL,
			&job_stat,
			&foo_id,
			context);
		if(ret < 0)
		{
			fprintf(stderr, "job_trove_dspace_create() failure.\n");
			return(-1);
		}
		if(ret == 0)
		{
			ret = block_on_job(foo_id, NULL, &job_stat,
			context);
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

		printf("trove created handle: %ld\n",
			(long)job_stat.handle);
	}

	job_close_context(context);
	job_finalize();

    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:p:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
		break;
	    case 'p':
		strncpy(path_to_file, optarg, PATH_SIZE);
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
