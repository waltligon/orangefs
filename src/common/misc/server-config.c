/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>

#include "dotconf.h"
#include "server-config.h"
#include "pvfs2.h"
#include "job.h"
#include "gossip.h"
#include "extent-utils.h"
#include "mkspace.h"
#include "pint-distribution.h"
#include "pvfs2-config.h"
#include "pvfs2-server.h"

static DOTCONF_CB(get_pvfs_server_id);
static DOTCONF_CB(get_logstamp);
static DOTCONF_CB(get_storage_space);
static DOTCONF_CB(enter_defaults_context);
static DOTCONF_CB(exit_defaults_context);
static DOTCONF_CB(enter_aliases_context);
static DOTCONF_CB(exit_aliases_context);
static DOTCONF_CB(enter_filesystem_context);
static DOTCONF_CB(exit_filesystem_context);
static DOTCONF_CB(enter_storage_hints_context);
static DOTCONF_CB(exit_storage_hints_context);
static DOTCONF_CB(enter_mhranges_context);
static DOTCONF_CB(exit_mhranges_context);
static DOTCONF_CB(enter_dhranges_context);
static DOTCONF_CB(exit_dhranges_context);
static DOTCONF_CB(enter_distribution_context);
static DOTCONF_CB(exit_distribution_context);
static DOTCONF_CB(get_unexp_req);
static DOTCONF_CB(get_perf_update_interval);
static DOTCONF_CB(get_root_handle);
static DOTCONF_CB(get_name);
static DOTCONF_CB(get_logfile);
static DOTCONF_CB(get_event_logging_list);
static DOTCONF_CB(get_event_log_size);
static DOTCONF_CB(get_filesystem_collid);
static DOTCONF_CB(get_alias_list);
static DOTCONF_CB(get_range_list);
static DOTCONF_CB(get_bmi_module_list);
static DOTCONF_CB(get_flow_module_list);
static DOTCONF_CB(get_handle_recycle_timeout_seconds);
static DOTCONF_CB(get_attr_cache_keywords_list);
static DOTCONF_CB(get_attr_cache_size);
static DOTCONF_CB(get_attr_cache_max_num_elems);
static DOTCONF_CB(get_trove_sync_meta);
static DOTCONF_CB(get_trove_sync_data);
static DOTCONF_CB(get_param);
static DOTCONF_CB(get_value);
static DOTCONF_CB(get_default_num_dfiles);
static DOTCONF_CB(get_server_job_bmi_timeout);
static DOTCONF_CB(get_server_job_flow_timeout);
static DOTCONF_CB(get_client_job_bmi_timeout);
static DOTCONF_CB(get_client_job_flow_timeout);
static DOTCONF_CB(get_client_retry_limit);
static DOTCONF_CB(get_client_retry_delay);
static FUNC_ERRORHANDLER(errorhandler);
const char *contextchecker(command_t *cmd, unsigned long mask);

/* internal helper functions */
static int is_valid_alias(PINT_llist * host_aliases, char *str);
static int is_valid_handle_range_description(char *h_range);
static void free_host_handle_mapping(void *ptr);
static void free_host_alias(void *ptr);
static void free_filesystem(void *ptr);
static void copy_filesystem(
    struct filesystem_configuration_s *dest_fs,
    struct filesystem_configuration_s *src_fs);
static int cache_config_files(
    struct server_configuration_s *config_s,
    char *global_config_filename,
    char *server_config_filename);
static int is_populated_filesystem_configuration(
    struct filesystem_configuration_s *fs);
static int is_root_handle_in_a_meta_range(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);
static int is_valid_filesystem_configuration(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);
static char *get_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs,
    int meta_handle_range);
static host_alias_s *find_host_alias_ptr_by_alias(
    struct server_configuration_s *config_s,
    char *alias);
static struct host_handle_mapping_s *get_or_add_handle_mapping(
    PINT_llist *list,
    char *alias);
static int build_extent_array(
    char *handle_range_str,
    PVFS_handle_extent_array *handle_extent_array);

#ifdef __PVFS2_TROVE_SUPPORT__
static int is_root_handle_in_my_range(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);
#endif

/* 
 * NOTE: The documentation for the server config format is generated
 * from the following static array.  The documentation for an option
 * is taken from the comments found before the beginning of each sub-structure
 * as shown.
 */
