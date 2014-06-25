/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <gossip.h>
#include <pvfs2.h>
#include <pvfs2-usrint.h>

#include <pvfs2-bgproc.h>

int bgproc_log_setup(void)
{
    if (gossip_enable(&gossip_mech_bgproc) < 0)
    {
        return -1;
    }
    return 0;
}

struct bgproc_mech_data {
    int fd;
};

static int bgproc_mech_startup(void *data, va_list ap)
{
    struct bgproc_mech_data *mydata = data;
    mydata->fd = pvfs_open(bgproc_log, O_WRONLY|O_APPEND|O_CREAT);
    if (mydata->fd == -1)
    {
        return -1;
    }
    return 0;
}

static int bgproc_mech_log(char *str, size_t len, void *data)
{
    struct bgproc_mech_data *mydata = data;
    if (pvfs_write(mydata->fd, str, len) < len)
    {
        return -1;
    }
    return 0;
}

static void bgproc_mech_shutdown(void *data)
{
    struct bgproc_mech_data *mydata = data;
    pvfs_close(mydata->fd);
}

struct gossip_mech gossip_mech_bgproc =
        {bgproc_mech_startup, bgproc_mech_log, bgproc_mech_shutdown,
        NULL, sizeof(struct bgproc_mech_data)};
