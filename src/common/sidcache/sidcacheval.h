/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#ifndef SIDCACHEVAL_H
#define SIDCACHEVAL_H

//#include <sidcache.h>
#include <policy.h>

typedef int BMI_addr;

typedef struct SID_cacheval_s
{
    int attr[SID_NUM_ATTR];
    BMI_addr bmi_addr;
    char url[0];
} SID_cacheval_t;

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
