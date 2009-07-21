/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_CONTEXT_H
#define PINT_CONTEXT_H

#include "pint-op.h"
#include "pint-queue.h"

enum PINT_context_type
{
    PINT_CONTEXT_TYPE_QUEUE = 1,
    PINT_CONTEXT_TYPE_CALLBACK = 2
};

typedef PVFS_id_gen_t PINT_context_id;

typedef int (*PINT_completion_callback)(PINT_context_id ctx_id,
                                        int count,
                                        PINT_op_id *op_ids,
                                        void ** user_ptrs,
                                        PVFS_error *errors);

int PINT_open_context(
    PINT_context_id *context_id,
    PINT_completion_callback callback);

int PINT_close_context(PINT_context_id context_id);


int PINT_context_complete(PINT_context_id context_id,
                          PINT_op_id op_id,
                          void * user_ptr,
                          PVFS_error error);

int PINT_context_complete_list(PINT_context_id context_id,
                               int count,
                               PINT_op_id *op_ids,
                               void **user_ptrs,
                               PVFS_error *errors);

int PINT_context_test_all(PINT_context_id context_id,
                          int * count,
                          PINT_op_id *op_ids,
                          void **user_ptrs,
                          PVFS_error * errors,
                          int timeout_ms);

int PINT_context_test_some(PINT_context_id context_id,
                           int count,
                           PINT_op_id *op_ids,
                           void **user_ptrs,
                           PVFS_error *errors,
                           int timeout_ms);

int PINT_context_test(PINT_context_id context_id,
                      PINT_op_id op_id,
                      void **user_ptr,
                      PVFS_error *error,
                      int timeout_ms);

int PINT_context_is_callback(PINT_context_id context_id);

int PINT_context_reference(PINT_context_id context_id);
int PINT_context_dereference(PINT_context_id context_id);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

