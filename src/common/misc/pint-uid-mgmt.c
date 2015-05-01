
/* 
 * (C) 2013 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 *
 */

#include "pvfs2-internal.h"
#include "pint-uid-mgmt.h"
#include "pint-util.h"
#include "gen-locks.h"

static list_head_t *uid_lru_list = NULL;
static hash_table_t *uid_hash_table = NULL;

static gen_mutex_t uid_mgmt_mutex = GEN_MUTEX_INITIALIZER;

static int uid_hash_compare_keys(const void* key, list_head_t *link);

/* PINT_uid_mgmt_initialize()
 *
 * Allocate memory for the uid management interface. A linked list is
 * used to implement lru eviction, and a hash table is used to locate
 * existing uid entries quickly.
 */
int PINT_uid_mgmt_initialize()
{
    list_head_t *list;
    hash_table_t *hash_tbl;
    PINT_uid_mgmt_s *tmp, *rover;
    int i;
    int ret = 0;

    /* free any already existing hash table and linked list */
    if (uid_lru_list)
    {
        qlist_for_each_entry_safe(rover, tmp, uid_lru_list, lru_link)
        {
            free(rover);
        }
        free(uid_lru_list);
        uid_lru_list = NULL;
    }

    if (uid_hash_table)
    {
        qhash_finalize(uid_hash_table);
        uid_hash_table = NULL;
    }

    /* initialize the linked list and the hash table */
    list = (list_head_t *)malloc(sizeof(list_head_t));
    if (!list)
    {
        ret = -PVFS_ENOMEM;
        return ret;
    }
    INIT_QLIST_HEAD(list);

    hash_tbl = qhash_init(uid_hash_compare_keys, quickhash_32bit_hash, UID_HISTORY_HASH_TABLE_SIZE);
    if (!hash_tbl)
    {
        ret = -PVFS_ENOMEM;
        return ret;
    }

    /* zero out the fields of uid structure, so they are not "occupied" */
    for (i = 0; i < UID_MGMT_MAX_HISTORY; i++)
    {
        tmp = (PINT_uid_mgmt_s *)malloc(sizeof(PINT_uid_mgmt_s));
        if (!tmp)
        {
            ret = -PVFS_ENOMEM;
            return ret;
        }
        tmp->info.count = 0;
        tmp->info.uid = 0;
        qlist_add_tail(&(tmp->lru_link), list);
    }

    uid_lru_list = list;
    uid_hash_table = hash_tbl;

    return 0;
}

/* PINT_uid_mgmt_finalize()
 *
 * Free all memory associated with the uid managment interface.
 */
void PINT_uid_mgmt_finalize()
{
    PINT_uid_mgmt_s *rover, *tmp;

    if (uid_lru_list)
    {
        qlist_for_each_entry_safe(rover, tmp, uid_lru_list, lru_link)
        {
            free(rover);
        }
        free(uid_lru_list);
        uid_lru_list = NULL;
    }

    if (uid_hash_table)
    {
        qhash_finalize(uid_hash_table);
        uid_hash_table = NULL;
    }

    return;
}

/* PINT_add_user_to_uid_mgmt()
 *
 * This function is called to add new PVFS_uid's to the uid management
 * interface. LRU eviction is used to keep list "recent"
 */
int PINT_add_user_to_uid_mgmt(PVFS_uid userID)
{
    list_head_t *found = NULL;
    PINT_uid_mgmt_s *tmp = NULL;
    int ret = 0;

    if ((!uid_hash_table) || (!uid_lru_list))
    {
        ret = -PVFS_ENODATA;
        return ret;
    }

    /* search the hash table for our uid */
    found = qhash_search(uid_hash_table, &userID);
    if (found)
    {
        tmp = qlist_entry(found, PINT_uid_mgmt_s, hash_link);
        tmp->info.count++;
        PINT_util_get_current_timeval(&(tmp->info.tv));
    }
    else
    {
        /* evict a node from the tail of the list and add new uid */
        tmp = qlist_entry(uid_lru_list->prev, PINT_uid_mgmt_s, lru_link);
        if (tmp->info.count)
        {
            /* make sure to remove this entry from the hash table if
               the count variable has already been defined (not 0) */
            qhash_search_and_remove(uid_hash_table, &(tmp->info.uid));
        }
        tmp->info.count = 1;
        tmp->info.uid = userID;
        PINT_util_get_current_timeval(&(tmp->info.tv0));
        tmp->info.tv = tmp->info.tv0;
        qhash_add(uid_hash_table, &(tmp->info.uid), &(tmp->hash_link));
    }

    /* splice the linked list around our tmp node, then move this
       tmp node to the head of the lru eviction list */
    tmp->lru_link.prev->next = tmp->lru_link.next;
    tmp->lru_link.next->prev = tmp->lru_link.prev;
    qlist_add(&(tmp->lru_link), uid_lru_list);

    return 0;
}

/* uid_hash_compare_keys()
 *
 * Compare will return true if hash entry has same uid as a given key.
 */
static int uid_hash_compare_keys(const void* key, list_head_t *link)
{
    PVFS_uid uid = *(const PVFS_uid *)key;
    PINT_uid_mgmt_s *tmp_entry = NULL;

    tmp_entry = qhash_entry(link, PINT_uid_mgmt_s, hash_link);

    if (uid == tmp_entry->info.uid)
    {
        return 1;
    }
    return 0;
}

/* PINT_dump_all_uid_stats()
 *
 * This function gathers all uid statistics (even inactive structures)
 * and stores them in the array that is passed in.
 */
void PINT_dump_all_uid_stats(PVFS_uid_info_s *uid_array)
{
    int i = 0;
    list_head_t *rover = uid_lru_list->next;
    PINT_uid_mgmt_s *tmp;

    gen_mutex_lock(&uid_mgmt_mutex);

    /* now that we have acquired the lock for the list, fill in our array
     * with the uid statistics
     */
    for (i = 0; i < UID_MGMT_MAX_HISTORY; i++, rover = rover->next)
    {
        tmp = qlist_entry(rover, PINT_uid_mgmt_s, lru_link);
        uid_array[i] = tmp->info;
    }
    gen_mutex_unlock(&uid_mgmt_mutex);

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
