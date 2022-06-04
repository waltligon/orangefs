/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */
   
/*
 * User cache functions - to speed credential lookup, the 
 * OrangeFS credential (UID/groups) are cached with the username.
 * Cache entries from certificates expire when the certificate 
 * expires.
 */

#include <Windows.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/asn1.h>

#include "pvfs2-types.h"
#include "gen-locks.h"
#include "security-util.h"

#include "client-service.h"
#include "user-cache.h"
#include "cred.h"

/* amount of time cache mgmt thread sleeps (ms) */
#define USER_THREAD_SLEEP_TIME    60000

extern PORANGEFS_OPTIONS goptions;

struct qhash_table *user_cache;

gen_mutex_t user_cache_mutex;

int user_compare(const void *key, 
                 struct qhash_head *link)
{
    struct user_entry *entry = qhash_entry(link, struct user_entry, hash_link);

    return !stricmp((char *) key, entry->user_name);
}

int add_cache_user(char *user_name, 
             PVFS_credential *credential,
             ASN1_UTCTIME *expires)
{
    struct qhash_head *link;
    struct user_entry *entry;

    /* search for existing entry -- delete if found */
    gen_mutex_lock(&user_cache_mutex);
    link = qhash_search(user_cache, user_name);
    if (link != NULL)
    {        
        client_debug("   add_cache_user: deleting user %s\n", user_name);
        qhash_del(link);
        free(qhash_entry(link, struct user_entry, hash_link));
    }
    gen_mutex_unlock(&user_cache_mutex);

    /* allocate entry */
    entry = (struct user_entry *) calloc(1, sizeof(struct user_entry));
    if (entry == NULL)
    {
        client_debug("   add_cache_user: out of memory\n");
        return -1;
    }
            
    /* add to hash table */
    strncpy(entry->user_name, user_name, 256);
    PINT_copy_credential(credential, &(entry->credential));

    /* set timeout of cache entry no greater than credential timeout */
    entry->expires = expires;
    if (entry->expires != NULL && 
        ASN1_UTCTIME_cmp_time_t(entry->expires, credential->timeout) == 1)
    {
        ASN1_UTCTIME_free(entry->expires);
        entry->expires = ASN1_UTCTIME_new();
        if (entry->expires == NULL)
        {
            client_debug("   add_cache_user: out of memory\n");
            return -1;
        }
        client_debug("   add_cache_user: setting timeout to %u\n", credential->timeout);
        ASN1_UTCTIME_set(entry->expires, credential->timeout);
    }
    
    gen_mutex_lock(&user_cache_mutex);
    qhash_add(user_cache, &entry->user_name, &entry->hash_link);
    if (goptions->user_mode != USER_MODE_SERVER)
    {
        client_debug("   add_cache_user: adding user %s (%u:%u) (expires %s)\n", 
        user_name, credential->userid, credential->group_array[0], 
        entry->expires != NULL ? entry->expires->data : "never");
    }
    else
    {
        client_debug("   add_cache_user: adding user %s (expires %s)\n", 
        user_name, entry->expires != NULL ? entry->expires->data : "never");
    }
    gen_mutex_unlock(&user_cache_mutex);

    return 0;
}

int get_cache_user(char *user_name, 
                    PVFS_credential *credential)
{
    struct qhash_head *link;
    struct user_entry *entry;

    /* locate user by user_name */
    gen_mutex_lock(&user_cache_mutex);
    link = qhash_search(user_cache, user_name);
    if (link != NULL)
    {
        /* if cache hit -- return credential */
        entry = qhash_entry(link, struct user_entry, hash_link);
        PINT_copy_credential(&(entry->credential), credential);
        if (goptions->user_mode != USER_MODE_SERVER)
        {
            client_debug("   get_cache_user: hit for %s (%u:%u)\n", user_name,
                credential->userid, credential->group_array[0]);
        }
        else
        {
            client_debug("   get_cache_user: hit for %s\n", user_name);
        }

        gen_mutex_unlock(&user_cache_mutex);

        return 0;
    }

    gen_mutex_unlock(&user_cache_mutex);

    /* cache miss */
    return 1;
}

/* remove user entry -- note user_cache_mutex 
   should be locked */
/* *** not currently needed
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
*/

unsigned int user_cache_thread(void *options)
{
    int i;
    struct qhash_head *head;
    struct user_entry *entry;
    time_t now;

    /* remove expired user entries from user cache */
    do
    {        
        Sleep(USER_THREAD_SLEEP_TIME);

        client_debug("user_cache_thread: checking\n");

        now = time(NULL);

        gen_mutex_lock(&user_cache_mutex);
        for (i = 0; i < user_cache->table_size; i++)
        {
            head = qhash_search_at_index(user_cache, i);
            if (head != NULL)
            {    
                entry = qhash_entry(head, struct user_entry, hash_link);
                if (entry->expires != NULL && 
                    ASN1_UTCTIME_cmp_time_t(entry->expires, now) == -1)
                {   
                    client_debug("user_cache_thread: removing %s\n", entry->user_name);
                    qhash_del(head);
                    PINT_cleanup_credential(&(entry->credential));
                    ASN1_UTCTIME_free(entry->expires);
                    free(entry);
                }
            }
        }
        gen_mutex_unlock(&user_cache_mutex);

        client_debug("user_cache_thread: check complete\n");

    } while (1);

    return 0;
}
