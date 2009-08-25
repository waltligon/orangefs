/*
 * (C) 2007 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_EVENT_H
#define __PINT_EVENT_H

#include "pvfs2-types.h"

typedef PVFS_id_gen_t PINT_event_type;
typedef PVFS_id_gen_t PINT_event_id;
typedef PVFS_id_gen_t PINT_event_group;

extern uint64_t PINT_event_enabled_mask;

enum PINT_event_method
{
    PINT_EVENT_TRACE_TAU
};

enum PINT_event_info
{
    PINT_EVENT_INFO_MAX_TRACES,
    PINT_EVENT_INFO_BLOCKING,
    PINT_EVENT_INFO_BUFFER_SIZE
};

int PINT_event_init(enum PINT_event_method type);

void PINT_event_finalize(void);

int PINT_event_enable(const char *events);
int PINT_event_disable(const char *events);

int PINT_event_setinfo(enum PINT_event_info info, void *value);
int PINT_event_getinfo(enum PINT_event_info info, void *value);

int PINT_event_thread_start(char *name);
int PINT_event_thread_stop(void);

int PINT_event_define_group(const char *name, PINT_event_group *group);

int PINT_event_define_event(PINT_event_group *group,
                            char *name,
                            char *format_start,
                            char *format_end,
                            PINT_event_type *type);

int PINT_event_start_event(PINT_event_type type,
                           int process_id,
                           int *thread_id,
                           PINT_event_id *event_id,
                           ...);

int PINT_event_end_event(PINT_event_type type,
                         int process_id,
                         int *thread_id,
                         PINT_event_id event_id,
                         ...);

int PINT_event_log_event(PINT_event_type type,
                         int process_id,
                         int *thread_id,
                         ...);

#ifdef __PVFS2_ENABLE_EVENT__

#define PINT_EVENT_START(ET, PID, TID, EID, args...)  \
   PINT_event_start_event(ET, PID, TID, EID, ## args)

#define PINT_EVENT_END(ET, PID, TID, EID, args...) \
   PINT_event_end_event(ET, PID, TID, EID, ## args)

#define PINT_EVENT_LOG(ET, PID, TID, args...) \
   PINT_event_log_event(ET, PID, TID, ## args)

#define PINT_EVENT_ENABLED 1

#else /* __PVFS2_ENABLE_EVENT__ */

#define PINT_EVENT_START(ET, PID, TID, EID, args...)  \
    do { } while(0)

#define PINT_EVENT_END(ET, PID, TID, EID, args...) \
    do { } while(0)

#define PINT_EVENT_LOG(ET, PID, TID, args...) \
    do { } while(0)

#define PINT_EVENT_ENABLED  0

#endif /* __PVFS2_ENABLE_EVENT__ */
#endif /* __PINT_EVENT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

