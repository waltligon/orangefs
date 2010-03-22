/*
 * Copyright  Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */

#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <aio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "quicklist.h"
#include "pint-util.h"
#include "pvfs2-internal.h"

struct options
{
    int total_ops;
    int concurrent_ops;
    long long size;
    char* filename;
    char* in_filename;
    int alt_aio;
    int rand_off;
    int mix_reads;
    int delete;
};

struct aio_entry
{
    struct aiocb cb;
    struct aiocb* cb_p;
    struct sigevent sig;
    PINT_time_marker start;
    PINT_time_marker end;
    struct qlist_head list_link;
};


QLIST_HEAD(aio_queue);
static int aio_queue_count = 0;
pthread_mutex_t aio_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static int aio_done_count = 0;
pthread_mutex_t aio_done_count_mutex = PTHREAD_MUTEX_INITIALIZER;

static int parse_args(int argc, char **argv, struct options* opts);
static void usage(void);
static void aio_callback(sigval_t sig);

static float time_sum = 0;
static float time_max = 0;
static pthread_mutex_t time_mutex = PTHREAD_MUTEX_INITIALIZER;

PINT_time_marker total_start;
PINT_time_marker total_end;

#define IO_SIZE (256*1024)

#define WORKER_THREADS 16

struct alt_aio_item
{
    struct aiocb *cb_p;
    struct sigevent *sig;
    struct qlist_head list_link;
};
QLIST_HEAD(alt_aio_queue);
pthread_mutex_t alt_aio_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alt_aio_queue_cond = PTHREAD_COND_INITIALIZER;

void* thread_fn_read(void*);
void* thread_fn_write(void*);
int alt_lio_listio(int mode, struct aiocb *list[],
    int nent, struct sigevent *sig);


static struct options opts;

int main(int argc, char **argv)	 
{
    int ret;
    struct aio_entry* aio_array;
    int i = 0;
    int my_fd = -1;
    int my_in_fd = -1;
    char* my_buffer = NULL;
    long long cur_offset = 0;
    int should_exit = 0;
    double wtime, utime, stime;
    int region;
    int num_regions;

    ret = parse_args(argc, argv, &opts);
    if (ret < 0) 
    {
	fprintf(stderr, "Error: argument parsing failed.\n");
        usage();
	return -1;
    }

    printf("file size: %lld\n", opts.size);
    printf("total ops: %d\n", opts.total_ops);
    printf("concurrent ops: %d\n", opts.concurrent_ops);
    printf("filename: %s\n", opts.filename);
    if(opts.alt_aio)
    {
        printf("aio: ALTERNATE.\n");
#if 0
        for(i=0; i<WORKER_THREADS; i++)
        {
            ret = pthread_create(&tid, NULL, thread_fn, NULL);
            assert(ret == 0);
        }
#endif
    }
    else
    {
        printf("aio: NORMAL.\n");
    }

    pthread_mutex_lock(&aio_queue_mutex);
    aio_queue_count = opts.total_ops;
    pthread_mutex_unlock(&aio_queue_mutex);

    if(opts.concurrent_ops > opts.total_ops)
    {
        fprintf(stderr, "Error: concurrent ops > total ops.\n");
        return(-1);
    }

    /* open file */
    my_fd = open(opts.filename, (O_RDWR|O_CREAT), (S_IRUSR|S_IWUSR));
    if(my_fd < 0)
    {
        perror("open");
        return(-1);
    }

    /* open file */
    /* do we have a seperate file for reads or not? */
    if(opts.in_filename)
    {
        my_in_fd = open(opts.in_filename, (O_RDWR), (S_IRUSR|S_IWUSR));
        if(my_in_fd < 0)
        {
            perror("open");
            return(-1);
        }
    }
    else
    {
        my_in_fd = my_fd;
    }

    /* allocate buffer to write */
    my_buffer = malloc(IO_SIZE);

    /* allocate structures ahead of time to run each op */
    aio_array = (struct aio_entry*)malloc(opts.total_ops*sizeof(struct
        aio_entry));
    assert(aio_array);
    memset(aio_array, 0, (opts.total_ops*sizeof(struct aio_entry)));

    srand((unsigned int)time(NULL));
    num_regions = opts.size / RAND_MAX;

    for(i = 0; i<opts.total_ops; i++)
    {
        /* fill in description */
        if(opts.rand_off)
        {
            /* this extra logic is go overcome the fact that rand won't
             * produce large enough integers for large files
             */
            if(num_regions)
            {
                region = rand() % num_regions;
            }
            else
            {
                region = 0;
            }
            aio_array[i].cb.aio_offset = 
                (off_t)((off_t)(rand()) % (off_t)opts.size);
            aio_array[i].cb.aio_offset += 
                (((off_t)(region)) * ((off_t)(RAND_MAX)));
            /* make sure we don't go beyond eof */
            aio_array[i].cb.aio_offset = 
                (aio_array[i].cb.aio_offset % ((off_t)(opts.size-IO_SIZE)));
        }
        else
        {
            aio_array[i].cb.aio_offset = 0;
            aio_array[i].cb.aio_offset += cur_offset;
            cur_offset += IO_SIZE;
            if(cur_offset > (opts.size-IO_SIZE))
            {
                cur_offset = 0;
            }
        }
        /* they will all write the same buffer of garbage */
        aio_array[i].cb.aio_buf = my_buffer;
        aio_array[i].cb.aio_nbytes = IO_SIZE;
        aio_array[i].cb.aio_reqprio = 0;
        if(opts.mix_reads)
        {
            /* 0 and 1 are reads and writes respectively */
            aio_array[i].cb.aio_lio_opcode = rand() % 2;
        }
        else
        {
            aio_array[i].cb.aio_lio_opcode = LIO_WRITE;
        }
        if(aio_array[i].cb.aio_lio_opcode == LIO_WRITE)
        {
            aio_array[i].cb.aio_fildes = my_fd;
        }
        else
        {
            aio_array[i].cb.aio_fildes = my_in_fd;
        }

        /* need to fill in sigevent */
        aio_array[i].sig.sigev_notify = SIGEV_THREAD;
        aio_array[i].sig.sigev_notify_attributes = NULL;
        aio_array[i].sig.sigev_notify_function = aio_callback;
        aio_array[i].sig.sigev_value.sival_ptr = (void *)(&aio_array[i]);

        aio_array[i].cb_p = &aio_array[i].cb;

        pthread_mutex_lock(&aio_queue_mutex);
        qlist_add_tail(&aio_array[i].list_link, &aio_queue);
        pthread_mutex_unlock(&aio_queue_mutex);
    }

    PINT_time_mark(&total_start);
    pthread_mutex_lock(&aio_queue_mutex);
    for(i=0; i<opts.concurrent_ops; i++)
    {
        qlist_del(&aio_array[i].list_link);
        aio_queue_count--;
        
        PINT_time_mark(&aio_array[i].start);
        if(opts.alt_aio)
        {
            ret = alt_lio_listio(LIO_NOWAIT, &aio_array[i].cb_p, 1,
                &aio_array[i].sig);
        }
        else
        {
            ret = lio_listio(LIO_NOWAIT, &aio_array[i].cb_p, 1,
                &aio_array[i].sig);
        }
        assert(ret == 0);
    }
    pthread_mutex_unlock(&aio_queue_mutex);

    do
    {
        sleep(1);
        pthread_mutex_lock(&aio_done_count_mutex);
        if(aio_done_count == opts.total_ops)
        {
            should_exit = 1;
        }
        pthread_mutex_unlock(&aio_done_count_mutex);
    }while(!should_exit);

    free(aio_array);

    if(my_fd != my_in_fd)
    {
        close(my_in_fd);
    }
        
    close(my_fd);

    if(opts.delete)
    {
        ret = unlink(opts.filename);
        if(ret < 0)
        {
            perror("unlink");
            return(-1);
        }
    }

    PINT_time_diff(total_start, total_end, &wtime, &utime, &stime);
    printf("TEST COMPLETE.\n");
    printf("Maximum service time: %f seconds\n", time_max);
    printf("Average service time: %f seconds\n", (time_sum/opts.total_ops));
    printf("Total time: %f seconds\n", wtime);

    return(0);
}

