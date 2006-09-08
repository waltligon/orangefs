/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include <dirent.h>
#include <sys/types.h>

#include "gossip.h"
#include "trove.h"
#include "trove-internal.h"
#include "trove-ledger.h"
#include "trove-handle-mgmt.h"
#include "dbpf.h"
#include "dbpf-thread.h"
#include "dbpf-bstream.h"
#include "dbpf-op-queue.h"
#include "dbpf-sync.h"
#include "pvfs2-util.h"

static pthread_t dbpf_thread[OP_QUEUE_LAST];
static char * threadNames[OP_QUEUE_LAST] = {
    "Meta read",
    "Meta write",
    "I/O",
    "Background file removal"
};

static int dbpf_threads_running = 0;

int dbpf_thread_initialize(void)
{
    int ret = 0, i;
    int * threadType[OP_QUEUE_LAST];
     
    if (dbpf_threads_running)
        return 0;

    ret = -1;
    
    pthread_cond_init(&dbpf_op_completed_cond, NULL);
    
    dbpf_threads_running = 1;
    for(i=0; i < OP_QUEUE_LAST ; i++)
    {
        pthread_cond_init(&dbpf_op_incoming_cond[i], NULL);
        dbpf_op_queue_init(& dbpf_op_queue[i]);
        gen_posix_mutex_init( & dbpf_op_queue_mutex[i]);
        
        if ( i == OP_QUEUE_BACKGROUND_FILE_REMOVAL )
        {
            ret = pthread_create(& dbpf_thread[i], NULL,
                             dbpf_background_file_removal_thread_function, 
                              NULL);
        }
        else    
        {
            threadType[i] = (int*) malloc(sizeof(int));
            *threadType[i] = i;
            
            ret = pthread_create(& dbpf_thread[i], NULL,
                             dbpf_thread_function, threadType[i]);
        }
        if ( ret != 0)
        {
            dbpf_threads_running = 0;
            gossip_lerr("dbpf_thread_initialize: failed (1)\n");
            exit(1);
        }
    }
    
    
#ifndef __PVFS2_USE_AIO__
    dbpf_bstream_threaded_initalize();
#endif        

    gossip_debug(GOSSIP_TROVE_DEBUG,
        "dbpf_thread_initialize: initialized\n");
    return ret;
}

int dbpf_thread_finalize(void)
{
    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_thread_finalize: start"
        " finalization\n");
    int ret = 0,i;
    if (! dbpf_threads_running)
        return 0;

    dbpf_threads_running = 0;
    
    /*send signals to wakeup threads:*/
    for(i=0; i < OP_QUEUE_LAST ; i++)
    {
        gen_mutex_lock(& dbpf_op_queue_mutex[i]);
        pthread_cond_signal(& dbpf_op_incoming_cond[i]);
        gen_mutex_unlock(& dbpf_op_queue_mutex[i]);
    }
    
    for(i=0; i < OP_QUEUE_LAST ; i++)
    {
        ret = pthread_join(dbpf_thread[i], NULL);
    }

#ifndef __PVFS2_USE_AIO__
    dbpf_bstream_threaded_finalize();
#endif

    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_thread_finalize: finalized\n");
    return ret;
}

void *dbpf_background_file_removal_thread_function(void *ptr)
{
    gen_mutex_t * work_queue_mutex;
    pthread_cond_t * queue_cond;
    int ret;
    char dirname[PATH_MAX];
    char filename[PATH_MAX];
    const enum operation_queue_type queue_type = 
        OP_QUEUE_BACKGROUND_FILE_REMOVAL;
    
    DIR * del_dir;
    struct dirent * dir_entry;
    
    work_queue_mutex = & dbpf_op_queue_mutex[queue_type];
    queue_cond = & dbpf_op_incoming_cond[queue_type];

    /*
     * Wait until my_storage_p is filled up. 
     */
    while(my_storage_p == NULL)
    {
        sleep(1);
    }
     
    /*
     * Make sure deletion directoy exists: (for now, silent upgrade)
     */
    DBPF_GET_SHADOW_REMOVE_BSTREAM_DIRNAME(dirname, PATH_MAX, 
        my_storage_p->name);
    
    ret = DBPF_MKDIR(dirname, 0755);

    /*
     * Additionally check on startup if there are undeleted files and delete 
     * them 
     */
    
    del_dir = opendir(dirname); 
    
    while(dbpf_threads_running)
    {
        gen_mutex_lock(work_queue_mutex);
        /*
         * Check if there are files to delete:
         */
        rewinddir(del_dir);
        do
        {
            dir_entry = readdir(del_dir);
        }while(dir_entry != NULL && dir_entry->d_name[0] == '.');
        
        if( dir_entry == NULL)
        {
            ret = pthread_cond_wait(queue_cond, work_queue_mutex);
            gen_mutex_unlock(work_queue_mutex);            
            continue;
        }        
        gen_mutex_unlock(work_queue_mutex);

        snprintf(filename, PATH_MAX-1, "%s/%s", dirname, dir_entry->d_name);
                
        gossip_debug(GOSSIP_TROVE_DEBUG, 
            "dbpf_background_file_removal_thread_function remove file %s\n"
            , filename);

        ret = DBPF_UNLINK(filename);
        if ( ret < 0 )
        {
            gossip_err("Error while deleting file %s: %s\n", filename, 
                strerror(errno));
            break;
        }
    }
    closedir(del_dir);
    return NULL;
}

