/*
 * Copyright © Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "pvfs2.h"
#include "pvfs2-internal.h"


#include "fsck-utils.h"

#define VERSION "0.1"
/** \defgroup pvfs2validate PVFS2 Validate 
 * 
 * The pvfs2-validate implementation provides a client side utility to find and 
 * repair PVFS2 file system problems. 
 *
 * @see fsckutils
 * 
 * Before running pvfs2-validate, the following requirements must be met:
 * - The PVFS2 servers must be running
 * - No clients should be accssing the file system
 * .
 * 
 * TODO:
 *  - Need to design a way to kick off process on the pvfs2-server to check 
 *    for orphaned bstreams (bstreams with no attributes and/or dfiles)
 *  - Needs to enter admin mode in beginning if needed, and leave at end. 
 *    Shouldn't leave admin mode during operation.
 *  - Add ability to run pvfs2-validate on an unmounted filesytem, and on a system
 *    with no entries in tabfiles (pvfs2tab, /etc/mtab, /etc/fstab)
 *  - Force a sync on filesystem before beginning fsck
 * .
 * 
 * QUESTIONS:
 * - Does the underlying filesystem fsck interfere with pvfs2-validate?
 * - Should the underlying filesytem fsck be run in any conditions?
 * - How/When to clean up lost+found directory?
 * - What happens when lost+found directory has errors?
 * - Can anyone with root access run pvfs2-validate? Do we need some sort of authorization?
 * - Any known limits? (memory usage in pvfs2-validate, number of files)?
 * - Should we enter admin mode if there is no chance of repairs on found objects?
 * - Can pvfs2-validate be initiated automatically in some cases?
 * - When pvfs2-validate runs, how does is affect performance (readonly/repair)?
 * - Can the this be parallelized?
 * - Can we have a "conditional admin mode", where admin mode only entered after
 *   a certain time condition where no file system activity takes place?  This
 *   would keep servers from entering admin mode in the middle of operations.
 * .
 *
 * @{
 */

/** \file
 * Implementation of PVFS2 FSCK tool.
 */ 

/* Function Prototypes */
static void usage(
    int,
    char **);

int validate_pvfs_object(
    const struct PINT_fsck_options *fsck_options,
    const PVFS_object_ref * pref,
    const PVFS_credentials * creds,
    int *cur_fs,
    char *current_path);

static struct PINT_fsck_options *parse_args(
    int argc,
    char **argv);

