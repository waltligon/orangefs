/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "gossip.h"

/* dbpf-collection.c
 *
 * This file implements functions designed to maintain a list of
 * collections that have been looked up.
 *
 * dbpf_collection_register() - add a collection to this list
 * dbpf_collection_find_registered() - look for collection in the
 *   list, using the collection id
 */
static struct dbpf_collection *root_coll_p = NULL;

void dbpf_collection_register(struct dbpf_collection *coll_p)
{
    coll_p->next_p = NULL;

    if (root_coll_p == NULL)
    {
	root_coll_p = coll_p;
    }
    else
    {
	struct dbpf_collection *ptr = root_coll_p;

	while(ptr->next_p != NULL)
        {
            ptr = ptr->next_p;
        }
	ptr->next_p = coll_p;
    }
    return;
}

struct dbpf_collection *dbpf_collection_find_registered(
    TROVE_coll_id coll_id)
{
    struct dbpf_collection *ptr = root_coll_p;

    /* look through the list; either end at NULL or a match */
    while((ptr != NULL) && (ptr->coll_id != coll_id))
    {
        ptr = ptr->next_p;
    }
    return ptr;
}

void dbpf_collection_clear_registered(void)
{
    int ret = -TROVE_EINVAL;
    struct dbpf_collection *ptr = root_coll_p, *free_ptr = NULL;

    while (ptr != NULL)
    {
	free_ptr = ptr;
	ptr = ptr->next_p;

        if ((ret = free_ptr->coll_attr_db->sync(
                 free_ptr->coll_attr_db, 0)) != 0)
        {
            return;
        }

        if ((ret = free_ptr->coll_attr_db->close(
                 free_ptr->coll_attr_db, 0)) != 0)
        {
            gossip_lerr("dbpf_finalize: %s\n", db_strerror(ret));
        }

        if ((ret = free_ptr->ds_db->sync(free_ptr->ds_db, 0)) != 0)
        {
            return;
        }

        if ((ret = free_ptr->ds_db->close(free_ptr->ds_db, 0)) != 0)
        {
            gossip_lerr("dbpf_finalize: %s\n", db_strerror(ret));
        }

	free(free_ptr->name);
	free(free_ptr);
    }
    root_coll_p = NULL;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
