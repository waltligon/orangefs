/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_PERF_COUNTER_H
#define __PINT_PERF_COUNTER_H

#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "gen-locks.h"

enum {
PERF_DEFAULT_TIME_INTERVAL_SECS = 300,
PERF_DEFAULT_HISTORY_SIZE       = 6,
};

/** flag that indicates that values for a particular key should be preserved
 * across rollover rather than reset to 0
 */
#define PINT_PERF_PRESERVE 1

/* TODO: this may be moved in the long term; it is an enumeration of keys
 * that pvfs2-server supports (used by trove and flow counters)
 */
enum PINT_server_perf_keys
{
    PINT_PERF_READ = 0,
    PINT_PERF_WRITE = 1,
    PINT_PERF_METADATA_READ = 2,
    PINT_PERF_METADATA_WRITE = 3,
    PINT_PERF_METADATA_DSPACE_OPS = 4,
    PINT_PERF_METADATA_KEYVAL_OPS = 5,
    PINT_PERF_REQSCHED = 6
};

/** enumeration of valid measurement operations */
enum PINT_perf_ops
{
    PINT_PERF_ADD = 0,
    PINT_PERF_SUB = 1,
    PINT_PERF_SET = 2,
};

/** enumeration of runtime options */
enum PINT_perf_option
{
    PINT_PERF_HISTORY_SIZE = 1,  /**< sets/gets the history size */
    PINT_PERF_KEY_COUNT = 2      /**< gets the key coung (cannot be set) */
};

/** describes a single key to be stored in the perf counter interface */
struct PINT_perf_key
{
    const char* key_name;    /**< string name for key */
    int key;           /**< integer representation of key */
    int flag;          /**< flags that modify behavior of values in this key */
};

/** struct representing a perf counter instance */
struct PINT_perf_counter
{
    gen_mutex_t mutex;
    struct PINT_perf_key* key_array;     /**< keys (provided by initialize()) */
    int key_count;                       /**< number of keys */
    int history_size;                    /**< number of history intervals */
    /** matrix of statistics, first dimension is key, second is history */
    int64_t** value_matrix; 
    uint64_t* start_time_array_ms;        /**< array of start times */
    uint64_t* interval_array_ms;          /**< array of interval lengths */
};

/** server-wide perf counter structure */
extern struct PINT_perf_counter *PINT_server_pc;

struct PINT_perf_counter* PINT_perf_initialize(
    struct PINT_perf_key* key_array);

void PINT_perf_finalize(
    struct PINT_perf_counter* pc);

void PINT_perf_reset(
    struct PINT_perf_counter* pc);

void __PINT_perf_count(
    struct PINT_perf_counter* pc,
    int key, 
    int64_t value,
    enum PINT_perf_ops op);

#ifdef __PVFS2_DISABLE_PERF_COUNTERS__
    #define PINT_perf_count(w,x,y,z) do{}while(0)
#else
    #define PINT_perf_count __PINT_perf_count
#endif

void PINT_perf_rollover(
    struct PINT_perf_counter* pc);

int PINT_perf_set_info(
    struct PINT_perf_counter* pc,
    enum PINT_perf_option option,
    unsigned int arg);

int PINT_perf_get_info(
    struct PINT_perf_counter* pc,
    enum PINT_perf_option option,
    unsigned int* arg);

void PINT_perf_retrieve(
    struct PINT_perf_counter* pc,        
    int64_t** value_matrix,
    uint64_t* start_time_array_ms,       
    uint64_t* interval_array_ms,         
    int max_key,                         
    int max_history);     

char* PINT_perf_generate_text(
    struct PINT_perf_counter* pc,
    int max_size);

#endif /* __PINT_PERF_COUNTER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
