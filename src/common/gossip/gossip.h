/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/*
 * April 2001
 *
 * This is a basic application logging facility.  It uses printf style
 * formatting and provides several mechanisms for output. 
 */

#ifndef __GOSSIP_H
#define __GOSSIP_H

#include <syslog.h>
#include <pvfs2-config.h>

/********************************************************************
 * Visible interface
 */

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
    int mask);

#ifdef GOSSIP_ENABLE_BACKTRACE
void gossip_backtrace(void);
#endif

#ifdef __GNUC__

/* do printf style type checking if built with gcc */
int __gossip_debug(
    int mask,
    const char *format,
    ...) __attribute__ ((format(printf, 2, 3)));
int gossip_err(
    const char *format,
    ...) __attribute__ ((format(printf, 1, 2)));

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, format, f...)	\
	do { } while(0)
#else
extern int gossip_debug_on;
extern int gossip_debug_mask;
extern int gossip_facility;

    /* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, format, f...)						\
        do {										\
	    if ((gossip_debug_on) && (gossip_debug_mask & mask) && (gossip_facility))	\
            {										\
	        __gossip_debug(mask, format, ##f);					\
            }										\
	} while(0)
#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, format, f...)			\
    do {							\
	gossip_debug(mask, "%s line %d: ", __FILE__, __LINE__);	\
	gossip_debug(mask, format, ##f);			\
    } while(0)

#ifdef GOSSIP_ENABLE_BACKTRACE
#define gossip_lerr(format, f...)			\
    do {						\
	gossip_err("%s line %d: ", __FILE__, __LINE__);	\
	gossip_err(format, ##f);			\
	gossip_backtrace();				\
	} while(0)
#else
#define gossip_lerr(format, f...)			\
    do {						\
	gossip_err("%s line %d: ", __FILE__, __LINE__);	\
	    gossip_err(format, ##f);                    \
	} while(0)
#endif
#else /* ! __GNUC__ */

int __gossip_debug(
    int mask,
    const char *format,
    ...);
int __gossip_debug_stub(
    int mask,
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif /* __GOSSIP_H */
