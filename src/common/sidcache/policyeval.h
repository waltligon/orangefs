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
#include <sidcacheval.h>

#define SID_OTHERS -1

/* DBval is a const SID_cacheval_t * in generated policy predicates.
 * Non-positive attribute values are treated as unset.
 */
#define SID_ATTR(x) \
( \
    (DBval->attr[(x)] <= 0) ? -1 : \
    DBval->attr[(x)] \
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
    int (*scfunc)(const struct SID_cacheval_s *DBval);
} SID_set_criteria_t;

typedef struct SID_policy_s
{
    int layout;               /* how servers are allocated from the sets */
    int join_count;           /* number of attributes in the intersection */
    SID_join_criteria_t *jc;  /* array of join criteria */
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

/* Pull one attribute value out of a cache record. Used by generated
 * extractors. Returns 0 on success.
 */
int SID_get_attr(const struct SID_cacheval_s *cval, int32_t *key, int attr_ix);

void SID_clear_selected(int size);

int SID_is_selected(int value);

void SID_select(int value);

int SID_cmp(SID_cmpop_t cmpop, int v1, int v2);

int SID_add_server_list(SID_server_list_t *sid_list, const PVFS_SID *sid);

int SID_pop_query_list(SID_server_list_t *sid_list,
                       PVFS_SID *sid,
                       PVFS_BMI_addr_t *addr,
                       char *url,
                       int url_size);

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
