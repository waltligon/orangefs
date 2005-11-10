/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>
#include "gossip.h"
#include "dbpf-attr-cache.h"
#include "gen-locks.h"
#include "str-utils.h"

/* public mutex lock */
gen_mutex_t dbpf_attr_cache_mutex = GEN_MUTEX_INITIALIZER;

/* these are based on code from src/server/request-scheduler.c */
static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);

static int s_cache_size = DBPF_ATTR_CACHE_DEFAULT_SIZE;
static int s_max_num_cache_elems = 
DBPF_ATTR_CACHE_DEFAULT_MAX_NUM_CACHE_ELEMS;
static struct qhash_table *s_key_to_attr_table = NULL;
static char **s_cacheable_keyword_array = NULL;
static int s_cacheable_keyword_array_size = 0;
static int s_current_num_cache_elems = 0;

#define DBPF_ATTR_CACHE_INITIALIZED() \
(s_key_to_attr_table)

int dbpf_attr_cache_set_keywords(char *keywords)
{
    assert(keywords);
    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "Setting dbpf_attr_cache "
                 "keywords to:\n%s\n", keywords);

    s_cacheable_keyword_array_size = PINT_split_string_list(
        &s_cacheable_keyword_array, keywords);
    return (s_cacheable_keyword_array ? 0 : -1);
}

int dbpf_attr_cache_set_size(int cache_size)
{
    s_cache_size = cache_size;
    return 0;
}

int dbpf_attr_cache_set_max_num_elems(int max_num_elems)
{
    s_max_num_cache_elems = max_num_elems;
    return 0;
}

/*
  the idea is that the other parameters are filled in
  by setinfo calls so that by the time this is called,
  we have all the info we need.  use defaults if none
  are specified
*/
int dbpf_attr_cache_do_initialize(void)
{
    int ret = -1;
    gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "There are %d cacheable "
                 "keywords registered\n", s_cacheable_keyword_array_size);
    ret = dbpf_attr_cache_initialize(
        s_cache_size, s_max_num_cache_elems,
        s_cacheable_keyword_array, s_cacheable_keyword_array_size);

    return ret;
}

int dbpf_attr_cache_initialize(
    int table_size,
    int cache_max_num_elems,
    char **cacheable_keywords,
    int num_cacheable_keywords)
{
    int ret = -1, i = 0;

    if (s_key_to_attr_table == NULL)
    {
        if (cacheable_keywords)
        {
            if ((num_cacheable_keywords < 0) ||
                (num_cacheable_keywords >
                 DBPF_ATTR_CACHE_MAX_NUM_KEYVALS))
            {
                gossip_err("****************************************\n");
                gossip_err(
                    "Warning: There are too many specified cacheable "
                    "keywords.\nEither reduce this in your "
                    "configuration settings, or adjust the value\n"
                    "DBPF_ATTR_CACHE_MAX_NUM_KEYVALS in "
                    "the file dbpf-attr-cache.h and recompile.\n");
                gossip_err(
                    "No specified Attribute Keywords will be cached\n");
                gossip_err("****************************************\n");
                goto return_error;
            }

            /*
              NOTE: our keyword array must have already
              been built by the do_initialize call.
              we make sure it's ok here.
            */
            for(i = 0; i < s_cacheable_keyword_array_size; i++)
            {
                assert(s_cacheable_keyword_array[i]);
            }
        }

        s_current_num_cache_elems = 0;
        s_max_num_cache_elems = cache_max_num_elems;
        s_key_to_attr_table = qhash_init(
            hash_key_compare, hash_key, table_size);
        if (!s_key_to_attr_table)
        {
            goto return_error;
        }

        srand((unsigned int)time(NULL));

        gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                     "dbpf_attr_cache_initialize: initialized\n");
        ret = 0;
    }
    else
    {
        gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                     "dbpf_attr_cache_initialize: already initialized\n");
        ret = 0;
    }

  return_error:
    return ret;
}

