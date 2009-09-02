/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __SERVER_CONFIG_H
#define __SERVER_CONFIG_H

#include "pvfs2-types.h"
#include "src/common/llist/llist.h"
#include "src/common/gossip/gossip.h"

#ifdef __PVFS2_TROVE_SUPPORT__
#include "trove.h"
#endif

enum
{
    CTX_GLOBAL           = (1 << 1),
    CTX_DEFAULTS         = (1 << 2),
    CTX_ALIASES          = (1 << 3),
    CTX_FILESYSTEM       = (1 << 4),
    CTX_METAHANDLERANGES = (1 << 5),
    CTX_DATAHANDLERANGES = (1 << 6),
    CTX_STORAGEHINTS     = (1 << 7),
    CTX_DISTRIBUTION     = (1 << 8),
    CTX_SECURITY         = (1 << 9),
    CTX_EXPORT           = (1 << 10),
    CTX_SERVER_OPTIONS   = (1 << 11),
};

typedef struct phys_server_desc
{
    PVFS_BMI_addr_t addr;
    char *addr_string;
    int server_type;
} phys_server_desc_s;

typedef struct host_alias_s
{
    char *host_alias;
    char *bmi_address;
} host_alias_s;

typedef struct host_handle_mapping_s
{
    struct host_alias_s *alias_mapping;
    char *handle_range;

    /*
      the handle_range above, represented as a
      PVFS_handle_extent_array type.  This is a
      convenient type for sending/receiving over
      the wire on create/mkdir requests.
    */
    PVFS_handle_extent_array handle_extent_array;
} host_handle_mapping_s;

typedef struct filesystem_configuration_s
{
    char *file_system_name;
    PVFS_fs_id coll_id;
    PVFS_handle  root_handle;
    int default_num_dfiles;

    /* ptrs are type host_handle_mapping_s* */
    PINT_llist *meta_handle_ranges;

    /* ptrs are type host_handle_mapping_s* */
    PINT_llist *data_handle_ranges;

    enum PVFS_flowproto_type flowproto; /* default flowprotocol */
    enum PVFS_encoding_type encoding;   /* encoding used for messages */

    /*
      misc storage hints.  may need to be a union later depending on
      which trove storage backends are available
    */
    struct timeval handle_recycle_timeout_sec;
    char *attr_cache_keywords;
    int attr_cache_size;
    int attr_cache_max_num_elems;
    int trove_sync_meta;
    int trove_sync_data;
    int immediate_completion;
    int coalescing_high_watermark;
    int coalescing_low_watermark;
    int file_stuffing;

    char *secret_key;

    int fp_buffer_size;
    int fp_buffers_per_flow;

    int trove_method;

    /* Export flags bitwise OR of flags specified */
    int exp_flags;

    int    ro_count;
    char **ro_hosts;
    int   *ro_netmasks;

    int    root_squash_count;
    char **root_squash_hosts;
    int   *root_squash_netmasks;

    int    root_squash_exceptions_count;
    char **root_squash_exceptions_hosts;
    int   *root_squash_exceptions_netmasks;

    int    all_squash_count;
    char **all_squash_hosts;
    int   *all_squash_netmasks;

    PVFS_uid exp_anon_uid;
    PVFS_gid exp_anon_gid;

    int32_t small_file_size;

    int32_t directio_thread_num;
    int32_t directio_ops_per_queue;
    int32_t directio_timeout;
} filesystem_configuration_s;

typedef struct distribution_param_configuration_s
{
    char* name;
    int64_t value;  /* Temporarily hard code to 64bit type */

} distribution_param_configuration;

/* Config struct to hold overloaded distribution defaults */
typedef struct distribution_configuration_s
{
    char* name;
    PINT_llist* param_list;

} distribution_configuration;

