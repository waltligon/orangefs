#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/queue.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <pvfs2.h>

struct proc {
	int pid; /* PID. */
	int outfd; /* Output stream. */
	char *cmdline; /* Invocation command line. */
	FILE *f; /* Stream to write log to. */
	SLIST_ENTRY(proc) procs;
};
SLIST_HEAD(prochead, proc);

struct proc *proc_alloc(int pid, int outfd, char *cmdline);
void proc_free(struct proc *p);
struct proc *prochead_find(struct prochead *prochead, int fd);
struct proc *prochead_findpid(struct prochead *prochead, int pid);

struct pollfd *makepollfds(struct prochead *prochead, int *nfds);
int dopollfds(struct prochead *prochead, char *procdir,
              struct pollfd *fds, int nfds);
int doinput(int fd, struct prochead *prochead, char *procdir);
int startprocess(char *s, struct prochead *prochead, char *procdir);
int dooutput(int fd, struct prochead *prochead);

int logstatus(struct prochead *prochead, char *procdir);

void childstatus(int signal);

struct proc *proc_alloc(int pid, int outfd, char *cmdline)
{
	struct proc *p;
	p = malloc(sizeof(struct proc));
	p->pid = pid;
	p->outfd = outfd;
	p->cmdline = strdup(cmdline);
	return p;
}

void proc_free(struct proc *p)
{
	free(p->cmdline);
	free(p);
}

struct proc *prochead_find(struct prochead *prochead, int fd)
{
	struct proc *p;
	SLIST_FOREACH(p, prochead, procs) {
		if (p->outfd == fd)
			return p;
	}
	return NULL;
}

struct proc *prochead_findpid(struct prochead *prochead, int pid)
{
	struct proc *p;
	SLIST_FOREACH(p, prochead, procs) {
		if (p->pid == pid)
			return p;
	}
	return NULL;
}

/*
 * Create pollfd structures from the process list.
 */
struct pollfd *makepollfds(struct prochead *prochead, int *nfds)
{
	int size = 0, i = 0;
	struct proc *p;
	struct pollfd *fds;

	/* Count the processes. */
	size = 0;
	SLIST_FOREACH(p, prochead, procs)
		size++;

	/* Allocate memory for each process plus the input stream. */
	fds = malloc(sizeof(struct pollfd)*(size+1));
	if (fds == NULL)
		return NULL;
	*nfds = size+1;

	/* Setup poll to wait for output from any running process. */
	SLIST_FOREACH(p, prochead, procs) {
		fds[i].fd = p->outfd;
		fds[i].events = POLLIN;
		i++;
	}

	/* Setup poll to wait for a request from the input stream. */
	fds[i].fd = 0;
	fds[i].events = POLLIN;

	return fds;
}

/*
 * Process each revents entry in the pollfd structures.
 */
int dopollfds(struct prochead *prochead, char *procdir,
              struct pollfd *fds, int nfds)
{
	int i;
	struct proc *p;
	for (i = 0; i < nfds; i++) {
		if (fds[i].revents & POLLIN) {
			/* This is the input stream. */
			if (fds[i].fd == 0)
				doinput(fds[i].fd, prochead, procdir);
			else
			/* This is program output. */
				dooutput(fds[i].fd, prochead);
		}
		if (fds[i].revents & POLLHUP || fds[i].revents & POLLERR
		    || fds[i].revents & POLLNVAL) {
			if (fds[i].fd == 0) {
				exit(0);
			} else {
				p = prochead_find(prochead, fds[i].fd);
				if (p == NULL) {
					close(fds[i].fd);
				} else {
					close(p->outfd);
					fclose(p->f);
					proc_free(p);
					SLIST_REMOVE(prochead, p, proc,
					             procs);
				}
			}
		}
	}
	return 0;
}

/*
 * Process input stream. This will start a process with the parameters
 * read.
 */
int doinput(int fd, struct prochead *prochead, char *procdir)
{
	static char buf[1024];
	static int offset;
	int size;
	char *string, *end = NULL, *oldend;

	/* Read from input stream. */
	size = read(fd, buf+offset, 1024-offset);
	if (size == -1) {
		if (errno == EINTR)
			return 0;
		else
			return 1;
	}
	size += offset;

	/* Split by the newlines. */
	string = buf;
	while (1) {
		oldend = end;
		end = memchr(string, '\n', size);
		if (end == NULL)
			break;
		*end = 0;
		startprocess(string, prochead, procdir);
		if (end + 1 - string <= size) {
			size -= end + 1 - string;
			string = end + 1;
		}
	}
	/* Put unterminated data at the beginning of the buffer and
	 * setup the offset if applicable. */
	if (size) {
		/* If oldend is not set, then the unterminated data is
		 * at the beginning of the buffer. */
		if (oldend != NULL)
			memmove(buf, oldend + 1, size);
		if (size < 1024) {
			offset = size;
		} else {
			fprintf(stderr, "discarding large input "
			        "data\n");
		}
	} else {
		offset = 0;
	}
	
	return 0;
}

