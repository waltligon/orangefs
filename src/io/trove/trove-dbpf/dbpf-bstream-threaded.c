/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
 
/*
 * Needed for O_DIRECT disk access
 */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <assert.h>
#include <errno.h>

#ifndef __PVFS2_USE_AIO__

#include "gossip.h"
#include "pvfs2-debug.h"
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-op-queue.h"
#include "dbpf-attr-cache.h"
#include "pint-event.h"
#include "pthread.h"
#include "dbpf-bstream.h"

#define PREAD pread
#define PWRITE pwrite

/*
 * Size for doing only one read operation instead of 
 * two for the ends of O_direct
 */
#define DIRECT_IO_SIZE (4*1024)
#define DIRECT_IO_SIZE_LIMIT_MULTIPLIER 4
#define MEM_PAGESIZE 4096
/* MEM_PAGESIZE = getpagesize(); */

/*
 * for testing of threaded impl:
 * 
 * undef O_DIRECT
 * #define O_DIRECT 0
 */

/*
 * Define this value to get O_DIRECT TO WORK PROPERLY,
 * THIS IS DEFINITELY A HACK FOR NOW, for evaluation purpose only ! 
 */
/*#define THREADS_SCHEDULE_ONLY_ONE_HANDLE 1*/

enum active_queue_e
{
    ACTIVE_QUEUE_READ,
    ACTIVE_QUEUE_WRITE,
    ACTIVE_QUEUE_LAST
};

/*during processing in the active queue we need extra information*/
typedef struct
{
    TROVE_size size;
    TROVE_offset stream_offset;

    char *mem_offset;
    dbpf_queued_op_t *parentRequest;
    /*
     * Number of requests to process, in case of real I/O ops read and write
     * these ops are transformed into their small contiguous I/O requests.
     * The remainingNumber of slices shows how much contiguous I/O requests
     * have to be done until this request is finished. 
     */    
    char *remainingNumberOfSlices;
} active_io_processing_slice_t;

typedef struct active_io_processing_t
{
    gen_mutex_t mutex;
    int ref_count;

    int filehandle;

    active_io_processing_slice_t *request[ACTIVE_QUEUE_LAST];
    char *requestNumberOfSlices[ACTIVE_QUEUE_LAST];
    int totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_LAST];
    int positionOfProcessing[ACTIVE_QUEUE_LAST];

    TROVE_size total_write_size;
    TROVE_size latest_offset_written;
    
    int openflags;
    int return_code;
} active_io_processing_t;

typedef struct io_handle_queue_t
{
    struct io_handle_queue_t *next;

    PVFS_handle handle;
    PVFS_fs_id fs_id;

    /*during processing in the active queue we need extra information */
    active_io_processing_t *active_io;
    
    dbpf_op_queue_s requests[IO_QUEUE_LAST];
    /*
     * Number of requests to process, in case of real I/O ops read and write
     * these ops are transformed into their small contiguous I/O requests
     * in the active_io pointer. 
     */
    int request_to_process_count[IO_QUEUE_LAST];
} io_handle_queue_t;

struct handle_queue_t
{
    io_handle_queue_t *first;
    io_handle_queue_t *last;
    int count;
};

static int threads_running = 0;
static int initialised = 0;
static int thread_count = 0;

static io_handle_queue_t *active_file_io;
static pthread_mutex_t active_file_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *io_threads;

static struct handle_queue_t handle_queue;

extern gen_mutex_t dbpf_attr_cache_mutex;

static void *bstream_threaded_thread_io_function(
    void *pthread_number);
static int checkAndUpdateCacheSize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size write_size);    

int dbpf_bstream_threaded_set_thread_count(
    int count)
{
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "DBPF dbpf_bstream_threaded_set_thread_count:%d \n", count);

    if (thread_count == count)
        return 1;

    if (!threads_running)
    {
        thread_count = count;
#ifdef THREADS_SCHEDULE_ONLY_ONE_HANDLE
        if (count > 1){
            gossip_err("In that mode currently only 1 thread is allowed\n");
            exit(1);
        }
#endif
        
        /*initialise threads:*/
        threads_running = 1;

        io_threads = (pthread_t *) calloc(thread_count, sizeof(pthread_t));
        int t, ret;
        for (t = 0; t < thread_count; t++)
        {
            int *p_thread_number = (int *) malloc(sizeof(int));

            *p_thread_number = t;
            ret =
                pthread_create(&io_threads[t], NULL, 
                    bstream_threaded_thread_io_function, p_thread_number);
            if (ret != 0)
            {
                gossip_lerr("Could not create I/O thread!\n");
                assert(0);
            }
        }
    }
    return 1;
}

