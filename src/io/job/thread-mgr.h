/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __THREAD_MGR_H
#define __THREAD_MGR_H

#include "pvfs2-types.h"
#include "bmi.h"
#include "pint-dev.h"

/* bmi thread */

struct PINT_thread_mgr_bmi_callback
{
    void (*fn)(void* data, PVFS_size actual_size, PVFS_error error_code);
    void* data;
};

int PINT_thread_mgr_bmi_cancel(PVFS_id_gen_t id, void* user_ptr);
int PINT_thread_mgr_bmi_start(void);
int PINT_thread_mgr_bmi_stop(void);
int PINT_thread_mgr_bmi_getcontext(PVFS_context_id *context);
int PINT_thread_mgr_bmi_unexp_handler(
    void (*fn)(struct BMI_unexpected_info* unexp));

/* trove thread */

struct PINT_thread_mgr_trove_callback
{
    void (*fn)(void* data, PVFS_error error_code);
    void* data;
};

int PINT_thread_mgr_trove_start(void);
int PINT_thread_mgr_trove_stop(void);
int PINT_thread_mgr_trove_getcontext(PVFS_context_id *context);
int PINT_thread_mgr_trove_cancel(PVFS_id_gen_t id, PVFS_fs_id fs_id, 
    void* user_ptr);

/* dev thread */

int PINT_thread_mgr_dev_start(void);
int PINT_thread_mgr_dev_stop(void);
int PINT_thread_mgr_dev_unexp_handler(
    void (*fn)(struct PINT_dev_unexp_info* unexp));


/* hooks to drive progress without threads */
void PINT_thread_mgr_trove_push(int max_idle_time);
void PINT_thread_mgr_bmi_push(int max_idle_time);
void PINT_thread_mgr_dev_push(int max_idle_time);

#endif /* __THREAD_MGR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
