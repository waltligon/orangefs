/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include "client.h"
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include "pvfs2-util.h"
#include "pvfs2-mgmt.h"
#include "mpi.h"
#include <mpi.h>

#define MAX_OL_PAIRS 64

#define MULTIPLE_LOCK 0
#define LIST_LOCK     1
#define DATATYPE_LOCK 2
#define MAX_LOCK      3

char *lock_name[MAX_LOCK] = 
{
    "multiple",
    "list    ",
    "datatype"
};

typedef struct
{
    int lock_method;
    int dfile_count;
    int filename_sz;
} test_params_t;

int main(
    int argc,
    char **argv)
{
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    PVFS_sysresp_lock resp_lock;
    char *filename;
    int rounds = -1, check = -1, ret = -1, i, j, myid, numprocs;
    PVFS_fs_id fs_id;
    char *name;
    PVFS_credentials credentials;
    char *entry_name;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_object_ref pinode_refn;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    PVFS_offset file_req_offset = 0; 
    struct timeval tp;
    struct timezone tzp;
    double begin_time, total_time, final_time;
    int total_locks = 0;
    PVFS_size disp_arr[MAX_OL_PAIRS];
    int32_t blk_arr[MAX_OL_PAIRS];
    /* list lock and multiple lock will need array pointers */
    PVFS_id_gen_t **lock_id_matrix = NULL;
    int *lock_id_count_arr = NULL;
    /* datatype lock does not*/
    PVFS_id_gen_t *lock_id_arr = NULL;
    int lock_id_arr_count = 0;
    test_params_t test_params;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    char *test_file = "/mnt/pvfs2/testfile";

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }

    /* First process takes care of the arguments and sends them along */
    if (myid == 0)
    {
	if (argc != 4)
	{
	    fprintf(stderr, "Usage: %s <dfile count> <method> <file name>\n", 
		    argv[0]);
	    fprintf(stderr, "methods: 0 = multiple, 1 = list lock, 2 = datatype lock\n");
	    return (-1);
	}
	
	if (atoi(argv[1]) < 1)
	{
	    fprintf(stderr, "Error: dfile_count must be greater than 0\n");
	    return -1;
	}
	test_params.dfile_count = atoi(argv[1]);
	
	if (atoi(argv[2]) < 0 || atoi(argv[2]) > 2)
	{
	    fprintf(stderr, "Error: lock method must be 0, 1, or 2\n");
	    return -1;
	}
	test_params.lock_method = atoi(argv[2]);
	
	if (index(argv[3], '/'))
	{
	    fprintf(stderr, "Error: please use simple file names, no path.\n");
	    return (-1);
	}
	/* build the full path name (work out of the root dir for this test) */

	/* include null terminator and slash */
	test_params.filename_sz = strlen(argv[3]) + 2;	
	if ((filename = calloc(1, test_params.filename_sz + 3)) == NULL)
	{
	    fprintf(stderr, "calloc filename failed");
	    return (-1);
	}
	filename[0] = '/';
	memcpy(&(filename[1]), argv[3], (test_params.filename_sz - 1));
	
    }	
    MPI_Bcast(&test_params, sizeof(test_params_t), MPI_BYTE, 0, 
	      MPI_COMM_WORLD);
    if (myid != 0)
    {
	/* Add some digits for the name */
	if ((filename = calloc(1, test_params.filename_sz + 3)) == NULL)
        {
            fprintf(stderr, "malloc filename failed");
            return (-1);
        }
        filename[0] = '/';
    }
    /* Pass around the file name */
    MPI_Bcast(filename, test_params.filename_sz, MPI_CHAR, 0, MPI_COMM_WORLD);
    if (myid != 0)
    {
	sprintf(&filename[test_params.filename_sz - 1], "%d", myid);
    }

#if 0	
    printf("id=%d, filename=%s\n", myid, filename);
    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
    }
#else
    
    ret = PVFS_util_resolve(test_file, &fs_id,
			    pvfs_path, PVFS_NAME_MAX);
    if (ret < 0)
    {
	printf("PVFS_util_resolve returned %d\n", ret);
	return -1;
    }