int main(int argc, char **argv)
{
    int ret = 0;
    int cur_fs = 0;
    char pvfs_path[PVFS_NAME_MAX] = { 0 };
    PVFS_credentials creds;
    PVFS_sysresp_lookup lookup_resp;
    struct PINT_fsck_options *fsck_options = NULL;

    memset(&creds, 0, sizeof(creds));
    memset(&lookup_resp, 0, sizeof(lookup_resp));
    
    fsck_options = parse_args(argc, argv);
    if(!fsck_options)
    {
        fprintf(stderr, "Error: failed to parse arguments.\n");
        usage(argc, argv);
        return -1;
    }

    if (!fsck_options->start_path)
    {
        fprintf(stderr, "Error: no starting path specified (See -d option below).\n");
        usage(argc, argv);
        free(fsck_options);
        return -1;
    }

    ret = PVFS_util_init_defaults();
    if (ret != 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        free(fsck_options);
        return -1;
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(
            fsck_options->start_path, 
            &cur_fs, pvfs_path,
            sizeof(pvfs_path));
            
    if (ret != 0)
    {
        PVFS_perror("PVFS_util_resolve", ret);
        PVFS_sys_finalize();
        free(fsck_options);
        return -1;
    }

    if (fsck_options->check_stranded_objects && (strcmp(pvfs_path, "/") != 0))
    {
        fprintf(stderr, "Error: -d must specify the pvfs2 root directory when utilizing the -c option.\n");
        usage(argc, argv);
        PVFS_sys_finalize();
        free(fsck_options);
        return -1;
    }

    PVFS_util_gen_credentials(&creds);

    ret = PVFS_sys_lookup(
            cur_fs, pvfs_path, 
            &creds, 
            &lookup_resp,
            PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
            
    if (ret != 0)
    {
        fprintf(stderr, "Error: failed lookup on [%s]\n", pvfs_path);
        PVFS_perror("PVFS_sys_lookup", ret);
        PVFS_sys_finalize();
        free(fsck_options);
        return -1;
    }

    ret = PVFS_fsck_initialize(fsck_options, &creds, &cur_fs);
    if (ret < 0)
    {
        PVFS_perror("PVFS_fsck_initialize", ret);
        PVFS_sys_finalize();
        free(fsck_options);
        return(-1);
    }

    ret = PVFS_fsck_check_server_configs(fsck_options, &creds, &cur_fs);
    if (ret < 0)
    {
        fprintf(stderr, "Error: a difference was detected while validating the server fs configs.\n");
        PVFS_perror("PVFS_fsck_check_server_configs", ret);
        PVFS_sys_finalize();
        free(fsck_options);
        return(-1);
    }

    /* stop right here if check_fs_configs option is enabled */
    if (fsck_options->check_fs_configs)
    {
        printf("All PVFS2 servers have consistent fs configurations.\n");
        free(fsck_options);
        return 0;
    }

    printf("pvfs2-validate starting validation at object [%s]\n",
           fsck_options->start_path);

    /* validate the object */
    ret = validate_pvfs_object(
            fsck_options, 
            &lookup_resp.ref, 
            &creds, 
            &cur_fs,
            fsck_options->start_path);
            
    if (ret != 0)
    {
        printf("failed to validate object tree starting at [%s]. rc=[%d]\n",
               fsck_options->start_path, ret);
    }
    else
    {
        printf("pvfs2-validate done validating object tree at [%s]\n",
               fsck_options->start_path);
    }

    PVFS_fsck_finalize(fsck_options, &cur_fs, &creds);
    PVFS_sys_finalize();
    free(fsck_options);

    return 0;
}

/**
 * Validate a PVFS2 file, directory or symlink.  Operates recursively to
 * descend into directories.
 *
 * \retval 0 on success 
 * \retval -PVFS_EWARNING for non critical warnings
 * \retval -PVFS_error on failure
 */
int validate_pvfs_object(
    const struct PINT_fsck_options *fsck_options, /**< fsck options */
    const PVFS_object_ref * pref,                 /**< object to validate */
    const PVFS_credentials * creds,               /**< caller's credentials */
    int *cur_fs,                                  /**< file system */
    char *current_path)                           /**< path to object */
{
    int ret = 0;
    int j = 0;
    PVFS_sysresp_getattr attributes;
    PVFS_dirent *directory_entries = NULL;
    char* err_string = NULL;

    memset(&attributes, 0, sizeof(attributes));
    
    /* get this objects attributes */
    ret = PVFS_fsck_get_attributes(fsck_options, pref, creds, &attributes);
    if(ret < 0)
    {
        fprintf(stderr, "Error: [%s] cannot retrieve attributes.\n", current_path);
    }
    else if (attributes.attr.objtype == PVFS_TYPE_METAFILE)
    {
        /* metadata file */
        ret = PVFS_fsck_validate_metafile(fsck_options, pref,
            &attributes, creds);
    }
    else if (attributes.attr.objtype == PVFS_TYPE_DIRECTORY)
    {
        /* directory */
        directory_entries = calloc(attributes.attr.dirent_count, sizeof(PVFS_dirent));
        if (directory_entries == NULL)
        {
            perror("calloc");
            return -PVFS_ENOMEM;
        }

        ret = PVFS_fsck_validate_dir(fsck_options, pref, &attributes, creds,
            directory_entries);

        if(ret == 0)
        {
            /* validate all directory entries recursively */
            for (j = 0; j < attributes.attr.dirent_count; j++)
            {
                PVFS_object_ref obj_ref;
                char new_path[PVFS_SEGMENT_MAX];

                obj_ref.handle = directory_entries[j].handle;
                obj_ref.fs_id = *cur_fs;

                /* build full path name of the next object */
                strcpy(new_path, current_path);
                strcat(new_path, "/");
                strcat(new_path, directory_entries[j].d_name);

                /* recurse */
                ret = validate_pvfs_object(fsck_options, &obj_ref, creds, 
                    cur_fs, new_path);
            }
        }
        
        free(directory_entries);
    }
    else if (attributes.attr.objtype == PVFS_TYPE_SYMLINK)
    {
        /* symbolic link */
        ret = PVFS_fsck_validate_symlink(fsck_options, pref,
            &attributes);
        free(attributes.attr.link_target);
    }
    else
    {
        fprintf(stderr, "Error: [%s] is of an unknown object type: [%d]\n",
                current_path, attributes.attr.objtype);
        ret = -PVFS_EINVAL;
    }

    if (ret == 0)
    {
        if (fsck_options->verbose)
        {
            printf("validated [%s] object ret=[%d]\n", current_path, ret);
        }
    }
    else
    {
        err_string = (char*)malloc(128*sizeof(char));
        if(err_string)
        {
            PVFS_strerror_r(ret, err_string, 128);
            fprintf(stderr, "Error: [%s] object is invalid (%s)\n", 
                current_path, err_string);
            free(err_string);
        }
        else
        {
            fprintf(stderr, "Error: [%s] object is invalid (%d)\n", 
                current_path, ret);
        }
    }

    /* Return 0, rather than "ret", too keep from propogating errors
     * all the way back to the root object.  We want to continue and show all
     * problem rather than stopping on the first
     */
    return 0;
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage : %s [-Vvharsfc] -d pvfs2_directory\n", argv[0]);
    fprintf(stderr, "Recursively checks a PVFS2 directory or file for problems.\n");
    fprintf(stderr, "  -h \t print this help screen\n");
    fprintf(stderr, "  -d \t path to a PVFS2 directory/file\n");
    fprintf(stderr, "     \t (specify mount point to check entire file system)\n");
    fprintf(stderr, "  -c \t check for stranded objects\n");
    fprintf(stderr, "     \t (requires that -d refer to the PVFS2 root directory)\n");
    fprintf(stderr, "  -s \t check for bad practice in symbolic links\n");
    fprintf(stderr, "  -f \t check for bad practice in directory/file names\n");
    fprintf(stderr,
            "  -F \t stop after confirming consistent configuration of all servers\n");
    fprintf(stderr, "  -V \t run in verbose mode\n");
    fprintf(stderr, "  -v \t print version and exit\n");
    fprintf(stderr, "  -a \t fix all problems found (NOT IMPLEMENTED)\n");
    fprintf(stderr, "  -r \t run in interactive mode (NOT IMPLEMENTED)\n");
    fprintf(stderr, "\n\n");
    fprintf(stderr, "  Return Codes:\n");
    fprintf(stderr,
            "  \tThe exit code returned by %s is the sum of the following conditions:\n", argv[0]);
    fprintf(stderr, "  \t\t 0     - No errors\n");
    fprintf(stderr, "  \t\t 1     - File system errors corrected\n");
    fprintf(stderr, "  \t\t 2     - System should be rebooted\n");
    fprintf(stderr, "  \t\t 4     - File system errors left uncorrected\n\n");

    fprintf(stderr, "  Example: %s -d /mnt/pvfs2\n", argv[0]);
}

static struct PINT_fsck_options *parse_args(int argc, char **argv)
{
    int opt = 0;
    int path_length = 0;

    struct PINT_fsck_options *opts;
    opts = calloc(1, sizeof(struct PINT_fsck_options));
    if (opts == NULL)
    {
        return NULL;
    }

    while ((opt = getopt(argc, argv, "d:VvharsfcF")) != EOF)
    {
        switch (opt)
        {
        case 'd':
            opts->start_path = optarg;
            path_length = strlen(optarg);
            /* remove trailing extraneous */
            if (optarg[path_length - 1] == '/' && path_length > 1)
            {
                optarg[path_length - 1] = '\0';
            }
            break;
        case 'V':
            opts->verbose = 1;
            break;
        case 'v':
            printf("pvfs2-fsck version %s\n", VERSION);
            exit(0);
            break;
        case 'c':
            opts->check_stranded_objects = 1;
            break;
        case 'a':
            opts->fix_errors = 1;
            printf("Error: file system repair not implemented\n");
            usage(argc, argv);
            exit(-PVFS_ENOSYS);
            break;
        case 'r':
            printf("Error: interactive mode not implemented\n");
            usage(argc, argv);
            exit(-PVFS_ENOSYS);
            break;
        case 's':
            opts->check_symlink_target = 1;
            break;
        case 'F':
            opts->check_fs_configs = 1;
            break;
        case 'f':
            opts->check_dir_entry_names = 1;
            break;
        case 'h':
            usage(argc, argv);
            exit(0);
            break;
        case '?':
            usage(argc, argv);  /* unknown option */
            exit(3);
        }
    }

    return opts;
}

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
