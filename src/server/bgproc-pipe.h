/*
 * Copyright 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

extern int bgproc_pipes[2];

int bgproc_startup(void);
uint32_t bgproc_start(const char *name);
int bgproc_list(uint32_t *num_procs, uint32_t **ids, char ***names);