static const configoption_t options[] =
{
    /* 
     * Specifies a string identifier for the pvfs2 server that is to be
     * run on this host.  The format of this string is:
     *
     * {transport}://{hostname}:{port}
     *
     * Where {transport} is one of the possible BMI transport modules
     * (tcp, ib, gm).  Example:
     *
     * tcp://myhost.mydn:12345
     *
     */
    {"HostID",ARG_STR, get_pvfs_server_id,NULL,CTX_GLOBAL,NULL},
    
    /* Specifies the local path for the pvfs2 server to use as storage space.
     * Example:
     *
     * /tmp/pvfs.storage
     */
    {"StorageSpace",ARG_STR, get_storage_space,NULL,CTX_GLOBAL,NULL},

    /* Specifies the beginning of the Defaults context.  Options specified
     * within the Defaults context are used as default values over all the
     * pvfs2 server specific config files.
     */
    {"<Defaults>",ARG_NONE, enter_defaults_context,NULL,CTX_GLOBAL,NULL},

    /* Specifies the end-tag for the Defaults context.
     */
    {"</Defaults>",ARG_NONE, exit_defaults_context,NULL,CTX_DEFAULTS,NULL},

    /* Specifies the beginning of the Aliases context.  This groups 
     * the Alias mapping options.
     *
     * The Aliases context should be defined before any FileSystem contexts
     * are defined, as options in the FileSystem context usually need to
     * reference the aliases defined in this context.
     */
    {"<Aliases>",ARG_NONE, enter_aliases_context,NULL,CTX_GLOBAL,NULL},

    /* Specifies the end-tag for the Aliases context.
     */
    {"</Aliases>",ARG_NONE, exit_aliases_context,NULL,CTX_ALIASES,NULL},

    /* Specifies an alias in the form of a non-whitespace string that
     * can be used to reference a BMI server address (a HostID).  This
     * allows us to reference individual servers by an alias instead of their
     * full HostID.  The format of the Alias option is:
     *
     * Alias {alias string} {bmi address}
     *
     * As an example:
     *
     * Alias mynode1 tcp://hostname1.clustername1.domainname:12345
     */
    {"Alias",ARG_LIST, get_alias_list,NULL,CTX_ALIASES,NULL},

    /* Specifies the beginning of a Filesystem context.  This groups
     * options specific to a filesystem.  A pvfs2 server may manage
     * more than one filesystem, so a config file may have more than
     * one Filesystem context, each defining the parameters of a different
     * Filesystem.
     */
    {"<FileSystem>",ARG_NONE, enter_filesystem_context,NULL,CTX_GLOBAL,NULL},

    /* Specifies the end-tag of a Filesystem context.
     */
    {"</FileSystem>",ARG_NONE, exit_filesystem_context,NULL,CTX_FILESYSTEM,
        NULL},

    /* Specifies the beginning of a StorageHints context.  This groups
     * options specific to a filesystem and related to the behavior of the
     * storage system.  Mostly these options are passed directly to the
     * TROVE storage module which may or may not support them.  The
     * DBPF module (the only TROVE module implemented at present) supports
     * all of them.
     */
    {"<StorageHints>",ARG_NONE, enter_storage_hints_context,NULL,
        CTX_FILESYSTEM,NULL},

    /* Specifies the end-tag of the StorageHints context.
     */
    {"</StorageHints>",ARG_NONE, exit_storage_hints_context,NULL,
        CTX_STORAGEHINTS,NULL},

    /* This context groups together the Range options that define valid values
     * for meta handles on a per-host basis for this filesystem.
     *
     * The MetaHandleRanges context is required to be present in a
     * Filesystem context.
     */
    {"<MetaHandleRanges>",ARG_NONE, enter_mhranges_context,NULL,
        CTX_FILESYSTEM,NULL},

    /* Specifies the end-tag for the MetaHandleRanges context.
     */
    {"</MetaHandleRanges>",ARG_NONE, exit_mhranges_context,NULL,
        CTX_METAHANDLERANGES,NULL},

    /* This context groups together the Range options that define valid values
     * for the data handles on a per-host basis for this filesystem.
     *
     * A DataHandleRanges context is required to be present in a
     * Filesystem context.
     */
    {"<DataHandleRanges>",ARG_NONE, enter_dhranges_context,NULL,
        CTX_FILESYSTEM,NULL},

    /* Specifies the end-tag for the DataHandleRanges context.
     */
    {"</DataHandleRanges>",ARG_NONE, exit_dhranges_context,NULL,
        CTX_DATAHANDLERANGES,NULL},
    
    /* Provides a context for defining the filesystem's default
     * distribution to use and the parameters to be set for that distribution.
     *
     * Valid options within the Distribution context are Name, Param, and Value.
     *
     * This context is an optional context within the Filesystem context.  If
     * not specified, the filesystem defaults to the simple-stripe distribution.
     */
    {"<Distribution>",ARG_NONE, enter_distribution_context,NULL,
        CTX_FILESYSTEM,NULL},

    /* Specifies the end-tag for the Distribution context.
     */
    {"</Distribution>",ARG_NONE, exit_distribution_context,NULL,
        CTX_DISTRIBUTION,NULL},

    /* As logical files are created in pvfs2, the data files and meta files
     * that represent them are given filesystem unique handle values.  The
     * user can specify a range of values (or set of ranges) 
     * to be allocated to data files and meta files for a particular server,
     * using the Range option in the DataHandleRanges and MetaHandleRanges
     * contexts.  Note that in most cases, its easier to let the 
     * pvfs2-genconfig script determine the best ranges to specify.
     *
     * This option specifies a range of handle values that can be used for 
     * a particular pvfs2 server in a particular context (meta handles
     * or data handles).  The DataHandleRanges and MetaHandleRanges contexts
     * should contain one or more Range options.  The format is:
     *
     * Range {alias} {min value1}-{max value1}[, {min value2}-{max value2},...]
     *
     * Where {alias} is one of the alias strings already specified in the
     * Aliases context.
     *
     * {min value} and {max value} are positive integer values that specify
     * the range of possible handles that can be given out for that particular
     * host.  {max value} must be less than 18446744073709551615 (UINT64_MAX).
     *
     * As shown in the specified format, multiple ranges can be specified for
     * the same alias.  The format requires that max value of a given range
     * is less than the min value of the next one, 
     * i.e. {max value1}<{min value2}
     * 
     * Example of a Range option for data handles:
     *
     * Range mynode1 2147483651-4294967297
     */
    {"Range",ARG_LIST, get_range_list,NULL,
        CTX_METAHANDLERANGES|CTX_DATAHANDLERANGES,NULL},

    /* Specifies the handle value for the root of the Filesystem.  This
     * is a required option in the Filesystem context.  The format is:
     *
     * RootHandle {handle value}
     *
     * Where {handle value} is a positive integer no greater than 
     * 18446744073709551615 (UIN64_MAX).
     *
     * In general its best to let the pvfs2-genconfig script specify a
     * RootHandle value for the filesystem.
     */
    {"RootHandle",ARG_STR, get_root_handle,NULL,
        CTX_FILESYSTEM,NULL},

    /* This option specifies the name of the particular filesystem or
     * distribution that its defined in.  It is a required option in
     * Filesystem and Distribution contexts.
     */
    {"Name",ARG_STR, get_name,NULL,
        CTX_FILESYSTEM|CTX_DISTRIBUTION,NULL},

    /* A pvfs2 server may manage more than one filesystem, and so a
     * unique identifier is used to represent each one.  
     * This option specifies such an ID (sometimes called a 'collection
     * id') for the filesystem it is defined in.  
     *
     * The ID value can be any positive integer, no greater than
     * 2147483647 (INT32_MAX).  It is a required option in the Filesystem
     * context.
     */
    {"ID",ARG_INT, get_filesystem_collid,NULL,
        CTX_FILESYSTEM,NULL},

    /* The gossip interface in pvfs2 allows users to specify different
     * levels of logging for the pvfs2 server.  The output of these
     * different log levels is written to a file, which is specified in
     * this option.  The value of the option must be the path pointing to a 
     * file with valid write permissions.  The Logfile option can be
     * specified for all the pvfs2 servers in the Defaults context or for
     * a particular server in the Global context.
     */
    {"LogFile",ARG_STR, get_logfile,NULL,
        CTX_DEFAULTS|CTX_GLOBAL,"/tmp/pvfs2-server.log"},

    /* The gossip interface in pvfs2 allows users to specify different
     * levels of logging for the pvfs2 server.  This option sets that level for
     * either all servers (by being defined in the Defaults context) or for
     * a particular server by defining it in the Global context.  Possible
     * values for event logging are:
     *
     * __EVENTLOGGING__
     *
     * The value of the EventLogging option can be a comma separated list
     * of the above values.  Individual values can also be negated with
     * a '-'.  Examples of possible values are:
     *
     * EventLogging flow,msgpair,io
     * EventLogging -storage
     * EventLogging -flow,-flowproto
     */
    {"EventLogging",ARG_LIST, get_event_logging_list,NULL,
        CTX_DEFAULTS|CTX_GLOBAL,"none,"},

    /* At startup each pvfs2 server allocates space for a set number
     * of incoming requests to prevent the allocation delay at the beginning
     * of each unexpected request.  This parameter specifies the number
     * of requests for which to allocate space.
     *
     * If set in the Defaults context, its value will be used for all servers.
     * The default value can also be overwritten by setting a separate value
     * in the Global context.
     */
     {"UnexpectedRequests",ARG_INT, get_unexp_req,NULL,
         CTX_DEFAULTS|CTX_GLOBAL,"50"},

     /* Specifies the timeout value in seconds for BMI jobs on the server.
      */
     {"ServerJobBMITimeoutSecs",ARG_INT, get_server_job_bmi_timeout,NULL,
         CTX_DEFAULTS|CTX_GLOBAL, "30"},
     
     /* Specifies the timeout value in seconds for TROVE jobs on the server.
      */
     {"ServerJobFlowTimeoutSecs",ARG_INT, get_server_job_flow_timeout,NULL,
         CTX_DEFAULTS|CTX_GLOBAL, "30"},
     
     /* Specifies the timeout value in seconds for BMI jobs on the client.
      */
     {"ClientJobBMITimeoutSecs",ARG_INT, get_client_job_bmi_timeout,NULL,
         CTX_DEFAULTS|CTX_GLOBAL, "300"},

     /* Specifies the timeout value in seconds for FLOW jobs on the client.
      */
     {"ClientJobFlowTimeoutSecs",ARG_INT, get_client_job_flow_timeout,NULL,
         CTX_DEFAULTS|CTX_GLOBAL, "300"},

     /* Specifies the number of retry attempts for operations (when possible)
      */
     {"ClientRetryLimit",ARG_INT, get_client_retry_limit,NULL,
         CTX_DEFAULTS|CTX_GLOBAL, "5"},

     /* Specifies the delay in milliseconds to wait between retries.
      */
     {"ClientRetryDelayMilliSecs",ARG_INT, get_client_retry_delay,NULL,
         CTX_DEFAULTS|CTX_GLOBAL, "2000"},

     /* This specifies the frequency (in milliseconds) 
      * that performance monitor should be updated
      * when the pvfs2 server is running in admin mode.
      *
      * Can be set in either Default or Global contexts.
      */
    {"PerfUpdateInterval",ARG_INT, get_perf_update_interval,NULL,
        CTX_DEFAULTS|CTX_GLOBAL,"1000"},

    /* List the BMI modules to load when the server is started.  At present,
     * only tcp, infiniband, and myrinet are valid BMI modules.  
     * The format of the list is a comma separated list of one of:
     *
     * bmi_tcp
     * bmi_ib
     * bmi_gm
     *
     * For example:
     *
     * BMIModules bmi_tcp,bmi_ib
     *
     * Note that only the bmi modules compiled into pvfs2 should be
     * specified in this list.  The BMIModules option can be specified
     * in either the Defaults or Global contexts.
     */
    {"BMIModules",ARG_LIST, get_bmi_module_list,NULL,
        CTX_DEFAULTS|CTX_GLOBAL,NULL},
    
    /* List the flow modules to load when the server is started.  The modules
     * available for loading currently are:
     *
     * flowproto_multiqueue - A flow module that handles all the possible flows,
     * bmi->trove, trove->bmi, mem->bmi, bmi->mem.  At present, this is the
     * default and only available flow for production use.
     *
     * flowproto_bmi_cache - A flow module that enables the use of the NCAC
     * (network-centric adaptive cache) in the pvfs2 server.  Since the NCAC
     * is currently disable and unsupported, this module exists as a proof
     * of concept only.
     *
     * flowproto_dump_offsets - Used for debugging, this module allows the
     * developer to see what/when flows are being posted, without making
     * any actual BMI or TROVE requests.  This should only be used if you
     * know what you're doing.
     *
     */
    {"FlowModules",ARG_LIST, get_flow_module_list,NULL,
        CTX_DEFAULTS|CTX_GLOBAL,"flowproto_multiqueue,"},

    /* The TROVE storage layer has a management component that deals with
     * allocating handle values for new metafiles and datafiles.  The underlying
     * trove module can be given a hint to tell it how long to wait before
     * reusing handle values that have become freed up (only deleting files will
     * free up a handle).  The HandleRecycleTimeoutSecs option specifies
     * the number of seconds to wait for each filesystem.  This is an
     * optional parameter that can be specified in the StorageHints context.
     */
    {"HandleRecycleTimeoutSecs", ARG_INT,
         get_handle_recycle_timeout_seconds, NULL, 
         CTX_STORAGEHINTS,"360"},
    
    /* The TROVE layer has an attribute caching component that handles
     * caching of stored attributes.  This is used to improve the performance of
     * metadata accesses.  The AttrCacheKeywords option is a list of the
     * object types that should get cached in the attribute cache.  
     * The possible values for this option are:
     *
     * datafile_handles - This will cache the array of datafile handles for
     *                    each logical file in this filesystem
     * 
     * metafile_dist - This will cache (for each logical file)
     *                 the file distribution information used to create/manage
     *                 the datafiles.  
     *
     * dir_ent - This will cache the handles of the directory entries in this
     *           filesystem
     *
     * symlink_target - This will cache the target path for the symbolic links
     *                  in this filesystem
     *
     * The format of this option is a comma-separated list of one or more
     * of the above values.  For example:
     *
     * AttrCacheKeywords datafile_handles,metafile_dist,dir_ent
     */
    {"AttrCacheKeywords",ARG_LIST, get_attr_cache_keywords_list,NULL,
        CTX_STORAGEHINTS,
        "datafile_handles,metafile_dist,dir_ent,symlink_target,"},
    
    /* The attribute cache in the TROVE layer mentioned in the documentation
     * for the AttrCacheKeywords option is managed as a hashtable.  The
     * AttrCacheSize adjusts the number of buckets that this hashtable contains.
     * This value can be adjusted for better performance.  A good hashtable
     * size should always be a prime number.
     */
    {"AttrCacheSize",ARG_INT, get_attr_cache_size, NULL,
        CTX_STORAGEHINTS,"511"},

    /* This option specifies the max cache size of the attribute cache 
     * in the TROVE layer mentioned in the documentation
     * for the AttrCacheKeywords option.  This value can be adjusted for
     * better performance.
     */
    {"AttrCacheMaxNumElems",ARG_INT,get_attr_cache_max_num_elems,NULL,
        CTX_STORAGEHINTS,"1024"},
    
    /* The TroveSyncMeta option allows users to turn off metadata
     * synchronization with every metadata write.  This can greatly improve
     * performance.  In general, this value should probably be set to yes,
     * otherwise metadata transaction could be lost in the event of server
     * failover.
     */
    {"TroveSyncMeta",ARG_STR, get_trove_sync_meta, NULL, 
        CTX_STORAGEHINTS,"yes"},

    /* The TroveSyncData option allows users to turn off datafile
     * synchronization with every write operation.  This can greatly improve
     * performance, but may cause lost data in the event of server failover.
     */
    {"TroveSyncData",ARG_STR, get_trove_sync_data, NULL, 
        CTX_STORAGEHINTS,"yes"},

    /* Specifies the format of the date/timestamp that events will have
     * in the event log.  Possible values are:
     *
     * usec: [%H:%M:%S
     *
     * datetime: [%m/%d %H:%M]
     *
     * none
     *
     * The format of the option is one of the above values.  For example,
     *
     * LogStamp datetime
     */
    {"LogStamp",ARG_STR, get_logstamp,NULL,
        CTX_DEFAULTS|CTX_GLOBAL,"usec"},
    
    /* This option specifies a parameter name to be passed to the 
     * distribution to be used.  This option should be immediately
     * followed by a Value option.
     */
    {"Param", ARG_STR, get_param, NULL, 
        CTX_DISTRIBUTION,NULL},
    
    /* This option specifies the value of the parameter who's name
     * was specified in the previous option.
     */
    {"Value", ARG_INT, get_value, NULL, 
        CTX_DISTRIBUTION,NULL},
    
    /* This option specifies the default number of datafiles to use
     * when a new file is created.  The value is passed to the distribution
     * and it determines whether to use that value or not.
     */
    {"DefaultNumDFiles", ARG_INT, get_default_num_dfiles, NULL,
        CTX_FILESYSTEM,"0"},

    LAST_OPTION
};