static int open_fd(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    int openflags 
    )
{
    char filename[PATH_MAX] = { 0 };
    int ifd = 0;

    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
                              my_storage_p->name, coll_id, llu(handle));
    
    gossip_debug(GOSSIP_PERFORMANCE_DEBUG, "DBPF open filehandle start\n");
    ifd = DBPF_OPEN(filename, O_RDWR | O_CREAT | openflags, 
        S_IRUSR | S_IWUSR);
    gossip_debug(GOSSIP_PERFORMANCE_DEBUG, "DBPF open filehandle %d with ODIRECT %d\n",
                     ifd, openflags && O_DIRECT);        

    return ((ifd < 0) ? -trove_errno_to_trove_error(errno) : ifd);
}


static void debugPrint(
    char *prefix,
    io_handle_queue_t * elems,
    int thread_number)
{
    gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                 "%s thread %d: queue:%p ops r:%d w:%d resize:%d flush:%d\n",
                 prefix,
                 thread_number,
                 elems,
                 elems->requests[IO_QUEUE_READ].elems,
                 elems->requests[IO_QUEUE_WRITE].elems,
                 elems->requests[IO_QUEUE_RESIZE].elems,
                 elems->requests[IO_QUEUE_FLUSH].elems);
}

static int noProcessElementAvailable(
    io_handle_queue_t * elem)
{
    return (!(
             elem->active_io->totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_READ]
             || 
             elem->active_io->totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_WRITE]
             || elem->request_to_process_count[IO_QUEUE_FLUSH]
             || elem->request_to_process_count[IO_QUEUE_RESIZE]));
}


static void transformIOrequestsIntoArray(
    io_handle_queue_t * elem,
    enum active_queue_e active_queue,
    enum IO_queue_type queue_type)
{
    int mem_num;
    int stream_num;
    int streamCount;
    int memCount;

    int current_request_slot = 0;

    /*Check size */
    TROVE_size total_stream_size;
    TROVE_size total_mem_size;
    TROVE_size cur_mem_size;
    TROVE_size cur_stream_size;
    TROVE_size cur_size;
    char *mem_offset;

    TROVE_size cur_stream_offset;
    struct dbpf_bstream_rw_list_op *IOreq;

    int count = elem->requests[queue_type].elems;
    if (count == 0)
        return;

    elem->active_io->request[active_queue] = (active_io_processing_slice_t *)
        calloc(elem->active_io->totalNumberOfUnprocessedSlices[active_queue], 
        sizeof(active_io_processing_slice_t));

    elem->active_io->requestNumberOfSlices[active_queue] = (char *)
        calloc(count, sizeof(char));

    dbpf_queued_op_t *op;
    struct qlist_head *tmp_link;
    dbpf_op_queue_s *queue = &elem->requests[queue_type];

    int current_trove_request = 0;
    qlist_for_each(tmp_link, &queue->list)
    {
        op = qlist_entry(tmp_link, dbpf_queued_op_t, link);
        IOreq = &op->op.u.b_rw_list;

        mem_num = 0;
        stream_num = 0;

        streamCount = IOreq->stream_count;
        memCount = IOreq->mem_count;
        cur_mem_size = IOreq->mem_size_array[0];
        cur_stream_size = IOreq->stream_size_array[0];
        cur_size = (cur_mem_size < cur_stream_size) ?
            cur_mem_size : cur_stream_size;
        mem_offset = IOreq->mem_offset_array[0];

        cur_stream_offset = IOreq->stream_offset_array[0];
        total_stream_size = cur_stream_size;
        total_mem_size = cur_mem_size;

        elem->active_io->
            requestNumberOfSlices[active_queue][current_trove_request] =
            (memCount > streamCount ? memCount : streamCount);

        while (1)
        {
            active_io_processing_slice_t *currentSlice =
                &elem->active_io->request[active_queue][current_request_slot];

            currentSlice->remainingNumberOfSlices =
                &elem->active_io->
                requestNumberOfSlices[active_queue][current_trove_request];
            currentSlice->parentRequest = op;
            currentSlice->mem_offset = mem_offset;
            currentSlice->size = cur_size;
            currentSlice->stream_offset = cur_stream_offset;

            current_request_slot++;
            if (cur_mem_size == cur_stream_size)
            {   /*take next mem_area and stream*/
                mem_num++;
                stream_num++;
                if (stream_num == streamCount || mem_num == memCount)
                {   
                /*we finished decoding...*/
                    break;
                }

                cur_stream_offset = IOreq->stream_offset_array[stream_num];
                mem_offset = IOreq->mem_offset_array[mem_num];
                cur_mem_size = IOreq->mem_size_array[mem_num];
                cur_stream_size = IOreq->stream_size_array[stream_num];
            }
            else if (cur_stream_size < cur_mem_size)
            {   /*take next stream update memory position*/
                stream_num++;
                if (stream_num == streamCount)  
                {/*we finished decoding... ERROR !!!*/
                    break;
                }
                cur_stream_offset = IOreq->stream_offset_array[stream_num];
                cur_mem_size -= cur_size;
                mem_offset += cur_size;
                cur_stream_size = IOreq->stream_size_array[stream_num];

                total_stream_size += cur_stream_size;
            }
            else
            {   /*cur_stream_size > cur_mem_size
                  keep stream position*/
                mem_num++;
                if (mem_num == memCount){/*we finished decoding... ERROR !!!*/
                    break;
                }

                mem_offset = IOreq->mem_offset_array[mem_num];
                cur_mem_size = IOreq->mem_size_array[mem_num];
                cur_stream_size -= cur_size;

                total_mem_size += cur_mem_size;
            }
            cur_size = (cur_mem_size < cur_stream_size) ?
                cur_mem_size : cur_stream_size;
        }
        if (total_mem_size != total_stream_size)
        {
            gossip_err
                ("Error I/O stream has a different total memory size than a stream size\n");
        }

        current_trove_request++;
    }
    assert( elem->active_io->totalNumberOfUnprocessedSlices[active_queue] ==
        current_request_slot );
}


