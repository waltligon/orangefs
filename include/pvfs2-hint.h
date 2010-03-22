/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_HINT_H
#define __PVFS2_HINT_H

#include "pvfs2-types.h"

#define PVFS_HINT_REQUEST_ID_NAME "pvfs.hint.request_id"
#define PVFS_HINT_CLIENT_ID_NAME  "pvfs.hint.client_id"
#define PVFS_HINT_HANDLE_NAME     "pvfs.hint.handle"
#define PVFS_HINT_OP_ID_NAME      "pvfs.hint.op_id"
#define PVFS_HINT_RANK_NAME       "pvfs.hint.rank"
#define PVFS_HINT_SERVER_ID_NAME  "pvfs.hint.server_id"

typedef struct PVFS_hint_s *PVFS_hint;

#define PVFS_HINT_NULL NULL

int PVFS_hint_add(
    PVFS_hint *hint,
    const char *type,
    int length,
    void *value);

int PVFS_hint_replace(
    PVFS_hint *hint,
    const char *type,
    int length,
    void *value);

int PVFS_hint_copy(PVFS_hint old_hint, PVFS_hint *new_hint);

void PVFS_hint_free(PVFS_hint hint);

/* check to see if a hint has already been added */
int PVFS_hint_check(PVFS_hint *hints, const char *type);

/*
 * function allows users to specify hints in an environment variable.
 */
int PVFS_hint_import_env(PVFS_hint *out_hint);

#endif /* __PVFS2_HINT_H */

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
