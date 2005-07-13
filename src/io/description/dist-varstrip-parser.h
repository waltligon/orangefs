/*
 * (C) 2005 Frederik Grüll <frederik.gruell@web.de>
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS_DIST_VARSTRIP_PARSER_H
#define PVFS_DIST_VARSTRIP_PARSER_H

#include "pvfs2-types.h"

struct Strips_s
{
    unsigned int servernr;
    PVFS_offset offset;
    PVFS_size size;
};

typedef struct Strips_s Strips;

void strips_FreeMem(Strips **strip);
int strips_Parse(const char *input, Strips **strip, unsigned *count);

#endif
/*
 * (C) 2005 Frederik Grüll <frederik.gruell@web.de>
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS_DIST_VARSTRIP_PARSER_H
#define PVFS_DIST_VARSTRIP_PARSER_H

#include "pvfs2-types.h"

struct Strips_s
{
    unsigned int servernr;
    PVFS_offset offset;
    PVFS_size size;
};

typedef struct Strips_s Strips;

void strips_FreeMem(Strips **strip);
int strips_Parse(const char *input, Strips **strip, unsigned *count);

#endif
