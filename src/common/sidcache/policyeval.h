/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef POLICYEVAL_H
#define POLICYEVAL_H 1

#include <db.h>
#include "sidcache.h"

#define SID_OTHERS -1

#define SID_ATTR(x) \
( \
    ((SID_cacheval_t *)DBval->data)->attr[(x)] <= 0 ? -1 : \
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
    int (*scfunc)(DBT *DBval);
} SID_set_criteria_t;

typedef struct SID_policy_s
{
    int layout;               /* how servers are allocated form the sets */
    int join_count;           /* number of attributes in join */
    SID_join_criteria_t *jc;  /* array of join criteria */
    DBC **carray;             /* array of cursors used in the join */
    int spread_attr;          /* attribute used for the spread func */
    int rule_count;           /* number of rules in set criteria */
    SID_set_criteria_t *sc;   /* array of set criteria */
} SID_policy_t;

typedef struct SID_server_list_s
{
    SID server_sid;
    BMI_addr server_addr;
    char *server_url;
    struct qlist_head *link;
} SID_server_list_t;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
