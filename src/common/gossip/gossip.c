/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup gossip
 *
 *  Implementation of gossip interface.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#ifdef WIN32
#include "wincommon.h"
#else
#include <syslog.h>
#include <sys/time.h>
#endif

#include "pvfs2-internal.h"
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include "gossip.h"
#include "gen-locks.h"

/* These gobal vars are used to control calls to gossip_debug()
 */

/** controls whether PVFS debugging is on or off */
int gossip_debug_on = 0;

int gossip_facility;

PVFS_debug_mask gossip_debug_mask;

/** controls the mask level for debugging messages */
static gossip_mask_stack
             gossip_debug_stack = {.debug_on = 0, .debug_facility = 0,
                    .debug_mask = {.mask1 = 0, .mask2 = 0},
                   .debug_stack = {.next = &gossip_debug_stack.debug_stack,
                                   .prev = &gossip_debug_stack.debug_stack}};

enum
{
    GOSSIP_STDERR = 1,
    GOSSIP_FILE = 2,
    GOSSIP_SYSLOG = 4
};

/* determines which logging facility to use. Default to stderr to begin with.
 */
int gossip_facility = GOSSIP_STDERR;

/* Using gossip to debug gossip can be problematic so this is a simple
 * macro that can be set on or off to enable direct printing to stderr
 */
#define GOSSIP_INTERNAL 0
#if defined(GOSSIP_INTERNAL) && GOSSIP_INTERNAL
#define gossip_internal(format, f...)                  \
do {                                                   \
    fprintf(stderr, format, ##f);                      \
} while(0)
#else
#define gossip_internal(format, f...) do {} while(0)
#endif /* GOSSIP_INTERNAL */

/* file handle used for file logging */
static FILE *internal_log_file = NULL;

/* syslog priority setting */
#ifndef WIN32
static int internal_syslog_priority = LOG_INFO;
#endif

/* what type of timestamp to put on logs */
static enum gossip_logstamp internal_logstamp = GOSSIP_LOGSTAMP_DEFAULT;

/*****************************************************************
 * prototypes
 */
static int gossip_disable_stderr(void);
static int gossip_disable_file(void);
static int gossip_disable_syslog(void);

static int gossip_debug_fp_va(FILE *fp,
                              char prefix,
                              const char *format,
                              va_list ap,
                              enum gossip_logstamp ts);
static int gossip_debug_syslog(char prefix,
                               const char *format,
                               va_list ap);
static int gossip_msg(const char prefix, const char *format, va_list ap);
static int gossip_msg_syslog(const char prefix, const char *format, va_list ap);


/*****************************************************************
 * visible functions
 */

/** Turns on syslog logging facility.  The priority argument is a
 *  combination of the facility and level to use, as seen in the
 *  syslog(3) man page.
 *
 *  \return 0 on success, -errno on failure.
 */
#ifdef WIN32
/** Only a stub on Windows
 *  TODO: possibly add logging to Windows Event Log
 */
int gossip_enable_syslog(int priority)
{
    return 0;
}
#else
int gossip_enable_syslog(int priority)
{
    /* Debug setting/clearing debug_mask */
    gossip_internal( "Gossip_enable_syslog\n");
    gossip_internal( "gdm = %d, (%lx , %lx)\n", 
                    gossip_debug_on, gossip_debug_mask.mask1, 
                                     gossip_debug_mask.mask2);

    /* turn off any running facility */
    gossip_disable(); /* includes  a push */

    internal_syslog_priority = priority;
    gossip_facility = GOSSIP_SYSLOG;

    openlog("PVFS2", 0, LOG_DAEMON);

    /* restore the logging settings */
    gossip_pop_mask(NULL, NULL);

    gossip_internal( "final enable syslog gdm = %d, (%lx , %lx)\n\n", 
                    gossip_debug_on, gossip_debug_mask.mask1, 
                                     gossip_debug_mask.mask2);
    return 0;
}
#endif

/** Turns on logging to stderr.
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_enable_stderr(void)
{
    /* Debug setting/clearing debug_mask */
    gossip_internal( "Gossip_enable_stderr\n");
    gossip_internal( "gdm = %d, (%lx , %lx)\n", 
                    gossip_debug_on, gossip_debug_mask.mask1, 
                                     gossip_debug_mask.mask2);

    /* turn off any running facility */
    gossip_disable();  /* includes a push */

    gossip_facility = GOSSIP_STDERR;

    /* restore the logging settings */
    gossip_pop_mask(NULL, NULL);

    gossip_internal( "final enable stderr gdm = %d, (%lx , %lx)\n\n", 
                    gossip_debug_on, gossip_debug_mask.mask1, 
                                     gossip_debug_mask.mask2);
    return 0;
}

