/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "pvfs2-types.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs-distribution.h"
#include "simple-stripe.h"
#include "pint-distribution.h"

void PINT_Dist_dump(PVFS_Dist *dist);

int main(int argc, char **argv)
{
    struct PVFS_Dist *d;
    int ret = -1;
    PVFS_offset tmp_off = 0;

    /* grab a distribution */
    d = PVFS_Dist_create("simple_stripe");
    assert(d);

    ret = PINT_Dist_lookup(d);
    assert(ret == 0);

    PINT_Dist_dump(d);

    /* easy case */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 0, 4, 100);
    printf("offset: %Ld\n", (long long)tmp_off);


    /* just before strip */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 0, 4, (64*1024-1));
    printf("offset: %Ld\n", (long long)tmp_off);

    /* at strip */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 0, 4, (64*1024));
    printf("offset: %Ld\n", (long long)tmp_off);

    /* just after strip */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 0, 4, (64*1024+1));
    printf("offset: %Ld\n", (long long)tmp_off);

    /* wrap around tests */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 0, 4, (64*1024*4+1));
    printf("offset: %Ld\n", (long long)tmp_off);

    /* try a different io server */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 3, 4, (64*1024-1));
    printf("offset: %Ld\n", (long long)tmp_off);

    /* same as above, but in his region w/ wrap around */
    tmp_off = d->methods->logical_to_physical_offset(d->params, 3, 4, (64*1024*7+15));
    printf("offset: %Ld\n", (long long)tmp_off);


    /* free dist */
    PVFS_Dist_free(d);

    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
