/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

#include "pvfs2.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
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
    const PVFS_util_tab* tab;
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_io resp_io;
    struct options* user_opts = NULL;
    int dest_fd = -1;
    int current_size = 0;
    void* buffer = NULL;
    int64_t total_written = 0;
    double time1, time2;
    PVFS_fs_id lk_fs_id;
    char* lk_name;
    PVFS_credentials credentials;
    PVFS_pinode_reference pinode_refn;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    int buffer_size;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	return(-1);
    }

    /* make sure we can access the dest file before we go to any
     * trouble to contact pvfs servers
     */
    dest_fd = open(user_opts->destfile, O_CREAT|O_WRONLY|O_TRUNC,
	0777);
    if(dest_fd < 0)
    {
	perror("open()");
	fprintf(stderr, "Error: could not access dest file: %s\n",
	    user_opts->destfile);
	return(-1);
    }

    /* look at pvfstab */
    tab = PVFS_util_parse_pvfstab(NULL);
    if(!tab)
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
	close(dest_fd);
        return(-1);
    }

    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(*tab, GOSSIP_NO_DEBUG, &resp_init);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	close(dest_fd);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->srcfile,
        &cur_fs, pvfs_path, PVFS_NAME_MAX);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_resolve", ret);
	ret = -1;
	goto main_out;
    }

    /* get the absolute path on the pvfs2 file system */
    if (PVFS_util_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
    {
        if (pvfs_path[0] != '/')
        {
            fprintf(stderr, "Error: poorly formatted path.\n");
        }
        fprintf(stderr, "Error: cannot retrieve entry name for creation on %s\n",
               pvfs_path);
	ret = -1;
	goto main_out;
    }

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));

    credentials.uid = getuid();
    credentials.gid = getgid();
    lk_fs_id = cur_fs;

    /* TODO: this is awkward- the remove_base_dir() function
     * doesn't leave an opening slash on the path (because it was
     * first written to help with sys_create() calls).  However,
     * in the sys_lookup() case, we need the opening slash.
     */
    lk_name = (char*)malloc(strlen(str_buf) + 2);
    if(!lk_name)
    {
	perror("malloc()");
	ret = -1;
	goto main_out;
    }
    lk_name[0] = '/';
    strcpy(&(lk_name[1]), str_buf);

    ret = PVFS_sys_lookup(lk_fs_id, lk_name, credentials,
                          &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_lookup", ret);
	ret = -1;
	goto main_out;
    }

    /* start moving data */
    buffer = malloc(user_opts->buf_size);
    if(!buffer)
    {
	PVFS_sys_finalize();
	ret = -1;
	goto main_out;
    }

    pinode_refn = resp_lookup.pinode_refn;
    buffer_size = user_opts->buf_size;

    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(buffer_size, PVFS_BYTE, &mem_req);
    if(ret < 0)
    {
	fprintf(stderr, "Error: PVFS_Request_indexed failure.\n");
	ret = -1;
	goto main_out;
    }

    time1 = Wtime();
    while((ret = PVFS_sys_read(pinode_refn, file_req, total_written,
                buffer, mem_req, credentials, &resp_io)) == 0 &&
	resp_io.total_completed > 0)
    {
	current_size = resp_io.total_completed;

	/* write out the data */
	ret = write(dest_fd, buffer, current_size);
	if(ret < 0)
	{
	    perror("write()");
	    ret = -1;
	    goto main_out;
	}

	total_written += current_size;
    };
    time2 = Wtime();

    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_read()", ret);
	ret = -1;
	goto main_out;
    }

    /* print some statistics */
    printf("PVFS2 Export Statistics:\n");
    printf("********************************************************\n");
    printf("Source path (local): %s\n", user_opts->srcfile);
    printf("Source path (PVFS2 file system): %s\n", pvfs_path);
    printf("Source PVFS2 file system identifier: %d\n", (int)cur_fs);
    printf("********************************************************\n");
    printf("Bytes written: %Ld\n", Ld(total_written));
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n",
	(((double)total_written)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n");

    ret = 0;

main_out:

    PVFS_sys_finalize();

    if(dest_fd > 0)
	close(dest_fd);
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
    char flags[] = "vb:";
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
    tmp_opts->buf_size = 10*1024*1024;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt){
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
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
	"Usage: %s [-b buffer_size] pvfs2_src_file unix_dest_file\n", 
	argv[0]); 
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

