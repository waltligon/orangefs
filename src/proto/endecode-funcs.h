/*
 * (C) 2003-6 Pete Wyckoff, Ohio Supercomputer Center <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * Defines for macros related to wire encoding and decoding.  Only included
 * by include/pvfs2-encode-stubs.h by core encoding users.
 */
/* NOTE: if you make any changes to the code contained in this file, please
 * update the PVFS2_PROTO_VERSION in pvfs2-req-proto.h accordingly
 */
#ifndef __SRC_PROTO_ENDECODE_FUNCS_H
#define __SRC_PROTO_ENDECODE_FUNCS_H

#include "src/io/bmi/bmi-byteswap.h"
#include <stdint.h>
#ifdef WIN32
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
typedef uint8_t  u_int8_t;

/* typeof not available on Windows */
#define typeof(t)   t
#endif

#include <assert.h>

/*
 * NOTE - Every macro defined here needs to have a stub defined in
 * include/pvfs2-encode-stubs.h
 */

/*
 * Generic macros to define encoding near target structure declarations.
 */

/* basic types */
#define encode_uint64_t(pptr,x) do { \
    *(u_int64_t *) *(pptr) = htobmi64(*(x)); \
    *(pptr) += 8; \
} while (0)
#define decode_uint64_t(pptr,x) do { \
    *(x) = bmitoh64(*(u_int64_t *) *(pptr)); \
    *(pptr) += 8; \
} while (0)
#define defree_uint64_t(x) do { \
} while (0)

#define encode_int64_t(pptr,x) do { \
    *(int64_t *) *(pptr) = htobmi64(*(x)); \
    *(pptr) += 8; \
} while (0)
#define decode_int64_t(pptr,x) do { \
    *(x) = bmitoh64(*(int64_t *) *(pptr)); \
    *(pptr) += 8; \
} while (0)
#define defree_int64_t(x) do { \
} while (0)

#define encode_uint32_t(pptr,x) do { \
    *(u_int32_t *) *(pptr) = htobmi32(*(x)); \
    *(pptr) += 4; \
} while (0)
#define decode_uint32_t(pptr,x) do { \
    *(x) = bmitoh32(*(u_int32_t *) *(pptr)); \
    *(pptr) += 4; \
} while (0)
#define defree_uint32_t(x) do { \
} while (0)

#define encode_int32_t(pptr,x) do { \
    *(int32_t *) *(pptr) = htobmi32(*(x)); \
    *(pptr) += 4; \
} while (0)
#define decode_int32_t(pptr,x) do { \
    *(x) = bmitoh32(*(int32_t *) *(pptr)); \
    *(pptr) += 4; \
} while (0)
#define defree_int32_t(x) do { \
} while (0)

#define encode_uint8_t(pptr,x) do { \
    *(u_int8_t *) *(pptr) = *(x); \
    *(pptr) += 1; \
} while (0)
#define decode_uint8_t(pptr,x) do { \
    *(x) = *(u_int8_t *) *(pptr); \
    *(pptr) += 1; \
} while (0)
#define defree_uint8_t(x) do { \
} while (0)

#define encode_int8_t(pptr,x) do { \
    *(int8_t *) *(pptr) = *(x); \
    *(pptr) += 1; \
} while (0)
#define decode_int8_t(pptr,x) do { \
    *(x) = *(int8_t *) *(pptr); \
    *(pptr) += 1; \
} while (0)
#define defree_int8_t(x) do { \
} while (0)

#define encode_char(pptr,x) do { \
    *(char *) *(pptr) = *(x); \
    *(pptr) += 1; \
} while (0)
#define decode_char(pptr,x) do { \
    *(x) = *(char *) *(pptr); \
    *(pptr) += 1; \
} while (0)
#define defree_char(pbuf) do { \
} while (0)

/* ALREADY DEFINED ABOVE */
#if 0
#define encode_int32_t(pptr,x) do { \
    *(int32_t *) *(pptr) = htobmi32(*(x)); \
    *(pptr) += 4; \
} while (0)
#define decode_int32_t(pptr,x) do { \
    *(x) = bmitoh32(*(int32_t *) *(pptr)); \
    *(pptr) += 4; \
} while (0)
#define defree_int32_t(pbuf) do { \
} while (0)
#endif

/* skip 4 bytes, maybe zeroing them to avoid valgrind getting annoyed */
#ifdef HAVE_VALGRIND_H
#define encode_skip4(pptr,x) do { \
    *(int32_t *) *(pptr) = 0; \
    *(pptr) += 4; \
} while (0)
#else
#define encode_skip4(pptr,x) do { \
    *(pptr) += 4; \
} while (0)
#endif

#define decode_skip4(pptr,x) do { \
    *(pptr) += 4; \
} while (0)
#define defree_skip4(x) do { \
} while (0)

#define encode_align8(pptr,x) do { \
    align8(pptr); \
} while (0)
#define decode_align8(pptr,x) do { \
    align8(pptr); \
} while (0)
#define defree_align8(x) do { \
} while (0)