/** Turns on logging to a file.  The filename argument indicates which
 *  file to use for logging messages, and the mode indicates whether the
 *  file should be truncated or appended (see fopen() man page).
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_enable_file(const char *filename, const char *mode)
{
    gossip_internal( "Gossip_enable_file\n");
    /* turn off any running facility */
    gossip_disable();  /* includes a push */

    internal_log_file = fopen(filename, mode);
    if (!internal_log_file)
    {
        /* should recover? */
        return -errno;
    }

    gossip_facility = GOSSIP_FILE;

    /* restore the logging settings */
    gossip_pop_mask(NULL, NULL);

    /* Debug setting/clearing debug_mask */
    gossip_debug(GOSSIP_GOSSIP_DEBUG,
                 "final enable file  gdm = %d, (%lx , %lx)\n\n", 
                 gossip_debug_on, gossip_debug_mask.mask1, 
                                  gossip_debug_mask.mask2);
    return 0;
}

int gossip_reopen_file(const char *filename, const char *mode)
{
    if(gossip_facility != GOSSIP_FILE)
    {
        return -EINVAL;
    }

    /* close the file */
    gossip_disable_file();

    /* open the file */
    gossip_enable_file(filename, mode);
    return 0;
}

/** Turns off any active logging facility and disables debugging.
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_disable(void)
{
    int ret = -EINVAL;
    gossip_internal( "Gossip_disable\n");

    /* if stack is not initialized yet, do it now */
    if (gossip_debug_stack.debug_stack.next == NULL ||
        gossip_debug_stack.debug_stack.prev == NULL)
    {
        INIT_QLIST_HEAD(&gossip_debug_stack.debug_stack);
    }

    gossip_push_mask(gossip_debug_on, &gossip_debug_mask);

    switch (gossip_facility)
    {
    case GOSSIP_STDERR:
        ret = gossip_disable_stderr();
        break;
    case GOSSIP_FILE:
        ret = gossip_disable_file();
        break;
    case GOSSIP_SYSLOG:
        ret = gossip_disable_syslog();
        break;
    default:
        break;
    }

    gossip_set_debug_mask(0, GOSSIP_NO_DEBUG);
    gossip_internal( "Gossip_disable complete\n");

    return ret;
}

/** Fills in args indicating whether debugging is on or off, and what the 
 *  mask level is.
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_get_debug_mask(int *debug_on, PVFS_debug_mask *mask)
{
    *debug_on = gossip_debug_on;
    *mask = gossip_debug_mask;
    return 0;
}

/** Determines whether debugging messages are turned on or off.  Also
 *  specifies the mask that determines which debugging messages are
 *  printed.
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_set_debug_mask(int debug_on, PVFS_debug_mask debug_mask)
{
    /* Debug setting/clearing debug_mask */
     gossip_internal( "Gossip_set_debug_mask(%d, (%lx , %lx))\n", 
                  debug_on, debug_mask.mask1, debug_mask.mask2);
     gossip_internal( "old gdm = %d, (%lx , %lx)\n", 
                  gossip_debug_on, gossip_debug_mask.mask1, 
                                  gossip_debug_mask.mask2);
     
    /* these old semantics don't make sense to me
     * chaning them to std C and if we have no problems
     * later remove this
     */
