/*
 * (C) 2015 Omnibond Systems L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gossip.h"
#include "quicklist.h"

#include "parallel-bgproc.h"

struct proc {
    struct qlist_head q;
    char *name;
    pid_t pid;
};

static int find_pid(struct qlist_head *, void *);

static QLIST_HEAD(procs);

/*
 * A qlist_compare callback which looks for the pid_t pointed to by ptr.
 */
static int find_pid(struct qlist_head *q, void *ptr)
{
    struct proc *p;
    pid_t pid;
    pid = *((pid_t *)ptr);
    p = qlist_entry(q, struct proc, q);
    return pid == p->pid;
}

/*
 * Start a new process.
 */
int parallel_start(const char *name, pid_t *rpid)
{
    struct proc *p;
    pid_t pid;

    /* p->pid is filled in after the fork. */
    p = malloc(sizeof *p);
    if (!p)
        return -errno;
    p->name = strdup(name);
    if (!p->name)
    {
        free(p);
        return -errno;
    }

    pid = fork();
    if (pid == -1)
    {
        free(p->name);
        free(p);
        return -errno;
    }
    else if (pid == 0)
    {
        /* XXX: Close all fds. */
        execl("/bin/sleep", "sleep", "300", NULL);
        _Exit(1);
    }

    if (rpid)
        *rpid = pid;
    p->pid = pid;
    qlist_add(&p->q, &procs);
    return 0;
}

/*
 * Stop a process.
 * XXX: If wait records process death, this will be recorded then, but it will
 * not show differently from any other process which has been terminated. In
 * that case, should this record that the killing was requested?
 */
int parallel_stop(pid_t pid)
{
    struct qlist_head *q;
    int r;

    /* Look for the process. If we did not start it, do not kill it. However,
     * its entry will be deleted after waiting on it and not here. */
    q = qlist_find(&procs, find_pid, &pid);
    if (!q)
    {
        return -ESRCH;
    }

    r = kill(pid, SIGTERM);
    if (r == -1)
    {
        return -errno;
    }
    return 0;
}

/*
 * Wait on a child after receiving the signal.
 */
void parallel_got_sigchld(void)
{
    struct qlist_head *q;
    int status;
    pid_t pid;
    pid = wait(&status);
    if (pid == -1)
    {
        gossip_lerr("wait fails after SIGCHLD: %s\n", strerror(errno));
        return;
    }
    /* XXX: Maybe this should store the reason for the process death. */
    q = qlist_find(&procs, find_pid, &pid);
    /* There is a possibility that non-parallel background children will die. */
    if (q)
        qlist_del(q);
}
