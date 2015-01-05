/*
 * Copyright 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gossip.h"
#include "pvfs2-debug.h"

#include "bgmon.h"

struct process
{
    unsigned short num;
    pid_t pid;
    char *name;
    LIST_ENTRY(process) entries;
};

LIST_HEAD(, process) processes = LIST_HEAD_INITIALIZER(processes);

static int proc_num(unsigned short *);
static int respond_err(int error);
static int respond_list(void);
static int respond_start(const char *);
static int respond_kill(unsigned short);
static int process(const unsigned char *, size_t);
static void child(int);
static int reap(void);

static sigset_t chldset;
static int got_sigchld;

/*
 * Generate a handle for a new process. Warning: the handles are capped at 2^16,
 * at which point no more will be generated.
 */
static int proc_num(unsigned short *r)
{
    static unsigned short num;
    if (((num+1) & 0xffff) == 0)
    {
        return -1;
    }
    else
    {
        *r = num;
        num = (num+1) & 0xffff;
        return 0;
    }
}

/*
 * Send response buffer.
 */
static int respond(unsigned char *buf, size_t sz)
{
    ssize_t i = 0, r;
    while ((r = write(STDOUT_FILENO, buf+i, sz-i)) != -1)
    {
        r += i;
        if (r == sz)
        {
            break;
        }
    }
    if (r == -1)
    {
        return -1;
    }
    return 0;
}

/*
 * Send error response.
 */
static int respond_err(int error)
{
    unsigned char buf[6];
    buf[0] = 0;
    buf[1] = sizeof buf;
    buf[2] = 0;
    buf[3] = TYPE_ERR;
    buf[4] = (error >> 8) & 0xff;
    buf[5] = error & 0xff;
    return respond(buf, sizeof buf);
}

/*
 * Send list response.
 */
static int respond_list(void)
{
    size_t nump = 0, sz = 6, i = 6;
    unsigned char *buf;
    struct process *p;
    int r;

    /* Calculate necessary buffer size. */
    LIST_FOREACH(p, &processes, entries)
    {
        nump++;
        sz += 4;
        sz += strlen(p->name);
    }

    /* Allocate and fill the buffer. */
    buf = malloc(sz);
    if (buf == NULL)
    {
        return -1;
    }
    buf[0] = (sz >> 8) & 0xff;
    buf[1] = sz & 0xff;
    buf[2] = 0;
    buf[3] = TYPE_LIST;
    buf[4] = (nump >> 8) & 0xff;
    buf[5] = nump & 0xff;

    LIST_FOREACH(p, &processes, entries)
    {
        size_t namelen;
        buf[i++] = (p->num >> 8) & 0xff;
        buf[i++] = p->num & 0xff;
        namelen = strlen(p->name);
        buf[i++] = (namelen >> 8) & 0xff;
        buf[i++] = namelen & 0xff;
        memcpy(buf+i, p->name, namelen);
        i += namelen;
    }

    r = respond(buf, sz);
    free(buf);
    return r;
}

/*
 * Send start response.
 */