/*
 * Start a process with a pipe for the output stream and add it to the
 * prochead list.
 */
int startprocess(char *s, struct prochead *prochead, char *procdir)
{
	int r;
	int fds[2];
	struct proc *p;
	char path[_POSIX_PATH_MAX];

	r = pipe(fds);
	if (r == -1)
		return 1;

	/* Startup a new process. */
	r = fork();
	if (r == -1) {
		close(fds[0]);
		close(fds[1]);
		return 1;
        } else if (r == 0) {
		close(fds[0]);
		close(0);
		close(1);
		close(2);
		openat(0, "/dev/null", O_WRONLY);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		execle("/bin/sh", "sh", "-c", s, NULL, NULL);
		/* Prevent atexit routines from running twice. There is
		 * no way to distinguish exec errors from program
		 * errors. */
		_exit(1);
	}
	close(fds[1]);

	/* Add to prochead list. */
	p = proc_alloc(r, fds[0], s);
	/* Leave the process running and file descriptors active if
	 * proc_alloc failed (i.e. there is no more system memory). */
	if (p == NULL)
		return 1;
	snprintf(path, _POSIX_PATH_MAX, "%s/log.%d", procdir, p->pid);
	p->f = fopen(path, "w");
	SLIST_INSERT_HEAD(prochead, p, procs);
	
	return 0;
}

/*
 * Process output stream.
 */
int dooutput(int fd, struct prochead *prochead)
{
	struct proc *p;
	int size;
	char buf[1024];

	p = prochead_find(prochead, fd);
	if (p == NULL)
		return 1;

	size = read(fd, buf, 1024);

	if (size == -1) {
		return 1;
	} else if (size == 0) {
		close(p->outfd);
		fclose(p->f);
		proc_free(p);
		SLIST_REMOVE(prochead, p, proc, procs);
	} else {
		/* XXX */
		if (p->f)
			fwrite(buf, size, 1, p->f);
	}

	return 0;
}

int logstatus(struct prochead *prochead, char *procdir)
{
	char path[_POSIX_PATH_MAX];
	FILE *f;
	struct proc *p;

	snprintf(path, _POSIX_PATH_MAX, "%s/table", procdir);

	f = fopen(path, "w");
	if (f == NULL)
		return 1;

	SLIST_FOREACH(p, prochead, procs) {
		fprintf(f, "%d %s\n", p->pid, p->cmdline);
	}

	fclose(f);

	return 0;
}

/* This is usually hidden by a local variable above. */
struct prochead prochead;
char procdir[_POSIX_PATH_MAX];

/*
 * Signal that a child has stopped running. Wait on it and report
 * its status back into the process table.
 */
void childstatus(int signal)
{
	int pid, status;
	struct proc *p;
	pid = waitpid(0, &status, 0);
	if (pid == -1)
		return;
	p = prochead_findpid(&prochead, pid);
	if (p == NULL)
		return;
	/* XXX */
/*	printf("okay? %d %d\n", pid, status);*/
}

int main(void)
{
	const PVFS_util_tab *tab;
	struct sigaction act;
	struct pollfd *fds;
	int nfds;
	int r;

	/* Start PVFS. */
	r = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
	if (r < 0) {
		PVFS_perror("PVFS_sys_initialize", r);
		return 1;
	}

	tab = PVFS_util_parse_pvfstab(NULL);
	if (tab == NULL) {
		fprintf(stderr, "Failed to parse pvfstab.\n");
		return 1;
	}

	if (tab->mntent_count < 1) {
		fprintf(stderr, "pvfstab does not contain any filesystems.\n");
		return 1;
	}

	snprintf(procdir, _POSIX_PATH_MAX, "%s/proc", 
	         tab->mntent_array[0].mnt_dir);
	if (procdir == NULL) {
		perror("Could not set mount directory");
		return 1;
	}

	/* Register signal handler. */
	act.sa_handler = childstatus;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (sigaction(SIGCHLD, &act, NULL) == -1) {
		perror("sigaction");
		return 1;
	}

	/* Initialize process list. */
	SLIST_INIT(&prochead);

	while (1) {
		logstatus(&prochead, procdir);

		/* Make pollfd structures from process list. */
		fds = makepollfds(&prochead, &nfds);
		if (fds == NULL) {
			perror("makepollfds");
			abort();
		}

		r = poll(fds, nfds, -1);
		if (r == -1 || r == 0) {
			if (r == 0 || errno == EINTR) {
				free(fds);
				continue;
			} else {
				perror("poll");
				abort();
			}
		}

		dopollfds(&prochead, procdir, fds, nfds);

		free(fds);
	}

	free(procdir);

	return 0;
}
