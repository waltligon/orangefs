/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _LOCK_STORAGE_H
#define _LOCK_STORAGE_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include "pvfs2-types.h"
#include "pint-request.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-internal.h"
#include "itree.h"
#include "rbtree.h"
#include "quickhash.h"
#include "gen-locks.h"
#include "lock-storage.h"
#include "pint-distribution.h"

enum PVFS_lock_req_status
{
   INCOMPLETE,
   ALL_LOCKS_GRANTED
};

int init_lock_file_table(void);
void free_lock_file_table(void);
void free_lock_file_table_all(void);
void print_lock_file_table_all(void);
void print_lock_file_table_all_info(void);
int add_lock_req(PVFS_object_ref *object_ref_p,
                 enum PVFS_io_type io_type,
                 PINT_Request *file_req,
                 PVFS_offset file_req_offset,
		 PINT_request_file_data *fdata_p,
                 PVFS_size nb_bytes,
                 PVFS_size bb_bytes,
                 PVFS_size aggregate_size,
                 PVFS_id_gen_t *req_id,
		 PVFS_size *granted_bytes_p);
int del_lock_req(PVFS_object_ref *object_ref_p,
                 PVFS_id_gen_t req_id,
                 PVFS_size nb_bytes,
		 PVFS_size *total_bytes_released_p);

#endif

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
