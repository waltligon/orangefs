#include <stdio.h>

#include "pvfs2.h"
#include "pvfs2-hint.h"

int main(int argc, char ** argv){
	PVFS_hint * hint = NULL;
    int ret;
    const char * hint_p;
    
    int outlength;
    int outlength2;
    char buffer[255];
    memset(buffer, 0,255);
    
    ret = PVFS_add_hint(& hint, REQUEST_ID, "REQUEST ID BLUB");
    if(ret < 0){
        printf("Error add hint: %d, %s \n", -ret, strerror(-ret));
        return 1;
    }
    
    ret = PVFS_add_hint(& hint, CREATE_SET_METAFILE_NODE, "CREATE_SET_METAFILE_NODE");
    if(ret < 0){
        printf("Error add hint: %d, %s \n", -ret, strerror(-ret));
        return 1;
    }
    
    hint_p = PVFS_get_hint(hint, CREATE_SET_METAFILE_NODE);
    printf("Reget hint CREATE_SET_METAFILE_NODE: %s\n", hint_p);
    if ( hint_p == NULL){
        return 1;
    }
    
    hint_p = PVFS_get_hint(hint, REQUEST_ID);
    printf("Reget hint REQUEST_ID: %s\n", hint_p);
    if ( hint_p == NULL){
        return 1;
    }
    
    ret = PINT_hint_encode(hint, buffer, & outlength, 255);
    if(ret < 0){
        printf("Error PINT_hint_encode: %d, %s \n", -ret, strerror(-ret));
        return 1;
    }
    
    printf("Encoded length: %d\n",outlength);
    
    PVFS_free_hint(& hint);
    
    ret = PINT_hint_decode(& hint, buffer, & outlength2);
    if(ret < 0){
        printf("Error PINT_hint_decode: %d, %s \n", -ret, strerror(-ret));
        return 1;
    }
    
    if ( outlength2 != outlength){
        printf("Error PINT_hint_decode length does not match length "
            "of encode: %d encode: %d \n", outlength2, outlength);
        return 1;        
    }

    hint_p = PVFS_get_hint(hint, REQUEST_ID);
    printf("Reget REQUEST_ID hint after decode: %s\n", hint_p);
    if ( hint_p == NULL){
        return 1;
    }
    
    PVFS_free_hint(& hint);   
    
	return 0;
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
