#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>
#include <string.h>

#include "trove.h"
#include "trove-types.h"
#include "pvfs2-internal.h"
#include "quicklist.h"

enum op_type
{
    WRITE,
    READ
};

struct bench_op
{
    TROVE_offset offset;
    TROVE_size size;
    enum op_type type;
    struct qlist_head list_link;
    TROVE_keyval_s key;
    TROVE_keyval_s value;
};

struct op_buffer
{
    char* buffer;
    TROVE_size size;
    struct qlist_head list_link;
};

QLIST_HEAD(op_list);
QLIST_HEAD(buffer_list);

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}
static double start_tm;
static double end_tm;
static int64_t total_size = 0;
int concurrent;
int meta_sync;
static char data_mode;
int ops = 0;

static TROVE_method_id trove_method_callback(TROVE_coll_id id)
{
    return(TROVE_METHOD_DBPF_DIRECTIO);
}

static int do_trove_test(char* dir)
{
    int ret;
    TROVE_op_id op_id;
    TROVE_handle_extent_array extent_array;
    TROVE_extent cur_extent;
    TROVE_handle test_handle;
    TROVE_context_id trove_context = -1;
    int count;
    TROVE_ds_state state;
    TROVE_coll_id coll_id;
    int inflight = 0;
    int i;
    struct op_buffer* tmp_buffer;
    struct qlist_head* tmp_link;
    struct bench_op* tmp_op;
    TROVE_size out_size;
    TROVE_op_id id_array[concurrent];
    TROVE_ds_state state_array[concurrent];
    void* user_ptr_array[concurrent];

    ret = trove_initialize(TROVE_METHOD_DBPF_DIRECTIO, trove_method_callback, dir, dir, 0);
    if(ret < 0)
    {
        /* try to create new storage space */
        ret = trove_storage_create(TROVE_METHOD_DBPF_DIRECTIO, dir, dir, NULL, &op_id);
        if(ret != 1)
        {
            fprintf(stderr, "Error: failed to create storage space at %s\n",
                dir);
            return(-1);
        }
        
        ret = trove_initialize(TROVE_METHOD_DBPF_DIRECTIO, trove_method_callback, dir, dir, 0);
        if(ret < 0)
        {
            fprintf(stderr, "Error: failed to initialize.\n");
            return(-1);
        }

        ret = trove_collection_create("foo", 1, NULL, &op_id);
        if(ret != 1)
        {
            fprintf(stderr, "Error: failed to create collection.\n");
            return(-1);
        }
    }

    ret = trove_open_context(1, &trove_context);
    if (ret < 0)
    {
        fprintf(stderr, "Error: trove_open_context failed\n");
        return -1;
    }

    ret = trove_collection_lookup(TROVE_METHOD_DBPF_DIRECTIO, "foo", &coll_id, NULL, &op_id);
    if (ret != 1) {
	fprintf(stderr, "collection lookup failed.\n");
	return -1;
    }

    trove_collection_setinfo(1, trove_context, 
        TROVE_COLLECTION_META_SYNC_MODE, (void *)&meta_sync);

    cur_extent.first = cur_extent.last = 1;
    extent_array.extent_count = 1;
    extent_array.extent_array = &cur_extent;

    ret = trove_dspace_create(1, &extent_array, &test_handle, 1, NULL,
        (TROVE_SYNC | TROVE_FORCE_REQUESTED_HANDLE), NULL, trove_context,
        &op_id, NULL);
    while (ret == 0) ret = trove_dspace_test(
        1, op_id, trove_context, &count, NULL, NULL, &state,
        10);
    if (ret < 0) {
	fprintf(stderr, "Error: failed to create test handle.\n");
	return -1;
    }
    if(state != 0 && state != -TROVE_EEXIST)
    {
	fprintf(stderr, "Error: failed to create test handle.\n");
	return -1;
    }

    /* create N buffers */
    for(i=0; i<concurrent; i++)
    {
        tmp_buffer = malloc(sizeof(*tmp_buffer));
        assert(tmp_buffer);
        /* TODO: make this configurable, and check that nothing in workload
         * is bigger than this 
         */
        tmp_buffer->size = 4*1024*1024;
        tmp_buffer->buffer = malloc(tmp_buffer->size);
        assert(tmp_buffer->buffer);
        qlist_add_tail(&tmp_buffer->list_link, &buffer_list);
    }

    start_tm = Wtime();

    while(inflight > 0 || !qlist_empty(&op_list))
    {
        /* first priority is to keep maximum ops posted */
        while(inflight < concurrent && !qlist_empty(&op_list))
        {
            /* get a buffer */
            tmp_link = qlist_pop(&buffer_list);
            assert(tmp_link);
            tmp_buffer = qlist_entry(tmp_link, struct op_buffer,
                list_link);

            /* get next op */
            tmp_link = qlist_pop(&op_list);
            assert(tmp_link);
            tmp_op = qlist_entry(tmp_link, struct bench_op, 
                list_link);

            total_size += tmp_op->size;
            assert(tmp_buffer->size <= tmp_buffer->size);

            if(tmp_op->type == WRITE && data_mode == 'd')
            {
                /* post operation */
                ret = trove_bstream_write_list(1, 1, &tmp_buffer->buffer,
                    &tmp_op->size, 1, &tmp_op->offset, &tmp_op->size, 1,
                    &out_size, TROVE_SYNC, NULL, tmp_buffer, trove_context, &op_id,
                    NULL);
            }
            else if(tmp_op->type == READ && data_mode == 'd')
            {
                /* post operation */
                ret = trove_bstream_read_list(1, 1, &tmp_buffer->buffer,
                    &tmp_op->size, 1, &tmp_op->offset, &tmp_op->size, 1,
                    &out_size, 0, NULL, tmp_buffer, trove_context, &op_id,
                    NULL);
            }
            else if(tmp_op->type == WRITE && data_mode == 'k')
            {
                /* post operation */
                tmp_op->key.buffer = &tmp_op->offset;
                tmp_op->key.buffer_sz = sizeof(tmp_op->offset);
                tmp_op->value.buffer = tmp_buffer->buffer;
                tmp_op->value.buffer_sz = tmp_op->size;

                ret = trove_keyval_write(1, 1, &tmp_op->key,
                    &tmp_op->value, TROVE_SYNC, NULL, tmp_buffer,
                    trove_context, &op_id, NULL);
            }
            else if(tmp_op->type == READ && data_mode == 'k')
            {
                /* post operation */
                tmp_op->key.buffer = &tmp_op->offset;
                tmp_op->key.buffer_sz = sizeof(tmp_op->offset);
                tmp_op->value.buffer = tmp_buffer->buffer;
                tmp_op->value.buffer_sz = tmp_op->size;

                ret = trove_keyval_read(1, 1, &tmp_op->key,
                    &tmp_op->value, 0, NULL, tmp_buffer,
                    trove_context, &op_id, NULL);
            }
            else
            {
                assert(0);
            }

            ops++;
            inflight++;

            assert(ret == 0);
        }

        /* poll */
        count = concurrent;
        ret = trove_dspace_testcontext(1, id_array, &count, state_array,
            user_ptr_array, 10, trove_context);
        for(i=0; i<count; i++)
        {
            assert(state_array[i] == 0);
            inflight--;
            tmp_buffer = user_ptr_array[i];
            qlist_add_tail(&tmp_buffer->list_link, &buffer_list);
        }
    }

    end_tm = Wtime();

    return 0;
}

