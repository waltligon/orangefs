#ifndef ORANGEFS_ENVVAR_HINT_H
#define ORANGEFS_ENVVAR_HINT_H

#define ENVVAR_ENUM_COUNT 6
enum orangefs_user_envvar {
    ORANGEFS_DIST_NAME,
    ORANGEFS_DIST_PARAMS,
    ORANGEFS_NUM_DFILES,
    ORANGEFS_LAYOUT,
    ORANGEFS_LAYOUT_SERVER_LIST,
    ORANGEFS_CACHE_FILE
};

struct orangefs_user_envvar_s
{
    const char *envvar_name;
    char * envvar_value;
    enum orangefs_user_envvar envvar_id; 
};

struct orangefs_user_envvars_s {
    struct orangefs_user_envvar_s envvar_array[ENVVAR_ENUM_COUNT];
    unsigned short envvar_cnt;
    unsigned char envvar_present;
};

extern const char * envvar_str_names[ENVVAR_ENUM_COUNT];
extern void envvar_struct_initialize(struct orangefs_user_envvars_s *envvars_ptr);
#endif
