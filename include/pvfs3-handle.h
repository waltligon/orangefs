/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS3_HANDLE_H
#define PVFS3_HANDLE_H 1

#include <stdint.h>
#include <uuid/uuid.h>
#include <string.h>

#define OID_SZ 16
#define OID_STR_SZ 36
#define SID_SZ 16
#define SID_STR_SZ 36

#define DW(x,y) (((uint64_t)(x))>>(y))
#define SW(x,y) (((uint32_t)(x))>>(y))

/* size of OID array of size n */
#define OASZ(n) (size_t)((size_t)(n) * sizeof(PVFS_OID))
/* size of SID array of size m */
#define SASZ(m) (size_t)((size_t)(m) * sizeof(PVFS_SID))
/* size of OID array of size n with m SIDs per OID */
#define OSASZ(n,m) (size_t)((size_t)(n) * (OASZ(1) + SASZ((size_t)m)))

/* uuid_t is an unsigned char[16] array and thus passes by reference */

/** Unique identifier for a server on a PVFS3 file system  128-bit */
typedef struct {uuid_t u;} PVFS_SID __attribute__ ((__aligned__ (8)));

#define encode_PVFS_SID(pptr,pbuf) do { \
    memcpy(*(pptr), (pbuf), SID_SZ); \
    *(pptr) += SID_SZ; \
} while (0)
    
#define decode_PVFS_SID(pptr,pbuf) do { \
    memcpy((pbuf), *(pptr), SID_SZ); \
    *(pptr) += SID_SZ; \
} while (0)

#define defree_PVFS_SID(pbuf) do { \
} while(0)
    
/** Unique identifier for an object on a PVFS3 file system  128-bit */
typedef struct {uuid_t u;} PVFS_OID __attribute__ ((__aligned__ (8)));

/* encode_PVFS_handle and decode_PVFS_handle are defined in include/pvfs2-types.h and
 * are defined as encode_PVFS_OID and decode_PVFS_OID
 */

#define encode_PVFS_OID(pptr,pbuf) do { \
    memcpy(*(pptr), (pbuf), OID_SZ); \
    *(pptr) += OID_SZ; \
} while (0)
    
#define decode_PVFS_OID(pptr,pbuf) do { \
    memcpy((pbuf), *(pptr), OID_SZ); \
    *(pptr) += OID_SZ; \
} while (0)

#define defree_PVFS_OID(pbuf) do { \
} while(0)

/** This union makes it easy to work with an array of mixed OID and SID
 * items - they are the same size and format, but this helps keep the
 * compiler happy and makes it clearer what we are doing in some spots.
 * Mostly this is used when we write such lists to disk
 */
typedef union PVFS_ID_u
{
    PVFS_OID oid;
    PVFS_SID sid;
} PVFS_ID;

#define PVFS_HANDLE_NULL_INIT {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}
static const PVFS_OID PVFS_HANDLE_NULL = PVFS_HANDLE_NULL_INIT;
static const PVFS_OID PVFS_HANDLE_HIGH = {{255,255,255,255,255,
                                           255,255,255,255,255,
                                           255,255,255,255,255,255}};
static const PVFS_SID PVFS_SID_NULL = PVFS_HANDLE_NULL_INIT;

    
/* OID Variant definitions */
#define PVFS_OID_variant_ncs UUID_VARIANT_NCS    /*0*/
#define PVFS_OID_variant_dce UUID_VARIANT_DCE    /*1*/
#define PVFS_OID_variant_microsoft UUID_VARIANT_MICROSOFT  /*2*/
#define PVFS_OID_variant_other UUID_VARIANT_OTHER  /*3*/

/* OID Type definitions */
#define PVFS_OID_type_dce_time UUID_TYPE_DCE_TIME   /*1*/
#define PVFS_OID_type_dce_random UUID_TYPE_DCE_RANDOM /*4*/

/* Allow OID constants to be defined */
#ifdef __GNUC__
#define PVFS_OID_DEFINE(name,u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15) \
    static const PVFS_OID name __attribute__ ((unused)) = {u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15}
#else
#endif

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

#define PVFS_OID_EQ(oid1, oid2) (PVFS_OID_cmp((oid1), (oid2)) == 0)
#define PVFS_OID_NE(oid1, oid2) (PVFS_OID_cmp((oid1), (oid2)) != 0)
#define PVFS_OID_GT(oid1, oid2) (PVFS_OID_cmp((oid1), (oid2)) >  0)
#define PVFS_OID_GE(oid1, oid2) (PVFS_OID_cmp((oid1), (oid2)) >= 0)
#define PVFS_OID_LT(oid1, oid2) (PVFS_OID_cmp((oid1), (oid2)) <  0)
#define PVFS_OID_LE(oid1, oid2) (PVFS_OID_cmp((oid1), (oid2)) <= 0)

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
    static char str[OID_STR_SZ + 1];
    uuid_unparse(oid->u, str);
    str[OID_STR_SZ] = 0;
    return str;
}