/*
 * Strings. Decoding just points into existing character data.  This handles
 * NULL strings too, just encoding the length and four zero bytes.  The
 * valgrind version zeroes out any padding.
 */
#ifdef HAVE_VALGRIND_H
#define encode_string(pptr,pbuf) do { \
    u_int32_t len = 0; \
    if (*pbuf) \
	    len = strlen(*pbuf); \
    *(u_int32_t *) *(pptr) = htobmi32(len); \
    if (len) { \
	    memcpy(*(pptr) + 4, *pbuf, len + 1); \
	    int pad = roundup8(4 + len + 1) - (4 + len + 1); \
	    *(pptr) += roundup8(4 + len + 1); \
	    memset(*(pptr)-pad, 0, pad); \
    } else { \
	    *(u_int32_t *) (*(pptr) + 4) = 0; \
	    *(pptr) += 8; \
    } \
} while (0)
#else
#define encode_string(pptr,pbuf) do { \
    u_int32_t len = 0; \
    if (*pbuf) \
	    len = strlen(*pbuf); \
    *(u_int32_t *) *(pptr) = htobmi32(len); \
    if (len) { \
	    memcpy(*(pptr) + 4, *pbuf, len + 1); \
	    *(pptr) += roundup8(4 + len + 1); \
    } else { \
	    *(u_int32_t *) (*(pptr) + 4) = 0; \
	    *(pptr) += 8; \
    } \
} while (0)
#endif

/* determines how much protocol space a string encoding will consume */
#define encode_string_size_check(pbuf) (strlen(*pbuf) + 5)

#define decode_string(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    *pbuf = *(pptr) + 4; \
    *(pptr) += roundup8(4 + len + 1); \
} while (0)
#define defree_string(pbuf) do { \
} while (0)

/* odd variation, space exists in some structure, must copy-in string */
#define encode_here_string(pptr,pbuf) encode_string(pptr,pbuf)
#define decode_here_string(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    memcpy(pbuf, *(pptr) + 4, len + 1); \
    *(pptr) += roundup8(4 + len + 1); \
} while (0)
#define defree_here_string(pbuf) do { \
} while (0)

/* keyvals; a lot like strings; decoding points existing character data */
/* BTW we are skipping the read_sz field - keep that in mind */
#define encode_PVFS_ds_keyval(pptr,pbuf) do { \
    u_int32_t len = ((PVFS_ds_keyval *)pbuf)->buffer_sz; \
    *(u_int32_t *) *(pptr) = htobmi32(len); \
    if (len){memcpy(*(pptr)+4, ((PVFS_ds_keyval *)pbuf)->buffer, len);} \
    *(pptr) += roundup8(4 + len); \
} while (0)
#define decode_PVFS_ds_keyval(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    ((PVFS_ds_keyval *)pbuf)->buffer_sz = len; \
    ((PVFS_ds_keyval *)pbuf)->buffer = *(pptr) + 4; \
    *(pptr) += roundup8(4 + len); \
} while (0)
#define defree_PVFS_ds_keyval(pbuf) do { \
} while (0)

/*
 * Type maps are put near the type definitions, except for this special one.
 *
 * Please remember when changing a fundamental type, e.g. PVFS_size, to update
 * also the set of #defines that tell the encoder what its type really is.
 */
#define encode_enum(pptr,pbuf) encode_int32_t(pptr,pbuf)
#define decode_enum(pptr,pbuf) decode_int32_t(pptr,pbuf)
#define defree_enum defree_int32_t

/* V3 These are supposed to be next to the definition of each type in
 * the headers - include/pvfs2-types.h
 */

#define encode_PVFS_signature(pptr,x) encode_char(pptr,x)
#define decode_PVFS_signature(pptr,x) decode_char(pptr,x)
#define defree_PVFS_signature(x) do { \
} while (0)

#define encode_PVFS_cert_data(pptr,x) encode_char(pptr,x)
#define decode_PVFS_cert_data(pptr,x) decode_char(pptr,x)
#define defree_PVFS_cert_data(x) do { \
} while (0)

#define encode_PVFS_key_data(pptr,x) encode_char(pptr,x)
#define decode_PVFS_key_data(pptr,x) decode_char(pptr,x)
#define defree_PVFS_key_data(x) do { \
} while (0)

/* memory alloc and free, just for decoding */
#if 0
/* this is for debugging, if you want to see what is malloc'd */
static inline void *decode_malloc (int n) {
	void *p;
	if (n>0)
		p = malloc(n);
	else
		p = (void *)0;
	printf("decode malloc %d bytes: %p\n",n,p);
	return p;
}
/* this is for debugging, if you want to see what is free'd */
static inline void decode_free (void *p) {
	printf("decode free: %p\n",p);
	free(p);
}
#else
#define decode_malloc(n) ((n) != 0 ? malloc(n) : 0)
#define decode_free(n) free(n)
#endif

