/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <assert.h>
#include "id-generator.h"
#include "quickhash.h"
#include "gen-locks.h"

#define DEFAULT_ID_GEN_SAFE_TABLE_SIZE 997

static gen_mutex_t s_id_gen_safe_mutex = GEN_MUTEX_INITIALIZER;
static int s_id_gen_safe_init_count = 0;

static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);

static BMI_id_gen_t s_id_gen_safe_tag = 0;

typedef struct
{
    struct qlist_head hash_link;

    BMI_id_gen_t id;
    void *item;
} id_gen_safe_t;

static struct qhash_table *s_id_gen_safe_table = NULL;

#define ID_GEN_SAFE_INITIALIZED() \
(s_id_gen_safe_table)

int id_gen_safe_initialize()
{
    if (!ID_GEN_SAFE_INITIALIZED())
    {
        s_id_gen_safe_table = qhash_init(
            hash_key_compare, hash_key, DEFAULT_ID_GEN_SAFE_TABLE_SIZE);
        if (!s_id_gen_safe_table)
        {
            return -ENOMEM;
        }
    }
    s_id_gen_safe_init_count++;
    return 0;
}

int id_gen_safe_finalize()
{
    s_id_gen_safe_init_count--;
    if(s_id_gen_safe_init_count == 0 && ID_GEN_SAFE_INITIALIZED())
    {
        gen_mutex_lock(&s_id_gen_safe_mutex);
        qhash_destroy_and_finalize(s_id_gen_safe_table, id_gen_safe_t, hash_link, free);
        s_id_gen_safe_table = NULL;
        gen_mutex_unlock(&s_id_gen_safe_mutex);
    }
    return 0;
}

int id_gen_safe_register(
    BMI_id_gen_t *new_id,
    void *item)
{
    id_gen_safe_t *id_elem = NULL;

    assert(s_id_gen_safe_table != NULL);

    if (!item)
    {
	return -EINVAL;
    }

    gen_mutex_lock(&s_id_gen_safe_mutex);

    id_elem = (id_gen_safe_t *)malloc(sizeof(id_gen_safe_t));
    if (!id_elem)
    {
        return -ENOMEM;
    }

    id_elem->id = ++s_id_gen_safe_tag;
    if(id_elem->id == 0)
    {
        /* don't want this to land on zero */
        id_elem->id = ++s_id_gen_safe_tag;
    }
    id_elem->item = item;

    qhash_add(s_id_gen_safe_table, &id_elem->id, &id_elem->hash_link);

    *new_id = id_elem->id;

    gen_mutex_unlock(&s_id_gen_safe_mutex);
    return 0;
}

void *id_gen_safe_lookup(BMI_id_gen_t id)
{
    void *ret = NULL;
    id_gen_safe_t *id_elem = NULL;
    struct qlist_head *hash_link = NULL;

    if (ID_GEN_SAFE_INITIALIZED())
    {
        gen_mutex_lock(&s_id_gen_safe_mutex);

        hash_link = qhash_search(s_id_gen_safe_table, &id);
        if (hash_link)
        {
            id_elem = qlist_entry(hash_link, id_gen_safe_t, hash_link);
            assert(id_elem);
            assert(id_elem->id == id);
            assert(id_elem->item);

            ret = id_elem->item;
        }
        gen_mutex_unlock(&s_id_gen_safe_mutex);
    }
    return ret;
}

int id_gen_safe_unregister(BMI_id_gen_t new_id)
{
    int ret = -EINVAL;
    id_gen_safe_t *id_elem = NULL;
    struct qlist_head *hash_link = NULL;

    if (ID_GEN_SAFE_INITIALIZED())
    {
        gen_mutex_lock(&s_id_gen_safe_mutex);

        hash_link = qhash_search_and_remove(
            s_id_gen_safe_table, &new_id);
        if (hash_link)
        {
            id_elem = qlist_entry(hash_link, id_gen_safe_t, hash_link);
            assert(id_elem);

            id_elem->item = NULL;
            free(id_elem);
            ret = 0;
        }
        gen_mutex_unlock(&s_id_gen_safe_mutex);
    }
    return ret;
}

static int hash_key(void *key, int table_size)
{
    unsigned long tmp = 0;
    BMI_id_gen_t *id = (BMI_id_gen_t *)key;

    tmp += *id;
    tmp = tmp % table_size;

    return ((int) tmp);
}

static int hash_key_compare(void *key, struct qlist_head *link)
{
    id_gen_safe_t *id_elem = NULL;
    BMI_id_gen_t id = *((BMI_id_gen_t *)key);

    id_elem = qlist_entry(link, id_gen_safe_t, hash_link);
    assert(id_elem);

    return (id_elem->id == id);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
