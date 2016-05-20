/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_PERF_COUNTER_H
#define __PINT_PERF_COUNTER_H

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "gen-locks.h"

enum PINT_perf_defaults
{
    PERF_DEFAULT_UPDATE_INTERVAL = 1000, /* msecs */
    PERF_DEFAULT_HISTORY_SIZE    = 10,
};

/** flag that indicates that values for a
 * particular key should be preserved
 * across rollover rather than reset to 0
 */
#define PINT_PERF_PRESERVE 1

/** enumeration of valid measurement operations */
enum PINT_perf_ops
{
    PINT_PERF_ADD = 0,
    PINT_PERF_SUB = 1,
    PINT_PERF_SET = 2,
    PINT_PERF_START = 3, /* use with a timer */
    PINT_PERF_END = 4,   /* use with a timer */
};

/** enumeration of runtime options */
enum PINT_perf_option
{
    PINT_PERF_UPDATE_HISTORY  = 1, /**< sets/gets the history size */
    PINT_PERF_KEY_COUNT       = 2, /**< gets the key count (cannot be set) */
    PINT_PERF_UPDATE_INTERVAL = 3  /**< sets/gets the update interval */
};

/** describes a single key to be stored in the perf counter interface */
struct PINT_perf_key
{
    const char* key_name;    /**< string name for key */
    int key;           /**< integer representation of key */
    int flag;          /**< flags that modify behavior of values in this key */
};

/** struct holding one sample for a multi-sample set of counters */
struct PINT_perf_sample
{
    PVFS_time start_time_ms; /**< time this sample was started */
    PVFS_time interval_ms;   /**< ms this sample lasted */
    union {
        void *v;
        int64_t *c;
        struct PINT_perf_timer *t;
    } value;  /**< this points to an array[key_count] of counters */
    struct PINT_perf_sample *next; /**< link to next sample in the list of */
                                   /**< history sameples */
};

/** struct representing a multi-sample set of perf counters */
struct PINT_perf_counter
{
    gen_mutex_t mutex;
    struct PINT_perf_key* key_array;     /**< keys (provided by initialize()) */
    enum PINT_perf_type cnt_type;        /**< counter or timer */
    int perf_counter_size;               /**< number of bytes in single cnt */
    int key_count;                       /**< number of keys */
    int history;                         /**< number of history intervals */
    int running;                         /**< true if a rollover running */
    int interval;                        /**< milliseconds between rollovers */
    struct PINT_perf_sample *sample;     /**< list of samples for this counter */
    int (*start_rollover)(struct PINT_perf_counter *pc,
                          struct PINT_perf_counter *tpc);
};


/** server-wide perf counter structure  - if this is server-wide, why is
 * it in common?
 */
extern struct PINT_perf_key server_keys[];

extern struct PINT_perf_key server_tkeys[];

/* this is rediculous, this is defined in trove, but "owned" by the
 * server!!!
 */
extern struct PINT_perf_counter *PINT_server_pc;

extern struct PINT_perf_counter *PINT_server_tpc;

struct PINT_perf_counter *PINT_perf_initialize(
        enum PINT_perf_type cnt_type,
        struct PINT_perf_key *key_array,
        int (*start_rollover)(struct PINT_perf_counter *pc,
                              struct PINT_perf_counter *tpc));

void PINT_perf_finalize(struct PINT_perf_counter *pc);

void PINT_perf_reset(struct PINT_perf_counter *pc);

void __PINT_perf_count(
        struct PINT_perf_counter *pc,
        int key, 
        int64_t value,
        enum PINT_perf_ops op);

void __PINT_perf_timer_start(struct timespec *start_time);

void __PINT_perf_timer_end(
        struct PINT_perf_counter *tpc,
        int key, 
        struct timespec *start_time);

#ifdef __PVFS2_DISABLE_PERF_COUNTERS__
    #define PINT_perf_count(w,x,y,z) do{}while(0)
    #define PINT_perf_timer_start(w) do{}while(0)
    #define PINT_perf_timer_end(w,x,y) do{}while(0)
#else
    #define PINT_perf_count __PINT_perf_count
    #define PINT_perf_timer_start __PINT_perf_timer_start
    #define PINT_perf_timer_end __PINT_perf_timer_end
#endif

void PINT_perf_rollover(struct PINT_perf_counter *pc);

int PINT_perf_set_info(
        struct PINT_perf_counter *pc,
        enum PINT_perf_option option,
        unsigned int arg);

int PINT_perf_get_info(
        struct PINT_perf_counter *pc,
        enum PINT_perf_option option,
        unsigned int* arg);

#if 0
int PINT_perf_start_rollover(void);
#endif

void PINT_perf_retrieve(
        struct PINT_perf_counter *pc,        
        int64_t *value_array,
        int array_size);
#if 0
        int max_key,                         
        int max_history);     
#endif

char* PINT_perf_generate_text(
        struct PINT_perf_counter *pc,
        int max_size);

void PINT_free_pc (struct PINT_perf_counter *pc);

#endif /* __PINT_PERF_COUNTER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
