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

char storage_space[SSPACE_SIZE] = "/tmp/storage-space-foo";
char file_system[FS_SIZE] = "fs-foo";
char path_to_dir[PATH_SIZE] = "/";
TROVE_handle requested_file_handle = 4095;

int parse_args(int argc, char **argv);

#define KEYVAL_ARRAY_LEN 10
int main(int argc, char **argv)
{
    int ret, chunk, num_processed, i,j;
    TROVE_coll_id coll_id;
    TROVE_handle handle;
    TROVE_keyval_s key[KEYVAL_ARRAY_LEN], val[KEYVAL_ARRAY_LEN];
    TROVE_ds_position pos = TROVE_ITERATE_START;
    char *method_name;
    char path_name[PATH_SIZE];
	 job_status_s job_stat;
	 job_id_t foo_id;
	 PVFS_vtag_s dummy_vtag;

    TROVE_handle ls_handle[KEYVAL_ARRAY_LEN];
    char ls_name[KEYVAL_ARRAY_LEN][PATH_SIZE];


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
		fprintf(stderr, "bad status after fs_lookup.\n");
		return(-1);
	}

	coll_id = job_stat.coll_id;

    strcpy(path_name, path_to_dir);
    printf("path is %s\n", path_name);

    /* find the directory handle */
    ret = path_lookup(coll_id, path_name, &handle);
    if (ret < 0) {
	return -1;
    }

    /* TODO: verify that this is in fact a directory! */

    /* iterate through keyvals in directory */

    /* trove_keyval_iterate will let the caller know how much progress was made
     * through the 'count' parameter.  The caller should check 'count' after
     * calling trove_keval_iterate: if it is different, that means EOF reached
     */

    for (j=0; j< KEYVAL_ARRAY_LEN; j++ ) {
	    key[j].buffer = ls_name[j];
	    key[j].buffer_sz = PATH_SIZE;
	    val[j].buffer = &ls_handle[j]; 
	    val[j].buffer_sz = sizeof(ls_handle);
    }
	 num_processed = chunk = KEYVAL_ARRAY_LEN;
    while ( num_processed == chunk ) 
	{
		ret = job_trove_keyval_iterate(coll_id, handle, pos, key, val, chunk,
			0, dummy_vtag, NULL, &job_stat, &foo_id);
		if(ret < 0)
		{
			fprintf(stderr, "keyval iterate failed.\n");
			return(-1);
		}
		if(ret == 0)
		{
			ret = block_on_job(foo_id, NULL, &job_stat);
			if(ret < 0)
			{
				fprintf(stderr, "keyval iterate failed (at job_test()).\n");
				return(-1);
			}
		}
		if(job_stat.error_code)
		{
			fprintf(stderr, "bad status after keyval_iterate.\n");
			return(-1);
		}
		pos = job_stat.position;
		num_processed = job_stat.count;
		for(i=0; i<job_stat.count; i++)
		{
			printf("%s (%Ld)\n", (char *) key[i].buffer, 
				*(TROVE_handle *) val[i].buffer);
		}
	}

	job_finalize();
    trove_finalize();

    return 0;
}

int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:f:h:")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, SSPACE_SIZE);
		break;
	    case 'c': /* collection */
		strncpy(file_system, optarg, FS_SIZE);
		break;
	    case 'p':
		strncpy(path_to_dir, optarg, PATH_SIZE);
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
