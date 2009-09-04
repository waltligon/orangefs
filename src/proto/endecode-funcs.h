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
    *(u_int64_t*) *(pptr) = htobmi64(*(x)); \
    *(pptr) += 8; \
} while (0)
#define decode_uint64_t(pptr,x) do { \
    *(x) = bmitoh64(*(u_int64_t*) *(pptr)); \
    *(pptr) += 8; \
} while (0)

#define encode_int64_t(pptr,x) do { \
    *(int64_t*) *(pptr) = htobmi64(*(x)); \
    *(pptr) += 8; \
} while (0)
#define decode_int64_t(pptr,x) do { \
    *(x) = bmitoh64(*(int64_t*) *(pptr)); \
    *(pptr) += 8; \
} while (0)

#define encode_uint32_t(pptr,x) do { \
    *(u_int32_t*) *(pptr) = htobmi32(*(x)); \
    *(pptr) += 4; \
} while (0)
#define decode_uint32_t(pptr,x) do { \
    *(x) = bmitoh32(*(u_int32_t*) *(pptr)); \
    *(pptr) += 4; \
} while (0)

#define encode_int32_t(pptr,x) do { \
    *(int32_t*) *(pptr) = htobmi32(*(x)); \
    *(pptr) += 4; \
} while (0)
#define decode_int32_t(pptr,x) do { \
    *(x) = bmitoh32(*(int32_t*) *(pptr)); \
    *(pptr) += 4; \
} while (0)

/* skip 4 bytes, maybe zeroing them to avoid valgrind getting annoyed */
#ifdef HAVE_VALGRIND_H
#define encode_skip4(pptr,x) do { \
    *(int32_t*) *(pptr) = 0; \
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

/*
 * Strings. Decoding just points into existing character data.  This handles
 * NULL strings too, just encoding the length and a single zero byte.  The
 * valgrind version zeroes out any padding.
 */
#ifdef HAVE_VALGRIND_H
#define encode_string(pptr,pbuf) do { \
    u_int32_t len = 0; \
    if (*pbuf) \
	len = strlen(*pbuf); \
    *(u_int32_t *) *(pptr) = htobmi32(len); \
    if (len) { \
	memcpy(*(pptr)+4, *pbuf, len+1); \
	int pad = roundup8(4 + len + 1) - (4 + len + 1); \
	*(pptr) += roundup8(4 + len + 1); \
	memset(*(pptr)-pad, 0, pad); \
    } else { \
	*(u_int32_t *) (*(pptr)+4) = 0; \
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
	memcpy(*(pptr)+4, *pbuf, len+1); \
	*(pptr) += roundup8(4 + len + 1); \
    } else { \
        *(u_int32_t *) (*(pptr)+4) = 0; \
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

/* odd variation, space exists in some structure, must copy-in string */
#define encode_here_string(pptr,pbuf) encode_string(pptr,pbuf)
#define decode_here_string(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    memcpy(pbuf, *(pptr) + 4, len + 1); \
    *(pptr) += roundup8(4 + len + 1); \
} while (0)


/* keyvals; a lot like strings; decoding points existing character data */
/* BTW we are skipping the read_sz field - keep that in mind */
#define encode_PVFS_ds_keyval(pptr,pbuf) do { \
    u_int32_t len = ((PVFS_ds_keyval *)pbuf)->buffer_sz; \
    *(u_int32_t *) *(pptr) = htobmi32(len); \
    memcpy(*(pptr)+4, ((PVFS_ds_keyval *)pbuf)->buffer, len); \
    *(pptr) += roundup8(4 + len); \
} while (0)
#define decode_PVFS_ds_keyval(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    ((PVFS_ds_keyval *)pbuf)->buffer_sz = len; \
    ((PVFS_ds_keyval *)pbuf)->buffer = *(pptr) + 4; \
    *(pptr) += roundup8(4 + len); \
} while (0)

/*
 * Type maps are put near the type definitions, except for this special one.
 *
 * Please remember when changing a fundamental type, e.g. PVFS_size, to update
 * also the set of #defines that tell the encoder what its type really is.
 */
#define encode_enum encode_int32_t
#define decode_enum decode_int32_t

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
#define decode_malloc(n) ((n) ? malloc(n) : 0)
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
}

#define endecode_fields_1(name, t1, x1) \
    endecode_fields_1_generic(name, name, t1, x1)
#define endecode_fields_1_struct(name, t1, x1) \
    endecode_fields_1_generic(name, struct name, t1, x1)

