/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>
#include "gossip.h"
#include "dbpf-attr-cache.h"

/* these are based on code from src/server/request-scheduler.c */
static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);

static int s_cache_size = DBPF_ATTR_CACHE_DEFAULT_SIZE;
static int s_max_num_cache_elems = 
DBPF_ATTR_CACHE_DEFAULT_MAX_NUM_CACHE_ELEMS;
static struct qhash_table *s_key_to_attr_table = NULL;
static char *s_cacheable_keywords = NULL;
static char *s_cacheable_keyword_array[
    DBPF_ATTR_CACHE_MAX_NUM_KEYVALS] = {0};
static int s_cacheable_keyword_array_size = 0;

int dbpf_attr_cache_set_keywords(char *keywords)
{
    if (s_cacheable_keywords)
    {
        free(s_cacheable_keywords);
    }
    s_cacheable_keywords = strdup(keywords);
    return (s_cacheable_keywords ? 0 : -1);
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
    int ret = -1, num_keywords = 0;
    char *ptr = NULL, *start = NULL, *end = NULL;
    char *limit  = NULL, *tmp = NULL;

    if (s_cacheable_keywords)
    {
        /* freed in finalize */
        tmp = strdup(s_cacheable_keywords);
        limit = (char *)(tmp + strlen(tmp));

        /* break up keywords into an array here */
        ptr = start = tmp;
        for(; (ptr && (start != limit)); ptr++)
        {
            if ((*ptr == '\0') || (*ptr == ' ') || (*ptr == ','))
            {
                end = ptr;
            }
            if (start && end)
            {
                *end = '\0';
                s_cacheable_keyword_array[num_keywords++] = start;

                gossip_debug(TROVE_DEBUG, "Got cacheable "
                             "attribute keyword %s\n",start);

                start = ++end;
                if (start >= limit)
                {
                    break;
                }
                end = NULL;
            }
        }
    }

    ret = dbpf_attr_cache_initialize(
        s_cache_size, s_max_num_cache_elems,
        s_cacheable_keyword_array, num_keywords);

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
                goto return_error;
            }

            s_cacheable_keyword_array_size = num_cacheable_keywords;
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

        s_max_num_cache_elems = cache_max_num_elems;
        s_key_to_attr_table = qhash_init(
            hash_key_compare, hash_key, table_size);
        ret = (s_key_to_attr_table ? 0 : -1);
        gossip_debug(TROVE_DEBUG, "dbpf_attr_cache_initialized\n");
    }

  return_error:
    return ret;
}

int dbpf_attr_cache_finalize(void)
{
    int ret = -1, i = 0;
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    if (s_key_to_attr_table)
    {
        for(i = 0; i < s_key_to_attr_table->table_size; i++)
        {
            do
            {
                hash_link =
                    qhash_search_and_remove(s_key_to_attr_table,&(i));
                if (hash_link)
                {
                    cache_elem = qhash_entry(
                        hash_link, dbpf_attr_cache_elem_t, hash_link);
                    free(cache_elem);
                }
            } while(hash_link);
        }
        ret = ((i == s_key_to_attr_table->table_size) ? 0 : -1);
        qhash_finalize(s_key_to_attr_table);
        s_key_to_attr_table = NULL;
        gossip_debug(TROVE_DEBUG, "dbpf_attr_cache_finalized\n");
    }

    if (s_cacheable_keywords)
    {
        free(s_cacheable_keywords);
        s_cacheable_keywords = NULL;

        /* NOTE: this array was allocated as a single string */
        free(s_cacheable_keyword_array);
    }
    return ret;
}

TROVE_ds_attributes *dbpf_attr_cache_lookup(TROVE_handle key)
{
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_ds_attributes *attr = NULL;

    if (s_key_to_attr_table)
    {
        hash_link = qhash_search(s_key_to_attr_table,&(key));
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);
            assert(cache_elem);
            attr = &(cache_elem->attr);
            gossip_debug(TROVE_DEBUG, "dbpf_attr_cache_lookup: cache "
                         "hit on %Lu\n", Lu(key));
        }
    }
    return attr;
}

dbpf_attr_cache_elem_t *dbpf_cache_elem_lookup(TROVE_handle key)
{
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    if (s_key_to_attr_table)
    {
        hash_link = qhash_search(s_key_to_attr_table,&(key));
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);
            assert(cache_elem);
            gossip_debug(TROVE_DEBUG, "dbpf_cache_elem_lookup: cache "
                         "elem matching %Lu returned\n", Lu(key));
        }
    }
    return cache_elem;
}

