/*
 * (C) 2005 Tobias Eberle <tobias.eberle@gmx.de>
 *
 * See COPYING in top-level directory.
 */       

#ifndef  __PVFS2_DIST_VARSTRIP_H 
#define  __PVFS2_DIST_VARSTRIP_H 

/* Identifier to use when looking up this distribution */
#define PVFS_DIST_VARSTRIP_NAME "varstrip_dist"
#define PVFS_DIST_VARSTRIP_NAME_SIZE 14

#define PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH 1025
struct PVFS_varstrip_params_s {
    /*
     * format:
     * "node number:size;node number:size"
     * example: "0:512;1:512"
     * results in: node number 0: 0-511
     *             node number 1: 512-1023
     * size is in bytes
     */
    char strips[PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH];
};
typedef struct PVFS_varstrip_params_s PVFS_varstrip_params;

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
