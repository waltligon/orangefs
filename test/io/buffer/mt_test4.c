/* this test is used to test refill and flush with multiple threads.
 * The idea is that each thread starts from "id" to issue req_cnt
 * requests. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "ncac-interface.h"
#include "internal.h"

#include "trove.h"

#include "trove-init.c"

TROVE_coll_id coll_id;
TROVE_handle file_handle;
TROVE_context_id trove_context;

void do_io(int *myid);
int   loop = 16;
int   req_cnt = 5;

int main()
{

    NCAC_info_t info;

    int ret1, ret2, ret3;
    int ret;
    int extcnt;
    int threadcnt;

    pthread_t thread1, thread2, thread3;



    trove_init( &coll_id, &file_handle, &trove_context );

    info.max_req_num = 1000;
    info.extsize     = 32768;
    info.cachesize   = 1048576;


    threadcnt = 2;
    extcnt = info.cachesize/info.extsize;
    req_cnt = extcnt/threadcnt/2;
    req_cnt = 8;

    cache_init(&info);

    ret1 = 0;
    ret2 = 1*req_cnt;
    ret3 = 2*req_cnt;

    if (pthread_create(&thread1,
                 NULL,
                 (void *) do_io,
                 (void *) &ret1) != 0)
        perror("pthread_create"), exit(1);

    if (pthread_create(&thread2,
                 NULL,
                 (void *) do_io,
                 (void *) &ret2) != 0)
        perror("pthread_create"), exit(1);

#if 0
    if (pthread_create(&thread3,
                 NULL,
                 (void *) do_io,
                 (void *) &ret3) != 0)
        perror("pthread_create"), exit(1);
#endif

    if (pthread_join(thread1, NULL) != 0)
        perror("pthread_join"),exit(1);

    if (pthread_join(thread2, NULL) != 0)
        perror("pthread_join"),exit(1);

#if 0
    if (pthread_join(thread3, NULL) != 0)
        perror("pthread_join"),exit(1);
#endif


    //cache_dump_active_list();
    //cache_dump_inactive_list();

    trove_close_context(coll_id, trove_context);
    trove_finalize();

    return 0;
}

void do_io(int *id)
{
    PVFS_offset offarr[100];
    PVFS_size sizearr[100];

    cache_write_desc_t desc;

    cache_request_t request[100];
    cache_reply_t reply[100];

    int ret;
    int flag;
    int i;

    int comp = 0;
    int start = 0;
    int end = 0;

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
    

    start = *id;
    end = start + req_cnt;

    /* first step: many writes to full the cache */

    for ( i=start; i< end; i++ ) {
        offarr[0] = i*65536;
        ret = cache_write_post(&desc, &request[i], &reply[i], NULL);
        if (ret<0){
            fprintf(stderr, "cache_write_post error\n");
        }else{
            DPRINT("cache_write_post ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
        }
    }

    comp = 0; 
    while ( comp < req_cnt ) {
        for ( i=start; i < end; i++ ) {
            if ( request[i].status == NCAC_COMPLETE ) continue;
            ret = cache_req_test(&request[i], &flag, &reply[i], NULL);
            if (ret<0){
                fprintf(stderr, "cache_req_test error\n");
			    return;
            }else{
                DPRINT("cache_req_test ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
            }

            if ( flag ){
                ret = cache_req_done(&request[i]);
                if (ret<0){
                    fprintf(stderr, "cache_req_done error\n");
			        return;
                }else{
                    DPRINT("cache_req_done ok---\n");
                    comp ++;
                }
            }
        }
    }

    /* bring it to the active list */
    for ( i=start; i< end; i++ ) {
        offarr[0] = i*65536;
        ret = cache_read_post((cache_read_desc_t*)&desc, &request[i], &reply[i], NULL);
        if (ret<0){
            fprintf(stderr, "cache_read_post error\n");
			return;
        }else{
            DPRINT("cache_read_post ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
        }
    }


    comp = 0; 
    while ( comp < req_cnt ) {
	    for ( i=start; i< end; i++ ) {
            if ( request[i].status == NCAC_COMPLETE ) continue;
            ret = cache_req_test(&request[i], &flag, &reply[i], NULL);
            if (ret<0){
            	fprintf(stderr, "cache_req_test error\n");
				return;
    		}else{
        		DPRINT("cache_req_test ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
    		}

    		if ( flag ){
        		ret = cache_req_done(&request[i]);
        		if (ret<0){
            		fprintf(stderr, "cache_req_done error\n");
					return;
        		}else{
            		DPRINT("cache_req_done ok---\n");
					comp ++;
        		}
    		}
		}
	}


#if 1 

    //if ( *id == 0 ) {
    if ( 1 ) {
    	fprintf(stderr, "*******************refill inactive begins\n");

		/* refill inactive needed. */
		for ( i=start; i< end; i++ ) {
    		offarr[0] = (i+loop)*65536;
    		ret = cache_read_post((cache_read_desc_t*)&desc, &request[i], &reply[i], NULL);
    		if (ret<0){
       			fprintf(stderr, "cache_read_post error\n");
				return;
    		}else{
        		DPRINT("cache_read_post ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
    		}
		}

	}


	//if ( *id == 0 ) {
    if ( 1 ) {

    	comp = 0;
    	while ( comp < req_cnt ) {
       		for ( i=start; i< end; i++ ) {
				if ( request[i].status == NCAC_COMPLETE ) continue;

            	ret = cache_req_test(&request[i], &flag, &reply[i], NULL);
            	if (ret<0){
        			fprintf(stderr, "cache_req_test error:%d\n", request[i].status);
					return;
    			}else{
       	   	 		DPRINT("cache_req_test ok: status: %d, cbufcnt=%d\n", request[i].status, reply[i].count);
        		}

    			if ( flag ){
        			ret = cache_req_done(&request[i]);
        			if (ret<0){
            			fprintf(stderr, "cache_req_done error\n");
				   	    return;
        		    }else{
            			fprintf(stderr, "cache_req_done ok---\n");
						comp ++;
        			}
				}
    		}
		}

	}
#endif

    return;
}