#endif	
    /*************************************************************
     * try to look up the target file 
     */
    
    name = filename;
    
    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_lookup(fs_id, name, &credentials,
			  &resp_lk, PVFS2_LOOKUP_LINK_FOLLOW);
    /* TODO: really we probably want to look for a specific error code,
     * like maybe ENOENT?
     */
    if (ret < 0)
    {
#if 0
	printf("TEST-LOCK: lookup failed; creating new file.\n");
#endif	

	/* get root handle */
	name = "/";
	ret = PVFS_sys_lookup(fs_id, name, &credentials,
			      &resp_lk, PVFS2_LOOKUP_LINK_NO_FOLLOW);
	if (ret < 0)
	{
	    fprintf(stderr,
		    "Error: PVFS_sys_lookup() failed to find root handle.\n");
	    return (-1);
	}
	
	/* create new file */
	attr.owner = credentials.uid;
	attr.group = credentials.gid;
	attr.perms = PVFS_U_WRITE | PVFS_U_READ;
	attr.atime = attr.ctime = attr.mtime = time(NULL);
	attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
	attr.dfile_count = test_params.dfile_count;
	attr.mask |= PVFS_ATTR_SYS_DFILE_COUNT;
	parent_refn.handle = resp_lk.ref.handle;
	parent_refn.fs_id = fs_id;
	entry_name = &(filename[1]);	/* leave off slash */
	
	ret = PVFS_sys_create(entry_name, parent_refn, attr,
			      &credentials, NULL, &resp_cr);
	if (ret < 0)
	{
	    fprintf(stderr, "Error: PVFS_sys_create() failure.\n");
	    return (-1);
	}
	
	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_cr.ref.handle;
    }
    else
    {
	printf("TEST-LOCK: lookup succeeded; performing lock on "
	       "existing file.\n");
	
	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_lk.ref.handle;
    }
    
    /* Do locking */
#if 0    
    printf("TEST-LOCK%d: performing test on handle: %ld, fs: %d\n", myid,
	   (long) pinode_refn.handle, 
	   (int) pinode_refn.fs_id);
