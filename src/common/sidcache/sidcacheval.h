/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#ifndef SIDCACHEVAL_H
#define SIDCACHEVAL_H 1


typedef int BMI_addr;

typedef struct SID_cacheval_s
{
    int attr[SID_NUM_ATTR];
    BMI_addr bmi_addr;
    char url[0];
} SID_cacheval_t;

/* these are defined in policyeval.c */
/* the depend on SID_NUM_ATTR and thus they are here */

DB *SID_attr_indices[SID_NUM_ATTR];

DBC *SID_attr_cursor[SID_NUM_ATTR];

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