/*
 * Function: PINT_parse_config
 *
 * Params:   struct server_configuration_s*,
 *           global_config_filename - common config file for all servers
 *                                    and clients
 *           server_config_filename - config file specific to one server
 *                                    (ignored on client side)
 *
 * Returns:  0 on success; 1 on failure
 *
 */
int PINT_parse_config(
    struct server_configuration_s *config_obj,
    char *global_config_filename,
    char *server_config_filename)
{
    struct server_configuration_s *config_s;
    configfile_t *configfile = (configfile_t *)0;

    if (!config_obj)
    {
        gossip_err("Invalid server_configuration_s object\n");
        return 1;
    }

    /* static global assignment */
    config_s = config_obj;
    memset(config_s, 0, sizeof(struct server_configuration_s));

    /* set some global defaults for optional parameters */
    config_s->logstamp_type = GOSSIP_LOGSTAMP_DEFAULT;
    config_s->server_job_bmi_timeout = PVFS2_SERVER_JOB_BMI_TIMEOUT_DEFAULT;
    config_s->server_job_flow_timeout = PVFS2_SERVER_JOB_FLOW_TIMEOUT_DEFAULT;
    config_s->client_job_bmi_timeout = PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT;
    config_s->client_job_flow_timeout = PVFS2_CLIENT_JOB_FLOW_TIMEOUT_DEFAULT;
    config_s->client_retry_limit = PVFS2_CLIENT_RETRY_LIMIT_DEFAULT;
    config_s->client_retry_delay_ms = PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT;

    if (cache_config_files(
            config_s, global_config_filename, server_config_filename))
    {
        return 1;
    }
    assert(config_s->fs_config_buflen && config_s->fs_config_buf);
    assert(config_s->server_config_buflen && config_s->server_config_buf);

    /* first read in the fs.conf defaults config file */
    config_s->configuration_context = CTX_GLOBAL;
    configfile = PINT_dotconf_create(config_s->fs_config_filename,
                                     options, (void *)config_s, 
                                     CASE_INSENSITIVE);
    if (!configfile)
    {
        gossip_err("Error opening config file %s\n",
                   config_s->fs_config_filename);
        return 1;
    }
    configfile->errorhandler = (dotconf_errorhandler_t)errorhandler;
    configfile->contextchecker = (dotconf_contextchecker_t)contextchecker;
    
    if(PINT_dotconf_command_loop(configfile) == 0)
    {
        /* NOTE: dotconf error handler will log message */
        return 1;
    }
    PINT_dotconf_cleanup(configfile);

    /* then read in the server.conf (host specific) config file */
    config_s->configuration_context = CTX_GLOBAL;
    configfile = PINT_dotconf_create(config_s->server_config_filename,
                                options, (void *)config_s, CASE_INSENSITIVE);
    if (!configfile)
    {
        gossip_err("Error opening config file: %s\n",
                   config_s->server_config_filename);
        return 1;
    }
    configfile->errorhandler = (dotconf_errorhandler_t)errorhandler;
    configfile->contextchecker = (dotconf_contextchecker_t)contextchecker;

    if (PINT_dotconf_command_loop(configfile) == 0)
    {
        /* NOTE: dotconf error handler will log message */
        return 1;
    }
    PINT_dotconf_cleanup(configfile);

    if (!config_s->host_id)
    {
        gossip_err("Configuration file error. "
                   "No host ID specified.\n");
        return 1;
    }

    if (!config_s->storage_path)
    {
        gossip_err("Configuration file error. "
                   "No storage path specified.\n");
        return 1;
    }

    if (!config_s->bmi_modules)
    {
	gossip_err("Configuration file error. "
                   "No BMI modules specified.\n");
	return 1;
    }

    /* We set to the default flow module since there's only one.
    */
    if (!config_s->flow_modules)
    {
        gossip_err("Configuration file error. No flow module specified\n");
	return 1;
    }
    
    /* Users don't need to learn about this unless they want to
    */
    if (!config_s->perf_update_interval)
    {
        gossip_err("Configuration file error.  "
                   "No PerfUpdateInterval specified.\n");
        return 1;
    }
    
    return 0;
}

const char *contextchecker(command_t *cmd, unsigned long mask)
{
    struct server_configuration_s *config_s = cmd->context;

    if(!(mask & config_s->configuration_context))
    {
        return "Option can't be defined in that context";
    }
    return NULL;
}
    
FUNC_ERRORHANDLER(errorhandler)
{
    gossip_err("Error: %s line %ld: %s", configfile->filename,
        configfile->line, msg);
    return(1);
}