#if 0
    if ((debug_on != 0) && (debug_on != 1))
    {
        return -EINVAL;
    }
#endif
    if (debug_on)
    {
        debug_on = 1; /* we will always store a 1 or 0 */
    }

    /* current values are on top of the stack
     * we will push the new ones so we can get back if we want
     * THIS breaks expected push/pop semantics, taking it out.
     */
    /* gossip_push_mask(debug_on, &debug_mask); */

    /* Set the mask and flag to the new value */
    gossip_debug_on = debug_on;
    gossip_debug_mask = debug_mask;

    gossip_internal( "final set gdm = %d, (%lx , %lx)\n\n", 
                    gossip_debug_on, gossip_debug_mask.mask1, 
                                     gossip_debug_mask.mask2);
    return 0;
}

/* gossip_set_logstamp()
 *
 * sets timestamp style for gossip messages
 *
 * returns 0 on success, -errno on failure
 */
int gossip_set_logstamp(enum gossip_logstamp ts)
{
    internal_logstamp = ts;
    return(0);
}

#ifndef __GNUC__
/* __gossip_debug_stub()
 * 
 * stub for gossip_debug that doesn't do anything; used when debugging
 * is "compiled out" on non-gcc builds
 *
 * returns 0
 */
int __gossip_debug_stub(PVFS_debug_mask mask,
                        char prefix,
                        const char *format, ...)
{
    return 0;
}
#endif


/* __gossip_debug()
 * 
 * Logs a standard debugging message.  It will not be printed unless the
 * mask value matches (logical "and" operation) with the mask specified in
 * gossip_set_debug_mask() and debugging is turned on.
 *
 * returns 0 on success, -errno on failure
 */
int __gossip_debug(PVFS_debug_mask mask,
                   char prefix,
                   const char *format,
                   ...)
{
    int ret = -EINVAL;
    va_list ap;

    /* rip out the variable arguments */
    va_start(ap, format);
    ret = __gossip_debug_va(mask, prefix, format, ap);
    va_end(ap);

    return ret;
}

int __gossip_debug_va(PVFS_debug_mask mask,
                      char prefix,
                      const char *format,
                      va_list ap)
{
    int ret = -EINVAL;

    /* NOTE: this check happens in the macro (before making a function call)
     * if we use gcc 
     */
#ifndef __GNUC__
    /* exit quietly if we aren't meant to print */
    if ((!gossip_debug_on) || 
        !gossip_isset(gossip_debug_mask, mask) || 
        (!gossip_facility))
    {
        return 0;
    }
#endif

    if(prefix == '?')
    {
        /* automatic prefix assignment */
        prefix = 'D';
    }

    switch (gossip_facility)
    {
    case GOSSIP_STDERR:
        ret = gossip_debug_fp_va(stderr, prefix, format, ap, internal_logstamp);
        break;
    case GOSSIP_FILE:
        ret = gossip_debug_fp_va(
            internal_log_file, prefix, format, ap, internal_logstamp);
        break;
    case GOSSIP_SYSLOG:
        ret = gossip_debug_syslog(prefix, format, ap);
        break;
    default:
        break;
    }

    return ret;
}

/** Logs a message.  This will print regardless of the
 *  mask value and whether debugging is turned on or off, as long as some
 *  logging facility has been enabled.
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_log(const char *format, ...)
{
    va_list ap;
    int ret = -EINVAL;

    va_start(ap, format);

    ret = gossip_msg('L', format, ap);

    va_end(ap);

    return ret;
}

/** Logs a critical error message.  This will print regardless of the
 *  mask value and whether debugging is turned on or off, as long as some
 *  logging facility has been enabled.
 *
 *  \return 0 on success, -errno on failure.
 */
