/*
 * This tool supervises given servers and decides when a specific datafile 
 * can be migrated to another better server.
 * Initial revision: Julian M. Kunkel
 *  
 * (C) 2001 Clemson University and The University of Chicago
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"
#include "pint-cached-config.h"

#define DFILE_KEY "system.pvfs2." DATAFILE_HANDLES_KEYSTR

/* optional parameters, filled in by parse_args() */
struct options
{
    char *filesystem;
    int refresh_time;
    int override;
    int verbose;
    
    float load_tolerance;    
};

const struct options default_options = 
{
    "/pvfs2", 
    30,
    0,
    0,
    0.5
};


static struct options *parse_args(
    int argc,
    char *argv[]);
static void usage(
    int argc,
    char **argv);
    
#define print_load(x) (((double) x) / 60000.0)
#define debug(x)                \
        if (user_opts->verbose) \
        {                       \
            x                   \
        }                       


int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr);
    
void print_servers(const char * prefix, 
    int cnt, PVFS_BMI_addr_t * bmi, char ** aliases, 
    PVFS_handle_extent_array * extends);

void print_servers(const char * prefix, 
    int cnt, PVFS_BMI_addr_t * bmi, char ** aliases, 
    PVFS_handle_extent_array * extends)
{
    int i;
    printf("%s - count: %d\n", prefix, cnt);
    for(i=0; i < cnt; i++)
    {
        printf(" %s - %s - extends: %d (%lld-%lld)\n", aliases[i], 
            BMI_addr_rev_lookup(bmi[i]), 
            extends[i].extent_count, lld(extends[i].extent_array[0].first), 
            lld(extends[i].extent_array[0].last));
    }
}
int server_stat_cmp ( const struct PVFS_mgmt_server_stat * stat_1, 
    const struct PVFS_mgmt_server_stat * stat_2);

int server_stat_cmp ( const struct PVFS_mgmt_server_stat * stat_1, 
    const struct PVFS_mgmt_server_stat * stat_2)
{
    return stat_1->load_1 < stat_2->load_1;
}