static int compareSlices(
    const void *ps1,
    const void *ps2)
{
    const active_io_processing_slice_t *s1 =
        (active_io_processing_slice_t *) ps1;
    const active_io_processing_slice_t *s2 =
        (active_io_processing_slice_t *) ps2;

    if (s1->stream_offset < s2->stream_offset)
        return -1;
    if (s1->stream_offset > s2->stream_offset)
        return 1;
    return 0;
}

/*
 * Do scheduling with current elements RW operations, run quicksort to sort stream_offsets
 * ascending.
 */
static void scheduleHandleOps(
    io_handle_queue_t * elem,
    enum active_queue_e queue_type)
{
    /*
     * Open file in O_DIRECT mode (for now), use later better scheduling
     * algorithms to decide (triggered by request scheduler ?)...
     */
#ifdef THREADS_SCHEDULE_ONLY_ONE_HANDLE
     elem->active_io->openflags = O_DIRECT;
#endif     
    
    if (elem->active_io->totalNumberOfUnprocessedSlices[queue_type] <= 1)
        return;

    qsort((void *) elem->active_io->request[queue_type],
          elem->active_io->totalNumberOfUnprocessedSlices[queue_type],
          sizeof(active_io_processing_slice_t), &compareSlices);
}

static void markOpsAsInserviceandCountSlices(
    io_handle_queue_t * elem)
{
    enum IO_queue_type i;
    
    elem->active_io =
        (active_io_processing_t *) malloc(sizeof(active_io_processing_t));
    memset(elem->active_io, 0, sizeof(active_io_processing_t));
        
    /*at first mark operations as in service (reason dspace_cancel)...*/
    for (i = 0; i < IO_QUEUE_LAST; i++)
    {
        dbpf_op_queue_s *queue = &elem->requests[i];
        dbpf_queued_op_t *op;
        struct qlist_head *tmp_link;
        int slice_count = 0;

        if (i == IO_QUEUE_WRITE || i == IO_QUEUE_READ){
            enum active_queue_e active_queue;
            active_queue = (i == IO_QUEUE_WRITE) ? 
                ACTIVE_QUEUE_WRITE : ACTIVE_QUEUE_READ; 
            qlist_for_each(tmp_link, &queue->list)
            {
                op = qlist_entry(tmp_link, dbpf_queued_op_t, link);
                op->op.state = OP_IN_SERVICE;
                slice_count+= (op->op.u.b_rw_list.mem_count > 
                    op->op.u.b_rw_list.stream_count) ? 
                    op->op.u.b_rw_list.mem_count : 
                    op->op.u.b_rw_list.stream_count;
            }
            elem->active_io->totalNumberOfUnprocessedSlices[active_queue] = slice_count;
        }else{
            qlist_for_each(tmp_link, &queue->list)
            {
                op = qlist_entry(tmp_link, dbpf_queued_op_t, link);
                op->op.state = OP_IN_SERVICE;
            }            
        }
    }

    active_file_io = elem;
    gen_mutex_init(&elem->active_io->mutex);
}

static void incrementHandleRef(
    io_handle_queue_t * elem)
{
    gen_mutex_lock(&elem->active_io->mutex);
    elem->active_io->ref_count++;
    gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF incrementHandleRef %p %d\n",
                 elem, elem->active_io->ref_count);
    if (elem->active_io->filehandle == 0)
    {
        transformIOrequestsIntoArray(elem, ACTIVE_QUEUE_READ, IO_QUEUE_READ);
        transformIOrequestsIntoArray(elem, ACTIVE_QUEUE_WRITE, IO_QUEUE_WRITE);

        gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                     "IO TransformIORequestsIntoArray r:%d w:%d into slices: r:%d w:%d\n",
                     elem->requests[IO_QUEUE_READ].elems,
                     elem->requests[IO_QUEUE_WRITE].elems,
                     elem->active_io->
                     totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_READ],
                     elem->active_io->
                     totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_WRITE]);
        scheduleHandleOps(elem, ACTIVE_QUEUE_WRITE);
        scheduleHandleOps(elem, ACTIVE_QUEUE_READ);
        
        elem->active_io->filehandle = open_fd(elem->fs_id, elem->handle, 
            elem->active_io->openflags );
    }
    gen_mutex_unlock(&elem->active_io->mutex);
}

