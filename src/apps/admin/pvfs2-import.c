/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "pvfs2-sysint.h"
#include "helper.h"

#define DEFAULT_TAB "/etc/pvfs2tab"

/* optional parameters, filled in by parse_args() */
struct options
{
    int ssize;
    int num_datafiles;
    char* srcfile;
    char* destfile;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

int main(int argc, char **argv)
{
    int ret = -1;
    char str_buf[PVFS_NAME_MAX] = {0};
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    PVFS_sysreq_create req_create;
    PVFS_sysresp_create resp_create;
    struct options* user_opts = NULL;
    int i = 0;
    int mnt_index = -1;

    gossip_enable_stderr();

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	return(-1);
    }

    /* look at pvfstab */
    if (parse_pvfstab(DEFAULT_TAB, &mnt))
    {
        fprintf(stderr, "Error: failed to parse pvfstab %s.\n", DEFAULT_TAB);
        return ret;
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

    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(mnt,&resp_init);
    if(ret < 0)
    {
	if(PVFS_ERROR_CLASS(-ret))
	{
	    fprintf(stderr, "Error: PVFS_sys_initialize: %s.\n", 
		strerror(PVFS_ERROR_TO_ERRNO(-ret)));
	}
	else
	{
	    fprintf(stderr, 
		"Warning: PVFS_sys_initialize() returned a non PVFS2 error code:\n");
	    fprintf(stderr, "Error: PVFS_sys_initialize: %s.\n", 
		strerror(-ret));
	}
        return(-1);
    }

    /* TODO: reformat this, print it somewhere else */
    printf("Dest file: %s maps to PVFS2 file: %s.\n", 
	user_opts->destfile, pvfs_path);
    printf("   on server: %s file system: %s.\n",
	mnt.ptab_p[mnt_index].meta_addr, mnt.ptab_p[mnt_index].service_name);

    /* get the absolute path on the pvfs2 file system */
    if (PINT_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
    {
        if (pvfs_path[0] != '/')
        {
            fprintf(stderr, "Error: poorly formatted path.\n");
        }
        fprintf(stderr, "Error: cannot retrieve entry name for creation on %s\n",
               pvfs_path);
	PVFS_sys_finalize();
        return(-1);
    }

    memset(&req_create, 0, sizeof(PVFS_sysreq_create));
    memset(&resp_create, 0, sizeof(PVFS_sysresp_create));

    cur_fs = resp_init.fsid_list[0];

    printf("Warning: overriding ownership and permissions to match prototype file system.\n");

    req_create.entry_name = str_buf;
    req_create.attrmask = (ATTR_UID | ATTR_GID | ATTR_PERM);
    req_create.attr.owner = 100;
    req_create.attr.group = 100;
    req_create.attr.perms = 1877;
    req_create.credentials.uid = 100;
    req_create.credentials.gid = 100;
    req_create.credentials.perms = 1877;
    req_create.attr.u.meta.nr_datafiles = -1;
    req_create.parent_refn.handle =
        lookup_parent_handle(pvfs_path,cur_fs);
    req_create.parent_refn.fs_id = cur_fs;

    /* Fill in the dist -- NULL means the system interface used the 
     * "default_dist" as the default
     */
    req_create.attr.u.meta.dist = NULL;

    ret = PVFS_sys_create(&req_create,&resp_create);
    if (ret < 0)
    {
	if(PVFS_ERROR_CLASS(-ret))
	{
	    fprintf(stderr, "Error: PVFS_sys_create: %s.\n", 
		strerror(PVFS_ERROR_TO_ERRNO(-ret)));
	}
	else
	{
	    fprintf(stderr, 
		"Warning: PVFS_sys_create() returned a non PVFS2 error code:\n");
	    fprintf(stderr, "Error: PVFS_sys_create: %s.\n", 
		strerror(-ret));
	}
	PVFS_sys_finalize();
        return(-1);
    }
	
    printf("copied %d bytes.\n", 0);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	if(PVFS_ERROR_CLASS(-ret))
	{
	    fprintf(stderr, "Error: PVFS_sys_finalize: %s.\n", 
		strerror(PVFS_ERROR_TO_ERRNO(-ret)));
	}
	else
	{
	    fprintf(stderr, 
		"Warning: PVFS_sys_finalize() returned a non PVFS2 error code:\n");
	    fprintf(stderr, "Error: PVFS_sys_finalize: %s.\n", 
		strerror(-ret));
	}
	return(-1);
    }

    gossip_disable();

    return(0);
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
    char flags[] = "s:n:";
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
    tmp_opts->ssize = 256 * 1024;
    tmp_opts->num_datafiles = -1;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt){
	    case('s'):
		ret = sscanf(optarg, "%d", &tmp_opts->ssize);
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
	"Usage: %s [-s strip_size] [-n num_datafiles] unix_source_file pvfs2_dest_file.\n",
	argv[0]);
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

