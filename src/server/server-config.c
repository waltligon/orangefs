#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <assert.h>
#include <ctype.h>

#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "pvfs2-storage.h"
#include "job.h"
#include "gossip.h"

static DOTCONF_CB(get_pvfs_server_id);
static DOTCONF_CB(get_storage_space);
static DOTCONF_CB(enter_defaults_context);
static DOTCONF_CB(exit_defaults_context);
static DOTCONF_CB(enter_aliases_context);
static DOTCONF_CB(exit_aliases_context);
static DOTCONF_CB(enter_filesystem_context);
static DOTCONF_CB(exit_filesystem_context);
static DOTCONF_CB(enter_bucket_context);
static DOTCONF_CB(exit_bucket_context);
static DOTCONF_CB(get_metaserver_list);
static DOTCONF_CB(get_dataserver_list);
static DOTCONF_CB(get_unexp_req);
static DOTCONF_CB(get_filesystem_name);
static DOTCONF_CB(get_alias_list);
static DOTCONF_CB(get_bucket_list);

/* misc helper functions */
static int is_valid_alias(char *str);
static int is_valid_bucket_range_description(char *b_range);
static int is_valid_filesystem_configuration(struct filesystem_configuration_s *fs);
static void free_host_bucket_mapping(void *ptr);
static void free_host_alias(void *ptr);
static void free_filesystem(void *ptr);

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
    {"<Buckets>",ARG_NONE, enter_bucket_context,NULL,CTX_ALL},
    {"</Buckets>",ARG_NONE, exit_bucket_context,NULL,CTX_ALL},
    {"Bucket",ARG_LIST, get_bucket_list,NULL,CTX_ALL},
    {"MetaServerList",ARG_LIST, get_metaserver_list,NULL,CTX_ALL},
    {"DataServerList",ARG_LIST, get_dataserver_list,NULL,CTX_ALL},
    {"UnexpectedRequests",ARG_INT, get_unexp_req,NULL,CTX_ALL},
    {"FS_Name",ARG_STR, get_filesystem_name,NULL,CTX_ALL},
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

    config_s->fs_config_filename = (argv[1] ? argv[1] : "fs.conf");
    config_s->server_config_filename = (argv[2] ? argv[2] : "server.conf");

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
        gossip_err("Error reading config file\n");

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
        gossip_err("Error reading config file\n");

    dotconf_cleanup(configfile);
    return 0;
}

DOTCONF_CB(get_pvfs_server_id)
{
    if (config_s->configuration_context != GLOBAL_CONFIG)
    {
        gossip_lerr("HostID Tags can only be within the Global context");
        return NULL;
    }
    if (config_s->host_id)
    {
        gossip_lerr("WARNING: HostID value being overwritten.");
        free(config_s->host_id);
    }
    config_s->host_id = strdup(cmd->data.str);
    return NULL;
}

