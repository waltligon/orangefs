#include "pvfs2-config.h"

#if PVFS_USER_ENV_VARS_ENABLED

#include <stdlib.h>
#include <stdio.h>
#include "env-vars.h"

struct orangefs_user_env_vars_s env_vars;

const char * env_var_str_names[] = {
    "ORANGEFS_DIST_NAME",
    "ORANGEFS_DIST_PARAMS",
    "ORANGEFS_NUM_DFILES",
    "ORANGEFS_LAYOUT",
    "ORANGEFS_LAYOUT_SERVER_LIST",
    "ORANGEFS_CACHE_FILE",
    "ORANGEFS_STRIP_SIZE_AS_BLKSIZE"
};

void env_vars_struct_initialize(struct orangefs_user_env_vars_s *env_vars_p)
{
    int i = 0;
    for(; i < ENV_VAR_ENUM_COUNT; i++)
    {
        env_vars_p->env_var_array[i].env_var_name = env_var_str_names[i];
        env_vars_p->env_var_array[i].env_var_value =
                getenv(env_var_str_names[i]);
        env_vars_p->env_var_array[i].env_var_id = i;
    }
}

void env_vars_struct_dump(struct orangefs_user_env_vars_s *env_vars_p)
{
    int i = 0;
    for(; i < ENV_VAR_ENUM_COUNT; i++)
    {
        printf("%s=%s\n",
               env_vars_p->env_var_array[i].env_var_name,
               env_vars_p->env_var_array[i].env_var_value);
    }
}

#endif /* PVFS_USER_ENV_VARS_ENABLED */