DOTCONF_CB(get_pvfs_server_id)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    if (config_s->host_id)
    {
        gossip_err("WARNING: HostID value being overwritten (from "
                   "%s to %s).\n",config_s->host_id,cmd->data.str);
        free(config_s->host_id);
    }
    config_s->host_id = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_logstamp)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if(!strcmp(cmd->data.str, "none"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_NONE;
    }
    else if(!strcmp(cmd->data.str, "usec"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_USEC;
    }
    else if(!strcmp(cmd->data.str, "datetime"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_DATETIME;
    }
    else
    {
        return("LogStamp tag (if specified) must have one of the following values: none, usec, or datetime.\n");
    }

    return NULL;
}


DOTCONF_CB(get_storage_space)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    if (config_s->storage_path)
    {
        gossip_err("WARNING: StorageSpace value being overwritten.\n");
        free(config_s->storage_path);
    }
    config_s->storage_path =
        (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(enter_defaults_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_DEFAULTS;

    return PINT_dotconf_set_defaults(
        cmd->configfile, CTX_DEFAULTS);
}

DOTCONF_CB(exit_defaults_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(enter_aliases_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_ALIASES;
    return NULL;
}

DOTCONF_CB(exit_aliases_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(enter_filesystem_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->host_aliases == NULL)
    {
        return("Error in context.  Filesystem tag cannot "
                   "be declared before an Aliases tag.\n");
    }

    fs_conf = (struct filesystem_configuration_s *)
        malloc(sizeof(struct filesystem_configuration_s));
    assert(fs_conf);
    memset(fs_conf,0,sizeof(struct filesystem_configuration_s));

    /* fill any fs defaults here */
    fs_conf->flowproto = FLOWPROTO_DEFAULT;
    fs_conf->encoding = ENCODING_DEFAULT;
    fs_conf->trove_sync_meta = TROVE_SYNC;
    fs_conf->trove_sync_data = TROVE_SYNC;

    if (!config_s->file_systems)
    {
        config_s->file_systems = PINT_llist_new();
    }
    PINT_llist_add_to_head(config_s->file_systems,(void *)fs_conf);
    assert(PINT_llist_head(config_s->file_systems) == (void *)fs_conf);
    config_s->configuration_context = CTX_FILESYSTEM;

    return PINT_dotconf_set_defaults(
        cmd->configfile,
        CTX_FILESYSTEM);
}

DOTCONF_CB(exit_filesystem_context)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    /*
      make sure last fs config object is valid
      (i.e. has all required values filled in)
    */
    if (!is_populated_filesystem_configuration(fs_conf))
    {
        gossip_err("Error: Filesystem configuration is invalid!\n");
        return("Possible Error in context.  Cannot have /Filesystem "
                   "tag before all filesystem attributes are declared.\n");
    }

    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(enter_storage_hints_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_STORAGEHINTS;

    return PINT_dotconf_set_defaults(
        cmd->configfile, CTX_STORAGEHINTS);
}

DOTCONF_CB(exit_storage_hints_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}


DOTCONF_CB(enter_mhranges_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_METAHANDLERANGES;
    return NULL;
}

DOTCONF_CB(exit_mhranges_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if (!fs_conf->meta_handle_ranges)
    {
        return("No valid mhandle ranges added to file system.\n");
    }
    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}

DOTCONF_CB(enter_dhranges_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_DATAHANDLERANGES;
    return NULL;
}

DOTCONF_CB(exit_dhranges_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if (!fs_conf->data_handle_ranges)
    {
        return("No valid dhandle ranges added to file system.\n");
    }
    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}

DOTCONF_CB(enter_distribution_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_DISTRIBUTION;
    return NULL;
}

DOTCONF_CB(exit_distribution_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(get_unexp_req)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->initial_unexpected_requests = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_server_job_bmi_timeout)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->server_job_bmi_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_server_job_flow_timeout)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->server_job_flow_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_job_bmi_timeout)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->client_job_bmi_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_job_flow_timeout)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->client_job_flow_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_retry_limit)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->client_retry_limit = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_retry_delay)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->client_retry_delay_ms = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_perf_update_interval)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->perf_update_interval = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_logfile)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    config_s->logfile = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_event_logging_list)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;

    if (config_s->event_logging != NULL)
    {
        len = strlen(config_s->event_logging);
        strncpy(ptr,config_s->event_logging,len);
        ptr += (len * sizeof(char));
        free(config_s->event_logging);
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    config_s->event_logging = strdup(buf);
    return NULL;
}

DOTCONF_CB(get_event_log_size)
{
    int res;
    
    if ((config_s->configuration_context != DEFAULTS_CONFIG) &&
        (config_s->configuration_context != GLOBAL_CONFIG))
    {
        gossip_err("EventLogSize Tag can only be in a "
                   "Defaults or Global block");
        return NULL;
    }

    if(cmd->data.value < 0)
    {
        gossip_err("EventLogSize value of %d "
                   "is not a valid non-negative integer", cmd->data.value);
        return NULL;
    }
    config_s->event_log_size = (ssize_t)cmd->data.value;
}

DOTCONF_CB(get_flow_module_list)
{
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;

    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->flow_modules != NULL)
    {
        len = strlen(config_s->flow_modules);
        strncpy(ptr,config_s->flow_modules,len);
        ptr += (len * sizeof(char));
        free(config_s->flow_modules);
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    config_s->flow_modules = strdup(buf);
    return NULL;
}


DOTCONF_CB(get_bmi_module_list)
{
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;

    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->bmi_modules != NULL)
    {
        len = strlen(config_s->bmi_modules);
        strncpy(ptr,config_s->bmi_modules,len);
        ptr += (len * sizeof(char));
        free(config_s->bmi_modules);
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    config_s->bmi_modules = strdup(buf);
    return NULL;
}

DOTCONF_CB(get_handle_recycle_timeout_seconds)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    fs_conf->handle_recycle_timeout_sec.tv_sec = (int)cmd->data.value;
    fs_conf->handle_recycle_timeout_sec.tv_usec = 0;

    return NULL;
}

DOTCONF_CB(get_attr_cache_keywords_list)
{
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;
    struct filesystem_configuration_s *fs_conf = NULL;

    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if (fs_conf->attr_cache_keywords != NULL)
    {
        len = strlen(fs_conf->attr_cache_keywords);
        strncpy(ptr,fs_conf->attr_cache_keywords,len);
        ptr += (len * sizeof(char));
        if (*(ptr-1) != ',')
        {
            *ptr = ',';
            ptr++;
        }
        free(fs_conf->attr_cache_keywords);
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    fs_conf->attr_cache_keywords = strdup(buf);
    return NULL;
}

DOTCONF_CB(get_attr_cache_size)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    fs_conf->attr_cache_size = (int)cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_attr_cache_max_num_elems)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    fs_conf->attr_cache_max_num_elems = (int)cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_trove_sync_meta)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if(strcasecmp(cmd->data.str, "yes") == 0)
    {
        fs_conf->trove_sync_meta = TROVE_SYNC;
    }
    else if(strcasecmp(cmd->data.str, "no") == 0)
    {
        fs_conf->trove_sync_meta = 0;
    }
    else
    {
        return("TroveSyncMeta value must be 'yes' or 'no'.\n");
    }
#ifndef HAVE_DB_DIRTY_READ
    if (fs_conf->trove_sync_meta != TROVE_SYNC)
    {
        gossip_err("WARNING: Forcing TroveSyncMeta to be yes instead of %s\n",
                   cmd->data.str);
        gossip_err("WARNING: Non-sync mode is NOT supported without "
                   "DB_DIRTY_READ support.\n");
        fs_conf->trove_sync_meta = TROVE_SYNC;
    }
#endif

    return NULL;
}

DOTCONF_CB(get_trove_sync_data)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if(strcasecmp(cmd->data.str, "yes") == 0)
    {
        fs_conf->trove_sync_data = TROVE_SYNC;
    }
    else if(strcasecmp(cmd->data.str, "no") == 0)
    {
        fs_conf->trove_sync_data = 0;
    }
    else
    {
        return("TroveSyncData value must be 'yes' or 'no'.\n");
    }

    return NULL;
}

DOTCONF_CB(get_root_handle)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    unsigned long long int tmp_var;
    int ret = -1;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);
    ret = sscanf(cmd->data.str, "%Lu", &tmp_var);
    if(ret != 1)
    {
        return("RootHandle does not have a long long unsigned value.\n");
    }
    fs_conf->root_handle = (PVFS_handle)tmp_var;
    return NULL;
}

DOTCONF_CB(get_name)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    if (config_s->configuration_context == CTX_FILESYSTEM)
    {
        struct filesystem_configuration_s *fs_conf = NULL;

        fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
        if (fs_conf->file_system_name)
        {
            gossip_err("WARNING: Overwriting %s with %s\n",
                       fs_conf->file_system_name,cmd->data.str);
        }
        fs_conf->file_system_name =
            (cmd->data.str ? strdup(cmd->data.str) : NULL);
    }
    else if (config_s->configuration_context == CTX_DISTRIBUTION)
    {
        if (0 == config_s->default_dist_config.name)
        {
            config_s->default_dist_config.name =
                (cmd->data.str ? strdup(cmd->data.str) : NULL);
            config_s->default_dist_config.param_list = PINT_llist_new();
        }
        else
        {
            return "Only one distribution configuration is allowed.\n";
        }
    }
    return NULL;
}

DOTCONF_CB(get_filesystem_collid)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    if (fs_conf->coll_id)
    {
        gossip_err("WARNING: Overwriting %d with %d\n",
                   (int)fs_conf->coll_id,(int)cmd->data.value);
    }
    fs_conf->coll_id = (PVFS_fs_id)cmd->data.value;
    return NULL;
}