static void decrementHandleRef(
    io_handle_queue_t * elem, TROVE_size latest_offset_written)
{
    gen_mutex_lock(&elem->active_io->mutex);
    elem->active_io->ref_count--;
    gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF decrementHandleRef %p %d\n",
                 elem, elem->active_io->ref_count);
    gen_mutex_unlock(&elem->active_io->mutex);

    if (elem->active_io->ref_count == 0 && noProcessElementAvailable(elem))
    {                           
        /* update cache file size and truncate file in case O_DIRECT was used !*/
#ifdef THREADS_SCHEDULE_ONLY_ONE_HANDLE
        active_file_io = NULL;                           
        if(checkAndUpdateCacheSize(elem->fs_id, elem->handle, 
            latest_offset_written)){                
            ftruncate(elem->active_io->filehandle, latest_offset_written);
        }
#else
        checkAndUpdateCacheSize(elem->fs_id, elem->handle, 
            latest_offset_written);
#endif
            
        /*have to free HandleQueue at the end !*/
        if (elem->active_io->filehandle)
            close(elem->active_io->filehandle);
        gossip_debug(GOSSIP_TROVE_DEBUG,
                     "DBPF free handlequeue %p fd:%d\n", elem,
                     elem->active_io->filehandle);
        if (elem->active_io->request[ACTIVE_QUEUE_READ] != NULL)
        {
            free(elem->active_io->request[ACTIVE_QUEUE_READ]);
            free(elem->active_io->requestNumberOfSlices[ACTIVE_QUEUE_READ]);
            elem->active_io->request[ACTIVE_QUEUE_READ] = 0;
            elem->active_io->requestNumberOfSlices[ACTIVE_QUEUE_READ] = 0;
        }
        if (elem->active_io->request[ACTIVE_QUEUE_WRITE] != NULL)
        {
            free(elem->active_io->request[ACTIVE_QUEUE_WRITE]);
            free(elem->active_io->requestNumberOfSlices[ACTIVE_QUEUE_WRITE]);
            elem->active_io->request[ACTIVE_QUEUE_WRITE] = 0;
            elem->active_io->requestNumberOfSlices[ACTIVE_QUEUE_WRITE] = 0;
        }
        free(elem->active_io);
        elem->active_io = 0;
        free(elem);
    }
}

static int insertOperation(
    dbpf_queued_op_t * new_op,
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    enum IO_queue_type type,
    PVFS_id_gen_t * out_op_id_p)
{
    int i;
    pthread_mutex_lock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
    /*Lookup handle*/

    io_handle_queue_t *current;
    for (current = handle_queue.first; current != NULL; current = current->next)
    {
        if (current->handle == handle && current->fs_id)
        {
            break;
        }
    }

    if (current == NULL)
    {

        /*create a new handle*/
        current = (io_handle_queue_t *) malloc(sizeof(io_handle_queue_t));
        gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF new handlequeue %p\n", current);
        if (current == NULL)
        {
            pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
            return -TROVE_ENOMEM;
        }

        memset(current, 0, sizeof(io_handle_queue_t));

        current->fs_id = coll_id;
        current->handle = handle;

        if (handle_queue.first == NULL)
            handle_queue.first = current;
        else
            handle_queue.last->next = current;

        handle_queue.last = current;
        handle_queue.count++;

        for (i = 0; i < IO_QUEUE_LAST; i++)
            dbpf_op_queue_init(&current->requests[i]);
    }
    /*add object to handles queue*/
    *out_op_id_p =
        dbpf_queued_op_queue_nolock(new_op, &current->requests[type]);
    current->request_to_process_count[type]++;
    pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
    return TEST_FOR_COMPLETION;
}

static void updateCacheSize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size size)
{
    /* adjust size in cached attribute element, if present */
    TROVE_object_ref ref = { handle, coll_id };
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    dbpf_attr_cache_ds_attr_update_cached_data_bsize(ref, size);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);
}

static int checkAndUpdateCacheSize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size write_size)
{
    /* adjust size in cached attribute element, if present */
    int ret;
    TROVE_object_ref ref = { handle, coll_id };
    gen_mutex_lock(&dbpf_attr_cache_mutex);
    ret = dbpf_attr_cache_ds_attr_change_cached_data_bsize_if_necessary(ref,
                                                                  write_size);
    gen_mutex_unlock(&dbpf_attr_cache_mutex);
    return ret;
}

static void moveAllToCompletionQueue(
    dbpf_op_queue_s * op_queue,
    int ret)
{
    int i;
    dbpf_queued_op_t *request;
    for (i = 0; i < op_queue->elems; i++)
    {
        request = dbpf_op_pop_front_nolock(op_queue);
        assert(request);
        dbpf_move_op_to_completion_queue(request, ret, OP_COMPLETED);
    }
}

