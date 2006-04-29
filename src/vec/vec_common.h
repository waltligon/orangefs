/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#ifndef _VEC_COMMON_H
#define _VEC_COMMON_H

#include <stdio.h>
#include "vec_prot.h"

#ifndef MIN
static inline int MIN(int a, int b)
{
    return (a < b) ? (a) : (b);
}
#endif

#ifndef MAX
static inline int MAX(int a, int b)
{
    return (a > b) ? (a) : (b);
}
#endif

#define bh_hash_shift 15
#define _hashfn(machine_number,inode_number,pagenumber) \
    ((((1) << (bh_hash_shift - 6)) ^ ((inode_number) << (bh_hash_shift - 9)))\
     ^ (((pagenumber)  << (bh_hash_shift - 6)) ^ ((pagenumber)  >> 13) ^\
         ((pagenumber) << (bh_hash_shift - 12))))


#define hash1(x)        _hashfn(0,(x),0)
#define hash2(x, y)  _hashfn(0, (x), (y))
#define hash3(x, y, z)  _hashfn((x), (y), (z))

extern void vec_print(vec_vectors_t *v);
extern int vec_copy(vec_vectors_t *dst, vec_vectors_t *src);
extern int vec_lcopy(vec_vectors_t *dst, vec_vectors_t *src);
extern void vec_dtor(vec_vectors_t *v);
extern int vec_ctor(vec_vectors_t *v, int nservers);
extern int vec_extend(vec_vectors_t *v, int nservers);
extern int vec_add(vec_vectors_t *vec1, vec_vectors_t *vec2, vec_vectors_t *vec3);
extern int svec_copy(vec_svectors_t *dst, vec_svectors_t *src);
extern int svec_lcopy(vec_svectors_t *dst, vec_svectors_t *src);
extern void svec_dtor(vec_svectors_t *v, int i);
extern int svec_ctor(vec_svectors_t *v, int scount, int vcount);
extern int svec_extend(vec_svectors_t *v, int scount, int vcount);
extern void svec_print(vec_svectors_t *sv);

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
