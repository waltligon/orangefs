/*
 * (C) 2003 Pete Wyckoff, Ohio Supercomputer Center <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * Defines for macros related to wire encoding and decoding.  Only included
 * by include/pvfs2-encode-stubs.h by core encoding users.
 */
#ifndef __SRC_PROTO_ENDECODE_FUNCS_H
#define __SRC_PROTO_ENDECODE_FUNCS_H

#include "src/io/bmi/bmi-byteswap.h"

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

/* skip 4 bytes */
#define encode_skip4(pptr) do { \
    *(pptr) += 4; \
} while (0)
#define decode_skip4(pptr) do { \
    *(pptr) += 4; \
} while (0)

/* strings; decoding just points into existing character data */
#define encode_string(pptr,pbuf) do { \
    u_int32_t len = strlen(*pbuf); \
    *(u_int32_t *) *(pptr) = htobmi32(len); \
    memcpy(*(pptr)+4, *pbuf, len+1); \
    *(pptr) += roundup4(4 + len + 1); \
} while (0)
#define decode_string(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    *pbuf = *(pptr) + 4; \
    *(pptr) += roundup4(4 + len + 1); \
} while (0)
/* odd variation, space exists in some structure, must copy-in string */
#define encode_here_string(pptr,pbuf) encode_string(pptr,pbuf)
#define decode_here_string(pptr,pbuf) do { \
    u_int32_t len = bmitoh32(*(u_int32_t *) *(pptr)); \
    memcpy(pbuf, *(pptr) + 4, len + 1); \
    *(pptr) += roundup4(4 + len + 1); \
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
#define decode_malloc(n) ((n) ? malloc(n) : 0)
#define decode_free(n) free(n)

/*
 * These wrappers define functions to do the encoding of the types or
 * structures they describe.  Please remember to update the empty stub versions
 * of these routines in include/pvfs2-encode-stubs.h, although the compiler
 * will certainly complain too.
 *
 * Note that decode can not take a const since we point into the
 * undecoded buffer for strings.
 *
 * There are two sections here:  first is for types, second is for structs.
 */
#define endecode_fields_0(name) \
static inline void encode_##name(char **pptr, const name *x) { } \
static inline void decode_##name(char **pptr, name *x) { } \

#define endecode_fields_1(name, t1, x1) \
static inline void encode_##name(char **pptr, const name *x) { \
    encode_##t1(pptr, &x->x1); \
} \
static inline void decode_##name(char **pptr, name *x) { \
    decode_##t1(pptr, &x->x1); \
}

#define endecode_fields_2(name, t1, x1, t2, x2) \
static inline void encode_##name(char **pptr, const name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
} \
static inline void decode_##name(char **pptr, name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
}

#define endecode_fields_3(name, t1, x1, t2, x2, t3, x3) \
static inline void encode_##name(char **pptr, const name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
} \
static inline void decode_##name(char **pptr, name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
}

#define endecode_fields_5(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
static inline void encode_##name(char **pptr, const name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
} \
static inline void decode_##name(char **pptr, name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
}

#define endecode_fields_8(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8) \
static inline void encode_##name(char **pptr, const name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
    encode_##t7(pptr, &x->x7); \
    encode_##t8(pptr, &x->x8); \
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
}

#define endecode_fields_11(name,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) \
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
}

#define endecode_fields_1_struct(name, t1, x1) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
}

#define endecode_fields_2_struct(name, t1, x1, t2, x2) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
}

#define endecode_fields_3_struct(name, t1, x1, t2, x2, t3, x3) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
}

#define endecode_fields_4_struct(name, t1, x1, t2, x2, t3, x3, t4, x4) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
}

#define endecode_fields_5_struct(name, t1, x1, t2, x2, t3, x3, t4, x4, t5, x5) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
}

#define endecode_fields_6_struct(name, t1,x1, t2,x2, t3,x3, t4,x4,t5,x5,t6,x6) \
static inline void encode_##name(char **pptr, const struct name *x) { \
    encode_##t1(pptr, &x->x1); \
    encode_##t2(pptr, &x->x2); \
    encode_##t3(pptr, &x->x3); \
    encode_##t4(pptr, &x->x4); \
    encode_##t5(pptr, &x->x5); \
    encode_##t6(pptr, &x->x6); \
} \
static inline void decode_##name(char **pptr, struct name *x) { \
    decode_##t1(pptr, &x->x1); \
    decode_##t2(pptr, &x->x2); \
    decode_##t3(pptr, &x->x3); \
    decode_##t4(pptr, &x->x4); \
    decode_##t5(pptr, &x->x5); \
    decode_##t6(pptr, &x->x6); \
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

/* ones with arrays that are allocated in the decode */
#define endecode_fields_0a(name, tn1, n1, ta1, a1) \
static inline void encode_##name(char **pptr, const name *x) { int i; \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, name *x) { int i; \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	decode_##ta1(pptr, &(x)->a1[i]); \
}

#define endecode_fields_0a_struct(name, tn1, n1, ta1, a1) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	decode_##ta1(pptr, &(x)->a1[i]); \
}

#define endecode_fields_0aa_struct(name, tn1, n1, ta1, a1, tn2, n2, ta2, a2) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) { int n; n = i; \
	encode_##ta1(pptr, &(x)->a1[n]); } \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n2; i++) { int n; n = i; \
	encode_##ta2(pptr, &(x)->a2[n]); } \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	decode_##ta1(pptr, &(x)->a1[i]); \
    decode_##tn2(pptr, &x->n2); \
    x->a2 = decode_malloc(x->n2 * sizeof(*x->a2)); \
    for (i=0; i<x->n2; i++) \
	decode_##ta2(pptr, &(x)->a2[i]); \
}

#define endecode_fields_1a_struct(name, t1, x1, tn1, n1, ta1, a1) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##tn1(pptr, &x->n1); \
    for (i=0; i<x->n1; i++) \
	encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##tn1(pptr, &x->n1); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	decode_##ta1(pptr, &(x)->a1[i]); \
}

#define endecode_fields_2a_struct(name, t1, x1, tn1, n1, tn2, n2, ta1, a1) \
static inline void encode_##name(char **pptr, const struct name *x) { int i; \
    encode_##t1(pptr, &x->x1); \
    encode_##tn1(pptr, &x->n1); \
    encode_##tn2(pptr, &x->n2); \
    for (i=0; i<x->n1; i++) \
	encode_##ta1(pptr, &(x)->a1[i]); \
} \
static inline void decode_##name(char **pptr, struct name *x) { int i; \
    decode_##t1(pptr, &x->x1); \
    decode_##tn1(pptr, &x->n1); \
    decode_##tn2(pptr, &x->n2); \
    x->a1 = decode_malloc(x->n1 * sizeof(*x->a1)); \
    for (i=0; i<x->n1; i++) \
	decode_##ta1(pptr, &(x)->a1[i]); \
}

#define endecode_fields_3a_struct(name, t1, x1, t2, x2, t3, x3, tn1,n1,ta1,a1) \
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

#endif  /* __SRC_PROTO_ENDECODE_FUNCS_H */