static int parse_args(int argc, char **argv, struct options* opts)
{
    int c;
    int ret;
    long long g2;

    memset(opts, 0, sizeof(struct options));
    opts->total_ops = 100;
    opts->concurrent_ops = 16;
    opts->size = 1024*1024*1024;

    while ((c = getopt(argc, argv, "t:c:s:hf:ardRi:")) != EOF) {
	switch (c) {
	    case 't': /* total operations */
                ret = sscanf(optarg, "%d", &opts->total_ops);
                if(ret != 1)
                {
                    return(-1);
                }
		break;
	    case 'c': /* concurrent operations */
                ret = sscanf(optarg, "%d", &opts->concurrent_ops);
                if(ret != 1)
                {
                    return(-1);
                }
		break;
	    case 's': /* size of file */
                ret = sscanf(optarg, "%lld",
                    &opts->size);
                if(ret != 1)
                {
                    return(-1);
                }
		break;
            case 'f':
                opts->filename = (char*)malloc(strlen(optarg)+1);
                assert(opts->filename);
                strcpy(opts->filename, optarg);
                break;
	    case 'h': /* help */
                usage();
                return(0);
		break;
	    case 'a':
                opts->alt_aio = 1;
		break;
	    case 'r':
                opts->rand_off = 1;
		break;
	    case 'd':
                opts->delete = 1;
		break;
	    case 'R':
                opts->mix_reads = 1;
		break;
	    case 'i':
                opts->in_filename = (char*)malloc(strlen(optarg)+1);
                assert(opts->in_filename);
                strcpy(opts->in_filename, optarg);
                break;
	    case '?':
	    default:
		return -1;
	}
    }
    if(!opts->filename)
    {
        fprintf(stderr, "Error: must specify filename with -f.\n");
        return(-1);
    }
    g2 = 1024*1024*1024;
    g2 *= 2;
    if((opts->rand_off) && (opts->size > g2) && (opts->size % g2))
    {
        fprintf(stderr, "Error: if size greater than 2G, then the size must\n");
        fprintf(stderr, "       be a multiple of 2G to preserve randomness.\n");
        return(-1);
    }
    return 0;
}

