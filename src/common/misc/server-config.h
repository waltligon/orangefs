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
    GLOBAL_CONFIG = 1,
    FILESYSTEM_CONFIG = 2,
    DEFAULTS_CONFIG = 3,
    ALIASES_CONFIG = 4,
    META_HANDLERANGES_CONFIG = 5,
    DATA_HANDLERANGES_CONFIG = 6,
    STORAGEHINTS_CONFIG = 7
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

} filesystem_configuration_s;

typedef struct server_configuration_s
{
    char *host_id;
    char *storage_path;
    char *fs_config_filename;       /* the fs.conf file name            */
    ssize_t fs_config_buflen;       /* the fs.conf file length          */
    char *fs_config_buf;            /* the fs.conf file contents        */
    char *server_config_filename;   /* the server.conf file name        */
    ssize_t server_config_buflen;   /* the server.conf file length      */
    char *server_config_buf;        /* the server.conf file contents    */
    int  initial_unexpected_requests;
    int  perf_update_interval;      /* how quickly (in msecs) to
                                       update perf monitor              */
    char *logfile;
    enum gossip_logstamp logstamp_type; /* how to timestamp logs */
    char *event_logging;
    char *bmi_modules;              /* BMI modules                      */
    char *flow_modules;             /* Flow modules                     */
    int  configuration_context;
    PINT_llist *host_aliases;       /* ptrs are type host_alias_s       */
    PINT_llist *file_systems;       /* ptrs are type
                                       filesystem_configuration_s       */
} server_configuration_s;

int PINT_parse_config(
    struct server_configuration_s *config_s,
    char *global_config_filename,
    char *server_config_filename);

void PINT_config_release(
    struct server_configuration_s *config_s);

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