int dbpf_attr_cache_finalize(void)
{
    int ret = -1, i = 0;
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    if (DBPF_ATTR_CACHE_INITIALIZED())
    {
        for(i = 0; i < s_key_to_attr_table->table_size; i++)
        {
            do
            {
                hash_link = qhash_search_and_remove_at_index(
                    s_key_to_attr_table,i);
                if (hash_link)
                {
                    cache_elem = qhash_entry(
                        hash_link, dbpf_attr_cache_elem_t, hash_link);
                    free(cache_elem);
                    s_current_num_cache_elems--;
                }
            } while(hash_link);
        }

        assert(s_current_num_cache_elems == 0);
        ret = ((i == s_key_to_attr_table->table_size) ? 0 : -1);
        qhash_finalize(s_key_to_attr_table);
        s_key_to_attr_table = NULL;

        gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG,
                     "dbpf_attr_cache_finalized\n");
    }

    if (s_cacheable_keyword_array && s_cacheable_keyword_array_size)
    {
        PINT_free_string_list(s_cacheable_keyword_array,
                              s_cacheable_keyword_array_size);
        s_cacheable_keyword_array = NULL;
        s_cacheable_keyword_array_size = 0;
    }
    return ret;
}

dbpf_attr_cache_elem_t *dbpf_attr_cache_elem_lookup(TROVE_object_ref key)
{
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    if (DBPF_ATTR_CACHE_INITIALIZED())
    {
        hash_link = qhash_search(s_key_to_attr_table,&(key));
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);
            assert(cache_elem);
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG,
                "dbpf_cache_elem_lookup: cache "
                "elem matching %llu returned (num_elems=%d)\n",
                llu(key.handle), s_current_num_cache_elems);
        }
    }
    return cache_elem;
}

int dbpf_attr_cache_ds_attr_update_cached_data(
    TROVE_object_ref key, TROVE_ds_attributes *src_ds_attr)
{
    int ret = -1;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    cache_elem = dbpf_attr_cache_elem_lookup(key);
    if (cache_elem && src_ds_attr)
    {
        if (cache_elem && src_ds_attr)
        {
            memcpy(&cache_elem->attr, src_ds_attr,
                   sizeof(TROVE_ds_attributes));
            gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "Updating "
                         "cached attributes for key %llu\n",
                         llu(key.handle));
            ret = 0;
        }
    }
    return ret;
}

int dbpf_attr_cache_ds_attr_update_cached_data_ksize(
    TROVE_object_ref key, PVFS_size k_size)
{
    int ret = -1;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    cache_elem = dbpf_attr_cache_elem_lookup(key);
    if (cache_elem)
    {
        if (cache_elem)
        {
            cache_elem->attr.k_size = k_size;
            gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "Updating "
                         "cached k_size for key %llu\n",
                         llu(key.handle));
            ret = 0;
        }
    }
    return ret;
}

int dbpf_attr_cache_ds_attr_update_cached_data_bsize(
    TROVE_object_ref key, PVFS_size b_size)
{
    int ret = -1;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    cache_elem = dbpf_attr_cache_elem_lookup(key);
    if (cache_elem)
    {
        if (cache_elem)
        {
            cache_elem->attr.b_size = b_size;
            gossip_debug(GOSSIP_DBPF_ATTRCACHE_DEBUG, "Updating "
                         "cached b_size for key %llu\n",
                         llu(key.handle));
            ret = 0;
        }
    }
    return ret;
}

int dbpf_attr_cache_ds_attr_fetch_cached_data(
    TROVE_object_ref key, TROVE_ds_attributes *target_ds_attr)
{
    int ret = -1;
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    if (DBPF_ATTR_CACHE_INITIALIZED() && target_ds_attr)
    {
        hash_link = qhash_search(s_key_to_attr_table,&(key));
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);
            assert(cache_elem);
            memcpy(target_ds_attr, &cache_elem->attr,
                   sizeof(TROVE_ds_attributes));
            ret = 0;
        }
    }
    return ret;
}

