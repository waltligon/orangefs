/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
    int strip_size;
    int num_datafiles;
    int buf_size;
    char* srcfile;
    char* destfile;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);
static double Wtime(void);

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[PVFS_NAME_MAX] = {0};
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_fs_id cur_fs;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_create resp_create;
    PVFS_sysresp_io resp_io;
    struct options* user_opts = NULL;
    int src_fd = -1;
    int current_size = 0;
    void* buffer = NULL;
    int64_t total_written = 0;
    double time1, time2;
    char* entry_name;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_object_ref ref;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    int buffer_size;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse "
                "command line arguments.\n");
	return(-1);
    }

    /* make sure we can access the source file before we go to any
     * trouble to contact pvfs servers
     */
    src_fd = open(user_opts->srcfile, O_RDONLY);
    if(src_fd < 0)
    {
	perror("open()");
	fprintf(stderr, "Error: could not access src file: %s\n",
	    user_opts->srcfile);
	return(-1);
    }

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->destfile,
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
    attr.dfile_count = user_opts->num_datafiles;
    if(attr.dfile_count > 0)
	attr.mask |= PVFS_ATTR_SYS_DFILE_COUNT;

    if (strcmp(pvfs_path,"/") == 0)
    {
        char *segp = NULL, *prev_segp = NULL;
        void *segstate = NULL;

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(cur_fs, pvfs_path,
                              &credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            ret = -1;
            goto main_out;
        }
        parent_ref.handle = resp_lookup.ref.handle;
        parent_ref.fs_id = resp_lookup.ref.fs_id;

        while(!PINT_string_next_segment(
                  user_opts->srcfile, &segp, &segstate))
        {
            prev_segp = segp;
        }
        entry_name = prev_segp;
    }
    else
    {
        /* get the absolute path on the pvfs2 file system */
        if (PINT_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
        {
            if (pvfs_path[0] != '/')
            {
                fprintf(stderr, "Error: poorly formatted path.\n");
            }
            fprintf(stderr, "Error: cannot retrieve entry name for "
                    "creation on %s\n",
                    pvfs_path);
            ret = -1;
            goto main_out;
        }

        ret = PINT_lookup_parent(pvfs_path, cur_fs, &credentials, 
                                 &parent_ref.handle);
        if (ret < 0)
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
                          user_opts->srcfile, &segp, &segstate))
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
                              PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret == 0)
    {
        fprintf(stderr, "Target file %s already exists!\n", entry_name);
        ret = -1;
        goto main_out;
    }

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));
    ret = PVFS_sys_create(entry_name, parent_ref, attr,
                          &credentials, NULL, &resp_create);
    if (ret < 0)
    {
	PVFS_perror("PVFS_sys_create", ret);
	ret = -1;
	goto main_out;
    }

    /* start moving data */
    buffer = malloc(user_opts->buf_size);
    if(!buffer)
    {
	perror("malloc");
	ret = -1;
	goto main_out;
    }

    ref = resp_create.ref;

    time1 = Wtime();
    while((current_size = read(src_fd, buffer, user_opts->buf_size)) > 0)
    {
	buffer_size = current_size;
	file_req = PVFS_BYTE;

	/* setup memory datatype */
	ret = PVFS_Request_contiguous(current_size, PVFS_BYTE, &mem_req);
	if(ret < 0)
	{
	    PVFS_perror("PVFS_Request_contiguous", ret);
	    ret = -1;
	    goto main_out;
	}

	/* write out the data */
	ret = PVFS_sys_write(ref, file_req,
                             total_written, buffer, mem_req, 
                             &credentials, &resp_io);
	if(ret < 0)
	{
	    PVFS_perror("PVFS_sys_write", ret);
	    ret = -1;
	    goto main_out;
	}

	/* sanity check */
	if(current_size != resp_io.total_completed)
	{
	    fprintf(stderr, "Error: short write!\n");
	    fprintf(stderr, "Tried to write %d bytes at offset %d.\n", 
		(int)current_size, (int)total_written);
	    fprintf(stderr, "Only got %d bytes.\n",
                    (int)resp_io.total_completed);
	    ret = -1;
	    goto main_out;
	}

	total_written += current_size;

	/* TODO: need to free the request descriptions */
    };
    time2 = Wtime();

    if(current_size < 0)
    {
	perror("read()");
	ret = -1;
	goto main_out;
    }

    /* print some statistics */
    printf("PVFS2 Import Statistics:\n");
    printf("********************************************************\n");
    printf("Destination path (local): %s\n", user_opts->destfile);
    printf("Destination path (PVFS2 file system): %s\n", pvfs_path);
    printf("Destination PVFS2 file system identifier: %d\n", (int)cur_fs);
    printf("********************************************************\n");
    printf("Bytes written: %Ld\n", Ld(total_written));
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n",
	(((double)total_written)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n");

    ret = 0;

main_out:

    PVFS_sys_finalize();

    if(src_fd > 0)
	close(src_fd);
    if(buffer)
	free(buffer);

    return(ret);
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "vs:n:b:";
    int one_opt = 0;

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* fill in defaults (except for hostid) */
    tmp_opts->strip_size = -1;
    tmp_opts->num_datafiles = -1;
    tmp_opts->buf_size = 10*1024*1024;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('s'):
		fprintf(stderr, "Error: strip size option not supported.\n");
		free(tmp_opts);
		return(NULL);
		ret = sscanf(optarg, "%d", &tmp_opts->strip_size);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('n'):
		ret = sscanf(optarg, "%d", &tmp_opts->num_datafiles);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('b'):
		ret = sscanf(optarg, "%d", &tmp_opts->buf_size);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(optind != (argc - 2))
    {
	usage(argc, argv);
	exit(EXIT_FAILURE);
    }

    /* TODO: should probably malloc and copy instead */
    tmp_opts->srcfile = argv[argc-2];
    tmp_opts->destfile = argv[argc-1];

    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, 
	"Usage: %s [-s strip_size] [-n num_datafiles] [-b buffer_size]\n",
	argv[0]);
    fprintf(stderr, "   unix_source_file pvfs2_dest_file\n");
    return;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

