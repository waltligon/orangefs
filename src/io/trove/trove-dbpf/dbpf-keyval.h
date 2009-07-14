/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_KEYVAL_H__
#define __DBPF_KEYVAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "pvfs2-internal.h"
#include "trove-types.h"

#define DBPF_MAX_KEY_LENGTH PVFS_NAME_MAX

struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char key[DBPF_MAX_KEY_LENGTH];
};

struct dbpf_keyval_db_value
{
    char *key;
    void *value;
};
#define DBPF_KEYVAL_DB_ENTRY_TOTAL_SIZE(_size) \
    (sizeof(TROVE_handle) + _size)

#define DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(_size) \
    (_size - sizeof(TROVE_handle))


#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
