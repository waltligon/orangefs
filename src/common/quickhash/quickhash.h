/* 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef QUICKHASH_H
#define QUICKHASH_H

#ifdef __KERNEL__

#define qhash_malloc(x)            kmalloc(x, GFP_KERNEL)
#define qhash_free(x)              kfree(x)
#define qhash_head                 list_head
#define INIT_QHASH_HEAD            INIT_LIST_HEAD
#define qhash_entry                list_entry
#define qhash_add_tail             list_add_tail
#define qhash_del                  list_del
#define qhash_for_each             list_for_each

#define qhash_lock_init(lock_ptr)  spin_lock_init(lock_ptr)
#define qhash_lock(lock_ptr)       spin_lock(lock_ptr)
#define qhash_unlock(lock_ptr)     spin_unlock(lock_ptr)

#else

#include "quicklist.h"

#define qhash_malloc(x)            malloc(x)
#define qhash_free(x)              free(x)
#define qhash_head                 qlist_head
#define INIT_QHASH_HEAD            INIT_QLIST_HEAD
#define qhash_entry                qlist_entry
#define qhash_add_tail             qlist_add_tail
#define qhash_del                  qlist_del
#define qhash_for_each             qlist_for_each

#define qhash_lock_init(lock_ptr)  do {} while(0)
#define qhash_lock(lock_ptr)       do {} while(0)
#define qhash_unlock(lock_ptr)     do {} while(0)

#endif /* __KERNEL__ */

struct qhash_table
{
	struct qhash_head* array;
	int table_size;
	int(*compare)(void* key, struct qhash_head* link);
	int(*hash)(void* key, int table_size);

#ifdef __KERNEL__
	spinlock_t lock;
#endif
};

struct qhash_table* qhash_init(
	int(*compare)(void* key, struct qhash_head* link), 
	int(*hash)(void* key, int table_size),
	int table_size);

void qhash_finalize(
	struct qhash_table* old_table);

struct qhash_head* qhash_search(
	struct qhash_table* table,
	void* key);

struct qhash_head* qhash_search_and_remove(
	struct qhash_table* table,
	void* key);

void qhash_add(
	struct qhash_table* table,
	void* key,
	struct qhash_head* link);

#endif /* QUICKHASH_H */
