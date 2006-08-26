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
#include "pint-sysint-utils.h"
#include "server-config.h"
#include "pvfs2-internal.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

inline void startTimer(
    struct timeval *start_time);
inline double getTimeDiff(
    struct timeval *start_time);
    
inline void startTimer(
    struct timeval *start_time)
{
    gettimeofday(start_time, NULL);
}

inline double getTimeDiff(
    struct timeval *start_time)
{
    struct timeval end_time;
    gettimeofday(&end_time, NULL);

    return (end_time.tv_sec - start_time->tv_sec) +
        (end_time.tv_usec - start_time->tv_usec) / 1000000.0;
}

struct options
{
    char *fs_path_hack;
    char *fs_path_real;
    char *mnt_point;
    char * bmi_target;
    int min_runtime_secs;
};

static struct options *parse_args(
    int argc,
    char *argv[]);
static void usage(
    int argc,
    char **argv);
static int noop_all_servers(
    PVFS_fs_id fsid);
    
int main(
    int argc,
    char **argv)
{
    int ret = -1, err = 0;
    int i;
    PVFS_fs_id fs_id;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = { 0 };
    PVFS_credentials creds;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
        fprintf(stderr, "Error: failed to parse command line " "arguments.\n");
        usage(argc, argv);
        return (-1);
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
    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->fs_path_hack,
                            &fs_id, pvfs_path, PVFS_NAME_MAX);
    if (ret < 0)
    {
        fprintf(stderr, "Failure: could not find filesystem for %s "
                "in pvfs2tab \n", user_opts->fs_path_real);
        return (-1);
    }

    PVFS_util_gen_credentials(&creds);

    
    if ( user_opts->bmi_target == 0)
    {
        /* send noop to everyone*/
        ret = noop_all_servers(fs_id);
        if (ret < 0)
        {
            fprintf(stderr, "Failure: could not communicate with "
                    "one of the servers.\n");
            err = 1;
        }
    }
    else
    {
        PVFS_BMI_addr_t addr;
        int ret;
        struct timeval start_time;
        double timediff = 0;
        long long int iterations = 0;
        
        ret = BMI_addr_lookup(& addr, user_opts->bmi_target);
        if( ret != 0)
        {
            fprintf(stderr, "Failure: could not lookup bmi address: %s\n",
                    user_opts->bmi_target);
            return -1;
        } 
        
        startTimer( & start_time);
        
        while ( timediff < user_opts->min_runtime_secs)
        {
            ret = PVFS_mgmt_noop(fs_id, &creds, addr, NULL);
            timediff = getTimeDiff(& start_time);        
            iterations++;    
        }
        printf("Elapsed time: %f, Noops done: %lld, Noops/sec: %f, sec/Noop:%f\n",
            timediff, iterations, (double) iterations / timediff, 
            timediff / (double) iterations);
            
        
    }    
    
    
    PVFS_sys_finalize();

    return (ret);
}


/* noop_all_servers()
 *
 * sends a noop to all servers listed in the config file 
 *
 * returns -PVFS_error on failure, 0 on success
 */
