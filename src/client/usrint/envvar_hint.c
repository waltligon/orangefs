#include "envvar_hint.h"
#include <stdlib.h>

const char * envvar_str_names[] = {
    "ORANGEFS_DIST_NAME",
    "ORANGEFS_DIST_PARAMS",
    "ORANGEFS_NUM_DFILES",
    "ORANGEFS_LAYOUT",
    "ORANGEFS_LAYOUT_SERVER_LIST",
    "ORANGEFS_CACHE_FILE",
};

void envvar_struct_initialize(struct orangefs_user_envvars_s *envvars_ptr)
{
    int i = 0;
    envvars_ptr->envvar_cnt = ENVVAR_ENUM_COUNT;
    for(; i < ENVVAR_ENUM_COUNT; i++)
    {
        envvars_ptr->envvar_array[i].envvar_name = envvar_str_names[i];
        envvars_ptr->envvar_array[i].envvar_value = getenv(envvar_str_names[i]);
        envvars_ptr->envvar_array[i].envvar_id = i;
        if(envvars_ptr->envvar_array[i].envvar_value)
        {
            envvars_ptr->envvar_present = 1;
        }
    }
}
