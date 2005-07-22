/*
 * (C) 2005 Frederik Grll <frederik.gruell@web.de>
 *
 * See COPYING in top-level directory.
 */


#include "dist-varstrip-parser.h"
#include "pvfs2-dist-varstrip.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int strips_parse_elem(char* inp, const PVFS_offset *prev_offset,
                             const PVFS_size *prev_size, unsigned *server_nr,
                             PVFS_offset *offset, PVFS_size *size);

static PINT_dist_strips* strips_alloc_mem(const char* inp);

void PINT_dist_strips_free_mem(PINT_dist_strips **strips)
{
    if (*strips)
    {
        free(*strips);
        *strips = 0;
    }
    return;
}


static int strips_parse_elem(
    char* inp, const PVFS_offset *prev_offset,
    const PVFS_size *prev_size, unsigned *server_nr,
    PVFS_offset *offset, PVFS_size *size)
{
    char *s_server, *s_size;
    unsigned i_server;
    PVFS_size i_size;

    if (prev_offset != NULL && prev_size != NULL) 
    {
        s_server = strtok(NULL, ":");
    }
    else
    {
        s_server = strtok(inp, ":");
    }

    if (s_server != NULL)
    {
        i_server = atoi(s_server);
        *server_nr = i_server;
    }
    else
    {
        return 1;
    }

    if (prev_offset != NULL && prev_size != NULL) 
    {
        *offset = (*prev_offset) + (*prev_size);
    }
    else
    {
        *offset = 0;
    }

    s_size = strtok(NULL, ";");
    if (s_size != NULL)
    {
        i_size = atoll(s_size);
        if (i_size > 0)
        {
            if (strlen(s_size) > 1)
            {
                switch (s_size[strlen(s_size) - 1])
                {
                    case 'k':
                    case 'K':
                        i_size *= 1024;
                        break;
                    case 'm':
                    case 'M':
                        i_size *= (1024 * 1024);
                        break;
                    case 'g':
                    case 'G':
                        i_size *= (1024 * 1024 * 1024);
                        break;
                }
            }
            *size = i_size;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    return 0;
}


static PINT_dist_strips* strips_alloc_mem(const char* inp)
{
    int i, count = 0;
    /* count ":" to allocate enough memory */
    for (i = 0; i < strlen(inp); i++)
    {
        if (inp[i] == ':')
        {
            count++;
        }
    }  

    if (!count)
    {
        /* no ";" found, abort */
        return (PINT_dist_strips*) NULL;
    }

    /* allocate array of struct slicing */
    return (PINT_dist_strips*) (malloc(sizeof(PINT_dist_strips) * count));
}

/*
 * parse hint to array of struct slicing
 * input sytax: {<datafile number>:<strip size>[K|M|G];}+
 */
int PINT_dist_strips_parse(
    const char *input, PINT_dist_strips **strips, unsigned *count)  
{
    char inp[PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH];
    PINT_dist_strips *strips_elem;
    PVFS_size *prev_size   = NULL;
    PVFS_offset *prev_offset = NULL;
    int i;

    *count = 0;
    *strips = 0;

    if (strlen(input) < PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH - 1)
    {
        strcpy(inp, input);
    }
    else
    {
        /* input string too long, abort */
        return -1;
    }

    *strips = strips_alloc_mem(inp);

    if (!(*strips))        
    {
        /* allocation failed, abort */
        return -1;
    }

    for (i = 0;; i++)
    {
        strips_elem = (*strips) + i;
        switch (
            strips_parse_elem(
                inp, prev_offset, prev_size, &(strips_elem->server_nr),
                &(strips_elem->offset), &(strips_elem->size)))
        {
            case 0:     
                /* do next element */
                prev_offset = &(strips_elem->offset);
                prev_size   = &(strips_elem->size);
                break;
            case -1:
                /* an error occured */
                PINT_dist_strips_free_mem(strips);
                *count = 0;
                return -1;
                break;
            case 1:
                /* finished */
                *count = i;
                if (*count == 0)    
                {         
                    /* 0 elements, abort */
                    PINT_dist_strips_free_mem(strips);
                    return -1;
                }
                else
                {
                    return 0;
                }
                break;
        }
    }
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
