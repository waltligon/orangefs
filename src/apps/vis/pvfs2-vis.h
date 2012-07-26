/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_VIS_H
#define __PVFS2_VIS_H

#include <pthread.h>

struct pvfs2_vis_buffer
{
    int io_count;
    int io_depth;
    struct PVFS_mgmt_perf_stat** io_perf_matrix;
    uint64_t* io_end_time_ms_array;
};

int pvfs2_vis_start(char* path, int update_interval);
int pvfs2_vis_stop(void);

extern struct pvfs2_vis_buffer pint_vis_shared;
extern int pint_vis_error;
extern pthread_mutex_t pint_vis_mutex;
extern pthread_cond_t pint_vis_cond;

#endif /* __PVFS2_VIS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