DOTCONF_CB(get_storage_space)
{
    if (config_s->configuration_context != DEFAULTS_CONFIG)
    {
        gossip_lerr("StorageSpace Tag can only be in a Defaults block");
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

    /* make sure last fs config object is valid */
    if (!is_valid_filesystem_configuration(fs_conf))
    {
        gossip_lerr("Error in context.  Cannot have /Filesystem tag "
                    "before all filesystem attributes are declared\n");
        return NULL;
    }

    config_s->configuration_context = GLOBAL_CONFIG;
    return NULL;
}

DOTCONF_CB(enter_bucket_context)
{
    if (config_s->configuration_context != FILESYSTEM_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Buckets tag here\n");
        return NULL;
    }
    config_s->configuration_context = BUCKETS_CONFIG;
    return NULL;
}

DOTCONF_CB(exit_bucket_context)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->configuration_context != BUCKETS_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have /Buckets tag here\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);
    assert(fs_conf);

    if (!fs_conf->bucket_ranges)
    {
        gossip_lerr("Error! No valid bucket ranges added to %s\n",
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
	
DOTCONF_CB(get_bucket_list)
{
    int i = 0;
    struct filesystem_configuration_s *fs_conf = NULL;
    struct host_bucket_mapping_s *bucket_mapping = NULL;

    if (config_s->configuration_context != BUCKETS_CONFIG)
    {
        gossip_lerr("Error in context.  Cannot have Bucket "
                    "outside of Buckets context\n");
        return NULL;
    }

    fs_conf = (struct filesystem_configuration_s *)
        llist_head(config_s->file_systems);

    if (!fs_conf->bucket_ranges)
    {
        fs_conf->bucket_ranges = llist_new();
    }

    for(i = 0; i < cmd->arg_count; i += 2)
    {
        if (is_valid_alias(cmd->data.list[i]))
        {
            i++;
            assert(cmd->data.list[i]);

            if (is_valid_bucket_range_description(cmd->data.list[i]))
            {
                bucket_mapping = (host_bucket_mapping_s *)malloc(
                    sizeof(host_bucket_mapping_s));
                assert(bucket_mapping);
                memset(bucket_mapping,0,sizeof(host_bucket_mapping_s));

                bucket_mapping->host_alias = strdup(cmd->data.list[i-1]);
                bucket_mapping->bucket_range = strdup(cmd->data.list[i]);

                llist_add_to_tail(fs_conf->bucket_ranges,
                                  (void *)bucket_mapping);
            }
            else
            {
                gossip_lerr("Error in bucket range description.\n"
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

        /* free all host alias objects */
        llist_free(config_s->host_aliases,free_host_alias);

        /* free all filesystem objects */
        llist_free(config_s->file_systems,free_filesystem);
    }
    return;
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

static int is_valid_bucket_range_description(char *b_range)
{
    int ret = 0;
    int len = 0;
    char *ptr = (char *)0;
    char *end = (char *)0;

    if (b_range)
    {
        len = strlen(b_range);
        end = (b_range + len);

        for(ptr = b_range; ptr < end; ptr++)
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

static int is_valid_filesystem_configuration(struct filesystem_configuration_s *fs)
{
    return ((fs && fs->file_system_name && fs->meta_server_list &&
             fs->data_server_list && fs->bucket_ranges) ? 1 : 0);
}

static void free_host_bucket_mapping(void *ptr)
{
    struct host_bucket_mapping_s *b_mapping =
        (struct host_bucket_mapping_s *)ptr;
    if (b_mapping)
    {
        free(b_mapping->host_alias);
        free(b_mapping->bucket_range);
        free(b_mapping);
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

        /* free all bucket ranges */
        llist_free(fs->bucket_ranges,free_host_bucket_mapping);

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
 * Returns:  char * (bucket range) on success; NULL on failure
 *
 * Synopsis: return the bucket range (string) on the specified
 *           filesystem that matches the host specific configuration
 *           
 */
char *PINT_server_config_get_handle_range_str(struct server_configuration_s *config_s,
                                              struct filesystem_configuration_s *fs)
{
    char *ret = (char *)0;
    char *my_alias = (char *)0;
    struct llist *cur = NULL;
    struct host_bucket_mapping_s *cur_b_mapping = NULL;

    if (config_s && config_s->host_id && fs)
    {
        my_alias = PINT_server_config_get_host_alias_ptr(config_s,config_s->host_id);
        if (my_alias)
        {
            cur = fs->bucket_ranges;
            while(cur)
            {
                cur_b_mapping = llist_head(cur);
                if (!cur_b_mapping)
                {
                    break;
                }
                assert(cur_b_mapping->host_alias);
                assert(cur_b_mapping->bucket_range);

                if (strcmp(cur_b_mapping->host_alias,my_alias) == 0)
                {
                    ret = cur_b_mapping->bucket_range;
                    break;
                }
                cur = llist_next(cur);
            }
        }
    }
    return ret;
}


/*
  vim:set ts=4:
  vim:set shiftwidth=4:
*/
