#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>

#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "pvfs2-storage.h"
#include "job.h"
#include "gossip.h"
#include "extent-utils.h"
#include "mkspace.h"

static DOTCONF_CB(get_pvfs_server_id);
static DOTCONF_CB(get_storage_space);
static DOTCONF_CB(enter_defaults_context);
static DOTCONF_CB(exit_defaults_context);
static DOTCONF_CB(enter_aliases_context);
static DOTCONF_CB(exit_aliases_context);
static DOTCONF_CB(enter_filesystem_context);
static DOTCONF_CB(exit_filesystem_context);
static DOTCONF_CB(enter_handleranges_context);
static DOTCONF_CB(exit_handleranges_context);
static DOTCONF_CB(get_metaserver_list);
static DOTCONF_CB(get_dataserver_list);
static DOTCONF_CB(get_unexp_req);
static DOTCONF_CB(get_root_handle);
static DOTCONF_CB(get_filesystem_name);
static DOTCONF_CB(get_filesystem_collid);
static DOTCONF_CB(get_alias_list);
static DOTCONF_CB(get_range_list);

/* misc helper functions */
static int cache_config_files(int argc, char **argv);
static int is_valid_alias(char *str);
static int is_valid_handle_range_description(char *h_range);
static int is_populated_filesystem_configuration(
    struct filesystem_configuration_s *fs);
static int is_valid_filesystem_configuration(
    struct filesystem_configuration_s *fs);
static void free_host_handle_mapping(void *ptr);
static void free_host_alias(void *ptr);
static void free_filesystem(void *ptr);
static int is_root_handle_in_my_range(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);


static struct server_configuration_s *config_s = NULL;

static const configoption_t options[] =
{
    {"HostID",ARG_STR, get_pvfs_server_id,NULL,CTX_ALL},
    {"StorageSpace",ARG_STR, get_storage_space,NULL,CTX_ALL},
    {"<Defaults>",ARG_NONE, enter_defaults_context,NULL,CTX_ALL},
    {"</Defaults>",ARG_NONE, exit_defaults_context,NULL,CTX_ALL},
    {"<Aliases>",ARG_NONE, enter_aliases_context,NULL,CTX_ALL},
    {"</Aliases>",ARG_NONE, exit_aliases_context,NULL,CTX_ALL},
    {"Alias",ARG_LIST, get_alias_list,NULL,CTX_ALL},
    {"<FileSystem>",ARG_NONE, enter_filesystem_context,NULL,CTX_ALL},
    {"</FileSystem>",ARG_NONE, exit_filesystem_context,NULL,CTX_ALL},
    {"<HandleRanges>",ARG_NONE, enter_handleranges_context,NULL,CTX_ALL},
    {"</HandleRanges>",ARG_NONE, exit_handleranges_context,NULL,CTX_ALL},
    {"Range",ARG_LIST, get_range_list,NULL,CTX_ALL},
    {"MetaServerList",ARG_LIST, get_metaserver_list,NULL,CTX_ALL},
    {"DataServerList",ARG_LIST, get_dataserver_list,NULL,CTX_ALL},
    {"UnexpectedRequests",ARG_INT, get_unexp_req,NULL,CTX_ALL},
    {"RootHandle",ARG_INT, get_root_handle,NULL,CTX_ALL},
    {"FS_Name",ARG_STR, get_filesystem_name,NULL,CTX_ALL},
    {"CollectionID",ARG_INT, get_filesystem_collid,NULL,CTX_ALL},
    LAST_OPTION
};

/*
 * Function: PINT_server_config
 *
 * Params:   struct server_configuration_s*,
 *           int argc,
 *           char **argv
 *
 * Returns:  0 on success; 1 on failure
 *
 * Synopsis: Parse the config file according to parameters set
 *           configuration struct above.
 *           
 */
