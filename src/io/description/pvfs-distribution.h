/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS_DISTRIBUTION_H
#define __PVFS_DISTRIBUTION_H

#include "pvfs2-types.h"

/* each particular distribution implementation will define this for itself */
#ifndef DIST_MODULE
struct PVFS_Dist_params_s {
    /* empty */
};
#endif
typedef struct PVFS_Dist_params PVFS_Dist_params;

typedef struct {
	PVFS_offset (*logical_to_physical_offset) (PVFS_Dist_params *dparam,
			uint32_t server_nr, uint32_t server_ct,
			PVFS_offset logical_offset);
	PVFS_offset (*physical_to_logical_offset) (PVFS_Dist_params *dparam,
			uint32_t server_nr, uint32_t server_ct,
			PVFS_offset physical_offset);
	PVFS_offset (*next_mapped_offset) (PVFS_Dist_params *dparam,
			uint32_t server_nr, uint32_t server_ct,
			PVFS_offset logical_offset);
	PVFS_size (*contiguous_length) (PVFS_Dist_params *dparam,
			uint32_t server_nr, uint32_t server_ct,
			PVFS_offset physical_offset);
	PVFS_size (*logical_file_size) (PVFS_Dist_params *dparam,
			uint32_t server_ct, PVFS_size *psizes);
	void (*encode) (PVFS_Dist_params *dparam, void *buffer);
	void (*decode) (PVFS_Dist_params *dparam, void *buffer);
	void (*encode_lebf) (char **pptr, PVFS_Dist_params *dparam);
	void (*decode_lebf) (char **pptr, PVFS_Dist_params *dparam);
} PVFS_Dist_methods;

/* this struct is used to define a distribution to PVFS */
typedef struct PVFS_Dist {
	char *dist_name;
	int32_t name_size;
	int32_t param_size; 
	PVFS_Dist_params *params;
	PVFS_Dist_methods *methods;
} PVFS_Dist;
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_Dist(pptr,x) do { PVFS_Dist *px = *(x); \
    encode_string(pptr, &px->dist_name); \
    if (!px->methods) { \
	gossip_err("%s: encode_PVFS_Dist: methods is null\n", __func__); \
	exit(1); \
    } \
    (px->methods->encode_lebf) (pptr, px->params); \
} while (0)
#define decode_PVFS_Dist(pptr,x) do { PVFS_Dist tmp_dist; PVFS_Dist *px; \
    extern int PINT_Dist_lookup(PVFS_Dist *dist); \
    decode_string(pptr, &tmp_dist.dist_name); \
    tmp_dist.methods = 0; \
    /* bizzare lookup function fills in most fields */ \
    PINT_Dist_lookup(&tmp_dist); \
    if (!tmp_dist.methods) { \
	gossip_err("%s: decode_PVFS_Dist: methods is null\n", __func__); \
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

extern PVFS_Dist *PVFS_Dist_create(char *name);
extern int PVFS_Dist_free(PVFS_Dist *dist);
extern PVFS_Dist *PVFS_Dist_copy(const PVFS_Dist *dist);
extern int PVFS_Dist_getparams(void *buf, const PVFS_Dist *dist);
extern int PVFS_Dist_setparams(PVFS_Dist *dist, const void *buf);

/******** macros for access to dist struct ***********/

#define PINT_DIST_PACK_SIZE(d) \
 (roundup8(sizeof(*(d))) + roundup8((d)->name_size) + roundup8((d)->param_size))

#endif /* __PVFS_DISTRIBUTION_H */
