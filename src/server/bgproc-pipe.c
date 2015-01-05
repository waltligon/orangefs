/*
 * Copyright 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bgproc-pipe.h"

int bgproc_pipes[2] = {-1, -1};

int bgproc_startup(void)
{
    int r;
    int pfds_in[2] = {-1, -1};
    int pfds_out[2] = {-1, -1};
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

