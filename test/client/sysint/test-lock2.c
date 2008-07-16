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

int main(
    int argc,
    char **argv)
{
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    PVFS_sysresp_lock resp_lock;
    char *filename;
    int name_sz, total_locks;
    int ret = -1;
    PVFS_fs_id fs_id;
    char *name;
    PVFS_credentials credentials;
    char *entry_name;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_object_ref pinode_refn;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    PVFS_offset file_req_offset; 
    PVFS_size total_bytes = 0;
    struct timeval tp;
    struct timezone tzp;
    double begin_time, total_time;
    int dfile_count = -1;
    PVFS_id_gen_t *lock_id_arr = NULL;
    int lock_id_arr_count = 0;

    if (argc != 3)
    {
	fprintf(stderr, "Usage: %s <dfile count> <file name>\n", argv[0]);
	return (-1);
    }

    if (atoi(argv[1]) < 1)
    {
	fprintf(stderr, "Error: dfile_count must be greater than 0\n");
	return -1;
    }
    dfile_count = atoi(argv[1]);

    if (index(argv[2], '/'))
    {
	fprintf(stderr, "Error: please use simple file names, no path.\n");
	return (-1);
    }

    /* build the full path name (work out of the root dir for this test) */

    name_sz = strlen(argv[2]) + 2;	/* include null terminator and slash */
    filename = malloc(name_sz);
    if (!filename)
    {
	perror("malloc");
	return (-1);
    }
    filename[0] = '/';

    memcpy(&(filename[1]), argv[2], (name_sz - 1));

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }
    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
    }

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
	printf("TEST-LOCK: lookup failed; creating new file.\n");

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
	attr.dfile_count = dfile_count;
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
	printf("TEST-LOCK: lookup succeeded; performing lock on existing file.\n");

	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_lk.ref.handle;
    }
     
    /* Do locking */

    printf("TEST-LOCK: performing test on handle: %ld, fs: %d\n",
	   (long) pinode_refn.handle, (int) pinode_refn.fs_id);

    total_bytes = 64*1024*8;
    total_locks = total_bytes;
    PVFS_Request_contiguous(total_bytes, PVFS_BYTE, &mem_req);
    /* count, blklength, stride */
#if 0
    PVFS_Request_vector(total_bytes, 1, 2, PVFS_BYTE, &file_req);
#else

    PVFS_Request_contiguous(total_bytes, PVFS_BYTE, &file_req);
#endif
    file_req_offset = 0;

    gettimeofday(&tp,&tzp);
    begin_time = ((double) tp.tv_sec + (double) tp.tv_usec * 1.e-6);

    ret = PVFS_sys_lock(pinode_refn, file_req, file_req_offset, mem_req,
			&credentials, &resp_lock, &lock_id_arr, 
			&lock_id_arr_count, PVFS_IO_READ, PVFS_ACQUIRE);

    gettimeofday(&tp,&tzp);
    total_time = ((double) tp.tv_sec + (double) tp.tv_usec * 1.e-6) -
        begin_time;

    fprintf(stdout, "%f random locks per sec (%d total locks, %f seconds)\n",
	    total_locks / total_time, total_locks, total_time);

    fprintf(stdout, "Responded with lock_id=%Ld,bstream_size=%Ld"
	    ",granted_bytes=%Ld\n", resp_lock.lock_id, 
	    resp_lock.bstream_size, resp_lock.granted_bytes);

    if (ret < 0)
    {
        fprintf(stderr, "Error: PVFS_sys_lock() failure.\n");
        return (-1);
    }


    ret = PVFS_sys_finalize();
    
    free(filename);
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
