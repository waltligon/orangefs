/*
 * (C) 2015 Omnibond Systems L.L.C.
 *
 * See COPYING in top-level directory.
 */

int parallel_start(const char *, pid_t *);
int parallel_stop(pid_t);
void parallel_got_sigchld(void);