typedef struct server_configuration_s
{
    char *host_id;
    int host_index;
    char *server_alias;             /* the command line server-alias parameter */
    int my_server_options;
    char *storage_path;
    char *fs_config_filename;       /* the fs.conf file name            */
    size_t fs_config_buflen;        /* the fs.conf file length          */
    char *fs_config_buf;            /* the fs.conf file contents        */
    int  initial_unexpected_requests;
    int  server_job_bmi_timeout;    /* job timeout values in seconds    */
    int  server_job_flow_timeout;
    int  client_job_bmi_timeout; 
    int  client_job_flow_timeout;
    int  client_retry_limit;        /* how many times to retry client operations */
    int  client_retry_delay_ms;     /* delay between retries */
    int  perf_update_interval;      /* how quickly (in msecs) to
                                       update perf monitor              */
    int  precreate_batch_size;
    int  precreate_low_threshold;
    char *logfile;                  /* what log file to write to */
    char *logtype;                  /* "file" or "syslog" destination */
    enum gossip_logstamp logstamp_type; /* how to timestamp logs */
    char *event_logging;
    int enable_events;
    char *bmi_modules;              /* BMI modules                      */
    char *flow_modules;             /* Flow modules                     */

    int tcp_buffer_size_receive;    /* Size of TCP receive buffer, is set
                                       later with setsockopt */
    int tcp_buffer_size_send;       /* Size of TCP send buffer */
    int tcp_bind_specific;          /* Flag indicates if we should bind to
                                     * specific server address
                                     */
#ifdef USE_TRUSTED
    int           ports_enabled;    /* Should we enable trusted port connections at all? */
    unsigned long allowed_ports[2]; /* {Min, Max} value of ports from which connections will be allowed */
    int          network_enabled;   /* Should we enable trusted network connections at all? */
    int   allowed_networks_count;   /* Number of trusted networks parsed */
    char  **allowed_networks;       /* BMI addresses of the trusted networks */
    int   *allowed_masks;            /* Netmasks for each of the specified trusted network */
    void  *security;                /* BMI module specific information */
    void  (*security_dtor)(void *); /* Destructor to free BMI module specific information */
#endif
    int  configuration_context;
    PINT_llist *host_aliases;       /* ptrs are type host_alias_s       */
    PINT_llist *file_systems;       /* ptrs are type
                                       filesystem_configuration_s       */
    distribution_configuration default_dist_config;  /* distribution conf */
    int db_cache_size_bytes;        /* cache size to use in berkeley db
                                       if zero, use defaults */
    char * db_cache_type;
    int trove_alt_aio_mode;         /* enables experimental alternative AIO
                                     * implementation for some types of 
                                     * operations 
                                     */
    int trove_max_concurrent_io;    /* allow the number of aio operations to
                                     * be configurable.
                                     */
    int trove_method;
    void *private_data;
} server_configuration_s;

int PINT_parse_config(
    struct server_configuration_s *config_s,
    char *global_config_filename,
    char *server_alias_name);

void PINT_config_release(
    struct server_configuration_s *config_s);

#ifdef USE_TRUSTED

int PINT_config_get_allowed_ports(
    struct server_configuration_s *config,
    int  *enabled,
    unsigned long *allowed_ports);

int PINT_config_get_allowed_networks(
    struct server_configuration_s *config,
    int  *enabled,
    int   *allowed_networks_count,
    char  ***allowed_networks,
    int   **allowed_masks);

#endif

char *PINT_config_get_host_addr_ptr(
    struct server_configuration_s *config_s,
    char *alias);

char *PINT_config_get_host_alias_ptr(
    struct server_configuration_s *config_s,
    char *bmi_address);

char *PINT_config_get_meta_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

int PINT_config_get_meta_handle_extent_array(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id,
    PVFS_handle_extent_array *extent_array);

char *PINT_config_get_data_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

char *PINT_config_get_merged_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

int PINT_config_is_valid_configuration(
    struct server_configuration_s *config_s);

int PINT_config_is_valid_collection_id(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id);

struct filesystem_configuration_s *PINT_config_find_fs_name(
    struct server_configuration_s *config_s,
    char *fs_name);

struct filesystem_configuration_s *PINT_config_find_fs_id(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id);

PVFS_fs_id PINT_config_get_fs_id_by_fs_name(
    struct server_configuration_s *config_s,
    char *fs_name);

PINT_llist *PINT_config_get_filesystems(
    struct server_configuration_s *config_s);

int PINT_config_trim_filesystems_except(
    struct server_configuration_s *config_s,
    PVFS_fs_id fs_id);

int PINT_config_get_fs_key(
    struct server_configuration_s *config,
    PVFS_fs_id fs_id,
    char ** key,
    int * length);

struct server_configuration_s *PINT_get_server_config(void);

#ifdef __PVFS2_TROVE_SUPPORT__
int PINT_config_pvfs2_mkspace(
    struct server_configuration_s *config);
int PINT_config_pvfs2_rmspace(
    struct server_configuration_s *config);
int PINT_config_get_trove_sync_meta(
    struct server_configuration_s *config,
    PVFS_fs_id fs_id);
int PINT_config_get_trove_sync_data(
    struct server_configuration_s *config,
    PVFS_fs_id fs_id);
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif  /* __SERVER_CONFIG_H */