int PINT_server_config(struct server_configuration_s *config_obj,
                       int argc, char **argv)
{
    configfile_t *configfile = (configfile_t *)0;

    if (!config_obj)
    {
        gossip_err("Invalid server_configuration_s object\n");
        return 1;
    }

    /* global assignment */
    config_s = config_obj;

    config_s->host_id = NULL;
    config_s->storage_path = NULL;
    config_s->host_aliases = NULL;
    config_s->file_systems = NULL;
    config_s->fs_config_buf = NULL;
    config_s->fs_config_buflen = 0;
    config_s->server_config_buf = NULL;
    config_s->server_config_buflen = 0;

    if (cache_config_files(argc,argv))
    {
        gossip_err("Failed to read config files.  "
                   "Please make sure they exist and are valid!\n");
        return 1;
    }
    assert(config_s->fs_config_buflen && config_s->fs_config_buf);
    assert(config_s->server_config_buflen && config_s->server_config_buf);

    /* first read in the fs.conf defaults config file */
    config_s->configuration_context = GLOBAL_CONFIG;
    configfile = dotconf_create(config_s->fs_config_filename,
                                options, NULL, CASE_INSENSITIVE);
    if (!configfile)
    {
        gossip_err("Error opening config file %s\n",
                   config_s->fs_config_filename);
        return 1;
    }

    if (dotconf_command_loop(configfile) == 0)
    {
        gossip_err("Error reading config file %s\n",
                   config_s->fs_config_filename);
        return 1;
    }
    dotconf_cleanup(configfile);

    /* then read in the server.conf (host specific) config file */
    config_s->configuration_context = GLOBAL_CONFIG;
    configfile = dotconf_create(config_s->server_config_filename,
                                options, NULL, CASE_INSENSITIVE);
    if (!configfile)
    {
        gossip_err("Error opening config file: %s\n",
                   config_s->server_config_filename);
        return 1;
    }

    if (dotconf_command_loop(configfile) == 0)
    {
        gossip_err("Error reading config file %s\n",
                   config_s->server_config_filename);
        return 1;
    }
    dotconf_cleanup(configfile);

    if (!config_s->host_id)
    {
        gossip_err("Configuration file error. No host ID specified.\n");
        return 1;
    }

    if (!config_s->storage_path)
    {
        gossip_err("Configuration file error. No storage path specified.\n");
        return 1;
    }
    return 0;
}

DOTCONF_CB(get_pvfs_server_id)
{
    if (config_s->configuration_context != GLOBAL_CONFIG)
    {
        gossip_lerr("HostID Tag can only be in the Global context");
        return NULL;
    }
    if (config_s->host_id)
    {
        gossip_lerr("WARNING: HostID value being overwritten (from "
                    "%s to %s).\n",config_s->host_id,cmd->data.str);
        free(config_s->host_id);
    }
    config_s->host_id = strdup(cmd->data.str);
    return NULL;
}

DOTCONF_CB(get_storage_space)
{
    if (config_s->configuration_context != GLOBAL_CONFIG)
    {
        gossip_lerr("StorageSpace Tag can only be in the Global context");
        return NULL;
    }
    if (config_s->storage_path)
    {
        gossip_lerr("WARNING: StorageSpace value being overwritten.\n");
        free(config_s->storage_path);
    }
    config_s->storage_path = strdup(cmd->data.str);
    return NULL;
}

DOTCONF_CB(enter_defaults_context)
{
    if (config_s->configuration_context != GLOBAL_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Defaults tag here\n");
        return NULL;
    }
    config_s->configuration_context = DEFAULTS_CONFIG;
    return NULL;
}

DOTCONF_CB(exit_defaults_context)
{
    if (config_s->configuration_context != DEFAULTS_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have /Defaults tag here\n");
        return NULL;
    }
    config_s->configuration_context = GLOBAL_CONFIG;
    return NULL;
}

DOTCONF_CB(enter_aliases_context)
{
    if (config_s->configuration_context != GLOBAL_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Aliases tag here\n");
        return NULL;
    }
    config_s->configuration_context = ALIASES_CONFIG;
    return NULL;
}

DOTCONF_CB(exit_aliases_context)
{
    if (config_s->configuration_context != ALIASES_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have /Aliases tag here\n");
        return NULL;
    }
    config_s->configuration_context = GLOBAL_CONFIG;
    return NULL;
}

DOTCONF_CB(enter_filesystem_context)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->host_aliases == NULL)
    {
        gossip_lerr("Error in context.  Filesystem tag cannot "
                    "be declared before an Aliases tag.\n");
        return NULL;
    }

    if (config_s->configuration_context != GLOBAL_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Filesystem tag here\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        malloc(sizeof(struct filesystem_configuration_s));
    assert(fs_conf);
    memset(fs_conf,0,sizeof(struct filesystem_configuration_s));

    if (!config_s->file_systems)
    {
        config_s->file_systems = llist_new();
    }
    llist_add_to_head(config_s->file_systems,(void *)fs_conf);
    assert(llist_head(config_s->file_systems) == (void *)fs_conf);
    config_s->configuration_context = FILESYSTEM_CONFIG;
    return NULL;
}

