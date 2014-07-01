/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.

All requests and responses end with a newline. All requests and
responses begin with a single letter identifying the message. There is
a limit on maximum message size.

Note that the handle is not a PID; it is a larger identifier that helps
to ensure there is no race between an S request and a K request.

Request Format

        K<int:handle>   Kills the proces indicated by handle, which is
                        a handle received from the S request.

        R<str:cmdline>  Runs process indicated by cmdline. The daemon
                        forks and runs /bin/sh -c <string>.

        S               Outputs status information.

Response Format

        E<str:message>  Indicates there is an error in the previous
                        request.

        K               Indicates the kill request was successful.

        R<int:handle>   Indicates the run request was successful.

        S<int:handle>,<str:cmdline>
                        This is repeated for each process under the
                        monitor.

        S               Indicates the status request was successful and
                        there is no more data to return. This is sent
                        after all process data is sent in the format
                        above.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Process List
 */

struct proclist
{
    char *cmdline;
    pid_t pid;
    long handle; /* Use long because strtol returns long. */
    int alive;
    char *reason;
    struct proclist *next;
};

static struct proclist *list;
static long prochandle = 0;

/* proclist_add: Add an alive process to the list. */
static int proclist_add(const char *const cmdline, const pid_t pid)
{
    struct proclist *new;
    unsigned long len;
    new = malloc(sizeof(struct proclist));
    if (new == NULL)
        return -1;
    len = strlen(cmdline)+1;
    new->cmdline = malloc(len);
    if (new->cmdline == NULL) {
        (void)free(new);
        return -1;
    }
    strncpy(new->cmdline, cmdline, len);
    new->pid = pid;
    new->handle = prochandle++;
    /* Do not allow prochandle to become negative. */
    if (prochandle < 0)
        prochandle = 0;
    new->alive = 1;
    new->reason = NULL;
    new->next = NULL;

    if (list != NULL)
    {
        struct proclist *p;
        p = list;
        while (p->next != NULL)
            p = p->next;
        p->next = new;
    }
    else
        list = new;
    return new->handle;
}

/* proclist_free: Free memory used by pid. */
static int proclist_free(struct proclist *const p)
{
    (void)free(p->cmdline);
    (void)free(p->reason);
    (void)free(p);
    return 0;
}

/* proclist_remove: Remove process from list. */
static void proclist_remove(const pid_t pid)
{
    struct proclist *p;
    p = list;
    if (p->pid == pid)
    {
        list = p->next;
        free(p);
        return;
    }
    while (p != NULL)
    {
        if (p->next && p->next->pid == pid)
        {
            struct proclist *tofree;
            tofree = p->next;
            p->next = p->next->next;
            proclist_free(tofree);
        }
        p = p->next;
    }
}

/* proclist_findpid: Find a pid for a process. */
static pid_t proclist_findpid(const long handle)
{
    struct proclist *p;
    p = list;
    while (p != NULL)
    {
        if (p->handle == handle)
            return p->pid;
        p = p->next;
    }
    return -1;
}

/*
 * msg: Output command result without buffering and taking care of
 * interrupted output.
 */
static int msg(const char *const msg, const unsigned long len)
{
    int r;
    unsigned long idx = 0;
    while (idx < len)
    {
        r = write(1, msg+idx, len-idx);
        if (r == -1)
        {
            if (errno == EINTR)
                continue;
        }
        else if (r == 0)
            return 1;
        idx += r;
    }
    return 0;
}

/*
 * Read Loop
 * The following functions are dispatched by procline.
 */

static int prockill(const char *const line, const unsigned long len)
{
    long handle;
    pid_t pid;
    (void)len;
    handle = strtol(line, NULL, 10);
    pid = proclist_findpid(handle);
    if (pid == -1)
    {
        (void)msg("Einvalid handle\n", 16);
        return 1;
    }
    if (kill(pid, SIGTERM) != 0)
    {
        char buf[256];
        unsigned long len;
        len = snprintf(buf, 256, "Ekill: %s\n",
            strerror(errno));
        (void)msg(buf, len);
    }
    (void)msg("K\n", 2);
    return 0;
}

static int procrun(const char *const line, const unsigned long len)
{
    int r;
    char buf[256];
    unsigned long buflen;
    (void)len;
    r = fork();
    if (r == -1)
    {
        buflen = snprintf(buf, 256, "Efork: %s\n",
            strerror(errno));
        (void)msg(buf, buflen);
        return 1;
    }
    else if (r == 0)
    {
        char *argv[4];
        argv[0] = "/bin/sh";
        argv[1] = "-c";
        argv[2] = (char *)line;
        argv[3] = NULL;
        execve("/bin/sh", argv, NULL);
        _exit(1);
    }
    if ((r = proclist_add(line, r)) == -1)
    {
        (void)msg("Ecould not add\n", 15);
        return 1;
    }
    buflen = snprintf(buf, 256, "R%d\n", r);
    (void)msg(buf, buflen);
    return 0;
}

static int procstatus(void)
{
    struct proclist *p;
    p = list;
    while (p != NULL)
    {
        if (p->alive)
        {
            char buf[256];
            unsigned long len;
            len = snprintf(buf, 256, "S%lu,%s\n", p->handle,
                p->cmdline);
            (void)msg(buf, len);
        }
        p = p->next;
    }
    (void)msg("S\n", 2);
    return 0;
}

/* procline: Dispatch the appropriate request handler. */
static int procline(const char *const line, unsigned long len)
{
    int r;
    if (len == 0)
    {
        (void)msg("Ezero length line\n", 18);
        return 1;
    }
    switch (*line)
    {
    case 'K':
        r = prockill(line+1, len-1);
        break;
    case 'R':
        r = procrun(line+1, len-1);
        break;
    case 'S':
        r = procstatus();
        break;
    default:
        r = 1;
        (void)msg("Eunknown operator\n", 18);
    }
    return r;
}

/* sigchld: Wait on children and record their status. */
static void sigchld(int signal)
{
    pid_t pid;
    char buf[256];
    int status;
    (void)signal;
    pid = wait(&status);
    if (pid == -1)
        return;
    if (WIFEXITED(status))
    {
        (void)snprintf(buf, 256, "exited with status %d",
            WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        (void)snprintf(buf, 256, "caught signal %d",
            WTERMSIG(status));
    }
    proclist_remove(pid);
}

/*
 * Read commands from input and respond. This is intended to be started
 * by the PVFS2 server to monitor background processes. Documentation on
 * request and response format is in README.
 */
int bgproc_main(void)
{
    const unsigned long buflen = 256;
    char buf[256];
    int r, drop = 0;
    unsigned long idx = 0;
    signal(SIGCHLD, sigchld);
    while (1)
    {
        r = read(0, buf+idx, buflen-idx);
        if (r == -1)
        {
            if (errno == EINTR)
                continue;
        } else if (r == 0)
            break;
        if (buf[idx+r-1] == '\n')
        {
            buf[idx+r-1] = 0;
            if (drop == 0)
                /* The length passed does not include
                 * the trailing null, but it shall be
                 * there. */
                (void)procline(buf, idx+r-1);
            idx = 0;
            drop = 0;
        }
        else
        {
            idx += r;
            if (idx >= buflen)
            {
                if (drop == 0)
                    (void)msg("Edropping line\n",
                        15);
                drop = 1;
                idx = 0;
            }
        }
    }
    return 0;
}