#endif
    /* Test 1 MByte of locks */
    total_locks = 64*1024;

    MPI_Barrier(MPI_COMM_WORLD);
    
    gettimeofday(&tp,&tzp);
    begin_time = ((double) tp.tv_sec + (double) tp.tv_usec * 1.e-6);

    switch(test_params.lock_method)
    {
	case MULTIPLE_LOCK:
	    lock_id_matrix = (PVFS_id_gen_t **) 
		malloc(total_locks * sizeof(PVFS_id_gen_t *));
	    if (lock_id_matrix == NULL)
	    {
		fprintf(stderr, "malloc lock_id_matrix failed\n");
		return -1;
	    }
	    lock_id_count_arr = (int *) malloc(total_locks * sizeof(int));
	    PVFS_Request_contiguous(1, PVFS_BYTE, &mem_req);
	    PVFS_Request_contiguous(1, PVFS_BYTE, &file_req);

	    for (i = 0; i < total_locks; i++)
	    {
		file_req_offset = i*2;
		
		ret = PVFS_sys_lock(pinode_refn, file_req, 
				    file_req_offset, 
				    mem_req, &credentials, 
				    &resp_lock, 
				    &(lock_id_matrix[i]), 
				    &(lock_id_count_arr[i]),
				    PVFS_IO_READ, PVFS_ACQUIRE);
		if (ret < 0)
		{
		    fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
		    return (-1);
		}
	    }
	    break;
	case LIST_LOCK:
	    rounds = total_locks / MAX_OL_PAIRS;
	    check = total_locks % MAX_OL_PAIRS;
	    if (check != 0)
	    {
		fprintf(stderr, "Error: list lock doesn't divide nicely\n");
		return -1;
	    }

	    lock_id_matrix = (PVFS_id_gen_t **) 
		malloc(rounds * sizeof(PVFS_id_gen_t *));
	    if (lock_id_matrix == NULL)
	    {
		fprintf(stderr, "malloc lock_id_matrix failed\n");
		return -1;
	    }
	    lock_id_count_arr = (int *) malloc(rounds * sizeof(int));

	    PVFS_Request_contiguous(MAX_OL_PAIRS, PVFS_BYTE, &mem_req);
	    for (i = 0; i < rounds; i++)
	    {
		for (j = 0; j < MAX_OL_PAIRS; j++)
		{
		    disp_arr[j] = (i * 2 * MAX_OL_PAIRS) + (j * 2);
		    blk_arr[j] = 1;
		}
	    
		PVFS_Request_hindexed(MAX_OL_PAIRS, blk_arr, disp_arr, 
				      PVFS_BYTE, &file_req);
		ret = PVFS_sys_lock(pinode_refn, file_req, 
				    file_req_offset, 
				    mem_req, &credentials, 
				    &resp_lock, 
				    &(lock_id_matrix[i]), 
				    &(lock_id_count_arr[i]),
				    PVFS_IO_READ, PVFS_ACQUIRE);
		if (ret < 0)
		{
		    fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
		    return (-1);
		}
		PVFS_Request_free(&file_req);
	    }
	    break;
	case DATATYPE_LOCK:
	    PVFS_Request_contiguous(total_locks, PVFS_BYTE, &mem_req);
	    PVFS_Request_vector(total_locks, 1, 2, PVFS_BYTE, &file_req);
	    
	    ret = PVFS_sys_lock(pinode_refn, file_req, 
				file_req_offset, 
				mem_req, &credentials, &resp_lock, 
				&lock_id_arr, &lock_id_arr_count,
				PVFS_IO_READ, PVFS_ACQUIRE);
	    if (ret < 0)
	    {
		fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
		return (-1);
	    }
	    break;
	default:
	    fprintf(stderr, "Error: Invalid lock method %d\n", test_params.lock_method);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    
    gettimeofday(&tp,&tzp);
    total_time = ((double) tp.tv_sec + (double) tp.tv_usec * 1.e-6) -
        begin_time;

#if 0    
    fprintf(stdout, "%s: locked %f locks/s (%d total locks, "
	    "%f s)\n", lock_name[test_params.lock_method],
            total_locks / total_time, total_locks, total_time);
#else
    MPI_Reduce(&total_time, &final_time, 1, MPI_DOUBLE,
	       MPI_MAX, 0, MPI_COMM_WORLD);
    if (myid == 0)
	fprintf(stdout, "%s | %f | locked locks/s "
		"| %d | total locks | %f | s\n", 
		lock_name[test_params.lock_method],
		numprocs * total_locks / total_time, numprocs * total_locks, 
		total_time);
#endif

    MPI_Barrier(MPI_COMM_WORLD);

    gettimeofday(&tp,&tzp);
    begin_time = ((double) tp.tv_sec + (double) tp.tv_usec * 1.e-6);
    
    /* Clean up locks */
    switch(test_params.lock_method)
    {
	case MULTIPLE_LOCK:
	    for (i = 0; i < total_locks; i++)
	    {
		file_req_offset = i*2;
		
		ret = PVFS_sys_lock(pinode_refn, file_req, 
				    file_req_offset, 
				    mem_req, &credentials, 
				    &resp_lock, 
				    &(lock_id_matrix[i]), 
				    &(lock_id_count_arr[i]),
				    PVFS_IO_READ, PVFS_RELEASE);
		if (ret < 0)
		{
		    fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
		    return (-1);
		}
	    }
	    free(lock_id_matrix);
	    free(lock_id_count_arr);
	    break;
	case LIST_LOCK:
	    for (i = 0; i < rounds; i++)
	    {
		for (j = 0; j < MAX_OL_PAIRS; j++)
		{
		    disp_arr[j] = (i * 2 * MAX_OL_PAIRS) + (j * 2);
		    blk_arr[j] = 1;
		}
		
		PVFS_Request_hindexed(MAX_OL_PAIRS, blk_arr, disp_arr, 
				      PVFS_BYTE, &file_req);
		ret = PVFS_sys_lock(pinode_refn, file_req,
				    file_req_offset, 
				    mem_req, &credentials, 
				    &resp_lock, 
				    &(lock_id_matrix[i]), 
				    &(lock_id_count_arr[i]),
				    PVFS_IO_READ, PVFS_RELEASE);
		if (ret < 0)
		{
		    fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
		    return (-1);
		}
		PVFS_Request_free(&file_req);
	    }
	    free(lock_id_matrix);
	    free(lock_id_count_arr);
	    break;
	case DATATYPE_LOCK:
	    ret = PVFS_sys_lock(pinode_refn, file_req,
				file_req_offset,
				mem_req, &credentials, &resp_lock,
				&lock_id_arr, &lock_id_arr_count,
				PVFS_IO_READ, PVFS_RELEASE);
	    if (ret < 0)
	    {
		fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
		return -1;
	    }
	    break;
	default:
	    fprintf(stderr, "Error: Invalid lock method %d\n", test_params.lock_method);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    gettimeofday(&tp,&tzp);
    total_time = ((double) tp.tv_sec + (double) tp.tv_usec * 1.e-6) - 
	begin_time;

#if 0    
    fprintf(stdout, "%s:  freed %f locks/s (%d total locks, "
	    "%f s)\n", lock_name[test_params.lock_method],
            total_locks / total_time, total_locks, total_time);
#else
    MPI_Reduce(&total_time, &final_time, 1, MPI_DOUBLE,
	       MPI_MAX, 0, MPI_COMM_WORLD);
    if (myid == 0)
	fprintf(stdout, "%s | %f |  freed locks/s "
		"| %d | total locks | %f | s\n", 
		lock_name[test_params.lock_method],
		numprocs * total_locks / total_time, numprocs * total_locks, 
		total_time);
#endif

    PVFS_Request_free(&mem_req);
    if (test_params.lock_method != LIST_LOCK)
	PVFS_Request_free(&file_req);

    MPI_Barrier(MPI_COMM_WORLD);
#if 1 /* Delete file afterward. */
    /* get root handle */
    name = "/";
    ret = PVFS_sys_lookup(fs_id, name, &credentials,
			  &resp_lk, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
	fprintf(stderr,
		"Error: PVFS_sys_lookup() failed to find root handle.\n");
	return (-1);
    }
    
    
    entry_name = &(filename[1]);	/* leave off slash */
    parent_refn.handle = resp_lk.ref.handle;
    parent_refn.fs_id = fs_id;
    ret = PVFS_sys_remove(entry_name, parent_refn, &credentials);
    if (ret < 0)
    {
	PVFS_perror("remove failed ", ret);
	return(-1);
    }
#endif

    ret = PVFS_sys_finalize();    
    free(filename);
    MPI_Finalize();
    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
