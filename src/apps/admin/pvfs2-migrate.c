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
    
    int verbose;
};


static struct options *parse_args(
    int argc,
    char *argv[]);
static void usage(
    int argc,
    char **argv);
    
int lookup(
    char * pvfs2_file,
    PVFS_credentials *credentials, 
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref
    );
    
int migrate_alias(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_object_ref * metafile_ref,
    const char * const source_alias,
    const char * const target_alias
    );
    
static inline double Wtime(
    void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return ((double) t.tv_sec + (double) (t.tv_usec) / 1000000);
}

int migrate_alias(
    PVFS_fs_id fs_id,
    PVFS_credentials *credentials,
    PVFS_object_ref * metafile_ref,
    const char * const  source_alias,
    const char * const  target_alias
    )
{
    PVFS_hint * hints = NULL;
    PVFS_BMI_addr_t metaserver_addr;
    PVFS_object_ref target_datafile_ref;
    PVFS_BMI_addr_t source_dataserver;
    PVFS_BMI_addr_t target_dataserver; 
    
    /*
     * Find target handle out of metadata
     */
     /*
     PVFS_mgmt_count_servers
     PVFS_mgmt_get_server_array
     
     foreach in server array
        PVFS_mgmt_map_addr => string name
        look for bmi_address => source_dataserver
     
     get_datafile_handles (meta_object)
     get_datafile_handles_alias (meta_object)
     
     PVFS_mgmt_map_addr(msg_p->fs_id, sm_p->cred_p,
+                                    msg_p->svr_addr, &iTmp));
     */
     
     
     /* Search for server and figure out handle range of that server*/
    
    /*
     * Find bmi addresses for source and target.
     */
    
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");    
    
    PVFS_mgmt_migrate(fs_id, credentials, metaserver_addr, target_datafile_ref,
        source_dataserver, target_dataserver, hints);
    PVFS_free_hint(& hints);
    
    return 0;
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


    if ( lookup(user_opts->file, &credentials, &fsid, & metafile_ref) != 0 )
    {
        return -1;
    }
    
    /*
    * Got metafile 
    */
    if( user_opts->verbose )
    {
        printf("Found metafile handle: %lld\n", lld(metafile_ref.handle));
        printf("Starting migration for pvfs2-file:%s \n\t replace dataserver %s with %s\n",
            user_opts->file, user_opts->old_dataserver_alias, user_opts->new_dataserver_alias);         
    }
    
    if ( migrate_alias( fsid, & credentials, & metafile_ref, user_opts->old_dataserver_alias,
        user_opts->new_dataserver_alias) != 0 )
    {
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
    while ((one_opt = getopt(argc, argv, "vV")) != EOF)
    {
        switch (one_opt)
        {
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

    if (optind != (argc - 3))
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    tmp_opts->file = argv[argc - 3];
    tmp_opts->old_dataserver_alias = argv[argc - 2];
    tmp_opts->new_dataserver_alias = argv[argc - 1];

    return (tmp_opts);
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr,
            "Usage: %s ARGS file old_dataserver_name new_dataserver_name\n",
            argv[0]);
    fprintf(stderr,
            "Where ARGS is one or more of"
            "\n-v\t\t\t\tverbose output\n"
            "\n-V\t\t\t\tprint version number and exit\n");
    return;
}


int lookup(
    char * pvfs2_file,
    PVFS_credentials *credentials, 
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref
    )
{
    char *entry_name;           /* relativ name of the pvfs2 file */
    char str_buf[PVFS_NAME_MAX];    /* basename of pvfs2 file */
    char file[PVFS_NAME_MAX];
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_object_ref parent_ref;
    int fsid;
    int64_t ret;
    
    strncpy(file, pvfs2_file, PVFS_NAME_MAX);
    
    /*
     * Step1: lookup filesystem 
     */
    ret = PVFS_util_resolve(file, &(fsid), 
        file, PVFS_NAME_MAX);
    if( ret < 0 ){
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

        if (PINT_remove_base_dir(file, str_buf,
                                 PVFS_NAME_MAX))
        {
            if (file[0] != '/')
            {
                fprintf(stderr, "Error: poorly formatted path.\n");
            }
            fprintf(stderr, "Error: cannot retrieve entry name for "
                    "creation on %s\n", file);
            return (-1);
        }
        ret = PINT_lookup_parent(file, 
                                     fsid, credentials,
                                     &parent_ref.handle);
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_lookup_parent", ret);
            return (-1);
        }
        else /* parent lookup succeeded. if the pvfs2 path is just a
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
                return(-1);
            }
            while (!PINT_string_next_segment(file, 
                &segp, &segstate))
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
    
    memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(*out_object_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                           credentials, &resp_getattr, NULL);
    if (ret)
    {
        fprintf(stderr, "Failed to do pvfs2 getattr on %s\n", entry_name);
        return -1;
    }

    if (resp_getattr.attr.objtype != PVFS_TYPE_METAFILE)
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
