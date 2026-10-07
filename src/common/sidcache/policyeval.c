/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#include <stdlib.h>
#include <string.h>

#include "pvfs2-internal.h"
#include "policy.h"
#include "sidcache.h"
#include "policyeval.h"
#include "sidcacheval.h"
#include "quicklist.h"

static int first_cursor_end = 0;

/*
 * The join of the attribute lists. LMDB has no DB->join, so the
 * records live here and SID_first_record / SID_next_record walk them.
 * A real cursor is used only when join_count is zero.
 * join_ready means the list is already built. SID_do_join then
 * repositions it, which is what the old join cursor did after a count.
 */
struct join_item
{
    PVFS_SID sid;
    SID_cacheval_t *cval;
};

static struct join_item *join_items = NULL;
static int join_n = 0;
static int join_pos = 0;
static int join_ready = 0;
static int join_is_list = 0;

/* One unpacked primary-cursor record. SID_next_record leaves this in
 * place when it rejoins, so the caller's key and value stay valid.
 */
static PVFS_SID held_sid;
static SID_cacheval_t *held_cval = NULL;

static void join_clear(void)
{
    int i;

    if (join_items)
    {
        for (i = 0; i < join_n; i++)
        {
            SID_cacheval_free(&join_items[i].cval);
        }
        free(join_items);
    }
    join_items = NULL;
    join_n = 0;
    join_pos = 0;
    join_ready = 0;
    join_is_list = 0;
    SID_cacheval_free(&held_cval);
}

int SID_get_attr(sid_db *pri,
                 const struct sid_data *pkey,
                 const struct sid_data *pdata,
                 struct sid_data *skey,
                 int attr_ix)
{
    (void)pri;
    (void)pkey;
    memset(skey, 0, sizeof(*skey));
    skey->data = &((SID_cacheval_t *)(pdata->data))->attr[attr_ix];
    skey->len = sizeof(int);
    return 0;
}

static int sid_cmp_q(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(PVFS_SID));
}

static int sid_intersect(PVFS_SID *a, int na, PVFS_SID *b, int nb,
                         PVFS_SID **out, int *nout)
{
    int i = 0;
    int j = 0;
    int n = 0;
    PVFS_SID *hit = NULL;

    *out = NULL;
    *nout = 0;
    if (na > 1)
    {
        qsort(a, na, sizeof(PVFS_SID), sid_cmp_q);
    }
    if (nb > 1)
    {
        qsort(b, nb, sizeof(PVFS_SID), sid_cmp_q);
    }
    if (na == 0 || nb == 0)
    {
        return 0;
    }
    hit = (PVFS_SID *)malloc((size_t)(na < nb ? na : nb) * sizeof(PVFS_SID));
    if (!hit)
    {
        return -PVFS_ENOMEM;
    }
    while (i < na && j < nb)
    {
        int cmp = memcmp(&a[i], &b[j], sizeof(PVFS_SID));
        if (cmp == 0)
        {
            if (n == 0 || memcmp(&hit[n - 1], &a[i], sizeof(PVFS_SID)) != 0)
            {
                hit[n++] = a[i];
            }
            i++;
            j++;
        }
        else if (cmp < 0)
        {
            i++;
        }
        else
        {
            j++;
        }
    }
    *out = hit;
    *nout = n;
    return 0;
}

/* Read the duplicate SIDs at the current key. The cursor is already
 * positioned, or SET failed with -PVFS_ENOENT.
 */
static int join_dups(sid_cursor *cursorp, struct sid_data *val, int positioned,
                     PVFS_SID **sids, int *n)
{
    PVFS_SID *buf = NULL;
    int count = 0;
    int rc = positioned ? 0 : -PVFS_ENOENT;

    struct sid_data key;

    *sids = NULL;
    *n = 0;
    SID_zero_dbt(&key, NULL, NULL);
    while (rc == 0)
    {
        if (val->len == sizeof(PVFS_SID) && val->data)
        {
            PVFS_SID *grown;

            grown = (PVFS_SID *)realloc(buf,
                                        (size_t)(count + 1) * sizeof(PVFS_SID));
            if (!grown)
            {
                free(buf);
                free(val->data);
                return -PVFS_ENOMEM;
            }
            buf = grown;
            memcpy(&buf[count], val->data, sizeof(PVFS_SID));
            count++;
        }
        free(val->data);
        val->data = NULL;
        val->len = 0;
        rc = sid_db_cursor_get(cursorp, &key, val, SID_DB_CURSOR_NEXT_DUP);
    }
    if (rc != -PVFS_ENOENT && rc != 0)
    {
        free(buf);
        free(val->data);
        return rc;
    }
    *sids = buf;
    *n = count;
    return 0;
}

