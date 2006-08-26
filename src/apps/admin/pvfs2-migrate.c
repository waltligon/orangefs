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

/* optional parameters, filled in by parse_args() */
struct options
{
    char *file;
    char *old_dataserver_alias;
    char *new_dataserver_alias;
    PVFS_handle old_dataserver_handle_number;
    
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
    
const char *get_server_for_handle(
    PVFS_handle handle,
    int server_count,
    char **server_names,
    PVFS_handle * data_lower_handle,
    PVFS_handle * data_upper_handle);

const char *get_server_for_handle(
    PVFS_handle handle,
    int server_count,
    char **server_names,
    PVFS_handle * data_lower_handle,
    PVFS_handle * data_upper_handle)
{
    int i;
    for (i = 0; i < server_count; i++)
    {
        if (data_lower_handle[i] <= handle && handle <= data_upper_handle[i])
        {
            return server_names[i];
        }
    }
    return NULL;
}

int migrate_alias(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_object_ref metafile_ref,
    PVFS_handle datafile_handle,
    const char * const metaserver_alias,
    const char * const source_alias,
    const char * const target_alias
    );
    
int getServers(
    int type,
    PVFS_fs_id fsid,
    PVFS_credentials * credentials,
    PVFS_BMI_addr_t ** out_addr_array_p,
    char ***out_server_names,
    int *out_server_count,
    PVFS_handle ** lower_handle,
    PVFS_handle ** upper_handle);
    
static inline double Wtime(
    void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return ((double) t.tv_sec + (double) (t.tv_usec) / 1000000);
}

int getServers(
    int type,
    PVFS_fs_id fsid,
    PVFS_credentials * credentials,
    PVFS_BMI_addr_t ** out_addr_array_p,
    char ***out_server_names,
    int *out_server_count,
    PVFS_handle ** lower_handle_p,
    PVFS_handle ** upper_handle_p)
{
    int ret, i;
    int count;
    PVFS_BMI_addr_t *addr_array;

    ret = PVFS_mgmt_count_servers(fsid, credentials, type, &count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers()", ret);
        exit(1);
    }

    *out_server_count = count;

    addr_array = (PVFS_BMI_addr_t *) malloc(count * sizeof(PVFS_BMI_addr_t));
    memset(addr_array, 0, count * sizeof(PVFS_BMI_addr_t));
    *out_addr_array_p = addr_array;

    *out_server_names = (char **) malloc(count * sizeof(char *));
    *lower_handle_p = (PVFS_handle *) malloc(count * sizeof(PVFS_handle));
    *upper_handle_p = (PVFS_handle *) malloc(count * sizeof(PVFS_handle));

    if (addr_array == NULL || *out_server_names == NULL ||
        *lower_handle_p == NULL || *upper_handle_p == NULL)
    {
        perror("malloc");
        return ret;
    }

    ret =
        PVFS_mgmt_get_server_array(fsid, credentials, type, addr_array,
                                   out_server_count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array()", ret);
        return ret;
    }

    if (count != *out_server_count)
    {
        fprintf(stderr, "Error count != *out_server_count: %d != %d \n", count,
                *out_server_count);
        return -1;
    }

    char **server_names = *out_server_names;
    PVFS_handle *lower_handle = *lower_handle_p;
    PVFS_handle *upper_handle = *upper_handle_p;

    for (i = 0; i < count; i++)
    {
        ret = PVFS_mgmt_map_addr_to_alias(fsid, credentials, addr_array[i],
                                          &server_names[i],
                                          &lower_handle[i], &upper_handle[i],
                                          type);
        if (ret != 0)
        {
            fprintf(stderr,
                    "Could not map bmi_address to string for entry: %d\n",
                    i + 1);
            exit(1);
        }
    }

    return 0;
}

int migrate_alias(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_object_ref metafile_ref,    
    PVFS_handle datafile_handle,
    const char * const metaserver_alias,
    const char * const source_alias,
    const char * const target_alias
    )
{
    PVFS_hint * hints = NULL;
    PVFS_BMI_addr_t source_address;
    PVFS_BMI_addr_t target_address;
    PVFS_BMI_addr_t meta_address;
    PVFS_error ret;

    ret = PVFS_get_bmi_address(source_alias, fs_id, &source_address);    
    if( ret != 0){
        return ret;
    }
    ret = PVFS_get_bmi_address(target_alias, fs_id, &target_address);    
    if( ret != 0){
        return ret;
    }
    ret = PVFS_get_bmi_address(metaserver_alias, fs_id, &meta_address);    
    if( ret != 0){
        return ret;
    }    
    
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");        
    ret = PVFS_mgmt_migrate(fs_id, credentials, meta_address , metafile_ref.handle, 
        datafile_handle, source_address, target_address, hints);
    PVFS_free_hint(& hints);
    
    return ret;
}


/*
 * Steps:
 * lookup filename
 * map aliases to handle_ranges
 * start migration and we are done once the migration finishes :)
 */
int main(
    int argc,
    char **argv)
{
    struct options *user_opts = NULL;
    int64_t ret;
    PVFS_credentials credentials;

    PVFS_fs_id fsid = 0;
    PVFS_object_ref metafile_ref;
    int data_server_count;
    PVFS_BMI_addr_t *data_addr_array = NULL;
    char **data_server_names = NULL;
    PVFS_handle *data_lower_handle = NULL;
    PVFS_handle *data_upper_handle = NULL;
    
    int meta_server_count;
    PVFS_BMI_addr_t *meta_addr_array = NULL;
    char **meta_server_names = NULL;
    PVFS_handle *meta_lower_handle = NULL;
    PVFS_handle *meta_upper_handle = NULL;
    
    char * metaserver_alias = NULL;

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
    
    ret = getServers(PVFS_MGMT_IO_SERVER,
                     fsid,
                     &credentials,
                     &data_addr_array,
                     &data_server_names,
                     &data_server_count,
                     &data_lower_handle, &data_upper_handle);
    if (ret < 0)
    {
        return ret;
    }    
    
    ret = getServers(PVFS_MGMT_META_SERVER,
                     fsid,
                     &credentials,
                     &meta_addr_array,
                     &meta_server_names,
                     &meta_server_count,
                     &meta_lower_handle, &meta_upper_handle);
    if (ret < 0)
    {
        return ret;
    }    
    
    /*
     * Lookup server alias !
     */
    metaserver_alias = (char *) get_server_for_handle(
                                      metafile_ref.handle,
                                      meta_server_count, meta_server_names,
                                      meta_lower_handle, meta_upper_handle);
     
    if( user_opts->old_dataserver_handle_number != 0){
        user_opts->old_dataserver_alias = (char *) get_server_for_handle(
                                      user_opts->old_dataserver_handle_number,
                                      data_server_count, data_server_names,
                                      data_lower_handle, data_upper_handle);
    }else{
        /*
         * lookup handle number !
         * get datafiles from acache if possible !
         */
         
        PVFS_sys_attr *attr;
        PVFS_handle *dfile_array;
        int dfile_count;
        int i;
        int matching = 0;
        PVFS_handle server_lower_range;
        PVFS_handle server_upper_range;
        
        attr = & resp_getattr.attr;
        
        dfile_array =
            (PVFS_handle *) malloc(sizeof(PVFS_handle) * attr->dfile_count);
        dfile_count = attr->dfile_count;
        ret =
            PVFS_mgmt_get_datafiles_from_acache(metafile_ref, dfile_array,
                                                &dfile_count);
        /*
         * find matching server.
         */
        
        for (i = 0; i < data_server_count; i++)
        {
            if( strcmp(data_server_names[i], user_opts->old_dataserver_alias) == 0 )
            {
               server_lower_range = data_lower_handle[i];
               server_upper_range = data_upper_handle[i];
               break;
            }
        }        
        if( i == data_server_count )
        {
            fprintf(stderr, "Server name could not be found !\n");
            return -1;
        }
        
        /*
         * Find matching handles in datafiles
         */
        for (i = 0; i < dfile_count; i++)
        {
                if (server_lower_range <= dfile_array[i] && 
                    dfile_array[i] <= server_upper_range)
                {
                    matching++;
                    user_opts->old_dataserver_handle_number = dfile_array[i];
                    if(user_opts->verbose )
                    {
                        printf("Found datafile %lld\n", dfile_array[i]);
                    }
                }           
        }
        
        if( matching < 1 )
        {
            fprintf(stderr, "Found no datafile in range of server !\n");
            return -1;
        }
        
        if( matching > 1 )
        {
            fprintf(stderr, "Found multiple datafiles in range of server, please specify datafile !\n");
            return -1;
        }

        free(dfile_array);
    }
   
    
    if (user_opts->old_dataserver_alias == NULL){
        fprintf(stderr, "Error, could not find handle for handle: %lld\n", 
            lld(user_opts->old_dataserver_handle_number));
        exit(1); 
    }
    /*
    * Got metafile 
    */
    if( strcmp(user_opts->old_dataserver_alias, user_opts->new_dataserver_alias) == 0 )
    {
        fprintf(stderr, "Error, source and target dataserver are %s, nothing to be done\n", 
            user_opts->old_dataserver_alias);
        /*exit(1);*/
    }
    
    if( user_opts->verbose )
    {
        printf("Found metafile handle: %lld\n", lld(metafile_ref.handle));
        printf("Starting migration for pvfs2-file:%s on metadaserver: %s \n"
                "\t replace dataserver %s with %s for handle: %lld\n",
            user_opts->file, metaserver_alias,  
            user_opts->old_dataserver_alias, user_opts->new_dataserver_alias,
            lld(user_opts->old_dataserver_handle_number));         
    }
    
    ret = migrate_alias( fsid, & credentials, metafile_ref, 
        user_opts->old_dataserver_handle_number,
        metaserver_alias, user_opts->old_dataserver_alias,
        user_opts->new_dataserver_alias);
    
    if ( ret != 0 )
    {
        PVFS_perror("Error file could not be migrated", ret);
        return -1;
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
    while ((one_opt = getopt(argc, argv, "s:d:vV")) != EOF)
    {
        switch (one_opt)
        {
        case('d'):        
            tmp_opts->old_dataserver_alias = optarg;
            break;
        case('s'):
            tmp_opts->old_dataserver_handle_number = (PVFS_handle) atoll(optarg);
            break;            
        case ('v'):
            tmp_opts->verbose = 1;
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
        tmp_opts->old_dataserver_alias  == NULL ){
        usage(argc, argv);
        exit(EXIT_FAILURE);   
    } 
    if (optind != (argc - 2))
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    tmp_opts->file = argv[argc - 2];
    tmp_opts->new_dataserver_alias = argv[argc - 1];

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