static int compare_aliases(void * vkey,
                           void * valias2)
{
    char * hostaliaskey1 = (char *)vkey;
    host_alias_s * alias2 = (host_alias_s *)valias2;
    
    return strcmp(hostaliaskey1, alias2->host_alias);
}

DOTCONF_CB(get_alias_list)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    struct host_alias_s *cur_alias = NULL;

    assert(cmd->arg_count == 2);

    /* prevent users from adding the same alias twice */
    if(config_s->host_aliases &&
       PINT_llist_search(config_s->host_aliases, 
                      (void *)cmd->data.list[0],
                      compare_aliases))
    {
        return "Error: alias already defined";
    }

    cur_alias = (host_alias_s *)
        malloc(sizeof(host_alias_s));
    cur_alias->host_alias = strdup(cmd->data.list[0]);
    cur_alias->bmi_address = strdup(cmd->data.list[1]);

    if (!config_s->host_aliases)
    {
        config_s->host_aliases = PINT_llist_new();
    }
    
    PINT_llist_add_to_tail(config_s->host_aliases,(void *)cur_alias);
    return NULL;
}

DOTCONF_CB(get_range_list)
{
    int i = 0, is_new_handle_mapping = 0;
    struct filesystem_configuration_s *fs_conf = NULL;
    struct host_handle_mapping_s *handle_mapping = NULL;
    PINT_llist **handle_range_list = NULL;
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    handle_range_list = ((config_s->configuration_context ==
                          CTX_METAHANDLERANGES) ?
                         &fs_conf->meta_handle_ranges :
                         &fs_conf->data_handle_ranges);

    if (*handle_range_list == NULL)
    {
        *handle_range_list = PINT_llist_new();
    }

    for(i = 0; i < cmd->arg_count; i += 2)
    {
        if (is_valid_alias(config_s->host_aliases, cmd->data.list[i]))
        {
            i++;
            assert(cmd->data.list[i]);

            if (is_valid_handle_range_description(cmd->data.list[i]))
            {
                handle_mapping = get_or_add_handle_mapping(
                    *handle_range_list, cmd->data.list[i-1]);
                if (!handle_mapping)
                {
                    return("Error: Alias allocation failed; "
                               "aborting alias handle range addition!\n");
                }

                if (!handle_mapping->alias_mapping)
                {
                    is_new_handle_mapping = 1;
                    handle_mapping->alias_mapping =
                        find_host_alias_ptr_by_alias(
                            config_s, cmd->data.list[i-1]);
                }

                assert(handle_mapping->alias_mapping ==
                       find_host_alias_ptr_by_alias(
                           config_s, cmd->data.list[i-1]));

                if (!handle_mapping->handle_range &&
                    !handle_mapping->handle_extent_array.extent_array)
                {
                    handle_mapping->handle_range =
                        strdup(cmd->data.list[i]);

                    /* build the extent array, based on range */
                    build_extent_array(
                        handle_mapping->handle_range,
                        &handle_mapping->handle_extent_array);
                }
                else
                {
                    char *new_handle_range = PINT_merge_handle_range_strs(
                        handle_mapping->handle_range, cmd->data.list[i]);
                    free(handle_mapping->handle_range);
                    handle_mapping->handle_range = new_handle_range;

                    /* re-build the extent array, based on range */
                    free(handle_mapping->handle_extent_array.extent_array);
                    build_extent_array(handle_mapping->handle_range,
                                       &handle_mapping->handle_extent_array);
                }

                if (is_new_handle_mapping)
                {
                    PINT_llist_add_to_tail(*handle_range_list,
                                      (void *)handle_mapping);
                }
            }
            else
            {
                return("Error in handle range description.\n");
            }
        }
        else
        {
            return("Unrecognized alias.\n");
        }
    }
    return NULL;
}

DOTCONF_CB(get_param)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    distribution_param_configuration* param =
        malloc(sizeof(distribution_param_configuration));

    if (NULL != param)
    {
        memset(param, 0, sizeof(param));
        param->name = (cmd->data.str ? strdup(cmd->data.str) : NULL);
        PINT_llist_add_to_tail(config_s->default_dist_config.param_list,
                               param);
    }
    return NULL;
}

DOTCONF_CB(get_value)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    distribution_param_configuration* param;
    param = (distribution_param_configuration*)PINT_llist_tail(
        config_s->default_dist_config.param_list);
    if (NULL != param)
    {
        param->value = (PVFS_size)cmd->data.value;
    }
    return NULL;
}

DOTCONF_CB(get_default_num_dfiles)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
        PINT_llist_head(config_s->file_systems);

    fs_conf->default_num_dfiles = (int)cmd->data.value;
    return NULL;
}

/*
 * Function: PINT_config_release
 *
 * Params:   struct server_configuration_s*
 *
 * Returns:  void
 *
 * Synopsis: De-allocates memory consumed internally
 *           by the specified server_configuration_s
 *           
 */
void PINT_config_release(struct server_configuration_s *config_s)
{
    if (config_s)
    {
        if (config_s->host_id)
        {
            free(config_s->host_id);
            config_s->host_id = NULL;
        }

        if (config_s->storage_path)
        {
            free(config_s->storage_path);
            config_s->storage_path = NULL;
        }

        if (config_s->fs_config_filename)
        {
            free(config_s->fs_config_filename);
            config_s->fs_config_filename = NULL;
        }

        if (config_s->server_config_filename)
        {
            free(config_s->server_config_filename);
            config_s->server_config_filename = NULL;
        }

        if (config_s->fs_config_buf)
        {
            free(config_s->fs_config_buf);
            config_s->fs_config_buf = NULL;
        }

        if (config_s->server_config_buf)
        {
            free(config_s->server_config_buf);
            config_s->server_config_buf = NULL;
        }

        if (config_s->logfile)
        {
            free(config_s->logfile);
            config_s->logfile = NULL;
        }

        if (config_s->event_logging)
        {
            free(config_s->event_logging);
            config_s->event_logging = NULL;
        }

        if (config_s->bmi_modules)
        {
            free(config_s->bmi_modules);
            config_s->bmi_modules = NULL;
        }

        /* free all filesystem objects */
        if (config_s->file_systems)
        {
            PINT_llist_free(config_s->file_systems,free_filesystem);
            config_s->file_systems = NULL;
        }

        /* free all host alias objects */
        if (config_s->host_aliases)
        {
            PINT_llist_free(config_s->host_aliases,free_host_alias);
            config_s->host_aliases = NULL;
        }
    }
}

static int is_valid_alias(PINT_llist * host_aliases, char *str)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_alias;

    if (str)
    {
        cur = host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
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
            cur = PINT_llist_next(cur);
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
             fs->meta_handle_ranges && fs->data_handle_ranges &&
             fs->root_handle) ? 1 : 0);
}
    
