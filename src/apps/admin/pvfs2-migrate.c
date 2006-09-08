/*
 * Author julian M. Kunkel
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

#define DFILE_KEY "system.pvfs2." DATAFILE_HANDLES_KEYSTR

/* optional parameters, filled in by parse_args() */
struct options
{
    char *file;
    char *old_dataserver;
    char *new_dataserver;
    PVFS_handle old_dataserver_handle_number;
    int override_restrictions;
    int verbose;
};


static struct options *parse_args(
    int argc,
    char *argv[]);
static void usage(
    int argc,
    char **argv);
    
    
int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr);
    
/*
 * nservers is an in-out style parameter
 * servers is allocated memory upto *nservers and each element inside that
 * is allocated internally in this function.
 * callers job is to free up all the memory
 */
static int generic_server_location( PVFS_object_ref obj, PVFS_credentials *creds,
        char **servers, PVFS_handle *handles, int *nservers)
{
    char *buffer = (char *) malloc(4096);
    int ret, num_dfiles, count;
    PVFS_fs_id fsid;
    
    PVFS_ds_keyval key, val;

    key.buffer = DFILE_KEY;
    key.buffer_sz = strlen(DFILE_KEY) + 1;
    val.buffer = buffer;
    val.buffer_sz = 4096;
    if ((ret = PVFS_sys_geteattr(obj, 
            creds, &key, &val)) < 0)
    {
        PVFS_perror("PVFS_sys_geteattr", ret);
        return -1;
    }
    ret = val.read_sz;
    fsid = obj.fs_id;

    /*
     * At this point, we know all the dfile handles 
     */
    num_dfiles = (ret / sizeof(PVFS_handle));
    count = num_dfiles < *nservers ? num_dfiles : *nservers;
    for (ret = 0; ret < count; ret++)
    {
        PVFS_handle *ptr = (PVFS_handle *) ((char *) buffer + ret * sizeof(PVFS_handle));
        servers[ret] = (char *) calloc(1, PVFS_MAX_SERVER_ADDR_LEN);
        handles[ret] = *ptr;
        if (servers[ret] == NULL)
        {
            break;
        }
        /* ignore any errors */
        PINT_cached_config_get_server_name(
                servers[ret], PVFS_MAX_SERVER_ADDR_LEN,
                *ptr, fsid);
    }
    if (ret != count)
    {
        int j;
        for (j = 0; j < ret; j++)
        {
            free(servers[j]);
            servers[j] = NULL;
        }
        return -1;
    }
    *nservers = count;
    return 0;
}

