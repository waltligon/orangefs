/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#ifndef  __PVFS2_DIST_OBJECT_H 
#define  __PVFS2_DIST_OBJECT_H 

/* Identifier to use when looking up this distribution */
#define PVFS_DIST_OBJECT_NAME "object_dist"
#define PVFS_DIST_OBJECT_NAME_SIZE 11

struct PVFS_object_params_s {
#ifdef WIN32
    int field;
#endif
};
typedef struct PVFS_object_params_s PVFS_object_params;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