/* One read transaction, then the cache records are copied out.
 * The transaction does not outlive this function.
 */
static int join_build(SID_policy_t *policy)
{
    int i;
    int rc;
    int txn = 0;
    PVFS_SID *cur = NULL;
    int ncur = 0;

    rc = sid_db_txn_begin(SID_envp, 1);
    if (rc)
    {
        return rc;
    }
    txn = 1;
    for (i = 0; i < policy->join_count; i++)
    {
        sid_cursor *cursorp = NULL;
        int local = 0;
        struct sid_data key;
        struct sid_data val;
        int32_t value;
        int attr;
        PVFS_SID *next = NULL;
        int nnext = 0;
        int positioned = 0;

        attr = policy->jc[i].attr;
        if (attr < 0 || attr >= SID_NUM_ATTR || !SID_attr_index[attr])
        {
            rc = -PVFS_EINVAL;
            goto out;
        }
        if (policy->carray && policy->carray[i])
        {
            cursorp = policy->carray[i];
        }
        else
        {
            rc = sid_db_cursor(SID_attr_index[attr], &cursorp, 1);
            if (rc)
            {
                goto out;
            }
            local = 1;
        }
        value = (int32_t)policy->jc[i].value;
        SID_zero_dbt(&key, &val, NULL);
        key.data = &value;
        key.len = sizeof(value);
        rc = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_SET);
        if (rc == 0)
        {
            positioned = 1;
        }
        else if (rc != -PVFS_ENOENT)
        {
            if (local)
            {
                sid_db_cursor_close(cursorp);
            }
            goto out;
        }
        rc = join_dups(cursorp, &val, positioned, &next, &nnext);
        if (local)
        {
            sid_db_cursor_close(cursorp);
        }
        if (rc)
        {
            free(next);
            goto out;
        }
        if (i == 0)
        {
            cur = next;
            ncur = nnext;
        }
        else
        {
            PVFS_SID *hit = NULL;
            int nhit = 0;

            rc = sid_intersect(cur, ncur, next, nnext, &hit, &nhit);
            free(cur);
            free(next);
            if (rc)
            {
                goto out;
            }
            cur = hit;
            ncur = nhit;
        }
    }
    sid_db_txn_abort(SID_envp);
    txn = 0;

    join_items = (struct join_item *)calloc(ncur > 0 ? (size_t)ncur : 1,
                                            sizeof(*join_items));
    if (!join_items)
    {
        free(cur);
        return -PVFS_ENOMEM;
    }
    join_n = 0;
    for (i = 0; i < ncur; i++)
    {
        SID_cacheval_t *cval = NULL;

        rc = SID_cache_get(SID_db, &cur[i], &cval);
        if (rc == -PVFS_ENOENT)
        {
            continue;
        }
        if (rc)
        {
            free(cur);
            join_clear();
            return rc;
        }
        join_items[join_n].sid = cur[i];
        join_items[join_n].cval = cval;
        join_n++;
    }
    free(cur);
    join_pos = 0;
    join_is_list = 1;
    join_ready = 1;
    return 0;

out:
    free(cur);
    if (txn)
    {
        sid_db_txn_abort(SID_envp);
    }
    return rc;
}

int SID_do_join(SID_policy_t *policy, sid_cursor **join_curs)
{
    int ret;

    if (!policy || !join_curs)
    {
        return -1;
    }
    if (*join_curs)
    {
        sid_db_cursor_close(*join_curs);
        *join_curs = NULL;
    }
    /* use a cursor if we don't need a join */
    if (policy->join_count <= 0)
    {
        join_is_list = 0;
        join_ready = 0;
        if ((ret = sid_db_cursor(SID_db, join_curs, 1)) != 0)
        {
            return -1;
        }
        return 0;
    }
    if (join_ready)
    {
        join_pos = 0;
        return 0;
    }
    if ((ret = join_build(policy)) != 0)
    {
        return -1;
    }
    return 0;
}

/* Next joined record. Flag 0 on a DB join cursor meant "next". */
static int join_get(sid_cursor *join_curs,
                    struct sid_data *key,
                    struct sid_data *value)
{
    struct sid_data raw_key;
    struct sid_data raw_val;
    SID_cacheval_t *cval = NULL;
    int rc;

