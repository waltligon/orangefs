/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS3_HANDLE_H
#define PVFS3_HANDLE_H 1

#include <stdint.h>
#include <uuid/uuid.h>

/* uuid_t is an unsigned char[16] array and thus passes by reference */

/** Unique identifier for a server on a PVFS3 file system  128-bit */
typedef struct {uuid_t u;} PVFS_SID __attribute__ ((__aligned__ (8)));

#define encode_PVFS_SID(pptr,pbuf) do { \
    memcpy(*(pptr), (pbuf), 16); \
    *(pptr) += 16; \
} while (0)
    
#define decode_PVFS_SID(pptr,pbuf) do { \
    memcpy((pbuf), *(pptr), 16); \
    *(pptr) += 16; \
} while (0)
    
/** Unique identifier for an object on a PVFS3 file system  128-bit */
typedef struct {uuid_t u;} PVFS_OID __attribute__ ((__aligned__ (8)));

#define encode_PVFS_OID(pptr,pbuf) do { \
    memcpy(*(pptr), (pbuf), 16); \
    *(pptr) += 16; \
} while (0)
    
#define decode_PVFS_OID(pptr,pbuf) do { \
    memcpy((pbuf), *(pptr), 16); \
    *(pptr) += 16; \
} while (0)

#define PVFS_HANDLE_NULL_INIT {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}
static const PVFS_OID PVFS_HANDLE_NULL = PVFS_HANDLE_NULL_INIT;
static const PVFS_OID PVFS_HANDLE_HIGH = {{255,255,255,255,255,
                                           255,255,255,255,255,
                                           255,255,255,255,255,255}};

    
/* UUID Variant definitions */
#define PVFS_OID_variant_ncs UUID_VARIANT_NCS    /*0*/
#define PVFS_OID_variant_dce UUID_VARIANT_DCE    /*1*/
#define PVFS_OID_variant_microsoft UUID_VARIANT_MICROSOFT  /*2*/
#define PVFS_OID_variant_other UUID_VARIANT_OTHER  /*3*/

/* UUID Type definitions */
#define PVFS_OID_type_dce_time UUID_TYPE_DCE_TIME   /*1*/
#define PVFS_OID_type_dce_random UUID_TYPE_DCE_RANDOM /*4*/

/* Allow UUID constants to be defined */
#define PVFS_OID_DEFINE(name,u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15) \
UUID_DEFINE(name,u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15)

/** PVFS_OIDs are uuids in a PVFS wrapper.  They are used for object handles
 * but should always be used via this wrapper to allow future migration
 * to a different implementation.  PVFS_SIDs are also uuids and are thus
 * managed the same way.
 */

static __inline__ void PVFS_OID_init(PVFS_OID *oid)
{
    uuid_clear(oid->u);
}

static __inline__ int PVFS_OID_cmp(const PVFS_OID *oid1, const PVFS_OID *oid2)
{
    return uuid_compare(oid1->u, oid2->u);
}

static __inline__ void PVFS_OID_cpy(PVFS_OID *dst, const PVFS_OID *src)
{
    return uuid_copy(dst->u, src->u);
}

static __inline__ void PVFS_OID_gen(PVFS_OID *out)
{
    uuid_generate(out->u);
}

static __inline__ int PVFS_OID_is_null(const PVFS_OID *oid)
{
    return uuid_is_null(oid->u);
}

static __inline__ int PVFS_OID_str2bin(const char *str, PVFS_OID *oid)
{
    return uuid_parse(str, oid->u);
}

static __inline__ void PVFS_OID_bin2str(const PVFS_OID *oid, char *str)
{
    uuid_unparse(oid->u, str);
}

static __inline__ char *PVFS_OID_str(const PVFS_OID *oid)
{
    static char str[17];
    uuid_unparse(oid->u, str);
    str[17] = 0;
    return str;
}

static __inline__ uint32_t PVFS_OID_hash32(const PVFS_OID *oid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    void *q = &oid;
    int32_t *p = q;
    return p[0] + p[1] + p[2] + p[3];
}

