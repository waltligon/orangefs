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

#define DEFAULT_IO_SIZE 8*1024*1024

int main(
    int argc,
    char **argv)
{
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    PVFS_sysresp_io resp_io;
    char *filename;
    int name_sz;
    int ret = -1;
    int io_size = DEFAULT_IO_SIZE;
    int *io_buffer = NULL;
    int i;
    PVFS_fs_id fs_id;
    char *name;
    PVFS_credentials credentials;
    char *entry_name;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_object_ref pinode_refn;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    void *buffer;
    int buffer_size;
    int32_t len_array1[17];
    int32_t len_array2[17];
    PVFS_offset off_array1[17];
    PVFS_offset off_array2[17];

    if (argc != 2)
    {
	fprintf(stderr, "Usage: %s <file name>\n", argv[0]);
	return (-1);
    }

    if (index(argv[1], '/'))
    {
	fprintf(stderr, "Error: please use simple file names, no path.\n");
	return (-1);
    }

    /* create a buffer for running I/O on */
    io_buffer = (int *) malloc(io_size * sizeof(int));
    if (!io_buffer)
    {
	return (-1);
    }

    /* put some data in the buffer so we can verify */
    for (i = 0; i < io_size; i++)
    {
	io_buffer[i] = i;
    }

    /* build the full path name (work out of the root dir for this test) */

    name_sz = strlen(argv[1]) + 2;	/* include null terminator and slash */
    filename = malloc(name_sz);
    if (!filename)
    {
	perror("malloc");
	return (-1);
    }
    filename[0] = '/';

    memcpy(&(filename[1]), argv[1], (name_sz - 1));

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
	printf("IO-TEST: lookup failed; creating new file.\n");

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
	attr.dfile_count = 4;
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
	printf("IO-TEST: lookup succeeded; performing I/O on existing file.\n");

	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_lk.ref.handle;
    }

	/**************************************************************
	 * carry out I/O operation
	 */

    printf("IO-TEST: performing write on handle: %ld, fs: %d\n",
	   (long) pinode_refn.handle, (int) pinode_refn.fs_id);

    buffer = io_buffer;
    buffer_size = io_size * sizeof(int);

    off_array1[0] = 0;
    off_array1[0] += (unsigned int)io_buffer;
    off_array2[0] = 327552;
    for(i=0; i<17; i++)
    {
	off_array1[i] = off_array1[0] + i*8;
	off_array2[i] = off_array2[0] + i*8;
	len_array1[i] = 4;
	len_array2[i] = 4;
    }

/* this works fine with 16 entries */
 PVFS_Request_hindexed(17, len_array1,
                     off_array1, PVFS_BYTE, &mem_req);
 PVFS_Request_hindexed(17, len_array2,
                     off_array2, PVFS_BYTE, &file_req);

    ret = PVFS_sys_write(pinode_refn, file_req, 0, PVFS_BOTTOM, mem_req,
			 &credentials, &resp_io);
    if (ret < 0)
    {
	fprintf(stderr, "Error: PVFS_sys_write() failure.\n");
	return (-1);
    }

    printf("IO-TEST: wrote %d bytes.\n", (int) resp_io.total_completed);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	fprintf(stderr, "Error: PVFS_sys_finalize() failed with errcode = %d\n",
		ret);
	return (-1);
    }

    free(filename);
    free(io_buffer);
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