/*
 * These wrappers define functions to do the encoding of the types or
 * structures they describe.  Please remember to update the empty stub versions
 * of these routines in include/pvfs2-encode-stubs.h, although the compiler
 * will certainly complain too.
 *
 * Note that decode can not take a const since we point into the
 * undecoded buffer for strings.
 */
#define endecode_fields_1_generic(name, sname, t1, x1) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
}

#define endecode_fields_1(name, t1, x1) \
    endecode_fields_1_generic(name, name, t1, x1)
#define endecode_fields_1_struct(name, t1, x1) \
    endecode_fields_1_generic(name, struct name, t1, x1)

/*****************************/

#define endecode_fields_2_generic(name, sname, t1, x1, t2, x2) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
}

#define endecode_fields_2(name, t1, x1, t2, x2) \
    endecode_fields_2_generic(name, name, t1, x1, t2, x2)
#define endecode_fields_2_struct(name, t1, x1, t2, x2) \
    endecode_fields_2_generic(name, struct name, t1, x1, t2, x2)

/*****************************/

#define endecode_fields_3_generic(name, sname, t1, x1, t2, x2, t3, x3) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
}

#define endecode_fields_3(name, t1, x1, t2, x2, t3, x3) \
    endecode_fields_3_generic(name, name, t1, x1, t2, x2, t3, x3)
#define endecode_fields_3_struct(name, t1, x1, t2, x2, t3, x3) \
    endecode_fields_3_generic(name, struct name, t1, x1, t2, x2, t3, x3)

/*****************************/

#define endecode_fields_4_generic(name, sname, t1, x1, t2, x2, t3, x3, t4, x4) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
}

#define endecode_fields_4(name, t1, x1, t2, x2, t3, x3, t4, x4) \
    endecode_fields_4_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4)
#define endecode_fields_4_struct(name, t1, x1, t2, x2, t3, x3, t4, x4) \
    endecode_fields_4_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4)

/*****************************/

#define endecode_fields_5_generic(name, sname, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
}

#define endecode_fields_5(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
    endecode_fields_5_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5)
#define endecode_fields_5_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
    endecode_fields_5_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5)

/*****************************/

#define endecode_fields_6_generic(name, sname, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
}

#define endecode_fields_6(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6) \
    endecode_fields_6_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6)

#define endecode_fields_6_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6) \
    endecode_fields_6_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6)

/*****************************/

#define endecode_fields_7_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
}

#define endecode_fields_7(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7) \
    endecode_fields_7_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7) 

#define endecode_fields_7_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7) \
    endecode_fields_7_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7) 

/*****************************/

#define endecode_fields_8_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
 defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
}

#define endecode_fields_8(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7, t8, x8) \
    endecode_fields_8_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7, t8, x8) 

#define endecode_fields_8_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7, t8, x8) \
    endecode_fields_8_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7, t8, x8) 

/*****************************/

#define endecode_fields_9_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
}

#define endecode_fields_9(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) \
    endecode_fields_9_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) 

#define endecode_fields_9_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) \
    endecode_fields_9_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) 

/*****************************/

#define endecode_fields_10_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
    encode_##t10(pptr, &x->x10); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
    decode_##t10(pptr, &x->x10); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
    defree_##t10(&x->x10); \
}

#define endecode_fields_10(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) \
    endecode_fields_10_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) 

#define endecode_fields_10_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) \
    endecode_fields_10_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) 

/*****************************/

#define endecode_fields_11_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
    encode_##t10(pptr, &x->x10); \
    encode_##t11(pptr, &x->x11); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
    decode_##t10(pptr, &x->x10); \
    decode_##t11(pptr, &x->x11); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
    defree_##t10(&x->x10); \
    defree_##t11(&x->x11); \
}

#define endecode_fields_11(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) \
    endecode_fields_11_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) 

#define endecode_fields_11_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) \
    endecode_fields_11_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) 

/*****************************/

#define endecode_fields_12_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
    encode_##t10(pptr, &x->x10); \
    encode_##t11(pptr, &x->x11); \
    encode_##t12(pptr, &x->x12); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
    decode_##t10(pptr, &x->x10); \
    decode_##t11(pptr, &x->x11); \
    decode_##t12(pptr, &x->x12); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
    defree_##t10(&x->x10); \
    defree_##t11(&x->x11); \
    defree_##t12(&x->x12); \
}

#define endecode_fields_12(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) \
    endecode_fields_12_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) 

#define endecode_fields_12_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) \
    endecode_fields_12_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) 

/*****************************/