static void processIOSlice(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    enum IO_queue_type type,
    int fd,
    active_io_processing_slice_t * slice,
    gen_mutex_t * mutex,
    TROVE_size * io_size_out,
    TROVE_size * total_write_size_out,
    TROVE_size * latest_offset_written,
    int *return_code_out,
    int thread_no,
    char * odirectbuf,
    int openflags)
{
    TROVE_size retSize = 0;
    /*
     * Calculate real I/O access boundaries
     */
    TROVE_offset physical_startPos;
    TROVE_size   physical_size;
       
   /*
    * we could calculate the startpos with division ops, too :)
    */
   physical_startPos = (TROVE_offset) (
        ((uintptr_t) slice->stream_offset) & (~((uintptr_t) MEM_PAGESIZE - 1)));
        
   physical_size = (TROVE_size)(((uintptr_t )  
        slice->stream_offset + slice->size + MEM_PAGESIZE - 1 ) & 
        (~((uintptr_t) MEM_PAGESIZE - 1))) - physical_startPos;
   
   assert(physical_size <= IO_BUFFER_SIZE + MEM_PAGESIZE);            
    /*
     * use tmp buffer in case data is not aligned to pagesize and
     * do a read modify write for writes or read more data than 
     * necessary (for reads).
     * Problem: files have to be a size * 512 resp. MEM_PAGESIZE.
     */
    /*
     * This optimization helps for example the pvfs2-cp op.
     */     
     if ( ! (openflags & O_DIRECT) )
        {
        if (type == IO_QUEUE_WRITE)
        {
            gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                         "IO slice on thread:%d - %p - WRITE FD:%d POS:%lld SIZE:%lld \n",
                         thread_no, slice, fd, llu(slice->stream_offset), llu(slice->size));
            retSize =
                PWRITE(fd, slice->mem_offset, slice->size, slice->stream_offset);
        }
        else
        {
            gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                         "IO slice on thread:%d - %p - READ FD:%d POS:%lld SIZE:%lld \n",
                         thread_no, slice, fd, llu(slice->stream_offset), llu(slice->size));
            retSize =
                PREAD(fd, slice->mem_offset, slice->size, slice->stream_offset);
        }
        
        if (retSize == 0)
        {
            retSize = 0;
            gossip_debug(GOSSIP_TROVE_DEBUG,"WARNING could not do IO to %d\n", fd);
            *return_code_out = -trove_errno_to_trove_error(errno);
        }
        else if (retSize < slice->size)
        {
            /*could not read/write correctly ! TODO better error handling !*/
            gossip_err
                ("WARNING could not do IO correct to %d - %lld of %llu bytes done, reason: %s\n",
                 fd, lld(retSize), llu(slice->size), strerror(errno));
            *return_code_out = -trove_errno_to_trove_error(errno);
        }         
    }else{
       gossip_debug(GOSSIP_PERFORMANCE_DEBUG, 
        "Unaligned data will be aligned %llu - %llu to %llu - %llu\n",
            llu(slice->stream_offset),  llu(slice->size),
            llu(physical_startPos), llu(physical_size));       
            
       if (type == IO_QUEUE_WRITE)
       {
            gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                         "IO slice on thread:%d - %p - WRITE FD:%d POS:%llu SIZE:%llu \n",
                         thread_no, slice, fd, llu(slice->stream_offset), llu(slice->size));
            if ( slice->stream_offset != physical_startPos || 
                slice->size != physical_size){
            /* read modify write data (for now, better to check if results can be
             * reused or cached)*/
            if ( physical_size <= DIRECT_IO_SIZE_LIMIT_MULTIPLIER * DIRECT_IO_SIZE ){
                /*
                 * read in one step
                 */
                retSize =
                    PREAD(fd, odirectbuf, physical_size, physical_startPos);
                if (retSize < physical_size){
                    gossip_debug(GOSSIP_TROVE_DEBUG,"Pread error %s %llu\n",strerror(errno), llu(retSize));
                    memset( odirectbuf + retSize, 0, physical_size - retSize );
                }                    
            }else{
                /* read both ends */
                TROVE_offset end_offset;
                end_offset = physical_size - DIRECT_IO_SIZE -1;
                if (slice->stream_offset != physical_startPos){
                    retSize =
                        PREAD(fd, odirectbuf, DIRECT_IO_SIZE, physical_startPos);
                    if (retSize < DIRECT_IO_SIZE){
                        /*
                         * End of file reached bzero stuff out !
                         */
                        gossip_debug(GOSSIP_TROVE_DEBUG,"Pread1 error %s %llu\n",strerror(errno), llu(retSize));
                        memset( odirectbuf + retSize, 0, DIRECT_IO_SIZE - retSize );
                        /*
                         * This is not really needed when the file is truncated at the end...
                         * But ensures that an parallel reader gets at least no old data.
                         * could be optimized, though 
                         */
                        memset( odirectbuf + end_offset, 0, DIRECT_IO_SIZE);
                        
                        goto modifyBuffer;
                    }
                }
                
                retSize =
                    PREAD(fd, odirectbuf + end_offset, DIRECT_IO_SIZE, 
                        physical_startPos + end_offset);
                if (retSize < DIRECT_IO_SIZE){
                     gossip_debug(GOSSIP_TROVE_DEBUG,"Pread2 error %s %llu\n",strerror(errno), llu(retSize));
                     memset( odirectbuf+end_offset+ retSize, 0, 
                        DIRECT_IO_SIZE - retSize );
                }
            }
            }
modifyBuffer:                       
            /* modify buffer */
            
            memcpy(odirectbuf + (slice->stream_offset - physical_startPos), 
                slice->mem_offset, slice->size);
            
            /* write stuff back to disk */
            retSize =
                PWRITE(fd, odirectbuf, physical_size, physical_startPos);
            if (retSize < physical_size){
                 gossip_debug(GOSSIP_TROVE_DEBUG,"PWRITE error %s %llu\n",strerror(errno), llu(retSize));
                 assert(0);
            }
            retSize = slice->size;           
       }
       else
       {
            TROVE_offset buff_offset = slice->stream_offset - physical_startPos;
            gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                         "IO slice on thread:%d - %p - READ FD:%d POS:%lld SIZE:%lld \n",
                         thread_no, slice, fd, llu(slice->stream_offset), llu(slice->size));    
            /* read more data and discard */
            retSize =
                PREAD(fd, odirectbuf, physical_size, physical_startPos);
            if (retSize < physical_size){
                 gossip_debug(GOSSIP_TROVE_DEBUG,"Pread file too small %s %llu\n",strerror(errno), llu(retSize));
                 retSize =  retSize - (TROVE_size) (physical_size - buff_offset);
            }
            memcpy(slice->mem_offset, odirectbuf + buff_offset, slice->size);
       }            
    }

    gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
                 "IO slice on thread:%d DONE: %p\n", thread_no, slice);

    gen_mutex_lock(mutex);
    (*slice->remainingNumberOfSlices)--;
    *io_size_out += retSize;
    if (type == IO_QUEUE_WRITE && retSize > 0 &&
        (slice->stream_offset + retSize > *total_write_size_out))
    {
        *total_write_size_out = slice->stream_offset + retSize;
    }

    if (*(slice->remainingNumberOfSlices) == 0)
    {
        /* request is finished */
        dbpf_op_queue_remove(slice->parentRequest);
        *latest_offset_written = *total_write_size_out;
        dbpf_move_op_to_completion_queue(slice->parentRequest,
                                    *return_code_out, OP_COMPLETED);
    }
    gen_mutex_unlock(mutex);
}

