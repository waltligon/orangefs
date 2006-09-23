/*
 * PVFS2 (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#ifndef REQUESTSCHEDULERLOGGER_H_
#define REQUESTSCHEDULERLOGGER_H_

#include "pvfs2-types.h"

/* each handle transfers 8*(3*2 + 1(handle no)) = 56 byte information */ 
#define MAX_LOGGED_HANDLES_PER_FS 200

enum sched_log_type{
    SCHED_LOG_READ = 0,
    SCHED_LOG_WRITE = 1,
    SCHED_LOG_MAX = 2
};

/*
 * The logger is not thread safe right now !
 */
void scheduler_logger_log_io(
    PVFS_fs_id   fsid,
    PVFS_handle  handle,
    PVFS_size    access_size,
    enum sched_log_type type
    );

typedef struct{
    uint64_t     io_number[SCHED_LOG_MAX];
    
    /*
     * if acc_size overflows increment acc_multiplier and put rest
     *  in acc_size with io_number => avg. access size
     */
    uint64_t     acc_size[SCHED_LOG_MAX];
} PVFS_request_statistics;

endecode_fields_4(
    PVFS_request_statistics,
    uint64_t, io_number[SCHED_LOG_READ],
    uint64_t, io_number[SCHED_LOG_WRITE],
    uint64_t, acc_size[SCHED_LOG_READ],
    uint64_t, acc_size[SCHED_LOG_WRITE]
    );

typedef struct{
    PVFS_handle handle;
    PVFS_request_statistics stat;
} PVFS_handle_request_statistics;

endecode_fields_2(
    PVFS_handle_request_statistics,
    PVFS_handle, handle,
    PVFS_request_statistics, stat
    );
    
typedef struct{
    int32_t count;
    PVFS_handle_request_statistics * stats;
} PVFS_handle_request_statistics_array;
endecode_fields_1a(
    PVFS_handle_request_statistics_array,
    skip4,,
    uint32_t, count,
    PVFS_handle_request_statistics, stats);

void scheduler_logger_initalize(void);
void scheduler_logger_finalize(void);
int scheduler_logger_fetch_data(
    PVFS_fs_id fsid, int * inout_count,
    PVFS_request_statistics * fs_stat, 
    PVFS_handle_request_statistics * h_stats);


#endif /*REQUESTSCHEDULERLOGGER_H_*/

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
