/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

#include <trove.h>
#include <trove-test.h>
#include <job.h>
#include <job-help.h>

char storage_space[SSPACE_SIZE] = "/tmp/trove-test-space";
char file_system[FS_SIZE] = "fs-foo";

int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle root_handle;
    TROVE_keyval_s key, val;
    char *method_name;
	 job_status_s job_stat;
	 job_id_t foo_id;

    char root_handle_string[] = ROOT_HANDLE_STRING;

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr, "argument parsing failed.\n");
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

    /* try to initialize; fails if storage space isn't there? */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr, "warning: initialize failed.  trying to create storage space.\n");

	/* create the storage space */
	/* Q: what good is the op_id here if we have to match on coll_id in test fn? */
	ret = trove_storage_create(storage_space, NULL, &op_id);
	if (ret < 0) {
	    fprintf(stderr, "storage create failed.\n");
	    return -1;
	}

	/* second try at initialize, in case it failed first try. */
	ret = trove_initialize(storage_space, 0, &method_name, 0);
	if (ret < 0) {
	    fprintf(stderr, "initialized failed second time.\n");
	    return -1;
	}
    }

	/* try to look up collection used to store file system */
	ret = job_trove_fs_lookup(file_system, NULL, &job_stat, &foo_id);
	if(ret == 1 && job_stat.error_code == 0)
	{
		fprintf(stderr, "collection lookup succeeded before it should.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret == 0)
		{
			fprintf(stderr, "collection lookup succeeded before it should (at job_test()).\n");
			return(-1);
		}
	}

	/* create the collection */
	ret = job_trove_fs_create(file_system, FS_COLL_ID, NULL,
		&job_stat, &foo_id);
	if(ret < 0)
	{
		fprintf(stderr, "fs create failed.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret < 0)
		{
			fprintf(stderr, "fs create failed (at job_test()).\n");
			return(-1);
		}
	}
	if(job_stat.error_code)
	{
		fprintf(stderr, "bad status after fs_create().\n");
		return(-1);
	}
    
    /* lookup collection.  this is redundant because we just gave it a coll. id to use,
     * but it's a good test i guess...
     */
	ret = job_trove_fs_lookup(file_system, NULL, &job_stat, &foo_id);
	if(ret < 0)
	{
		fprintf(stderr, "collection lookup failed.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret < 0)
		{
			fprintf(stderr, "collection lookup failed (at job_test()).\n");
			return(-1);
		}
	}
	if(job_stat.error_code)
	{
		fprintf(stderr, "bad status after fs_lookup().\n");
		return(-1);
	}

	coll_id = job_stat.coll_id;

    /* create a dataspace to hold the root directory */
    /* Q: what should the bitmask be? */
    /* Q: where are we going to define the dspace types? -- trove-test.h for now. */
    root_handle = 7;

	ret = job_trove_dspace_create(coll_id,
		root_handle, 
		0xffffffff,
		TROVE_TEST_DIR,
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

	root_handle = job_stat.handle;

    /* add attribute to collection for root handle */
    /* NOTE: should be using the data_sz field, but it doesn't exist yet. */
    /* NOTE: put ROOT_HANDLE_STRING in trove-test.h; not sure where it should be. */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &root_handle;
    val.buffer_sz = sizeof(root_handle);

	ret = job_trove_fs_seteattr(coll_id, &key, &val, 0, NULL,
		&job_stat, &foo_id);
	if(ret < 0)
	{
		fprintf(stderr, "job_trove_fs_seteattr() failure.\n");
		return(-1);
	}
	if(ret == 0)
	{
		ret = block_on_job(foo_id, NULL, &job_stat);
		if(ret < 0)
		{
			fprintf(stderr, "fs_seteattr failed (at job_test()).\n");
			return(-1);
		}
	}
	if(job_stat.error_code)
	{
		fprintf(stderr, "bad status after fs_seteattr().\n");
		return(-1);
	}

    /* add attribute to collection for last used handle ??? */

	 job_finalize();
	 trove_finalize();
    
    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
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