static void * bstream_threaded_thread_io_function(
    void *vpthread_number)
{
    /*
     * We reuse the biggest buffer, so no contig. malloc / free necessary.
     */
    void * odirectBuffAlloc;
    char * buff_real;
    int ret;
    dbpf_queued_op_t *request;
    active_io_processing_slice_t *io_processing_slice;
    int io_slice_number;
    int thread_number = *((int *) vpthread_number);
    enum IO_queue_type req_type = -1;
    free((int *) vpthread_number);
    
    odirectBuffAlloc = malloc(IO_BUFFER_SIZE + 2 * MEM_PAGESIZE);
    /*
     * keep valgrind happy, also for debugging...
     */
    memset(odirectBuffAlloc, 255, IO_BUFFER_SIZE + 2 * MEM_PAGESIZE);
    
    if (odirectBuffAlloc == NULL) {
        gossip_err("Not enough free memory to allocate ODIRECT buffer\n");
        return NULL;
    }
    buff_real = (unsigned char *)(((uintptr_t )odirectBuffAlloc + 
                MEM_PAGESIZE - 1) & (~(MEM_PAGESIZE - 1)));

    while (1)
    { 
        /*case 1: active file has IO jobs
          case 2: active file has no IO jobs*/
        pthread_mutex_lock(&active_file_mutex);
        io_handle_queue_t *current_handle = active_file_io;

        if (current_handle != NULL)
        {
            incrementHandleRef(current_handle);

            /*choose next operation*/
            if (current_handle->request_to_process_count[IO_QUEUE_RESIZE] > 0)
            {
                req_type = IO_QUEUE_RESIZE;
                /*first RESIZE (value only last request):*/
                request =
                    (dbpf_queued_op_t *) current_handle->
                    requests[req_type].list.prev;

                current_handle->request_to_process_count[req_type] = 0;
            }
            else if (current_handle->active_io->
                     totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_WRITE] > 0)
            {
                current_handle->active_io->
                    totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_WRITE]--;
                req_type = IO_QUEUE_WRITE;
                io_slice_number =
                    current_handle->active_io->
                    positionOfProcessing[ACTIVE_QUEUE_WRITE]++;
                io_processing_slice =
                    &current_handle->active_io->
                    request[ACTIVE_QUEUE_WRITE][io_slice_number];
            }
            else if (current_handle->active_io->
                     totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_READ] > 0)
            {
                current_handle->active_io->
                    totalNumberOfUnprocessedSlices[ACTIVE_QUEUE_READ]--;
                req_type = IO_QUEUE_READ;
                io_slice_number =
                    current_handle->active_io->
                    positionOfProcessing[ACTIVE_QUEUE_READ]++;
                io_processing_slice =
                    &current_handle->active_io->
                    request[ACTIVE_QUEUE_READ][io_slice_number];
            }
            else if (current_handle->
                     request_to_process_count[IO_QUEUE_FLUSH] > 0)
            {
                /*(value only first request)*/
                req_type = IO_QUEUE_FLUSH;
                request =
                    (dbpf_queued_op_t *) current_handle->
                    requests[req_type].list.prev;
                current_handle->request_to_process_count[req_type] = 0;
            }
            else
            {
                /*
                 * do nothing
                 */
            }