#define endecode_fields_2_generic(name, sname, t1, x1, t2, x2) \
static inline void encode_##name(char **pptr, const sname *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
} \
static inline void decode_##name(char **pptr, sname *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
}

#define endecode_fields_2(name, t1, x1, t2, x2) \
    endecode_fields_2_generic(name, name, t1, x1, t2, x2)
#define endecode_fields_2_struct(name, t1, x1, t2, x2) \
    endecode_fields_2_generic(name, struct name, t1, x1, t2, x2)

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
}

#define endecode_fields_3(name, t1, x1, t2, x2, t3, x3) \
    endecode_fields_3_generic(name, name, t1, x1, t2, x2, t3, x3)
#define endecode_fields_3_struct(name, t1, x1, t2, x2, t3, x3) \
    endecode_fields_3_generic(name, struct name, t1, x1, t2, x2, t3, x3)

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
}

#define endecode_fields_4(name, t1, x1, t2, x2, t3, x3, t4, x4) \
    endecode_fields_4_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4)
#define endecode_fields_4_struct(name, t1, x1, t2, x2, t3, x3, t4, x4) \
    endecode_fields_4_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4)

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
}

#define endecode_fields_5(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
    endecode_fields_5_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5)
#define endecode_fields_5_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
    endecode_fields_5_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5)

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
}

#define endecode_fields_6(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6) \
    endecode_fields_6_generic(name, name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6)
#define endecode_fields_6_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6) \
    endecode_fields_6_generic(name, struct name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5, t6, x6)

#define endecode_fields_7(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7) \
static inline void encode_##name(char **pptr, const name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
} \
static inline void decode_##name(char **pptr, name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
}

#define endecode_fields_7_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
}

#define endecode_fields_8_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
}

#define endecode_fields_9_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) \
static inline void encode_##name(char **pptr, const struct name *x) { \
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
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
    decode_##t7(pptr, &x->x7); \
    decode_##t8(pptr, &x->x8); \
    decode_##t9(pptr, &x->x9); \
}

#define endecode_fields_10_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) \
static inline void encode_##name(char **pptr, const struct name *x) { \
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
static inline void decode_##name(char **pptr, struct name *x) { \
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
}

#define endecode_fields_11_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) \
static inline void encode_##name(char **pptr, const struct name *x) { \
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
static inline void decode_##name(char **pptr, struct name *x) { \
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
}

#define endecode_fields_12(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) \
static inline void encode_##name(char **pptr, const name *x) { \
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
static inline void decode_##name(char **pptr, name *x) { \
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
}

#define endecode_fields_15_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7, \
    t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15) \
static inline void encode_##name(char **pptr, const struct name *x) { \
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
static inline void decode_##name(char **pptr, struct name *x) { \
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
}

#define endecode_fields_16_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7, \
    t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16) \
static inline void encode_##name(char **pptr, const struct name *x) { \
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
static inline void decode_##name(char **pptr, struct name *x) { \
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
}


#define endecode_fields_17_struct(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7, \
    t8,x8,t9,x9,t10,x10,t11,x11,t12,x12,t13,x13,t14,x14,t15,x15,t16,x16,t17,x17) \
static inline void encode_##name(char **pptr, const struct name *x) { \
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
static inline void decode_##name(char **pptr, struct name *x) { \
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
}

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
#define endecode_fields_1a_1a_struct(name, t1,x1, tn1, n1, ta1, a1, t2,x2, tn2, n2, ta2, a2) \
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
    decode_##t1(pptr, &x->x1); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	decode_##ta2(pptr, &(x)->a2[i]); \
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
}

#define DEFINE_STATIC_ENDECODE_FUNCS(__name__, __type__) \
__attribute__((unused)) \
static void encode_func_##__name__(char **pptr, void *x) \
{ \
    encode_##__name__(pptr, (__type__ *)x); \
}; \
__attribute__((unused)) \
static void decode_func_##__name__(char **pptr, void *x) \
{ \
    decode_##__name__(pptr, (__type__ *)x); \
}

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
};                                                                            \
static inline void decode_##name(char **pptr, struct name *x)                 \
{                                                                             \
    decode_enum(pptr, &x->ename);                                             \
    switch(x->ename)                                                          \
    {                                                                         \
        case en1: decode_##ut1(pptr, &x->uname.un1); break;                   \
        case en2: decode_##ut2(pptr, &x->uname.un2); break;                   \
        default: assert(0);                                                   \
    }                                                                         \
};

#endif  /* __SRC_PROTO_ENDECODE_FUNCS_H */

