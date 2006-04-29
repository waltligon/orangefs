/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include "pvfs2.h"
#include "pvfs2-types.h"
#include "vec_prot.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "vec_common.h"

void vec_print(vec_vectors_t *v)
{
    int i, ret = 0, total_len;
    char *str;
    if (!v)
        return;
    total_len = v->vec_vectors_t_len * sizeof(int);
    str = (char *)calloc(total_len, sizeof(char));
    if (!str)
        return;
    ret = 0;
    for (i = 0; i < v->vec_vectors_t_len; i++) {
        int pos;
        pos = snprintf(str + ret, total_len - ret, "%d ", v->vec_vectors_t_val[i]);
        ret += pos;
    }
    gossip_debug(GOSSIP_VEC_DEBUG, "%s\n", str);
    free(str);
    return;
}

/* deep-copy a vector object */
int vec_copy(vec_vectors_t *dst, vec_vectors_t *src)
{
    if (!dst || !src) {
        gossip_err("Invalid args to vec_copy\n");
        return -EINVAL;
    }
    if (!dst->vec_vectors_t_val || !src->vec_vectors_t_val) {
        gossip_err("Invalidly constructed vector objects (NULL pointers) in vec_copy\n");
        return -EINVAL;
    }
    if (dst->vec_vectors_t_len != src->vec_vectors_t_len) {
        gossip_err("Not equal vector length (dst %d) (src %d)\n", 
                dst->vec_vectors_t_len, src->vec_vectors_t_len);
        return -EINVAL;
    }
    memcpy(dst->vec_vectors_t_val, src->vec_vectors_t_val, dst->vec_vectors_t_len * sizeof(uint32_t));
    return 0;
}

/* Deep copy only as much as dst can hold from src */
int vec_lcopy(vec_vectors_t *dst, vec_vectors_t *src)
{
    int len;
    if (!dst || !src) {
        gossip_err("Invalid args to vec_lcopy\n");
        return -EINVAL;
    }
    if (!dst->vec_vectors_t_val || !src->vec_vectors_t_val) {
        gossip_err("Invalidly constructed vector objects (NULL pointers) in vec_lcopy\n");
        return -EINVAL;
    }
    len = MIN(dst->vec_vectors_t_len, src->vec_vectors_t_len);
    memcpy(dst->vec_vectors_t_val, src->vec_vectors_t_val, len * sizeof(uint32_t));
    dst->vec_vectors_t_len = len;
    return 0;
}

/* Destroy a vector */
void vec_dtor(vec_vectors_t *v)
{
    if (v) {
        if (v->vec_vectors_t_val) {
            free(v->vec_vectors_t_val);
            v->vec_vectors_t_val = NULL;
        }
        v->vec_vectors_t_len = 0;
    }
    return;
}

/* Allocate a vector of a given size */
int vec_ctor(vec_vectors_t *v, int nservers)
{
    if (v) {
        v->vec_vectors_t_len = nservers;
        v->vec_vectors_t_val = (uint32_t *) calloc(nservers, sizeof(uint32_t));
        if (v->vec_vectors_t_val == NULL) {
            v->vec_vectors_t_len = 0;
            return -ENOMEM;
        }
    }
    return 0;
}

/* Extend a vector object */
int vec_extend(vec_vectors_t *v, int nservers)
{
    if (v) {
        /* Don't extend if lengths are not different */
        if (v->vec_vectors_t_len == nservers)
            return 0;
        else if (nservers < v->vec_vectors_t_len) 
            return -EINVAL;

        v->vec_vectors_t_val = realloc(v->vec_vectors_t_val, nservers * sizeof(uint32_t));
        if (v->vec_vectors_t_val == NULL) {
            v->vec_vectors_t_len = 0;
            return -ENOMEM;
        }
        /* Zero out the remaining */
        memset(&v->vec_vectors_t_val[v->vec_vectors_t_len], 0, 
                (nservers - v->vec_vectors_t_len) * sizeof(uint32_t));
        v->vec_vectors_t_len = nservers;
    }
    return 0;
}

/* Add two vectors, i.e. we will do vec1 = vec2 + vec3 */
int vec_add(vec_vectors_t *vec1, vec_vectors_t *vec2, vec_vectors_t *vec3)
{
    int i;

    if (!vec1 || !vec2 || !vec3) {
        return -ENOMEM;
    }
    if (vec1->vec_vectors_t_len != vec2->vec_vectors_t_len
            || vec1->vec_vectors_t_len != vec3->vec_vectors_t_len
            || vec2->vec_vectors_t_len != vec3->vec_vectors_t_len) {
        gossip_err("vector_add dimension's dont match (%d, %d, %d)\n", 
                vec1->vec_vectors_t_len, vec2->vec_vectors_t_len, vec3->vec_vectors_t_len);
        return -EINVAL;
    }
    for (i = 0; i < vec1->vec_vectors_t_len; i++) {
        vec1->vec_vectors_t_val[i] = vec2->vec_vectors_t_val[i] + vec3->vec_vectors_t_val[i];
    }
    return 0;
}

