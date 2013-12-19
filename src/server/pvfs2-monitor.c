/*
 * Copyright 2013 Omnibond Systems LLC.
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>

#include "pvfs2.h"
#include "pvfs2-util.h"

struct process {
    int fd;
    char *cmdline;
    int pid;
    FILE *out;
};

char *mntpt;

void child_status(int signal)
{
    int pid, status;
    pid = waitpid(0, &status, 0);
    if (WIFEXITED(status)) {
        printf("%d exited %d\n", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("%d terminated %d\n", pid, WTERMSIG(status));
    }
}

int add_proc(struct process **procs, int *nprocs, int fd, char *cmdline,
             int pid)
{
    struct process *proc;
    char buf[PVFS_PATH_MAX];
    if (*procs == NULL) {
        *nprocs = 1;
        *procs = malloc(sizeof(struct process));
        if (*procs == NULL)
            return 1;
        proc = *procs;
    } else {
        *nprocs += 1;
        *procs = realloc(*procs, sizeof(struct process)*(*nprocs));
        if (*procs == NULL && errno == ENOMEM)
            return 1;
        proc = *procs+(*nprocs-1);
    }
    proc->fd = fd;
    proc->cmdline = strdup(cmdline);
    proc->pid = pid;
    snprintf(buf, PVFS_PATH_MAX, "%s/log.%d", mntpt, proc->pid);
    proc->out = fopen(buf, "w+");
    if (proc->out == NULL) {
        *nprocs -= 1;
        *procs = realloc(*procs, sizeof(struct process)*(*nprocs));
        return 1;
    }
    return 0;
}

struct process *find_proc(struct process *procs, int nprocs, int fd)
{
    int i;
    for (i = 0; i < nprocs; i++) {
        if (procs[i].fd == fd)
            return procs+i;
    }
    return NULL;
}

int del_proc(struct process **procs, int *nprocs, int fd)
{
    int i, j = -1;
    for (i = 0; i < *nprocs; i++){
        if ((*procs)[i].fd == fd) {
            free((*procs)[i].cmdline);
            fclose((*procs)[i].out);
            j = i+1;
            break;
        }
    }
    if (j == -1)
        return 1;
    for (; j <= *nprocs; j++)
        memcpy(*procs+j-1, *procs+j, sizeof(struct process));
    *procs = realloc(*procs, sizeof(struct process)*(*nprocs-1));
    if (*procs == NULL && errno == ENOMEM)
        return 1; 
    *nprocs -= 1;
    return 0;
}

int add_fd(struct pollfd **fds, int *nfds, int fd)
{
    if (*fds == NULL) {
        *nfds = 1;
        *fds = malloc(sizeof(struct pollfd));
        if (*fds == NULL)
            return 1;
        (*fds)[0].fd = fd;
        (*fds)[0].events = POLLIN;
    } else {
        *nfds += 1;
        *fds = realloc(*fds, sizeof(struct pollfd)*(*nfds));
        if (*fds == NULL && errno == ENOMEM)
            return 1;
        (*fds)[*nfds-1].fd = fd;
        (*fds)[*nfds-1].events = POLLIN;
    }
    return 0;
}

int del_fd(struct pollfd **fds, int *nfds, int fd)
{
    int i, j = -1;
    for (i = 0; i < *nfds; i++){
        if ((*fds)[i].fd == fd) {
            j = i+1;
            break;
        }
    }
    if (j == -1)
        return 1;
    for (; j <= *nfds; j++)
        memcpy(*fds+j-1, *fds+j, sizeof(struct pollfd));
    *fds = realloc(*fds, sizeof(struct pollfd)*(*nfds-1));
    if (*fds == NULL && errno == ENOMEM)
        return 1; 
    *nfds -= 1;
    return 0;
}

int startproc(char *arg, int *pid)
{
    int r, i;
    int pipefds[2];
    /* Start process. */
    r = pipe(pipefds);
    if (r == -1) {
        return 0;
    }
    r = fork();
    if (r == -1) {
        close(pipefds[0]);
        close(pipefds[1]);
        return 0;
    } else if (r == 0) {
        /* Close all fds except the pipe. */
        close(0);
        close(1);
        close(2);
        close(pipefds[0]);
        if (dup2(pipefds[1], 1) || dup2(pipefds[1], 2)) {
            fprintf(stderr, "Could not duplicate fd: %s.\n",
                    strerror(errno));
        }
        for (i = 3; i < sysconf(_SC_OPEN_MAX); i++)
            close(i);
        execlp("sh", "sh", "-c", arg, NULL);
        exit(1);
    }
    *pid = r;
    close(pipefds[1]);
    return pipefds[0];
}

