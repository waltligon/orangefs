/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>

#include "fsck-utils.h"

#define HANDLE_BATCH 1000
#define MAX_DIR_ENTS 64

#define SERVER_CONFIG_BUFFER_SIZE 5000
#define FS_CONFIG_BUFFER_SIZE 10000

/** \file
 *  \ingroup fsckutils
 * Implementation of the fsck-utils component.
 *
 * TODO's:
 * - What action should be taken to recover broken on missing PVFS2 objects? Can
 *   we recover pieces and zero out unknown? Can we create missing metadata 
 *   for data files that are orphaned? Do we automatically move to lost+found
 *   directory, and/or allow option to simply remove and delete?
 * - What needs to be done for extended attributes
 * - What needs to be done for access control lists (extended attributes & 
 *   accounts)
 * .
 *
 * Terminology:
 * - Orphaned bstreams: bstreams with no dfiles/attributes
 * - Stranded Object: An object exists with no way to get to it 
 *      - Datafile missing metadata
 *      - Metadata missing directory entry 
 *      - Dirdata missing directory object
 * - Dangling directory entry: directory entry exists, but object/attributes don't
 * .
 */

static void set_return_code(
    int *ret,               
    const int retval);
    
static int compare_handles(
    const void *handle1,
    const void *handle2);

/** The following declarations deal with the option to check for stranded objects */
struct PINT_handle_wrangler_handlelist
{
    int num_servers;
    PVFS_handle **list_array;
    char **list_array_seen;
    int *size_array;
    int *used_array;
    int *stranded_array;
    PVFS_BMI_addr_t *addr_array;
} PINT_handle_wrangler_handlelist;

#if 0
/* not used yet */
static int PINT_handle_wrangler_get_stranded_handles(
    const PVFS_fs_id * cur_fs, 
    int *num_stranded_handles, 
    PVFS_handle ** stranded_handles);
#endif

static int PINT_handle_wrangler_display_stranded_handles(
    const struct PINT_fsck_options *fsck_options,
    const PVFS_fs_id * cur_fs,                   
    const PVFS_credentials * creds);

static int PINT_handle_wrangler_load_handles(
    const struct PINT_fsck_options *fsck_options,
    const PVFS_fs_id * cur_fs,                   
    const PVFS_credentials * creds);

static int PINT_handle_wrangler_remove_handle(
    const PVFS_handle * handle,
    const PVFS_fs_id * cur_fs);

/**
 * Initializes API and checks options for correctness 
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_initialize(
    const struct PINT_fsck_options *fsck_options,   /**< Populated options */
    const PVFS_credentials * creds,                 /**< Populated creditials structure */
    const PVFS_fs_id * cur_fs)                      /**< filesystem id */
{
    int ret = 0;

    if(fsck_options->fix_errors)
    {
        return(-PVFS_ENOSYS);
    }

    /* If check stranded objects... Setup handle stuff */
    if (fsck_options->check_stranded_objects)
    {
        /* get all handles from all servers */
        ret = PINT_handle_wrangler_load_handles(fsck_options, cur_fs, creds);
    }

    return ret;
}

