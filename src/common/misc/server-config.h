#ifndef __SERVER_CONFIG_H
#define __SERVER_CONFIG_H

/* FIXME: header in header */
#include "llist.h"

enum 
{
    GLOBAL_CONFIG = 1,
    FILESYSTEM_CONFIG = 2,
    DEFAULTS_CONFIG = 3,
    ALIASES_CONFIG = 4,
    HANDLERANGES_CONFIG = 5
};

enum
{
    ENABLE_BMI_TCP = 1,
    ENABLE_BMI_GM = 2
};

typedef struct host_alias_s
{
    char *host_alias;
    char *bmi_address;
} host_alias_s;

typedef struct host_handle_mapping_s
{
    char *host_alias;
    char *handle_range;
} host_handle_mapping_s;

typedef struct filesystem_configuration_s
{
    TROVE_coll_id coll_id;
    char *file_system_name;
    int  root_handle;               /* FIXME: should be 64 bit?             */
    struct llist *meta_server_list; /* ptrs are type char*                  */
    struct llist *data_server_list; /* ptrs are type char*                  */
    struct llist *handle_ranges;    /* ptrs are type host_handle_mapping_s* */
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
    int argc,char **argv);

void PINT_server_config_release(
    struct server_configuration_s *config_s);

char *PINT_server_config_get_host_addr_ptr(
    struct server_configuration_s *config_s,
    char *alias);

char *PINT_server_config_get_host_alias_ptr(
    struct server_configuration_s *config_s,
    char *bmi_address);

char *PINT_server_config_get_handle_range_str(
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

#endif  /* __SERVER_CONFIG_H */
