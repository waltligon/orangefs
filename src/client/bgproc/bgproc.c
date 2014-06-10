/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>

#include <pvfs2.h>
#include <pvfs2-bgproc.h>

char *bgproc_fs;
char *bgproc_outdir;

int bgproc_setup(int initialize)
{
    int ret;
    bgproc_fs = getenv("bgproc_fs");
    if (!bgproc_fs)
    {
        fprintf(stderr, "missing environment variable bgproc_fs\n");
        exit(EXIT_FAILURE);
    }
    bgproc_outdir = getenv("bgproc_outdir");
    if (!bgproc_outdir)
    {
        fprintf(stderr, "missing environment variable bgproc_outdir\n");
        exit(EXIT_FAILURE);
    }
    if (initialize)
    {
        ret = PVFS_util_init_defaults();
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_init_defaults", ret);
            exit(EXIT_FAILURE);
        }
    }
    return 0;
}
