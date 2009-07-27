/*
 * (C) 2003-6 Pete Wyckoff, Ohio Supercomputer Center <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 *
 * Stubs for user codes to hide the encoding functions.
 */
#ifndef __PVFS2_ENCODE_STUBS_H
#define __PVFS2_ENCODE_STUBS_H

/*
 * All character types are rounded up to avoid seriously unaligned accesses;
 * generally handy elsewhere too.
 */
#define roundup4(x) (((x)+3) & ~3)
#define roundup8(x) (((x)+7) & ~7)

/*
 * Look at the pointer value, leave it alone if aligned, or push it up to
 * the next 8 bytes.
 */
#ifdef HAVE_VALGRIND_H
#define align8(pptr) do { \
    int _pad = roundup8((uintptr_t) *(pptr)) - (uintptr_t) *(pptr); \
    memset(*(pptr), 0, _pad); \
    *(pptr) += _pad; \
} while(0);
#else
#define align8(pptr) do { \
    int _pad = roundup8((uintptr_t) *(pptr)) - (uintptr_t) *(pptr); \
    *(pptr) += _pad; \
} while(0);
#endif

/*
 * Files that want full definitions for the encoding and decoding functions
 * will define this.  They need access to the full source tree.  Most users
 * expect these noop #defines.
 */
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#  include "src/proto/endecode-funcs.h"
#else  /* __PINT_REQPROTO_ENCODE_FUNCS_C */

/* dummy declarations to turn off functions */
#define endecode_fields_1(n,t1,x1) struct endecode_fake_struct
#define endecode_fields_1_struct(n,t1,x1) struct endecode_fake_struct
#define endecode_fields_2(n,t1,x1,t2,x2) struct endecode_fake_struct
#define endecode_fields_2_struct(n,t1,x1,t2,x2) struct endecode_fake_struct
#define endecode_fields_3(n,t1,x1,t2,x2,t3,x3) struct endecode_fake_struct
#define endecode_fields_3_struct(n,t1,x1,t2,x2,t3,x3) struct endecode_fake_struct
#define endecode_fields_4(n,t1,x1,t2,x2,t3,x3,t4,x4) struct endecode_fake_struct
#define endecode_fields_4_struct(n,t1,x1,t2,x2,t3,x3,t4,x4) struct endecode_fake_struct
#define endecode_fields_5(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5) struct endecode_fake_struct
#define endecode_fields_5_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5) struct endecode_fake_struct
#define endecode_fields_6(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6) struct endecode_fake_struct
#define endecode_fields_7_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7) struct endecode_fake_struct
#define endecode_fields_8_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8) struct endecode_fake_struct
#define endecode_fields_9_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9) struct endecode_fake_struct
#define endecode_fields_10_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10) struct endecode_fake_struct
#define endecode_fields_11_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11) struct endecode_fake_struct
#define endecode_fields_12(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11,t12,x12) struct endecode_fake_struct

#define endecode_fields_1a(n,t1,x1,tn1,n1,ta1,a1) struct endecode_fake_struct
#define endecode_fields_1a_struct(n,t1,x1,tn1,n1,ta1,a1) struct endecode_fake_struct
#define endecode_fields_1aa_struct(n,t1,x1,tn1,n1,ta1,a1,ta2,a2) struct endecode_fake_struct
#define endecode_fields_2a_struct(n,t1,x1,t2,x2,tn1,n1,ta1,a1) struct endecode_fake_struct
#define endecode_fields_2aa_struct(n,t1,x1,t2,x2,tn1,n1,ta1,a1,ta2,a2) struct endecode_fake_struct
#define endecode_fields_3a_struct(n,t1,x1,t2,x2,t3,x3,tn1,n1,ta1,a1) struct endecode_fake_struct
#define endecode_fields_4aa_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,tn1,n1,ta1,a1,ta2,a2) struct endecode_fake_struct
#define endecode_fields_1a_1a_struct(n,t1,x1,tn1,n1,ta1,a1,t2,x2,tn2,n2,ta2,a2) struct endecode_fake_struct
#define endecode_fields_4a_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,tn1,n1,ta1,a1) struct endecode_fake_struct
#define endecode_fields_5a_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,tn1,n1,ta1,a1) struct endecode_fake_struct

#define encode_enum_union_2_struct(name, ename, uname, ut1, un1, en1, ut2, un2, en2) struct endecode_fake_struct

#endif  /* __PINT_REQPROTO_ENCODE_FUNCS_C */

#endif  /* __PVFS2_ENCODE_STUBS_H */
