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

#include <errno.h>
#include <stdint.h>
#include "pvfs2-config.h"

#ifdef __PVFS2_TYPES_H
typedef PVFS_id_gen_t BMI_id_gen_t;
#else
typedef int64_t BMI_id_gen_t;
#endif

/* id_gen_fast_register()
 * 
 * registers a piece of data (a pointer of some sort) and
 * returns an opaque id for it.  
 *
 * *new_id will be 0 if item is NULL
 */
static inline void id_gen_fast_register(BMI_id_gen_t * new_id,
				       void *item)
{
#if SIZEOF_VOID_P == 8
    *new_id = (int64_t) item;
#else
    *new_id = 0;
    *new_id += (int32_t) item;
#endif
}

/* id_gen_fast_lookup()
 * 
 * Returns the piece of data registered with an id.  It does no error
 * checking!  It does not make sure that that the id was actually
 * registered before proceeding.
 *
 * returns pointer to data on success, NULL on failure
 */
static inline void *id_gen_fast_lookup(BMI_id_gen_t id)
{
#if SIZEOF_VOID_P == 8
    return (void *) id;
#else
    return (void *) (uint32_t) id;
#endif
}

#define id_gen_fast_unregister(id) do { } while(0)

int id_gen_safe_initialize(void);
int id_gen_safe_finalize(void);

/* id_gen_safe_register()
 * 
 * registers a piece of data (a pointer of some sort) and returns an
 * opaque id for it.  this register is safe because it is guaranteed
 * to have an indirect association with the data being registered
 *
 * returns 0 on success, -errno on failure
 */
int id_gen_safe_register(BMI_id_gen_t *new_id,
                         void *item);

void *id_gen_safe_lookup(BMI_id_gen_t id);

int id_gen_safe_unregister(BMI_id_gen_t new_id);

#endif /* __ID_GENERATOR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