static int is_root_handle_in_a_meta_range(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    PINT_llist *extent_list = NULL;
    char *cur_host_id = (char *)0;
    host_handle_mapping_s *cur_h_mapping = NULL;

    if (config && is_populated_filesystem_configuration(fs))
    {
        /*
          check if the root handle is within one of the
          specified meta host's handle ranges for this fs;
          a root handle can't exist in a data handle range!
        */
        cur = fs->meta_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            assert(cur_h_mapping->alias_mapping);
            assert(cur_h_mapping->alias_mapping->host_alias);
            assert(cur_h_mapping->alias_mapping->bmi_address);
            assert(cur_h_mapping->handle_range);

            cur_host_id = cur_h_mapping->alias_mapping->bmi_address;
            if (!cur_host_id)
            {
                gossip_err("Invalid host ID for alias %s.\n",
                           cur_h_mapping->alias_mapping->host_alias);
                break;
            }

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
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

static int is_valid_filesystem_configuration(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs)
{
    int ret = is_root_handle_in_a_meta_range(config,fs);
    if (ret == 0)
    {
        gossip_err("RootHandle (%Lu) is NOT within the meta handle "
                   "ranges specified for this filesystem (%s).\n",
                   Lu(fs->root_handle),fs->file_system_name);
    }
    return ret;
}

static void free_host_handle_mapping(void *ptr)
{
    struct host_handle_mapping_s *h_mapping =
        (struct host_handle_mapping_s *)ptr;
    if (h_mapping)
    {
        /*
          NOTE: h_mapping->alias_mapping is freed by free_host_alias,
          as the pointer points into the config_s->host_aliases list;
          it's not copied.
        */
        h_mapping->alias_mapping = NULL;

        free(h_mapping->handle_range);
        h_mapping->handle_range = NULL;

        free(h_mapping->handle_extent_array.extent_array);
        h_mapping->handle_extent_array.extent_count = 0;
        h_mapping->handle_extent_array.extent_array = NULL;

        free(h_mapping);
        h_mapping = NULL;
    }
}

static void free_host_alias(void *ptr)
{
    struct host_alias_s *alias = (struct host_alias_s *)ptr;
    if (alias)
    {
        free(alias->host_alias);
        alias->host_alias = NULL;

        free(alias->bmi_address);
        alias->bmi_address = NULL;

        free(alias);
        alias = NULL;
    }
}

static void free_filesystem(void *ptr)
{
    struct filesystem_configuration_s *fs =
        (struct filesystem_configuration_s *)ptr;

    if (fs)
    {
        free(fs->file_system_name);
        fs->file_system_name = NULL;

        /* free all handle ranges */
        PINT_llist_free(fs->meta_handle_ranges,free_host_handle_mapping);
        PINT_llist_free(fs->data_handle_ranges,free_host_handle_mapping);

        /* if the optional hints are used, free them */
        if (fs->attr_cache_keywords)
        {
            free(fs->attr_cache_keywords);
            fs->attr_cache_keywords = NULL;
        }

        free(fs);
        fs = NULL;
    }
}

static void copy_filesystem(
    struct filesystem_configuration_s *dest_fs,
    struct filesystem_configuration_s *src_fs)
{
    PINT_llist *cur = NULL;
    struct host_handle_mapping_s *cur_h_mapping = NULL;
    struct host_handle_mapping_s *new_h_mapping = NULL;

    if (dest_fs && src_fs)
    {
        dest_fs->file_system_name = strdup(src_fs->file_system_name);
        assert(dest_fs->file_system_name);

        dest_fs->coll_id = src_fs->coll_id;
        dest_fs->root_handle = src_fs->root_handle;
        dest_fs->default_num_dfiles = src_fs->default_num_dfiles;

        dest_fs->flowproto = src_fs->flowproto;
        dest_fs->encoding = src_fs->encoding;

        dest_fs->meta_handle_ranges = PINT_llist_new();
        dest_fs->data_handle_ranges = PINT_llist_new();

        assert(dest_fs->meta_handle_ranges);
        assert(dest_fs->data_handle_ranges);

        /* copy all meta handle ranges */
        cur = src_fs->meta_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }

            new_h_mapping = (struct host_handle_mapping_s *)
                malloc(sizeof(struct host_handle_mapping_s));
            assert(new_h_mapping);

            new_h_mapping->alias_mapping = (struct host_alias_s *)
                malloc(sizeof(struct host_alias_s));
            assert(new_h_mapping->alias_mapping);

            new_h_mapping->alias_mapping->host_alias =
                strdup(cur_h_mapping->alias_mapping->host_alias);
            assert(new_h_mapping->alias_mapping->host_alias);

            new_h_mapping->alias_mapping->bmi_address =
                strdup(cur_h_mapping->alias_mapping->bmi_address);
            assert(new_h_mapping->alias_mapping->bmi_address);

            new_h_mapping->handle_range =
                strdup(cur_h_mapping->handle_range);
            assert(new_h_mapping->handle_range);

            build_extent_array(new_h_mapping->handle_range,
                               &new_h_mapping->handle_extent_array);

            PINT_llist_add_to_tail(
                dest_fs->meta_handle_ranges, new_h_mapping);

            cur = PINT_llist_next(cur);
        }

        /* copy all data handle ranges */
        cur = src_fs->data_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }

            new_h_mapping = (struct host_handle_mapping_s *)
                malloc(sizeof(struct host_handle_mapping_s));
            assert(new_h_mapping);

            new_h_mapping->alias_mapping = (struct host_alias_s *)
                malloc(sizeof(struct host_alias_s));
            assert(new_h_mapping->alias_mapping);

            new_h_mapping->alias_mapping->host_alias =
                strdup(cur_h_mapping->alias_mapping->host_alias);
            assert(new_h_mapping->alias_mapping->host_alias);

            new_h_mapping->alias_mapping->bmi_address =
                strdup(cur_h_mapping->alias_mapping->bmi_address);
            assert(new_h_mapping->alias_mapping->bmi_address);

            new_h_mapping->handle_range =
                strdup(cur_h_mapping->handle_range);
            assert(new_h_mapping->handle_range);

            build_extent_array(new_h_mapping->handle_range,
                               &new_h_mapping->handle_extent_array);

            PINT_llist_add_to_tail(
                dest_fs->data_handle_ranges, new_h_mapping);

            cur = PINT_llist_next(cur);
        }

        /* if the optional hints are used, copy them too */
        if (src_fs->attr_cache_keywords)
        {
            dest_fs->attr_cache_keywords =
                strdup(src_fs->attr_cache_keywords);
            assert(dest_fs->attr_cache_keywords);
        }

        dest_fs->handle_recycle_timeout_sec =
            src_fs->handle_recycle_timeout_sec;
        dest_fs->attr_cache_size = src_fs->attr_cache_size;
        dest_fs->attr_cache_max_num_elems =
            src_fs->attr_cache_max_num_elems;
        dest_fs->trove_sync_meta = src_fs->trove_sync_meta;
        dest_fs->trove_sync_data = src_fs->trove_sync_data;
    }
}


static host_alias_s *find_host_alias_ptr_by_alias(
    struct server_configuration_s *config_s,
    char *alias)
{
    PINT_llist *cur = NULL;
    struct host_alias_s *ret = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && alias)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(cur_alias->host_alias,alias) == 0)
            {
                ret = cur_alias;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

static struct host_handle_mapping_s *get_or_add_handle_mapping(
    PINT_llist *list,
    char *alias)
{
    PINT_llist *cur = list;
    struct host_handle_mapping_s *ret = NULL;
    struct host_handle_mapping_s *handle_mapping = NULL;

    while(cur)
    {
        handle_mapping = PINT_llist_head(cur);
        if (!handle_mapping)
        {
            break;
        }
        assert(handle_mapping->alias_mapping);
        assert(handle_mapping->alias_mapping->host_alias);
        assert(handle_mapping->handle_range);

        if (strcmp(handle_mapping->alias_mapping->host_alias,
                   alias) == 0)
        {
            ret = handle_mapping;
            break;
        }
        cur = PINT_llist_next(cur);
    }

    if (!ret)
    {
        ret = (host_handle_mapping_s *)
            malloc(sizeof(struct host_handle_mapping_s));
        if (ret)
        {
            memset(ret,0,sizeof(struct host_handle_mapping_s));
        }
    }
    return ret;
}

static int build_extent_array(
    char *handle_range_str,
    PVFS_handle_extent_array *handle_extent_array)
{
    int i = 0, status = 0, num_extents = 0;
    PVFS_handle_extent cur_extent;

    if (handle_range_str && handle_extent_array)
    {
        /* first pass, find out how many extents there are total */
        while(PINT_parse_handle_ranges(handle_range_str,
                                       &cur_extent, &status))
        {
            num_extents++;
        }

        if (num_extents)
        {
            handle_extent_array->extent_count = num_extents;
            handle_extent_array->extent_array = (PVFS_handle_extent *)
                malloc(num_extents * sizeof(PVFS_handle_extent));
            if (!handle_extent_array->extent_array)
            {
                gossip_err("Error: failed to alloc %d extents\n",
                           handle_extent_array->extent_count);
                return -1;
            }
            memset(handle_extent_array->extent_array,0,
                   (num_extents * sizeof(PVFS_handle_extent)));

            /* reset opaque handle parsing state for next iteration */
            status = 0;

            /* second pass, fill in the extent array */
            while(PINT_parse_handle_ranges(handle_range_str,
                                           &cur_extent, &status))
            {
                handle_extent_array->extent_array[i] = cur_extent;
                i++;
            }
        }
    }
    return 0;
}

/*
 * Function: PINT_config_get_host_addr_ptr
 *
 * Params:   struct server_configuration_s*,
 *           char *alias
 *
 * Returns:  char * (bmi_address) on success; NULL on failure
 *
 * Synopsis: retrieve the bmi_address matching the specified alias
 *           
 */
char *PINT_config_get_host_addr_ptr(
    struct server_configuration_s *config_s,
    char *alias)
{
    char *ret = (char *)0;
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && alias)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
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
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
 * Function: PINT_config_get_host_alias_ptr
 *
 * Params:   struct server_configuration_s*,
 *           char *bmi_address
 *
 * Returns:  char * (alias) on success; NULL on failure
 *
 * Synopsis: retrieve the alias matching the specified bmi_address
 *           
 */
char *PINT_config_get_host_alias_ptr(
    struct server_configuration_s *config_s,
    char *bmi_address)
{
    char *ret = (char *)0;
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && bmi_address)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
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
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
 * Function: PINT_config_get_meta_handle_range_str
 *
 * Params:   struct server_configuration_s*,
 *           struct filesystem_configuration_s *fs
 *
 * Returns:  char * (handle range) on success; NULL on failure
 *
 * Synopsis: return the meta handle range (string) on the specified
 *           filesystem that matches the host specific configuration
 *           
 */
char *PINT_config_get_meta_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs)
{
    return get_handle_range_str(config_s,fs,1);
}

