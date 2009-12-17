#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>

#include "trove.h"
#include "pvfs2-internal.h"
#include "quicklist.h"

enum op_type
{
    WRITE,
    READ
};

struct bench_op
{
    int64_t offset;
    int64_t size;
    enum op_type type;
    struct qlist_head list_link;
};


QLIST_HEAD(op_list);

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

static int do_trove_test(void)
{
    start_tm = Wtime();

    sleep(1);

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

    if(argc != 4)
    {
        fprintf(stderr, "Usage: trove-bench-concurrent <workload description file> <trove dir> <concurrent ops>\n");
        return(-1);
    }

    ret = sscanf(argv[3], "%d", &concurrent);
    if(ret != 1 || concurrent < 1)
    {
        fprintf(stderr, "Usage: trove-bench-concurrent <1|2> <workload description file> <trove dir> <concurrent ops>\n");
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

        /* only writes for now */
        assert(strcmp(op_string, "write") == 0);
        tmp_op->type = WRITE;
        tmp_op->offset = offset;
        tmp_op->size = size;
        qlist_add_tail(&tmp_op->list_link, &op_list);
    }
    fclose(desc);

    do_trove_test();

    printf("# Moved %lld bytes in %f seconds.\n", lld(total_size),
        (end_tm-start_tm));
    printf("%f MB/s\n", (((double)total_size)/(1024.0*1024.0))/(end_tm-start_tm));

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