int gossip_err(const char *format, ...)
{
    va_list ap;
    int ret = -EINVAL;

    va_start(ap, format);

    ret = gossip_msg('E', format, ap);

    va_end(ap);

    return ret;
}

/** Generic message logging.  Pass a prefix to indicate type
 *  \return 0 on success, -errno on failure.
 */
#ifdef WIN32
/** just a stub on Windows
 *  TODO: possibly add errors to Windows Event Log
 */
static int gossip_msg(const char prefix, const char *format, va_list ap)
{
    return 0;
}
#else
static int gossip_msg(const char prefix, const char *format, va_list ap)
{
    int ret = -EINVAL;

    if (!gossip_facility)
    {
        return 0;
    }

    switch (gossip_facility)
    {
    case GOSSIP_STDERR:
        ret = gossip_debug_fp_va(stderr, prefix, format, ap, internal_logstamp);
        break;
    case GOSSIP_FILE:
        ret = gossip_debug_fp_va(internal_log_file, prefix, format, ap, internal_logstamp);
        break;
    case GOSSIP_SYSLOG:
        ret = gossip_msg_syslog(prefix, format, ap);
        break;
    default:
        break;
    }

    return ret;
}
#endif

#ifdef GOSSIP_ENABLE_BACKTRACE
#  ifndef GOSSIP_BACKTRACE_DEPTH
#    define GOSSIP_BACKTRACE_DEPTH 24
#  endif
#  ifndef GOSSIP_MAX_BT
#    define GOSSIP_MAX_BT 8
#  endif

/** Prints out a dump of the current stack (excluding this function)
 *  using gossip_err.
 */
void gossip_backtrace(void)
{
    void *trace[GOSSIP_BACKTRACE_DEPTH];
    char **messages = NULL;
    int i, trace_size;
    static int btcnt = 0;

    trace_size = backtrace(trace, GOSSIP_BACKTRACE_DEPTH);
    messages = backtrace_symbols(trace, trace_size);
    for(i = 1; i < trace_size; i++)
    {
        gossip_err("\t[bt] %s\n", messages[i]);
    }
    /* backtrace_symbols is a libc call that mallocs */
    /* we need to free it with clean_free, not our free */
    clean_free(messages);
    if (++btcnt >= GOSSIP_MAX_BT)
    {
        /* something is really wrong, time to bail out */
        exit(-1);
    }
}
#else
void gossip_backtrace(void)
{
}
#endif /* GOSSIP_ENABLE_BACKTRACE */

/****************************************************************
 * Internal functions
 */

/* gossip_debug_syslog()
 * 
 * This is the standard debugging message function for the syslog logging
 * facility
 *
 * returns 0 on success, -errno on failure
 */
#ifdef WIN32
/** Only a stub on Windows
 *  TODO: possibly add logging to Windows Event Log
 */
static int gossip_debug_syslog(char prefix, const char *format, va_list ap)
{
    return 0;
}
#else
static int gossip_debug_syslog( char prefix, const char *format, va_list ap)
{
    char buffer[GOSSIP_BUF_SIZE];
    char *bptr = buffer;
    int bsize = sizeof(buffer);
    int ret = -EINVAL;

    sprintf(bptr, "[%c] ", prefix);
    bptr += 4;
    bsize -= 4;

    ret = vsnprintf(bptr, bsize, format, ap);
    if (ret < 0)
    {
        return -errno;
    }

    syslog(internal_syslog_priority, "%s", buffer);

    return 0;
}
#endif

int gossip_debug_fp(FILE *fp,
                    char prefix, 
                    enum gossip_logstamp ts,
                    const char *format, ...)
{
    int ret;
    va_list ap;

    /* rip out the variable arguments */
    va_start(ap, format);
    ret = gossip_debug_fp_va(fp, prefix, format, ap, ts);
    va_end(ap);
    return ret;
}