#define endecode_fields_15_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7, \
    t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
    encode_##t10(pptr, &x->x10); \
    encode_##t11(pptr, &x->x11); \
    encode_##t12(pptr, &x->x12); \
    encode_##t13(pptr, &x->x13); \
    encode_##t14(pptr, &x->x14); \
    encode_##t15(pptr, &x->x15); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
    decode_##t10(pptr, &x->x10); \
    decode_##t11(pptr, &x->x11); \
    decode_##t12(pptr, &x->x12); \
    decode_##t13(pptr, &x->x13); \
    decode_##t14(pptr, &x->x14); \
    decode_##t15(pptr, &x->x15); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
    defree_##t10(&x->x10); \
    defree_##t11(&x->x11); \
    defree_##t12(&x->x12); \
    defree_##t13(&x->x13); \
    defree_##t14(&x->x14); \
    defree_##t15(&x->x15); \
}

#define endecode_fields_15(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15) \
    endecode_fields_15_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15) 

#define endecode_fields_15_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15) \
    endecode_fields_15_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15) 

/*****************************/

#define endecode_fields_16_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7, \
    t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
    encode_##t10(pptr, &x->x10); \
    encode_##t11(pptr, &x->x11); \
    encode_##t12(pptr, &x->x12); \
    encode_##t13(pptr, &x->x13); \
    encode_##t14(pptr, &x->x14); \
    encode_##t15(pptr, &x->x15); \
    encode_##t16(pptr, &x->x16); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
    decode_##t10(pptr, &x->x10); \
    decode_##t11(pptr, &x->x11); \
    decode_##t12(pptr, &x->x12); \
    decode_##t13(pptr, &x->x13); \
    decode_##t14(pptr, &x->x14); \
    decode_##t15(pptr, &x->x15); \
    decode_##t16(pptr, &x->x16); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
    defree_##t10(&x->x10); \
    defree_##t11(&x->x11); \
    defree_##t12(&x->x12); \
    defree_##t13(&x->x13); \
    defree_##t14(&x->x14); \
    defree_##t15(&x->x15); \
    defree_##t16(&x->x16); \
}

#define endecode_fields_16(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16) \
    endecode_fields_16_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16) 

#define endecode_fields_16_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16) \
    endecode_fields_16_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16) 

/*****************************/

#define endecode_fields_17_generic(name,sname,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7, \
    t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16,t17,x17) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
    encode_##t9(pptr, &x->x9); \
    encode_##t10(pptr, &x->x10); \
    encode_##t11(pptr, &x->x11); \
    encode_##t12(pptr, &x->x12); \
    encode_##t13(pptr, &x->x13); \
    encode_##t14(pptr, &x->x14); \
    encode_##t15(pptr, &x->x15); \
    encode_##t16(pptr, &x->x16); \
    encode_##t17(pptr, &x->x17); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
    decode_##t10(pptr, &x->x10); \
    decode_##t11(pptr, &x->x11); \
    decode_##t12(pptr, &x->x12); \
    decode_##t13(pptr, &x->x13); \
    decode_##t14(pptr, &x->x14); \
    decode_##t15(pptr, &x->x15); \
    decode_##t16(pptr, &x->x16); \
    decode_##t17(pptr, &x->x17); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    defree_##t8(&x->x8); \
    defree_##t9(&x->x9); \
    defree_##t10(&x->x10); \
    defree_##t11(&x->x11); \
    defree_##t12(&x->x12); \
    defree_##t13(&x->x13); \
    defree_##t14(&x->x14); \
    defree_##t15(&x->x15); \
    defree_##t16(&x->x16); \
    defree_##t17(&x->x17); \
}

#define endecode_fields_17(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16,t17,x17) \
    endecode_fields_17_generic(name, name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16,t17,x17) 

#define endecode_fields_17_struct(name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16,t17,x17) \
    endecode_fields_17_generic(name, struct name, t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16,t17,x17) 

/*****************************/

/* ones with arrays that are allocated in the decode */

/* one field then one array */
#define endecode_fields_1a_generic(name, sname, t1, x1, tn1, n1, ta1, a1) \
static inline void encode_##name(char **pptr, const sname *x) { typeof(tn1) i; \
    encode_##t1(pptr, &x->x1); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, sname *x) { typeof(tn1) i; \
    decode_##t1(pptr, &x->x1); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    if (x->n1 > 0) decode_free(x->a1); \
}

/* one field then two arrays */
#define endecode_fields_1aa_generic(name, sname, t1, x1, tn1, n1, ta1, a1, ta2, a2) \
static inline void encode_##name(char **pptr, const sname *x) { typeof(tn1) i; \
    encode_##t1(pptr, &x->x1); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, sname *x) { typeof(tn1) i; \
    decode_##t1(pptr, &x->x1); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
}

#define endecode_fields_1a(name, t1, x1, tn1, n1, ta1, a1) \
    endecode_fields_1a_generic(name, name, t1, x1, tn1, n1, ta1, a1)
#define endecode_fields_1a_struct(name, t1, x1, tn1, n1, ta1, a1) \
    endecode_fields_1a_generic(name, struct name, t1, x1, tn1, n1, ta1, a1)

#define endecode_fields_1aa(name, t1, x1, tn1, n1, ta1, a1, ta2, a2) \
    endecode_fields_1aa_generic(name, name, t1, x1, tn1, n1, ta1, a1, ta2, a2)
