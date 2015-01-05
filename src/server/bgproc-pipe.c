/*
 * Copyright 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gen-locks.h"
#include "bgmon/bgmon.h"

#include "bgproc-pipe.h"

int bgproc_pipes[2] = {-1, -1};
static gen_mutex_t bgproc_pipe_mutex; 

static int bgproc_request(unsigned char *buf, size_t size,
    unsigned char *rbuf, size_t *rsize);

/* Start the parallel background process monitor. This should only be
 * called once, for it sets up a mutex and resources which will be later
 * controlled by it. */
int bgproc_startup(void)
{
    int r;
    int pfds_in[2] = {-1, -1};
    int pfds_out[2] = {-1, -1};
    if (gen_posix_mutex_init(&bgproc_pipe_mutex) != 0)
        return -1;
    r = pipe(pfds_in);
    if (r == -1)
        goto failure;
    r = pipe(pfds_out);
    if (r == -1)
        goto failure;
    r = fork();
    if (r == -1)
    {
        goto failure;
    }
    else if (r == 0)
    {
        char *args[2] = {"pvfs2-bgmon", NULL};
        close(pfds_in[1]);
        close(pfds_out[0]);
        if (dup2(pfds_in[0], 0) == -1 || dup2(pfds_out[1], 1) == -1)
        {
            perror("cannot start parallel background process monitor");
            _exit(EXIT_FAILURE);
        }
        close(pfds_in[0]);
        close(pfds_out[1]);
        execv(SBINDIR"/pvfs2-bgmon", args);
        perror("cannot start parallel background process monitor");
        _exit(EXIT_FAILURE);
    }
    else
    {
        close(pfds_in[0]);
        close(pfds_out[1]);
        bgproc_pipes[0] = pfds_out[0]; /* read here */
        bgproc_pipes[1] = pfds_in[1]; /* write here */
        return 0;
    }
failure:         
    if (pfds_in[0] != -1)
        close(pfds_in[0]);
    if (pfds_in[1] != -1) 
        close(pfds_in[1]); 
    if (pfds_out[0] != -1)        
        close(pfds_out[0]);        
    if (pfds_out[1] != -1)
        close(pfds_out[1]);
    return -1;
}

/* Send a request buf of size size and receive into rbuf which has
 * a maximal size of rsize then record the actual received size into
 * rsize. */
static int bgproc_request(unsigned char *buf, size_t size,
    unsigned char *rbuf, size_t *rsize)
{
    size_t i = 0;
    if (gen_posix_mutex_lock(&bgproc_pipe_mutex) != 0)
        return -1;
    /* Send request. */
    while (i < size)
    {
        size_t r;
        r = write(bgproc_pipes[1], buf+i, size-i);
        if (r == -1)
            goto fail;
        i += r;
    }
    i = read(bgproc_pipes[0], rbuf, *rsize);
    if (i == -1)
        return -1;
    *rsize = i;
    if (gen_posix_mutex_unlock(&bgproc_pipe_mutex) != 0)
        return -1;
    return 0;
fail:
    if (gen_posix_mutex_unlock(&bgproc_pipe_mutex) != 0)
        return -1;
    return -1;
}

long bgproc_start(const char *name)
{
    unsigned char req[200], resp[200];
    size_t len;
    len = strlen(name);
    if (len + 6 > sizeof req)
        return -1;
    req[0] = sizeof req >> 8 & 0xff;
    req[1] = sizeof req & 0xff;
    req[2] = TYPE_START >> 8 & 0xff;
    req[3] = TYPE_START & 0xff;
    req[4] = len >> 8 & 0xff;
    req[5] = len & 0xff;
    memcpy(req+6, name, len);
    len = sizeof resp;
    if (bgproc_request(req, sizeof req, resp, &len) == -1)
        return -1;
    if ((resp[0] << 8 | resp[1]) != len)
        return -1;
    if (len < 6)
        return -1;
    if ((resp[2] << 8 | resp[3]) != TYPE_START)
        return -1;
    return (int)(resp[4] << 8 | resp[5]);
}