/* Deep copy a set of vectors. Be particular about set sizes etc */
int svec_copy(vec_svectors_t *dst, vec_svectors_t *src)
{
    int i, err;

    if (!dst || !src) {
        gossip_err("Invalid args to svec_copy\n");
        return -EINVAL;
    }
    if (!dst->vec_svectors_t_val || !src->vec_svectors_t_val) {
        gossip_err("Invalidly constructed vector objects (NULL pointers) in svec_copy\n");
        return -EINVAL;
    }
    if (dst->vec_svectors_t_len != src->vec_svectors_t_len) {
        gossip_err("Not equal vector length (dst %d) (src %d)\n", 
                dst->vec_svectors_t_len, src->vec_svectors_t_len);
        return -EINVAL;
    }
    for (i = 0; i < dst->vec_svectors_t_len; i++) {
        if ((err= vec_copy(&dst->vec_svectors_t_val[i], &src->vec_svectors_t_val[i])) < 0) {
            return err;
        }
    }
    return 0;
}

/* Deep copy a set of vectors. Copy only as much as dst can hold */
int svec_lcopy(vec_svectors_t *dst, vec_svectors_t *src)
{
    int i, err, len;

    if (!dst || !src) {
        gossip_err("Invalid args to svec_lcopy\n");
        return -EINVAL;
    }
    if (!dst->vec_svectors_t_val || !src->vec_svectors_t_val) {
        gossip_err("Invalidly constructed vector objects (NULL pointers) in svec_lcopy\n");
        return -EINVAL;
    }
    len = MIN(dst->vec_svectors_t_len, src->vec_svectors_t_len);
    for (i = 0; i < len; i++) {
        if ((err= vec_lcopy(&dst->vec_svectors_t_val[i], &src->vec_svectors_t_val[i])) < 0) {
            return err;
        }
    }
    dst->vec_svectors_t_len = len;
    return 0;
}

/* Destroy a set of vector object */
void svec_dtor(vec_svectors_t *v, int i)
{
    if (v && v->vec_svectors_t_val)
    {
        int j;
        for (j = 0; j < i; j++) {
            vec_dtor(&v->vec_svectors_t_val[j]);
        }
        free(v->vec_svectors_t_val);
        v->vec_svectors_t_val = NULL;
        v->vec_svectors_t_len = 0;
    }
    return;
}

/* Allocate a set of a pre-specified number of vector objects */
int svec_ctor(vec_svectors_t *v, int scount, int vcount)
{
    if (v)
    {
        int i;
        v->vec_svectors_t_len = scount;
        v->vec_svectors_t_val = (vec_vectors_t *) calloc(scount, sizeof(vec_vectors_t));
        if (v->vec_svectors_t_val == NULL) {
            v->vec_svectors_t_len = 0;
            return -ENOMEM;
        }
        for (i = 0; i < scount; i++) {
            if (vec_ctor(&v->vec_svectors_t_val[i], vcount) < 0) {
                break;
            }
        }
        if (i != scount) {
            svec_dtor(v, i);
            return -ENOMEM;
        }
    }
    return 0;
}

/* Extend a previously allocated/constructed svector object */
int svec_extend(vec_svectors_t *v, int scount, int vcount)
{
    if (v)
    {
        int i;

        /* No need to extend if new size is not any different */
        if (scount == v->vec_svectors_t_len)
            return 0;
        else if (scount < v->vec_svectors_t_len)
            return -EINVAL;

        v->vec_svectors_t_val = (vec_vectors_t *)
            realloc(v->vec_svectors_t_val, scount * sizeof(vec_vectors_t));
        if (v->vec_svectors_t_val == NULL) {
            v->vec_svectors_t_len = 0;
            return -ENOMEM;
        }
        /* Zero out the remaining regions */
        memset(&v->vec_svectors_t_val[v->vec_svectors_t_len], 0, 
                (scount - v->vec_svectors_t_len) * sizeof(vec_vectors_t));
        for (i = 0; i < scount; i++) {
            if (vec_extend(&v->vec_svectors_t_val[i], vcount) < 0) {
                break;
            }
        }
        if (i != scount) {
            svec_dtor(v, i);
            return -ENOMEM;
        }
        v->vec_svectors_t_len = scount;
    }
    return 0;
}

void svec_print(vec_svectors_t *sv)
{
    int i;

    if (!sv)
        return;
    for (i = 0; i < sv->vec_svectors_t_len; i++) {
        gossip_debug(GOSSIP_VEC_DEBUG, "Vector %d  -> ", i);
        vec_print(&sv->vec_svectors_t_val[i]);
    }
    return;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