void *dbpf_thread_function(void *ptr)
{
    dbpf_op_queue_t * work_queue;
    gen_mutex_t * work_queue_mutex;
    pthread_cond_t * queue_cond;
    char * thread_type;
    enum operation_queue_type queue_type;
    
    queue_type = *((int*) ptr);
    free ((int*)ptr);
    
    thread_type = threadNames[queue_type];

    work_queue = & dbpf_op_queue[queue_type];
    work_queue_mutex = & dbpf_op_queue_mutex[queue_type];
    queue_cond = & dbpf_op_incoming_cond[queue_type];

    int op_queued_empty = 0, ret = 0;
    dbpf_queued_op_t *cur_op = NULL;
    
#ifndef __PVFS2_USE_AIO__
    /*
     * dbpf-bstream-threaded maintains its own thread managemenent. 
     */
    if (queue_type == OP_QUEUE_IO){
        return NULL; 
    }
#endif
    
    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_meta_thread_function \"%s\""
        " started\n",thread_type);

    while(1)
    {
        gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_meta_thread_function \"%s\""
            " ITERATING\n",thread_type);
        /* check if we any have ops to service in our work queue */
        gen_mutex_lock(work_queue_mutex);
        if (! dbpf_threads_running)
        {
            gen_mutex_unlock(work_queue_mutex);
            break;
        }        
        op_queued_empty = dbpf_op_queue_empty(work_queue);

        if (op_queued_empty)
        {
            gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_meta_thread_function \"%s\"" 
                            "QUEUE EMPTY\n",thread_type);
            ret = pthread_cond_wait(queue_cond,
                                         work_queue_mutex);
            gen_mutex_unlock(work_queue_mutex);
            continue;
        }
        gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_meta_thread_function \"%s\""
            " fetching new element\n",thread_type);
        cur_op = dbpf_op_pop_front_nolock(work_queue);
        dbpf_sync_coalesce_dequeue(cur_op);

        assert(cur_op);
        dbpf_op_change_status(cur_op, OP_IN_SERVICE);
        gen_mutex_unlock(work_queue_mutex);

        /* service the current operation now */
        gossip_debug(GOSSIP_TROVE_OP_DEBUG,"***** STARTING TROVE "
                     "SERVICE ROUTINE (%s) *****\n",
                     dbpf_op_type_to_str(cur_op->op.type));

        ret = cur_op->op.svc_fn(&(cur_op->op));

        gossip_debug(GOSSIP_TROVE_OP_DEBUG,"***** FINISHED TROVE "
                     "SERVICE ROUTINE (%s) *****\n",
                     dbpf_op_type_to_str(cur_op->op.type));
                               
        if ( DBPF_OP_MODIFYING_META_OP(cur_op->op.type) )
        {
            if (ret == IMMEDIATE_COMPLETION )
            {
                ret = dbpf_sync_coalesce(cur_op, 1, 0);
            }
            else if( ret < 0 )
            {
                /*
                 * We cannot enqueue error responses, instead we 
                 * sync and return !
                 */
                ret = dbpf_sync_coalesce(cur_op, 0, ret);
            }
            else
            {
                assert(0);
            }
            if(ret < 0)
            {
                    gossip_err("Error: dbpf_sync_coalesce ret < 0 !\n");
                     /* not sure how to recover from failure here */
            }
        }
        else if (ret == IMMEDIATE_COMPLETION || ret < 0)
        {
            dbpf_move_op_to_completion_queue(
                cur_op, ((ret == 1) ? 0 : ret), OP_COMPLETED);
        }else if(ret == TEST_FOR_COMPLETION){
            /* readd it to the queue */
            int yield; 
            /* lazy check to check if we are the only op, if yes 
               let other threads go first */            
            yield = dbpf_op_queue_empty(& dbpf_op_queue[OP_QUEUE_IO]);
#ifndef __PVFS2_TROVE_AIO_THREADED__
            /*
              check if trove is telling us to NOT mark this as
              completed, and also to NOT re-add it to the service
              queue.  this can happen if trove is throttling I/O
              internally and will handle re-starting the operation
              without our help.
            */
            if (dbpf_op_get_status(cur_op) == OP_INTERNALLY_DELAYED)
            {
                if (yield)
                {
                    sched_yield();
                }
                continue;
            }
#endif            
            assert(cur_op->op.state != OP_COMPLETED);
            
            dbpf_queued_op_queue(cur_op, & dbpf_op_queue[OP_QUEUE_IO]);
            if (yield)
            {
                sched_yield();
            }
        }else{
            assert(0);
        }
    }

    gossip_debug(GOSSIP_TROVE_DEBUG, "dbpf_meta_thread_function \"%s\""
        " ending\n",thread_type);
    return ptr;
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
