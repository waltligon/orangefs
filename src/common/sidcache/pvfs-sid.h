
/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  Functions for accessing SIDcache
 */

#ifndef PVFS_SID_H
#define PVFS_SID_H 1

#include "pvfs3-handle.h"
#include "pvfs2-types.h"
#include "sidcache.h"

/**
 * This routine runs a policy query against the sid cache to select
 * a sid for one or more new metadata objects (inode, dir, symlink, etc.)
 */
int PVFS_OBJ_gen_meta(PVFS_object_ref *obj,
                      int obj_count,
                      PVFS_fs_id fs_id);

/**
 * This routine runs a policy query against the sid cache to select
 * a sid for one or more new datafile objects
 */
int PVFS_OBJ_gen_data(PVFS_object_ref *obj,
                      int obj_count,
                      PVFS_fs_id fs_id);

/**
 * Look up the SID provided and return the matching BMI address
 */
int PVFS_SID_get_addr(PVFS_BMI_addr_t *bmi_addr, const PVFS_SID *sid);

/**
 * Look up a list of SIDs by type and return the matching BMI address
 * _n versions require number of bmi_addr in the provided buffer and
 * return the number actually read in n
 */
int PVFS_SID_get_server_first(PVFS_BMI_addr_t *bmi_addr, 
                              PVFS_SID *sid,
                              uint32_t stype);
int PVFS_SID_get_server_first_n(PVFS_BMI_addr_t *bmi_addr, 
                                PVFS_SID *sid,
                                int *n, /* inout */
                                uint32_t stype);

int PVFS_SID_get_server_next(PVFS_BMI_addr_t *bmi_addr,
                             PVFS_SID *sid,
                             uint32_t stype);
int PVFS_SID_get_server_next_n(PVFS_BMI_addr_t *bmi_addr, 
                               PVFS_SID *sid,
                               int *n, /* inout */
                               uint32_t stype);

int PVFS_OBJ_gen_file(PVFS_fs_id fs_id,
                      PVFS_handle *handle,
                      uint32_t sid_count,
                      PVFS_SID **sid_array,
                      uint32_t datafile_count,
                      PVFS_handle **datafile_handles,
                      uint32_t datafile_sid_count,
                      PVFS_SID **datafile_sid_array);
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

