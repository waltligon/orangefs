/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "pint-perf-counter.h"

enum 
{
   ACACHE_NUM_ENTRIES = 0,
   ACACHE_SOFT_LIMIT = 1,
   ACACHE_HARD_LIMIT = 2,
   ACACHE_HITS = 3,
   ACACHE_MISSES = 4,
   ACACHE_UPDATES = 5,
   ACACHE_PURGES = 6,
   ACACHE_REPLACEMENTS = 7,
   ACACHE_ENABLED = 8,
};

struct PINT_perf_key acache_keys[] = 
{
   {"ACACHE_NUM_ENTRIES", ACACHE_NUM_ENTRIES, PINT_PERF_PRESERVE},
   {"ACACHE_SOFT_LIMIT", ACACHE_SOFT_LIMIT, PINT_PERF_PRESERVE},
   {"ACACHE_HARD_LIMIT", ACACHE_HARD_LIMIT, PINT_PERF_PRESERVE},
   {"ACACHE_HITS", ACACHE_HITS, 0},
   {"ACACHE_MISSES", ACACHE_MISSES, 0},
   {"ACACHE_UPDATES", ACACHE_UPDATES, 0},
   {"ACACHE_PURGES", ACACHE_PURGES, 0},
   {"ACACHE_REPLACEMENTS", ACACHE_REPLACEMENTS, 0},
   {"ACACHE_ENABLED", ACACHE_ENABLED, PINT_PERF_PRESERVE},
   {NULL, 0, 0},
};

static void usage(int argc, char** argv);

static void print_counters(struct PINT_perf_counter* pc, int* in_key_count,
    int* in_history_size);

int main(int argc, char **argv)
{
    struct PINT_perf_counter* pc;
    int tmp_key_count;
    int tmp_history_size;
    char* tmp_str;

    if(argc != 1)
    {
        usage(argc, argv);
        return(-1);
    }

    printf("Initializing...");
    pc = PINT_perf_initialize(acache_keys);
    if(!pc)
    {
        fprintf(stderr, "Error: PINT_perf_initialize() failure.\n");
        return(-1);
    }
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    PINT_perf_count(pc, ACACHE_NUM_ENTRIES, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_NUM_ENTRIES, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_ENABLED, 1, PINT_PERF_SET);
    PINT_perf_count(pc, ACACHE_HARD_LIMIT, 500, PINT_PERF_SET);

    print_counters(pc, NULL, NULL);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    PINT_perf_count(pc, ACACHE_NUM_ENTRIES, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_HARD_LIMIT, 300, PINT_PERF_SET);

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    PINT_perf_count(pc, ACACHE_NUM_ENTRIES, 1, PINT_PERF_SUB);

    printf("Testing generate text...\n");
    tmp_str = PINT_perf_generate_text(pc, 4096);
    printf("%s", tmp_str);
    printf("Done.\n");
    free(tmp_str);

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    sleep(2);

    print_counters(pc, NULL, NULL);

    printf("Reducing history size...");
    PINT_perf_set_info(pc, PINT_PERF_HISTORY_SIZE, 3);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);

    print_counters(pc, NULL, NULL);

    sleep(2);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    printf("Increasing history size...");
    PINT_perf_set_info(pc, PINT_PERF_HISTORY_SIZE, 5);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    printf("Retrieving larger history and smaller keys than available.\n");
    tmp_key_count = 4;
    tmp_history_size = 8;
    print_counters(pc, &tmp_key_count, &tmp_history_size);

    printf("Reducing history size to one...");
    PINT_perf_set_info(pc, PINT_PERF_HISTORY_SIZE, 1);
    printf("Done.\n");

    print_counters(pc, NULL, NULL);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);
    PINT_perf_count(pc, ACACHE_HITS, 1, PINT_PERF_ADD);

    sleep(1);

    print_counters(pc, NULL, NULL);

    printf("Testing rollover...");
    PINT_perf_rollover(pc);
    printf("Done.\n");

    printf("Testing generate text...\n");
    tmp_str = PINT_perf_generate_text(pc, 4096);
    printf("%s", tmp_str);
    printf("Done.\n");
    free(tmp_str);

    printf("Finalizing...");
    PINT_perf_finalize(pc);
    printf("Done.\n");

    return(0);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s\n", argv[0]);
    return;
}

static void print_counters(struct PINT_perf_counter* pc, int* in_key_count,
    int* in_history_size)
{
    unsigned int key_count;
    unsigned int history_size;
    int ret;
    int64_t** stat_matrix;
    uint64_t* start_time_array_ms;
    uint64_t* interval_array_ms;
    int i,j;

    if(in_key_count)
    {
        key_count = *in_key_count;
    }
    else
    {
        /* get dimensions */
        ret = PINT_perf_get_info(pc, PINT_PERF_KEY_COUNT, 
            &key_count);
        assert(ret == 0);
    }

    if(in_history_size)
    {
        history_size = *in_history_size;
    }
    else
    {
        ret = PINT_perf_get_info(pc, PINT_PERF_HISTORY_SIZE, 
            &history_size);
        assert(ret == 0);
    }

    /* allocate storage for results */
    stat_matrix = malloc(key_count*sizeof(int64_t*));
    for(i=0; i<key_count; i++)
    {
        stat_matrix[i] = malloc(history_size*sizeof(int64_t));
        assert(stat_matrix[i]);
    }
    start_time_array_ms = malloc(history_size*sizeof(uint64_t));
    assert(start_time_array_ms);
    interval_array_ms = malloc(history_size*sizeof(uint64_t));
    assert(interval_array_ms);

    /* retrieve values from perf counter api */
    PINT_perf_retrieve(
       pc,
       stat_matrix,
       start_time_array_ms,
       interval_array_ms,
       key_count,
       history_size);

    printf("===================\n");

    /* print times (column headings) */
    printf("First start time (ms): %llu\n", start_time_array_ms[0]);
    printf("%24.24s: ", "Interval size (ms)");
    for(i=0; i<history_size; i++)
    {
        printf("%llu\t", llu(interval_array_ms[i]));
    }
    printf("\n");

    /* print key names and values */
    for(i=0; i<key_count; i++)
    {
        printf("%24.24s: ", acache_keys[i].key_name);

        for(j=0; j<history_size; j++)
        {
            printf("%lld\t", lld(stat_matrix[i][j]));
        }
        printf("\n");
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