int main(int argc, char *argv[])
{
    FILE *desc = 0;
    char line[2048];
    char op_string[100];
    int64_t size;
    int64_t offset;
    int ret;
    struct bench_op* tmp_op;

    if(argc != 6)
    {
        fprintf(stderr, "Usage: trove-bench-concurrent <workload description file> <trove dir> <concurrent ops> <meta sync 1|0> <data/keyval d|k>\n");
        return(-1);
    }

    ret = sscanf(argv[3], "%d", &concurrent);
    if(ret != 1 || concurrent < 1)
    {
        fprintf(stderr, "Usage: trove-bench-concurrent <workload description file> <trove dir> <concurrent ops> <meta sync 1|0> <data/keyval d|k>\n");
    }
    ret = sscanf(argv[4], "%d", &meta_sync);
    if(ret != 1 || meta_sync > 1 || meta_sync < 0)
    {
        fprintf(stderr, "Usage: trove-bench-concurrent <workload description file> <trove dir> <concurrent ops> <meta sync 1|0> <data/keyval d|k>\n");
    }
    ret = sscanf(argv[5], "%c", &data_mode);
    if(ret != 1 || (data_mode != 'k' && data_mode != 'd'))
    {
        fprintf(stderr, "Usage: trove-bench-concurrent <workload description file> <trove dir> <concurrent ops> <meta sync 1|0> <data/keyval d|k>\n");
    }

    /* parse description of workload */
    desc = fopen(argv[1], "r");
    if(!desc)
    {
        perror("fopen");
        return(-1);
    }
    while(fgets(line, 2048, desc))
    {
        if(line[0] == '#')
            continue;
#if SIZEOF_LONG_INT == 4
        ret = sscanf(line, "%s %lld %lld", op_string, &offset, &size);
#else
        ret = sscanf(line, "%s %ld %ld", op_string, &offset, &size);
#endif
        if(ret != 3)
        {
            fprintf(stderr, "Error: bad line: %s\n", line);
            return(-1);
        }
        tmp_op = malloc(sizeof(*tmp_op));
        assert(tmp_op);

        if(strcmp(op_string, "write") == 0)
            tmp_op->type = WRITE;
        else if(strcmp(op_string, "read") == 0)
            tmp_op->type = READ;
        else
            assert(0);
        tmp_op->offset = offset;
        tmp_op->size = size;
        qlist_add_tail(&tmp_op->list_link, &op_list);
    }
    fclose(desc);

    do_trove_test(argv[2]);

    printf("# Moved %lld bytes in %f seconds.\n", lld(total_size),
        (end_tm-start_tm));
    printf("%f MB/s\n", (((double)total_size)/(1024.0*1024.0))/(end_tm-start_tm));
    printf("%f ops/s\n", ((double)ops)/(end_tm-start_tm));

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
