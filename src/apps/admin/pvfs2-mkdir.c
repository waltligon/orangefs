/*
 * (C) 2004 Clemson University and The University of Chicago
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

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
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
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_mkdir resp_mkdir;
    struct options* user_opts = NULL;
    char* entry_name;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse "
                "command line arguments.\n");
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

    /* this if-else statement just pulls apart the pathname into its
     * parts....I think...this should be a function somewhere
     */
    if (strcmp(pvfs_path,"/") == 0)
    {

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

    }
    else
    {
        /* get the absolute path on the pvfs2 file system */
        if (PVFS_util_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
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

        ret = PVFS_util_lookup_parent(pvfs_path, cur_fs, &credentials, 
                                      &parent_ref.handle);
        if(ret < 0)
        {
            PVFS_perror("PVFS_util_lookup_parent", ret);
            ret = -1;
            goto main_out;
        }
        else
        {
            parent_ref.fs_id = cur_fs;
        }
    }

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_ref_lookup(parent_ref.fs_id, entry_name,
                              parent_ref, &credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret == 0)
    {
        fprintf(stderr, "Target '%s' already exists!\n", entry_name);
        ret = -1;
        goto main_out;
    }

    memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));

    ret = PVFS_sys_mkdir(entry_name,parent_ref,attr,
	                 &credentials,&resp_mkdir);
    if (ret < 0) {
	PVFS_perror("PVFS_sys_mkdir", ret);
	ret = -1;
	goto main_out;
    }

	/* TODO: need to free the request descriptions */
    ret = 0;

main_out:

    PVFS_sys_finalize();

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

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(optind != (argc - 1))
    {
	usage(argc, argv);
	exit(EXIT_FAILURE);
    }

    /* TODO: should probably malloc and copy instead */
    tmp_opts->destfile = argv[argc-1];

    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s pvfs2_directory\n",argv[0]);
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

