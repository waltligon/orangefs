/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#ifndef SID_H
#define SID_H 1

#include "pvfs-sid.h"

extern int SID_initialize(void);
extern int SID_load(void);
extern int SID_save(char *path);
extern int SID_finalize(void);

#endif /* SID_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