DOTCONF_CB(exit_filesystem_context)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have /Filesystem tag here\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    assert(fs_conf);

    /*
      make sure last fs config object is valid
      (i.e. has all required values filled in)
    */
    if (!is_populated_filesystem_configuration(fs_conf))
    {
        gossip_lerr("Error: Filesystem configuration is invalid!\n");
        gossip_lerr("Possible Error in context.  Cannot have /Filesystem "
                    "tag before all filesystem attributes are declared\n");
        return NULL;
    }

    config_s->configuration_context = GLOBAL_CONFIG;
    return NULL;
}

DOTCONF_CB(enter_handleranges_context)
{
    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have HandleRanges tag here\n");
        return NULL;
    }
    config_s->configuration_context = HANDLERANGES_CONFIG;
    return NULL;
}

DOTCONF_CB(exit_handleranges_context)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != HANDLERANGES_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have /HandleRanges tag here\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    assert(fs_conf);

    if (!fs_conf->handle_ranges)
    {
        gossip_lerr("Error! No valid handle ranges added to %s\n",
                    fs_conf->file_system_name);
    }
    config_s->configuration_context = FILESYSTEM_CONFIG;
    return NULL;
}

DOTCONF_CB(get_metaserver_list)
{
    int i = 0;
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have MetaServerList tag here\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    assert(fs_conf);

    if (!fs_conf->meta_server_list)
    {
        fs_conf->meta_server_list = llist_new();
    }

    for(i = 0; i < cmd->arg_count; i++)
    {
        if (is_valid_alias(cmd->data.list[i]))
        {
            llist_add_to_head(fs_conf->meta_server_list,
                              (void *)strdup(cmd->data.list[i]));
        }
        else
        {
            gossip_lerr("Error! %s is an unrecognized alias\n",
                        cmd->data.list[i]);
        }
    }
    return NULL;
}

DOTCONF_CB(get_dataserver_list)
{
    int i = 0;
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have DataServerList tag here\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    assert(fs_conf);

    if (!fs_conf->data_server_list)
    {
        fs_conf->data_server_list = llist_new();
    }

    for(i = 0; i < cmd->arg_count; i++)
    {
        if (is_valid_alias(cmd->data.list[i]))
        {
            llist_add_to_tail(fs_conf->data_server_list,
                              (void *)strdup(cmd->data.list[i]));
        }
        else
        {
            gossip_lerr("Error! %s is an unrecognized alias\n",
                        cmd->data.list[i]);
        }
    }
    return NULL;
}

DOTCONF_CB(get_unexp_req)
{
    if (config_s->configuration_context != DEFAULTS_CONFIG)
    {
        gossip_lerr("UnexpectedRequests Tag can only be in a Defaults block");
        return NULL;
    }
    config_s->initial_unexpected_requests = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_root_handle)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("RootHandle Tag can only be in a Filesystem block");
        return NULL;
    }
    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    assert(fs_conf);
    fs_conf->root_handle = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_filesystem_name)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("FS_Name Tags can only be within Filesystem tags");
        return NULL;
    }
    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    if (fs_conf->file_system_name)
    {
        gossip_lerr("WARNING: Overwriting %s with %s\n",
                    fs_conf->file_system_name,cmd->data.str);
    }
    fs_conf->file_system_name = strdup(cmd->data.str);
    return NULL;
}

DOTCONF_CB(get_filesystem_collid)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("CollectionID Tags can only be within Filesystem tags");
        return NULL;
    }
    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    if (fs_conf->coll_id)
    {
        gossip_lerr("WARNING: Overwriting %d with %d\n",
                    (int)fs_conf->coll_id,(int)cmd->data.value);
    }
    fs_conf->coll_id = (TROVE_coll_id)cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_alias_list)
{
    struct host_alias_s *cur_alias = NULL;

    if (config_s->configuration_context != ALIASES_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Alias "
                    "outside of Aliases context\n");
        return NULL;
    }
    assert(cmd->arg_count == 2);

    cur_alias = (host_alias_s *)
        malloc(sizeof(host_alias_s));
    cur_alias->host_alias = strdup(cmd->data.list[0]);
    cur_alias->bmi_address = strdup(cmd->data.list[1]);

    if (!config_s->host_aliases)
    {
        config_s->host_aliases = llist_new();
    }
    llist_add_to_tail(config_s->host_aliases,(void *)cur_alias);
    return NULL;
}
	