void writetable(struct process *procs, int nprocs)
{
    int i;
    FILE *f;
    char buf[PVFS_PATH_MAX];
    strncpy(buf, mntpt, PVFS_PATH_MAX);
    strncat(buf, "/proctable", PVFS_PATH_MAX);
    f = fopen(buf, "w+");
    for (i = 0; i < nprocs; i++) {
        fprintf(f, "%d %d %s\n", procs[i].fd, procs[i].pid, procs[i].cmdline);
    }
    fclose(f);
}

int main(void)
{
    const PVFS_util_tab *tab;
    int i;
    struct sigaction act;
    char buf[1024];
    int buflen;
    int r;
    struct pollfd *fds = NULL;
    int nfds = 0;
    struct process *procs = NULL;
    int nprocs = 0;
    int pid;
    struct process *proc;

    tab = PVFS_util_parse_pvfstab(NULL);
    if (tab == NULL) {
        fprintf(stderr, "Could not parse pvfstab.\n");
        return 1;
    }

    if (tab->mntent_count == 0) {
        fprintf(stderr, "There are no filesystems in the pvfstab.\n");
        return 1;
    }
    mntpt = tab->mntent_array[0].mnt_dir;

    /* Setup signal handler. */
    act.sa_handler = child_status;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, NULL)) {
        fprintf(stderr, "Could not setup SIGCHLD handler.\n");
        return 1;
    }

    if (add_fd(&fds, &nfds, 0)) {
        fprintf(stderr, "Could not add fd.\n");
        return 1;
    }

    /* Main read loop. */
    while (nfds) {
        writetable(procs, nprocs);
        if (poll(fds, nfds, -2) == -1) {
            if (errno == EAGAIN)
                continue;
            break;
        }
        /* Check for program to start. */
        if (fds[0].revents & POLLIN) {
            /* Read input. */
            if (fgets(buf, 1024, stdin) == NULL) {
                if (ferror(stdin) && errno != EAGAIN) {
                    clearerr(stdin);
                    continue;
                } else {
                    if (ferror(stdin)) {
                        fprintf(stderr, "Could not read: %s.\n",
                                strerror(errno));
                        return 1;
                    } else {
                        break;
                    }
                }
            }
            /* Start process. */
            *(strrchr(buf, '\n')) = 0;
            r = startproc(buf, &pid);
            if (r == 0) {
                fprintf(stderr, "Could not start process: %s.\n",
                        strerror(errno));
                continue;
            }
            /* Add the fd to the table. */
            if (add_fd(&fds, &nfds, r)) {
                fprintf(stderr, "Could not add fd.\n");
                return 1;
            }
            if (add_proc(&procs, &nprocs, r, buf, pid)) {
                del_fd(&fds, &nfds, r);
                fprintf(stderr, "Could not add process.\n");
                return 1;
            }
            continue;
        }
        /* If the input is closed, stay open until all children have died. */
        if (fds[0].revents & POLLHUP) {
            if (del_fd(&fds, &nfds, 0)) {
                fprintf(stderr, "Could not delete fd.\n");
                return 1;
            }
            continue;
        }
        /* Check other fds for input. */
        for (i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                r = read(fds[i].fd, buf, 1024);
                if (r == -1) {
                    if (errno == EAGAIN)
                        continue;
                    fprintf(stderr, "Could not read: %s.\n",
                            strerror(errno));
                    close(fds[i].fd);
                } else {
                    /* Record output. */
                    buflen = r;
                    proc = find_proc(procs, nprocs, fds[i].fd);
                    if (proc != NULL) {
                        while (r != -1 && buflen > 0) {
                            r = fwrite(buf, buflen, 1, proc->out);
                            if (r != -1)
                                buflen -= r;
                        }
                    }
                }
            }
            if (fds[i].revents & POLLHUP) {
                /* Remove from list. */
                if (del_proc(&procs, &nprocs, fds[i].fd)) {
                    fprintf(stderr, "Could not delete process.\n");
                    return 1;
                }
                if (del_fd(&fds, &nfds, fds[i].fd)) {
                    fprintf(stderr, "Could not delete fd.\n");
                    return 1;
                }
                continue;
            }
        }
    }
    return 0;
}
