/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#ifndef SIDCACHEVAL_H
#define SIDCACHEVAL_H 1

#include "policy.h"

typedef int64_t BMI_addr; /* equivalent to PVFS_BMI_adddr_t */

typedef struct SID_cacheval_s
{
    BMI_addr bmi_addr;
    int attr[SID_NUM_ATTR]; /* user defined attributes */
    char *url;              /* space to be allocated */
                            /* immediately following this struct */
} SID_cacheval_t;

struct SID_type_s
{
    PVFS_fs_id fsid;      /* want to sort by fsid first */
    uint32_t server_type;
};

/* Server_type flags
 * Built-in flags indicate what the server does
 * This allows us to OR these together in memory as needed
 * doubting there will be 32 types
 * All valid entries here should be in the define below or they
 * may get overlooked
 * Each of these must be defined in type conv table defined in
 * sidcache.c
 * This is in unsigned long format and will generally be carried around
 * in a unint32_t 
 */

enum {
    SID_SERVER_NULL =     0000UL,
    SID_SERVER_ROOT =     0001UL,
    SID_SERVER_PRIME =    0002UL,
    SID_SERVER_CONFIG =   0004UL,
    SID_SERVER_LOCAL =    0010UL,
    SID_SERVER_META =     0020UL,
    SID_SERVER_DATA =     0040UL,
    SID_SERVER_DIRM =     0100UL,
    SID_SERVER_DIRD =     0200UL,
    SID_SERVER_SECURITY = 0400UL,
    /* This should always be last */
    SID_SERVER_ME =       020000000000UL
};

#define SID_SERVER_VALID_TYPES \
        (SID_SERVER_ROOT | SID_SERVER_PRIME | SID_SERVER_CONFIG | \
         SID_SERVER_META | SID_SERVER_DATA | SID_SERVER_DIRD | \
         SID_SERVER_DIRM | \
         SID_SERVER_SECURITY | SID_SERVER_LOCAL | SID_SERVER_ME)

#define SID_SERVER_ALL SID_SERVER_VALID_TYPES

/* these are defined in policyeval.c */
/* they depend on SID_NUM_ATTR and thus they are here */

extern DB *SID_attr_index[SID_NUM_ATTR];

extern DBC *SID_attr_cursor[SID_NUM_ATTR];

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
