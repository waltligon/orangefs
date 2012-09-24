/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#include <stdlib.h>
#include <string.h>
#include <db.h>

#include <policy.h>
#include <sidcache.h>
#include <policyeval.h>
#include <sidcacheval.h>
#include <quicklist.h>

static int first_cursor_end = 0;

DB *SID_attr_indices[SID_NUM_ATTR];

DBC *SID_attr_cursor[SID_NUM_ATTR];

int SID_get_attr (DB *pri,
                  const DBT *pkey,
                  const DBT *pdata,
                  DBT *skey,
                  int attr_ix)
{
    memset(skey, 0, sizeof(DBT));
    skey->data = &((SID_cacheval_t *)(pdata->data))->attr[attr_ix];
    skey->size = sizeof(int);
    return (0);
}

int SID_do_join(SID_policy_t *policy, DBC **join_curs)
{
    DBT DBkey; /* key for EBD get */
    DBT DBval; /* return value from DB get */
    int val;   /* data buffer for attribute index key */
    int i;     /* general loop index */
    int ret;   /* return value from DB calls */

    if (*join_curs)
    {
        (*join_curs)->close(*join_curs);
    }

    /* position secondary cursors and add them to join cursor array */
    for (i = 0; i < policy->join_count; i++)
    {
        /* each used attr for this policy is listed here */
        val = policy->jc[i].value;
        DBkey.data = &val;
        DBkey.size = sizeof(int);
        if ((ret = policy->carray[i]->get(policy->carray[i],
                                          &DBkey, &DBval, DB_SET)) != 0)
            goto err;
    }

    /* use a DB cursor if we don't need a join */
    if (policy->join_count <= 0)
    {
        if ((ret = SID_db->cursor(SID_db, SID_txn, join_curs, 0)) != 0)
        {
            goto err;
        }
    }
    else
    {
        if ((ret = SID_db->join(SID_db, policy->carray, join_curs, 0)) != 0)
        {
            goto err;
        }
    }
    return 0;

err:
    return -1;
}

int SID_join_count(DBC *join_curs, int *count)
{
    DBT DBkey; /* key for EBD get */
    DBT DBval; /* return value from DB get */
    int ret;   /* return value from DB calls */

    *count = 0;
    while ((ret = join_curs->get(join_curs, &DBkey, &DBval, 0)) == 0)
    {
        (*count)++;
    }
    if (ret < 0)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

int SID_first_record(SID_policy_t *policy,
                     DBC **join_curs,
                     DBT *key,
                     DBT *value)
{
    int i;     /* general loop index */
    int ret;   /* return value from DB calls */
    int first = 0;
    int num_rec;

    /* random select the first one */
    if ((ret = SID_join_count(*join_curs, &num_rec)) != 0)
        goto err;
    first = rand() % num_rec;
    /* must reset the join after a count */
    SID_do_join(policy, join_curs);
    for (i = 0; i < first; i++)
    {
        if ((ret = (*join_curs)->get(*join_curs, key, value, 0)) != 0)
            goto err;
    }
    first_cursor_end = 0;
    return 0;

err:
    return -1;
}

int SID_next_record(SID_policy_t *policy,
                    DBC **join_curs,
                    DBT *key,
                    DBT *value)
{
    int ret;
    switch (policy->layout)
    {
    case PVFS_LAYOUT_ROUND_ROBIN :
            /* go to the next record */
            if ((ret = (*join_curs)->get(*join_curs, key, value, 0)) != 0)
            {
                if (ret == DB_NOTFOUND)
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
    if (!map || map_size <= 0 || value < 0)
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
    if (!map || map_size <= 0 || value < 0)
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
                       DBT *key,
                       DBT *value)
{
    SID_server_list_t *new;
    int url_len;

    new = (SID_server_list_t *)malloc(sizeof(SID_server_list_t));
    if (!new)
    {
        return -1; /* ENOMEM should be set */
    }
    INIT_QLIST_HEAD(&new->link);
    qlist_add_tail(&new->link, &sid_list->link);
    memcpy(&new->server_sid, key->data, sizeof(PVFS_SID));
    new->server_addr = ((SID_cacheval_t *)value->data)->bmi_addr;
    url_len = strlen(((SID_cacheval_t *)value->data)->url) + 1;
    new->server_url = (char *)malloc(url_len);
    if (!new->server_url)
    {
        qlist_del(&new->link);
        free(new);
        return -1; /* ENOMEM should be set */
    }
    memcpy(new->server_url, ((SID_cacheval_t *)value->data)->url, url_len);
    return 0;
}

int SID_select_servers(SID_policy_t *policy,
                       int num_servers,
                       int copies,
                       SID_server_list_t *sid_list)
{
    int i;
    int set;
    DBC *join_curs;
    DBT DBkey_s, DBval_s;
    DBT *DBkey = &DBkey_s;
    DBT *DBval = &DBval_s; /* more convenient to have pointers */

    policy->layout = PVFS_LAYOUT_ROUND_ROBIN;

    policy->carray = (DBC **)malloc(sizeof(DBC *) * policy->join_count);

    /* each attr used by this policy is copied to carray */
    for (i = 0; i < policy->join_count; i++)
    {
        policy->carray[i] = SID_attr_cursor[policy->jc[i].attr];
    }
    /* do the join */
    SID_do_join(policy, &join_curs);

    /* loop over sets to be created - could be var via arg */
    for (set = 0; set < num_servers; set++)
    {
        int i;
        int set_size_remaining = copies;
        /* initialize counter for each rule */
        for (i = 0; i < policy->rule_count; i++)
        {
            policy->sc[i].count = 0;
        }

        /* position the cursor to the first record we plan to use */
        SID_first_record(policy, &join_curs, DBkey, DBval);
        SID_clear_selected(255);

        /* select servers */
        while (set_size_remaining)
        {
            if (SID_is_selected(SID_ATTR(policy->spread_attr)))
            {
                /* do not select again */
                SID_next_record(policy, &join_curs, DBkey, DBval);
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
                    if ( (*policy->sc[i].scfunc)(DBval) )
                    {
                        /* add to output list */
                        SID_add_query_list(sid_list, DBkey, DBval);
                        policy->sc[i].count++;
                        SID_next_record(policy, &join_curs, DBkey, DBval);
                        set_size_remaining--;
                        SID_select(SID_ATTR(policy->spread_attr));
                        continue;
                    }
                    /* otherwise fall through */
                }
            }
            if (i >= policy->rule_count)
            {
                /* not selected by any rule so skip */
                SID_next_record(policy, &join_curs, DBkey, DBval);
            }
        }
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
