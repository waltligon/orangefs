/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_DISTRIBUTION_H
#define __PINT_DISTRIBUTION_H

#include "pvfs2-types.h"

/* Distribution table size limits */
#define PINT_DIST_NAME_SZ 32

/* Distribution functions that must be supplied by each dist implmentation */
typedef struct PINT_dist_methods_s {

    /* Returns the physical storage offset for a logical file offset */
    PVFS_offset (*logical_to_physical_offset)(void* params,
                                              uint32_t server_nr,
                                              uint32_t server_ct,
                                              PVFS_offset logical_offset);
    
    /* Returns the logical file offset for a given physical storage offset */
    PVFS_offset (*physical_to_logical_offset)(void* params,
                                              uint32_t server_nr,
                                              uint32_t server_ct,
                                              PVFS_offset physical_offset);

    /* Returns the next physical offset for the file on server_nr given an
     * arbitraty logical offset (i.e. an offset anywhere in the file) */
    PVFS_offset (*next_mapped_offset)(void* params,
                                      uint32_t server_nr,
                                      uint32_t server_ct,
                                      PVFS_offset logical_offset);

    /* Returns the contiguous length of file data starting at physical_offset*/
    PVFS_size (*contiguous_length)(void* params,
                                   uint32_t server_nr,
                                   uint32_t server_ct,
                                   PVFS_offset physical_offset);

    /* Returns the logical file size */
    PVFS_size (*logical_file_size)(void* params,
                                   uint32_t server_ct,
                                   PVFS_size *psizes);

    /* Returns the number of data file objects to use for a file */
    int (*get_num_dfiles)(void* params,
                          uint32_t num_servers_requested,
                          uint32_t num_dfiles_requested);

    /* Sets the parameter designated by name to the given value */
    int (*set_param)(const char* dist_name, void* params,
                     const char* param_name, void* value);

    /* Stores parameters in lebf memory at pptr */
    void (*encode_lebf)(char **pptr, void* params);
    
    /* Restores parameters in lebf memory at pptr */
    void (*decode_lebf)(char **pptr, void* params);

    /* Called when the distribution is registered */
    void (*registration_init)(void* params);
    
} PINT_dist_methods;

/* Internal representation of a PVFS2 Distribution */
typedef struct PINT_dist_s {
	char *dist_name;
	int32_t name_size;
	int32_t param_size; 
	void *params;
	PINT_dist_methods *methods;
} PINT_dist;


/* Macros to encode/decode distributions for sending requests */
#define PINT_DIST_PACK_SIZE(d) \
 (roundup8(sizeof(*(d))) + roundup8((d)->name_size) + roundup8((d)->param_size))

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PINT_dist(pptr,x) do { PINT_dist *px = *(x); \
    encode_string(pptr, &px->dist_name); \
    if (!px->methods) { \
	gossip_err("%s: encode_PINT_dist: methods is null\n", __func__); \
	exit(1); \
    } \
    (px->methods->encode_lebf) (pptr, px->params); \
} while (0)
#define decode_PINT_dist(pptr,x) do { PINT_dist tmp_dist; PINT_dist *px; \
    decode_string(pptr, &tmp_dist.dist_name); \
    tmp_dist.params = 0; \
    tmp_dist.methods = 0; \
    /* bizzare lookup function fills in most fields */ \
    PINT_dist_lookup(&tmp_dist); \
    if (!tmp_dist.methods) { \
	gossip_err("%s: decode_PINT_dist: methods is null\n", __func__); \
	exit(1); \
    } \
    /* later routines assume dist is a big contiguous thing, do so */ \
    *(x) = px = decode_malloc(PINT_DIST_PACK_SIZE(&tmp_dist)); \
    memcpy(px, &tmp_dist, sizeof(*px)); \
    px->dist_name = (char *) px + roundup8(sizeof(*px)); \
    memcpy(px->dist_name, tmp_dist.dist_name, tmp_dist.name_size); \
    px->params = (void *)(px->dist_name + roundup8(px->name_size)); \
    (px->methods->decode_lebf) (pptr, px->params); \
} while (0)
#endif

/* Return a cloned copy of the distribution registered for name*/
PINT_dist* PINT_dist_create(const char *name);

/* Deallocate resources in a PINT_dist */
int PINT_dist_free(PINT_dist *dist);

/* Return a cloned copy of dist */
PINT_dist* PINT_dist_copy(const PINT_dist *dist);

/* Makes a memcpy of the distribution parameters in buf.
 * buf must be allocated to the correct size */
int PINT_dist_getparams(void *buf, const PINT_dist *dist);

/* Memcpys the the distribution params from buf into dist */
int PINT_dist_setparams(PINT_dist *dist, const void *buf);

/* Given a distribution with only the dist_name filled in, the remaining
 * distribution parameters are copied from the registered distribution for
 * that name */
int PINT_dist_lookup(PINT_dist *dist);

/* pack dist struct for sending over wire */
void PINT_dist_encode(void *buffer, PINT_dist *dist);

/* unpack dist struct after receiving from wire */
void PINT_dist_decode(PINT_dist *dist, void *buffer);

/* Print dist state to debug system */
void PINT_dist_dump(PINT_dist *dist);

/* Registers the distribution d_p
 *
 * the registration key d_p->dist_name
 */
int PINT_register_distribution(PINT_dist *d_p);

/* Removes the distribution registered for the name dist_name */
int PINT_unregister_distribution(char *dist_name);

#endif /* __PINT_DISTRIBUTION_H */

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 noexpandtab
 */
