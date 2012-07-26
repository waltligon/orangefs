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
enum PVFS_event_api
{
    PVFS_EVENT_API_JOB =   (1 << 0),
    PVFS_EVENT_API_BMI =   (1 << 1),
    PVFS_EVENT_API_TROVE = (1 << 2),
    PVFS_EVENT_API_ENCODE_REQ = (1 << 3),  /* protocol request encode */
    PVFS_EVENT_API_ENCODE_RESP = (1 << 4), /* protocol response encode */
    PVFS_EVENT_API_DECODE_REQ = (1 << 5),  /* protocol request decode */
    PVFS_EVENT_API_DECODE_RESP = (1 << 6), /* protocol response decode */
    PVFS_EVENT_API_SM =	   (1 << 7)        /* state machines */
};

/* what kind of event */
enum PVFS_event_flag
{
    PVFS_EVENT_FLAG_NONE =  0,
    PVFS_EVENT_FLAG_START = (1 << 0),
    PVFS_EVENT_FLAG_END =   (1 << 1),
    PVFS_EVENT_FLAG_INVALID = (1 << 2)
};

/* kind of operation, may exist in multiple APIs */
enum PVFS_event_op
{
     PVFS_EVENT_BMI_SEND = 1,
     PVFS_EVENT_BMI_RECV = 2,
     PVFS_EVENT_FLOW = 3,
     PVFS_EVENT_TROVE_READ_AT = 4,
     PVFS_EVENT_TROVE_WRITE_AT = 5,
     PVFS_EVENT_TROVE_BSTREAM_FLUSH = 6,
     PVFS_EVENT_TROVE_KEYVAL_FLUSH = 7,
     PVFS_EVENT_TROVE_READ_LIST = 8,
     PVFS_EVENT_TROVE_WRITE_LIST = 9,
     PVFS_EVENT_TROVE_KEYVAL_READ = 10,
     PVFS_EVENT_TROVE_KEYVAL_READ_LIST = 11,
     PVFS_EVENT_TROVE_KEYVAL_WRITE = 12,
     PVFS_EVENT_TROVE_DSPACE_GETATTR = 13,
     PVFS_EVENT_TROVE_DSPACE_SETATTR = 14,
     PVFS_EVENT_TROVE_BSTREAM_RESIZE = 15,
     PVFS_EVENT_TROVE_KEYVAL_REMOVE = 16,
     PVFS_EVENT_TROVE_KEYVAL_ITERATE = 17,
     PVFS_EVENT_TROVE_KEYVAL_ITERATE_KEYS = 18,
     PVFS_EVENT_TROVE_DSPACE_ITERATE_HANDLES = 19,
     PVFS_EVENT_TROVE_DSPACE_CREATE = 20,
     PVFS_EVENT_TROVE_DSPACE_REMOVE = 21,
     PVFS_EVENT_TROVE_DSPACE_VERIFY = 22,
     PVFS_EVENT_TROVE_BSTREAM_VALIDATE = 23,
     PVFS_EVENT_TROVE_KEYVAL_VALIDATE = 24,
     PVFS_EVENT_TROVE_KEYVAL_WRITE_LIST = 25,
     PVFS_EVENT_TROVE_KEYVAL_GET_HANDLE_INFO = 26,
     PVFS_EVENT_TROVE_DSPACE_GETATTR_LIST = 27,
     PVFS_EVENT_TROVE_KEYVAL_REMOVE_LIST = 28,
};

#endif /* __PVFS2_EVENT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
