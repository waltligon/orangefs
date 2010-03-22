#include <string.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pvfs2-hint.h"
#include "gossip.h"

/* Size: 4 for sentinel at the end, 8 + string size for each element,
 * type + length*/
int32_t PINT_hint_calc_size(const PVFS_hint * hint){
#ifdef NO_PVFS_HINT_SUPPORT
    return 0;
#endif

    int count = 4;
    PVFS_hint * act;
    for( act = (PVFS_hint *) hint ; act != NULL ; act = act->next_hint){
        if (hint_types[act->type].transfer_to_server){
            /* length + type + act. string + 8-byte alignment*/
            count += 4 + 4 + roundup8(act->length);
        }
    }
    return (int32_t) count;
}


int PINT_hint_encode(const PVFS_hint * hint, char * buffer, int * out_length, int max_length){
#ifdef NO_PVFS_HINT_SUPPORT
    return 0;
#endif

    PVFS_hint * act;
    char * start_buffer = buffer;
    const int32_t number_hint_types = NUMBER_HINT_TYPES;

    for( act = (PVFS_hint *) hint ; act != NULL ; act = act->next_hint){
        if (hint_types[act->type].transfer_to_server){
            if ( buffer - start_buffer + act->length + 8 <= max_length){
                encode_int32_t(& buffer, & (act->type));
                encode_string(& buffer, &(act->hint));

                continue;
            }
            gossip_err("PINT_hint_encode too many hints !\n");
            return -PVFS_ENOMEM;
        }
    }

    encode_int32_t(& buffer,  & number_hint_types);

    *out_length = buffer - start_buffer;
    /*printf("encode_l %d\n",*out_length);*/
    return 0;
}

/*
 * out_hint must be NULL before running the function !
 */
int PINT_hint_decode(PVFS_hint ** out_hint, const char * buffer, int * out_length){
#ifdef NO_PVFS_HINT_SUPPORT
    * out_hint =  NULL;
    * out_length = 0;
    return 0;
#endif

    PVFS_hint * hint = NULL;
    char * buff = (char *) buffer;
    char * cur_hint;
    const char * start_buffer = buffer;
    int32_t act_hint_type;

    int ret;

    while(1){
         decode_int32_t(& buff,  & act_hint_type);
         if ( act_hint_type == NUMBER_HINT_TYPES ) {
            break;
         }

         assert(act_hint_type < NUMBER_HINT_TYPES);
         decode_string(& buff, &(cur_hint));

         ret = PVFS_add_hint(& hint, act_hint_type, cur_hint);

         if ( ret < 0 ) {
            gossip_err("PINT_hint_decode error ! %d - %s", -ret,
                strerror(-ret));
            return ret;
         }
    }

    * out_length = buff - start_buffer;
    /*printf("decode_l %d\n",*out_length);*/
    * out_hint = hint;
    return 0;
}


void PVFS_free_hint(
    PVFS_hint hint)
{
    PINT_hint * act = hint;
    PINT_hint * old;

    while(act != NULL)
    {
        old = act;
        act = act->next;

        free(old->hint);
        free(old);
    }
}

/*
 * example environment variable
 * PVFS2_HINTS =
 *'REQUEST_ID:blubb+CREATE_SET_DATAFILE_NODES:localhost,localhost'
 */
int PINT_hint_add_environment_hints(PVFS_hint ** out_hint)
{
    char * env;
    char * env_copy;
    char * save_ptr;
    char * aktvar;
    int len;
    if( out_hint == NULL )
    {
        return 1;
    }
    env = getenv("PVFS2_HINTS");
    if( env == NULL )
    {
        return 0;
    }
    len = strlen(env);
    env_copy = (char *) malloc(sizeof(char) * (len+1));
    strncpy(env_copy, env, len+1);

    /* parse hints and do not overwrite already specified hints !*/
    aktvar = strtok_r(env_copy, "+", & save_ptr);
    while( aktvar != NULL )
    {
        enum pvfs2_hint_type hint_type;
        char * rest;

        rest = index(aktvar, ':');
        if (rest == NULL)
        {
            gossip_err("Environment variable PVFS2_HINTS is malformed starting with: %s\n",
                save_ptr);
            free(env_copy);
            return 0;
        }

        *rest = 0;

        hint_type = PVFS_hint_get_type(aktvar);
        if( hint_type == -1)
        {
            gossip_err("Environment variable PVFS2_HINTS is malformed, unknown "
                " hint name: %s\n", aktvar);
        }
        else
        {
            char * old_hint;
            old_hint = PVFS_get_hint(*out_hint, hint_type);

            /* do not overwrite old hints */
            if ( old_hint == NULL )
            {
                PVFS_add_hint( out_hint, hint_type, rest+1 );
            }
        }

        aktvar = strtok_r(NULL, "+", & save_ptr);
    }

    free(env_copy);
    return 1;
}


/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
