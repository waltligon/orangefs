/*
 * (C) 2001 Clemson University and The University of Chicago
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <sys/time.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <quicklist.h>

/* for thread ids */
#if WIN32
#include "wincommon.h"
#else
#include "gen-locks.h"
#endif

#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include <syslog.h>

#include "gossip.h"

/*
 * setup
 */

/* xxx: threads */

struct gossip_config
{
    struct gossip_mech mech;
    void *data;
    int nextlevel;
    struct qlist_head list;
};

static int gossip_enabled = 0;
static struct gossip_config *gossip_config;

void gossip_disable(void)
{
    struct gossip_config *p;
    if (!gossip_enabled)
        return;
    gossip_enabled = 0;
    if (!gossip_config)
        return;
    p = qlist_entry(&gossip_config->list, struct gossip_config, list);
    do {
        p->mech.shutdown(p->data);
        free(p->data);
        p = qlist_entry(&p->list.next, struct gossip_config, list);
    } while (&p->list != &gossip_config->list);
}

int gossip_enable(struct gossip_mech *mech, ...)
{
    int r = 0;
    va_list ap;
    va_start(ap, mech);
    if (gossip_enabled)
        gossip_disable();

    gossip_config = malloc(sizeof *gossip_config);
    INIT_QLIST_HEAD(&gossip_config->list);

    memcpy(&gossip_config->mech, mech, sizeof gossip_config->mech);
    gossip_config->nextlevel = 0;
    gossip_config->data = malloc(gossip_config->mech.datasz);
    if (gossip_config->data == NULL) {
        free(gossip_config);
        gossip_config = NULL;
        return -1;
    }
    if (gossip_config->mech.startup)
    {
        r = gossip_config->mech.startup(gossip_config->data, ap);
        if (r < 0) {
            free(gossip_config->data);
            free(gossip_config);
            gossip_config = NULL;
            return r;
        }
    }
    gossip_enabled = 1;
    va_end(ap);
    return 0;
}

int gossip_reset(void)
{
    struct gossip_config *p;
    int r = 0;
    if (!gossip_enabled)
        return -1;
    p = qlist_entry(&gossip_config->list, struct gossip_config, list);
    do {
        if (r >= 0)
            r = p->mech.reset(p->data);
        p = qlist_entry(&p->list.next, struct gossip_config, list);
    } while (&p->list != &gossip_config->list);
    return 0;
}

/*
 * parameters
 */

static enum gossip_logstamp gossip_ts = GOSSIP_LOGSTAMP_DEFAULT;
uint64_t gossip_debug_mask;
int gossip_debug_on;

void gossip_get_debug_mask(int *debug_on, uint64_t *mask)
{
    *debug_on = gossip_debug_on;
    *mask = gossip_debug_mask;
}

void gossip_set_debug_mask(int debug_on, uint64_t mask)
{
    gossip_debug_on = debug_on;
    gossip_debug_mask = mask;
}

int gossip_debug_enabled(uint64_t mask)
{
   return gossip_debug_on && mask & gossip_debug_mask;
}

void gossip_get_logstamp(enum gossip_logstamp *ts)
{
    *ts = gossip_ts;
}

void gossip_set_logstamp(enum gossip_logstamp ts)
{
    gossip_ts = ts;
}

/*
 * log functions
 */

#if HAVE_EXECINFO_H && defined(GOSSIP_ENABLE_BACKTRACE)
#ifndef GOSSIP_BACKTRACE_DEPTH
#define GOSSIP_BACKTRACE_DEPTH 24
#endif
#ifndef GOSSIP_MAX_BT
#define GOSSIP_MAX_BT 8
#endif
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
        gossip_print('E', "\t[bt] %s\n", messages[i]);
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
#endif