int PINT_config_get_meta_handle_extent_array(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id,
    PVFS_handle_extent_array *extent_array)
{
    int ret = -1;
    PINT_llist *cur = NULL;
    char *my_alias = NULL;
    filesystem_configuration_s *cur_fs = NULL;
    struct host_handle_mapping_s *cur_h_mapping = NULL;

    if (config_s && extent_array)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            if (cur_fs->coll_id == fs_id)
            {
                break;
            }
            cur = PINT_llist_next(cur);
        }

        if (cur_fs)
        {
            my_alias = PINT_config_get_host_alias_ptr(
                config_s, config_s->host_id);
            if (my_alias)
            {
                cur = cur_fs->meta_handle_ranges;

                while(cur)
                {
                    cur_h_mapping = PINT_llist_head(cur);
                    if (!cur_h_mapping)
                    {
                        break;
                    }

                    assert(cur_h_mapping->handle_range);
                    assert(cur_h_mapping->alias_mapping);
                    assert(cur_h_mapping->alias_mapping->host_alias);

                    if (strcmp(cur_h_mapping->alias_mapping->host_alias,
                               my_alias) == 0)
                    {
                        extent_array->extent_count = 
                            cur_h_mapping->handle_extent_array.extent_count;
                        extent_array->extent_array = malloc(
                            (extent_array->extent_count *
                             sizeof(PVFS_handle_extent)));
                        assert(extent_array->extent_array);
                        memcpy(extent_array->extent_array,
                               cur_h_mapping->handle_extent_array.extent_array,
                               (extent_array->extent_count *
                                sizeof(PVFS_handle_extent)));

                        ret = 0;
                        break;
                    }
                    cur = PINT_llist_next(cur);
                }
            }
        }
    }
    return ret;
}


/*
 * Function: PINT_config_get_data_handle_range_str
 *
 * Params:   struct server_configuration_s*,
 *           struct filesystem_configuration_s *fs
 *
 * Returns:  char * (handle range) on success; NULL on failure
 *
 * Synopsis: return the data handle range (string) on the specified
 *           filesystem that matches the host specific configuration
 *           
 */
char *PINT_config_get_data_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs)
{
    return get_handle_range_str(config_s,fs,0);
}

/*
 * Function: PINT_config_get_merged_handle_range_str
 *
 * Params:   struct server_configuration_s*,
 *           struct filesystem_configuration_s *fs
 *
 * Returns:  char * (handle range) on success; NULL on failure
 *           NOTE: The returned string MUST be freed by the caller
 *           if it's a non-NULL value
 *
 * Synopsis: return the meta handle range and data handle range strings
 *           on the specified filesystem that matches the host specific
 *           configuration merged as one single handle range
 *           
 */
char *PINT_config_get_merged_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs)
{
    char *merged_range = NULL;
    char *mrange = get_handle_range_str(config_s,fs,1);
    char *drange = get_handle_range_str(config_s,fs,0);

    if (mrange && drange)
    {
        merged_range = PINT_merge_handle_range_strs(mrange, drange);
    }
    else if (mrange)
    {
        merged_range = strdup(mrange);
    }
    else if (drange)
    {
        merged_range = strdup(drange);
    }
    return merged_range;
}

/*
  verify that both config files exist.  if so, cache them in RAM so
  that getconfig will not have to re-read the file contents each time.
  returns 0 on success; 1 on failure.

  even if this call fails half way into it, a PINT_config_release
  call should properly de-alloc all consumed memory.
*/
static int cache_config_files(
    struct server_configuration_s *config_s,
    char *global_config_filename,
    char *server_config_filename)
{
    int fd = 0, nread = 0;
    struct stat statbuf;
    char *working_dir = NULL;
    char *my_global_fn = NULL;
    char *my_server_fn = NULL;
    char buf[512] = {0};

    assert(config_s);

    working_dir = getenv("PWD");

    /* pick some filenames if not provided */
    my_global_fn = ((global_config_filename != NULL) ?
                    global_config_filename : "fs.conf");
    my_server_fn = ((server_config_filename != NULL) ?
                    server_config_filename : "server.conf");

  open_global_config:
    memset(&statbuf, 0, sizeof(struct stat));
    if (stat(my_global_fn, &statbuf) == 0)
    {
        if (statbuf.st_size == 0)
        {
            gossip_err("Invalid global config file %s.  This "
                       "file is 0 bytes in length!\n", my_global_fn);
            goto error_exit;
        }
        config_s->fs_config_filename = strdup(my_global_fn);
        config_s->fs_config_buflen = statbuf.st_size + 1;
    }
    else if (errno == ENOENT)
    {
	gossip_err("Failed to find global config file %s.  This "
                   "file does not exist!\n", my_global_fn);
        goto error_exit;
    }
    else
    {
        assert(working_dir);
        snprintf(buf, 512, "%s/%s",working_dir, my_global_fn);
        my_global_fn = buf;
        goto open_global_config;
    }

    if (!config_s->fs_config_filename ||
        (config_s->fs_config_buflen == 0))
    {
        gossip_err("Failed to stat fs config file.  Please make sure that ");
        gossip_err("the file %s\nexists, is not a zero file size, and has\n",
                   config_s->fs_config_filename);
        gossip_err("permissions suitable for opening and reading it.\n");
        goto error_exit;
    }

  open_server_config:
    memset(&statbuf,0,sizeof(struct stat));
    if (stat(my_server_fn, &statbuf) == 0)
    {
        if (statbuf.st_size == 0)
        {
            gossip_err("Invalid server config file %s.  This "
                       "file is 0 bytes in length!\n", my_server_fn);
            goto error_exit;
        }
        config_s->server_config_filename = strdup(my_server_fn);
        config_s->server_config_buflen = statbuf.st_size + 1;
    }
    else if (errno == ENOENT)
    {
	gossip_err("Failed to find server config file %s.  This "
                   "file does not exist!\n", my_server_fn);
        goto error_exit;
    }
    else
    {
        assert(working_dir);
        snprintf(buf, 512, "%s/%s", working_dir, my_server_fn);
        my_server_fn = buf;
        goto open_server_config;
    }

    if (!config_s->server_config_filename ||
        (config_s->server_config_buflen == 0))
    {
        gossip_err("Failed to stat server config file.  (0 file size?)\n");
        goto error_exit;
    }

    if ((fd = open(my_global_fn, O_RDONLY)) == -1)
    {
        gossip_err("Failed to open fs config file %s.\n",
                   my_global_fn);
        goto error_exit;
    }

    config_s->fs_config_buf = (char *) malloc(config_s->fs_config_buflen);
    if (!config_s->fs_config_buf)
    {
        gossip_err("Failed to allocate %d bytes for caching the fs "
                   "config file\n", (int) config_s->fs_config_buflen);
        goto close_fd_fail;
    }

    memset(config_s->fs_config_buf, 0, config_s->fs_config_buflen);
    nread = read(fd, config_s->fs_config_buf,
                 (config_s->fs_config_buflen - 1));
    if (nread != (config_s->fs_config_buflen - 1))
    {
        gossip_err("Failed to read fs config file %s "
                   "(nread is %d | config_buflen is %d)\n",
                   my_global_fn, nread, (int)(config_s->fs_config_buflen - 1));
        goto close_fd_fail;
    }
    close(fd);

    if ((fd = open(my_server_fn,O_RDONLY)) == -1)
    {
        gossip_err("Failed to open fs config file %s.\n", my_server_fn);
        goto error_exit;
    }

    config_s->server_config_buf = (char *)
        malloc(config_s->server_config_buflen);
    if (!config_s->server_config_buf)
    {
        gossip_err("Failed to allocate %d bytes for caching the server "
                   "config file\n", (int) config_s->server_config_buflen);
        goto close_fd_fail;
    }

    memset(config_s->server_config_buf, 0, config_s->server_config_buflen);
    nread = read(fd, config_s->server_config_buf,
                 (config_s->server_config_buflen - 1));
    if (nread != (config_s->server_config_buflen - 1))
    {
        gossip_err("Failed to read server config file %s "
                   "(nread is %d | config_buflen is %d)\n",
                   my_server_fn, nread,
                   (int)(config_s->server_config_buflen - 1));
        goto close_fd_fail;
    }

    close(fd);
    return 0;

  close_fd_fail:
    close(fd);

  error_exit:
    return 1;
}

static char *get_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs,
    int meta_handle_range)
{
    char *ret = (char *)0;
    char *my_alias = (char *)0;
    PINT_llist *cur = NULL;
    struct host_handle_mapping_s *cur_h_mapping = NULL;

    if (config_s && config_s->host_id && fs)
    {
        my_alias = PINT_config_get_host_alias_ptr(
            config_s,config_s->host_id);
        if (my_alias)
        {
            cur = (meta_handle_range ? fs->meta_handle_ranges :
                   fs->data_handle_ranges);
            while(cur)
            {
                cur_h_mapping = PINT_llist_head(cur);
                if (!cur_h_mapping)
                {
                    break;
                }
                assert(cur_h_mapping->alias_mapping);
                assert(cur_h_mapping->alias_mapping->host_alias);
                assert(cur_h_mapping->handle_range);

                if (strcmp(cur_h_mapping->alias_mapping->host_alias,
                           my_alias) == 0)
                {
                    ret = cur_h_mapping->handle_range;
                    break;
                }
                cur = PINT_llist_next(cur);
            }
        }
    }
    return ret;
}

