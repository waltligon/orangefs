/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_ATTR_CACHE_H
#define __DBPF_ATTR_CACHE_H

#include "dbpf.h"
#include "trove-types.h"
#include "quickhash.h"

/*
  the maximum number of keyval pairs that can be
  automatically cached; pick a reasonable default
*/
#define DBPF_ATTR_CACHE_MAX_NUM_KEYVALS                 8

#define DBPF_ATTR_CACHE_DEFAULT_SIZE                  511
#define DBPF_ATTR_CACHE_DEFAULT_MAX_NUM_CACHE_ELEMS  1024

typedef struct
{
    char *key;
    void *data;
    int data_sz;
} dbpf_keyval_pair_cache_elem_t;

/*
  the keyval pair list contains cacheable keyvals based
  on the keyword list set by set_keywords time.
*/
typedef struct
{
    struct qlist_head hash_link;

    TROVE_handle key;
    TROVE_ds_attributes attr;
    dbpf_keyval_pair_cache_elem_t keyval_pairs[
        DBPF_ATTR_CACHE_MAX_NUM_KEYVALS];
    int num_keyval_pairs;
} dbpf_attr_cache_elem_t;


/***********************************************
 * dbpf-attr-cache generic methods
 *
 * all methods return 0 on success; -1 on failure
 * (unless noted)
 *
 ***********************************************/

/*
  - table size is the hash table size
  - cache_max_num_elems bounds the number of elems stored
    in that hash table
  - cacheable_keywords are keywords that we are allowed to cache
    during keyval reads/writes
  - num_cacheable_keywords is the number of keywords in the
    cacheable_keywords array.  it MUST be between 0 and
    DBPF_ATTR_CACHE_MAX_NUM_KEYVALS
*/
int dbpf_attr_cache_initialize(
    int table_size,
    int cache_max_num_elems,
    char **cacheable_keywords,
    int num_cacheable_keywords);

/* returns the cached element object on success; NULL on failure */
dbpf_attr_cache_elem_t *dbpf_attr_cache_elem_lookup(TROVE_handle key);

int dbpf_attr_cache_elem_set_data_based_on_key(
    TROVE_handle key, char *key_str, void *data, int data_sz);

/* do an atomic update of the attributes in the cache for this key */
int dbpf_attr_cache_ds_attr_pair_update_cached_data(
    dbpf_attr_cache_elem_t *cached_elem,
    TROVE_ds_attributes *src_ds_attr);
/*
  do an atomic copy of the cached attributes into the provided
  target_ds_attr object
*/
int dbpf_attr_cache_ds_attr_pair_fetch_cached_data(
    dbpf_attr_cache_elem_t *cached_elem,
    TROVE_ds_attributes *target_ds_attr);

int dbpf_attr_cache_insert(TROVE_handle key, TROVE_ds_attributes *attr);
int dbpf_attr_cache_remove(TROVE_handle key);
int dbpf_attr_cache_finalize(void);


/***********************************************
 * dbpf-attr-cache keyval related methods
 ***********************************************/
/*
  given a cached elem and a keyval key, return associated
  data if cached; NULL otherwise
*/
dbpf_keyval_pair_cache_elem_t *dbpf_attr_cache_elem_get_data_based_on_key(
    dbpf_attr_cache_elem_t *cached_elem, char *key);

/* map data to key_str, based on specified handle attr cache entry */ 
int dbpf_attr_cache_elem_set_data_based_on_key(
    TROVE_handle key, char *key_str, void *data, int data_sz);

/* do an atomic update of the data in the cache for this keyval */
int dbpf_attr_cache_keyval_pair_update_cached_data(
    dbpf_attr_cache_elem_t *cached_elem,
    dbpf_keyval_pair_cache_elem_t *keyval_pair,
    void *src_data, int src_data_sz);
/*
  do an atomic copy of the cached data into the provided
  buffer; assumes target buffer is large enough to store all
  data.  the size of the data is stored in target_data_sz.
*/
int dbpf_attr_cache_keyval_pair_fetch_cached_data(
    dbpf_attr_cache_elem_t *cached_elem,
    dbpf_keyval_pair_cache_elem_t *keyval_pair,
    void *target_data, int *target_data_sz);


/***********************************************
 * dbpf-attr-cache to trove setinfo hooks
 ***********************************************/
int dbpf_attr_cache_set_keywords(char *keywords);
int dbpf_attr_cache_set_size(int cache_size);
int dbpf_attr_cache_set_max_num_elems(int max_num_elems);
int dbpf_attr_cache_do_initialize(void);

#endif /* __DBPF_ATTR_CACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
