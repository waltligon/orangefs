/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __SERVER_CONFIG_H
#define __SERVER_CONFIG_H

#include "llist.h"
#include "trove.h"

enum 
{
    GLOBAL_CONFIG = 1,
    FILESYSTEM_CONFIG = 2,
    DEFAULTS_CONFIG = 3,
    ALIASES_CONFIG = 4,
    META_HANDLERANGES_CONFIG = 5,
    DATA_HANDLERANGES_CONFIG = 6
};

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
    TROVE_coll_id coll_id;
    char *file_system_name;
    int  root_handle;                 /* FIXME: should be 64 bit?             */
    struct llist *meta_handle_ranges; /* ptrs are type host_handle_mapping_s* */
    struct llist *data_handle_ranges; /* ptrs are type host_handle_mapping_s* */
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
    int  configuration_context;
    struct llist *host_aliases;     /* ptrs are type host_alias_s               */
    struct llist *file_systems;     /* ptrs are type filesystem_configuration_s */
} server_configuration_s;

int PINT_server_config(
    struct server_configuration_s *config_s,
    char *global_config_filename,
    char *server_config_filename);

void PINT_server_config_release(
    struct server_configuration_s *config_s);

char *PINT_server_config_get_host_addr_ptr(
    struct server_configuration_s *config_s,
    char *alias);

char *PINT_server_config_get_host_alias_ptr(
    struct server_configuration_s *config_s,
    char *bmi_address);

char *PINT_server_config_get_meta_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

char *PINT_server_config_get_data_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

char *PINT_server_config_get_merged_handle_range_str(
    struct server_configuration_s *config_s,
    struct filesystem_configuration_s *fs);

int PINT_server_config_is_valid_configuration(
    struct server_configuration_s *config_s);

int PINT_server_config_is_valid_collection_id(
    struct server_configuration_s *config_s,
    TROVE_coll_id coll_id);

int PINT_server_config_has_fs_config_info(
    struct server_configuration_s *config_s,
    char *fs_name);

int PINT_server_config_pvfs2_mkspace(
    struct server_configuration_s *config);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif  /* __SERVER_CONFIG_H */
