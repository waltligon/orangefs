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
#include <stdlib.h>
#include <getopt.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-event.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* mnt_point;
    int mnt_point_set;
    int api_mask;
    int api_mask_set;
    int op_mask;
    int op_mask_set;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options* user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_credentials creds;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command "
                "line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->mnt_point,
        &cur_fs, pvfs_path, PVFS_NAME_MAX);
    if(ret < 0)
    {
	fprintf(stderr, "Error: could not find filesystem "
                "for %s in pvfstab\n", user_opts->mnt_point);
	return(-1);
    }

    PVFS_util_gen_credentials(&creds);

    if(!user_opts->op_mask || !user_opts->api_mask)
    {
	/* turn off event logging */
	ret = PVFS_mgmt_setparam_all(cur_fs, &creds,
	    PVFS_SERV_PARAM_EVENT_ON, 0, NULL, NULL, NULL);
    }
    else
    {
	/* set mask */
	ret = PVFS_mgmt_setparam_all(cur_fs, &creds,
	    PVFS_SERV_PARAM_EVENT_MASKS,
	    (int64_t)(((int64_t)user_opts->op_mask << 32)
		+ user_opts->api_mask),
	    NULL, NULL, NULL);
	if(ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_setparam_all", ret);
	    return(-1);
	}

	/* turn on event logging */
	ret = PVFS_mgmt_setparam_all(cur_fs, &creds,
	    PVFS_SERV_PARAM_EVENT_ON, 1, NULL, NULL, NULL);
    }

    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_setparam_all", ret);
	return(-1);
    }

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
    char flags[] = "vm:a:o:";
    int one_opt = 0;
    int len = 0;

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt)
        {
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('m'):
		len = strlen(optarg)+1;
		tmp_opts->mnt_point = (char*)malloc(len+1);
		if(!tmp_opts->mnt_point)
		{
		    free(tmp_opts);
		    return(NULL);
		}
		memset(tmp_opts->mnt_point, 0, len+1);
		ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		/* TODO: dirty hack... fix later.  The remove_dir_prefix()
		 * function expects some trailing segments or at least
		 * a slash off of the mount point
		 */
		strcat(tmp_opts->mnt_point, "/");
		tmp_opts->mnt_point_set = 1;
		break;
	    case('a'):{
            char *saveptr;
            char *token;
            tmp_opts->api_mask = 0;
            tmp_opts->api_mask_set = 1;

            if ( strstr(optarg, ",") == NULL ){ /* none or all are allowed */
                if( strcmp(optarg, "all") == 0 ){
                    tmp_opts->api_mask = ~0;
                    break;
                }else if (strcmp(optarg, "none") == 0){
                    tmp_opts->api_mask = 0;
                    break;
                }
            }

            token = strtok_r(optarg, ",", & saveptr);
            while(token != NULL){
                enum PVFS_event_api api = PVFS_event_get_api_nr(token);

                if(api < 0){
                    printf("Could not parse API: %s\n", token);
                    return(NULL);
                }
                tmp_opts->api_mask |= api;

                token = strtok_r(NULL, ",", & saveptr);
            }

    		break;
        }
	    case('o'):
		sscanf(optarg, "%x", &tmp_opts->op_mask);
		if(ret < 1){
		    if(tmp_opts->mnt_point) free(tmp_opts->mnt_point);
		    free(tmp_opts);
		    return(NULL);
		}
		tmp_opts->op_mask_set = 1;
		break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(!tmp_opts->mnt_point_set || !tmp_opts->api_mask_set ||
	!tmp_opts->op_mask_set)
    {
	if(tmp_opts->mnt_point) free(tmp_opts->mnt_point);
	free(tmp_opts);
	return(NULL);
    }

    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    enum PVFS_event_api api_nr = 1;
    char * name = 0;
    int i = 1;

    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-m fs_mount_point] "
            "[-a hex_api_mask] [-o <API_List>]\n", argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2 -a bmi,trove -o 0xFFFF\n"
                    "\tAPI_List is none or all or a komma separated list of the following APIs: \n",
            argv[0]);

    while ( api_nr != PVFS_EVENT_API_LAST){
        name = PVFS_event_get_api_name(api_nr);
        fprintf(stderr, "\t%s\n", name);
        api_nr = 1 << i;
        i++;
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

