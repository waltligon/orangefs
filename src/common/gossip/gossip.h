/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \defgroup gossip gossip logging interface
 *
 * This is a basic application logging facility.  It uses printf style
 * formatting and provides several mechanisms for output.
 *
 * @{
 */

/** \file
 *
 *  Declarations for the gossip logging interface.
 */

#ifndef __GOSSIP_H
#define __GOSSIP_H

#ifdef WIN32
#include "wincommon.h"
#endif

#ifndef __KERNEL__
#include <stdint.h>
#include <stdarg.h>
#ifndef WIN32
#include "syslog.h"
#endif
#endif
#include "gossip.h"
#include "quicklist.h"
#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "pvfs2-debug.h"

/********************************************************************
 * Visible interface
 */

extern PVFS_debug_mask gossip_debug_mask;

extern int gossip_debug_on;

extern int gossip_facility;

struct gossip_mask_stack_s
{
    int                 debug_on;
    int                 debug_facility;
    PVFS_debug_mask     debug_mask;
    struct qlist_head   debug_stack;
};
typedef struct gossip_mask_stack_s gossip_mask_stack;


#define GOSSIP_BUF_SIZE 5120

/* what type of timestamp to place in msgs */
enum gossip_logstamp
{
    GOSSIP_LOGSTAMP_NONE = 0,
    GOSSIP_LOGSTAMP_USEC = 1,
    GOSSIP_LOGSTAMP_DATETIME = 2,
    GOSSIP_LOGSTAMP_THREAD = 3
};
#define GOSSIP_LOGSTAMP_DEFAULT GOSSIP_LOGSTAMP_USEC

/* Keep a simplified version for the kmod */
#ifdef __KERNEL__

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, format, f...) do {} while(0)
#else

#define gossip_isset(__m1,__m2) ((__m1.mask1 & (__m2).mask1) || \
                                 (__m1.mask2 & (__m2).mask2))

/* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, format, f...)                  \
do {                                                      \
    if (gossip_isset(gossip_debug_mask, mask))            \
    {                                                     \
        printk(format, ##f);                              \
    }                                                     \
} while(0)
#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, format, f...)                  \
do {                                                       \
    gossip_debug(mask, "%s: " format, __func__ , ##f); \
} while(0)

#define gossip_err printk
#define gossip_log printk
#define gossip_lerr(format, f...)                  \
do {                                               \
    gossip_err("%s line %d: " format, __FILE__ , __LINE__ , ##f); \
} while(0)

#else /* __KERNEL__ */

/* stdio is needed by gossip_debug_fp declaration for FILE* */
#include <stdio.h>

int gossip_enable_syslog(int priority);
int gossip_enable_stderr(void);
int gossip_enable_file(const char *filename, const char *mode);
int gossip_reopen_file(const char *filename, const char *mode);
int gossip_disable(void);
int gossip_set_debug_mask(int debug_on, PVFS_debug_mask mask);
int gossip_get_debug_mask(int *debug_on, PVFS_debug_mask *mask);
int gossip_push_mask(int debug_on, PVFS_debug_mask *mask);
int gossip_pop_mask(int *debug_on, PVFS_debug_mask *mask);

int gossip_set_logstamp(enum gossip_logstamp ts);

void gossip_backtrace(void);

#ifdef __GNUC__

/* do printf style type checking if built with gcc */
int __gossip_debug(PVFS_debug_mask mask,
                   char prefix,
                   const char *format,
                   ...) __attribute__ ((format(printf, 3, 4)));
int gossip_err(const char *format,
               ...) __attribute__ ((format(printf, 1, 2)));
int gossip_log(const char *format,
               ...) __attribute__ ((format(printf, 1, 2)));
int __gossip_debug_va(PVFS_debug_mask mask,
                      char prefix,
                      const char *format,
                      va_list ap);
int gossip_debug_fp(FILE *fp,
                    char prefix,
                    enum gossip_logstamp ts,
                    const char *format,
                    ...) __attribute__ ((format(printf, 4, 5)));

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, format, f...) do {} while(0)
#define gossip_perf_log(format, f...) do {} while(0)
#define gossip_isset(__m1, __m2) 0
#define gossip_debug_enabled(__m) 0
#else /* GOSSIP_DISABLE_DEBUG */

#define gossip_isset(__m1,__m2) ((__m1.mask1 & (__m2).mask1) || \
                                 (__m1.mask2 & (__m2).mask2))

#define gossip_debug_enabled(__m)  \
                  (gossip_debug_on && gossip_isset(gossip_debug_mask, __m))

#if 0
/* Gossip Debug Mask inline funccombine multiple mask values into one
 * This is done at runtime so it is best avoided where possible but it
 * can be very convenient in some places
 */