#define endecode_fields_1aa_struct(name, t1, x1, tn1, n1, ta1, a1, ta2, a2) \
    endecode_fields_1aa_generic(name, struct name, t1, x1, tn1, n1, ta1, a1, ta2, a2)

/* one field, and array, another field, another array - a special case */
#define endecode_fields_1a1a_struct(name, t1, x1, tn1, n1, ta1, a1, t2, x2, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) { int n; n = i; \
	    encode_##ta1(pptr, &(x)->a1[n]); } \
    encode_##t2(pptr, &x->x2); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) { int n; n = i; \
	    encode_##ta2(pptr, &(x)->a2[n]); } \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t2(&x->x2); \
    if (x->n2 > 0) decode_free(x->a2); \
}

/* one field, and array, another field, another array - a special case */
#define endecode_fields_1a1a1a_struct(name, t1, x1, tn1, n1, ta1, a1, t2, x2, tn2, n2, ta2, a2, t3, x3, tn3, n3, ta3, a3) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) { int n; n = i; \
	    encode_##ta1(pptr, &(x)->a1[n]); } \
    encode_##t2(pptr, &x->x2); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) { int n; n = i; \
	    encode_##ta2(pptr, &(x)->a2[n]); } \
    encode_##t3(pptr, &x->x3); \
    encode_##tn3(pptr, &x->n3); \
    for (i=0; i<x->n3; i++) { int n; n = i; \
	    encode_##ta3(pptr, &(x)->a3[n]); } \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn3(pptr, &x->n3); \
    x->a3 = decode_malloc(x->n3 * sizeof(*x->a3)); \
    for (i=0; i<x->n3; i++) \
	    decode_##ta3(pptr, &(x)->a3[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t2(&x->x2); \
    if (x->n2 > 0) decode_free(x->a2); \
    defree_##t3(&x->x3); \
    if (x->n3 > 0) decode_free(x->a3); \
}

/* 2 fields, then an array */
#define endecode_fields_2a_generic(name, sname, t1, x1, t2, x2, tn1, n1, ta1, a1) \
static inline void encode_##name(char **pptr, const sname *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, sname *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void defree_##name(sname *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    if (x->n1 > 0) decode_free(x->a1); \
}

#define endecode_fields_2a(name, t1, x1, t2, x2, tn1, n1, ta1, a1) \
    endecode_fields_2a_generic(name, name, t1, x1, t2, x2, tn1, n1, ta1, a1)
#define endecode_fields_2a_struct(name, t1, x1, t2, x2, tn1, n1, ta1, a1) \
    endecode_fields_2a_generic(name, struct name, t1, x1, t2, x2, tn1, n1, ta1, a1)

/* special case where we have two arrays of the same size after 2 fields */
#define endecode_fields_2aa_struct(name, t1, x1, t2, x2, tn1, n1, ta1, a1, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    for (i=0; i<x->n1; i++) \
    	encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
}

/* special case where we have two arrays of different sizes after 2 fields */
#define endecode_fields_2a1a_struct(name, t1, x1, t2, x2, tn1, n1, ta1, a1, t3, x3, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t3(pptr, &x->x3); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t3(&x->x3); \
    if (x->n2 > 0) decode_free(x->a2); \
}

