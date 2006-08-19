#include <string.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C

#include "pvfs2-hint.h"
#include "gossip.h"
#include "assert.h"
/*#include "PINT-reqproto-encode.h"*/

static int hint_transfer_to_server[NUMBER_HINT_TYPES] = {
    1,
    0,
    0
};

int PVFS_add_hint(
    PVFS_hint ** old_hint, 
    enum pvfs2_hint_type type, 
    char * hint)
{
    PVFS_hint * new_hint = calloc(1, sizeof(PVFS_hint));
    if ( ! new_hint ){
        return -PVFS_ENOMEM;
    }
    
    new_hint->type = type;
    new_hint->length = strlen(hint) + 1;
    new_hint->hint = malloc(new_hint->length);
    strncpy(new_hint->hint, hint, new_hint->length);
    
    new_hint->next_hint = *old_hint;
    *old_hint = new_hint;
    
    return 0;
}

/* Size: 4 for sentinel at the end, 8 + string size for each element,
 * type + length*/
int32_t PINT_hint_calc_size(const PVFS_hint * hint){
    int count = 4;
    PVFS_hint * act;
    for( act = (PVFS_hint *) hint ; act != NULL ; act = act->next_hint){
        if (hint_transfer_to_server[act->type]){
            count += 8 + act->length; 
        }
    }
    return (int32_t) count;
}


int PINT_hint_encode(const PVFS_hint * hint, char * buffer, int * out_length, int max_length){
    PVFS_hint * act;
    char * start_buffer = buffer;
    const int32_t number_hint_types = NUMBER_HINT_TYPES;

    for( act = (PVFS_hint *) hint ; act != NULL ; act = act->next_hint){
        if (hint_transfer_to_server[act->type]){
            if ( buffer - start_buffer + act->length + 8 <= max_length){
                encode_int32_t((int32_t*) & buffer, & (act->type));
                encode_string(& buffer, &(act->hint));
                
                continue;   
            }
            gossip_err("PINT_hint_encode too many hints !\n");
            return -PVFS_ENOMEM;
        }
    }
    
    encode_int32_t((int32_t*)& buffer,  & number_hint_types);
    
    *out_length = buffer - start_buffer;
    /*printf("encode_l %d\n",*out_length);*/    
    return 0;
}

/*
 * out_hint must be NULL before running the function !
 */ 
int PINT_hint_decode(PVFS_hint ** out_hint, const char * buffer, int * out_length){
    PVFS_hint * hint = NULL;
    char * buff = (char *) buffer; 
    char * cur_hint;
    const char * start_buffer = buffer;
    int32_t act_hint_type;
    
    int ret;
    
    while(1){
         decode_int32_t((int32_t*)& buff,  & act_hint_type);
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


char * PVFS_get_hint(
    const PVFS_hint * hint,
    const enum pvfs2_hint_type type
    )
{
    PVFS_hint * act;
    for( act = (PVFS_hint *) hint ; act != NULL ; act = act->next_hint){
        if (act->type == type){
            return act->hint; 
        }
    }
    return NULL;
}

void PVFS_free_hint(
    PVFS_hint ** hint)
{
    PVFS_hint * act;
    PVFS_hint * old;
    
    for( act  = *hint ; act != NULL ; ){
        old = act;
        act = act->next_hint;
        
        free(old->hint);
        free(old);
    }
    
    *hint = NULL;
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
