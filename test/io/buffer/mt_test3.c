/* this test is used to test refill and flush */

#include <stdio.h>
#include <stdlib.h>
#include "ncac-interface.h"
#include "internal.h"

#include "trove.h"

#include "trove-init.c"

extern void   cache_dump_active_list(void);
extern void   cache_dump_inactive_list(void);

TROVE_coll_id coll_id;
TROVE_handle file_handle;
TROVE_context_id trove_context;

int main(int argc, char *argv[])
{
    PVFS_offset offarr[100];
    PVFS_size sizearr[100];

    cache_write_desc_t desc;

    cache_request_t request[100];
    cache_reply_t reply[100];

    NCAC_info_t info;

    int ret;
    int flag;
    int i;

    int loop;
    int comp = 0;

    trove_init( &coll_id, &file_handle, &trove_context );

    info.max_req_num = 1000;
    info.extsize     = 32768;
    info.cachesize   = 1048576;

    cache_init(&info);

    desc.coll_id        = coll_id;
    desc.handle         = file_handle;
    desc.context_id     = trove_context;

    desc.buffer = 0;
    desc.len    = 0;

    desc.stream_array_count=1;
    desc.stream_offset_array = offarr;
    desc.stream_size_array = sizearr;
    offarr[0] = 1024;
    sizearr[0] = 65536;
    

    loop = 16;

    /* first step: many writes to full the cache */
for ( i=0; i< loop; i++ ) {
    offarr[0] = i*65536;
    ret = cache_write_post(&desc, &request[i], &reply[i], NULL);
    if (ret<0){
        fprintf(stderr, "cache_write_post error\n");
    }else{
        fprintf(stderr, "cache_write_post ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
    }
}

   comp = 0; 
while ( comp < loop ) {
   for ( i=0; i < loop; i++ ) {
        if ( request[i].status == NCAC_COMPLETE ) continue;
        ret = cache_req_test(&request[i], &flag, &reply[i], NULL);
        if (ret<0){
            fprintf(stderr, "cache_req_test error\n");
        }else{
            fprintf(stderr, "cache_req_test ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
        }

        if ( flag ){
            ret = cache_req_done(&request[i]);
            if (ret<0){
                fprintf(stderr, "cache_req_done error\n");
            }else{
                fprintf(stderr, "cache_req_done ok---\n");
                comp ++;
            }
        }
    }
}
    cache_dump_active_list();
    cache_dump_inactive_list();



    /* bring it to the active list */
for ( i=0; i< loop; i++ ) {
    offarr[0] = i*65536;
    ret = cache_read_post((cache_read_desc_t*)&desc, &request[i], &reply[i], NULL);
    if (ret<0){
        fprintf(stderr, "cache_read_post error\n");
    }else{
        fprintf(stderr, "cache_read_post ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
    }
}

    cache_dump_active_list();
    cache_dump_inactive_list();


for ( i=0; i< loop; i++ ) {
    ret = cache_req_test(&request[i], &flag, &reply[i], NULL);
    if (ret<0){
        fprintf(stderr, "cache_req_test error\n");
    }else{
        fprintf(stderr, "cache_req_test ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
    }

    if ( flag ){
        ret = cache_req_done(&request[i]);
        if (ret<0){
            fprintf(stderr, "cache_req_done error\n");
        }else{
            fprintf(stderr, "cache_req_done ok---\n");
        }
    }
}

    cache_dump_active_list();
    cache_dump_inactive_list();



    comp = 0;

/* refill inactive needed. */
for ( i=0; i< loop; i++ ) {
    offarr[0] = (i+loop)*65536;
    ret = cache_read_post((cache_read_desc_t*)&desc, &request[i], &reply[i], NULL);
    if (ret<0){
        fprintf(stderr, "cache_read_post error\n");
    }else{
        fprintf(stderr, "cache_read_post ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
    }
}

    cache_dump_active_list();
    cache_dump_inactive_list();


#if 1 
    while ( comp < loop ) {
        for ( i=0; i< loop; i++ ) {
			if ( request[i].status == NCAC_COMPLETE ) continue;

            ret = cache_req_test(&request[i], &flag, &reply[i], NULL);
            if (ret<0){
        		fprintf(stderr, "cache_req_test error\n");
				return -1;
    		}else{
       	   	 	fprintf(stderr, "cache_req_test ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
        	}

    		if ( flag ){
        		ret = cache_req_done(&request[i]);
        		if (ret<0){
            		fprintf(stderr, "cache_req_done error\n");
				    return -1;
        		}else{
            		fprintf(stderr, "cache_req_done ok---\n");
					comp ++;
        		}
			}
    	}
	}


    cache_dump_active_list();
    cache_dump_inactive_list();

#endif

    trove_close_context(coll_id, trove_context);
    trove_finalize();

    return 0;
}
