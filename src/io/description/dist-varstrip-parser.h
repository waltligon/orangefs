/*
 * (C) 2005 Frederik Grüll <frederik.gruell@web.de>
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS_DIST_VARSTRIP_PARSER_H
#define PVFS_DIST_VARSTRIP_PARSER_H

#include "pvfs2-types.h"

struct PINT_dist_strips_s
{
    unsigned int server_nr;
    PVFS_offset offset;
    PVFS_size size;
};

typedef struct PINT_dist_strips_s PINT_dist_strips;

void PINT_dist_strips_free_mem(PINT_dist_strips **strip);
int PINT_dist_strips_parse(
    const char *input, PINT_dist_strips **strip, unsigned *count);

#endif

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
