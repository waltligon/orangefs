/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_HINT_H
#define __PVFS2_HINT_H

#include "pvfs2-types.h"

/* these are for tracing requests */
#define PVFS_HINT_REQUEST_ID_NAME "pvfs.hint.request_id"
#define PVFS_HINT_CLIENT_ID_NAME  "pvfs.hint.client_id"
#define PVFS_HINT_HANDLE_NAME     "pvfs.hint.handle"
#define PVFS_HINT_OP_ID_NAME      "pvfs.hint.op_id"
#define PVFS_HINT_RANK_NAME       "pvfs.hint.rank"
#define PVFS_HINT_SERVER_ID_NAME  "pvfs.hint.server_id"
/* these are file creation parameters */
#define PVFS_HINT_DISTRIBUTION_NAME    "pvfs.hint.distribution"
#define PVFS_HINT_DFILE_COUNT_NAME     "pvfs.hint.dfile_count"
#define PVFS_HINT_LAYOUT_NAME          "pvfs.hint.layout"
#define PVFS_HINT_SERVERLIST_NAME      "pvfs.hint.serverlist"
#define PVFS_HINT_CACHE_NAME           "pvfs.hint.cache"
#define PVFS_HINT_DISTRIBUTION_PV_NAME "pvfs.hint.distribution.pv"
/* local uid hint for client capcache */
#define PVFS_HINT_LOCAL_UID_NAME     "pvfs.hint.local_uid"
/* owner gid for file creation */
#define PVFS_HINT_OWNER_GID_NAME     "pvfs.hint.owner_gid"

typedef struct PVFS_hint_s *PVFS_hint;

#define PVFS_HINT_NULL NULL

int PVFS_hint_add(PVFS_hint *hint,
                  const char *name,
                  int length,
                  void *value);

int PVFS_hint_replace(PVFS_hint *hint,
                      const char *name,
                      int length,
                      void *value);

int PVFS_hint_copy(PVFS_hint old_hint, PVFS_hint *new_hint);

void PVFS_hint_free(PVFS_hint hint);

/* check to see if a hint has already been added */
int PVFS_hint_check(PVFS_hint *hints, const char *name);

/* check to see if a hint should be transferred to the server */
int PVFS_hint_check_transfer(PVFS_hint *hints);

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
