
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_HINT_H__
#define __PINT_HINT_H__

#define PVFS_HINT_MAX 24
#define PVFS_HINT_MAX_LENGTH 1024
#define PVFS_HINT_MAX_NAME_LENGTH 512

#define PINT_HINT_TRANSFER 0x01

#include "pvfs2-hint.h"

enum PINT_hint_type
{
    PINT_HINT_UNKNOWN = 0,
    PINT_HINT_REQUEST_ID,
    PINT_HINT_CLIENT_ID,
    PINT_HINT_HANDLE,
    PINT_HINT_OP_ID,
    PINT_HINT_RANK,
    PINT_HINT_SERVER_ID
};

typedef struct PVFS_hint_s
{
    enum PINT_hint_type type;
    char *type_string;
    char *value;
    int32_t length;

    void (*encode)(char **pptr, void *value);
    void (*decode)(char **pptr, void *value);

    int flags;
    struct PVFS_hint_s *next;

} PINT_hint;

void encode_PINT_hint(char **pptr, const PINT_hint *hint);
void decode_PINT_hint(char **pptr, PINT_hint **hint);

void *PINT_hint_get_value_by_type(struct PVFS_hint_s *hint, enum PINT_hint_type type,
                                  int *length);

void *PINT_hint_get_value_by_name(
    struct PVFS_hint_s *hint, const char *name, int *length);

int PVFS_hint_add_internal(
    PVFS_hint *hint,
    enum PINT_hint_type type,
    int length,
    void *value);

int PVFS_hint_replace_internal(
    PVFS_hint *hint,
    enum PINT_hint_type type,
    int length,
    void *value);

#define PINT_HINT_GET_REQUEST_ID(hints) \
    PINT_hint_get_value_by_type(hints, PINT_HINT_REQUEST_ID, NULL) ? \
    *(uint32_t *)PINT_hint_get_value_by_type(hints, PINT_HINT_REQUEST_ID, NULL) : 0

#define PINT_HINT_GET_CLIENT_ID(hints) \
    PINT_hint_get_value_by_type(hints, PINT_HINT_CLIENT_ID, NULL) ? \
    *(uint32_t *)PINT_hint_get_value_by_type(hints, PINT_HINT_CLIENT_ID, NULL) : 0

#define PINT_HINT_GET_HANDLE(hints) \
    PINT_hint_get_value_by_type(hints, PINT_HINT_HANDLE, NULL) ? \
    *(uint64_t *)PINT_hint_get_value_by_type(hints, PINT_HINT_HANDLE, NULL) : 0

#define PINT_HINT_GET_OP_ID(hints) \
    PINT_hint_get_value_by_type(hints, PINT_HINT_OP_ID, NULL) ? \
    *(uint32_t *)PINT_hint_get_value_by_type(hints, PINT_HINT_OP_ID, NULL) : 0

#define PINT_HINT_GET_RANK(hints) \
    PINT_hint_get_value_by_type(hints, PINT_HINT_RANK, NULL) ? \
    *(uint32_t *)PINT_hint_get_value_by_type(hints, PINT_HINT_RANK, NULL) : 0

#endif /* __PINT_HINT_H__ */

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
