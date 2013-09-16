/*
 * (C) 2010-2013 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/*
 * IO cache functions
 */

#include "gen-locks.h"

#include "client-service.h"
#include "io-cache.h"

extern PORANGEFS_OPTIONS goptions;

struct qhash_table *io_cache;

gen_mutex_t io_cache_mutex;

int io_cache_compare(void *key,
                     struct qhash_head *link)
{
    struct io_cache_entry *entry = qhash_entry(link, 
        struct io_cache_entry, hash_link);

    return *((ULONG64 *) key) == entry->context;
}

int io_cache_add(ULONG64 context, 
                 PVFS_object_ref *object_ref,
                 PVFS_Request req)
{
    struct qhash_head *link;
    struct io_cache_entry *entry;

    /* look for existing context -- exit if found */
    gen_mutex_lock(&io_cache_mutex);
    link = qhash_search(io_cache, &context);
    gen_mutex_unlock(&io_cache_mutex);
    if (link != NULL)
    {
        DbgPrint("   io_cache_add: context %llx already exists\n", context);
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
    entry->req = req;

    /* add entry */
    gen_mutex_lock(&io_cache_mutex);
    qhash_add(io_cache, &context, &entry->hash_link);
    gen_mutex_unlock(&io_cache_mutex);

    DbgPrint("   io_cache_add: added context %llx handle %llu request %p\n",
        entry->context, entry->object_ref.handle, entry->req);

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
        PVFS_Request_free(&entry->req);
        free(entry);
    }

    return 0;
}

int io_cache_get(ULONG64 context, 
                 PVFS_object_ref *object_ref, 
                 PVFS_Request *req)
{
    struct qhash_head *link;
    struct io_cache_entry *entry;

    if (object_ref == NULL || req == NULL)
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
        *req = entry->req;

        return IO_CACHE_HIT;
    }

    return IO_CACHE_MISS;
}