#ifndef THREADS_SCHEDULE_ONLY_ONE_HANDLE
            /*check if this is the last operation:*/
            if (noProcessElementAvailable(current_handle))
            {
                active_file_io = NULL;
            }
#endif            
        }
        pthread_mutex_unlock(&active_file_mutex);

        /*case 2*/
        if (current_handle == NULL)
        {
            pthread_mutex_lock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);

            if (active_file_io == NULL)
            {
                if (handle_queue.first == NULL)
                {
                    /*gossip_debug(GOSSIP_TROVE_DEBUG, "%d: no active file waiting\n", thread_number);*/
                    if (!threads_running)
                    {
                        /*abort thread!*/
                        pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
                        gossip_debug(GOSSIP_TROVE_DEBUG,
                                     "Quit IO thread %d\n", thread_number);
                        break;
                    }
                    /*gossip_debug(GOSSIP_TROVE_DEBUG, "%d: LOCK COND WAIT\n", thread_number);*/
                    pthread_cond_wait(&dbpf_op_incoming_cond[OP_QUEUE_IO],
                                      &dbpf_op_queue_mutex[OP_QUEUE_IO]);

                    pthread_mutex_lock(&active_file_mutex);
                    if (active_file_io != NULL)
                    {
                        pthread_mutex_unlock(&active_file_mutex);
                        pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
                        continue;
                    }
                    pthread_mutex_unlock(&active_file_mutex);

                    if (handle_queue.first == NULL)
                    {
                        /*
                         * Spurious wakeup
                         */
                        pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
                        /*gossip_debug(GOSSIP_TROVE_DEBUG, "DBPF %d: Fruits eaten by other threads\n", thread_number);*/
                        continue;
                    }
                }

                current_handle = handle_queue.first;
                debugPrint("fetching new IO request queue", current_handle,
                           thread_number);

                handle_queue.first = current_handle->next;
                if (handle_queue.first == NULL)
                    handle_queue.last = NULL;

                handle_queue.count--;

                markOpsAsInserviceandCountSlices(current_handle);
            }
            pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
            continue;
        }

        switch (req_type)
        {
        case (IO_QUEUE_READ):
            processIOSlice(current_handle->fs_id, current_handle->handle,
                           req_type, current_handle->active_io->filehandle,
                           io_processing_slice,
                           &current_handle->active_io->mutex,
                           &current_handle->active_io->total_write_size,
                           io_processing_slice->parentRequest->op.u.b_rw_list.
                           out_size_p, 
                           &current_handle->active_io->latest_offset_written,
                           &current_handle->active_io->return_code,
                           thread_number, buff_real, 
                           current_handle->active_io->openflags);
            break;
        case (IO_QUEUE_WRITE):
            {
                processIOSlice(current_handle->fs_id, current_handle->handle,
                               req_type, current_handle->active_io->filehandle,
                               io_processing_slice,
                               &current_handle->active_io->mutex,
                               &current_handle->active_io->total_write_size,
                               io_processing_slice->parentRequest->op.u.
                               b_rw_list.out_size_p,
                               &current_handle->active_io->latest_offset_written,
                               &current_handle->active_io->return_code,
                               thread_number, buff_real,
                               current_handle->active_io->openflags);
                break;
            }
        case (IO_QUEUE_RESIZE):
            {
                ret = ftruncate(current_handle->active_io->filehandle,
                                request->op.u.b_resize.size);
                if (ret != 0)
                {
                    ret = -trove_errno_to_trove_error(errno);
                }
                else
                {
                    updateCacheSize(current_handle->fs_id,
                                    current_handle->handle,
                                    request->op.u.b_resize.size);
                }

                moveAllToCompletionQueue(&current_handle->
                                         requests[IO_QUEUE_RESIZE], ret);

                break;
            }
        case (IO_QUEUE_FLUSH):
            {
                ret = fdatasync(current_handle->active_io->filehandle);
                if (ret != 0)
                {
                    ret = -trove_errno_to_trove_error(errno);
                }
                moveAllToCompletionQueue(&current_handle->
                                         requests[IO_QUEUE_FLUSH], ret);

                break;
        default:
                gossip_lerr("ERROR, unknown IO type i!\n");
                assert(0);
            }
        }

        decrementHandleRef(current_handle,
            current_handle->active_io->latest_offset_written);
    }
    
    free(odirectBuffAlloc);
    return NULL;
}