int main(
    int argc,
    char **argv)
{
    struct options *user_opts = NULL;
    int64_t ret;
    PVFS_credentials credentials;

    PVFS_fs_id fsid = 0;
    PVFS_object_ref metafile_ref;
    int i;

    char *servers[1024];
    char metadataserver[256];
    
    PVFS_handle handles[1024];
    int nservers = 1024;
    
    PVFS_BMI_addr_t bmi_metadataserver;
    PVFS_BMI_addr_t bmi_olddatataserver;
    PVFS_BMI_addr_t bmi_newdatataserver;
    
    /*
    PVFS_hint * hints = NULL;
    
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");
    PINT_hint_add_environment_hints(& hints);
    */
    
    PVFS_sysresp_getattr resp_getattr;  
    
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


    if ( lookup(user_opts->file, &credentials, &fsid, & metafile_ref, & resp_getattr) != 0 )
    {
        return -1;
    }
    
    ret = generic_server_location(metafile_ref, &credentials, servers, 
        handles, &nservers);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read server location information!\n");
        return -1;
    }    
    
    if( user_opts->old_dataserver_handle_number != 0){
        /*
         * Lookup server bmi-address !
         */
        for(i = 0 ; i < nservers; i++)
        {
            if( user_opts->old_dataserver_handle_number == handles[i] )
            {
                user_opts->old_dataserver = servers[i];
                break;
            }
        }
         
        if( i == nservers)
        {
            fprintf(stderr, "Error, could not find dataserver for handle: %lld\n", 
                lld(user_opts->old_dataserver_handle_number));
            return(-1);
        }
    }else{
        /*
         * Lookup by server bmi address:
         */
        for(i = 0 ; i < nservers; i++)
        {
            if( strcmp(servers[i], user_opts->old_dataserver) == 0)
            {
                user_opts->old_dataserver_handle_number = handles[i];
                break;
            }
        }
        if( i == nservers)
        {
            fprintf(stderr, "Error, could not find handle within datafile for server: %s\n", 
                user_opts->old_dataserver);
            return(-1);
        }
    }
   
    

    /*
    * Got metafile 
    */
    if( strcmp(user_opts->old_dataserver, user_opts->new_dataserver) == 0 )
    {
        if( user_opts->override_restrictions )
        {
            fprintf(stderr, "Warning, source and target dataserver are %s, override active\n", 
                user_opts->old_dataserver);
        }
        else
        {
            fprintf(stderr, "Error, source and target dataserver are %s, nothing to be done\n", 
                user_opts->old_dataserver);
            return (-1);
        }
    }
    
    ret = PINT_cached_config_get_server_name(metadataserver, 256, 
        metafile_ref.handle, metafile_ref.fs_id);
    if( ret != 0)
    {
        fprintf(stderr, "Error, could not get metadataserver name\n");
        return (-1);
    }
    
    
    ret = BMI_addr_lookup(&bmi_metadataserver,metadataserver);
    if (ret < 0)
    {
        fprintf(stderr, "Error, BMI_addr_lookup unsuccessful %s\n",
        metadataserver );
        return(-1);
    }
    
    ret = BMI_addr_lookup(&bmi_newdatataserver, user_opts->new_dataserver);
    if (ret < 0)
    {
        fprintf(stderr, "Error, BMI_addr_lookup unsuccessful %s\n",
         user_opts->new_dataserver );
        return(-1);
    }    
    
    ret = BMI_addr_lookup(&bmi_olddatataserver,user_opts->old_dataserver);
    if (ret < 0)
    {
        fprintf(stderr, "Error, BMI_addr_lookup unsuccessful %s\n",
        user_opts->old_dataserver );
        return(-1);
    }
    
    if( user_opts->verbose )
    {
        printf("Found metafile handle: %lld\n", lld(metafile_ref.handle));
        printf("Starting migration for pvfs2-file:%s \n\tMetadataserver: %s \n"
                "\treplace dataserver %s with %s \n\taffected datafile with handle: %lld\n",
            user_opts->file, metadataserver,  
            user_opts->old_dataserver, user_opts->new_dataserver,
            lld(user_opts->old_dataserver_handle_number));         
    }

    ret = PVFS_mgmt_migrate(metafile_ref.fs_id, & credentials, bmi_metadataserver
        , metafile_ref.handle, 
        user_opts->old_dataserver_handle_number, 
        bmi_olddatataserver, bmi_newdatataserver);
    /*PVFS_free_hint(& hints);*/
    
    if ( ret != 0 )
    {
        PVFS_perror("Error file could not be migrated", ret);
        return -1;
    }
    if( user_opts->verbose )
    {
        printf("Migration done\n");
    }
        
    PVFS_sys_finalize();
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
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, "s:d:voV")) != EOF)
    {
        switch (one_opt)
        {
        case('d'):        
            tmp_opts->old_dataserver = optarg;
            break;
        case('s'):
            tmp_opts->old_dataserver_handle_number = (PVFS_handle) atoll(optarg);
            break;            
        case ('v'):
            tmp_opts->verbose = 1;
            break;
        case ('o'):
            tmp_opts->override_restrictions = 1;
            break;
        case ('V'):
            printf("%s\n", PVFS2_VERSION);
            exit(0);
        case ('?'):
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }

    if( tmp_opts->old_dataserver_handle_number == 0 &&
        tmp_opts->old_dataserver  == NULL ){
        usage(argc, argv);
        exit(EXIT_FAILURE);   
    } 
    if (optind != (argc - 2))
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    tmp_opts->file = argv[argc - 2];
    tmp_opts->new_dataserver = argv[argc - 1];

    return (tmp_opts);
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr,
            "Usage: %s ARGS [-s|-d] file new_dataserver_name\n",
            argv[0]);
    fprintf(stderr,
            "\n-d server_name \t\t migrate datafile of servername to new_dataserver"
            "\n-s handle_number \t migrate datafile with handle number to new_dataserver"
            "\nWhere ARGS is one or more of"
            "\n-v\t\t\t verbose output\n"
            "\n-o\t\t\t override restrictions\n"
            "\n-V\t\t\t print version number and exit\n");
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
                              PVFS2_LOOKUP_LINK_FOLLOW);
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
                              PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret)
    {
        fprintf(stderr, "Failed to lookup object: %s\n", entry_name);
        return -1;
    }

    *out_object_ref = resp_lookup.ref;

    memset(out_resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(*out_object_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                           credentials, out_resp_getattr);
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
