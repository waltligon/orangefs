/* 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include<quicklist.h>

struct qhash_table
{
	struct qlist_head* array;
	int table_size;
	int(*compare)(void* key, struct qlist_head* link);
	int(*hash)(void* key, int table_size);
};

struct qhash_table* qhash_init(
	int(*compare)(void* key, struct qlist_head* link), 
	int(*hash)(void* key, int table_size),
	int table_size);

void qhash_finalize(
	struct qhash_table* old_table);

struct qlist_head* qhash_search(
	struct qhash_table* table,
	void* key);

void qhash_add(
	struct qhash_table* table,
	void* key,
	struct qlist_head* link);