static int respond_start(const char *str)
{
    /* Static buffer. buf[4] and buf[5] will contain the process number. */
    unsigned char buf[6] = {0, 6, 0, TYPE_START};
    struct process *p;
    pid_t pid;

    /* Allocate and fill a struct process. pid is not yet filled. */
    p = malloc(sizeof *p);
    if (p == NULL)
    {
        return respond_err(ERR_NOMEM);
    }
    if (proc_num(&p->num) == -1)
    {
        free(p);
        return respond_err(ERR_NOMEM);
    }
    p->name = strdup(str);
    if (p->name == NULL)
    {
        free(p);
        return respond_err(ERR_NOMEM);
    }

    pid = fork();
    if (pid == -1)
    {
        free(p->name);
        free(p);
        return respond_err(ERR_INVAL);
    }
    else if (pid == 0)
    {
        /* Become a parallel background process. */
        char path[FILENAME_MAX];
        char *args[2];
        int r;
        r = snprintf(path, sizeof path, LIBEXECDIR"/%s", str);
        if (r < 0 || r >= sizeof path)
        {
            gossip_lerr("executable path too long\n");
            exit(EXIT_FAILURE);
        }
        args[0] = (char *)str;
        args[1] = NULL;
        execv(path, args);
        gossip_lerr("cannot exec: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {
        /* Finish up and respond. */
        p->pid = pid;
        LIST_INSERT_HEAD(&processes, p, entries);
        buf[4] = (p->num >> 8) & 0xff;
        buf[5] = p->num & 0xff;
        return respond(buf, sizeof buf);
    }
}

/*
 * Send a kill response. The process is removed from the list by reap after
 * receipt of a SIGCHLD signal.
 */
static int respond_kill(unsigned short num)
{
    unsigned char buf[4] = {0, 4, 0, TYPE_KILL};
    struct process *p;
    LIST_FOREACH(p, &processes, entries)
    {
        if (p->num == num)
        {
            if (kill(p->pid, SIGTERM) == -1)
            {
                return respond_err(ERR_KILL);
            }
            break;
        }
    }
    return respond(buf, sizeof buf);
}

/*
 * Dispatch an incoming request.
 */
static int process(const unsigned char *buf, size_t sz)
{
    int type;

    if (sz >= 4)
    {
        type = (buf[2] << 8) + buf[3];
    }
    else
    {
        return respond_err(ERR_INVAL);
    }

    switch (type)
    {
    case TYPE_LIST:
        return respond_list();
    case TYPE_START:
    {
        size_t slen;
        char *s;
        if (sz >= 6)
        {
            slen = (buf[4] << 8) + buf[5];
            s = malloc(slen+1);
            if (s == NULL)
            {
                goto error;
            }
            memcpy(s, buf+6, slen);
            s[slen] = 0;
        }
        return respond_start(s);
    }
    case TYPE_KILL:
    {
        int num;
        if (sz >= 6)
        {
            num = (buf[4] << 8) + buf[5];
        }
        return respond_kill(num);
    }
    default:
        return respond_err(ERR_INVAL);
    }

error:
    return respond_err(ERR_NOMEM);
}

/*
 * Set a flag indicating that there are processes to reap.
 */
static void child(int signal)
{
    got_sigchld = 1;
    if (sigprocmask(SIG_BLOCK, &chldset, NULL) == -1)
    {
        gossip_lerr("could not set signal mask: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/*
 * Reap and remove from the list any processes which have died.
 */
static int reap(void)
{
    struct process *p;
    int status;
    pid_t pid;
    while (1)
    {
        pid = waitpid(WAIT_ANY, &status, WNOHANG);
        if (pid == -1)
        {
            break;
        }
        LIST_FOREACH(p, &processes, entries)
        {
            if (p->pid == pid)
            {
                LIST_REMOVE(p, entries);
                free(p->name);
                free(p);
                break;
            }
        }
    }
    if (errno == ECHILD)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int main(void)
{
    size_t bufsz = 0, req = 0;
    unsigned char buf[1024];
    struct sigaction sa;
    ssize_t r;

    gossip_debug(GOSSIP_SERVER_DEBUG,
            "Parallel background process monitor starting.\n");

    /* Generate a signal set, so the signal catcher can use it without handling
     * a possible error. */ 
    if (sigemptyset(&chldset) == -1 || sigaddset(&chldset, SIGCHLD) == -1)
    {
        gossip_lerr("could not setup signal set: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Setup the SIGCHLD handler. */
    sa.sa_handler = child;
    if (sigemptyset(&sa.sa_mask) == -1)
    {
        gossip_lerr("could not setup signal set: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    sa.sa_flags = 0;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        gossip_lerr("could not setup signal handler: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Read incoming requests. The idea is to keep a pointer (req, reqsz) into
     * the buffer (buf, bufsz) and dispatch the request once the entire request
     * is read. Special handling is needed for requests which are too large for
     * the buffer (which should not be sent to the program). */
top:
    while (1)
    {
        unsigned long reqsz;
        r = read(STDIN_FILENO, buf+bufsz, sizeof buf-bufsz);
        if ((r == -1 && errno != EINTR) || r == 0)
        {
            break;
        }
        if (r == -1 && errno == EINTR)
        {
            r = 0;
        }
        bufsz += r;
        while (req < bufsz)
        {
            reqsz = (buf[req+0] << 8) + buf[req+1];
            if (reqsz > sizeof buf)
            {
                gossip_lerr("request too large\n");
                return EXIT_FAILURE;
            }
            else
            {
                if (req+reqsz > sizeof buf)
                {
                    memmove(buf, buf+req, bufsz-req);
                    bufsz = bufsz-req;
                    req = 0;
                    goto top;
                }
                else
                {
                    if (req+reqsz > bufsz)
                    {
                        goto top;
                    }
                    else
                    {
                        /* Dispatch a request. Reap if necessary and make sure
                         * SIGCHLD is blocked while processing the request. */
                        if (got_sigchld)
                        {
                            reap();
                            got_sigchld = 0;
                        }
                        else
                        {
                            if (sigprocmask(SIG_BLOCK, &chldset, NULL) == -1)
                            {
                                gossip_lerr("could not set signal mask: %s\n",
                                        strerror(errno));
                                exit(EXIT_FAILURE);
                            }
                        }
                        process(buf+req, reqsz);
                        if (sigprocmask(SIG_UNBLOCK, &chldset, NULL) == -1)
                        {
                            gossip_lerr("could not set signal mask: %s\n",
                                    strerror(errno));
                            exit(EXIT_FAILURE);
                        }
                        req += reqsz;
                    }
                }
            }
        }
    }
    if (r == -1)
    {
        gossip_lerr("could not read: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