static __inline__ uint32_t PVFS_OID_hash32(const PVFS_OID *oid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    char *p = (char *)oid;
    return SW(p[0],0)  + SW(p[1],8)  + SW(p[2],16)  + SW(p[3],24)  + 
           SW(p[4],0)  + SW(p[5],8)  + SW(p[6],16)  + SW(p[7],24)  +
           SW(p[8],0)  + SW(p[9],8)  + SW(p[10],16) + SW(p[11],24) +
           SW(p[12],0) + SW(p[13],8) + SW(p[14],16) + SW(p[15],24);
}

static __inline__ uint64_t PVFS_OID_hash64(const PVFS_OID *oid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    char *p = (char *)oid;
    return DW(p[0],0)   + DW(p[1],8)   + DW(p[2],16)  + DW(p[3],24)  + 
           DW(p[4],32)  + DW(p[5],40)  + DW(p[6],48)  + DW(p[7],56)  +
           DW(p[8],0)   + DW(p[9],8)   + DW(p[10],16) + DW(p[11],24) +
           DW(p[12],32) + DW(p[13],40) + DW(p[14],48) + DW(p[15],56);
}

#if 0
#define PVFS_OID_encode(d,s) PVFS_OID_cpy(((PVFS_OID *)d),(s))
#define PVFS_OID_decode(d,s) PVFS_OID_cpy((d),((PVFS_OID *)s))
#define PVFS_OID_defree(d,s) do { } while (0)
#endif

/* SID Variant definitions */
#define PVFS_SID_variant_ncs UUID_VARIANT_NCS    /*0*/
#define PVFS_SID_variant_dce UUID_VARIANT_DCE    /*1*/
#define PVFS_SID_variant_microsoft UUID_VARIANT_MICROSOFT  /*2*/
#define PVFS_SID_variant_other UUID_VARIANT_OTHER  /*3*/

/* SID Type definitions */
#define PVFS_SID_type_dce_time UUID_TYPE_DCE_TIME   /*1*/
#define PVFS_SID_type_dce_random UUID_TYPE_DCE_RANDOM /*4*/

/* Allow SID constants to be defined */
#ifdef __GNUC__
#define PVFS_SID_DEFINE(name,u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15) \
    static const PVFS_SID name __attribute__ ((unused)) = {u0,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11,u12,u13,u14,u15}
#else
#endif

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

#define PVFS_SID_EQ(sid1, sid2) (PVFS_SID_cmp((sid1), (sid2)) == 0)
#define PVFS_SID_NE(sid1, sid2) (PVFS_SID_cmp((sid1), (sid2)) != 0)
#define PVFS_SID_GT(sid1, sid2) (PVFS_SID_cmp((sid1), (sid2)) > 0)
#define PVFS_SID_GE(sid1, sid2) (PVFS_SID_cmp((sid1), (sid2)) >= 0)
#define PVFS_SID_LT(sid1, sid2) (PVFS_SID_cmp((sid1), (sid2)) < 0)
#define PVFS_SID_LE(sid1, sid2) (PVFS_SID_cmp((sid1), (sid2)) <=0)

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
    static char str[SID_STR_SZ + 1];
    uuid_unparse(sid->u, str);
    str[SID_STR_SZ] = 0;
    return str;
}

static __inline__ uint32_t PVFS_SID_hash32(const PVFS_SID *sid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    char *p = (char *)sid;
    return SW(p[0],0)  + SW(p[1],8)  + SW(p[2],16)  + SW(p[3],24)  + 
           SW(p[4],0)  + SW(p[5],8)  + SW(p[6],16)  + SW(p[7],24)  +
           SW(p[8],0)  + SW(p[9],8)  + SW(p[10],16) + SW(p[11],24) +
           SW(p[12],0) + SW(p[13],8) + SW(p[14],16) + SW(p[15],24);
}

static __inline__ uint64_t PVFS_SID_hash64(const PVFS_SID *sid)
{
    /* this might need an alternate implementation on some */
    /* architectures, but is fine in an all-intel world */
    char *p = (char *)sid;
    return DW(p[0],0)   + DW(p[1],8)   + DW(p[2],16)  + DW(p[3],24)  + 
           DW(p[4],32)  + DW(p[5],40)  + DW(p[6],48)  + DW(p[7],56)  +
           DW(p[8],0)   + DW(p[9],8)   + DW(p[10],16) + DW(p[11],24) +
           DW(p[12],32) + DW(p[13],40) + DW(p[14],48) + DW(p[15],56);
}

#if 0
#define PVFS_SID_encode(d,s) PVFS_SID_cpy(((PVFS_SID *)d),(s))
#define PVFS_SID_decode(d,s) PVFS_SID_cpy((d),((PVFS_SID *)s))
#define PVFS_SID_defree(d,s) do { } while (0)
#endif

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