static __inline__ uint64_t PVFS_OID_hash64(const PVFS_OID *oid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    void *q = &oid;
    int64_t *p = q;
    return p[0] + p[1];
}

#define PVFS_OID_encode(d,s) PVFS_OID_cpy(((PVFS_OID *)d),(s))
#define PVFS_OID_decode(d,s) PVFS_OID_cpy((d),((PVFS_OID *)s))

/* UUID Variant definitions */
#define PVFS_SID_variant_ncs UUID_VARIANT_NCS    /*0*/
#define PVFS_SID_variant_dce UUID_VARIANT_DCE    /*1*/
#define PVFS_SID_variant_microsoft UUID_VARIANT_MICROSOFT  /*2*/
#define PVFS_SID_variant_other UUID_VARIANT_OTHER  /*3*/

/* UUID Type definitions */
#define PVFS_SID_type_dce_time UUID_TYPE_DCE_TIME   /*1*/
#define PVFS_SID_type_dce_random UUID_TYPE_DCE_RANDOM /*4*/

/* Allow UUID constants to be defined */
#define PVFS_SID_DEFINE(name,u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15) \
UUID_DEFINE(name,u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15)

/** PVFS_OIDs are uuids in a PVFS wrapper.  They are used for object handles
 * but should always be used via this wrapper to allow future migration
 * to a different implementation.  PVFS_SIDs are also uuids and are thus
 * managed the same way.
 */

static __inline__ void PVFS_SID_init(PVFS_SID *sid)
{
    uuid_clear(sid->u);
}

static __inline__ int PVFS_SID_cmp(const PVFS_SID *sid1, const PVFS_SID *sid2)
{
    return uuid_compare(sid1->u, sid2->u);
}

static __inline__ void PVFS_SID_cpy(PVFS_SID *dst, const PVFS_SID *src)
{
    return uuid_copy(dst->u, src->u);
}

static __inline__ void PVFS_SID_gen(PVFS_SID *out)
{
    uuid_generate(out->u);
}

static __inline__ int PVFS_SID_is_null(const PVFS_SID *sid)
{
    return uuid_is_null(sid->u);
}

static __inline__ int PVFS_SID_str2bin(const char *str, PVFS_SID *sid)
{
    return uuid_parse(str, sid->u);
}

static __inline__ void PVFS_SID_bin2str(const PVFS_SID *sid, char *str)
{
    uuid_unparse(sid->u, str);
}

static __inline__ char *PVFS_SID_str(const PVFS_SID *sid)
{
    static char str[17];
    uuid_unparse(sid->u, str);
    str[17] = 0;
    return str;
}

static __inline__ uint32_t PVFS_SID_hash32(const PVFS_SID *sid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    void *q = &sid;
    int32_t *p = q;
    return p[0] + p[1] + p[2] + p[3];
}

static __inline__ uint64_t PVFS_SID_hash64(const PVFS_SID *sid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    void *q = &sid;
    int64_t *p = q;
    return p[0] + p[1];
}

#define PVFS_SID_encode(d,s) PVFS_SID_cpy(((PVFS_SID *)d),(s))
#define PVFS_SID_decode(d,s) PVFS_SID_cpy((d),((PVFS_SID *)s))


#if 0
typedef struct          /* 5x 64-bit */
{
    PVFS_fs_id fs_id;
    int32_t    num_sid;
    PVFS_OID   oid;
    PVFS_SID   sid[0];
} PVFS_object_group;

static __inline__ PVFS_object_group *NEW_object_group(int num_sid)
{
    return (PVFS_object_group *)malloc(sizeof(PVFS_object_group) +
                                       num_sid * sizeof(PVFS_SID));
}

static __inline__ PVFS_object_group *RESIZE_object_group(PVFS_object_group *grp,
                                                         int num_sid)
{
    return (PVFS_object_group *)realloc(grp, sizeof(PVFS_object_group) +
                                             num_sid * sizeof(PVFS_SID));
}
#endif

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