/*
  returns 1 if the specified configuration object is valid
  (i.e. contains values that make sense); 0 otherwise
*/
int PINT_config_is_valid_configuration(
    struct server_configuration_s *config_s)
{
    int ret = 0, fs_count = 0;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;
    
    if (config_s && config_s->bmi_modules && config_s->event_logging &&
        config_s->logfile)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            ret += is_valid_filesystem_configuration(config_s,cur_fs);
            fs_count++;

            cur = PINT_llist_next(cur);
        }
        ret = ((ret == fs_count) ? 1 : 0);
    }
    return ret;
}


/*
  returns 1 if the specified coll_id is valid based on
  the specified server_configuration struct; 0 otherwise
*/
int PINT_config_is_valid_collection_id(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            if (cur_fs->coll_id == fs_id)
            {
                ret = 1;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
  returns pointer to fs config if the config object has information on 
  the specified filesystem; NULL otherwise
*/
struct filesystem_configuration_s* PINT_config_find_fs_name(
    struct server_configuration_s *config_s,
    char *fs_name)
{
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s && fs_name)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            assert(cur_fs->file_system_name);
            if (strcmp(cur_fs->file_system_name,fs_name) == 0)
            {
                return(cur_fs);
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return(NULL);
}

/* PINT_config_find_fs()
 *
 * searches the given server configuration information to find a file
 * system configuration that matches the fs_id
 *
 * returns pointer to file system config struct on success, NULL on failure
 */
struct filesystem_configuration_s* PINT_config_find_fs_id(
    struct server_configuration_s* config_s,
    PVFS_fs_id fs_id)
{
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            if (cur_fs->coll_id == fs_id)
            {
                return(cur_fs);
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return(NULL);
}

PVFS_fs_id PINT_config_get_fs_id_by_fs_name(
    struct server_configuration_s *config_s,
    char *fs_name)
{
    PVFS_fs_id fs_id = 0;
    struct filesystem_configuration_s *fs =
        PINT_config_find_fs_name(config_s, fs_name);
    if (fs)
    {
        fs_id = fs->coll_id;
    }
    return fs_id;
}

/* PINT_config_get_filesystems()
 *
 * returns a PINT_llist of all filesystems registered in the
 * specified configuration object
 *
 * returns pointer to a list of file system config structs on success,
 * NULL on failure
 */
PINT_llist *PINT_config_get_filesystems(
    struct server_configuration_s *config_s)
{
    return (config_s ? config_s->file_systems : NULL);
}

/*
  given a configuration object, weed out all information about other
  filesystems if the fs_id does not match that of the specifed fs_id
*/
int PINT_config_trim_filesystems_except(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id)
{
    int ret = -PVFS_EINVAL;
    PINT_llist *cur = NULL, *new_fs_list = NULL;
    struct filesystem_configuration_s *cur_fs = NULL, *new_fs = NULL;

    if (config_s)
    {
        new_fs_list = PINT_llist_new();
        if (!new_fs_list)
        {
            return -PVFS_ENOMEM;
        }

        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            if (cur_fs->coll_id == fs_id)
            {
                new_fs = (struct filesystem_configuration_s *)malloc(
                    sizeof(struct filesystem_configuration_s));
                assert(new_fs);

                memset(new_fs, 0,
                       sizeof(struct filesystem_configuration_s));

                copy_filesystem(new_fs, cur_fs);
                PINT_llist_add_to_head(new_fs_list, (void *)new_fs);
                break;
            }
            cur = PINT_llist_next(cur);
        }

        PINT_llist_free(config_s->file_systems,free_filesystem);
        config_s->file_systems = new_fs_list;

        if (PINT_llist_count(config_s->file_systems) == 1)
        {
            ret = 0;
        }
    }
    return ret;
}

#ifdef __PVFS2_TROVE_SUPPORT__
static int is_root_handle_in_my_range(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    PINT_llist *extent_list = NULL;
    char *cur_host_id = (char *)0;
    host_handle_mapping_s *cur_h_mapping = NULL;

    if (config && is_populated_filesystem_configuration(fs))
    {
        /*
          check if the root handle is within one of the
          specified meta host's handle ranges for this fs;
          a root handle can't exist in a data handle range!
        */
        cur = fs->meta_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            assert(cur_h_mapping->alias_mapping);
            assert(cur_h_mapping->alias_mapping->host_alias);
            assert(cur_h_mapping->alias_mapping->bmi_address);
            assert(cur_h_mapping->handle_range);

            cur_host_id = cur_h_mapping->alias_mapping->bmi_address;
            if (!cur_host_id)
            {
                gossip_err("Invalid host ID for alias %s.\n",
                           cur_h_mapping->alias_mapping->host_alias);
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
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
  create a storage space based on configuration settings object
  with the particular host settings local to the caller
*/
int PINT_config_pvfs2_mkspace(
    struct server_configuration_s *config)
{
    int ret = 1;
    PVFS_handle root_handle = 0;
    int create_collection_only = 0;
    PINT_llist *cur = NULL;
    char *cur_meta_handle_range, *cur_data_handle_range = NULL;
    filesystem_configuration_s *cur_fs = NULL;

    if (config)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            cur_meta_handle_range = PINT_config_get_meta_handle_range_str(
                config, cur_fs);
            cur_data_handle_range = PINT_config_get_data_handle_range_str(
                config, cur_fs);

            /*
              make sure have either a meta or data handle range (or
              both).  if we have no handle range, the config is
              broken.
            */
            if (!cur_meta_handle_range && !cur_data_handle_range)
            {
                gossip_err("Could not find handle range for host %s\n",
                           config->host_id);
                gossip_err("Please make sure that the host names in "
                           "%s and %s are consistent\n",
                           config->fs_config_filename,
                           config->server_config_filename);
                break;
            }

            /*
              check if root handle is in our handle range for this fs.
              if it is, we're responsible for creating it on disk when
              creating the storage space
            */
            root_handle = (is_root_handle_in_my_range(config, cur_fs) ?
                           cur_fs->root_handle : PVFS_HANDLE_NULL);

            /*
              for the first fs/collection we encounter, create the
              storage space if it doesn't exist.
            */
            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");
            gossip_debug(
                GOSSIP_SERVER_DEBUG, "Creating new PVFS2 %s\n",
                (create_collection_only ? "collection" :
                 "storage space"));

            ret = pvfs2_mkspace(
                config->storage_path, cur_fs->file_system_name,
                cur_fs->coll_id, root_handle, cur_meta_handle_range,
                cur_data_handle_range, create_collection_only, 1);

            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");

            /*
              now that the storage space is created, set the
              create_collection_only variable so that subsequent
              calls to pvfs2_mkspace will not fail when it finds
              that the storage space already exists; this causes
              pvfs2_mkspace to only add the collection to the
              already existing storage space.
            */
            create_collection_only = 1;

            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
  remove a storage space based on configuration settings object
  with the particular host settings local to the caller
*/
int PINT_config_pvfs2_rmspace(
    struct server_configuration_s *config)
{
    int ret = 1;
    int remove_collection_only = 0;
    PINT_llist *cur = NULL;
    filesystem_configuration_s *cur_fs = NULL;

    if (config)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            cur = PINT_llist_next(cur);
            remove_collection_only = (PINT_llist_head(cur) ? 1 : 0);

            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");
            gossip_debug(
                GOSSIP_SERVER_DEBUG,"Removing existing PVFS2 %s\n",
                (remove_collection_only ? "collection" :
                 "storage space"));
            ret = pvfs2_rmspace(config->storage_path,
                                cur_fs->file_system_name,
                                cur_fs->coll_id,
                                remove_collection_only,
                                1);
            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");
        }
    }
    return ret;
}

/*
  returns the metadata sync mode (storage hint) for the specified
  fs_id if valid; TROVE_SYNC otherwise
*/
int PINT_config_get_trove_sync_meta(
    struct server_configuration_s *config,
    PVFS_fs_id fs_id)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config)
    {
        fs_conf = PINT_config_find_fs_id(config, fs_id);
    }
    return (fs_conf ? fs_conf->trove_sync_meta : TROVE_SYNC);
}

/*
  returns the data sync mode (storage hint) for the specified
  fs_id if valid; TROVE_SYNC otherwise
*/
int PINT_config_get_trove_sync_data(
    struct server_configuration_s *config,
    PVFS_fs_id fs_id)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config)
    {
        fs_conf = PINT_config_find_fs_id(config, fs_id);
    }
    return (fs_conf ? fs_conf->trove_sync_data : TROVE_SYNC);
}

#endif

const char * PVFS_server_param_names[PVFS_SERVER_PARAM_COUNT] =
{
    "INVALID",
    "GOSSIP",
    "FSIDCHECK",
    "ROOTCHECK",
    "MODE",
    "EVENTON",
    "EVENTMASK",
    "LOGEVENTS"
};

const char * PVFS_server_mode_names[PVFS_SERVER_MODE_COUNT] =
{
    "NORMAL",
    "ADMIN"
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