/**
 * Verifies the same fs config is present on each pvfs2 server.
 * Ignores extraneous whitespace and comments begining with #.  Does not stop
 * on the first problem- will show any config differences, using the first
 * server as the golden standard.
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_check_server_configs(
    const struct PINT_fsck_options *fsck_options,   /**< Populated options */
    const PVFS_credentials * creds,                 /**< pvfs2 credentials structure */
    const PVFS_fs_id * cur_fs)                      /**< the current fs */
{
    int ret = 0;
    int num_servers = 0;
    int i = 0;
    int server_type = 0;
    PVFS_BMI_addr_t *addresses = NULL;
    char *fs_config = NULL;
    char *fs_config_diff = NULL;
    char *reference_config_server = NULL;
    const char *server_name = NULL;
    FILE *pin = NULL;
    char line[130] = { 0 };
    char *cmd = NULL;
    /* temp file name and file descriptor to store our reference config */
    char reference_fs_config_tmp_file[] = "/tmp/pvfs2-fsck.XXXXXX";
    int fout = 0;
    int final_ret = 0;

    /* count how many servers we have */
    ret = PVFS_mgmt_count_servers(*cur_fs,
                                (PVFS_credentials *) creds,
                                PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                                &num_servers);
    if(ret < 0)
    {
        PVFS_perror_gossip("PVFS_mgmt_count_servers", ret);
        return ret; 
    }

    addresses = 
        (PVFS_BMI_addr_t *) calloc(num_servers, sizeof(PVFS_BMI_addr_t));
    if (addresses == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* get a list of the pvfs2 servers */
    ret = PVFS_mgmt_get_server_array(
        *cur_fs,
        (PVFS_credentials *) creds,
        PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
        addresses, &num_servers);
    if(ret < 0)
    {
        free(addresses);
        PVFS_perror_gossip("PVFS_mgmt_get_server_array", ret);
        return ret;
    }

    for (i = 0; i < num_servers; i++)
    {
        server_type = 0;
        server_name = NULL;

        /* get the pretty server name */
        server_name = PINT_cached_config_map_addr(
                        *cur_fs, addresses[i], &server_type);
        assert(server_name);

        fs_config = calloc(FS_CONFIG_BUFFER_SIZE, sizeof(char));
        if (fs_config == NULL)
        {
            free(addresses);
            return -PVFS_ENOMEM;
        }
        
        ret = PVFS_mgmt_get_config(cur_fs,
                                 &addresses[i],
                                 fs_config,
                                 FS_CONFIG_BUFFER_SIZE);
        if (ret < 0)
        {
            PVFS_perror_gossip("PVFS_mgmt_get_config", ret);
            gossip_err("Error: failed to retrieve configuration on server %d of %d.\n", 
                i, num_servers);
            free(addresses);
            free(fs_config);
            return(ret);
        }

        if (i == 0)
        {
            /* store the "reference" server name for nice output later */
            reference_config_server = calloc(1, strlen(server_name) + 1);
            if (reference_config_server == NULL)
            {
                free(addresses);
                free(fs_config);
                return -PVFS_ENOMEM;
            }
            strcpy(reference_config_server, server_name);

            /* store the first server config as the reference */
            fout = mkstemp(reference_fs_config_tmp_file);
            if (fout < 0)
            {
                ret = -errno;
                gossip_err("Error opening temp file [%s]\n",
                        reference_fs_config_tmp_file);
                free(addresses);
                free(fs_config);
                free(reference_config_server);
                return ret;
            }

            ret = write(fout, fs_config, strlen(fs_config));
            if (ret < 0)
            {
                ret = -errno;
                gossip_err("Error: could not write reference configuration.\n"); 
                free(addresses);
                free(fs_config);
                free(reference_config_server);
                return(ret);
            }

            close(fout);
        }
        else
        {
            /* allocate enough space to hold our diff command */
            cmd = calloc(1,
                    strlen(fs_config) + strlen(reference_config_server) +
                    strlen(server_name) + 1000);
            if (cmd == NULL)
            {
                free(addresses);
                free(fs_config);
                free(reference_config_server);
                return -PVFS_ENOMEM;
            }

            /* build the diff command */
            sprintf(cmd,
                    "echo \"%s\"| diff -EbBu -I '^#.*' -L %s -L %s %s -",
                    fs_config,
                    reference_config_server,
                    server_name, reference_fs_config_tmp_file);

            pin = popen(cmd, "r");
            if (pin == NULL)
            {
                ret = -errno;
                gossip_err("Error: failed popen() for command: %s\n", cmd);
                free(addresses);
                free(fs_config);
                free(reference_config_server);
                free(cmd);
                return ret;
            }

            fs_config_diff = calloc(1, strlen(fs_config) + 100);
            if (fs_config_diff == NULL)
            {
                free(addresses);
                free(fs_config);
                free(reference_config_server);
                free(cmd);
                return -PVFS_ENOMEM;
            }

            /* get the output from diff */
            while (fgets(line, sizeof line, pin))
            {
                strcat(fs_config_diff, line);
            }
            
            ret = pclose(pin);
            if(ret != 0)
            {
                gossip_err("Error: failed popen() for command: %s\n", cmd);
                free(addresses);
                free(fs_config);
                free(reference_config_server);
                free(cmd);
                free(fs_config_diff);
                return(-PVFS_EINVAL);
            }

            /* if diff shows any problems, display it but keep going (we want
             * to see all config file problems if there is more than one)
             */
            if (strlen(fs_config_diff) > 0)
            {
                gossip_err("Error: File system config on [%s] doesn't\n",
                       server_name);
                gossip_err("   match reference config from [%s]:\n\n%s\n",
                       reference_config_server, fs_config_diff);

                final_ret = -PVFS_EINVAL;
            }

            free(fs_config_diff);
            free(cmd);
        }

        free(fs_config);
    }

    unlink(reference_fs_config_tmp_file);
    free(reference_config_server);
    free(addresses);

    return final_ret;
}

/**
 * Performs sanity checking on the PVFS_TYPE_DATAFILE PVFS_Object type
 * Checks the following:
 * - Existence of attributes 
 * .
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dfile(
    const struct PINT_fsck_options *fsck_options, /**< generic fsck options */
    const PVFS_handle * handle,                   /**< The dfile handle */
    const PVFS_fs_id * cur_fs,        /**< The fsid the handle belongs to */
    const PVFS_credentials * creds,   /**< Populated creditials structure */
    PVFS_size * dfiles_total_size)    /**< Total size of all dfiles */
{
    int ret = 0;
    PVFS_object_ref obj_ref;
    PVFS_sysresp_getattr dfile_attributes;
    int err = 0;
    
    memset(&dfile_attributes, 0, sizeof(dfile_attributes));

    if (fsck_options->check_stranded_objects)
    {
        if (PINT_handle_wrangler_remove_handle(handle, cur_fs))
        {
            gossip_err("WARNING: unable to remove handle [%llu] from handle list while verifying stranded objects\n",
                    llu(*handle));
        }
    }

    /* Build the needed PVFS_Object reference needed for API calls */
    obj_ref.handle = *handle;
    obj_ref.fs_id = *cur_fs;

    /* Check for existence of attributes */
    err = PVFS_fsck_get_attributes(fsck_options, &obj_ref, creds,
        &dfile_attributes);
    if(err < 0)
    {
        gossip_err("Error: unable to get dfile attributes\n");
        return(err);
    }
    set_return_code(&ret, err);

    /* total up the dfile sizes. */
    *dfiles_total_size += dfile_attributes.attr.size;

    /* Do attributes contain valid data */
    err = PVFS_fsck_validate_dfile_attr(fsck_options, &dfile_attributes);
    if(err < 0)
    {
        gossip_err("Error: dfile has invalid attributes\n");
        return(err);
    }
    set_return_code(&ret, err);

    return ret;
}

/**
 * Performs validity checking for the attributes for PVFS_TYPE_DATAFILE
 * Checks the following:
 * - Object Type must be PVFS_TYPE_DFILE
 * - size >=  0 
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dfile_attr(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_sysresp_getattr * getattr_resp)      /**< DFILE attributes */
{
    int ret = 0;

    if (getattr_resp->attr.objtype != PVFS_TYPE_DATAFILE)
    {
        gossip_err(
                "dfile object type [%d] does not match expected type PVFS_TYPE_DATAFILE %d\n",
                getattr_resp->attr.objtype, PVFS_TYPE_DATAFILE);

        set_return_code(&ret, -PVFS_EINVAL);
    }

    if (getattr_resp->attr.size < 0)
    {
        /* invalid size attribute */
        gossip_err("Error: dfile size [%lld] is invalid.\n",
                lld(getattr_resp->attr.size));

        set_return_code(&ret, -PVFS_EINVAL);
    }

    return ret;
}