dbpf_keyval_pair_cache_elem_t *dbpf_cache_elem_get_data_based_on_key(
    dbpf_attr_cache_elem_t *cached_elem, char *key)
{
    int i = 0;

    if (cached_elem && key && cached_elem->num_keyval_pairs)
    {
        for(i = 0; i < cached_elem->num_keyval_pairs; i++)
        {
            if ((strcmp(cached_elem->keyval_pairs[i].key, key) == 0) &&
                (cached_elem->keyval_pairs[i].data != NULL))
            {
                gossip_debug(TROVE_DEBUG, "Returning data %p based on "
                             "key %Lu and key_str %s (data_sz=%d)\n",
                             cached_elem->keyval_pairs[i].data,
                             Lu(cached_elem->key), key,
                             cached_elem->keyval_pairs[i].data_sz);
                return &cached_elem->keyval_pairs[i];
            }
        }
    }
    return NULL;
}

int dbpf_cache_elem_set_data_based_on_key(
    TROVE_handle key, char *key_str, void *data, int data_sz)
{
    int ret = - 1, i = 0;
    dbpf_attr_cache_elem_t *cache_elem = NULL;

    cache_elem = dbpf_cache_elem_lookup(key);
    if (cache_elem && key_str && cache_elem->num_keyval_pairs)
    {
        for(i = 0; i < cache_elem->num_keyval_pairs; i++)
        {
            if (strcmp(cache_elem->keyval_pairs[i].key, key_str) == 0)
            {
                gossip_debug(TROVE_DEBUG, "Setting data %p based on key "
                             "%Lu and key_str %s (data_sz=%d)\n", data,
                             Lu(key), key_str, data_sz);

                if (cache_elem->keyval_pairs[i].data)
                {
                    free(cache_elem->keyval_pairs[i].data);
                }
                cache_elem->keyval_pairs[i].data = (void *)
                    malloc(data_sz);
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

int dbpf_attr_cache_insert(
    TROVE_handle key,
    TROVE_ds_attributes *attr)
{
    int ret = -1, i = 0, already_exists = 0;
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    struct qlist_head *hash_link = NULL;

    /* should enforce cache size boundary here (s_max_num_cache_elems) */

    if (s_key_to_attr_table)
    {
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
            memset(cache_elem, 0, sizeof(dbpf_attr_cache_elem_t));

            if (s_cacheable_keywords)
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
            memcpy(&(cache_elem->attr), attr, sizeof(TROVE_ds_attributes));
            if (!already_exists)
            {
                qhash_add(
                    s_key_to_attr_table,&(key),&(cache_elem->hash_link));
                gossip_debug(
                    TROVE_DEBUG, "dbpf_attr_cache_insert: inserting "
                    "%Lu (k_size is %Lu | b_size is %Lu)\n",
                    Lu(key),
                    Lu(cache_elem->attr.k_size),
                    Lu(cache_elem->attr.b_size));
            }
            ret = 0;
        }
    }
    return ret;
}

int dbpf_attr_cache_remove(TROVE_handle key)
{
    int ret = -1, i = 0;
    struct qlist_head *hash_link = NULL;
    dbpf_attr_cache_elem_t *cache_elem = NULL;    

    if (s_key_to_attr_table)
    {
        hash_link = qhash_search_and_remove(s_key_to_attr_table,&(key));
        if (hash_link)
        {
            cache_elem = qhash_entry(
                hash_link, dbpf_attr_cache_elem_t, hash_link);

            gossip_debug(TROVE_DEBUG, "dbpf_attr_cache_remove: removing "
                         "%Lu\n", Lu(key));

            /* free any keyval data cached as well */
            if (s_cacheable_keywords)
            {
                /* initialize all of the keyvals we're able to cache */
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
            ret = 0;
        }
    }
    return ret;
}

/* hash_fsid()
 *
 * hash function for fsids added to table
 *
 * returns integer offset into table
 */
static int hash_key(void *key, int table_size)
{
    unsigned long tmp = 0;
    TROVE_handle *real_handle = (TROVE_handle *)key;

    tmp += (*(real_handle));
    tmp = (tmp % table_size);

    return ((int)tmp);
}

/* hash_fsid_compare()
 *
 * performs a comparison of a hash table entry to a given key
 * (used for searching)
 *
 * returns 1 if match found, 0 otherwise
 */
static int hash_key_compare(void *key, struct qlist_head *link)
{
    dbpf_attr_cache_elem_t *cache_elem = NULL;
    TROVE_handle *real_handle = (TROVE_handle *)key;

    cache_elem = qlist_entry(link, dbpf_attr_cache_elem_t, hash_link);
    assert(cache_elem);

    if (cache_elem->key == *real_handle)
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