    if (join_is_list)
    {
        if (join_pos >= join_n)
        {
            return -PVFS_ENOENT;
        }
        key->data = &join_items[join_pos].sid;
        key->len = sizeof(PVFS_SID);
        value->data = join_items[join_pos].cval;
        value->len = sizeof(SID_cacheval_t);
        join_pos++;
        return 0;
    }
    if (!join_curs)
    {
        return -PVFS_EINVAL;
    }
    SID_zero_dbt(&raw_key, &raw_val, NULL);
    rc = sid_db_cursor_get(join_curs, &raw_key, &raw_val, SID_DB_CURSOR_NEXT);
    if (rc)
    {
        return rc;
    }
    SID_cacheval_unpack(&cval, &raw_val);
    free(raw_val.data);
    if (!cval || raw_key.len != sizeof(PVFS_SID) || !raw_key.data)
    {
        free(raw_key.data);
        SID_cacheval_free(&cval);
        return -PVFS_EIO;
    }
    SID_cacheval_free(&held_cval);
    memcpy(&held_sid, raw_key.data, sizeof(PVFS_SID));
    free(raw_key.data);
    held_cval = cval;
    key->data = &held_sid;
    key->len = sizeof(PVFS_SID);
    value->data = cval;
    value->len = sizeof(SID_cacheval_t);
    return 0;
}

int SID_join_count(sid_cursor *join_curs, int *count)
{
    struct sid_data DBkey;
    struct sid_data DBval;
    int ret;

    if (!count)
    {
        return -1;
    }
    *count = 0;
    if (join_is_list)
    {
        *count = join_n;
        join_pos = join_n;
        return 0;
    }
    if (!join_curs)
    {
        return -1;
    }
    SID_zero_dbt(&DBkey, &DBval, NULL);
    while ((ret = sid_db_cursor_get(join_curs, &DBkey, &DBval,
                                    SID_DB_CURSOR_NEXT)) == 0)
    {
        (*count)++;
        free(DBkey.data);
        free(DBval.data);
        SID_zero_dbt(&DBkey, &DBval, NULL);
    }
    if (ret == -PVFS_ENOENT)
    {
        return 0;
    }
    return -1;
}

int SID_first_record(SID_policy_t *policy,
                     sid_cursor **join_curs,
                     struct sid_data *key,
                     struct sid_data *value)
{
    int i;
    int ret;
    int first = 0;
    int num_rec;

    /* random select the first one */
    if ((ret = SID_join_count(*join_curs, &num_rec)) != 0)
    {
        goto err;
    }
    if (num_rec <= 0)
    {
        goto err;
    }
    first = rand() % num_rec;
    /* must reset the join after a count */
    if (SID_do_join(policy, join_curs) != 0)
    {
        goto err;
    }
    /* Land on the chosen record. The old loop skipped `first` gets
     * and left key empty when the start was record 0.
     */
    for (i = 0; i <= first; i++)
    {
        if ((ret = join_get(*join_curs, key, value)) != 0)
        {
            goto err;
        }
    }
    first_cursor_end = 0;
    return 0;

err:
    return -1;
}

/*
 * A policy defines a set of viable SIDs for a given purpose, then we
 * select from those SIDs using a layout much as we did in V2
 */

int SID_next_record(SID_policy_t *policy,
                    sid_cursor **join_curs,
                    struct sid_data *key,
                    struct sid_data *value)
{
    int ret;

    switch (policy->layout)
    {
    case PVFS_LAYOUT_ROUND_ROBIN :
            /* go to the next record */
            if ((ret = join_get(*join_curs, key, value)) != 0)
            {
                if (ret == -PVFS_ENOENT)
                {
                    /* if end of records, return to start */
                    /* but only once */
                   if (!first_cursor_end)
                   {
                       first_cursor_end = 1;
                       SID_do_join(policy, join_curs);
                   }
                   else
                   {
                       return -1; /* cannot complete policy */
                   }
                }
                else
                {
                    goto err;
                }
            }
        break;
    case PVFS_LAYOUT_RANDOM :
        /* randomly pick a record */
        break;
    }
    return 0;

err:
    return -1;
}

/*
 * Map is a bit mask used to keep up with which SIDs have been selected by
 * a query.  SID_clear_selected initializes the bit mask (including
 * mallocing it) SID_is_selected checks to see if a bit is set, and
 * SID_select sets a bit
 */

static unsigned char *map = NULL;
static int map_size = 0;