/**
 * Performs sanity checking on the PVFS_TYPE_METAFILE PVFS_Object type
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_metafile(
    const struct PINT_fsck_options *fsck_options, /**< generic fsck options */
    const PVFS_object_ref * obj_ref,              /**< PVFS_Object reference */
    const PVFS_sysresp_getattr * attributes,      /**< METAFILE attributes */
    const PVFS_credentials * creds)               /**< Populated creditials structure */
{
    int ret = 0;
    int i = 0;
    PVFS_handle *df_handles;
    PVFS_size dfiles_total_size = 0;
    int err = 0;

    if (fsck_options->check_stranded_objects)
    {
        if (PINT_handle_wrangler_remove_handle
            (&obj_ref->handle, &obj_ref->fs_id))
        {
            gossip_err("WARNING: unable to remove handle [%llu] from \
                handle list while verifying stranded objects\n", llu(obj_ref->handle));
        }
    }

    /* Check for validity of attributes */
    err = PVFS_fsck_validate_metafile_attr(fsck_options, attributes);
    if(err < 0)
    {
        gossip_err("Error: metafile has invalid attributes\n");
        return(err);
    }
    set_return_code(&ret, err);

    df_handles = (PVFS_handle *) calloc(attributes->attr.dfile_count,
                                        sizeof(PVFS_handle));
    if (df_handles == NULL)
    {
        return -PVFS_ENOMEM;
    }

    err = PVFS_mgmt_get_dfile_array(*obj_ref,
                                  (PVFS_credentials *) creds,
                                  df_handles, attributes->attr.dfile_count, NULL);
    if(err < 0)
    {
        PVFS_perror("PVFS_mgmt_get_dfile_array", err);
        free(df_handles);
        return(err);
    }

    /* verify dfiles */
    for (i = 0; i < attributes->attr.dfile_count; i++)
    {
        err = PVFS_fsck_validate_dfile(fsck_options,
                                     &df_handles[i],
                                     &obj_ref->fs_id,
                                     creds, &dfiles_total_size);
        if(err < 0)
        {
            gossip_err("Error: metafile dfile [%d] is invalid\n", i);
            free(df_handles);
            return(err);
        }
        set_return_code(&ret, err);
    }

    if (dfiles_total_size > attributes->attr.size)
    {
        gossip_err(
                "Error: sum size of dfiles [%lld] is greater than expected size of [%lld]\n",
                lld(dfiles_total_size), lld(attributes->attr.size));

        free(df_handles);
        return(-PVFS_EINVAL);
    }

    free(df_handles);
    return ret;
}

/**
 * Performs validity checking for the attributes for PVFS_TYPE_METAFILE
 * Checks the following:
 * - Object type must be PVFS_TYPE_METAFILE
 * - Existence of dfiles
 * - Existence of distribution
 * - size >= 0
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_metafile_attr(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_sysresp_getattr * attributes)        /**< METAFILE attributes */
{
    int ret = 0;

    /* check attributes */
    if (attributes->attr.objtype != PVFS_TYPE_METAFILE)
    {
        gossip_err(
                "Error: metafile type [%d] does not match expected type PVFS_TYPE_METAFILE %d\n",
                attributes->attr.objtype, PVFS_TYPE_METAFILE);

        set_return_code(&ret, -PVFS_EINVAL);
    }

    /* verify we have dfiles */
    if (attributes->attr.dfile_count <= 0)
    {
        gossip_err("Error: metafile has an invalid number of dfiles [%d]\n",
                attributes->attr.dfile_count);

        set_return_code(&ret, -PVFS_EINVAL);
    }

    /* TODO: check to make sure that dfile array and dist is present. */

    return ret;
}

/**
 * Performs sanity checking on the PVFS_TYPE_SYMLINK PVFS_Object type
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_symlink(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_object_ref * obj_ref,                /**< PVFS_Object reference */
    const PVFS_sysresp_getattr * attributes)        /**< SYMLINK attributes */
{
    int ret = 0;

    if (fsck_options->check_stranded_objects)
    {
        if (PINT_handle_wrangler_remove_handle
            (&obj_ref->handle, &obj_ref->fs_id))
        {
            gossip_err("WARNING: unable to remove handle [%llu] from handle \
                    list while verifying stranded objects\n", llu(obj_ref->handle));
        }
    }

    ret = PVFS_fsck_validate_symlink_attr(fsck_options, attributes);

    return ret;
}

/**
 * Performs validity checking for the attributes for PVFS_TYPE_SYMLINK
 * Checks the following:
 * - Object type must be PVFS_TYPE_SYMLINK
 * - Target must be non-null
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_symlink_attr(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_sysresp_getattr * attributes)        /**< SYMLINK attributes */
{
    int ret = 0;
    int err = 0;

    /* check attributes */
    if (attributes->attr.objtype != PVFS_TYPE_SYMLINK)
    {
        gossip_err(
                "Error: symlink type [%d] does not match expected type PVFS_TYPE_SYMLINK [%d]\n",
                attributes->attr.objtype, PVFS_TYPE_SYMLINK);

        set_return_code(&ret, -PVFS_EINVAL);
    }

    if (attributes->attr.link_target)
    {
        if (fsck_options->check_symlink_target)
        {
            /* we do have a target, make sure it's valid */
            err = PVFS_fsck_validate_symlink_target(fsck_options, attributes);
            if (err < 0)
            {
                gossip_err("Error: symlink target [%s] is invalid\n",
                        attributes->attr.link_target);
                return(err);
            }
            set_return_code(&ret, err);
        }
    }
    else
    {
        gossip_err("Error: symlink missing target attribute\n");
        set_return_code(&ret, -PVFS_EINVAL);
    }

    return ret;
}

/**
 * Performs "bad practice" warning checks for the target attributes for 
 * PVFS_TYPE_SYMLINK. Checks the following:
 * - Does target exist
 * - Does target back out of PVFS2 filesystem
 * - Does target use absolute path
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_symlink_target(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_sysresp_getattr * attributes)        /**< SYMLINK attributes */
{
    int ret = 0;

    /* TODO: finish other tests */

    if (attributes->attr.link_target[0] == '/')
    {
        gossip_err("WARNING: symlink target [%s] uses absolute path\n",
                attributes->attr.link_target);
    }

    return ret;
}