/* gossip_debug_fp_va()
 * 
 * This is the standard debugging message function for the file logging
 * facility or to stderr.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_debug_fp_va(FILE *fp,
                              char prefix,
                              const char *format,
                              va_list ap,
                              enum gossip_logstamp ts)
{
    char buffer[GOSSIP_BUF_SIZE], *bptr = buffer;
    int bsize = sizeof(buffer), temp_size;
    int ret = -EINVAL;
    struct timeval tv;
    time_t tp;

    sprintf(bptr, "[%c ", prefix);
    bptr += 3;
    bsize -= 3;

    switch(ts)
    {
        case GOSSIP_LOGSTAMP_USEC:
            gettimeofday(&tv, 0);
            tp = tv.tv_sec;
            strftime(bptr, 9, "%H:%M:%S", localtime(&tp));
            sprintf(bptr+8, ".%06ld] ", (long)tv.tv_usec);
            bptr += 17;
            bsize -= 17;
            break;
        case GOSSIP_LOGSTAMP_DATETIME:
            gettimeofday(&tv, 0);
            tp = tv.tv_sec;
            strftime(bptr, 22, "%m/%d/%Y %H:%M:%S] ", localtime(&tp));
            bptr += 21;
            bsize -= 21;
            break;
        case GOSSIP_LOGSTAMP_THREAD:
            gettimeofday(&tv, 0);
            tp = tv.tv_sec;
            strftime(bptr, 9, "%H:%M:%S", localtime(&tp));
            bptr += 8;
            bsize -= 8;
#ifdef WIN32
            temp_size = sprintf(bptr, ".%03ld (%4ld)] ", (long)tv.tv_usec / 1000,
                           GetThreadId(GetCurrentThread()));
#else
            temp_size = sprintf(bptr, ".%06ld (%ld)] ", (long)tv.tv_usec, 
                           (long int)gen_thread_self());
#endif
            bptr += temp_size;
            bsize -= temp_size;
            break;

        case GOSSIP_LOGSTAMP_NONE:
            bptr--;
            sprintf(bptr, "] ");
            bptr += 2;
            bsize++;
            break;
        default:
            break;
    }
    
#ifndef WIN32
    ret = vsnprintf(bptr, bsize, format, ap);
    if (ret < 0)
    {
        return -errno;
    }
#else
    ret = vsnprintf_s(bptr, bsize, _TRUNCATE, format, ap);
    if (ret == -1 && errno != 0)
    {
        return -errno;
    }
#endif

    ret = fprintf(fp, "%s", buffer);
    if (ret < 0)
    {
        return -errno;
    }
    fflush(fp);

    return 0;
}

/* gossip_msg_syslog()
 * 
 * error message function for the syslog logging facility
 *
 * returns 0 on success, -errno on failure
 */
#ifdef WIN32
/** just a stub on Windows
 *  TODO: possibly add errors to Windows Event Log
 */
static int gossip_msg_syslog(const char prefix, const char *format, va_list ap)
{
    return 0;
}
#else
static int gossip_msg_syslog(const char prefix, const char *format, va_list ap)
{
    /* for syslog we have the opportunity to change the priority level
     * for errors
     */
    int tmp_priority = internal_syslog_priority;
    internal_syslog_priority = LOG_ERR;

    gossip_debug_syslog(prefix, format, ap);

    internal_syslog_priority = tmp_priority;

    return 0;
}
#endif

/* gossip_disable_stderr()
 * 
 * The shutdown function for the stderr logging facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_disable_stderr(void)
{
    /* this function doesn't need to do anything... */
    return 0;
}

/* gossip_disable_file()
 * 
 * The shutdown function for the file logging facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_disable_file(void)
{
    if (internal_log_file)
    {
        fclose(internal_log_file);
        internal_log_file = NULL;
    }
    return 0;
}

/* gossip_disable_syslog()
 * 
 * The shutdown function for the syslog logging facility.
 *
 * returns 0 on success, -errno on failure
 */
