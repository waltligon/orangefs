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
    BUCKETS_CONFIG = 5
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

typedef struct host_bucket_mapping_s
{
    char *host_alias;
    char *bucket_range;
} host_bucket_mapping_s;

typedef struct filesystem_configuration_s
{
    TROVE_coll_id coll_id;
    char *file_system_name;
    struct llist *meta_server_list; /* ptrs are type char* */
    struct llist *data_server_list; /* ptrs are type char* */
    struct llist *bucket_ranges;    /* ptrs are type host_bucket_mapping_s* */
} filesystem_configuration_s;

typedef struct server_configuration_s
{
    char *host_id;
    char *storage_path;
    int  initial_unexpected_requests;
    int  configuration_context;
    struct llist *host_aliases;     /* ptrs are type host_alias_s               */
    struct llist *file_systems;     /* ptrs are type filesystem_configuration_s */
} server_configuration_s;

int PINT_server_config(struct server_configuration_s *config_s,
                       int argc,char **argv);

void PINT_server_config_release(struct server_configuration_s *config_s);

#endif  /* __SERVER_CONFIG_H */