/* special case where we have one array of a different size and then two arrays of the same size after 2 fields */
#define endecode_fields_2a1aa_struct(name, t1, x1, t2, x2, tn1, n1, ta1, a1, t3, x3, tn2, n2, ta2, a2, ta3, a3) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t3(pptr, &x->x3); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta2(pptr, &(x)->a2[i]); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta3(pptr, &(x)->a3[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
    x->a3 = decode_malloc(x->n2 * sizeof(*x->a3)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta3(pptr, &(x)->a3[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t3(&x->x3); \
    if (x->n2 > 0) decode_free(x->a2); \
    if (x->n2 > 0) decode_free(x->a3); \
}

/* special case where we have 2 fields then 1 array of a different size and 
 * then 2 fields then 3 arrays of the same size */
#define endecode_fields_2a2aaa_struct(name, t1, x1, t2, x2, tn1, n1, ta1, a1, t3, x3, t4, x4, tn2, n2, ta2, a2, ta3, a3, ta4, a4) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta2(pptr, &(x)->a2[i]); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta3(pptr, &(x)->a3[i]); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta4(pptr, &(x)->a4[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
    x->a3 = decode_malloc(x->n2 * sizeof(*x->a3)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta3(pptr, &(x)->a3[i]); \
    x->a4 = decode_malloc(x->n2 * sizeof(*x->a4)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta4(pptr, &(x)->a4[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    if (x->n2 > 0) decode_free(x->a2); \
    if (x->n2 > 0) decode_free(x->a3); \
    if (x->n2 > 0) decode_free(x->a4); \
}

/* special case where we have 2 fields then 1 array of a different size and 
 * then 3 fields then 3 arrays of the same size */
#define endecode_fields_2a3aaa_struct(name, t1, x1, t2, x2, tn1, n1, ta1, a1, t3, x3, t4, x4, t5, x5, tn2, n2, ta2, a2, ta3, a3, ta4, a4) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta2(pptr, &(x)->a2[i]); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta3(pptr, &(x)->a3[i]); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta4(pptr, &(x)->a4[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta2(pptr, &(x)->a2[i]); \
    x->a3 = decode_malloc(x->n2 * sizeof(*x->a3)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta3(pptr, &(x)->a3[i]); \
    x->a4 = decode_malloc(x->n2 * sizeof(*x->a4)); \
    for (i=0; i<x->n2; i++) \
	    decode_##ta4(pptr, &(x)->a4[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    if (x->n2 > 0) decode_free(x->a2); \
    if (x->n2 > 0) decode_free(x->a3); \
    if (x->n2 > 0) decode_free(x->a4); \
}

/* 3 fields, then an array */
#define endecode_fields_3a_struct(name, t1, x1, t2, x2, t3, x3, tn1, n1, ta1, a1) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	    decode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    if (x->n1 > 0) decode_free(x->a1); \
}

/* 3 fields, then an array, then one field, then an array */
#define endecode_fields_3a1a_struct(name, t1, x1, t2, x2, t3, x3, tn1, n1, ta1, a1, t4, x4, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	    encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t4(pptr, &x->x4); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
	    encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t4(pptr, &x->x4); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t4(&x->x4); \
    if (x->n2 > 0) decode_free(x->a2); \
}

/* special case where we have two arrays of the same size after 4 fields */
#define endecode_fields_4aa_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, tn1, n1, ta1, a1, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	encode_##ta1(pptr, &(x)->a1[i]); \
    for (i=0; i<x->n1; i++) \
	encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
}

/* special case where we have two arrays of the same size after 4 fields */
#define endecode_fields_4a1a_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, tn1, n1, ta1, a1, t5, x5, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
        encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t5(&x->x5); \
    if (x->n2 > 0) decode_free(x->a2); \
}
/* special case where we have three arrays of the same size after 4 
fields */
#define endecode_fields_4aaa_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, tn1, n1, ta1, a1, ta2, a2, ta3, a3) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
     encode_##t1(pptr, &x->x1); \
     encode_##t2(pptr, &x->x2); \
     encode_##t3(pptr, &x->x3); \
     encode_##t4(pptr, &x->x4); \
     encode_##tn1(pptr, &x->n1); \
     for (i=0; i<x->n1; i++) \
            encode_##ta1(pptr, &(x)->a1[i]); \
     for (i=0; i<x->n1; i++) \
            encode_##ta2(pptr, &(x)->a2[i]); \
     for (i=0; i<x->n1; i++) \
            encode_##ta3(pptr, &(x)->a3[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
     decode_##t1(pptr, &x->x1); \
     decode_##t2(pptr, &x->x2); \
     decode_##t3(pptr, &x->x3); \
     decode_##t4(pptr, &x->x4); \
     decode_##tn1(pptr, &x->n1); \
     x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
     for (i=0; i<x->n1; i++) \
            decode_##ta1(pptr, &(x)->a1[i]); \
     x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
     for (i=0; i<x->n1; i++) \
            decode_##ta2(pptr, &(x)->a2[i]); \
     x->a3 = decode_malloc(x->n1 * sizeof(*x->a3)); \
     for (i=0; i<x->n1; i++) \
            decode_##ta3(pptr, &(x)->a3[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
    if (x->n1 > 0) decode_free(x->a3); \
}

/* 4 fields, then an array */
#define endecode_fields_4a_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, tn1,n1,ta1,a1) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    if (x->n1 > 0) decode_free(x->a1); \
}

/* 5 fields, then an array */
#define endecode_fields_5a_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, tn1,n1,ta1,a1) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    if (x->n1 > 0) decode_free(x->a1); \
}

#ifdef WIN32
#define DEFINE_STATIC_ENDECODE_FUNCS(__name__, __type__) \
static void encode_func_##__name__(char **pptr, void *x) \
{ \
    encode_##__name__(pptr, (__type__ *)x); \
} \
static void decode_func_##__name__(char **pptr, void *x) \
{ \
    decode_##__name__(pptr, (__type__ *)x); \
} \
static void defree_func_##__name__(void *x) \
{ \
    defree_##__name__((__type__ *)x); \
}
#else
#define DEFINE_STATIC_ENDECODE_FUNCS(__name__, __type__) \
__attribute__((unused)) \
static void encode_func_##__name__(char **pptr, void *x) \
{ \
    encode_##__name__(pptr, (__type__ *)x); \
} \
__attribute__((unused)) \
static void decode_func_##__name__(char **pptr, void *x) \
{ \
    decode_##__name__(pptr, (__type__ *)x); \
} \
__attribute__((unused)) \
static void defree_func_##__name__(void *x) \
{ \
    defree_##__name__((__type__ *)x); \
}
#endif

