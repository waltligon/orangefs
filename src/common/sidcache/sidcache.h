/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef SIDCACHE_H
#define SIDCACHE_H 1

#include <stdint.h>
#include "pvfs2-types.h"
#include "pvfs3-handle.h"
#include "sidcacheval.h"
#include "policyeval.h"

/* Size of the string form of a SID, including the NUL. */
#define SID_STR_LEN (37)

/*
 * Read the primary record for sid. On success *cacheval is malloc'd
 * and the caller frees it. Returns 0, or -PVFS_ENOENT when sid is absent.
 */
int SID_cache_get(const PVFS_SID *sid, SID_cacheval_t **cacheval);

/*
 * Store a primary record and its attribute index entries.
 * num_records non-NULL means insert only: an existing key is left
 * unchanged and the call returns 0. NULL means overwrite.
 */
int SID_cache_put(const PVFS_SID *sid,
                  const SID_cacheval_t *cacheval,
                  int *num_records);

/* SIDs whose attribute attr equals value. Caller frees *sids. */
int SID_attr_list(int attr, int32_t value, PVFS_SID **sids, int *n);

/* Every primary SID. Caller frees *sids. */
int SID_list_all(PVFS_SID **sids, int *n);

/*
 * Number of type-index records for key.
 * server_type SID_SERVER_ALL counts distinct SIDs with that fsid.
 * A missing key yields *count 0 and return 0.
 */
int SID_type_count(const struct SID_type_s *key, int *count);

/*
 * Step through SIDs stored under key.
 * first non-zero starts at the first duplicate.
 * first zero returns the SID after the previous one for this key.
 * Returns 0, or -PVFS_ENOENT when there is no further SID.
 */
int SID_type_step(const struct SID_type_s *key, int first, PVFS_SID *sid);

int SID_cacheval_alloc(SID_cacheval_t **cacheval,
                       const int sid_attributes[],
                       BMI_addr sid_bmi,
                       const char *sid_url);

void SID_cacheval_free(SID_cacheval_t **cacheval);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