static int noop_all_servers(
    PVFS_fs_id fsid)
{
    PVFS_credentials creds;
    int ret = -1;
    int count;
    PVFS_BMI_addr_t *addr_array;
    int i;
    int tmp;

    PVFS_util_gen_credentials(&creds);

    printf("No target bmi alias for benchmark given, printing all servers\n"
        "   meta servers:\n");
    ret = PVFS_mgmt_count_servers(fsid, &creds, PVFS_MGMT_META_SERVER, &count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers()", ret);
        return ret;
    }
    addr_array = (PVFS_BMI_addr_t *) malloc(count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
        perror("malloc");
        return -PVFS_ENOMEM;
    }

    ret =
        PVFS_mgmt_get_server_array(fsid, &creds, PVFS_MGMT_META_SERVER,
                                   addr_array, &count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array()", ret);
        return ret;
    }

    for (i = 0; i < count; i++)
    {
        printf("   %s ", PVFS_mgmt_map_addr(fsid, &creds, addr_array[i], &tmp));
        ret = PVFS_mgmt_noop(fsid, &creds, addr_array[i], NULL);
        if (ret == 0)
        {
            printf("Ok\n");
        }
        else
        {
            printf("FAILURE: PVFS_mgmt_noop failed for server: %s\n",
                   PVFS_mgmt_map_addr(fsid, &creds, addr_array[i], &tmp));
            return ret;
        }
    }
    free(addr_array);

    printf("\n   data servers:\n");
    ret = PVFS_mgmt_count_servers(fsid, &creds, PVFS_MGMT_IO_SERVER, &count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers()", ret);
        return ret;
    }
    addr_array = (PVFS_BMI_addr_t *) malloc(count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
        perror("malloc");
        return -PVFS_ENOMEM;
    }

    ret =
        PVFS_mgmt_get_server_array(fsid, &creds, PVFS_MGMT_IO_SERVER,
                                   addr_array, &count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array()", ret);
        return ret;
    }

    for (i = 0; i < count; i++)
    {
        printf("   %s ", PVFS_mgmt_map_addr(fsid, &creds, addr_array[i], &tmp));
        ret = PVFS_mgmt_noop(fsid, &creds, addr_array[i], NULL);
        if (ret == 0)
        {
            printf("Ok\n");
        }
        else
        {
            printf("Failure!\n");
            return ret;
        }
    }
    free(addr_array);

    return (0);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options *parse_args(
    int argc,
    char *argv[])
{
    char flags[] = "vm:t:s:";
    int one_opt = 0;
    int len;

    struct options *tmp_opts = NULL;
    int ret = -1;

    if (argc == 1)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if (tmp_opts == NULL)
    {
        return (NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    
    tmp_opts->min_runtime_secs = 10;
    
    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, flags)) != EOF)
    {
        switch (one_opt)
        {
        case ('v'):
            printf("%s\n", PVFS2_VERSION);
            exit(0);
        case ('t'):
            tmp_opts->bmi_target = optarg;
            break;
        case ('s'):
            sscanf(optarg, "%d", & tmp_opts->min_runtime_secs);
            break;
        case ('m'):
            /* taken from pvfs2-statfs.c */
            len = strlen(optarg) + 1;
            tmp_opts->mnt_point = (char *) malloc(len + 1);
            if (!tmp_opts->mnt_point)
            {
                free(tmp_opts);
                return NULL;
            }
            memset(tmp_opts->mnt_point, 0, len + 1);
            ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
            if (ret < 1)
            {
                free(tmp_opts);
                return NULL;
            }
            /* TODO: dirty hack... fix later.  The remove_dir_prefix()
             * function expects some trailing segments or at least
             * a slash off of the mount point
             */
            strcat(tmp_opts->mnt_point, "/");
            break;
        case ('?'):
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }

    if (optind != (argc))
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    /* get the path of the file system, this one has a trailing slash
     * tacked on, see comment below for why 
     */
    tmp_opts->fs_path_hack = (char *) malloc(strlen(argv[argc - 1]) + 2);
    if (tmp_opts->fs_path_hack == NULL)
    {
        free(tmp_opts);
        return NULL;
    }
    ret = sscanf(argv[argc - 1], "%s", tmp_opts->fs_path_hack);
    if (ret < 1)
    {
        free(tmp_opts->fs_path_hack);
        free(tmp_opts);
        return NULL;
    }
    /* TODO: this is a hack... fix later.  The remove_dir_prefix()
     * function expects some trailing segments or at least a slash
     * off of the mount point
     */
    strcat(tmp_opts->fs_path_hack, "/");

    /* also preserve the real path, to use in print statements elsewhre */
    tmp_opts->fs_path_real = (char *) malloc(strlen(argv[argc - 1]) + 2);
    if (tmp_opts->fs_path_real == NULL)
    {
        free(tmp_opts->fs_path_hack);
        free(tmp_opts);
        return NULL;
    }
    ret = sscanf(argv[argc - 1], "%s", tmp_opts->fs_path_real);
    if (ret < 1)
    {
        free(tmp_opts->fs_path_hack);
        free(tmp_opts->fs_path_real);
        free(tmp_opts);
        return NULL;
    }

    return (tmp_opts);
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr, "%s version %s\n\n", argv[0], PVFS2_VERSION);
    fprintf(stderr, "Usage  : %s [-t target_bmi_address] [-s runtime_sec]"
                    " -m file_system_path\n", argv[0]);
    fprintf(stderr, "Example: %s -t tcp://localhost:3334 -m /mnt/pvfs2\n", argv[0]);
    fprintf(stderr, "If no target bmi address is given all addresses are printed");
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
