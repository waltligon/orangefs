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

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* hostname;
    char* fsname;
    int   port;
    char* network_proto;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

int main(int argc, char **argv)
{
    int ret = -1;
    struct options* user_opts = NULL;
    struct PVFS_sys_mntent* tmp_ent = NULL;
    char config_server[256];

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
        fprintf(stderr, "Error: failed to parse command "
                        "line arguments.\n");
        usage(argc, argv);
        return(-1);
    }

    sprintf(config_server, "%.50s://%.150s:%d", user_opts->network_proto,
        user_opts->hostname, user_opts->port);

    /* build mnt entry */
    tmp_ent = PVFS_util_gen_mntent(config_server, user_opts->fsname);
    if(!tmp_ent)
    {
        fprintf(stderr, "Error: failed to build mnt entry.\n");
        return(-1);
    }

    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to initialize PVFS2 library.\n");
        return(-1);
    }

    ret = PVFS_sys_fs_add(tmp_ent);
    if(ret < 0)
    {
        PVFS_perror("Error: could not retrieve configuration from server", ret);
        return(-1);
    }

    PVFS_sys_finalize();

    PVFS_util_gen_mntent_release(tmp_ent);

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
    char flags[] = "h:f:n:p:";
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
            case('h'):
                len = strlen(optarg)+1;
                tmp_opts->hostname = (char*)malloc(len+1);
                if(!tmp_opts->hostname)
                {
                    free(tmp_opts);
                    return(NULL);
                }
                memset(tmp_opts->hostname, 0, len+1);
                ret = sscanf(optarg, "%s", tmp_opts->hostname);
                if(ret < 1){
                    free(tmp_opts);
                    return(NULL);
                }
                break;
            case('f'):
                len = strlen(optarg)+1;
                tmp_opts->fsname = (char*)malloc(len+1);
                if(!tmp_opts->fsname)
                {
                    free(tmp_opts);
                    return(NULL);
                }
                memset(tmp_opts->fsname, 0, len+1);
                ret = sscanf(optarg, "%s", tmp_opts->fsname);
                if(ret < 1){
                    free(tmp_opts);
                    return(NULL);
                }
                break;
            case('n'):
                len = strlen(optarg)+1;
                tmp_opts->network_proto = (char*)malloc(len+1);
                if(!tmp_opts->network_proto)
                {
                    free(tmp_opts);
                    return(NULL);
                }
                memset(tmp_opts->network_proto, 0, len+1);
                ret = sscanf(optarg, "%s", tmp_opts->network_proto);
                if(ret < 1){
                    free(tmp_opts);
                    return(NULL);
                }
                break;
            case('p'):
                ret = sscanf(optarg, "%d", &tmp_opts->port);
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

    if(!tmp_opts->hostname || !tmp_opts->fsname ||
        !tmp_opts->network_proto || !tmp_opts->port)
    {
        if(tmp_opts->hostname) 
            free(tmp_opts->hostname);

        if(tmp_opts->fsname) 
            free(tmp_opts->fsname);

        if(tmp_opts->network_proto) 
            free(tmp_opts->network_proto);

        free(tmp_opts);
        return(NULL);
    }

    return(tmp_opts);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s -h <server> "
            "-f <fsname> -n <proto> -p <port>\n", argv[0]);

    fprintf(stderr, "Check to see if a server is responding.\n\n");

    fprintf(stderr,"  -h <server>                name of the server\n");
    fprintf(stderr,"  -f <fsname>                name of the exported"
            " file system\n");
    fprintf(stderr,"  -n <proto>                 name of the network"
            " protocol to use\n");
    fprintf(stderr,"  -p <port>                  port number on which"
            " the pvfs2 server is listening\n");

    fprintf(stderr, "Example: %s -h localhost -f pvfs2-fs -n tcp -p 7500\n",
            argv[0]);
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

