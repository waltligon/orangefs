/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include <pvfs2.h>

struct config {
	char *logpath;
	FILE *log;
};

char *start_pvfs(void);
int setupsignals(void);
void signal_sigchld(int);
void signal_sigusr1(int);
void log_info(char *, ...);
void log_error(char *, ...);
void log_state(int stop, int pid, char *cmdline, int signaled, int status);
int start_proc(char *);

struct config config;
int sigchld_received;
int sigusr1_received;

int main(void)
{
	char *basepath;

	basepath = start_pvfs();
	if (basepath == NULL) {
		fprintf(stderr, "Could not start PVFS.\n");
		return 1;
	}

	asprintf(&config.logpath, "%s/proc/log", basepath);

	setupsignals();

	config.log = fopen(config.logpath, "a");
	log_info("Log opened.");

	while (1) {
		char *line = NULL;
		size_t linecap;
		ssize_t len;
		char *newline;

		/* Handle signals received. */
		while (sigchld_received) {
			pid_t pid;
			int status;
			errno = 0;
			pid = wait(&status);
			if (pid == -1) {
				log_error("wait failed (%s)", strerror(errno));
				break;
			}
			if (WIFEXITED(status)) {
				log_state(1, pid, NULL, 0,
				          WEXITSTATUS(status));
			}
			if (WIFSIGNALED(status)) {
				log_state(1, pid, NULL, 1,
				          WTERMSIG(status));
			}
			sigchld_received--;
		}
		if (sigusr1_received) {
			FILE *old, *new;
			old = config.log;
			new = fopen(config.logpath, "a");
			if (new) {
				fclose(old);
				config.log = new;
				log_info("Log re-opened.");
			}
			sigusr1_received = 0;
		}

		/* Read line. */
		errno = 0;
		clearerr(stdin);
		len = getline(&line, &linecap, stdin);
		if (len == -1) {
			if (ferror(stdin)) {
				if (errno == EINTR) {
					if (line == NULL)
						free(line);
					continue;
				} else {
					log_error("getline failed (%s)",
					          strerror(errno));
				}
				return 1;
			} else if (feof(stdin)) {
				log_error("end of file.");
				break;
			}
		}
		newline = strrchr(line, '\n');
		if (newline)
			*newline = 0;
		errno = 0;
		if (start_proc(line) != 0)
			log_error("start_proc failed (%s)",
			          strerror(errno));
		free(line);
	}

	return 0;
}

char *start_pvfs(void)
{
	int r;
	const PVFS_util_tab *tab;
	/* Start PVFS. */
	r = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
	if (r < 0) {
		PVFS_perror("PVFS_sys_initialize", r);
		return NULL;
	}
	/* Parse pvfstab to obtain filesystem path. */
	tab = PVFS_util_parse_pvfstab(NULL);
	if (tab == NULL) {
		fprintf(stderr, "Failed to parse pvfstab.\n");
		return NULL;
	}
	if (tab->mntent_count < 1) {
		fprintf(stderr, "pvfstab does not contain any "
		        "filesystems.\n");
		return NULL;
	}
	return tab->mntent_array[0].mnt_dir;
}

int setupsignals(void)
{
	struct sigaction act;
	act.sa_handler = signal_sigchld;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGCHLD);
	sigaddset(&act.sa_mask, SIGUSR1);
	act.sa_flags = 0;
	if (sigaction(SIGCHLD, &act, NULL) == -1) {
		perror("sigaction");
		return 1;
	}
	act.sa_handler = signal_sigusr1;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGCHLD);
	sigaddset(&act.sa_mask, SIGUSR1);
	act.sa_flags = 0;
	if (sigaction(SIGUSR1, &act, NULL) == -1) {
		perror("sigaction");
		return 1;
	}
	return 0;
}

void signal_sigchld(int signal)
{
	sigchld_received++;
}

void signal_sigusr1(int signal)
{
	sigusr1_received = 1;
}

void log_info(char *fmt, ...)
{
	time_t t;
	char *s;
	va_list ap;
	va_start(ap, fmt);
	t = time(NULL);
	s = ctime(&t);
	*(strrchr(s, '\n')) = 0;
	fprintf(config.log, "I[%s]", s);
	vfprintf(config.log, fmt, ap);
	putc('\n', config.log);
	fflush(config.log);
	va_end(ap);
}

void log_error(char *fmt, ...)
{
	time_t t;
	char *s;
	va_list ap;
	va_start(ap, fmt);
	t = time(NULL);
	s = ctime(&t);
	*(strrchr(s, '\n')) = 0;
	fprintf(config.log, "E[%s]", s);
	vfprintf(config.log, fmt, ap);
	putc('\n', config.log);
	fflush(config.log);
	va_end(ap);
}

void log_state(int stop, int pid, char *cmdline, int signaled, int status)
{
	time_t t;
	char *s;
	t = time(NULL);
	s = ctime(&t);
	*(strrchr(s, '\n')) = 0;
	if (stop)
		fprintf(config.log, "S[%s]D %d %c %d\n", s, pid,
		        signaled ? 'S' : 'E', status);
	else
		fprintf(config.log, "S[%s]U %d %s\n", s, pid, cmdline);
	fflush(config.log);
}

int start_proc(char *cmdline)
{
	int r;
	r = fork();
	if (r == -1) {
		return -1;
	} else if (r == 0) {
		close(0);
		close(1);
		close(2);
		openat(0, "/dev/null", O_RDWR);
		dup2(0, 1);
		dup2(0, 2);
		execle("/bin/sh", "sh", "-c", cmdline, NULL, NULL);
		_exit(1);
	}
	log_state(0, r, cmdline, 0, 0);
	return 0;
}