int main(
    int argc,
    char **argv)
{
    struct options *user_opts = NULL;
    int64_t ret;
    int i;
    PVFS_credentials credentials;

    PVFS_fs_id fsid = 0;
    int dataserver_cnt = 0;
    int metaserver_cnt = 0;
    PVFS_BMI_addr_t * dataserver_array = NULL;
    PVFS_BMI_addr_t * metaserver_array = NULL;
    char ** dataserver_aliases = NULL;
    char ** metaserver_aliases = NULL;
    PVFS_handle_extent_array * dataserver_extend_array = NULL;
    PVFS_handle_extent_array * metaserver_extend_array = NULL;
    double tmp_double;
    
    PVFS_sysresp_mgmt_get_scheduler_stats resp;
    PVFS_sysresp_getattr                  getattr_stats;
    
    struct server_configuration_s *config;
    PVFS_hint * hints = NULL;
    struct PVFS_mgmt_server_stat *stat_array = NULL;
    
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");
    PINT_hint_add_environment_hints(& hints);
    
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
        fprintf(stderr, "Error, failed to parse command line arguments\n");
        return (1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }

    PVFS_util_gen_credentials(&credentials);

    /*
     * Step1: lookup filesystem 
     */
    {
        char out[PVFS_NAME_MAX];   
        ret = PVFS_util_resolve(user_opts->filesystem, &(fsid), 
            out, PVFS_NAME_MAX);
        if ( strlen(out) > 0 && ! (strlen(out) == 1 && out[0] == '/' ) )
        {
             fprintf(stderr, "Error, please specify just a filesystem and no "
                "residing file: %s\n",out);
             exit(1);
        }
    }
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_resolve error\n", ret);
        exit(1);
    }
    
    if( user_opts->verbose )
    {
        printf("Starting automatic rebalancing tool for filesystem:"
            " %s (FS_ID:%d)\n", user_opts->filesystem, (int) fsid);
    }
   
    ret = PVFS_mgmt_count_servers(fsid, & credentials, 
        PVFS_MGMT_META_SERVER, & metaserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers error\n", ret);
        exit(1);
    }
    
    ret = PVFS_mgmt_count_servers(fsid, & credentials, 
        PVFS_MGMT_IO_SERVER, & dataserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers error\n", ret);
        exit(1);
    }

    dataserver_array = (PVFS_BMI_addr_t *) malloc(
         dataserver_cnt * sizeof(PVFS_BMI_addr_t));
    metaserver_array = (PVFS_BMI_addr_t *) malloc(
         metaserver_cnt * sizeof(PVFS_BMI_addr_t));
    dataserver_aliases = (char ** ) malloc(sizeof(char*) 
        * dataserver_cnt);
    metaserver_aliases = (char ** ) malloc(sizeof(char*) 
        * metaserver_cnt);
        
    dataserver_extend_array = (PVFS_handle_extent_array *) malloc(
        sizeof(PVFS_handle_extent_array) * dataserver_cnt);
    metaserver_extend_array = (PVFS_handle_extent_array *) malloc(
        sizeof(PVFS_handle_extent_array) * metaserver_cnt);
            
    if ( metaserver_array == NULL || dataserver_array == NULL ||
        dataserver_aliases == NULL || metaserver_aliases == NULL ||
        dataserver_extend_array == NULL || metaserver_extend_array == NULL )
    {
        fprintf(stderr,"Could not malloc arrays for addresses\n");
        exit(1);
    }             

    ret = PVFS_mgmt_get_server_array(fsid, & credentials, 
        PVFS_MGMT_IO_SERVER, dataserver_array, & dataserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array getting IO servers error\n", ret);
        exit(1);
    }
   
   ret = PVFS_mgmt_get_server_array(fsid, & credentials, 
        PVFS_MGMT_META_SERVER, metaserver_array , & metaserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array getting meta servers error\n", ret);
        exit(1);
    }
   
    config = PINT_get_server_config_struct(fsid);
    PINT_put_server_config_struct(config); 
   
    for( i=0; i < metaserver_cnt; i++ )
    {
        /*
        metaserver_aliases[i] = 
            PINT_config_get_host_alias_ptr(config, 
                (char *) BMI_addr_rev_lookup(metaserver_array[i]));
        */
        ret = PINT_cached_config_get_one_server_alias(
            BMI_addr_rev_lookup( metaserver_array[i] ), 
            config,
            fsid,
            & metaserver_extend_array[i],
            & metaserver_aliases[i],
            0);
        if( ret != 0 )
        {
            PVFS_perror("PINT_cached_config_get_one_server_str\n", ret);
            exit(1);
        }
    }
    
    for( i=0; i < dataserver_cnt; i++ )
    {
        ret = PINT_cached_config_get_one_server_alias(
            BMI_addr_rev_lookup( dataserver_array[i] ), 
            config,
            fsid,
            & dataserver_extend_array[i],
            & dataserver_aliases[i],
            1);
        if( ret != 0 )
        {
            PVFS_perror("PINT_cached_config_get_one_server_str\n", ret);
            exit(1);
        }
    }    
    
        
    if( user_opts->verbose )
    {   
        print_servers("Metaservers", metaserver_cnt, metaserver_array, 
            metaserver_aliases, metaserver_extend_array); 
        print_servers("Dataservers", dataserver_cnt, dataserver_array, 
            dataserver_aliases, dataserver_extend_array);
    }
    
    if ( dataserver_cnt < 2 )
    {
        printf("Warning, at least 2 dataservers have to be available to do"
        " useful migration!\n Abort..\n");
        if ( ! user_opts->override )
        {
            exit(0);
        }
    }
    
    /* now call mgmt functions to determine per-server statistics */
    stat_array = (struct PVFS_mgmt_server_stat *)
        malloc( dataserver_cnt *
               sizeof(struct PVFS_mgmt_server_stat));
    if (stat_array == NULL)
    {
        perror("Error in malloc of stat_array");
        exit(1);
    }
  
    resp.handle_stats.stats = (PVFS_handle_request_statistics*) 
        malloc(sizeof(PVFS_handle_request_statistics) * MAX_LOGGED_HANDLES_PER_FS );
 
    while(1)
    {
    /* gather intelligence */
    
    if( user_opts->verbose )
    {
        printf("Iteration \n");
    }
    
    ret = PVFS_mgmt_statfs_list(
        fsid, &credentials, stat_array, dataserver_array,
        dataserver_cnt, NULL, hints);
    if( ret != 0 )
    {
        /* todo proper error handling, e.g. sleep for a while and retry etc... */
        return (ret);
    }
    
    /* make decisions depending on intelligence */
    /* long term scheduling and short term scheduling 
     * long term scheduling policy: 
     *      if the server is under high load now, then get detailed statistics,
     *      if the sample statistics show that some files are hit repetative, migrate a
     *      couple of these... 
     */
    /* step 1: server with load > 1 are potential candidates, however sort the list */
    
    qsort(stat_array, dataserver_cnt, sizeof(struct PVFS_mgmt_server_stat), 
         (int(*)(const void *, const void *)) server_stat_cmp);
      
    /* figure out load gradient between servers */
    tmp_double = ((double) stat_array[0].load_1 - 
        (double) stat_array[dataserver_cnt-1].load_1 ) ;

    debug( printf("Load: highest: %f lowest: %f diff:%f \n",
            print_load(stat_array[0].load_1), 
            print_load(stat_array[dataserver_cnt-1].load_1), 
            print_load(tmp_double)); )
    
    if ( print_load(tmp_double) < 1.0  || (double) (stat_array[0].load_1) /
        (double) stat_array[dataserver_cnt-1].load_1 < 
            1.0 + user_opts->load_tolerance || 
        stat_array[0].load_1 == 0 )
    {
        debug( printf("Load gradient not sufficient, do nothing\n"); )
        /* wait for next iteration if nothing to do */
idle:        
        sleep(user_opts->refresh_time);
        continue;
    }
        
/*
    memset(& getattr_stats, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(*out_object_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                           & credentials, & getattr_stats, NULL);

    for( i = 0; i < dataserver_cnt ; i++)
    {
        PVFS_error ret;
        int j;
        
        ret = PVFS_mgmt_get_scheduler_stats(
                fsid, &credentials,
                dataserver_array[i],
                MAX_LOGGED_HANDLES_PER_FS,
                & resp,
                hints);        
        if( user_opts->verbose )
        {
            printf("- %s: Load: %f accessed-read:%lld -write:%lld\n", dataserver_aliases[i] , 
                (double)(stat_array[i].load_1) ,
                lld(resp.fs_stats.acc_size[SCHED_LOG_READ]),
                lld(resp.fs_stats.acc_size[SCHED_LOG_WRITE]) );
        }
            
        for(j = 0; j < resp.handle_stats.count ; j++)
        {
            printf(" handle:%lld read:%lld write:%lld\n",
                lld(resp.handle_stats.stats->handle),
                lld(resp.handle_stats.stats->stat.acc_size[SCHED_LOG_READ]),
                lld(resp.handle_stats.stats->stat.acc_size[SCHED_LOG_WRITE])
                );
        }
    }
 */
    
    
    /* time if a possible merge pos is found */
    sleep(user_opts->refresh_time);
    } /* end_while */
    
    PVFS_sys_finalize();
    free(resp.handle_stats.stats);
    free(user_opts);
    return (ret);
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
    int one_opt = 0;

    struct options *tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if (!tmp_opts)
    {
        return (NULL);
    }
    memcpy(tmp_opts, & default_options, sizeof(struct options));
    

    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, "r:t:voV")) != EOF)
    {
        switch (one_opt)
        {
        case ('t'):
            tmp_opts->load_tolerance = (float) atof(optarg);
            break;
        case ('o'):
            tmp_opts->override = 1;
            break;
        case ('r'):
            tmp_opts->refresh_time = atoi(optarg);
            break;
        case ('v'):
            tmp_opts->verbose = 1;
            break;
        case ('V'):
            printf("%s\n", PVFS2_VERSION);
            exit(0);
        case ('h'):
        case ('?'):
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }
    
    if (optind != (argc - 1))
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    tmp_opts->filesystem = argv[argc - 1];
    
    return (tmp_opts);
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr,
            "Usage: %s ARGS <mount-point-to-observe>\n",
            argv[0]);
    fprintf(stderr,
            "default mount point: %s \n"
            "Where ARGS is one or more of\n"
            "\t-o\t override (default: %d)\n"
            "\t-r\t refresh rate (default: %d)\n"
            "\t-t\t load tolerance in percent (default: %.3f)\n"
            "\t-v\t verbose output (default: %d)\n"
            "\t-V\t print version number and exit\n",
            default_options.filesystem,
            default_options.override,
            default_options.refresh_time,
            default_options.load_tolerance,
            default_options.verbose
            );
    return;
}


