#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "pvfs2-types.h"

struct proc {
    int fd;
    pid_t pid;
    char *cmdline;
    FILE *log;
    struct proc *next;
};

struct proclist {
    int nprocs;
    struct proc *procs;
};

void proclist_init(struct proclist *pl);
void proclist_add(struct proclist *pl, struct proc *newproc);
struct proc *proclist_findfd(struct proclist *pl, int fd);
struct proc *proclist_findpid(struct proclist *pl, int fd);
void proclist_remove(struct proclist *pl, struct proc *oldproc);
void proclist_fillpollfds(struct proclist *pl, struct pollfd *pfds, int events);

void child_status(int signal);
int setup_child_status(void);
int startproc(void);
void endproc(int fd);
void copytolog(struct proc *proc, char *buf, int buflen);

void proclist_init(struct proclist *pl)
{
    pl->nprocs = 0;
    pl->procs = NULL;
}

void proclist_add(struct proclist *pl, struct proc *newproc)
{
    struct proc *proc;
    /* Make sure the linked list is not contaminated. */
    newproc->next = NULL;
    pl->nprocs++;
    if (pl->procs) {
        proc = pl->procs;
        while (proc->next)
            proc = proc->next;
        proc->next = newproc;
    } else {
        pl->procs = newproc;
    }
}

struct proc *proclist_findfd(struct proclist *pl, int fd)
{
    struct proc *proc = pl->procs;
    while (proc) {
        if (proc->fd == fd)
            return proc;
        proc = proc->next;
    }
    return NULL;
}

struct proc *proclist_findpid(struct proclist *pl, pid_t pid)
{
    struct proc *proc = pl->procs;
    while (proc) {
        if (proc->pid == pid)
            return proc;
        proc = proc->next;
    }
    return NULL;
}

void proclist_remove(struct proclist *pl, struct proc *oldproc)
{
    struct proc *prevproc, *proc;
    /* Should the first item be removed. */
    if (pl->procs == oldproc) {
        pl->procs = oldproc->next;
        pl->nprocs--;
        return;
    }
    /* Delete the appropriate item. */
    prevproc = pl->procs;
    proc = pl->procs->next;
    while (proc) {
        if (proc == oldproc) {
            prevproc->next = oldproc->next;
            pl->nprocs--;
            return;
        }
        prevproc = proc;
        proc = proc->next;
    }
}

void proclist_fillpollfds(struct proclist *pl, struct pollfd *pfds, int events)
{
    int i = 0;
    struct proc *proc = pl->procs;
    while (i < pl->nprocs) {
        pfds[i].fd = proc->fd;
        pfds[i].events = events;
        i++;
        proc = proc->next;
    }
}

struct proclist pl;

void child_status(int signal)
{
    pid_t pid;
    int status;
    struct proc *proc;
    pid = waitpid(0, &status, 0);
    if (pid == -1) {
        perror("Could not waitpid");
        return;
    }
    proc = proclist_findpid(&pl, pid);
    if (proc == NULL)
        return;
    printf("status change on %d %s\n", proc->pid, proc->cmdline);
}

int setup_child_status(void)
{
    struct sigaction act;
    act.sa_handler = child_status;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    return sigaction(SIGCHLD, &act, NULL);
}

int startproc(void)
{
    char buf[1024];
    int r;
    int pipefds[2];
    struct proc *proc;
    /* Read program command line. */
    if (fgets(buf, 1024, stdin) == NULL) {
        if (feof(stdin))
            return 1;
        return 2;
    }
    *(strrchr(buf, '\n')) = 0;
    if (pipe(pipefds)) {
        return 2;
    }
    r = fork();
    if (r == -1) {
        return 2;
    } else if (r == 0) {
        /* Setup standard I/O and exec. */
        close(pipefds[0]);
        close(0);
        close(1);
        close(2);
        dup2(pipefds[1], 1);
        dup2(pipefds[1], 2);
        close(pipefds[1]);
        execlp("sh", "sh", "-c", buf, NULL);
    } else {
        char logname[PATH_MAX];
        /* Success. */
        close(pipefds[1]);
        proc = malloc(sizeof(struct proc));
        /* Or not. This could leak memory, but there are probably other
         * problems if malloc is returning NULL. */
        if (proc == NULL)
            return 2;
        proc->fd = pipefds[0];
        proc->pid = r;
        proc->cmdline = strdup(buf);
        /* XXX: Get the correct path... */
        snprintf(logname, PVFS_PATH_MAX, "/pvfsmnt/proc.%d", proc->pid);
        proc->log = fopen(logname, "w");
        if (proc->log == NULL)
            return 2;
        proclist_add(&pl, proc);
    }
    return 0;
}

void endproc(int fd)
{
    struct proc *proc;
    proc = proclist_findfd(&pl, fd);
    free(proc->cmdline);
    fflush(proc->log);
    fclose(proc->log);
    proclist_remove(&pl, proc);;
}

void copytolog(struct proc *proc, char *buf, int buflen)
{
    printf("log one '%c'\n", *buf);
    fwrite(buf, buflen, 1, proc->log);
    fflush(proc->log);
}

int main(void)
{
    struct pollfd *pfds;

    if (setup_child_status()) {
        perror("Could not setup signal handler");
        return 1;
    }

    proclist_init(&pl);

    while (1) {
        int r, i;
        /* Generate list of fds to poll. */
        pfds = malloc(sizeof(struct pollfd)*(pl.nprocs+1));
        proclist_fillpollfds(&pl, pfds+1, POLLIN);
        pfds[0].fd = 0;
        pfds[0].events = POLLIN;

        /* Poll. Be careful of delivered signals. */
        r = poll(pfds, pl.nprocs+1, -1);
        if (r == -1) {
            if (errno == EINTR) {
                free(pfds);
                continue;
            }
            perror("poll: ");
            return 1;
        }

        /* Handle conditions on the first fd (standard input) specially.
         * Stop if it should close. */
        if (pfds[0].revents & POLLNVAL) {
            return 0;
        } else if (pfds[0].revents & POLLIN) {
            r = startproc();
            if (r == 1)
                return 0;
            else if (r)
                perror("Could not start process");
        }

        /* Handle conditions on the other fds, which are connected to
         * processes we are monitoring. */
        for (i = 1; i < pl.nprocs+1; i++) {
            char buf[1024];
            struct proc *proc;
            /* Remove the process if there are errors. Copy the data to a log
             * file otherwise. */
            if (pfds[i].revents & POLLNVAL) {
                endproc(pfds[i].fd);
            } else if (pfds[i].revents & POLLIN) {
                r = read(pfds[i].fd, buf, 1024);
                if (r == -1) {
                    if (errno != EINTR)
                        perror("Cannot read from pipe");
                } else if (r == 0) {
                    close(pfds[i].fd);
                    endproc(pfds[i].fd);
                } else {
                    proc = proclist_findfd(&pl, pfds[i].fd);
                    copytolog(proc, buf, r);
                }
            }
        }

        /* A new pollfd table will be allocated each time. This is inefficient,
         * but the intent is that this is not used for high-speed operations. */
        free(pfds);
    }
    return 0;
}
