/*
 * (C) 2003 Pete Wyckoff, Ohio Supercomputer Center <pw@osc.edu>
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
 * Look at the pointer value, push it up to the next 8 bytes.
 */
#define align8(pptr) do { \
    *(pptr) = ((void *)(((unsigned long)(*(pptr)) + 8) & ~7)); \
} while(0);

/*
 * Files that want full definitions for the encoding and decoding functions
 * will define this.  They need access to the full source tree.  Most users
 * expect these noop #defines.
 */
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#  include "src/proto/endecode-funcs.h"
#else  /* __PINT_REQPROTO_ENCODE_FUNCS_C */

/* dummy declarations to turn off functions */
#define endecode_fields_0(n)
#define endecode_fields_0a(n,tn1,n1,ta1,a1)
#define endecode_fields_1(n,t1,x1)
#define endecode_fields_2(n,t1,x1,t2,x2)
#define endecode_fields_3(n,t1,x1,t2,x2,t3,x3)
#define endecode_fields_5(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5)
#define endecode_fields_8(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8)
#define endecode_fields_11(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7,t8,x8,t9,x9,t10,x10,t11,x11)
#define endecode_fields_1_struct(n,t1,x1)
#define endecode_fields_2_struct(n,t1,x1,t2,x2)
#define endecode_fields_3_struct(n,t1,x1,t2,x2,t3,x3)
#define endecode_fields_4_struct(n,t1,x1,t2,x2,t3,x3,t4,x4)
#define endecode_fields_5_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5)
#define endecode_fields_6_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6)
#define endecode_fields_7_struct(n,t1,x1,t2,x2,t3,x3,t4,x4,t5,x5,t6,x6,t7,x7)
#define endecode_fields_0a_struct(n,tn1,n1,ta1,a1)
#define endecode_fields_0aa_struct(n,tn1,n1,ta1,a1,tn2,n2,ta2,a2)
#define endecode_fields_1a_struct(n,t1,x1,tn1,n1,ta1,a1)
#define endecode_fields_2a_struct(n,t1,x1,tn1,n1,tn2,n2,ta1,a1) 
#define endecode_fields_3a_struct(n,t1,x1,t2,x2,t3,x3,tn1,n1,ta1,a1)

#endif  /* __PINT_REQPROTO_ENCODE_FUNCS_C */

#endif  /* __PVFS2_ENCODE_STUBS_H */
