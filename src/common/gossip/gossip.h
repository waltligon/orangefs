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

#include <stdint.h>
#include "syslog.h"
#include "pvfs2-config.h"

/********************************************************************
 * Visible interface
 */

/* what type of timestamp to place in msgs */
enum gossip_logstamp
{
    GOSSIP_LOGSTAMP_NONE = 0,
    GOSSIP_LOGSTAMP_USEC = 1,
    GOSSIP_LOGSTAMP_DATETIME = 2
};
#define GOSSIP_LOGSTAMP_DEFAULT GOSSIP_LOGSTAMP_USEC

int gossip_enable_syslog(
    int priority);
int gossip_enable_stderr(
    void);
int gossip_enable_file(
    const char *filename,
    const char *mode);
int gossip_disable(
    void);
int gossip_set_debug_mask(
    int debug_on,
    uint64_t mask);
int gossip_get_debug_mask(
    int *debug_on,
    uint64_t *mask);
int gossip_set_logstamp(
    enum gossip_logstamp ts);

#ifdef GOSSIP_ENABLE_BACKTRACE
void gossip_backtrace(void);
#endif

#ifdef __GNUC__

/* do printf style type checking if built with gcc */
int __gossip_debug(
    uint64_t mask,
    const char *format,
    ...) __attribute__ ((format(printf, 2, 3)));
int gossip_err(
    const char *format,
    ...) __attribute__ ((format(printf, 1, 2)));

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, format, f...) do {} while(0)
#else
extern int gossip_debug_on;
extern int gossip_facility;
extern uint64_t gossip_debug_mask;

/* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, format, f...)                  \
do {                                                      \
    if ((gossip_debug_on) && (gossip_debug_mask & mask) &&\
        (gossip_facility))                                \
    {                                                     \
        __gossip_debug(mask, format, ##f);                \
    }                                                     \
} while(0)
#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, format, f...)                  \
do {                                                       \
    gossip_debug(mask, "%s: " format, __func__ , ##f); \
} while(0)

#ifdef GOSSIP_ENABLE_BACKTRACE
#define gossip_lerr(format, f...)                  \
do {                                               \
    gossip_err("%s line %d: " format, __FILE__ , __LINE__ , ##f); \
    gossip_backtrace();                            \
} while(0)
#else
#define gossip_lerr(format, f...)                  \
do {                                               \
    gossip_err("%s line %d: " format, __FILE__ , __LINE__ , ##f); \
} while(0)
#endif
#else /* ! __GNUC__ */

int __gossip_debug(
    uint64_t mask,
    const char *format,
    ...);
int __gossip_debug_stub(
    uint64_t mask,
    const char *format,
    ...);
int gossip_err(
    const char *format,
    ...);

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug __gossip_debug_stub
#define gossip_ldebug __gossip_debug_stub
#else
#define gossip_debug __gossip_debug
#define gossip_ldebug __gossip_debug
#endif /* GOSSIP_DISABLE_DEBUG */

#define gossip_lerr gossip_err

#endif /* __GNUC__ */

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