/**
 * Performs sanity checking on the PVFS_TYPE_DIRDATA PVFS_Object type.
 * Checks the following:
 * - Do attributes exist
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dirdata(
    const struct PINT_fsck_options *fsck_options, /**< generic fsck options */
    const PVFS_handle * handle,     /**< The dirdata handle */
    const PVFS_fs_id * cur_fs,      /**< The fsid the handle belongs to */
    const PVFS_credentials * creds) /**< Populated creditials structure */
{
    int ret = 0;
    int err = 0;
    PVFS_sysresp_getattr dirdata_attributes;
    PVFS_object_ref obj_ref;

    memset(&dirdata_attributes, 0, sizeof(dirdata_attributes));

    obj_ref.handle = *handle;
    obj_ref.fs_id = *cur_fs;

    if (fsck_options->check_stranded_objects)
    {
        if (PINT_handle_wrangler_remove_handle(handle, cur_fs))
        {
            gossip_err("WARNING: unable to remove handle [%llu] from handle \
                    list while verifying stranded objects\n", llu(*handle));
        }
    }

    err = PVFS_fsck_get_attributes
        (fsck_options, &obj_ref, creds, &dirdata_attributes);
    if(err < 0)
    {
        gossip_err("Error: failed to get attributes for dirdata object\n");
        return(err);
    }
    set_return_code(&ret, err);

    err = PVFS_fsck_validate_dirdata_attr(fsck_options, &dirdata_attributes);
    if(err < 0)
    {
        gossip_err("Error: dirdata entry has invalid attributes\n");
        return(err);
    }
    set_return_code(&ret, err);

    return ret;
}

/**
 * Performs validity checking for the attributes for PVFS_TYPE_DIRDATA.
 * Checks the following:
 * - Object type must be PVFS_TYPE_DIRDATA
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dirdata_attr(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_sysresp_getattr * attributes)        /**< DIRDATA attributes */
{
    int ret = 0;

    if (attributes->attr.objtype != PVFS_TYPE_DIRDATA)
    {
        gossip_err(
                "Error: dirdata type [%d] does not match expected type PVFS_TYPE_DIRDATA %d\n",
                attributes->attr.objtype, PVFS_TYPE_DIRDATA);

        set_return_code(&ret, -PVFS_EINVAL);
    }

    return ret;
}

/**
 * Performs sanity checking on the PVFS_TYPE_DIRECTORY PVFS_Object type.
 * Checks the following:
 * - gets and validates directory entry filenames
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dir(
    const struct PINT_fsck_options *fsck_options, /**< generic fsck options */
    const PVFS_object_ref * obj_ref,         /**< DIRECTORY object reference */
    const PVFS_sysresp_getattr * attributes, /**< DIRECTORY attributes */
    const PVFS_credentials * creds,          /**< populated creditials structure */
    PVFS_dirent * directory_entries)         /**< \return readdir response */
{
    int ret = 0;
    int i = 0;
    int err = 0;
    int current_dir_entry = 0;
    PVFS_ds_position token = PVFS_READDIR_START;
    PVFS_sysresp_readdir readdir_resp;
    PVFS_handle dirdata_handle;

    if (fsck_options->check_stranded_objects)
    {
        if (PINT_handle_wrangler_remove_handle
            (&obj_ref->handle, &obj_ref->fs_id))
        {
            gossip_err("WARNING: unable to remove handle [%llu] from \
                    handle list while verifying stranded objects\n", llu(obj_ref->handle));
        }
    }

    err = PVFS_fsck_validate_dir_attr(fsck_options, attributes);
    if(err < 0)
    {
        gossip_err("Error: directory has invalid attributes\n");
        return(err);
    }
    set_return_code(&ret, err);

    /* get the dirdata handle and validate */
    err = PVFS_mgmt_get_dirdata_handle
        (*obj_ref, &dirdata_handle, (PVFS_credentials *) creds, NULL);
    if(err < 0)
    {
        gossip_err("Error: unable to get dirdata handle\n");
        return(err);
    }

    err = PVFS_fsck_validate_dirdata
        (fsck_options, &dirdata_handle, &obj_ref->fs_id, creds);
    if(err < 0)
    {
        gossip_err("Error: directory dirdata is invalid\n");
        return(err);
    }
    set_return_code(&ret, err);

    /* get and validate all directory entries */
    do
    {
        memset(&readdir_resp, 0, sizeof(PVFS_sysresp_readdir));

        err = PVFS_sys_readdir(*obj_ref,
                             token,
                             MAX_DIR_ENTS,
                             (PVFS_credentials *) creds, &readdir_resp, NULL);
        if(err < 0)
        {
            gossip_err("Error: could not read directory entries\n");
            return(err);
        }

        for (i = 0; i < readdir_resp.pvfs_dirent_outcount; i++)
        {
            strncpy(directory_entries[current_dir_entry].d_name,
                    readdir_resp.dirent_array[i].d_name, PVFS_NAME_MAX + 1);

            directory_entries[current_dir_entry].handle = 
                readdir_resp.dirent_array[i].handle;
            current_dir_entry++;

            if (fsck_options->check_dir_entry_names)
            {
                err = PVFS_fsck_validate_dir_ent(fsck_options,
                                                    readdir_resp.
                                                    dirent_array[i].d_name);
                /* continue even if we hit errors; we want to see all entries */
                if (err < 0)
                {
                    gossip_err("Error: directory entry [%s] is invalid\n",
                            readdir_resp.dirent_array[i].d_name);
                }
                set_return_code(&ret, err);
            }
        }

        free(readdir_resp.dirent_array);
        token = readdir_resp.token;

    } while (readdir_resp.pvfs_dirent_outcount == MAX_DIR_ENTS);

    return ret;
}

