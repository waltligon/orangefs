#ifndef __PINT_UID_MGMT_H
#define __PINT_UID_MGMT_H

#include "pvfs2-internal.h"
#include "quicklist.h"
#include "quickhash.h"
#include "pvfs2-types.h"

/* UID_MGMT_MAX_HISTORY is the number of UIDs stored in history
 * UID_HISTORY_HASH_TABLE_SIZE is the size of the hash tbl used to store uids
 */
#define UID_MGMT_MAX_HISTORY 25
#define UID_HISTORY_HASH_TABLE_SIZE 19

typedef struct qlist_head list_head_t;
typedef struct qhash_table hash_table_t;

/* information stored in each uid management structure defined below */
typedef struct
        {
        PVFS_uid uid;
        uint64_t count;
        struct timeval tv0;
        struct timeval tv;
        } PVFS_uid_info_s;
endecode_fields_2_struct(
        timeval,
        uint64_t, tv_sec,
        uint32_t, tv_usec);
endecode_fields_4(
        PVFS_uid_info_s,
        PVFS_uid, uid,
        uint64_t, count,
        timeval, tv0,
        timeval, tv);

/* our uid management structure */
typedef struct
        {
        PVFS_uid_info_s info;
        list_head_t lru_link;
        list_head_t hash_link;
        } PINT_uid_mgmt_s;

/* macro helper to determine if a UID is within the history or not */
#define IN_UID_HISTORY(current, oldest)                   \
           (((current.tv_sec * 1e6) + current.tv_usec) >  \
           ((oldest.tv_sec * 1e6) + oldest.tv_usec))

/* FUNCTION PROTOTYPES */
int PINT_uid_mgmt_initialize(void);
void PINT_uid_mgmt_finalize(void);
int PINT_add_user_to_uid_mgmt(PVFS_uid userID);
void PINT_dump_all_uid_stats(PVFS_uid_info_s *uid_stats);

#endif /* __PINT_UID_MGMT_H */
