/*
 * usage:  -d /path/to/directory -n number_of_files
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <assert.h>
#include <mpi.h>
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

extern char *optarg;
int opt_nfiles = -1;
char opt_basedir[PATH_MAX];
int opt_dirarg = -1;

void usage(char *name);
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str);

void usage(char *name)
{
    fprintf(stderr, "usage: %s -d /path/to/directory -n #_of_files\n", name);
    exit(-1);
}
int parse_args(int argc, char **argv)
{
    int c;

	 if(argc < 3)
	 {
		  usage(argv[0]);
	 }

    while ( (c = getopt(argc, argv, "d:n:")) != -1 ) {
		  switch (c) {
				case 'd':
					 strncpy(opt_basedir, optarg, PATH_MAX);
					 opt_dirarg = 1;
					 break;
				case 'n':
					 opt_nfiles = atoi(optarg);
					 break;
				case '?':
				case ':':
				default:
					 usage(argv[0]);
		  }
	 }

	 if(opt_nfiles == -1 || opt_dirarg == -1)
	 {
		  usage(argv[0]);
	 }

	 return 0;
}

void handle_error(int errcode, char *str)
{
    char msg[MPI_MAX_ERROR_STRING];
    int resultlen;
    MPI_Error_string(errcode, msg, &resultlen);
    fprintf(stderr, "%s: %s\n", str, msg);
    MPI_Abort(MPI_COMM_WORLD, 1);
}


int main(int argc, char **argv)
{
	 PVFS_error pvfs_error;
    int i;
	 char test_file[PATH_MAX];
	 PVFS_sys_attr attr;
	 PVFS_fs_id cur_fs;
	 PVFS_credentials credentials;
	 PVFS_sysresp_lookup lookup_resp;
	 PVFS_sysresp_create create_resp;

    int rank, nprocs, ret;
    MPI_Info info;
    time_t test_start, test_end, timediff;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    parse_args(argc, argv);

    /* provide hints if you want  */
    info = MPI_INFO_NULL;

	 if (rank == 0)
	 {
		  printf("\nprocs: %d\nops: %d\n===========\n", nprocs, opt_nfiles);
	 }

	 ret = PVFS_util_init_defaults();
	 if(ret != 0)
	 {
		  return ret;
	 }

	 ret = PVFS_util_get_default_fsid(&cur_fs);
	 if(ret != 0)
	 {
		  return ret;
	 }

	 PVFS_util_gen_credentials(&credentials);

	 attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
	 attr.owner = credentials.uid;
	 attr.group = credentials.gid;
	 attr.perms = 1877;
	 attr.atime = attr.ctime = attr.mtime = time(NULL);

	 pvfs_error = PVFS_sys_lookup(cur_fs, opt_basedir, &credentials, &lookup_resp, 
						  PVFS2_LOOKUP_LINK_NO_FOLLOW);
	 if(pvfs_error != 0)
	 {
		  return PVFS_get_errno_mapping(pvfs_error);
	 }

    for(i = 0; i < opt_nfiles; ++i)
    {
		  snprintf(test_file, PATH_MAX, "testfile.%d.%d", rank, i);
		  test_start = time(NULL);

		  pvfs_error = PVFS_sys_create(test_file, lookup_resp.ref, 
												 attr, &credentials,
												 NULL, &create_resp);
		  if(pvfs_error != 0)
		  {
				return PVFS_get_errno_mapping(pvfs_error);
		  }

		  test_end = time(NULL);
		  timediff = test_end - test_start;

		  printf("%d %d\n", (rank * opt_nfiles) + i, (int)timediff);

		  pvfs_error = PVFS_sys_remove(test_file, lookup_resp.ref, &credentials);
		  if(pvfs_error != 0)
		  {
				return PVFS_get_errno_mapping(pvfs_error);
		  }
	 }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 