int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr)
{
    char *entry_name;           /* relativ name of the pvfs2 file */
    char str_buf[PVFS_NAME_MAX];        /* basename of pvfs2 file */
    char file[PVFS_NAME_MAX];
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    int fsid;
    int64_t ret;

    strncpy(file, pvfs2_file, PVFS_NAME_MAX);

    /*
     * Step1: lookup filesystem 
     */
    ret = PVFS_util_resolve(file, &(fsid), file, PVFS_NAME_MAX);
    if (ret < 0)
    {
        fprintf(stderr, "PVFS_util_resolve error\n");
        return -1;
    }
    *out_fs_id = fsid;

    /*
     * ripped from pvfs2-cp.c lookup filename
     */
    entry_name = str_buf;
    if (strcmp(file, "/") == 0)
    {
        /* special case: PVFS2 root file system, so stuff the end of
         * srcfile onto pvfs2_path */
        char *segp = NULL, *prev_segp = NULL;
        void *segstate = NULL;

        /* can only perform this special case if we know srcname */
        if (file == NULL)
        {
            fprintf(stderr, "unable to guess filename in " "toplevel PVFS2\n");
            return -1;
        }

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(fsid, file,
                              credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            return (-1);
        }

        while (!PINT_string_next_segment(file, &segp, &segstate))
        {
            prev_segp = segp;
        }
        entry_name = prev_segp; /* see... points to basename of srcname */
    }
    else        /* given either a pvfs2 directory or a pvfs2 file */
    {
        /* get the absolute path on the pvfs2 file system */

        /*parent_ref.fs_id = obj->pvfs2.fs_id; */

        if (PINT_remove_base_dir(file, str_buf, PVFS_NAME_MAX))
        {
            if (file[0] != '/')
            {
                fprintf(stderr, "Error: poorly formatted path.\n");
            }
            fprintf(stderr, "Error: cannot retrieve entry name for "
                    "creation on %s\n", file);
            return (-1);
        }
        ret = PINT_lookup_parent(file, fsid, credentials, &parent_ref.handle);
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_lookup_parent", ret);
            return (-1);
        }
        else    /* parent lookup succeeded. if the pvfs2 path is just a
                   directory, use basename of src for the new file */
        {
            int len = strlen(file);
            if (file[len - 1] == '/')
            {
                char *segp = NULL, *prev_segp = NULL;
                void *segstate = NULL;

                if (file == NULL)
                {
                    fprintf(stderr, "unable to guess filename\n");
                    return (-1);
                }
                while (!PINT_string_next_segment(file, &segp, &segstate))
                {
                    prev_segp = segp;
                }
                strncat(file, prev_segp, PVFS_NAME_MAX);
                entry_name = prev_segp;
            }
            parent_ref.fs_id = fsid;
        }
    }

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_ref_lookup(parent_ref.fs_id, entry_name,
                              parent_ref, credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret)
    {
        fprintf(stderr, "Failed to lookup object: %s\n", entry_name);
        return -1;
    }

    *out_object_ref = resp_lookup.ref;

    memset(out_resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(*out_object_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                           credentials, out_resp_getattr, NULL);
    if (ret)
    {
        fprintf(stderr, "Failed to do pvfs2 getattr on %s\n", entry_name);
        return -1;
    }

    if (out_resp_getattr->attr.objtype != PVFS_TYPE_METAFILE)
    {
        fprintf(stderr, "Object is no file ! %s\n", entry_name);
        return -1;
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