/**
 * Performs validity checking for the attributes for PVFS_TYPE_DIRECTORY.
 * Checks the following:
 * - Object type must be PVFS_TYPE_DIRECTORY
 * - dirent_count must be >= 0
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dir_attr(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_sysresp_getattr * attributes)        /**< DIRECTORY attributes */
{
    int ret = 0;

    if (attributes->attr.objtype != PVFS_TYPE_DIRECTORY)
    {
        gossip_err("Error: directory type [%d] does not \
            match expected type PVFS_TYPE_DIRECTORY %d\n", attributes->attr.objtype, PVFS_TYPE_DIRECTORY);

        set_return_code(&ret, -PVFS_EINVAL);
    }

    if (attributes->attr.dirent_count < 0)
    {
        gossip_err("Error: directory entry count [%lld] is invalid\n",
                lld(attributes->attr.dirent_count));

        set_return_code(&ret, -PVFS_EINVAL);
    }

    return ret;
}

/**
 * Performs validity checking for the PVFS_TYPE_DIRECTORY directory entries
 * Checks the following:
 * - invalid characters in entry names
 * - entry_names <= PVFS2_SEGMENT_MAX
 * - warnings for characters that tend to confuse shells
 * .
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_validate_dir_ent(
    const struct PINT_fsck_options *fsck_options, /**< generic fsck options */
    const char *filename)              /**< Filename associated with handle */
{
    const char *cp;
    int ret = 0;

    if (strlen(filename) > PVFS_SEGMENT_MAX)
    {
        gossip_err(
                "Error: directory entry [%s] length is > PVFS_SEGMENT_MAX [%d]\n",
                filename, PVFS_SEGMENT_MAX);
        set_return_code(&ret, -PVFS_EINVAL);
    }

    if (strspn(filename, "/") > 0)
    {
        gossip_err("Error: directory entry [%s] contains invalid / character \n",
                filename);
        set_return_code(&ret, -PVFS_EINVAL);
    }

    cp = filename;
    while (*cp)
    {
        /* isprint is ' ' through ~ in ASCII; no tabs or newlines */
        if (!isprint(*cp))
        {
            gossip_err("WARNING: directory entry [%s] contains odd character\n",
                       filename);
            break;
        }
        cp++;
    }

    return ret;
}

/**
 * Get a pvfs2 objects attributes. 
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_get_attributes(
    const struct PINT_fsck_options *fsck_options,   /**< generic fsck options */
    const PVFS_object_ref * pref, /**< object reference requesting attributes */
    const PVFS_credentials * creds,      /**< populated credentials structure */
    PVFS_sysresp_getattr * getattr_resp) /**< attribute structure to populate */
{
    time_t r_atime, r_mtime, r_ctime;
    int ret = 0;

    ret = PVFS_sys_getattr
        (*pref, PVFS_ATTR_SYS_ALL, (PVFS_credentials *) creds, getattr_resp, NULL);
    if(ret < 0)
    {
        gossip_err("Error: unable to retrieve attributes\n");
        return(ret);
    }

    r_atime = (time_t) getattr_resp->attr.atime;
    r_mtime = (time_t) getattr_resp->attr.mtime;
    r_ctime = (time_t) getattr_resp->attr.ctime;

    /* if the gossip fsck debug mask is enabled, then print detailed
     * information about the attributes
     */
    gossip_debug(GOSSIP_FSCK_DEBUG, "\tFSID        : %d\n", (int) pref->fs_id);
    gossip_debug(GOSSIP_FSCK_DEBUG, "\tHandle      : %llu\n", llu(pref->handle));

    if (getattr_resp->attr.mask & PVFS_ATTR_SYS_COMMON_ALL)
    {
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tuid         : %d\n", getattr_resp->attr.owner);
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tgid         : %d\n", getattr_resp->attr.group);
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tperms       : %o\n", getattr_resp->attr.perms);
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tatime       : %s", ctime(&r_atime));
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tmtime       : %s", ctime(&r_mtime));
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tctime       : %s", ctime(&r_ctime));

    }

    if (getattr_resp->attr.objtype == PVFS_TYPE_SYMLINK)
    {
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\ttarget:     : %s\n", getattr_resp->attr.link_target);
    }

    if (getattr_resp->attr.mask & PVFS_ATTR_SYS_SIZE)
    {
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tfile size   : %lld\n", lld(getattr_resp->attr.size));
    }

    if (getattr_resp->attr.mask & PVFS_ATTR_SYS_DFILE_COUNT)
    {
        gossip_debug(GOSSIP_FSCK_DEBUG, 
            "\tdfile count : %d\n", getattr_resp->attr.dfile_count);
    }

    gossip_debug(GOSSIP_FSCK_DEBUG, "\tobject type : ");

    switch (getattr_resp->attr.objtype)
    {
    case PVFS_TYPE_NONE:
        gossip_debug(GOSSIP_FSCK_DEBUG, "none\n");
        break;
    case PVFS_TYPE_METAFILE:
        gossip_debug(GOSSIP_FSCK_DEBUG, "meta file\n");
        break;
    case PVFS_TYPE_DATAFILE:
        gossip_debug(GOSSIP_FSCK_DEBUG, "data file\n");
        break;
    case PVFS_TYPE_DIRECTORY:
        gossip_debug(GOSSIP_FSCK_DEBUG, "directory\n");
        break;
    case PVFS_TYPE_SYMLINK:
        gossip_debug(GOSSIP_FSCK_DEBUG, "symlink\n");
        break;
    case PVFS_TYPE_DIRDATA:
        gossip_debug(GOSSIP_FSCK_DEBUG, "dirdata\n");
        break;
    case PVFS_TYPE_INTERNAL:
        gossip_debug(GOSSIP_FSCK_DEBUG, "internal\n");
        break;
    }
    gossip_debug(GOSSIP_FSCK_DEBUG, "\n");

    return ret;
}

/**
 * Performs final steps of fsck.  If option was enabled to check for stranded
 * objects, then it will print any leftover objects.
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
int PVFS_fsck_finalize(
    const struct PINT_fsck_options *fsck_options,     /**< Populated options */
    const PVFS_fs_id * cur_fs,           /**< The fsid the handle belongs to */
    const PVFS_credentials * creds)     /**< populated credentials structure */
{
    /* display leftover handles */
    if (fsck_options->check_stranded_objects)
    {
        PINT_handle_wrangler_display_stranded_handles(fsck_options, cur_fs,
                                                      creds);
    }

    return(0);
}

