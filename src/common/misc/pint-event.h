/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_EVENT_H
#define __PINT_EVENT_H

#include <pvfs2-types.h>

/* TODO: put this somewhere else? read from config file? */
#define PINT_EVENT_DEFAULT_RING_SIZE 2000

/* different API levels where we can log events */
enum PINT_event_api
{
    PINT_EVENT_API_JOB =   (1 << 0),
    PINT_EVENT_API_BMI =   (1 << 2),
    PINT_EVENT_API_TROVE = (1 << 3)
};

/* what kind of event */
enum PINT_event_flag
{
    PINT_EVENT_FLAG_NONE = 0,
    PINT_EVENT_FLAG_START = 1,
    PINT_EVENT_FLAG_END = 2
};

/* what kind of operation, seperate list for each API */
/* TODO: is there a better way to do this?  any point in registering these
 * at runtime rather than listing them all in a header here?
 */
enum PINT_event_flow_op
{
    PINT_EVENT_FLOW = 1
};
enum PINT_event_bmi_op
{
    PINT_EVENT_BMI_SEND =      (1 << 0),
    PINT_EVENT_BMI_RECV =      (1 << 1),
    PINT_EVENT_BMI_SEND_LIST = (1 << 2),
    PINT_EVENT_BMI_RECV_LIST = (1 << 3)
};
enum PINT_event_trove_op
{
    PINT_EVENT_TROVE_READ =       (1 << 0),
    PINT_EVENT_TROVE_WRITE =      (1 << 1),
    PINT_EVENT_TROVE_READ_LIST =  (1 << 2),
    PINT_EVENT_TROVE_WRITE_LIST = (1 << 3)
};

/* variables that provide runtime control over which events are recorded */
extern int PINT_event_on;
extern int32_t PINT_event_api_mask;
extern int32_t PINT_event_op_mask;

int PINT_event_initialize(int ring_size);
void PINT_event_finalize(void);
void PINT_event_set_masks(int event_on, int32_t api_mask, int32_t op_mask);
void PINT_event_get_masks(int* event_on, int32_t* api_mask, int32_t* op_mask);
void __PINT_event_timestamp(
    enum PINT_event_api api,
    int32_t operation,
    int64_t value,
    PVFS_id_gen_t id,
    int8_t flags);

#ifdef __PVFS2_DISABLE_EVENT__
#define PINT_event_timestamp(__api, __operation, __value, __id, __flags) \
    do{}while(0)
#else
#define PINT_event_timestamp(__api, __operation, __value, __id, __flags) \
    do{ \
	if(PINT_event_on && (PINT_event_api_mask & __api) && \
	    (PINT_event_op_mask & __operation)){\
	    __PINT_event_timestamp(__api, __operation, __value, __id, \
	    __flags); }\
    }while(0)
#endif

#endif /* __PINT_EVENT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

