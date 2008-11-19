/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include <stdio.h>

#include <unistd.h>

#include "pint-event.h"
#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "gossip.h"
#include "quicklist.h"
#include "quickhash.h"
#include "id-generator.h"
#include "str-utils.h"

#include "pvfs2-config.h"

#ifdef HAVE_TAU
#include "pvfs_tau_api.h"
#endif

/* variables that provide runtime control over which events are recorded */

static PINT_event_group default_group;

static struct qhash_table *events_table = NULL;
static struct qhash_table *groups_table = NULL;
static uint32_t event_count = 0;
uint64_t PINT_event_enabled_mask = 0;

#ifdef HAVE_TAU
static int PINT_event_default_buffer_size = 1024*1024;
static int PINT_event_default_max_traces = 1024;
#endif

struct PINT_group
{
    char *name;
    PINT_event_group id;
    struct qlist_head events;
    uint64_t mask;
    struct qhash_head link;
};

struct PINT_event
{
    char *name;
    PINT_event_type type;
    PINT_event_group group;
    uint64_t mask;
    struct qlist_head group_link;
    struct qlist_head link;
};

#if defined(HAVE_TAU)

static void PINT_event_tau_init(void);
static void PINT_event_tau_fini(void);
static void PINT_event_tau_thread_init(char* gname);
static void PINT_event_tau_thread_fini(void);

#endif /* HAVE_TAU */

static int PINT_group_compare(void *key, struct qhash_head *link)
{
    struct PINT_group *eg = qhash_entry(link, struct PINT_group, link);

    if(!strcmp(eg->name, (char *)key))
    {
        return 1;
    }
    return 0;
}

static int PINT_events_compare(void *key, struct qhash_head *link)
{
    struct PINT_event *e = qhash_entry(link, struct PINT_event, link);

    if(!strcmp(e->name, (char *)key))
    {
        return 1;
    }
    return 0;
}

int PINT_event_init(enum PINT_event_method method)
{
    int ret;

    events_table = qhash_init(PINT_events_compare, quickhash_string_hash, 1024);
    if(!events_table)
    {
        return -PVFS_ENOMEM;
    }

    groups_table = qhash_init(PINT_group_compare, quickhash_string_hash, 1024);
    if(!groups_table)
    {
        qhash_finalize(events_table);
        return -PVFS_ENOMEM;
    }

    ret = PINT_event_define_group("defaults", &default_group);
    if(ret < 0)
    {
        qhash_finalize(events_table);
        qhash_finalize(groups_table);
        return ret;
    }

    switch(method)
    {
        case PINT_EVENT_TRACE_TAU:
#if defined(HAVE_TAU)
            PINT_event_tau_init();
            break;
#else
            return -PVFS_ENOSYS;
#endif
    }

    return(0);
}

void PINT_event_finalize(void)
{

#if defined(HAVE_TAU)
    PINT_event_tau_fini();
#endif

    qhash_finalize(groups_table);
    qhash_finalize(events_table);
    return;
}

int PINT_event_thread_start(char *name)
{
    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

#if defined(HAVE_TAU)
    PINT_event_tau_thread_init(name);
#endif

    return 0;
}

int PINT_event_thread_stop(void)
{
    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

#if defined(HAVE_TAU)
    PINT_event_tau_thread_fini();
    return 0;
#endif

    return 0;
}

int PINT_event_enable(const char *events)
{
    struct qhash_head *entry;
    struct PINT_event *event;
    struct PINT_group *group;
    char **event_strings;
    int count, i;
    int ret = 0;

    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

    count = PINT_split_string_list(&event_strings, events);

    for(i = 0; i < count; ++i)
    {
        entry = qhash_search(events_table, event_strings[i]);
        if(entry)
        {
            event = qhash_entry(entry, struct PINT_event, link);
            PINT_event_enabled_mask |= event->mask;
        }
        else
        {
            entry = qhash_search(groups_table, event_strings[i]);
            if(entry)
            {
                group = qhash_entry(entry, struct PINT_group, link);
                PINT_event_enabled_mask |= group->mask;
            }
        }

        if(!strcmp(events, "all"))
        {
            PINT_event_enabled_mask = 0xFFFFFFFF;
            goto done;
        }

        if(!entry)
        {
            gossip_err("Unknown event or event group: %s\n", event_strings[i]);
            ret = -PVFS_EINVAL;
            goto done;
        }
    }

done:
    for(i = 0; i < count; ++i)
    {
        free(event_strings[i]);
    }
    free(event_strings);

    return ret;
}