/* union of two types with an enum to select proper type */
#define encode_enum_union_2_struct(name, ename, uname, ut1, un1, en1, ut2, un2, en2)                         \
static inline void encode_##name(char **pptr, const struct name *x)           \
{                                                                             \
    encode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: encode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: encode_##ut2(pptr, &x->uname.un2); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
}                                                                             \
static inline void decode_##name(char **pptr, struct name *x)                 \
{                                                                             \
    decode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: decode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: decode_##ut2(pptr, &x->uname.un2); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
}                                                                             \
static inline void defree_##name(struct name *x) {                            \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: defree_##ut1(&x->uname.un1); break;                         \
        case en2: defree_##ut2(&x->uname.un1); break;                         \
        default: assert(0);                                                   \
    }                                                                         \
}

/* union of three types with an enum to select proper type */
#define encode_enum_union_3_struct(name, ename, uname, ut1, un1, en1, ut2, un2, en2, ut3, un3, en3)    \
static inline void encode_##name(char **pptr, const struct name *x)           \
{                                                                             \
    encode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: encode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: encode_##ut2(pptr, &x->uname.un2); break;                   \
        case en3: encode_##ut3(pptr, &x->uname.un3); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
}                                                                             \
static inline void decode_##name(char **pptr, struct name *x)                 \
{                                                                             \
    decode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: decode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: decode_##ut2(pptr, &x->uname.un2); break;                   \
        case en3: decode_##ut3(pptr, &x->uname.un3); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
}                                                                             \
static inline void defree_##name(struct name *x) {                            \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: defree_##ut1(&x->uname.un1); break;                         \
        case en2: defree_##ut2(&x->uname.un1); break;                         \
        case en3: defree_##ut3(&x->uname.un1); break;                         \
        default: assert(0);                                                   \
    }                                                                         \
}

/* union of four types with an enum to select proper type */
#define encode_enum_union_4_struct(name, ename, uname, ut1, un1, en1, ut2, un2, en2, ut3, un3, en3, ut4, un4, en4)    \
static inline void encode_##name(char **pptr, const struct name *x)           \
{                                                                             \
    encode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: encode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: encode_##ut2(pptr, &x->uname.un2); break;                   \
        case en3: encode_##ut3(pptr, &x->uname.un3); break;                   \
        case en4: encode_##ut4(pptr, &x->uname.un4); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
}                                                                             \
static inline void decode_##name(char **pptr, struct name *x)                 \
{                                                                             \
    decode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: decode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: decode_##ut2(pptr, &x->uname.un2); break;                   \
        case en3: decode_##ut3(pptr, &x->uname.un3); break;                   \
        case en4: decode_##ut4(pptr, &x->uname.un4); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
}                                                                             \
static inline void defree_##name(struct name *x) {                            \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: defree_##ut1(&x->uname.un1); break;                         \
        case en2: defree_##ut2(&x->uname.un2); break;                         \
        case en3: defree_##ut3(&x->uname.un3); break;                         \
        case en4: defree_##ut4(&x->uname.un4); break;                         \
        default: assert(0);                                                   \
    }                                                                         \
}

/* 3 fields, then an array, then 2 fields, then an array */
#define endecode_fields_3a2a_struct(name, t1, x1, t2, x2, t3, x3, tn1, n1, ta1, a1, t4, x4, t5, x5, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i;  \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##tn1(pptr, &x->n1); \
    if (x->n1 > 0) \
        for (i=0; i<(int)(x->n1); i++) \
            encode_##ta1(pptr, &(x)->a1[i]); \
    align8(pptr); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn2(pptr, &x->n2); \
    if (x->n2 > 0) \
        for (i=0; i<(int)(x->n2); i++) \
            encode_##ta2(pptr, &(x)->a2[i]); \
    align8(pptr); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn1(pptr, &x->n1); \
    if (x->n1 > 0) \
    { \
        x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
        for (i=0; i<(int)(x->n1); i++) \
            decode_##ta1(pptr, &(x)->a1[i]); \
    } \
    else \
        x->a1 = NULL; \
    align8(pptr); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn2(pptr, &x->n2); \
    if (x->n2 > 0) \
    { \
        x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
        for (i=0; i<(int)(x->n2); i++) \
            decode_##ta2(pptr, &(x)->a2[i]); \
    } \
    else \
        x->a2 = NULL; \
    align8(pptr); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    if (x->n2 > 0) decode_free(x->a2); \
}

