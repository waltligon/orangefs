/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_EVENT_H
#define __PINT_EVENT_H

#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "gen-locks.h"
#include "pvfs2-event.h"

/* TODO: put this value somewhere else? read from config file? */
#define PINT_EVENT_DEFAULT_RING_SIZE 4000

/* variables that provide runtime control over which events are recorded */
extern int PINT_event_on;
extern int32_t PINT_event_api_mask;
extern int32_t PINT_event_op_mask;

int PINT_event_initialize(int ring_size);
void PINT_event_finalize(void);
void PINT_event_set_masks(int event_on, int32_t api_mask, int32_t op_mask);
void PINT_event_get_masks(int* event_on, int32_t* api_mask, int32_t* op_mask);
void PINT_event_retrieve(struct PVFS_mgmt_event* event_array,
			 int count);
void __PINT_event_timestamp(enum PVFS_event_api api,
			    int32_t operation,
			    int64_t value,
			    PVFS_id_gen_t id,
			    int8_t flags);

#if defined(HAVE_PABLO)
#include "SystemDepend.h"
#include "Trace.h"

int PINT_event_pablo_init(void);
void PINT_event_pablo_finalize(void);

void __PINT_event_pablo(enum PVFS_event_api api,
			int32_t operation,
			int64_t value,
			PVFS_id_gen_t id,
			int8_t flags);
#endif

#if defined(HAVE_MPE)
#include "mpe.h"
extern int PINT_event_job_start, PINT_event_job_stop;
extern int PINT_event_trove_rd_start, PINT_event_trove_rd_stop;
extern int PINT_event_trove_wr_start, PINT_event_trove_wr_stop;
extern int PINT_event_bmi_start, PINT_event_bmi_stop;
extern int PINT_event_flow_start, PINT_event_flow_stop;

int PINT_event_mpe_init(void);
void PINT_event_mpe_finalize(void);

void __PINT_event_mpe(enum PVFS_event_api api,
		      int32_t operation,
		      int64_t value,
		      PVFS_id_gen_t id,
		      int8_t flags);

#endif

int PINT_event_default_init(int ringsize);
void PINT_event_default_finalize(void);

void __PINT_event_default(enum PVFS_event_api api,
			  int32_t operation,
			  int64_t value,
			  PVFS_id_gen_t id,
			  int8_t flags);

#ifdef __PVFS2_DISABLE_EVENT__
#define PINT_event_timestamp(__api, __operation, __value, __id, __flags) \
    do {} while(0)
#else
#define PINT_event_timestamp(__api, __operation, __value, __id, __flags) \
    do { \
	if(PINT_event_on && (PINT_event_api_mask & (__api)) && \
	    ((PINT_event_op_mask & (__operation))||((__operation)==0))){\
	    __PINT_event_timestamp((__api), (__operation), (__value), (__id), \
	    (__flags)); }\
    } while(0)
#endif

#endif /* __PINT_EVENT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