int PINT_event_disable(const char *events)
{
    struct qhash_head *entry;
    struct PINT_event *event;
    struct PINT_group *group;
    char **event_strings;
    int count, i;
    int ret = 0;

    count = PINT_split_string_list(&event_strings, events);

    for(i = 0; i < count; ++i)
    {
        entry = qhash_search(events_table, event_strings[i]);
        if(entry)
        {
            event = qhash_entry(entry, struct PINT_event, link);
            PINT_event_enabled_mask &= ~(event->mask);
        }
        else
        {
            entry = qhash_search(groups_table, event_strings[i]);
            if(entry)
            {
                group = qhash_entry(entry, struct PINT_group, link);
                PINT_event_enabled_mask &= ~(group->mask);
            }
        }

        if(!entry)
        {
            gossip_err("Unknown event or event group: %s\n", event_strings[i]);
            ret = -PVFS_EINVAL;
            goto done;
        }
    }

    if(!strcmp(events, "none"))
    {
        PINT_event_enabled_mask = 0;
    }

done:
    for(i = 0; i < count; ++i)
    {
        free(event_strings[i]);
    }
    free(event_strings);

    return ret;
}

int PINT_event_define_group(const char *name, PINT_event_group *group)
{
    struct PINT_group *g;

    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

    g = malloc(sizeof(*g));
    if(!g)
    {
        return -PVFS_ENOMEM;
    }
    memset(g, 0, sizeof(*g));

    g->name = strdup(name);
    if(!g->name)
    {
        return -PVFS_ENOMEM;
    }

    INIT_QLIST_HEAD(&g->events);

    qhash_add(groups_table, g->name, &g->link);
    id_gen_fast_register(&g->id, g);

    *group = g->id;
    return 0;
}

int PINT_event_define_event(PINT_event_group *group,
                            char *name,
                            char *format_start,
                            char *format_end,
                            PINT_event_type *et)
{
    struct PINT_group *g;
    PINT_event_group ag;
    struct PINT_event *event;

    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

    if(!group)
    {
        /* use default group */
        ag = default_group;
    }
    else
    {
        ag = *group;
    }

    event = malloc(sizeof(*event));
    if(!event)
    {
        return -PVFS_ENOMEM;
    }
    memset(event, 0, sizeof(*event));

    event->name = strdup(name);
    if(!event->name)
    {
        free(event);
        return -PVFS_ENOMEM;
    }

#ifdef HAVE_TAU
    Ttf_event_define(name, format_start, format_end, (int *)&event->type);
#endif

    event->group = ag;
    event->mask = (1 << event_count);
    ++event_count;

    g = id_gen_fast_lookup(ag);
    g->mask |= event->mask;
    qlist_add(&event->group_link, &g->events);
    qhash_add(events_table, event->name, &event->link);

    id_gen_fast_register(et, event);
    return 0;
}

int PINT_event_start_event(
    PINT_event_type type, int process_id, int *thread_id, PINT_event_id *id, ...)
{
    va_list ap;
    struct PINT_event *event;

    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

event = id_gen_fast_lookup(type);
    if(event && (event->mask & PINT_event_enabled_mask))
    {
        va_start(ap, id);
#ifdef HAVE_TAU
        Ttf_EnterState_info_va(event->type, process_id, thread_id, (int *)id, ap);
#endif
        va_end(ap);
    }
    return 0;
}

int PINT_event_end_event(
    PINT_event_type type, int process_id, int *thread_id, PINT_event_id id, ...)
{
    va_list ap;
    struct PINT_event *event;

    if(!groups_table)
    {
        /* assume that the events interface just hasn't been initialized */
        return 0;
    }

    event = id_gen_fast_lookup(type);
    if(event && (event->mask & PINT_event_enabled_mask))
    {
        va_start(ap, id);
#ifdef HAVE_TAU
        Ttf_LeaveState_info_va(event->type, process_id, thread_id, id, ap);
#endif
        va_end(ap);
    }
    return 0;
}

/******************************************************************************/
#if defined(HAVE_TAU)

void PINT_event_tau_init(void) {
    char* foldername = "/tmp/";
    char* prefix = "pvfs2";
    int bufsz = 0; //use default

    Ttf_init(getpid(), foldername, prefix, bufsz);

    return;
}


void PINT_event_tau_fini(void) {
    Ttf_finalize();
    return;
}

static void PINT_event_tau_thread_init(char* gname) {
    int tid = 0;
    struct tau_thread_group_info tg_info;
    strncpy(tg_info.name, gname, sizeof(tg_info.name));
    tg_info.buffer_size = PINT_event_default_buffer_size;
    tg_info.max = PINT_event_default_max_traces;
    int isnew = 0;
    Ttf_thread_start(&tg_info,  &tid, &isnew);

    return;
}


static void PINT_event_tau_thread_fini() {
    Ttf_thread_stop();
    return;
}

#endif /* HAVE_TAU */
/******************************************************************************/


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