static inline PVFS_debug_mask GDM_OR(int count, ...)
{
    PVFS_debug_mask maskA = {GOSSIP_NO_DEBUG_INIT};
    va_list args;
    va_start(args, count);
    if (count < 0)
    {
        return maskA;
    }
    while (count--)
    {
        PVFS_debug_mask maskB = va_arg(args, PVFS_debug_mask);;
        maskA.mask1 = maskA.mask1 | maskB.mask1;
        maskA.mask2 = maskA.mask2 | maskB.mask2;
    }
    va_end(args);
    return mask;
}

static inline PVFS_debug_mask GDM_AND(int count, ...)
{
    PVFS_debug_mask maskA = {__DEBUG_ALL_INIT};
    va_list args;
    va_start(args, count);
    if (count < 0)
    {
        return maskA;
    }
    while (count--)
    {
        PVFS_debug_mask maskB = va_arg(args, PVFS_debug_mask);;
        maskA.mask1 = maskA.mask1 & maskB.mask1;
        maskA.mask2 = maskA.mask2 & maskB.mask2;
    }
    va_end(args);
    return mask;
}

static inline int GDM_ZERO(PVFS_debug_mask mask)
{
    return mask.mask1 || mask.mask2;
}
#endif

/* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, format, f...)                  \
do {                                                      \
    if ((gossip_debug_on) &&                              \
        gossip_isset(gossip_debug_mask, mask) &&          \
        (gossip_facility))                                \
    {                                                     \
        __gossip_debug(mask, '?', format, ##f);           \
    }                                                     \
} while(0)
#define gossip_perf_log(format, f...)                     \
do {                                                      \
    if ((gossip_debug_on) &&                              \
        gossip_isset(gossip_debug_mask, GOSSIP_PERFCOUNTER_DEBUG) &&  \
        (gossip_facility))                                \
    {                                                     \
        __gossip_debug(GOSSIP_PERFCOUNTER_DEBUG, 'P',     \
            format, ##f);                                 \
    }                                                     \
} while(0)

#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, format, f...)                  \
do {                                                       \
    gossip_debug(mask, "%s: " format, __func__ , ##f);     \
} while(0)

#define gossip_lerr(format, f...)                          \
do {                                                       \
    gossip_err("%s line %d: " format, __FILE__ , __LINE__ , ##f); \
    gossip_backtrace();                                    \
} while(0)
#else /* ! __GNUC__ */

#define gossip_perf_log(format, ...)                      \
do {                                                      \
    if ((gossip_debug_on) &&                              \
        gossip_isset(gossip_debug_mask, GOSSIP_PERFCOUNTER_DEBUG) &&  \
        (gossip_facility))                                \
    {                                                     \
        __gossip_debug(GOSSIP_PERFCOUNTER_DEBUG, 'P',     \
            format, __VA_ARGS__);                         \
    }                                                     \
} while(0)

int __gossip_debug(PVFS_debug_mask mask, char prefix, const char *format, ...);
int __gossip_debug_va(PVFS_debug_mask mask,
                      char prefix,
                      const char *format,
                      va_list ap);
int __gossip_debug_stub(PVFS_debug_mask mask,
                        char prefix,
                        const char *format,
                        ...);
int gossip_log(const char *format, ...);
int gossip_err(const char *format, ...);

#ifdef GOSSIP_DISABLE_DEBUG
#ifdef WIN32
#define gossip_debug(__m, __f, ...) __gossip_debug_stub(__m, '?', __f, __VA_ARGS__);
#define gossip_ldebug(__m, __f, ...) __gossip_debug_stub(__m, '?', __f, __VA_ARGS__);
#else /* WIN32 */
#define gossip_debug(__m, __f, f...) __gossip_debug_stub(__m, '?', __f, ##f);
#define gossip_ldebug(__m, __f, f...) __gossip_debug_stub(__m, '?', __f, ##f);
#endif /* WIN32 */
#define gossip_isset(__m1, __m2) 0
#define gossip_debug_enabled(__m) 0
#else /* GOSSIP_DISABLE_DEBUG */
#ifdef WIN32
#define gossip_debug(__m, __f, ...) __gossip_debug(__m, '?', __f, __VA_ARGS__);
#define gossip_ldebug(__m, __f, ...) __gossip_debug(__m, '?', __f, __VA_ARGS__);
#else /* WIN32 */
#define gossip_debug(__m, __f, f...) __gossip_debug(__m, '?', __f, ##f);
#define gossip_ldebug(__m, __f, f...) __gossip_debug(__m, '?', __f, ##f);
#endif /* WIN32 */
#define gossip_isset(__m1, __m2) ((__m1.mask1 & (__m2).mask1) || \
                                  (__m1.mask2 & (__m2).mask2))
#define gossip_debug_enabled(__m) ((gossip_debug_on != 0) && \
                                   gossip_isset(gossip_debug_mask, __m))
#endif /* GOSSIP_DISABLE_DEBUG */

#define gossip_lerr gossip_err

#endif /* __GNUC__ */

#endif /* __KERNEL__ */

#endif /* __GOSSIP_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
