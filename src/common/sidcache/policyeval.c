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

int SID_get_attr(const struct SID_cacheval_s *cval, int32_t *key, int attr_ix)
{
    if (!cval || !key || attr_ix < 0 || attr_ix >= SID_NUM_ATTR)
    {
        return -1;
    }
    *key = cval->attr[attr_ix];
    return 0;
}

static int sid_append(SID_server_list_t *sid_list,
                      const PVFS_SID *sid,
                      const SID_cacheval_t *cval)
{
    SID_server_list_t *new;
    int url_len;

    new = (SID_server_list_t *)malloc(sizeof(SID_server_list_t));
    if (!new)
    {
        return -PVFS_ENOMEM;
    }
    INIT_QLIST_HEAD(&new->link);
    qlist_add_tail(&new->link, &sid_list->link);
    new->server_sid = *sid;
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
            return -PVFS_ENOMEM;
        }
        memcpy(new->server_url, cval->url, url_len);
    }
    return 0;
}

int SID_add_server_list(SID_server_list_t *sid_list, const PVFS_SID *sid)
{
    int ret;
    SID_cacheval_t *temp_cacheval = NULL;

    ret = SID_cache_get(sid, &temp_cacheval);
    if (ret != 0)
    {
        return ret;
    }
    ret = sid_append(sid_list, sid, temp_cacheval);
    SID_cacheval_free(&temp_cacheval);
    return ret;
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

static int sid_policy_sids(SID_policy_t *policy, PVFS_SID **sids, int *n)
{
    int i;
    int rc;
    PVFS_SID *cur = NULL;
    int ncur = 0;

    *sids = NULL;
    *n = 0;
    if (policy->join_count <= 0)
    {
        return SID_list_all(sids, n);
    }
    if (!policy->jc)
    {
        return -PVFS_EINVAL;
    }
    rc = SID_attr_list(policy->jc[0].attr, policy->jc[0].value, &cur, &ncur);
    if (rc)
    {
        return rc;
    }
    for (i = 1; i < policy->join_count; i++)
    {
        PVFS_SID *next = NULL;
        PVFS_SID *hit = NULL;
        int nnext = 0;
        int nhit = 0;

        rc = SID_attr_list(policy->jc[i].attr, policy->jc[i].value,
                           &next, &nnext);
        if (rc)
        {
            free(cur);
            return rc;
        }
        rc = sid_intersect(cur, ncur, next, nnext, &hit, &nhit);
        free(cur);
        free(next);
        if (rc)
        {
            return rc;
        }
        cur = hit;
        ncur = nhit;
    }
    *sids = cur;
    *n = ncur;
    return 0;
}

struct sid_match
{
    PVFS_SID sid;
    SID_cacheval_t *cval;
};

static void sid_free_matches(struct sid_match *matches, int n)
{
    int i;

    if (!matches)
    {
        return;
    }
    for (i = 0; i < n; i++)
    {
        SID_cacheval_free(&matches[i].cval);
    }
    free(matches);
}

/*
 * A policy defines a set of viable SIDs for a given purpose, then we
 * select from those SIDs using a layout much as we did in V2.
 *
 * LMDB has no DB->join. The attribute duplicate lists are intersected
 * in memory. One pass over that set stops when fewer servers match
 * than the requested copy count, and *copies is the number stored.
 */
int SID_select_servers(SID_policy_t *policy,
                       int num_servers,
                       int *copies,
                       SID_server_list_t *sid_list)
{
    int set;
    int i;
    int rc;
    int target = 2;
    int nsrc = 0;
    int nmatches = 0;
    PVFS_SID *src = NULL;
    struct sid_match *matches = NULL;

    if (!policy || !copies || !sid_list || num_servers < 0)
    {
        return -PVFS_EINVAL;
    }
    *copies = target;
    policy->layout = PVFS_SYS_LAYOUT_ROUND_ROBIN;

    rc = sid_policy_sids(policy, &src, &nsrc);
    if (rc)
    {
        return rc;
    }
    matches = (struct sid_match *)calloc(nsrc > 0 ? (size_t)nsrc : 1,
                                         sizeof(*matches));
    if (!matches)
    {
        free(src);
        return -PVFS_ENOMEM;
    }
    for (i = 0; i < nsrc; i++)
    {
        SID_cacheval_t *cval = NULL;
        rc = SID_cache_get(&src[i], &cval);
        if (rc == -PVFS_ENOENT)
        {
            continue;
        }
        if (rc)
        {
            free(src);
            sid_free_matches(matches, nmatches);
            return rc;
        }
        matches[nmatches].sid = src[i];
        matches[nmatches].cval = cval;
        nmatches++;
    }
    free(src);
    if (nmatches == 0)
    {
        sid_free_matches(matches, 0);
        *copies = 0;
        return -PVFS_ENOENT;
    }

    for (set = 0; set < num_servers; set++)
    {
        int added = 0;
        int idle = 0;
        int pos = 0;
        unsigned char *used;

        if (policy->layout == PVFS_SYS_LAYOUT_RANDOM)
        {
            pos = rand() % nmatches;
        }
        for (i = 0; i < policy->rule_count; i++)
        {
            policy->sc[i].count = 0;
        }
        SID_clear_selected(255);
        used = (unsigned char *)calloc((size_t)nmatches, 1);
        if (!used)
        {
            sid_free_matches(matches, nmatches);
            return -PVFS_ENOMEM;
        }
        while (added < target && idle < nmatches)
        {
            const struct SID_cacheval_s *DBval = matches[pos].cval;
            int picked = 0;

            if (used[pos] || SID_is_selected(SID_ATTR(policy->spread_attr)))
            {
                pos = (pos + 1) % nmatches;
                idle++;
                continue;
            }
            used[pos] = 1;
            for (i = 0; i < policy->rule_count; i++)
            {
                if (policy->sc[i].count_max == SID_OTHERS ||
                    policy->sc[i].count < policy->sc[i].count_max)
                {
                    if (policy->sc[i].scfunc &&
                        (*policy->sc[i].scfunc)(DBval))
                    {
                        rc = sid_append(sid_list, &matches[pos].sid, DBval);
                        if (rc)
                        {
                            free(used);
                            sid_free_matches(matches, nmatches);
                            return rc;
                        }
                        policy->sc[i].count++;
                        SID_select(SID_ATTR(policy->spread_attr));
                        added++;
                        picked = 1;
                        break;
                    }
                }
            }
            pos = (pos + 1) % nmatches;
            if (!picked)
            {
                idle++;
            }
        }
        free(used);
        if (set == 0)
        {
            *copies = added;
        }
        if (added == 0)
        {
            sid_free_matches(matches, nmatches);
            return -PVFS_ENOENT;
        }
    }
    sid_free_matches(matches, nmatches);
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
