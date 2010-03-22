/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 */

#ifndef __MIRROR_H
#define __MIRROR_H

enum MIRROR_MODE_t { 
   BEGIN_MIRROR_MODE   = 100,
   NO_MIRRORING        = 100,
   MIRROR_ON_IMMUTABLE = 200,
   END_MIRROR_MODE     = 200
};

typedef enum MIRROR_MODE_t MIRROR_MODE;

#define USER_PVFS2_MIRROR_HANDLES "user.pvfs2.mirror.handles"
#define USER_PVFS2_MIRROR_COPIES  "user.pvfs2.mirror.copies"
#define USER_PVFS2_MIRROR_STATUS  "user.pvfs2.mirror.status"
#define USER_PVFS2_MIRROR_MODE    "user.pvfs2.mirror.mode"

#endif /* __MIRROR_H */