dbpf_keyval_pair_cache_elem_t *dbpf_attr_cache_elem_get_data_based_on_key(
    dbpf_attr_cache_elem_t *cache_elem, char *key)
{
    int i = 0;

    if (DBPF_ATTR_CACHE_INITIALIZED() &&
        (cache_elem && key && cache_elem->num_keyval_pairs))
    {
        for(i = 0; i < cache_elem->num_keyval_pairs; i++)
        {
            if ((strcmp(cache_elem->keyval_pairs[i].key, key) == 0) &&
                (cache_elem->keyval_pairs[i].data != NULL))
            {
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG, "Returning data %p "
                    "based on key %llu and key_str %s (data_sz=%d)\n",
                    cache_elem->keyval_pairs[i].data,
                    llu(cache_elem->key.handle), key,
                    cache_elem->keyval_pairs[i].data_sz);
                return &cache_elem->keyval_pairs[i];
            }
        }
    }
    return NULL;
}

int dbpf_attr_cache_elem_set_data_based_on_key(
    TROVE_object_ref key, char *key_str, void *data, int data_sz)
{
    int ret = - 1, i = 0;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    cache_elem = dbpf_attr_cache_elem_lookup(key);
    if (cache_elem && key_str && cache_elem->num_keyval_pairs)
    {
        if (!cache_elem || !cache_elem->num_keyval_pairs)
        {
            return ret;
        }
        for(i = 0; i < cache_elem->num_keyval_pairs; i++)
        {
            if (strcmp(cache_elem->keyval_pairs[i].key, key_str) == 0)
            {
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,
                    "Setting data %p based on key "
                    "%llu and key_str %s (data_sz=%d)\n", data,
                    llu(key.handle), key_str, data_sz);

                if (cache_elem->keyval_pairs[i].data)
                {
                    free(cache_elem->keyval_pairs[i].data);
                }
                cache_elem->keyval_pairs[i].data = malloc(data_sz);
                assert(cache_elem->keyval_pairs[i].data);
                memcpy(cache_elem->keyval_pairs[i].data, data, data_sz);
                cache_elem->keyval_pairs[i].data_sz = data_sz;
                ret = 0;
                break;
            }
        }
    }
    return ret;
}

int dbpf_attr_cache_keyval_pair_fetch_cached_data(
    dbpf_attr_cache_elem_t *cache_elem,
    dbpf_keyval_pair_cache_elem_t *keyval_pair,
    void *target_data, int *target_data_sz)
{
    int ret = -1;

    if (DBPF_ATTR_CACHE_INITIALIZED() &&
        (keyval_pair && target_data && target_data_sz))
    {
        if (cache_elem && keyval_pair)
        {
            memcpy(target_data, keyval_pair->data, keyval_pair->data_sz);
            *target_data_sz = keyval_pair->data_sz;
            ret = 0;
        }
    }
    return ret;
}

