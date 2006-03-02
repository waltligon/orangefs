
#ifndef BENCHMARK_UTILS
#define BENCHMARK_UTILS

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"

int test_util_init_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credentials creds,
    int32_t flags,
    int * server_count);

int test_util_get_queue_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credentials creds,
    int count);

void test_util_start_timing(void);
void test_util_stop_timing(void);
void test_util_print_avg_and_dev(void);

#endif