DOTCONF_CB(get_range_list)
{
    int i = 0;
    struct filesystem_configuration_s *fs_conf = NULL;
    struct host_handle_mapping_s *handle_mapping = NULL;

    if (config_s->configuration_context != HANDLERANGES_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Range keyword "
                    "outside of HandleRanges context\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);

    if (!fs_conf->handle_ranges)
    {
        fs_conf->handle_ranges = llist_new();
    }

    for(i = 0; i < cmd->arg_count; i += 2)
    {
        if (is_valid_alias(cmd->data.list[i]))
        {
            i++;
            assert(cmd->data.list[i]);

            if (is_valid_handle_range_description(cmd->data.list[i]))
            {
                handle_mapping = (host_handle_mapping_s *)malloc(
                    sizeof(host_handle_mapping_s));
                assert(handle_mapping);
                memset(handle_mapping,0,sizeof(host_handle_mapping_s));

                handle_mapping->host_alias = strdup(cmd->data.list[i-1]);
                handle_mapping->handle_range = strdup(cmd->data.list[i]);

                llist_add_to_tail(fs_conf->handle_ranges,
                                  (void *)handle_mapping);
            }
            else
            {
                gossip_lerr("Error in handle range description.\n"
                            "%s is invalid input data!\n",cmd->data.list[i]);
            }
        }
        else
        {
            gossip_lerr("Error! %s is an unrecognized alias\n",
                        cmd->data.list[i]);
        }
    }
    return NULL;
}

/*
 * Function: PINT_server_config_release
 *
 * Params:   struct server_configuration_s*
 *
 * Returns:  void
 *
 * Synopsis: De-allocates memory consumed internally
 *           by the specified server_configuration_s
 *           
 */
void PINT_server_config_release(struct server_configuration_s *config_s)
{
    if (config_s)
    {
        if (config_s->host_id)
        {
            free(config_s->host_id);
        }

        if (config_s->storage_path)
        {
            free(config_s->storage_path);
        }

        if (config_s->fs_config_filename)
        {
            free(config_s->fs_config_filename);
        }

        if (config_s->server_config_filename)
        {
            free(config_s->server_config_filename);
        }

        if (config_s->fs_config_buf)
        {
            free(config_s->fs_config_buf);
        }

        if (config_s->server_config_buf)
        {
            free(config_s->server_config_buf);
        }

        /* free all host alias objects */
        llist_free(config_s->host_aliases,free_host_alias);

        /* free all filesystem objects */
        llist_free(config_s->file_systems,free_filesystem);
    }
}

static int is_valid_alias(char *str)
{
    int ret = 0;
    struct llist *cur = NULL;
    struct host_alias_s *cur_alias;

    if (str)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(str,cur_alias->host_alias) == 0)
            {
                ret = 1;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

static int is_valid_handle_range_description(char *h_range)
{
    int ret = 0;
    int len = 0;
    char *ptr = (char *)0;
    char *end = (char *)0;

    if (h_range)
    {
        len = strlen(h_range);
        end = (h_range + len);

        for(ptr = h_range; ptr < end; ptr++)
        {
            if (!isdigit((int)*ptr) && (*ptr != ',') &&
                (*ptr != ' ') && (*ptr != '-'))
            {
                break;
            }
        }
        if (ptr == end)
        {
            ret = 1;
        }
    }
    return ret;
}

static int is_populated_filesystem_configuration(
    struct filesystem_configuration_s *fs)
{
    return ((fs && fs->coll_id && fs->file_system_name &&
             fs->meta_server_list && fs->data_server_list &&
             fs->handle_ranges && fs->root_handle) ? 1 : 0);
}

static int is_root_handle_in_my_range(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs)
{
    int ret = 0;
    struct llist *cur = NULL;
    struct llist *extent_list = NULL;
    char *cur_host_id = (char *)0;
    host_handle_mapping_s *cur_h_mapping = NULL;

    if (config && is_populated_filesystem_configuration(fs))
    {
        /*
          check if the root handle is within one of the
          specified host's handle ranges for this fs
        */
        cur = fs->handle_ranges;
        while(cur)
        {
            cur_h_mapping = llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            assert(cur_h_mapping->host_alias);
            assert(cur_h_mapping->handle_range);

            cur_host_id = PINT_server_config_get_host_addr_ptr(
                config,cur_h_mapping->host_alias);
            if (!cur_host_id)
            {
                gossip_err("Invalid host ID for alias %s.\n",
                           cur_h_mapping->host_alias);
                break;
            }

            /* only check if this is *our* range */
            if (strcmp(config->host_id,cur_host_id) == 0)
            {
                extent_list = PINT_create_extent_list(
                    cur_h_mapping->handle_range);
                if (!extent_list)
                {
                    gossip_err("Failed to create extent list.\n");
                    break;
                }

                ret = PINT_handle_in_extent_list(
                    extent_list,fs->root_handle);
                PINT_release_extent_list(extent_list);
                if (ret == 1)
                {
                    break;
                }
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

static int is_valid_filesystem_configuration(
    struct filesystem_configuration_s *fs)
{
    int ret = 0;
    struct llist *cur = NULL;
    struct llist *extent_list = NULL;
    host_handle_mapping_s *cur_h_mapping = NULL;

    if (is_populated_filesystem_configuration(fs))
    {
        /*
          first, make sure the root handle is within one of the
          specified handle ranges for this fs
        */
        cur = fs->handle_ranges;
        while(cur)
        {
            cur_h_mapping = llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            assert(cur_h_mapping->host_alias);
            assert(cur_h_mapping->handle_range);

            extent_list = PINT_create_extent_list(cur_h_mapping->handle_range);
            if (!extent_list)
            {
                gossip_err("Failed to create extent list.\n");
                break;
            }

            ret = PINT_handle_in_extent_list(extent_list,fs->root_handle);
            PINT_release_extent_list(extent_list);
            if (ret == 1)
            {
                break;
            }
            cur = llist_next(cur);
        }

        if (ret == 0)
        {
            gossip_err("RootHandle (%d) is NOT within the handle ranges "
                       "specified for this filesystem (%s).\n",
                       fs->root_handle,fs->file_system_name);
        }
    }
    return ret;
}

static void free_host_handle_mapping(void *ptr)
{
    struct host_handle_mapping_s *h_mapping =
        (struct host_handle_mapping_s *)ptr;
    if (h_mapping)
    {
        free(h_mapping->host_alias);
        free(h_mapping->handle_range);
        free(h_mapping);
    }
}

static void free_host_alias(void *ptr)
{
    struct host_alias_s *alias = (struct host_alias_s *)ptr;
    if (alias)
    {
        free(alias->host_alias);
        free(alias->bmi_address);
        free(alias);
    }
}

static void free_filesystem(void *ptr)
{
    struct filesystem_configuration_s *fs =
        (struct filesystem_configuration_s *)ptr;
    if (fs)
    {
        free(fs->file_system_name);

        /* free all meta server strings */
        llist_free(fs->meta_server_list,free);

        /* free all data server strings */
        llist_free(fs->data_server_list,free);

        /* free all handle ranges */
        llist_free(fs->handle_ranges,free_host_handle_mapping);

        free(fs);
    }
}

/*
 * Function: PINT_server_config_get_host_addr_ptr
 *
 * Params:   struct server_configuration_s*,
 *           char *alias
 *
 * Returns:  char * (bmi_address) on success; NULL on failure
 *
 * Synopsis: retrieve the bmi_address matching the specified alias
 *           
 */
char *PINT_server_config_get_host_addr_ptr(struct server_configuration_s *config_s,
                                           char *alias)
{
    char *ret = (char *)0;
    struct llist *cur = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && alias)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(cur_alias->host_alias,alias) == 0)
            {
                ret = cur_alias->bmi_address;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

/*
 * Function: PINT_server_config_get_host_alias_ptr
 *
 * Params:   struct server_configuration_s*,
 *           char *bmi_address
 *
 * Returns:  char * (alias) on success; NULL on failure
 *
 * Synopsis: retrieve the alias matching the specified bmi_address
 *           
 */
char *PINT_server_config_get_host_alias_ptr(struct server_configuration_s *config_s,
                                            char *bmi_address)
{
    char *ret = (char *)0;
    struct llist *cur = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && bmi_address)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(cur_alias->bmi_address,bmi_address) == 0)
            {
                ret = cur_alias->host_alias;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

/*
 * Function: PINT_server_config_get_handle_range_str
 *
 * Params:   struct server_configuration_s*,
 *           struct filesystem_configuration_s *fs
 *
 * Returns:  char * (handle range) on success; NULL on failure
 *
 * Synopsis: return the handle range (string) on the specified
 *           filesystem that matches the host specific configuration
 *           
 */
char *PINT_server_config_get_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs)
{
    char *ret = (char *)0;
    char *my_alias = (char *)0;
    struct llist *cur = NULL;
    struct host_handle_mapping_s *cur_h_mapping = NULL;

    if (config_s && config_s->host_id && fs)
    {
        my_alias = PINT_server_config_get_host_alias_ptr(
            config_s,config_s->host_id);
        if (my_alias)
        {
            cur = fs->handle_ranges;
            while(cur)
            {
                cur_h_mapping = llist_head(cur);
                if (!cur_h_mapping)
                {
                    break;
                }
                assert(cur_h_mapping->host_alias);
                assert(cur_h_mapping->handle_range);

                if (strcmp(cur_h_mapping->host_alias,my_alias) == 0)
                {
                    ret = cur_h_mapping->handle_range;
                    break;
                }
                cur = llist_next(cur);
            }
        }
    }
    return ret;
}

/*
  verify that both config files exist.  if so, cache them in RAM so
  that getconfig will not have to re-read the file contents each time.
  returns 0 on success; 1 on failure.

  even if this call fails half way into it, a PINT_server_config_release
  call should properly de-alloc all consumed memory.
*/
static int cache_config_files(int argc, char **argv)
{
    int fd = 0, nread = 0;
    struct stat statbuf;
    char *working_dir = (char *)0;
    char *fs_config_filename = (char *)0;
    char *server_config_filename = (char *)0;
    char buf[512] = {0};

    assert(config_s);

    working_dir = getenv("PWD");
    fs_config_filename = (argv[1] ? argv[1] : "fs.conf");
    server_config_filename = (argv[2] ? argv[2] : "server.conf");

    memset(&statbuf,0,sizeof(struct stat));
    if (stat(fs_config_filename,&statbuf) == 0)
    {
        config_s->fs_config_filename = strdup(fs_config_filename);
        config_s->fs_config_buflen = statbuf.st_size + 1;
    }
    else
    {
        assert(working_dir);
        snprintf(buf,512,"%s/%s",working_dir,fs_config_filename);
        memset(&statbuf,0,sizeof(struct stat));
        if (stat(buf,&statbuf) == 0)
        {
            config_s->fs_config_filename = strdup(buf);
            config_s->fs_config_buflen = statbuf.st_size + 1;
        }
    }

    if (!config_s->fs_config_filename ||
        (config_s->fs_config_buflen == 0))
    {
        gossip_err("Failed to stat fs config file.  (0 file size?)\n");
        return 1;
    }

    memset(&statbuf,0,sizeof(struct stat));
    if (stat(server_config_filename,&statbuf) == 0)
    {
        config_s->server_config_filename = strdup(server_config_filename);
        config_s->server_config_buflen = statbuf.st_size + 1;
    }
    else
    {
        assert(working_dir);
        snprintf(buf,512,"%s/%s",working_dir,server_config_filename);
        memset(&statbuf,0,sizeof(struct stat));
        if (stat(buf,&statbuf) == 0)
        {
            config_s->server_config_filename = strdup(buf);
            config_s->server_config_buflen = statbuf.st_size + 1;
        }
    }

    if (!config_s->server_config_filename ||
        (config_s->server_config_buflen == 0))
    {
        gossip_err("Failed to stat server config file.  (0 file size?)\n");
        return 1;
    }

    if ((fd = open(fs_config_filename,O_RDONLY)) == -1)
    {
        gossip_err("Failed to open fs config file %s.\n",
                   fs_config_filename);
        return 1;
    }

    config_s->fs_config_buf = (char *)malloc(config_s->fs_config_buflen);
    if (!config_s->fs_config_buf)
    {
        gossip_err("Failed to allocate %d bytes for caching the fs "
                   "config file\n",config_s->fs_config_buflen);
        return 1;
    }

    memset(config_s->fs_config_buf,0,config_s->fs_config_buflen);
    nread = read(fd,config_s->fs_config_buf,
                 (config_s->fs_config_buflen - 1));
    if (nread != (config_s->fs_config_buflen - 1))
    {
        gossip_err("Failed to read fs config file %s (nread is %d)\n",
                   fs_config_filename,nread);
        close(fd);
        return 1;
    }
    close(fd);

    if ((fd = open(server_config_filename,O_RDONLY)) == -1)
    {
        gossip_err("Failed to open fs config file %s.\n",
                   fs_config_filename);
        return 1;
    }

    config_s->server_config_buf = (char *)
        malloc(config_s->server_config_buflen);
    if (!config_s->server_config_buf)
    {
        gossip_err("Failed to allocate %d bytes for caching the server "
                   "config file\n",config_s->server_config_buflen);
        return 1;
    }

    memset(config_s->server_config_buf,0,config_s->server_config_buflen);
    nread = read(fd,config_s->server_config_buf,
                 (config_s->server_config_buflen - 1));
    if (nread != (config_s->server_config_buflen - 1))
    {
        gossip_err("Failed to read server config file %s (nread is %d)\n",
                   server_config_filename,nread);
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

/*
  returns 1 if the specified configuration object is valid
  (i.e. contains values that make sense); 0 otherwise
*/
int PINT_server_config_is_valid_configuration(
    struct server_configuration_s *config_s)
{
    int ret = 0, fs_count = 0;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;
    
    if (config_s)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            ret += is_valid_filesystem_configuration(cur_fs);
            fs_count++;

            cur = llist_next(cur);
        }

        ret = ((ret == fs_count) ? 1 : 0);
    }
    return ret;
}


/*
  returns 1 if the specified coll_id is valid based on
  the specified server_configuration struct; 0 otherwise
*/
int PINT_server_config_is_valid_collection_id(
    struct server_configuration_s *config_s, TROVE_coll_id coll_id)
{
    int ret = 0;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            if (cur_fs->coll_id == coll_id)
            {
                ret = 1;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

/*
  returns 1 if the config object has information on the specified
  filesystem; 0 otherwise
*/
int PINT_server_config_has_fs_config_info(
    struct server_configuration_s *config_s, char *fs_name)
{
    int ret = 0;
    struct llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s && fs_name)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            assert(cur_fs->file_system_name);
            if (strcmp(cur_fs->file_system_name,fs_name) == 0)
            {
                ret = 1;
                break;
            }
            cur = llist_next(cur);
        }
    }
    return ret;
}

/*
  create a storage space based on configuration settings object
  with the particular host settings local to the caller
*/
int PINT_server_config_pvfs2_mkspace(struct server_configuration_s *config)
{
    int ret = 1;
    int root_handle = 0;
    int create_collection_only = 0;
    struct llist *cur = NULL;
    char *cur_handle_range = (char *)0;
    filesystem_configuration_s *cur_fs = NULL;

    if (config)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            cur_handle_range = PINT_server_config_get_handle_range_str(
                config,cur_fs);
            if (!cur_handle_range)
            {
                gossip_err("Invalid configuration handle range\n");
                break;
            }

            /*
              check if root handle is in our handle range.
              if it is, we're responsible for creating
              it on disk when creating the storage space
            */
            root_handle = (is_root_handle_in_my_range(config,cur_fs) ?
                           cur_fs->root_handle : 0);

            /*
              for the first fs we encounter, create the storage space
              if it doesn't exist.
            */
            fprintf(stderr,"\n*****************************\n");
            fprintf(stderr,"Creating new storage space\n");
            ret = pvfs2_mkspace(config->storage_path,
                                cur_fs->file_system_name,
                                cur_fs->coll_id,
                                root_handle,
                                cur_handle_range,
                                create_collection_only,
                                1);
            fprintf(stderr,"\n*****************************\n");

            /*
              now that the storage space is created, set the
              create_collection_only variable so that subsequent
              calls to pvfs2_mkspace will not fail when it finds
              that the storage space already exists; this causes
              pvfs2_mkspace to only add the collection to the
              already existing storage space
            */
            create_collection_only = 1;

            cur = llist_next(cur);
        }
    }
    return ret;
}

/*
  vim:set ts=4:
  vim:set shiftwidth=4:
*/
