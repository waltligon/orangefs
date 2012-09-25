/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 */

#ifndef __MIRROR_H
#define __MIRROR_H

enum MIRROR_MODE_t { 
   BEGIN_MIRROR_MODE   = 1,
   NO_MIRRORING        = 100,
   MIRROR_ON_IMMUTABLE = 200,
   MIRROR_ON_WRITE     = 300,
   END_MIRROR_MODE     = 0
};

typedef enum MIRROR_MODE_t MIRROR_MODE;

#define USER_PVFS2_MIRROR_HANDLES "user.pvfs2.mirror.handles"
#define USER_PVFS2_MIRROR_COPIES  "user.pvfs2.mirror.copies"
#define USER_PVFS2_MIRROR_STATUS  "user.pvfs2.mirror.status"
#define USER_PVFS2_MIRROR_MODE    "user.pvfs2.mirror.mode"
#define USER_PVFS2_MIRROR_LAYOUT  "user.pvfs2.mirror.layout"

#endif /* __MIRROR_H */
