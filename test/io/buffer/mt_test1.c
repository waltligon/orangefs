/* In this test, we use multiple threads to work on a 
 * a same file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "ncac-interface.h"
#include "internal.h"

#include "trove.h"

#include "trove-init.c"
#include "pvfs2-internal.h"

TROVE_coll_id coll_id;
TROVE_handle file_handle;
TROVE_context_id trove_context;

void do_io_read(int *myid);

int main(int argc, char * argv[])
{
    NCAC_info_t info;

    int ret1, ret2, ret3;

    pthread_t thread1, thread2, thread3;



    trove_init( &coll_id, &file_handle, &trove_context );

    info.max_req_num = 1000;
    info.extsize     = 32768;
    info.cachesize   = 1048576;


    cache_init(&info);
   
    if (pthread_create(&thread1,
                 NULL,
                 (void *) do_io_read,
                 (void *) &ret1) != 0)
        perror("pthread_create"), exit(1);

    if (pthread_create(&thread2,
                 NULL,
                 (void *) do_io_read,
                 (void *) &ret2) != 0)
        perror("pthread_create"), exit(1);

    if (pthread_create(&thread3,
                 NULL,
                 (void *) do_io_read,
                 (void *) &ret3) != 0)
        perror("pthread_create"), exit(1);

    if (pthread_join(thread1, NULL) != 0)
        perror("pthread_join"),exit(1);

    if (pthread_join(thread2, NULL) != 0)
        perror("pthread_join"),exit(1);

    if (pthread_join(thread3, NULL) != 0)
        perror("pthread_join"),exit(1);

    trove_close_context(coll_id, trove_context);
    trove_finalize();

    //cache_dump_active_list();
    //cache_dump_inactive_list();

    return 0;
}

void do_io_read(int *result)
{
    PVFS_offset offarr[100];
    PVFS_size sizearr[100];

    cache_read_desc_t desc;
    cache_request_t request[100];
    cache_reply_t reply[100];

    int flag;
    int ret;
    int i;
    int loop=10;

    int ncnt;

    desc.coll_id 	= coll_id;
    desc.handle 	= file_handle;
    desc.context_id 	= trove_context;

    desc.buffer = 0;
    desc.len    = 0;

    desc.stream_array_count=1;
    desc.stream_offset_array = offarr;
    desc.stream_size_array = sizearr;

    for ( i = 0; i< loop; i++ ) {
        offarr[0] = i*65536+1024;
        sizearr[0] = 32768;
    
        ret = cache_read_post(&desc, &request[0], &reply[0], NULL);
        if (ret<0){
            fprintf(stderr, "cache_read_post error\n");
        }else{
            fprintf(stderr, "cache_read_post ok: status: %d, cbufcnt=%d\n", request[0].status, reply[0].count);
        }


        while ( 1 ){
            ret = cache_req_test(&request[0], &flag, &reply[0], NULL);
            if (ret<0){
                fprintf(stderr, "cache_req_test error\n");
            }else{
                if (flag){
                    fprintf(stderr, "cache_req_test ok: flag=%d, status: %d, cbufcnt=%d\n", flag, request[0].status, reply[0].count);
					for ( ncnt=0; ncnt < reply[0].count; ncnt ++ ) {
						fprintf(stderr, "[%d] %p len:%lld\n", ncnt,
									reply[0].cbuf_offset_array[ncnt], 
									lld(reply[0].cbuf_size_array[ncnt]));
					}	
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
    }

    *result = ret;

}
