/*
 * Copyright 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

extern int bgproc_pipes[2];

int bgproc_startup(void);
long bgproc_start(const char *name);