int dbpf_attr_cache_insert(
    TROVE_object_ref key,
    TROVE_ds_attributes *attr)
{
    int ret = -1, i = 0, already_exists = 0;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    struct qlist_head *hash_link = NULL;

    if (DBPF_ATTR_CACHE_INITIALIZED())
    {
        if ((s_current_num_cache_elems + 1) > s_max_num_cache_elems)
        {
            TROVE_object_ref sacrificial_lamb_key =
                {TROVE_HANDLE_NULL, TROVE_COLL_ID_NULL};
            /*
              we have the lock, so we can safely remove
              any element in this cache at this point;
              since our hashtable isn't sorted, and a hit/age
              count would be too costly in a linear search
              here, just remove the first element at an arbitrary
              hashed index that we can find in the hash table
            */
            i = (rand() % s_key_to_attr_table->table_size);
          hashtable_linear_scan:
            for(; i < s_key_to_attr_table->table_size; i++)
            {
                hash_link =
                    qhash_search_at_index(s_key_to_attr_table,i);
                if (hash_link)
                {
                    cache_elem = qhash_entry(
                        hash_link, dbpf_attr_cache_elem_t, hash_link);
                    sacrificial_lamb_key = cache_elem->key;
                    break;
                }
            }

            /*
              handle a special case where the random index
              yields a completely empty linked list; start again
              at 0, as *something* must be in the hash table
            */
            if ((i == s_key_to_attr_table->table_size) &&
                (sacrificial_lamb_key.handle == TROVE_HANDLE_NULL))
            {
                i = 0;
                goto hashtable_linear_scan;
            }
            assert(sacrificial_lamb_key.handle != TROVE_HANDLE_NULL);
            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG, "*** Cache is full -- "
                "removing key %llu to insert key %llu\n",
                llu(sacrificial_lamb_key.handle), llu(key.handle));
            dbpf_attr_cache_remove(sacrificial_lamb_key);
        }

        hash_link = qhash_search(s_key_to_attr_table,&(key));
        if (!hash_link)
        {
            cache_elem = (dbpf_attr_cache_elem_t *)
                malloc(sizeof(dbpf_attr_cache_elem_t));
        }
        else
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);
            already_exists = 1;
        }

        if (cache_elem)
        {
            if (!already_exists)
            {
                memset(cache_elem, 0, sizeof(dbpf_attr_cache_elem_t));
            }

            if (s_cacheable_keyword_array)
            {
                /* initialize all of the keyvals we're able to cache */
                for(i = 0; i < s_cacheable_keyword_array_size; i++)
                {
                    cache_elem->keyval_pairs[i].key =
                        s_cacheable_keyword_array[i];
                    cache_elem->keyval_pairs[i].data = NULL;
                }
                cache_elem->num_keyval_pairs =
                    s_cacheable_keyword_array_size;
            }

            cache_elem->key = key;
            memcpy(&(cache_elem->attr), attr,
                   sizeof(TROVE_ds_attributes));
            if (!already_exists)
            {
                qhash_add(
                    s_key_to_attr_table,&(key),&(cache_elem->hash_link));
                s_current_num_cache_elems++;
                gossip_debug(
                    GOSSIP_DBPF_ATTRCACHE_DEBUG,
                    "dbpf_attr_cache_insert: inserting %llu "
                    "(k_size is %llu | b_size is %llu)\n", llu(key.handle),
                    llu(cache_elem->attr.k_size),
                    llu(cache_elem->attr.b_size));
            }
            ret = 0;
        }
    }
    return ret;
}

int dbpf_attr_cache_remove(TROVE_object_ref key)
{
    int ret = -1, i = 0;
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    if (DBPF_ATTR_CACHE_INITIALIZED())
    {
        hash_link = qhash_search_and_remove(s_key_to_attr_table,&(key));
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);
            assert(cache_elem);

            gossip_debug(
                GOSSIP_DBPF_ATTRCACHE_DEBUG, "dbpf_attr_cache_remove: "
                "removing %llu\n", llu(key.handle));

            /* free any keyval data cached as well */
            if (s_cacheable_keyword_array)
            {
                /* free all of the keyvals we've cached */
                assert(s_cacheable_keyword_array_size ==
                       cache_elem->num_keyval_pairs);
                for(i = 0; i < cache_elem->num_keyval_pairs; i++)
                {
                    cache_elem->keyval_pairs[i].key = NULL;
                    if (cache_elem->keyval_pairs[i].data)
                    {
                        free(cache_elem->keyval_pairs[i].data);
                        cache_elem->keyval_pairs[i].data = NULL;
                    }
                }
            }
            free(cache_elem);
            s_current_num_cache_elems--;
            ret = 0;
        }
    }
    return ret;
}

/* hash_key()
 *
 * hash function for fsids added to table
 *
 * returns integer offset into table
 */
static int hash_key(void *key, int table_size)
{
    unsigned long tmp = 0;
    TROVE_object_ref *ref = (TROVE_object_ref *)key;

    tmp = (ref->fs_id << 12);
    tmp += ref->handle;
    tmp = (tmp % table_size);

    return ((int)tmp);
}

/* hash_key_compare()
 *
 * performs a comparison of a hash table entry to a given key
 * (used for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_key_compare(void *key, struct qlist_head *link)
{
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_object_ref *ref = (TROVE_object_ref *)key;

    cache_elem = qlist_entry(link, dbpf_attr_cache_elem_t, hash_link);
    assert(cache_elem);

    if ((cache_elem->key.handle == ref->handle) &&
        (cache_elem->key.fs_id == ref->fs_id))
    {
        return(1);
    }
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
