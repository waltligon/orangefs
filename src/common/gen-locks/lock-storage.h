/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _LOCK_STORAGE_H
#define _LOCK_STORAGE_H

#include <unistd.h>
#include <string.h>
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
#include "pvfs2-request.h"

enum PVFS_lock_req_status
{
   INCOMPLETE,
   ALL_LOCKS_GRANTED
};

typedef struct {
   struct qlist_head list_link;
   int lock_id;
   itree_t itree_link;
} linked_itree_t;

typedef struct {
   void (*fn)(void* data);
   void* data;
} lock_callback_t;

typedef struct {
   /* Pointer to the first lock */
   struct qlist_head lock_head;
   
   enum PVFS_lock_req_status lock_req_status;
   /* Numerous links to the same objects */
   rbtree_t granted_req_link;
   struct qlist_head queued_req_link;
   struct qlist_head all_req_link;
   
   PVFS_id_gen_t req_id; /* ID returned to client */
   enum PVFS_io_type io_type; /* read or write operation */
   
   PVFS_size actual_locked_bytes; /* Total actual bytes that are locked. */
   PINT_Request_state *file_req_state; /* Use this for processing */
   PVFS_offset target_offset; /* Where to start the request state */
   PVFS_size aggregate_size; /* Total bytes in the entire req */
   PVFS_size wait_size ; /* Maximum number of bytes waiting on */
   PINT_Request *file_req;
   PVFS_offset file_req_offset; /* Bytes in the file req to skip */

   /* job information for resuming state machine (if needed for wait size) */
   lock_callback_t lock_callback;
 } lock_req_t;

typedef struct {
   itree_t *write_itree; /* head of write lock tree */
   itree_t *read_itree;  /* head of read lock tree */
   
   rbtree_t *granted_req;          /* head of granted lock requests */
   struct qlist_head queued_req; /* head of queue lock requests */
   struct qlist_head all_req;    /* all lock requests */

   PVFS_object_ref refn;        /* object infofrmation */
   PINT_request_file_data fdata; /* distribution information */
   struct qlist_head hash_link; /* location in the hash linked list */
} lock_node_t;

int init_lock_file_table(void);
void free_lock_file_table(void);
void free_lock_file_table_all(void);
void print_lock_file_table_all(void);
void print_lock_file_table_all_info(void);
int check_lock_reqs(lock_node_t *lock_node_p);
int add_lock_req(PVFS_object_ref *object_ref_p, 
		 enum PVFS_io_type io_type, 
		 PINT_Request *file_req,
		 PVFS_offset file_req_offset,
		 PINT_request_file_data *fdata_p,
		 PVFS_size nb_bytes,
		 PVFS_size bb_bytes,
		 PVFS_size aggregate_size,
		 PVFS_id_gen_t *req_id,
		 PVFS_size *granted_bytes_p,
		 lock_req_t **lock_req_p_p);
int revise_lock_req(PVFS_object_ref *object_ref_p,
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
