/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* based off of io-test.c.  This is meant to exercise a bug found by Florin
 * Isaila in which two concurrent threads running sys_io() will deadlock on a
 * configuration struct lock ordering problem.
 */

#include <pthread.h>
#include <time.h>
#include "client.h"
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "pvfs2-util.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"

/* NOTE: this is not in bytes, but rather a number of ints to read and write */
#define DEFAULT_IO_SIZE 8*1024*1024

struct thread_info
{
    PVFS_object_ref* pinode_refn;
    PVFS_Request* file_req;
    PVFS_Request* mem_req;
    PVFS_credentials* credentials;
};

void* thread_fn(void* foo);
pthread_mutex_t error_count_mutex = PTHREAD_MUTEX_INITIALIZER;
int error_count = 0;

int main(int argc, char **argv)
{
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    PVFS_sysresp_io resp_io;
    char *filename = NULL;
    int ret = -1, io_size = DEFAULT_IO_SIZE;
    int *io_buffer = NULL;
    int i, buffer_size;
    PVFS_fs_id fs_id;
    char name[512] = {0};
    char *entry_name = NULL;
    PVFS_credentials credentials;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_object_ref pinode_refn;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    void *buffer = NULL;
    struct thread_info info;
    pthread_t* thread_id_array;
    int num_threads = 1;

    if (argc != 3)
    {
	fprintf(stderr, "Usage: %s <num threads> <file name>\n", argv[0]);
	return (-1);
    }

    if(sscanf(argv[1], "%d", &num_threads) != 1)
    {
	fprintf(stderr, "Usage: %s <num threads> <file name>\n", argv[0]);
	return (-1);
    }

    thread_id_array = malloc(num_threads* sizeof(pthread_t));
    if(!thread_id_array)
    {
        perror("malloc");
        return(-1);
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

    if (argv[2][0] == '/')
    {
        snprintf(name, 512, "%s", argv[2]);
    }
    else
    {
        snprintf(name, 512, "/%s", argv[2]);
    }

    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_lookup(fs_id, name, &credentials,
			  &resp_lk, PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret == -PVFS_ENOENT)
    {
        PVFS_sysresp_getparent gp_resp;

        memset(&gp_resp, 0, sizeof(PVFS_sysresp_getparent));
	ret = PVFS_sys_getparent(fs_id, name, &credentials, &gp_resp, NULL);
	if (ret < 0)
	{
            PVFS_perror("PVFS_sys_getparent failed", ret);
	    return ret;
	}

	attr.owner = credentials.uid;
	attr.group = credentials.gid;
	attr.perms = PVFS_U_WRITE | PVFS_U_READ;
	attr.atime = attr.ctime = attr.mtime = time(NULL);
	attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
	parent_refn = gp_resp.parent_ref;

        entry_name = rindex(name, (int)'/');
        assert(entry_name);
        entry_name++;
        assert(entry_name);

	ret = PVFS_sys_create(entry_name, parent_refn, attr,
			      &credentials, NULL, &resp_cr, NULL, NULL);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_sys_create() failure", ret);
	    return (-1);
	}

	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_cr.ref.handle;
    }
    else
    {
	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_lk.ref.handle;
    }

	/**************************************************************
	 * carry out I/O operation
	 */

    buffer = io_buffer;
    buffer_size = io_size * sizeof(int);

    /*
      file datatype is tiled, so we can get away with a trivial type
      here
    */
    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(io_size * sizeof(int),
                                  PVFS_BYTE, &(mem_req));
    if (ret < 0)
    {
        PVFS_perror("PVFS_request_contiguous failure", ret);
	return (-1);
    }

    ret = PVFS_sys_write(pinode_refn, file_req, 0, buffer, mem_req,
			 &credentials, &resp_io, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_write failure", ret);
	return (-1);
    }

    /* fill in information for threads */
    info.pinode_refn = &pinode_refn;
    info.file_req = &file_req;
    info.mem_req = &mem_req;
    info.credentials = &credentials;

    /* launch threads then wait for them to finish */
    for(i=0; i<num_threads; i++)
    {
        ret = pthread_create(&thread_id_array[i], NULL, thread_fn, &info);
        assert(ret == 0);
    }

    for(i=0; i<num_threads; i++)
    {
        pthread_join(thread_id_array[i], NULL);
    }

	/**************************************************************
	 * shut down pending interfaces
	 */

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	fprintf(stderr, "Error: PVFS_sys_finalize() failed with errcode = %d\n",
		ret);
	return (-1);
    }

    free(filename);
    free(io_buffer);

    pthread_mutex_lock(&error_count_mutex);
    if(error_count != 0)
    {
        fprintf(stderr, "Error: %d threads had problems\n", error_count);
        ret = -1;
    }
    else
    {
        ret = 0;
    }
    pthread_mutex_unlock(&error_count_mutex);

    free(thread_id_array);

    return (ret);
}

void* thread_fn(void* foo)
{
    int io_size = DEFAULT_IO_SIZE;
    int *io_buffer = NULL;
    PVFS_sysresp_io resp_io;
    int ret = 0;
    int errors = 0;
    int i = 0;
    struct thread_info* info = foo;

    /* create a buffer for running I/O on */
    io_buffer = (int *) malloc(io_size * sizeof(int));
    if (!io_buffer)
    {
        perror("malloc");
        pthread_mutex_lock(&error_count_mutex);
        error_count++;
        pthread_mutex_unlock(&error_count_mutex);
	return (NULL);
    }

    memset(io_buffer, 0, io_size * sizeof(int));

    /* verify */
    ret = PVFS_sys_read(*info->pinode_refn, *info->file_req, 0, io_buffer, *info->mem_req,
			info->credentials, &resp_io, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_read failure", ret);
        pthread_mutex_lock(&error_count_mutex);
        error_count++;
        pthread_mutex_unlock(&error_count_mutex);
	return (NULL);
    }
    if ((io_size * sizeof(int)) != resp_io.total_completed)
    {
	fprintf(stderr, "Error: SHORT READ!\n");
        pthread_mutex_lock(&error_count_mutex);
        error_count++;
        pthread_mutex_unlock(&error_count_mutex);
        return(NULL);
    }
    else
    {
	errors = 0;
	for (i = 0; i < io_size; i++)
	{
	    if (i != io_buffer[i])
	    {
		fprintf(stderr,
			"error: element %d differs: should be %d, is %d\n", i,
			i, io_buffer[i]);
		errors++;
	    }
	}
	if (errors != 0)
	{
	    fprintf(stderr, "ERROR: found %d errors\n", errors);
            pthread_mutex_lock(&error_count_mutex);
            error_count++;
            pthread_mutex_unlock(&error_count_mutex);
            return(NULL);
	}
    }

    free(io_buffer);

    return(NULL);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
