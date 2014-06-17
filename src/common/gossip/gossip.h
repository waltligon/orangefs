/*
 * (C) 2001 Clemson University and The University of Chicago
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#ifndef GOSSIP_H
#define GOSSIP_H

#ifdef __KERNEL__

/* The kernel does not really use Gossip. This lets it pretend. It would
 * probably be a good idea to use printk directly in the kernel to
 * reduce the maintenence burden on the Linux developers. */

/* This is duplicated from below and declared in pvfs2-mod.c. */
extern uint64_t gossip_debug_mask;

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, format, f...) do {} while(0)
#else

/* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, format, f...)                  \
do {                                                      \
    if (gossip_debug_mask & mask)                         \
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
#define gossip_lerr(format, f...)                  \
do {                                               \
    gossip_err("%s line %d: " format, __FILE__ , __LINE__ , ##f); \
} while(0)

#else /* __KERNEL__ */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

enum gossip_logstamp {
    GOSSIP_LOGSTAMP_NONE,
    GOSSIP_LOGSTAMP_USEC,
    GOSSIP_LOGSTAMP_DATETIME,
    GOSSIP_LOGSTAMP_THREAD
};
#define GOSSIP_LOGSTAMP_DEFAULT GOSSIP_LOGSTAMP_USEC

struct gossip_mech {
    int (*startup)(void *, va_list);
    int (*log)(char *, size_t, void *);
    void (*shutdown)(void *);
    int (*reset)(void *);
    void *data;
};

/*
 * setup
 */

void gossip_disable(void);
int gossip_enable(struct gossip_mech *, ...);
int gossip_reset(void);

/*
 * parameters
 */

extern uint64_t gossip_debug_mask;
extern int gossip_debug_on;

void gossip_get_debug_mask(int *, uint64_t *);
void gossip_set_debug_mask(int, uint64_t);
int gossip_debug_enabled(uint64_t mask);
void gossip_get_logstamp(enum gossip_logstamp *);
void gossip_set_logstamp(enum gossip_logstamp);

/*
 * log functions
 */

void gossip_backtrace(void);
int gossip_vprint(char, const char *, va_list);
int gossip_print(char, const char *, ...);
int gossip_debug(uint64_t, const char *, ...);
int gossip_err(const char *, ...);
int gossip_perf_log(const char *, ...);

#define gossip_ldebug(mask, ...) do { \
        gossip_debug(mask, "%s:%d: ", __FILE__, __LINE__); \
        gossip_debug(mask, __VA_ARGS__); \
    } while (0);

#define gossip_lerr(...) do { \
        gossip_err("%s:%d: ", __FILE__, __LINE__); \
        gossip_err(__VA_ARGS__); \
    } while (0);

/*
 * log mechanisms
 */

extern struct gossip_mech gossip_mech_stderr;
extern struct gossip_mech gossip_mech_syslog;
extern struct gossip_mech gossip_mech_file;

#endif /* __KERNEL__ */

#endif /* GOSSIP_H */