int dbpf_bstream_threaded_initalize(
    )
{
    handle_queue.count = 0;
    handle_queue.first = NULL;
    handle_queue.last = NULL;

    active_file_io = NULL;
    /*spawn all threads during setting of thread NO*/

    if (initialised)
        return -1;

    initialised = 1;

    return 0;
}

int dbpf_bstream_threaded_finalize(
    )
{
    threads_running = 0;
    initialised = 0;
    /*shutdown threads safely*/
    int t;
    void *p;
    for (t = 0; t < thread_count; t++)
    {
        pthread_mutex_lock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
        pthread_cond_signal(&dbpf_op_incoming_cond[OP_QUEUE_IO]);
        pthread_mutex_unlock(&dbpf_op_queue_mutex[OP_QUEUE_IO]);
    }
    for (t = 0; t < thread_count; t++)
    {
        pthread_join(io_threads[t], &p);
    }
    return 0;
}


static int dbpf_bstream_resize(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_size * inout_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        BSTREAM_RESIZE,
                        handle, coll_p, NULL, user_ptr, flags, context_id);

    /* initialize the op-specific members */
    q_op_p->op.u.b_resize.size = *inout_size_p;

    return insertOperation(q_op_p, coll_id, handle, IO_QUEUE_RESIZE,
                           out_op_id_p);
}

/* dbpf_bstream_rw_list()
 *
 * Handles queueing of both read and write list operations
 *
 * opcode parameter should be LIO_READ or LIO_WRITE
 */
static inline int dbpf_bstream_rw_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p,
    enum IO_queue_type opcode)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        opcode ==
                        IO_QUEUE_WRITE ? BSTREAM_WRITE_LIST :
                        BSTREAM_READ_LIST, handle, coll_p, NULL, user_ptr,
                        flags, context_id);


    q_op_p->op.u.b_rw_list.mem_count = mem_count;
    q_op_p->op.u.b_rw_list.mem_offset_array = mem_offset_array;
    q_op_p->op.u.b_rw_list.mem_size_array = mem_size_array;
    q_op_p->op.u.b_rw_list.stream_count = stream_count;
    q_op_p->op.u.b_rw_list.stream_offset_array = stream_offset_array;
    q_op_p->op.u.b_rw_list.stream_size_array = stream_size_array;

    q_op_p->op.u.b_rw_list.out_size_p = out_size_p;

    return insertOperation(q_op_p, coll_id, handle, opcode, out_op_id_p);

}

static int dbpf_bstream_flush(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    dbpf_queued_op_t *q_op_p = NULL;
    struct dbpf_collection *coll_p = NULL;

    coll_p = dbpf_collection_find_registered(coll_id);
    if (coll_p == NULL)
    {
        return -TROVE_EINVAL;
    }

    q_op_p = dbpf_queued_op_alloc();
    if (q_op_p == NULL)
    {
        return -TROVE_ENOMEM;
    }

    /* initialize all the common members */
    dbpf_queued_op_init(q_op_p,
                        BSTREAM_FLUSH,
                        handle, coll_p, NULL, user_ptr, flags, context_id);

    return insertOperation(q_op_p, coll_id, handle, IO_QUEUE_FLUSH,
                           out_op_id_p);
}

static int dbpf_bstream_read_at(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    void *buffer,
    TROVE_size * inout_size_p,
    TROVE_offset offset,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_write_at(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    void *buffer,
    TROVE_size * inout_size_p,
    TROVE_offset offset,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    return -TROVE_ENOSYS;
}

static int dbpf_bstream_read_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    return dbpf_bstream_rw_list(coll_id,
                                handle,
                                mem_offset_array,
                                mem_size_array,
                                mem_count,
                                stream_offset_array,
                                stream_size_array,
                                stream_count,
                                out_size_p,
                                flags,
                                vtag,
                                user_ptr,
                                context_id, out_op_id_p, IO_QUEUE_READ);
}

static int dbpf_bstream_write_list(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    char **mem_offset_array,
    TROVE_size * mem_size_array,
    int mem_count,
    TROVE_offset * stream_offset_array,
    TROVE_size * stream_size_array,
    int stream_count,
    TROVE_size * out_size_p,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    return dbpf_bstream_rw_list(coll_id,
                                handle,
                                mem_offset_array,
                                mem_size_array,
                                mem_count,
                                stream_offset_array,
                                stream_size_array,
                                stream_count,
                                out_size_p,
                                flags,
                                vtag,
                                user_ptr,
                                context_id, out_op_id_p, IO_QUEUE_WRITE);
}

static int dbpf_bstream_validate(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    TROVE_ds_flags flags,
    TROVE_vtag_s * vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    TROVE_op_id * out_op_id_p)
{
    return -TROVE_ENOSYS;
}

struct TROVE_bstream_ops dbpf_bstream_ops = {
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    dbpf_bstream_read_list,
    dbpf_bstream_write_list,
    dbpf_bstream_flush
};

#endif
