/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* A small library for generating and looking up fully opaque id's for
 * an arbitrary piece of data.
 */

/* This will hopefully eventually be a library of mechanisms for doing
 * fast registration and lookups of data structures.  Right now it only
 * has routines that directly convert pointers into integer types and vice 
 * versa.
 */

#ifndef __ID_GENERATOR_H
#define __ID_GENERATOR_H

#include "pvfs2-types.h"
#include "pvfs2-config.h"
#include "errno.h"

/* id_gen_fast_register()
 * 
 * registers a piece of data (a pointer of some sort) and
 * returns an opaque id for it.  
 *
 * returns 0 on success, -errno on failure
 */
static inline int id_gen_fast_register(
    PVFS_id_gen_t * new_id,
    void *item)
{
    if (!item)
	return (-EINVAL);

#if SIZEOF_VOID_P == 8
    *new_id = (int64_t) item;
#else
    *new_id = 0;
    *new_id += (int32_t) item;
#endif

    return (0);
}

/* id_gen_fast_lookup()
 * 
 * Returns the piece of data registered with an id.  It does no error
 * checking!  It does not make sure that that the id was actually
 * registered before proceeding.
 *
 * returns pointer to data on success, NULL on failure
 */
static inline void *id_gen_fast_lookup(
    PVFS_id_gen_t id)
{
    int32_t little_int = 0;

    if (!id)
	return (NULL);

#if SIZEOF_VOID_P == 8
    return ((void *) id);
#else
    little_int += id;
    return ((void *) little_int);
#endif
}

#endif /* __ID_GENERATOR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
