/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include "pvfs2-sysint.h"
#include "helper.h"
#include "simple-stripe.h"

#define DEFAULT_TAB "/etc/pvfs2tab"

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
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_create resp_create;
    PVFS_sysresp_io resp_io;
    struct options* user_opts = NULL;
    int i = 0;
    int mnt_index = -1;
    int src_fd = -1;
    int current_size = 0;
    void* buffer = NULL;
    int64_t total_written = 0;
    double time1, time2;
    int32_t blocklength = 0;
    PVFS_size displacement = 0;
    char* entry_name;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_pinode_reference pinode_refn;
    PVFS_Request io_req;
    int buffer_size;

    gossip_enable_stderr();

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
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

    /* look at pvfstab */
    if(parse_pvfstab(DEFAULT_TAB, &mnt))
    {
        fprintf(stderr, "Error: failed to parse pvfstab %s.\n", DEFAULT_TAB);
	close(src_fd);
        return(-1);
    }

    /* see if the destination resides on any of the file systems
     * listed in the pvfstab; find the pvfs fs relative path
     */
    for(i=0; i<mnt.nr_entry; i++)
    {
	ret = PINT_remove_dir_prefix(user_opts->destfile,
	    mnt.ptab_p[i].local_mnt_dir, pvfs_path, PVFS_NAME_MAX);
	if(ret == 0)
	{
	    mnt_index = i;
	    break;
	}
    }

    if(mnt_index == -1)
    {
	fprintf(stderr, "Error: could not find filesystem for %s in pvfstab %s\n", 
	    user_opts->destfile, DEFAULT_TAB);
	close(src_fd);
	return(-1);

    }

    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(mnt,&resp_init);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	close(src_fd);
	return(-1);
    }

    /* get the absolute path on the pvfs2 file system */
    if (PINT_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
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

    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));

    cur_fs = resp_init.fsid_list[mnt_index];

    entry_name = str_buf;
    attr.owner = getuid(); 
    attr.group = getgid();
    attr.perms = PVFS_U_WRITE|PVFS_U_READ;
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);
    credentials.uid = getuid();
    credentials.gid = getgid();
    parent_refn.handle =
        lookup_parent_handle(pvfs_path,cur_fs);
    parent_refn.fs_id = cur_fs;

    ret = PVFS_sys_create(entry_name, parent_refn, attr, credentials,
			    &resp_create);
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
	PVFS_sys_finalize();
	ret = -1;
	goto main_out;
    }

    pinode_refn = resp_create.pinode_refn;

    time1 = Wtime();
    while((current_size = read(src_fd, buffer, user_opts->buf_size)) > 0)
    {
	/* setup I/O description */
	buffer_size = current_size;
	blocklength = current_size;
	displacement = total_written;
	ret = PVFS_Request_indexed(1, &blocklength,
	    &displacement, PVFS_BYTE, &io_req);
	if(ret < 0)
	{
	    fprintf(stderr, "Error: PVFS_Request_indexed failure.\n");
	    ret = -1;
	    goto main_out;
	}

	/* write out the data */
	ret = PVFS_sys_write(pinode_refn, io_req, 0, buffer, buffer_size, 
			    credentials, &resp_io);
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
	    fprintf(stderr, "Only got %d bytes.\n", (int)resp_io.total_completed);
	    ret = -1;
	    goto main_out;
	}

	total_written += current_size;

	/* TODO: need to free the request description */
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
    printf("File system name: %s\n", mnt.ptab_p[mnt_index].service_name);
    printf("Initial config server: %s\n", mnt.ptab_p[mnt_index].meta_addr);
    printf("********************************************************\n");
    printf("Bytes written: %ld\n", (long)total_written);
    printf("Elapsed time: %f seconds\n", (time2-time1));
    printf("Bandwidth: %f MB/second\n",
	(((double)total_written)/((double)(1024*1024))/(time2-time1)));
    printf("********************************************************\n");

    ret = 0;

main_out:

    PVFS_sys_finalize();

    gossip_disable();

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
    char flags[] = "s:n:b:";
    char one_opt = ' ';

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
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt){
	    case('s'):
		gossip_lerr("Error: strip size option not supported.\n");
		free(tmp_opts);
		return(NULL);
		ret = sscanf(optarg, "%d", &tmp_opts->strip_size);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('n'):
		gossip_lerr("Error: num datafiles option not supported.\n");
		free(tmp_opts);
		return(NULL);
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
    fprintf(stderr, "\n      Note: this utility reads /etc/pvfs2tab for file system configuration.\n");
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