#define endecode_fields_3a2a1_struct(name, t1, x1, t2, x2, t3, x3, tn1, n1, ta1, a1, t4, x4, t5, x5, tn2, n2, ta2, a2, t6, x6) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##tn1(pptr, &x->n1); \
    if (x->n1 > 0) \
        for (i=0; i<(int)(x->n1); i++) \
            encode_##ta1(pptr, &(x)->a1[i]); \
    align8(pptr); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn2(pptr, &x->n2); \
    if (x->n2 > 0) \
        for (i=0; i<(int)(x->n2); i++) \
            encode_##ta2(pptr, &(x)->a2[i]); \
    align8(pptr); \
    encode_##t6(pptr, &x->x6); \
    align8(pptr); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##tn1(pptr, &x->n1); \
    if (x->n1 > 0) \
    { \
        x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
        for (i=0; i<(int)(x->n1); i++) \
            decode_##ta1(pptr, &(x)->a1[i]); \
    } \
    else \
        x->a1 = NULL; \
    align8(pptr); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn2(pptr, &x->n2); \
    if (x->n2 > 0) \
    { \
        x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
        for (i=0; i<(int)(x->n2); i++) \
            decode_##ta2(pptr, &(x)->a2[i]); \
    } \
    else \
        x->a2 = NULL; \
    align8(pptr); \
    decode_##t6(pptr, &x->x6); \
    align8(pptr); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    if (x->n2 > 0) decode_free(x->a2); \
    defree_##t6(&x->x6); \
}

/* special case where we have two arrays of the same size after 5 fields */
#define endecode_fields_5aa_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, tn1, n1, ta1, a1, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
    for (i=0; i<x->n1; i++) \
        encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
}

/* special case where we have two arrays of different size after 5 fields */
#define endecode_fields_5a1a_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, tn1, n1, ta1, a1, t6, x6, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##t6(pptr, &x->x6); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
        encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##t6(pptr, &x->x6); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2* sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    if (x->n1 > 0) decode_free(x->a1); \
    defree_##t6(&x->x6); \
    if (x->n2 > 0) decode_free(x->a2); \
}

/* special case where we have two arrays of the same size after 6 fields */
#define endecode_fields_6aa_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, tn1, n1, ta1, a1, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
    for (i=0; i<x->n1; i++) \
        encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
}

/* special case where we have an array of one size, then two arrays of the same size after 6 fields */
#define endecode_fields_6a2a_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, tn1, n1, tn2, n2, ta1, a1, ta2, a2, ta3, a3) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1);           \
    encode_##t2(pptr, &x->x2);           \
    encode_##t3(pptr, &x->x3);           \
    encode_##t4(pptr, &x->x4);           \
    encode_##t5(pptr, &x->x5);           \
    encode_##t6(pptr, &x->x6);           \
    encode_##tn1(pptr, &x->n1);          \
    encode_##tn2(pptr, &x->n2);          \
    for (i = 0; i < x->n2; i++)          \
        encode_##ta1(pptr, &(x)->a1[i]); \
    for (i = 0; i < x->n1; i++)          \
        encode_##ta2(pptr, &(x)->a2[i]); \
    for (i = 0; i < x->n1; i++)          \
        encode_##ta3(pptr, &(x)->a3[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1);                     \
    decode_##t2(pptr, &x->x2);                     \
    decode_##t3(pptr, &x->x3);                     \
    decode_##t4(pptr, &x->x4);                     \
    decode_##t5(pptr, &x->x5);                     \
    decode_##t6(pptr, &x->x6);                     \
    decode_##tn1(pptr, &x->n1);                    \
    decode_##tn2(pptr, &x->n2);                    \
    x->a1 = decode_malloc(x->n2 * sizeof(*x->a1)); \
    for (i=0; i<x->n2; i++)                        \
        decode_##ta1(pptr, &(x)->a1[i]);           \
    x->a2 = decode_malloc(x->n1 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++)                        \
        decode_##ta2(pptr, &(x)->a2[i]);           \
    x->a3 = decode_malloc(x->n1 * sizeof(*x->a3)); \
    for (i=0; i<x->n1; i++)                        \
        decode_##ta2(pptr, &(x)->a3[i]);           \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    if (x->n2 > 0) decode_free(x->a1); \
    if (x->n1 > 0) decode_free(x->a2); \
    if (x->n1 > 0) decode_free(x->a3); \
}

/* special case where we have two arrays of different sizes after 7 fields */
#define endecode_fields_7a1a_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6, t7, x7, tn1, n1, ta1, a1, t8, x8, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
        encode_##ta1(pptr, &(x)->a1[i]); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) \
        encode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n1; i++) \
        decode_##ta2(pptr, &(x)->a2[i]); \
} \
static inline void defree_##name(struct name *x) { \
    defree_##t1(&x->x1); \
    defree_##t2(&x->x2); \
    defree_##t3(&x->x3); \
    defree_##t4(&x->x4); \
    defree_##t5(&x->x5); \
    defree_##t6(&x->x6); \
    defree_##t7(&x->x7); \
    if (x->n1 > 0) decode_free(x->a1); \
    if (x->n2 > 0) decode_free(x->a2); \
}
#endif  /* __SRC_PROTO_ENDECODE_FUNCS_H */