void SID_clear_selected(int size)
{
    if (size <= 0)
    {
        if (map)
        {
            free(map);
        }
        map_size = 0;
        return;
    }
    if (size != map_size)
    {
        if (map)
        {
            free(map);
        }
        map = (unsigned char *)malloc((size / 8) + 1);
        map_size = size;
    }
    memset(map, 0, (size / 8) + 1);
    return;
}

int SID_is_selected(int value)
{
    int slot = value >> 3;
    int bit = value & 0x07;
    if (!map || map_size <= 0 || value < 0 || value > map_size)
    {
        return 0;
    }
    if (map[slot] & (0x01 << bit))
    {
        return 1;
    }
    return 0;
}

void SID_select(int value)
{
    int slot = value >> 3;
    int bit = value & 0x07;
    if (!map || map_size <= 0 || value < 0 || value > map_size)
    {
        return;
    }
    map[slot] |= (0x01 << bit);
    return;
}

int SID_cmp(SID_cmpop_t cmpop, int v1, int v2)
{
    switch (cmpop)
    {
    case SID_EQ :
        return (v1 == v2);
    case SID_NE :
        return (v1 != v2);
    case SID_GT :
        return (v1 >  v2);
    case SID_GE :
        return (v1 >= v2);
    case SID_LT :
        return (v1 <  v2);
    case SID_LE :
        return (v1 <= v2);
    default:
        return (v1 == v2);
    }
}

int SID_add_query_list(SID_server_list_t *sid_list,
                       struct sid_data *key,
                       struct sid_data *value)
{
    SID_server_list_t *new;
    SID_cacheval_t *cval;
    int url_len;

    if (!sid_list || !key || !key->data || !value || !value->data)
    {
        return -1;
    }
    cval = (SID_cacheval_t *)value->data;
    new = (SID_server_list_t *)malloc(sizeof(SID_server_list_t));
    if (!new)
    {
        return -1; /* ENOMEM should be set */
    }
    INIT_QLIST_HEAD(&new->link);
    qlist_add_tail(&new->link, &sid_list->link);

    memcpy(&new->server_sid, key->data, sizeof(PVFS_SID));
    /* do we need this? we have nowhere to put it at the moment */
    new->server_addr = cval->bmi_addr;
    new->server_url = NULL;
    if (cval->url)
    {
        url_len = (int)strlen(cval->url) + 1;
        new->server_url = (char *)malloc(url_len);
        if (!new->server_url)
        {
            qlist_del(&new->link);
            free(new);
            return -1; /* ENOMEM should be set */
        }
        memcpy(new->server_url, cval->url, url_len);
    }
    /* end of do we need this? */
    return 0;
}

int SID_add_server_list(SID_server_list_t *sid_list, const PVFS_SID *sid)
{
    int ret;
    SID_server_list_t *new;
    SID_cacheval_t *temp_cacheval;
    int url_len;

    ret = SID_cache_get(SID_db, sid, &temp_cacheval);
    if (ret != 0)
    {
        return ret;
    }

    new = (SID_server_list_t *)malloc(sizeof(SID_server_list_t));
    if (!new)
    {
        SID_cacheval_free(&temp_cacheval);
        return -1; /* ENOMEM should be set */
    }
    INIT_QLIST_HEAD(&new->link);
    qlist_add_tail(&new->link, &sid_list->link);

    new->server_sid = *sid;
    /* do we need this? we have nowhere to put it at the moment */
    new->server_addr = temp_cacheval->bmi_addr;
    url_len = (int)strlen(temp_cacheval->url) + 1;
    new->server_url = (char *)malloc(url_len);
    if (!new->server_url)
    {
        qlist_del(&new->link);
        free(new);
        SID_cacheval_free(&temp_cacheval);
        return -1; /* ENOMEM should be set */
    }
    memcpy(new->server_url, temp_cacheval->url, url_len);
    /* end of do we need this? */
    SID_cacheval_free(&temp_cacheval);
    return 0;
}

int SID_pop_query_list(SID_server_list_t *sid_list,
                       PVFS_SID *sid,
                       PVFS_BMI_addr_t *addr,
                       char *url,
                       int url_size)
{
    struct qlist_head *item = NULL;
    SID_server_list_t *server = NULL;
    item = qlist_pop(&sid_list->link);
    if (!item)
    {
        return -1;
    }
    server = qlist_entry(item, SID_server_list_t, link);
    if (sid)
    {
        *sid = server->server_sid;
    }
    if (addr)
    {
        *addr = server->server_addr;
    }
    if (url && url_size > 0)
    {
        if (server->server_url)
        {
            strncpy(url, server->server_url, url_size - 1);
        }
        else
        {
            url[0] = 0;
        }
        url[url_size - 1] = 0;
    }
    free(server->server_url);
    free(server);
    return 0;
}