#ifdef WIN32
/** just a stub on Windows
 * TODO: Possibly add logging to Windows Event Log
 */
static int gossip_disable_syslog(void)
{
    return 0;
}
#else
static int gossip_disable_syslog(void)
{
    closelog();
    return 0;
}
#endif

/* push debug mask onto stack */
int gossip_push_mask(int debug_on,
                     PVFS_debug_mask *debug_mask)
{
    gossip_mask_stack *new_mask;
    new_mask = (gossip_mask_stack *)malloc(sizeof(struct gossip_mask_stack_s));

    /* Debug setting/clearing debug_mask */
    gossip_internal( "Gossip_push_mask(%d, (%lx , %lx))\n", 
                  debug_on, debug_mask->mask1, debug_mask->mask2);
    gossip_internal( "old gdm = %d, (%lx , %lx)\n", 
                  gossip_debug_on, gossip_debug_mask.mask1, 
                                   gossip_debug_mask.mask2);

    if (!new_mask)
    {
        return -1;
    }
    if (!debug_mask)
    {
        return -1;
    }

    /* Add entry to the stack */
    new_mask->debug_on = debug_on;
    new_mask->debug_mask = *debug_mask;
    new_mask->debug_facility = gossip_facility;
    qlist_add(&new_mask->debug_stack, &gossip_debug_stack.debug_stack);

    /* Set gossip controlling variables */
    gossip_debug_on = debug_on;
    gossip_debug_mask = *debug_mask;

    gossip_internal( "new gdm = %d, (%lx , %lx)\n", 
                  gossip_debug_on, gossip_debug_mask.mask1, 
                                   gossip_debug_mask.mask2);

    gossip_internal( "Gossip_push_mask complete\n"); 
    return 0;
}

/* pops an entry off of the gossip mask stack and loads it into
 * variables gossip_debug_mask and gossip_debug_on
 * if pointers are provided as arguments, will also output the values.
 */
/* pop debug mask from stack */
int gossip_pop_mask(int *debug_on,
                    PVFS_debug_mask *debug_mask)
{
    gossip_mask_stack *new_stack;
    struct qlist_head *qh;
#if defined(GOSSIP_INTERNAL) && GOSSIP_INTERNAL
    int pflag = -1;
    unsigned long pmask1 = -1, pmask2 = -1;

    /* this is just for debugging gossip */
    if (debug_on != NULL)
    {
        pflag = *debug_on;
    }
    if (debug_mask != NULL)
    {
        pmask1 = debug_mask->mask1;
        pmask2 = debug_mask->mask2;
    }
#endif
        
    /* Debug setting/clearing debug_mask */
    gossip_internal( "Gossip_pop_mask(%d, (%lx , %lx))\n", 
                  pflag, pmask1, pmask2);
    gossip_internal( "old gdm = %d, (%lx , %lx)\n", 
                  gossip_debug_on, gossip_debug_mask.mask1, 
                                   gossip_debug_mask.mask2);

    if (qlist_empty(&gossip_debug_stack.debug_stack))
    {
        gossip_err("gossip_pop_mask has failed rendering gossip off.");
        return -1;
    }

    qh = qlist_pop(&gossip_debug_stack.debug_stack);
    new_stack = qlist_entry(qh, gossip_mask_stack, debug_stack);
    /* set optional arguments as outputs */
    if (debug_on != NULL)
    {
        *debug_on = new_stack->debug_on;
    }
    if (debug_mask != NULL)
    {
        *debug_mask = new_stack->debug_mask;
    }

    /* set current debug mask */
    gossip_debug_on = new_stack->debug_on;
    gossip_debug_mask = new_stack->debug_mask;
    free(new_stack);

    gossip_internal( "new gdm = %d, (%lx , %lx)\n", 
                  gossip_debug_on, gossip_debug_mask.mask1, 
                                   gossip_debug_mask.mask2);

    gossip_internal( "Gossip_pop_mask complete\n"); 

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