/** 
 * Gets a list of all the handles used from the PVFS2 servers. The PVFS2 system
 * interface must have already been initialized.
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
static int PINT_handle_wrangler_load_handles(
    const struct PINT_fsck_options *fsck_options, /**< Populated options */
    const PVFS_fs_id * cur_fs,                    /**< fs_id */
    const PVFS_credentials * creds)   /**< populdated credentials structure */
{
    int ret = 0;
    int server_count;
    struct PVFS_mgmt_server_stat *stat_array = NULL;
    PVFS_handle **handle_matrix = NULL;
    int i = 0;
    int *handle_count_array = NULL;
    PVFS_ds_position *position_array = NULL;
    int more_handles = 0;
    int err = 0;

    gossip_debug(GOSSIP_FSCK_DEBUG, 
        "getting all file handles from filesystem\n");

    memset(&PINT_handle_wrangler_handlelist, 0,
        sizeof(PINT_handle_wrangler_handlelist));

    /* count how many servers we have */
    err = PVFS_mgmt_count_servers(*cur_fs,
                                (PVFS_credentials *) creds,
                                PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                                &server_count);
    if(err < 0)
    {
        PVFS_perror_gossip("PVFS_mgmt_count_servers", err);
        return err;
    }

    PINT_handle_wrangler_handlelist.num_servers = server_count;

    PINT_handle_wrangler_handlelist.addr_array =
        (PVFS_BMI_addr_t *) calloc(server_count, sizeof(PVFS_BMI_addr_t));
    if (PINT_handle_wrangler_handlelist.addr_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    /* get a list of the pvfs2 servers */
    err = PVFS_mgmt_get_server_array(*cur_fs,
                               (PVFS_credentials *) creds,
                               PVFS_MGMT_IO_SERVER | PVFS_MGMT_META_SERVER,
                               PINT_handle_wrangler_handlelist.addr_array,
                               &server_count);
    if(err < 0)
    {
        PVFS_perror_gossip("PVFS_mgmt_get_server_array", err);
        ret = err;
        goto load_handles_error;
    }

    stat_array = (struct PVFS_mgmt_server_stat *)
        calloc(server_count, sizeof(struct PVFS_mgmt_server_stat));
    if (stat_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    /* this gives us a count of the handles on each server */
    err = PVFS_mgmt_statfs_list(*cur_fs,
                          (PVFS_credentials *) creds,
                          stat_array,
                          PINT_handle_wrangler_handlelist.addr_array,
                          server_count, NULL, NULL);
    if(err < 0)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    /* allocate big chunks of memory to keep up with handles */
    handle_count_array = (int *) calloc(server_count, sizeof(int));
    if (handle_count_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    position_array = (PVFS_ds_position *) calloc(server_count, 
                                                 sizeof(PVFS_ds_position));
    if (position_array == NULL)
    {        
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    PINT_handle_wrangler_handlelist.list_array =
        (PVFS_handle **) calloc(server_count, sizeof(PVFS_handle *));
    if (PINT_handle_wrangler_handlelist.list_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }
    memset(PINT_handle_wrangler_handlelist.list_array, 0, 
        server_count*sizeof(PVFS_handle*));

    PINT_handle_wrangler_handlelist.list_array_seen =
        (char **) calloc(server_count, sizeof(char *));
    if (PINT_handle_wrangler_handlelist.list_array_seen == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }
    memset(PINT_handle_wrangler_handlelist.list_array_seen, 0, 
        server_count*sizeof(char*));

    PINT_handle_wrangler_handlelist.size_array =
        (int *) calloc(server_count, sizeof(int));
    if (PINT_handle_wrangler_handlelist.size_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    PINT_handle_wrangler_handlelist.used_array =
        (int *) calloc(server_count, sizeof(int));
    if (PINT_handle_wrangler_handlelist.used_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    PINT_handle_wrangler_handlelist.stranded_array =
        (int *) calloc(server_count, sizeof(int));
    if (PINT_handle_wrangler_handlelist.stranded_array == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }

    /* temporary array to keep state while fetching handles */
    handle_matrix =
        (PVFS_handle **) calloc(server_count, sizeof(PVFS_handle *));
    if (handle_matrix == NULL)
    {
        ret = -PVFS_ENOMEM;
        goto load_handles_error;
    }
    memset(handle_matrix, 0, server_count*sizeof(PVFS_handle*));

    /* populating a nice "handlelist" struct with all this various handle data */
    for (i = 0; i < server_count; i++)
    {
        PINT_handle_wrangler_handlelist.size_array[i] =
            stat_array[i].handles_total_count -
            stat_array[i].handles_available_count;

        PINT_handle_wrangler_handlelist.used_array[i] = 0;

        PINT_handle_wrangler_handlelist.list_array[i] =
            (PVFS_handle *) calloc(PINT_handle_wrangler_handlelist.size_array[i],
                                   sizeof(PVFS_handle));
        if (PINT_handle_wrangler_handlelist.list_array[i] == NULL)
        {
            ret = -PVFS_ENOMEM;
            goto load_handles_error;
        }

        PINT_handle_wrangler_handlelist.list_array_seen[i] =
            (char *) calloc(PINT_handle_wrangler_handlelist.size_array[i],
                            sizeof(char));
        if (PINT_handle_wrangler_handlelist.list_array_seen[i] == NULL)
        {
            ret = -PVFS_ENOMEM;
            goto load_handles_error;
        }

        handle_matrix[i] =
            (PVFS_handle *) calloc(PINT_handle_wrangler_handlelist.size_array[i],
                                   sizeof(PVFS_handle));
        if (handle_matrix[i] == NULL)
        {
            ret = -PVFS_ENOMEM;
            goto load_handles_error;
        }

        position_array[i] = PVFS_ITERATE_START;
        handle_count_array[i] = HANDLE_BATCH;
    }

    /* repeatedly grab handles until we get them all */
    do
    {
        /* mgmt call to get block of handles */
        err = PVFS_mgmt_iterate_handles_list(*cur_fs,
                                             (PVFS_credentials *) creds,
                                             handle_matrix,
                                             handle_count_array,
                                             position_array,
                                             PINT_handle_wrangler_handlelist.addr_array,
                                             server_count,
                                             0,
                                             NULL,
                                             NULL);
        if(err < 0)
        {
            PVFS_perror_gossip("PVFS_mgmt_iterate_handles", err);
            ret = err;
            goto load_handles_error;
        }

        more_handles = 0;

        for (i = 0; i < server_count; i++)
        {
            /* add this block of handles to the full list */
            int j = 0;
            for (j = 0; j < handle_count_array[i]; j++)
            {
                PINT_handle_wrangler_handlelist.list_array[i]
                    [PINT_handle_wrangler_handlelist.used_array[i] + j] =
                    handle_matrix[i][j];
            }

            PINT_handle_wrangler_handlelist.used_array[i] +=
                handle_count_array[i];

            /* are there more handles? */
            if (position_array[i] != PVFS_ITERATE_END)
            {
                more_handles = 1;
            }
        }
    } while (more_handles != 0);

    for (i = 0; i < server_count; i++)
    {
        /* sort the list of handles */
        qsort(PINT_handle_wrangler_handlelist.list_array[i],
              PINT_handle_wrangler_handlelist.used_array[i],
              sizeof(PVFS_size), compare_handles);

        /* we will decrement this during the actual fsck as we check each handle */
        PINT_handle_wrangler_handlelist.stranded_array[i] =
            PINT_handle_wrangler_handlelist.used_array[i];
    }

    /* now look for reserved handles from each server */
    for (i = 0; i < server_count; i++)
    {
        position_array[i] = PVFS_ITERATE_START;
        handle_count_array[i] = HANDLE_BATCH;
    }

    do
    {
        err = PVFS_mgmt_iterate_handles_list(*cur_fs,
                                             (PVFS_credentials *) creds,
                                             handle_matrix,
                                             handle_count_array,
                                             position_array,
                                             PINT_handle_wrangler_handlelist.addr_array,
                                             server_count,
                                             PVFS_MGMT_RESERVED,
                                             NULL,
                                             NULL);
        if(err < 0)
        {
            PVFS_perror_gossip("PVFS_mgmt_iterate_handles", err);
            ret = err;
            goto load_handles_error;
        }

        more_handles = 0;

        for (i = 0; i < server_count; i++)
        {
            /* remove these handles */
            int j = 0;
            for (j = 0; j < handle_count_array[i]; j++)
            {
                PINT_handle_wrangler_remove_handle(&handle_matrix[i][j],
                    cur_fs);
            }

            /* are there more handles? */
            if (position_array[i] != PVFS_ITERATE_END)
            {
                more_handles = 1;
                handle_count_array[i] = HANDLE_BATCH;
            }
        }
    } while (more_handles != 0);

    ret = 0;
    goto load_handles_success;

load_handles_error:
    if(PINT_handle_wrangler_handlelist.stranded_array)
        free(PINT_handle_wrangler_handlelist.stranded_array);
    if(PINT_handle_wrangler_handlelist.used_array)
        free(PINT_handle_wrangler_handlelist.used_array);
    if(PINT_handle_wrangler_handlelist.size_array)
        free(PINT_handle_wrangler_handlelist.size_array);
    if(PINT_handle_wrangler_handlelist.addr_array)
        free(PINT_handle_wrangler_handlelist.addr_array);
    if(PINT_handle_wrangler_handlelist.list_array)
    {
        for(i=0; i<server_count; i++)
        {
            if(PINT_handle_wrangler_handlelist.list_array[i])
                free(PINT_handle_wrangler_handlelist.list_array[i]);
        }
        free(PINT_handle_wrangler_handlelist.list_array);
    }
    if(PINT_handle_wrangler_handlelist.list_array_seen)
    {
        for(i=0; i<server_count; i++)
        {
            if(PINT_handle_wrangler_handlelist.list_array_seen[i])
                free(PINT_handle_wrangler_handlelist.list_array_seen[i]);
        }
        free(PINT_handle_wrangler_handlelist.list_array_seen);
    }

    /* fall through on purpose */

load_handles_success:
    if(handle_matrix)
    {
        for(i=0; i<server_count; i++)
        {
            if(handle_matrix[i])
                free(handle_matrix[i]);
        }
        free(handle_matrix);
    }
    if(position_array)
        free(position_array);
    if(handle_count_array)
        free(handle_count_array);
    if(stat_array)
        free(stat_array);

    return ret;
}

/**
 * Removes the given handle from the list of handles stored.  Each check in 
 * the fsck calls this function as it sees a handle.  The end result is a 
 * list of left over "stranded" handles.
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
static int PINT_handle_wrangler_remove_handle(
    const PVFS_handle * handle,     /**< handle to remove */
    const PVFS_fs_id * cur_fs)      /**< fs_id */
{
    int ret = 0;
    int i = 0;
    int j = 0;
    PVFS_BMI_addr_t server_addr;
    int found = 0;

    /* find which server the handle is on */
    ret = PINT_cached_config_map_to_server(&server_addr, *handle, *cur_fs);
    if(ret < 0)
    {
        PVFS_perror_gossip("PINT_cached_config_map_to_server", ret);
        gossip_err("Error: could not resolve handle [%llu] to server\n", 
            llu(*handle));
        return(ret);
    }

    /* get the index of the server this handle is located on */
    for (i = 0; i < PINT_handle_wrangler_handlelist.num_servers; i++)
    {
        if (PINT_handle_wrangler_handlelist.addr_array[i] == server_addr)
        {
            found = 1;
            break;
        }
    }
    if(!found)
    {
        gossip_err("Error: could not find matching server for handle [%llu]\n",
            llu(*handle));
        return(-PVFS_EINVAL);
    }

    /* keep up with which handles have been "seen" */
    found = 0;
    for (j = 0; j < PINT_handle_wrangler_handlelist.used_array[i]; j++)
    {
        if (PINT_handle_wrangler_handlelist.list_array[i][j] == *handle)
        {
            PINT_handle_wrangler_handlelist.list_array_seen[i][j] = 'x';
            PINT_handle_wrangler_handlelist.stranded_array[i]--;
            found = 1;
            break;
        }
    }
    if(!found)
    {
        gossip_err("Error: could not find handle [%llu]\n",
            llu(*handle));
        return(-PVFS_EINVAL);
    }

    return ret;
}

#if 0
/** 
  * Returns the handles left over from the fsck 
 */
static int PINT_handle_wrangler_get_stranded_handles(
    const PVFS_fs_id * cur_fs,                  /**< filesystem id */
    int *num_stranded_handles,                  /**< number of handles in array of handles returned */
    PVFS_handle ** stranded_handles)            /**< array of stranded handles on fs cur_fs */
{
    int ret = 0;
    int i = 0;
    int j = 0;
    int position = 0;

    *num_stranded_handles = 0;

    for (i = 0; i < PINT_handle_wrangler_handlelist.num_servers; i++)
    {
        (*num_stranded_handles) +=
            PINT_handle_wrangler_handlelist.stranded_array[i];
    }

    *stranded_handles = (PVFS_handle *) calloc((*num_stranded_handles), sizeof(PVFS_handle));
    if (*stranded_handles == NULL)
    {
        return -PVFS_ENOMEM;
    }

    for (i = 0; i < PINT_handle_wrangler_handlelist.num_servers; i++)
    {
        for (j = 0; j < PINT_handle_wrangler_handlelist.used_array[i]; j++)
        {
            if (!PINT_handle_wrangler_handlelist.list_array_seen[i][j])
            {
                (*stranded_handles)[position] =
                    PINT_handle_wrangler_handlelist.list_array[i][j];
                position++;
            }
        }
    }

    return ret;
}
#endif

/** 
 * Displays the handles left over from the fsck 
 *
 * \retval 0 on success 
 * \retval -PVFS_error on failure
 */
static int PINT_handle_wrangler_display_stranded_handles(
    const struct PINT_fsck_options *fsck_options, /**< populated fsck options */
    const PVFS_fs_id * cur_fs,                             /**< filesystem id */
    const PVFS_credentials * creds)      /**< populated credentials structure */
{
    int ret = 0;
    int i = 0;
    int j = 0;
    int server_type = 0;
    PVFS_sysresp_getattr attributes;
    PVFS_object_ref pref;
    const char *server_name = NULL;
    int header = 0;
    char buf[128] = {0};

    for (i = 0; i < PINT_handle_wrangler_handlelist.num_servers; i++)
    {
        /* get the pretty server name */
        server_name = PINT_cached_config_map_addr(
            *cur_fs, PINT_handle_wrangler_handlelist.
            addr_array[i], &server_type);

        for (j = 0; j < PINT_handle_wrangler_handlelist.size_array[i]; j++)
        {
            if (!PINT_handle_wrangler_handlelist.list_array_seen[i][j])
            {
                pref.handle = PINT_handle_wrangler_handlelist.list_array[i][j];
                pref.fs_id = *cur_fs;

                if(!header)
                {
                    printf("\n");
                    printf("Stranded Objects:\n");
                    printf
                        ("[  Handle  ] [  FSID  ] [    Size    ] [File Type] [      PVFS2 Server     ]\n");
                    header = 1;
                }

                /* get this objects attributes */
                ret = PVFS_fsck_get_attributes(fsck_options, &pref, creds,
                                         &attributes);
                
                printf(" %llu   %d  ",
                       llu(PINT_handle_wrangler_handlelist.list_array[i][j]),
                       *cur_fs);

                if(ret < 0)
                {
                    PVFS_strerror_r(ret, buf, 127);
                    printf("Unknown: getattr error: %s)\n", buf);
                }
                else
                {

                    if (attributes.attr.mask & PVFS_ATTR_SYS_SIZE)
                    {
                        printf("%13lld   ", lld(attributes.attr.size));
                    }

                    switch (attributes.attr.objtype)
                    {
                    case PVFS_TYPE_NONE:
                        printf("none     ");
                        break;
                    case PVFS_TYPE_METAFILE:
                        printf("meta file");
                        break;
                    case PVFS_TYPE_DATAFILE:
                        printf("data file");
                        break;
                    case PVFS_TYPE_DIRECTORY:
                        printf("directory");
                        break;
                    case PVFS_TYPE_SYMLINK:
                        printf("symlink  ");
                        free(attributes.attr.link_target);
                        break;
                    case PVFS_TYPE_DIRDATA:
                        printf("dirdata  ");
                        break;
                    case PVFS_TYPE_INTERNAL:
                        printf("internal  ");
                        break;
                    }
                    printf("   %s\n", server_name);
                }
            }
        }
    }

    if(!header)
    {
        printf("No stranded objects found.\n");
    }

    return ret;
}

/**
 * Compares two handles, for qsort()
 *
 * \retval 0 if equal
 * \retval <0 if handle1 less than handle2
 * \retval >0 if handle1 greater than handle2
 */
static int compare_handles(
    const void *handle1,    /**< handle 1*/
    const void *handle2)    /**< handle 2*/
{
    PVFS_size temp_handle =
        *((PVFS_handle *) handle1) - *((PVFS_handle *) handle2);

    if (temp_handle > 0)
    {
        return 1;
    }
    else if (temp_handle < 0)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

/**
 * Set the return code for a function, taking previous return values into 
 * account.  The purpose of this is to make sure when we are propigating
 * errors that warnings do not take precident over standard error codes.
 */
static void set_return_code(
    int *ret,         /**< error code to populate */
    const int retval) /**< the value we are proposing to set the error code to */
{
    if (*ret >= 0)
    {
        *ret = retval;
    }
    else if (retval != 0)
    {
        *ret = retval;
    }
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