static void usage(void)
{
    printf("USAGE: test-aio [options]\n");
    printf("  -t: total number of operations\n");
    printf("  -c: number of operations to process concurrently\n");
    printf("  -s: overall size of file\n");
    printf("  -h: display usage information\n");
    printf("  -f: file name\n");
    printf("  -a: enable alternat aio implementation\n");
    printf("  -r: enable random offset selection\n");
    printf("  -R: include a mixture of read operations.\n");
    printf("  -d: delete file upon completion.\n");
    printf("  -i: specify a different file for input (read operations).\n");
}

static void aio_callback(sigval_t sig)
{
    struct aio_entry* tmp_ent = (struct aio_entry*)sig.sival_ptr;
    int ret;
    struct aio_entry* next_ent;
    double wtime, utime, stime;

    pthread_mutex_lock(&aio_queue_mutex);

    /* check error code */
    ret = aio_error(tmp_ent->cb_p);
    assert(ret == 0);

    PINT_time_mark(&tmp_ent->end);
    PINT_time_diff(tmp_ent->start, tmp_ent->end, &wtime, &utime, &stime);

    /* submit another one if available */
    if(aio_queue_count > 0)
    {
        /* get next item off of the queue */
        next_ent = qlist_entry(aio_queue.next, struct aio_entry, list_link);
        qlist_del(&next_ent->list_link);
        aio_queue_count--;

        PINT_time_mark(&next_ent->start);
        if(opts.alt_aio)
        {
            ret = alt_lio_listio(LIO_NOWAIT, &next_ent->cb_p, 1,
                &next_ent->sig);
        }
        else
        {
            ret = lio_listio(LIO_NOWAIT, &next_ent->cb_p, 1,
                &next_ent->sig);
        }
        assert(ret == 0);
    }

    pthread_mutex_unlock(&aio_queue_mutex);

    pthread_mutex_lock(&time_mutex);
    time_sum += wtime;
    if(wtime > time_max)
    {
        time_max = wtime;
    }
    pthread_mutex_unlock(&time_mutex);

    pthread_mutex_lock(&aio_done_count_mutex);
    aio_done_count++;
    printf("callback count: %d, elapsed time: %f seconds\n",
        aio_done_count, wtime);
    if(aio_done_count == opts.total_ops)
    {
        PINT_time_mark(&total_end);
    }
    pthread_mutex_unlock(&aio_done_count_mutex);

    return;
}

void* thread_fn_write(void* foo)
{
    struct alt_aio_item* tmp_item = (struct alt_aio_item*)foo;
    int ret = 0;

    ret = pwrite(tmp_item->cb_p->aio_fildes,
        (const void*)tmp_item->cb_p->aio_buf,
        tmp_item->cb_p->aio_nbytes,
        tmp_item->cb_p->aio_offset);
    assert(ret == tmp_item->cb_p->aio_nbytes);

    /* run callback fn */
    tmp_item->sig->sigev_notify_function(
        tmp_item->sig->sigev_value);

    free(tmp_item);
        
    return(NULL);
}

void* thread_fn_read(void* foo)
{
    struct alt_aio_item* tmp_item = (struct alt_aio_item*)foo;
    int ret = 0;

    ret = pread(tmp_item->cb_p->aio_fildes,
        (void*)tmp_item->cb_p->aio_buf,
        tmp_item->cb_p->aio_nbytes,
        tmp_item->cb_p->aio_offset);
    assert(ret == tmp_item->cb_p->aio_nbytes);

    /* run callback fn */
    tmp_item->sig->sigev_notify_function(
        tmp_item->sig->sigev_value);

    free(tmp_item);
        
    return(NULL);
}


int alt_lio_listio(int mode, struct aiocb *list[],
    int nent, struct sigevent *sig)
{
    struct alt_aio_item* tmp_item;
    int ret;
    pthread_t tid;

    assert(mode == LIO_NOWAIT);
    assert(nent == 1);

    tmp_item = (struct alt_aio_item*)malloc(sizeof(struct alt_aio_item));
    tmp_item->cb_p = list[0];
    tmp_item->sig = sig;

    if(tmp_item->cb_p->aio_lio_opcode == LIO_READ)
    {
        ret = pthread_create(&tid, NULL, thread_fn_read, tmp_item);
    }
    else if(tmp_item->cb_p->aio_lio_opcode == LIO_WRITE)
    {
        ret = pthread_create(&tid, NULL, thread_fn_write, tmp_item);
    }
    else
    {
        assert(0);
    }
    if(ret != 0)
    {
        perror("pthread_create");
    }
    assert(ret == 0);
    ret = pthread_detach(tid);
    assert(ret == 0);
    
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