/*
 * Same call sequence as the Berkeley DB version. *copies starts at 2
 * (FIXME) and is then the number stored on the first set. The layout
 * is round robin. continue inside the rule for continues that for.
 * The while stops when SID_next_record returns -1. carray slots are
 * the SID_attr_cursor globals, which are NULL. The join opens a
 * cursor for the call.
 */
int SID_select_servers(SID_policy_t *policy,
                       int num_servers,
                       int *copies,
                       SID_server_list_t *sid_list)
{
    int i;
    int set;
    int stored = 0;
    int target;
    sid_cursor *join_curs = NULL;
    struct sid_data DBkey_s, DBval_s;
    struct sid_data *DBkey = &DBkey_s;
    struct sid_data *DBval = &DBval_s; /* more convenient to have pointers */

    if (!policy || !copies || !sid_list || num_servers < 0)
    {
        return -PVFS_EINVAL;
    }
    memset(&DBkey_s, 0, sizeof(DBkey_s));
    memset(&DBval_s, 0, sizeof(DBval_s));

    *copies = 2; /* FIXME */
    target = *copies;

    policy->layout = PVFS_LAYOUT_ROUND_ROBIN;

    if (policy->join_count > 0)
    {
        policy->carray = (sid_cursor **)malloc(sizeof(sid_cursor *) *
                                               policy->join_count);
        if (!policy->carray)
        {
            return -PVFS_ENOMEM;
        }
        /* each attr used by this policy is copied to carray */
        for (i = 0; i < policy->join_count; i++)
        {
            policy->carray[i] = SID_attr_cursor[policy->jc[i].attr];
        }
    }
    else
    {
        policy->carray = NULL;
    }
    /* do the join */
    if (SID_do_join(policy, &join_curs) != 0)
    {
        free(policy->carray);
        policy->carray = NULL;
        join_clear();
        return -1;
    }

    /* loop over sets to be created - could be var via arg */
    for (set = 0; set < num_servers; set++)
    {
        int set_size_remaining = target;
        /* initialize counter for each rule */
        for (i = 0; i < policy->rule_count; i++)
        {
            policy->sc[i].count = 0;
        }

        /* position the cursor to the first record we plan to use */
        if (SID_first_record(policy, &join_curs, DBkey, DBval) != 0)
        {
            break;
        }
        SID_clear_selected(255);

        /* select servers */
        while (set_size_remaining)
        {
            if (!DBval->data)
            {
                set_size_remaining = 0;
                break;
            }
            if (SID_is_selected(SID_ATTR(policy->spread_attr)))
            {
                /* do not select again */
                if (SID_next_record(policy, &join_curs, DBkey, DBval) != 0)
                {
                    set_size_remaining = 0;
                }
                continue;
            }
            for (i = 0; i < policy->rule_count; i++)
            {
                if (policy->sc[i].count_max == SID_OTHERS ||
                    policy->sc[i].count < policy->sc[i].count_max)
                {
                    /* if ( SID_cmp(policy->sc[i].cmpop,
                                SID_ATTR(policy->sc[i].attr),
                                policy->sc.value) ) */
                    if (policy->sc[i].scfunc &&
                        (*policy->sc[i].scfunc)(DBval))
                    {
                        /* add to output list */
                        if (SID_add_query_list(sid_list, DBkey, DBval) != 0)
                        {
                            set_size_remaining = 0;
                            break;
                        }
                        policy->sc[i].count++;
                        set_size_remaining--;
                        if (set == 0)
                        {
                            stored++;
                        }
                        SID_select(SID_ATTR(policy->spread_attr));
                        if (SID_next_record(policy, &join_curs, DBkey, DBval) != 0)
                        {
                            set_size_remaining = 0;
                        }
                        continue;
                    }
                    /* otherwise fall through */
                }
            }
            if (i >= policy->rule_count)
            {
                /* not selected by any rule so skip */
                if (SID_next_record(policy, &join_curs, DBkey, DBval) != 0)
                {
                    break;
                }
            }
        }
        if (set == 0)
        {
            *copies = stored;
        }
    }
    if (join_curs)
    {
        sid_db_cursor_close(join_curs);
    }
    join_clear();
    free(policy->carray);
    policy->carray = NULL;
    if (stored == 0)
    {
        return -PVFS_ENOENT;
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
