/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef POLICYEVAL_H
#define POLICYEVAL_H 1

#include <pvfs2-types.h>
#include <pvfs3-handle.h>
#include <quicklist.h>
#include <sidcache-db.h>
#include <sidcacheval.h>

#define SID_OTHERS -1

/* DBval is a struct sid_data * whose data is a SID_cacheval_t.
 * Non-positive attribute values are treated as unset.
 * This is the old DBT shape: the generated predicates still say DBval.
 */
#define SID_ATTR(x) \
( \
    (((SID_cacheval_t *)DBval->data)->attr[(x)] <= 0) ? -1 : \
    ((SID_cacheval_t *)DBval->data)->attr[(x)] \
)

typedef enum SID_cmpop_e
{
    SID_EQ,
    SID_NE,
    SID_GT,
    SID_GE,
    SID_LT,
    SID_LE
} SID_cmpop_t;

typedef struct SID_join_criteria_s
{
    int attr;
    int value;
} SID_join_criteria_t;

typedef struct SID_set_criteria_s
{
    int count;
    int count_max;
    int (*scfunc)(struct sid_data *DBval);
} SID_set_criteria_t;

typedef struct SID_policy_s
{
    int layout;               /* how servers are allocated from the sets */
    int join_count;           /* number of attributes in the intersection */
    SID_join_criteria_t *jc;  /* array of join criteria */
    sid_cursor **carray;      /* array of cursors used in the join */
    int spread_attr;          /* attribute used for the spread func */
    int rule_count;           /* number of rules in set criteria */
    SID_set_criteria_t *sc;   /* array of set criteria */
} SID_policy_t;

typedef struct SID_server_list_s
{
    PVFS_SID server_sid;
    PVFS_BMI_addr_t server_addr;
    char *server_url;
    struct qlist_head link;
} SID_server_list_t;

extern SID_policy_t SID_policies[]; /* defined in compiled policy files */

#define PVFS_POLICY_MAX 10 /* FIXME */

/* Associate callback. pri and pkey are unused. skey points at the
 * attribute inside pdata. Same call the secondary index used to make.
 */
int SID_get_attr(sid_db *pri,
                 const struct sid_data *pkey,
                 const struct sid_data *pdata,
                 struct sid_data *skey,
                 int attr_ix);

/* Position the join. join_count <= 0 opens a primary cursor.
 * Otherwise the records are the intersection of the attribute lists,
 * walked by SID_first_record and SID_next_record. Berkeley DB did
 * this with DB->join. Both walks look at every match, because the
 * old path counted the join before it selected. The extra work here
 * is the in-memory lists. The table is servers, not files. This was
 * not timed.
 *
 * carray entries are SID_attr_cursor slots, which stay NULL. This
 * function opens a cursor for the call. A non-NULL carray slot that
 * already holds the environment lock will deadlock with the
 * transaction this function begins. Do not put the process-lifetime
 * cursors back to avoid that.
 */
int SID_do_join(SID_policy_t *policy, sid_cursor **join_curs);

int SID_join_count(sid_cursor *join_curs, int *count);

int SID_first_record(SID_policy_t *policy,
                     sid_cursor **join_curs,
                     struct sid_data *key,
                     struct sid_data *value);

int SID_next_record(SID_policy_t *policy,
                    sid_cursor **join_curs,
                    struct sid_data *key,
                    struct sid_data *value);

void SID_clear_selected(int size);

int SID_is_selected(int value);

void SID_select(int value);

int SID_cmp(SID_cmpop_t cmpop, int v1, int v2);

int SID_add_query_list(SID_server_list_t *sid_list,
                       struct sid_data *key,
                       struct sid_data *value);

int SID_add_server_list(SID_server_list_t *sid_list, const PVFS_SID *sid);

int SID_pop_query_list(SID_server_list_t *sid_list,
                       PVFS_SID *sid,
                       PVFS_BMI_addr_t *addr,
                       char *url,
                       int url_size);

/* Build the server list for one policy. *copies starts at 2 (FIXME:
 * how many to try) and is then the number stored from the first set.
 * The layout is PVFS_LAYOUT_ROUND_ROBIN. The rule loop is the old
 * one. Returns -PVFS_ENOENT when nothing was stored.
 */
int SID_select_servers(SID_policy_t *policy,
                       int num_servers,
                       int *copies,
                       SID_server_list_t *sid_list);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
