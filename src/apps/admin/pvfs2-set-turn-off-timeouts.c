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
#include "pint-cached-config.h"
#include "pvfs2-config.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char *mnt_point;
    int mnt_point_set;
    char *turn_off_timeouts;
    int turn_off_timeouts_set;
};

static struct options* parse_args(int argc, char *argv[]);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_credential creds;
    struct PVFS_mgmt_setparam_value param_value;


#if defined(ENABLE_SECURITY_KEY) || defined(ENABLE_SECURITY_CERT)
    fprintf(stderr,"\nIf either key or certificate security is configured, then you "
                   "cannot modify the TurnOffTimeouts config option.\n\n");
    exit(0);
#endif

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
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
                            &cur_fs,
                            pvfs_path,
                            PVFS_NAME_MAX);
    if(ret < 0)
    {
	fprintf(stderr,
                "Error: could not find filesystem for %s in pvfstab\n", 
	        user_opts->mnt_point);
	return(-1);
    }

    ret = PVFS_util_gen_credential_defaults(&creds);
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_gen_credential_defaults", ret);
        return(-1);
    }

    param_value.type = PVFS_MGMT_PARAM_TYPE_STRING;
    param_value.u.string_value = user_opts->turn_off_timeouts;

    ret = PVFS_mgmt_setparam_all(cur_fs,
                                 &creds,
     		                 PVFS_SERV_PARAM_TURN_OFF_TIMEOUTS,
                                 &param_value,
  	                         NULL,/* status details */
                                 NULL /* hints */);

    if (ret)
    {
        fprintf(stderr,
                "Error(%d) setting TurnOffTimeouts(%s) for mount point(%s)\n",
                ret,
                user_opts->turn_off_timeouts,
                user_opts->mnt_point);
        goto out;
     }
     else
     {
        fprintf(stderr,
                "Successfully set TurnOffTimeouts (%s) for mount point(%s)\n",
                user_opts->turn_off_timeouts,
                user_opts->mnt_point);
     }
out:
    PVFS_sys_finalize();

    return(ret);
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options *parse_args(int argc, char* argv[])
{
    char flags[] = "vm:t:";
    int one_opt = 0;
    int mflag=0,tflag=0;

    struct options* tmp_opts = NULL;


    /* create storage for the command line options */
    tmp_opts = (struct options *)calloc(1,sizeof(struct options));
    if(!tmp_opts)
    {
	return(NULL);
    }

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != -1)
    {
	switch(one_opt)
        {
            case('v'):
            {
                printf("%s\n", PVFS2_VERSION);
                exit(0); 
            }
	    case('m'):
            {
                tmp_opts->mnt_point = strdup(optarg);
		strcat(tmp_opts->mnt_point, "/");
                mflag=1;
		break;
            }
            case('t'):
            {
                tmp_opts->turn_off_timeouts = strdup(optarg);
                tflag=1;
                break;
            }
	    default: /* '?' */
            {
                break;
            }
	}/*end switch*/
    }

    /* Do we have all of the arguments? */
    if ( !mflag && !tflag )
    {
       goto error_exit;
    }
    else if ( !tflag )
    {
       fprintf(stderr,"\nError: Missing TurnOffTimeouts value\n");
       goto error_exit;
    }
    else if ( !mflag )
    {
       fprintf(stderr,"\nError: Missing mount point\n");
       goto error_exit;
    }

    /* We have both arguments.  Edit TurnOffTimeouts value */
    if ( strcasecmp(tmp_opts->turn_off_timeouts,"yes") && strcasecmp(tmp_opts->turn_off_timeouts,"no")  )
    {
       fprintf(stderr,"\nInvalid TurnOffTimeouts value\n\n");
       goto error_exit;    
    }

    return(tmp_opts);

error_exit:
    if (tmp_opts && tmp_opts->mnt_point)
    {
       free(tmp_opts->mnt_point);
    }

    if (tmp_opts && tmp_opts->turn_off_timeouts)
    {   
       free(tmp_opts->turn_off_timeouts);
    }
 
    if (tmp_opts)
    {
       free(tmp_opts);
    }

    usage(argc,argv);

    return(NULL);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "This program allows you to turn <on|off> capability and credential "
                    "timeout checking server-side.  This program affects all servers "
                    "defined in the server config file containing the filesystem described "
                    "by the mount point.  NOTE:  if certificate or key security is running, "
                    "this feature cannot be turned off.\n\n");
    fprintf(stderr,
            "Usage : %s -m <filesystem mount point>  -t <yes|no>\n\n",argv[0]);
    fprintf(stderr,
            "Mount point and TurnOffTimeouts are required.\n\n");
    fprintf(stderr,
            "Example : %s -m /mnt/pvfs2 -t yes\n\n",argv[0]);

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

