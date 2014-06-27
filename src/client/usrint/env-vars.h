#include "pvfs2-config.h"

#ifdef PVFS_USER_ENV_VARS_ENABLED

#ifndef PVFS_USER_ENV_VAR_H
#define PVFS_USER_ENV_VAR_H

#define ENV_VAR_ENUM_COUNT 6
enum orangefs_user_env_var {
    ORANGEFS_DIST_NAME,
    ORANGEFS_DIST_PARAMS,
    ORANGEFS_NUM_DFILES,
    ORANGEFS_LAYOUT,
    ORANGEFS_LAYOUT_SERVER_LIST,
    ORANGEFS_CACHE_FILE
};

struct orangefs_user_env_var_s
{
    const char *env_var_name;
    const char *env_var_value;
    enum orangefs_user_env_var env_var_id;
    char pad[4];
};

struct orangefs_user_env_vars_s {
    struct orangefs_user_env_var_s env_var_array[ENV_VAR_ENUM_COUNT];
    unsigned char env_var_present;
    unsigned char pad[7];
};

extern struct orangefs_user_env_vars_s env_vars;
extern const char * env_var_str_names[ENV_VAR_ENUM_COUNT];
extern void env_vars_struct_initialize(struct orangefs_user_env_vars_s *env_vars_p);
extern void env_vars_struct_dump(struct orangefs_user_env_vars_s *env_vars_p);
#endif /* PVFS_USER_ENV_VAR_H */
#endif /* PVFS_USER_ENV_VARS_ENABLED */
