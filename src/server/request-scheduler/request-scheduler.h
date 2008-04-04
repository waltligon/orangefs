/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \defgroup reqsched Request scheduler
 *
 *  The request scheduler maintains structures that track ongoing and
 *  incoming requests for the purposes of maintaining consistency and
 *  server-side scheduling for performance.
 *
 * @{
 */

/** \file
 *  Declarations for the request scheduler.
 */

#ifndef __REQUEST_SCHEDULER_H
#define __REQUEST_SCHEDULER_H

#include "pvfs2-req-proto.h"

typedef PVFS_id_gen_t req_sched_id;
typedef int req_sched_error_code;
enum PINT_server_req_access_type
{
    PINT_SERVER_REQ_READONLY = 0,
    PINT_SERVER_REQ_MODIFY
};
enum PINT_server_sched_policy
{
    PINT_SERVER_REQ_BYPASS = 0,
    PINT_SERVER_REQ_SCHEDULE
};

/* setup and teardown */
int PINT_req_sched_initialize(
    void);

int PINT_req_sched_finalize(
    void);

/* retrieving information about incoming requests */
/* scheduler submission */
int PINT_req_sched_post(enum PVFS_server_op op,
                        PVFS_fs_id fs_id,
                        PVFS_handle handle,
                        enum PINT_server_req_access_type access_type,
                        enum PINT_server_sched_policy sched_policy,
			void *in_user_ptr,
			req_sched_id * out_id);

enum PVFS_server_mode PINT_req_sched_get_mode(void);

int PINT_req_sched_change_mode(enum PVFS_server_mode mode,
                               void *user_ptr,
                               req_sched_id *id);

int PINT_req_sched_unpost(req_sched_id in_id,
			  void **returned_user_ptr);

int PINT_req_sched_release(req_sched_id in_completed_id,
			   void *in_user_ptr,
			   req_sched_id * out_id);

int PINT_req_sched_post_timer(int msecs,
			      void *in_user_ptr,
			      req_sched_id * out_id);

/* testing for completion */
int PINT_req_sched_test(req_sched_id in_id,
			int *out_count_p,
			void **returned_user_ptr_p,
			req_sched_error_code * out_status);

int PINT_req_sched_testsome(req_sched_id * in_id_array,
			    int *inout_count_p,
			    int *out_index_array,
			    void **returned_user_ptr_array,
			    req_sched_error_code * out_status_array);

int PINT_req_sched_testworld(int *inout_count_p,
			     req_sched_id * out_id_array,
			     void **returned_user_ptr_array,
			     req_sched_error_code * out_status_array);

#endif /* __REQUEST_SCHEDULER_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