int gossip_vprint(char prefix, const char *fmt, va_list ap)
{
    /* optimize for smaller error messages; also reduce the chance of not
     * being able to print out of memory due to heap exhaustion */
    struct timeval tv;
    va_list aq;
    char stackbuf[512], prefixbuf[32];
    char *buf = NULL;
    struct gossip_config *p;
    size_t len, prefixlen = 0;
    int r;
    if (!gossip_enabled)
        return 0;
    /* generate prefix */
    prefixlen = snprintf(prefixbuf+prefixlen, sizeof prefixbuf-prefixlen,
            "[%c", prefix);
    switch (gossip_ts)
    {
        case GOSSIP_LOGSTAMP_NONE:
            prefixlen += snprintf(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen, "] ");
            break;
        case GOSSIP_LOGSTAMP_USEC:
            gettimeofday(&tv, 0);
            prefixlen += strftime(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen, " %H:%M:%S",
                    localtime(&tv.tv_sec));
            prefixlen += snprintf(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen, ".%06ld] ", tv.tv_usec);
            break;
        case GOSSIP_LOGSTAMP_DATETIME:
            gettimeofday(&tv, 0);
            prefixlen += strftime(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen, " %m/%d/%Y %H:%M:%S] ",
                    localtime(&tv.tv_sec));
            break;
        case GOSSIP_LOGSTAMP_THREAD:
            gettimeofday(&tv, 0);
            prefixlen += strftime(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen, " %H:%M:%S",
                    localtime(&tv.tv_sec));
            prefixlen += snprintf(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen, ".%06ld] ", tv.tv_usec);
#ifdef WIN32
            prefixlen += snprintf(prefixbuf+prefixlen,
                sizeof prefixbuf-prefixlen,
                "(%4ld) ", GetThreadId(GetCurrentThread())));
#else
            prefixlen += snprintf(prefixbuf+prefixlen,
                    sizeof prefixbuf-prefixlen,
                    "(%ld) ", (long int)gen_thread_self());
#endif
            break;
    }
    /* generate log message */
    va_copy(aq, ap);
    len = vsnprintf(stackbuf, sizeof stackbuf, fmt, ap)+prefixlen+1;
    if (len > sizeof stackbuf)
    {
        buf = malloc(len);
        if (buf == NULL)
            return -1;
    }
    else
    {
        buf = stackbuf;
    }
    strncpy(buf, prefixbuf, prefixlen+1);
    vsnprintf(buf+prefixlen, len-prefixlen, fmt, aq);
    /* write out prefix and message */
    /* r will be negative if any log fails. */
    r = 0;
    p = qlist_entry(&gossip_config->list, struct gossip_config, list);
    do {
        if (r >= 0)
            r = p->mech.log(buf, len, p->data);
        p = qlist_entry(&p->list.next, struct gossip_config, list);
    } while (&p->list != &gossip_config->list);
    if (buf != stackbuf)
        free(buf);
    return r;
}

int gossip_err(const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = gossip_vprint('E', fmt, ap);
    va_end(ap);
    gossip_backtrace();
    return r;
}

int gossip_perf_log(const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = gossip_vprint('P', fmt, ap);
    va_end(ap);
    return r;
}

int gossip_debug(uint64_t mask, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (gossip_debug_on && mask & gossip_debug_mask)
    {
        va_start(ap, fmt);
        r = gossip_vprint('D', fmt, ap);
        va_end(ap);
        return r;
    }
    return 0;
}

int gossip_print(char prefix, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = gossip_vprint(prefix, fmt, ap);
    va_end(ap);
    return r;
}

/*
 * log mechanisms
 */

/* stderr */

static int gossip_mech_stderr_log(char *str, size_t len, void *data)
{
    (void)len;
    (void)data;
    if (fputs(str, stderr) == EOF)
    {
        return -1;
    }
    return 0;
}

struct gossip_mech gossip_mech_stderr = {NULL, gossip_mech_stderr_log};

/* syslog */

struct gossip_mech_syslog_data {
    char *ident;
    int option;
    int facility;
};

static int gossip_mech_syslog_startup(void *data, va_list ap)
{
    struct gossip_mech_syslog_data *mydata = data;
    mydata->ident = strdup(va_arg(ap, char *));
    mydata->option = va_arg(ap, int);
    mydata->facility = va_arg(ap, int);
    openlog(mydata->ident, mydata->option, mydata->facility);
    return 0;
}

static int gossip_mech_syslog_log(char *str, size_t len, void *data)
{
    (void)len;
    syslog(LOG_INFO, str);
    return 0;
}

static void gossip_mech_syslog_shutdown(void *data)
{
    struct gossip_mech_syslog_data *mydata = data;
    free(mydata->ident);
    closelog();
}

struct gossip_mech gossip_mech_syslog =
        {gossip_mech_syslog_startup, gossip_mech_syslog_log,
        gossip_mech_syslog_shutdown, NULL,
        sizeof(struct gossip_mech_syslog_data)};

/* file */

struct gossip_mech_file_data {
    char *path;
    FILE *f;
};

static int gossip_mech_file_startup(void *data, va_list ap)
{
    struct gossip_mech_file_data *mydata = data;
    char *path;
    path = va_arg(ap, char *);
    mydata->path = strdup(path);
    mydata->f = fopen(path, "a");
    if (mydata->f == NULL)
        return -1;
    return 0;
}

static int gossip_mech_file_log(char *str, size_t len, void *data)
{
    struct gossip_mech_file_data *mydata = data;
    (void)len;
    if (fputs(str, mydata->f) == EOF)
    {
        return -1;
    }
    return 0;
}

static void gossip_mech_file_shutdown(void *data)
{
    struct gossip_mech_file_data *mydata = data;
    free(mydata->path);
    if (fclose(mydata->f) == EOF) {
        perror("could not close log");
    }
}

static int gossip_mech_file_reset(void *data)
{
    struct gossip_mech_file_data *mydata = data;
    if (fclose(mydata->f) == EOF) {
        perror("could not close log");
    }
    mydata->f = fopen(mydata->path, "a");
    if (mydata->f == NULL)
        return -1;
    return 0;
}

struct gossip_mech gossip_mech_file =
        {gossip_mech_file_startup, gossip_mech_file_log,
        gossip_mech_file_shutdown, gossip_mech_file_reset,
        sizeof(struct gossip_mech_file_data)};
