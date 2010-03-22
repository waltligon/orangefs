/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

#ifndef PVFS_REQ_LIMIT_PINT_REQUEST_NUM
#define PVFS_REQ_LIMIT_PINT_REQUEST_NUM 100
#endif

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#ifndef FNAME
#define FNAME "/mnt/pvfs2/testfile"
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef Ld
#define Ld(x) (x)
#endif

#ifndef MAX_REGIONS
#define MAX_REGIONS 2
#endif

#ifndef MAX_REGIONS
#define MAX_REGIONS 2
#endif

#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE 32768
#endif

static int max_regions = MAX_REGIONS;

static void usage(int argc, char** argv);
static double Wtime(void);
typedef int32_t cm_count_t;
typedef int64_t cm_offset_t;
typedef int32_t cm_size_t;

typedef struct {
    cm_count_t	 cf_valid_count;
    cm_offset_t *cf_valid_start;
    cm_size_t   *cf_valid_size;
    void	*wr_buffer;
    void	*rd_buffer;
    int		 buf_size;
} cm_frame_t;

static int fillup_buffer(cm_frame_t *frame)
{
    int c, size = sizeof(double), should_be = 0, chunk_size = 0;

    //srand(time(NULL));
    if (frame->buf_size < 0 || frame->buf_size % size != 0)
    {
	fprintf(stderr, "buffer size [%d] must be a multiple of %d\n",
		frame->buf_size, size);
	return -1;
    }
    frame->wr_buffer = (char *) calloc(frame->buf_size, sizeof(char));
    assert(frame->wr_buffer);
    frame->rd_buffer = (char *) calloc(frame->buf_size, sizeof(char));
    assert(frame->rd_buffer);

    for (c = 0; c < frame->buf_size / size; c++)
    {
	*((int *) frame->wr_buffer + c) = c;
    }
    //frame->cf_valid_count = (rand() % max_regions) + 1;
    frame->cf_valid_count = max_regions;
    frame->cf_valid_start = (cm_offset_t *) calloc(sizeof(cm_offset_t), frame->cf_valid_count);
    frame->cf_valid_size = (cm_size_t *) calloc(sizeof(cm_size_t), frame->cf_valid_count);
    assert(frame->cf_valid_start && frame->cf_valid_size);
    chunk_size = frame->buf_size / frame->cf_valid_count;

    printf("Buffer size is %d bytes\n", frame->buf_size);
    printf("Generating %d valid regions\n", frame->cf_valid_count);
    for (c = 0; c < frame->cf_valid_count; c++)
    {
	int tmp_start;

	tmp_start = rand() % chunk_size;
	frame->cf_valid_start[c] = c * chunk_size + tmp_start;
	frame->cf_valid_size[c] = (rand() % (chunk_size - tmp_start)) + 1;
	assert(frame->cf_valid_start[c] + frame->cf_valid_size[c] <= frame->buf_size);
	
	printf("(%d): valid_start: %lld, valid_size: %d\n", c, lld(frame->cf_valid_start[c]),
		frame->cf_valid_size[c]);
	
	should_be += frame->cf_valid_size[c];
    }
    printf("PVFS_sys_write should write %d bytes\n", should_be);
    return should_be;
}

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[PVFS_NAME_MAX] = {0}, *fname = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_fs_id cur_fs;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_create resp_create;
    PVFS_sysresp_io resp_io;
    PVFS_sysresp_getattr resp_getattr;
    int64_t total_written = 0, total_read = 0;
    double time1, time2;
    char* entry_name;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_object_ref ref;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    int c, count = 0, should_be = 0;
    cm_frame_t    frame;

    frame.cf_valid_count = 0;
    frame.cf_valid_start = NULL;
    frame.cf_valid_size = NULL;
    frame.wr_buffer   = NULL;
    frame.rd_buffer = NULL;
    frame.buf_size = 32768;

    while ((c = getopt(argc, argv, "o:r:b:f:h")) != EOF)
    {
	switch (c)
	{
	    case 'r':
		max_regions = atoi(optarg);
		break;
	    case 'f':
		fname = optarg;
		break;
	    case 'b':
		frame.buf_size = atoi(optarg);
		break;
	    case 'h':
	    default:
		usage(argc, argv);
		exit(1);
	}
    }

    if (fname == NULL)
    {
	fname = FNAME;
    }
    if ((should_be = fillup_buffer(&frame)) < 0)
    {
	usage(argc, argv);
	exit(1);
    }

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(fname,
	&cur_fs, pvfs_path, PVFS_NAME_MAX);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_resolve", ret);
	ret = -1;
	goto main_out;
    }
    PVFS_util_gen_credentials(&credentials);

    entry_name = str_buf;
    attr.owner = credentials.uid; 
    attr.group = credentials.gid;
    attr.perms = PVFS_U_WRITE|PVFS_U_READ;
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);
    attr.dfile_count = -1;

    if (strcmp(pvfs_path,"/") == 0)
    {
        char *segp = NULL, *prev_segp = NULL;
        void *segstate = NULL;

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(cur_fs, pvfs_path,
                              &credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            ret = -1;
            goto main_out;
        }
        parent_ref.handle = resp_lookup.ref.handle;
        parent_ref.fs_id = resp_lookup.ref.fs_id;

        while(!PINT_string_next_segment(
                  fname, &segp, &segstate))
        {
            prev_segp = segp;
        }
        entry_name = prev_segp;
    }
    else
    {
        /* get the absolute path on the pvfs2 file system */
        if (PINT_remove_base_dir(pvfs_path, str_buf, PVFS_NAME_MAX))
        {
            if (pvfs_path[0] != '/')
            {
                fprintf(stderr, "Error: poorly formatted path.\n");
            }
            fprintf(stderr, "Error: cannot retrieve entry name for "
                    "creation on %s\n", pvfs_path);
            ret = -1;
            goto main_out;
        }

        ret = PINT_lookup_parent(pvfs_path, cur_fs, &credentials, &parent_ref.handle);
	
        if(ret < 0)
        {
            PVFS_perror("PVFS_util_lookup_parent", ret);
            ret = -1;
            goto main_out;
        }
        else
        {
            int len = strlen(pvfs_path);
            if (pvfs_path[len - 1] == '/')
            {
                char *segp = NULL, *prev_segp = NULL;
                void *segstate = NULL;

                while(!PINT_string_next_segment(
                          fname, &segp, &segstate))
                {
                    prev_segp = segp;
                }
                strncat(pvfs_path, prev_segp, PVFS_NAME_MAX);
                entry_name = prev_segp;
            }
            parent_ref.fs_id = cur_fs;
        }
    }

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_ref_lookup(parent_ref.fs_id, entry_name,
                              parent_ref, &credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
    if (ret == 0)
    {
         fprintf(stderr, "Target file %s already exists! Deleting.\n", entry_name);
			if (PVFS_sys_remove(entry_name,
					 parent_ref, &credentials, NULL) < 0)
			{
				 fprintf(stderr, "Could not unlink?\n");
			}
			else
			{
				 fprintf(stdout, "Unlinked successfully!\n");
			}
			ret = 0;
    }

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));
    ret = PVFS_sys_create(entry_name, parent_ref, attr,
                          &credentials, NULL, &resp_create, NULL, NULL);
    if (ret < 0)
    {
			PVFS_perror("PVFS_sys_create", ret);
			ret = -1;
			goto main_out;
    }

    ref = resp_create.ref;
    c = frame.cf_valid_count;
    count = 0;
    total_written = 0;
    time1 = Wtime();
    while (c > 0)
    {
			int tmp_valid_count;
			
			tmp_valid_count = MIN(c, 64);
			ret = PVFS_Request_hindexed(
					tmp_valid_count, 
					&frame.cf_valid_size[count],
					&frame.cf_valid_start[count], PVFS_BYTE, &mem_req);
			if(ret < 0)
			{
				 PVFS_perror("PVFS_Request_hindexed", ret);
				 ret = -1;
				 goto main_out;
			}
			ret = PVFS_Request_hindexed(
					tmp_valid_count, 
					&frame.cf_valid_size[count], 
					&frame.cf_valid_start[count], PVFS_BYTE, &file_req);
			if(ret < 0)
			{
				 PVFS_perror("PVFS_Request_hindexed", ret);
				 ret = -1;
				 goto main_out;
			}
			/* write out the data */
			ret = PVFS_sys_write(ref, file_req,
											  0, frame.wr_buffer, mem_req, 
											  &credentials, &resp_io, NULL);
			if(ret < 0)
			{
				 PVFS_perror("PVFS_sys_write", ret);
				 ret = -1;
				 goto main_out;
			}
			total_written += resp_io.total_completed;

			c -= tmp_valid_count;
			count += tmp_valid_count;
			PVFS_Request_free(&mem_req);
			PVFS_Request_free(&file_req);
    }
    time2 = Wtime();
    /* sanity check */
    if(should_be != total_written)
    {
			fprintf(stderr, "Error: short write!\n");
			fprintf(stderr, "Tried to write %d bytes\n", 
				 should_be);
			fprintf(stderr, "Only got %lld bytes.\n",
				lld(total_written));
			ret = -1;
			goto main_out;
    }

    /* print some statistics */
    printf("********************************************************\n");
    printf("PVFS2 Hindexed Test Write Statistics:\n");
    printf("********************************************************\n");
    printf("Bytes written: %lld\n", lld(total_written));
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n", (((double)total_written)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n\n");

    ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL,
	    &credentials, &resp_getattr, NULL);
    if (ret < 0)
    {
			PVFS_perror("Getattr failed", ret);
			ret = -1;
			goto main_out;
    }
    printf("Measured file size is %lld\n", lld(resp_getattr.attr.size));

    /* now read it back from the file and make sure we have the correct data */
    time1 = Wtime();
	 c = frame.cf_valid_count;
    count = 0;
    total_read = 0;
    time1 = Wtime();
    while (c > 0)
    {
			int tmp_valid_count;
			
			tmp_valid_count = MIN(c, 64);
			ret = PVFS_Request_hindexed(
					tmp_valid_count, 
					&frame.cf_valid_size[count],
					&frame.cf_valid_start[count], PVFS_BYTE, &mem_req);
			if(ret < 0)
			{
				 PVFS_perror("PVFS_Request_hindexed", ret);
				 ret = -1;
				 goto main_out;
			}
			ret = PVFS_Request_hindexed(
					tmp_valid_count, 
					&frame.cf_valid_size[count], 
					&frame.cf_valid_start[count], PVFS_BYTE, &file_req);
			if(ret < 0)
			{
				 PVFS_perror("PVFS_Request_hindexed", ret);
				 ret = -1;
				 goto main_out;
			}
		   /* read back the data */
			ret = PVFS_sys_read(ref, file_req,
											  0, frame.rd_buffer, mem_req, 
											  &credentials, &resp_io, NULL);
			if(ret < 0)
			{
				 PVFS_perror("PVFS_sys_read", ret);
				 ret = -1;
				 goto main_out;
			}
			total_read += resp_io.total_completed;

			c -= tmp_valid_count;
			count += tmp_valid_count;
			PVFS_Request_free(&mem_req);
			PVFS_Request_free(&file_req);
    }
    time2 = Wtime();

    /* sanity check */
    if(total_written != total_read)
    {
			fprintf(stderr, "Error: short reads!\n");
			fprintf(stderr, "Tried to read %lld bytes\n", 
				lld(total_written));
			fprintf(stderr, "Only got %lld bytes.\n",
				lld(total_read));
			ret = -1;
			goto main_out;
    }

    /* print some statistics */
    printf("\n********************************************************\n");
    printf("PVFS2 Hindexed Test Read Statistics:\n");
    printf("********************************************************\n");
    printf("Bytes read: %lld\n", lld(total_read));
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n",
	(((double)total_read)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n");


    ret = 0;
    /* now compare the relevant portions */
    for (c = 0; c < frame.cf_valid_count; c++)
    {
			if (memcmp((char *)frame.rd_buffer + frame.cf_valid_start[c],
					 (char *) frame.wr_buffer + frame.cf_valid_start[c],
					 frame.cf_valid_size[c]))
			{
				 fprintf(stderr, "(%d) -> Read buffer did not match with write buffer from [%lld upto %d bytes]\n",
					 c, lld(frame.cf_valid_start[c]), frame.cf_valid_size[c]);
				 ret = -1;
			}
    }
    if (ret == 0)
    {
		fprintf(stdout, "Test passed!\n");
    }
    else
    {
		fprintf(stdout, "Test failed!\n");
    }

main_out:

    PVFS_sys_finalize();

    if(frame.rd_buffer)
	free(frame.rd_buffer);
    if(frame.wr_buffer)
	free(frame.wr_buffer);
    if(frame.cf_valid_start)
	free(frame.cf_valid_start);
    if(frame.cf_valid_size)
	free(frame.cf_valid_size);

    return(ret);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "Usage: %s -f [filename]\n", argv[0]);
    fprintf(stderr, "	       -b [max. buffer size] default %d bytes.\n", MAX_BUF_SIZE);
    fprintf(stderr, "	       -r [max. valid regions] default %d regions\n", MAX_REGIONS);
    return;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

