/* Copyright (C) 2011 Omnibond, LLC
   User cache functions */

#include <Windows.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "client-service.h"
#include "user-cache.h"

struct qhash_table *user_cache;

int user_compare(void *key, 
                 struct qhash_head *link)
{
    struct user_entry *entry = qhash_entry(link, struct user_entry, hash_link);

    return !stricmp((char *) key, entry->user_name);
}

int add_user(char *user_name, 
             PVFS_credentials *credentials,
             time_t expires)
{
    struct user_entry *entry;

    /* allocate entry */
    entry = (struct user_entry *) calloc(1, sizeof(struct user_entry));
    if (entry == NULL)
    {
        DbgPrint("   add_credentials: out of memory\n");
        return;
    }
            
    /* add to hash table */
    strncpy(entry->user_name, user_name, 256);
    entry->credentials.uid = credentials->uid;
    entry->credentials.gid = credentials->gid;
    entry->expires = expires;
    qhash_add(user_cache, &entry->user_name, &entry->hash_link);

    return 0;
}

int get_cached_user(char *user_name, 
                    PVFS_credentials *credentials)
{
    qhash_head *item;
    struct user_entry *entry;

    /* locate user by user_name */
    item = qhash_search(user_cache, user_name);
    if (item != NULL)
    {
        /* if cache hit -- return credentials */
        entry = qhash_entry(item, struct user_entry, hash_link);
        credentials->uid = entry->credentials.uid;
        credentials->gid = entry->credentials.gid;
        return 0;
    }

    /* cache miss */
    return 1;
}

int remove_user(char *user_name)
{
    struct qhash_head *link; 
    
    link = qhash_search_and_remove(user_cache, user_name);
    if (link != NULL)
    {
        free(qhash_entry(link, struct user_entry, hash_link));
    }

    return 0;
}

unsigned int user_cache_thread(void *options)
{
    return 0;
}
