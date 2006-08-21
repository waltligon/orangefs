#ifndef PVFS2HINT_H_
#define PVFS2HINT_H_

#include "pvfs2-types.h"

#define MAX_HINT_LENGTH 1024

enum pvfs2_hint_type{
    REQUEST_ID = 0, 
/*
 * separate multiple tokens with komma ","
 */
    CREATE_SET_DATAFILE_NODES = 1,
    CREATE_SET_METAFILE_NODE = 2,
    NUMBER_HINT_TYPES = 3  
};

typedef struct pvfs2_hint_t{
    int32_t type;
    
    int32_t length;
    char * hint;
    
    struct pvfs2_hint_t * next_hint;
} PVFS_hint;

int PVFS_add_hint(
    PVFS_hint ** old_hint, 
    enum pvfs2_hint_type type, 
    char * hint
    );

char * PVFS_get_hint(
    const PVFS_hint * hint,
    const enum pvfs2_hint_type type);

void PVFS_free_hint(
    PVFS_hint ** hint
    );
    

int32_t PINT_hint_calc_size(const PVFS_hint * hint);

enum pvfs2_hint_type PVFS_hint_get_type(const char * hint_str);
const char *         PVFS_hint_get_str(enum pvfs2_hint_type type);
int                  PVFS_hint_get_count(void);
    
int PINT_hint_encode(const PVFS_hint * hint, char * buffer, int * out_length, int max_length);
int PINT_hint_decode(PVFS_hint ** out_hint, const char * buffer, int * out_length);

#endif /*PVFS2HINT_H_*/

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
