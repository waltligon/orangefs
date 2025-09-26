/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/*
 * IO cache functions
 */

#include "gen-locks.h"
#include "gossip.h"

#include "client-service.h"
#include "io-cache.h"

extern PORANGEFS_OPTIONS goptions;

struct qhash_table *io_cache;

gen_mutex_t io_cache_mutex;

int io_cache_compare(const void *key,
                     struct qhash_head *link)
{
    struct io_cache_entry *entry = qhash_entry(link, 
        struct io_cache_entry, hash_link);

    return *((ULONG64 *) key) == entry->context;
}

int io_cache_add(ULONG64 context, 
                 PVFS_object_ref *object_ref,
                 enum PVFS_io_type io_type,
                 int update_flag)

{
    struct qhash_head *link;
    struct io_cache_entry *entry;

    if (object_ref == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* look for existing context -- exit if found */
    gen_mutex_lock(&io_cache_mutex);
    link = qhash_search(io_cache, &context);
    gen_mutex_unlock(&io_cache_mutex);
    if (link != NULL)
    {
        entry = qhash_entry(link, struct io_cache_entry, hash_link);
        if (io_type != entry->io_type)
        {
            gossip_debug(GOSSIP_IO_DEBUG, "io_cache_add: updating io_type to %s for context %llx\n",
                (io_type == PVFS_IO_READ) ? "READ" : "WRITE", context);
            entry->io_type = io_type;
        }
        else
        {
            gossip_debug(GOSSIP_IO_DEBUG, "io_cache_add: context %llx already exists\n", context);
        }
        return 0;
    }

    /* allocate entry */
    entry = (struct io_cache_entry *) malloc(sizeof(struct io_cache_entry));
    if (entry == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* copy fields */
    entry->context = context;
    memcpy(&entry->object_ref, object_ref, sizeof(PVFS_object_ref));
    entry->io_type = io_type;
    entry->update_flag = update_flag;

    /* add entry */
    gen_mutex_lock(&io_cache_mutex);
    qhash_add(io_cache, &context, &entry->hash_link);
    gen_mutex_unlock(&io_cache_mutex);

    gossip_debug(GOSSIP_IO_DEBUG, "io_cache_add: added context %llx handle %llu\n",
        entry->context, entry->object_ref.handle);

    return 0;
}

int io_cache_remove(ULONG64 context)
{
    struct qhash_head *link; 
    struct io_cache_entry *entry;
    
    gen_mutex_lock(&io_cache_mutex);
    link = qhash_search_and_remove(io_cache, &context);
    gen_mutex_unlock(&io_cache_mutex);
    
    if (link != NULL)
    {
        entry = qhash_entry(link, struct io_cache_entry, hash_link);
        gossip_debug(GOSSIP_IO_DEBUG, "io_cache_remove: removed context %llx\n",
            entry->context);
        free(entry);
    }

    return 0;
}

int io_cache_get(ULONG64 context, 
                 PVFS_object_ref *object_ref, 
                 enum PVFS_io_type *io_type,
                 int *update_flag)
{
    struct qhash_head *link;
    struct io_cache_entry *entry;

    if (object_ref == NULL || io_type == NULL || update_flag == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* look for existing context -- exit if found */
    gen_mutex_lock(&io_cache_mutex);
    link = qhash_search(io_cache, &context);
    gen_mutex_unlock(&io_cache_mutex);
    if (link != NULL)
    {
        entry = qhash_entry(link, struct io_cache_entry, hash_link);
        memcpy(object_ref, &entry->object_ref, sizeof(PVFS_object_ref));
        *io_type = entry->io_type;
        *update_flag = entry->update_flag;

        gossip_debug(GOSSIP_IO_DEBUG, "io_cache_get: got context %llx "
            "handle %llu\n", context, object_ref->handle);

        return IO_CACHE_HIT;
    }

    gossip_debug(GOSSIP_IO_DEBUG, "io_cache_get: miss for context %llx\n",
        context);

    return IO_CACHE_MISS;
}
