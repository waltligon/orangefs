/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS2_BGPROC_H
#define PVFS2_BGPROC_H

extern char *bgproc_fs;
extern char *bgproc_log;
extern char *bgproc_outdir;

int bgproc_setup(int);

int bgproc_log_setup(void);

#ifdef GOSSIP_H
extern struct gossip_mech gossip_mech_bgproc;
#endif

#endif
