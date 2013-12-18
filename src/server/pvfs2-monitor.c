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
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>

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
        if (*fds == NULL)
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
    for (; j < *nfds; j++)
        memcpy(*fds+j-1, *fds+j, sizeof(struct pollfd));
    *nfds -= 1;
    *fds = realloc(*fds, sizeof(struct pollfd)*(*nfds));
    if (*fds == NULL)
        return 1; 
    return 0;
}

int startproc(char *arg)
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
    close(pipefds[1]);
    return pipefds[0];
}

int main(void)
{
    struct sigaction act;
    char buf[1024];
    int r, i;
    struct pollfd *fds = NULL;
    int nfds = 0;

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
        if (poll(fds, nfds, -2) == -1) {
            if (errno == EAGAIN)
                continue;
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
            r = startproc(buf);
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
                    printf("read %d bytes.\n", r);
                }
            }
            if (fds[i].revents & POLLHUP) {
                /* Remove from list. */
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
