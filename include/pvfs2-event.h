/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header defines values necessary to interpret PVFS2 event logs */

/* TODO: need to come up with a more friendly way to manage these enums
 *       maybe a way to map them to strings as well?
 */

#ifndef __PVFS2_EVENT_H
#define __PVFS2_EVENT_H

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
    PINT_EVENT_FLAG_END = 2,
    PINT_EVENT_FLAG_INVALID = 3
};

/* what kind of operation, seperate list for each API */
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

#endif /* __PVFS2_EVENT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
