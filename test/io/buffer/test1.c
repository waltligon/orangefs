#include <stdio.h>
#include <stdlib.h>
#include "ncac-interface.h"
#include "internal.h"

#include "trove.h"

#include "trove-init.c"

int main(int argc, char * argv[])
{
    PVFS_offset offarr[100];
    PVFS_size sizearr[100];

    cache_read_desc_t desc;
    cache_request_t request[100];
    cache_reply_t reply[100];
    NCAC_info_t info;

    int ret;

    int flag;

    TROVE_coll_id coll_id;
    TROVE_handle file_handle;
    TROVE_context_id trove_context;

    trove_init( &coll_id, &file_handle, &trove_context );

    info.max_req_num = 1000;
    info.extsize     = 32768;
    info.cachesize   = 1048576;

    cache_init(&info);

    desc.coll_id 	= coll_id;
    desc.handle 	= file_handle;
    desc.context_id 	= trove_context;

    desc.buffer = 0;
    desc.len    = 0;

    desc.stream_array_count=1;
    desc.stream_offset_array = offarr;
    desc.stream_size_array = sizearr;
    offarr[0] = 1024;
    sizearr[0] = 32768;
    
    ret = cache_read_post(&desc, &request[0], &reply[0], NULL);
    if (ret<0){
        fprintf(stderr, "cache_read_post error\n");
    }else{
        fprintf(stderr, "cache_read_post ok: status: %d, cbufcnt=%d\n", request[0].status, reply[0].count);
    }


#if 1
    while ( 1 ){
        ret = cache_req_test(&request[0], &flag, &reply[0], NULL);
        if (ret<0){
            fprintf(stderr, "cache_req_test error\n");
        }else{
            if (flag){
                fprintf(stderr, "cache_req_test ok: flag=%d, status: %d, cbufcnt=%d\n", flag, request[0].status, reply[0].count);
		break;		
            }
        }
    }

    if (flag){
        ret = cache_req_done(&request[0]);
        if (ret<0){
            fprintf(stderr, "cache_req_done error\n");
        }else
            fprintf(stderr, "cache_req_done ok---\n");
    }
#endif

    trove_close_context(coll_id, trove_context);
    trove_finalize(TROVE_METHOD_DBPF);

    return 0;
}
