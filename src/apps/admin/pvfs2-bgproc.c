/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pint-cached-config.h"

#define MODE_STOP 1
#define MODE_START 2
#define MODE_STATUS 3

int stop(PVFS_fs_id fs_id, int addr_count, PVFS_BMI_addr_t *addr_array,
        uint32_t handle)
{
    int ret, i;
    int *statuses;
    uint32_t *handles;
    statuses = calloc(addr_count, sizeof *statuses);
    handles = calloc(addr_count, sizeof *handles);
    if (!statuses || !handles)
    {
        (void)fputs("out of memory\n", stderr);
        return EXIT_FAILURE;
    }
    for (i = 0; i < addr_count; i++)
        handles[i] = handle;
    ret = PVFS_mgmt_proc_stop(fs_id, addr_count, addr_array, handles,
            statuses);
    if (ret < 0)
    {
        (void)PVFS_perror("PVFS_mgmt_proc_stop", ret);
        return EXIT_FAILURE;
    }
    for (i = 0; i < addr_count; i++)
    {
        if (statuses[i] == 0)
        {
            (void)printf("%s stopped %lu\n",
                PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                (unsigned long)handles[i]);
        }
        else
        {
            (void)printf("%s failure to stop %lu\n",
                PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                (unsigned long)handles[i]);
        }
    }
    (void)free(statuses);
    (void)free(handles);
    return EXIT_SUCCESS;
}

int start(PVFS_fs_id fs_id, int addr_count, PVFS_BMI_addr_t *addr_array,
        char *cmdline)
{
    int ret, i;
    int *statuses;
    uint32_t *handles;
    statuses = calloc(addr_count, sizeof *statuses);
    handles = calloc(addr_count, sizeof *handles);
    if (!statuses || !handles)
    {
        (void)fputs("out of memory\n", stderr);
        return EXIT_FAILURE;
    }
    ret = PVFS_mgmt_proc_start(fs_id, cmdline, addr_count, addr_array,
            statuses, handles);
    if (ret < 0)
    {
        (void)PVFS_perror("PVFS_mgmt_proc_start", ret);
        return EXIT_FAILURE;
    }
    for (i = 0; i < addr_count; i++)
    {
        if (statuses[i] == 0)
        {
            (void)printf("%s started %lu\n",
                PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                (unsigned long)handles[i]);
        }
        else
        {
            (void)printf("%s failure to start %d %s\n",
                PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                statuses[i], strerror(-statuses[i]));
        }
    }
    (void)free(statuses);
    (void)free(handles);
    return EXIT_SUCCESS;
}

int status(PVFS_fs_id fs_id, int addr_count, PVFS_BMI_addr_t *addr_array)
{
    int ret, i, problem = 0;
    uint32_t num, j;
    uint32_t *handles;
    char **cmdlines;
    for (i = 0; i < addr_count; i++)
    {
        ret = PVFS_mgmt_proc_status(fs_id, 1, &(addr_array[i]),
                &num, &handles, &cmdlines);
        if (ret < 0)
        {
            problem = 1;
            (void)PVFS_perror("PVFS_mgmt_proc_status", ret);
            (void)fprintf(stderr, "server: %s\n",
                    PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL));
            continue;
        }
        for (j = 0; j < num; j++)
        {
            (void)printf("%-40s %lu\t%s\n",
                    PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                    (unsigned long)handles[j], cmdlines[j]);
            (void)free(cmdlines[j]);
        }
        (void)free(handles);
        (void)free(cmdlines);
    }
    if (problem)
    {
        return EXIT_FAILURE;
    }
    else
    {
        return EXIT_SUCCESS;
    }
}

void usage(void)
{
    (void)fputs("usage: pvfs2-bgproc -K -h handle -s server\n",
            stderr);
    (void)fputs("\tstop (kill) handle on server\n", stderr);
    (void)fputs("usage: pvfs2-bgproc -R -c cmdline [-s server]\n", stderr);
    (void)fputs("\tstart (run) le on server\n", stderr);
    (void)fputs("usage: pvfs2-bgproc -S [-s server]\n", stderr);
    (void)fputs("\tshow status information\n", stderr);
}

int main(int argc, char *argv[])
{
    int c, mode = 0, hvalid = 0;
    char *cmdline = NULL, *serverlist = NULL;
    long handle = 0;
    int ret;
    const PVFS_util_tab *tab;
    PVFS_fs_id fs_id;
    char path[PVFS_PATH_MAX];
    int addr_count;
    PVFS_BMI_addr_t *addr_array = NULL;
    if (argc < 2)
    {
        (void)usage();
        return EXIT_FAILURE;
    }
    while ((c = getopt(argc, argv, "KRSc:h:s:")) != -1)
    {
        switch (c)
        {
        case 'K':
            if (mode != 0)
            {
                (void)usage();
                return EXIT_FAILURE;
            }
            mode = MODE_STOP;
            break;
        case 'R':
            if (mode != 0)
            {
                (void)usage();
                return EXIT_FAILURE; }
            mode = MODE_START;
            break;
        case 'S':
            if (mode != 0)
            {
                (void)usage();
                return EXIT_FAILURE;
            }
            mode = MODE_STATUS;
            break;
        case 'c':
            cmdline = optarg;
            break;
        case 'h':
            handle = strtol(optarg, NULL, 10);
            hvalid = 1;
            break;
        case 's':
            serverlist = optarg;
            break;
        case '?':
            (void)usage();
            return EXIT_FAILURE;
        }
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return EXIT_FAILURE;
    }

    tab = PVFS_util_parse_pvfstab(NULL);
    if (tab == NULL)
    {
        fputs("there are no filesystems in the pvfstab\n", stderr);
        return EXIT_FAILURE;
    }

    ret = PVFS_util_resolve(tab->mntent_array[0].mnt_dir, &fs_id,
            path, PVFS_PATH_MAX);
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_resolve", ret);
        return EXIT_FAILURE;
    }
    if (*path == 0)
    {
        path[0] = '/';
        path[1] = 0;
    }

    /* Parse server list here. */
    if (serverlist == NULL && mode != MODE_STOP)
    {
        ret = PVFS_mgmt_count_servers(fs_id, PINT_SERVER_TYPE_ALL,
                &addr_count);
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_count_servers", ret);
            return EXIT_FAILURE;
        }
        if (addr_count <= 0)
        {
            fputs("unable to load any server addresses\n", stderr);
            return EXIT_FAILURE;
        }
        addr_array = calloc(addr_count, sizeof *addr_array);
        if (addr_array == NULL)
        {
            fputs("out of memory\n", stderr);
            return EXIT_FAILURE;
        }
        ret = PVFS_mgmt_get_server_array(fs_id, PINT_SERVER_TYPE_ALL,
                addr_array, &addr_count);
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_get_server_array", ret);
            return EXIT_FAILURE;
        }
    }
    else if (serverlist != NULL)
    {
        int i = 0;
        char *s, *ss;
        /* count servers */
        s = serverlist;
        addr_count++;
        while (*s != 0)
        {
            if (*s++ == ',')
            {
                addr_count++;
            }
        }
        /* allocate memory */
        addr_array = calloc(addr_count, sizeof *addr_array);
        if (addr_array == NULL)
        {
            fputs("out of memory\n", stderr);
            return EXIT_FAILURE;
        }
        /* parse list */
        s = serverlist;
        do
        {
            ss = strchr(s, ',');
            if (ss != NULL)
            {
                *ss = 0;
            }
            ret = BMI_addr_lookup(&(addr_array[i++]), s);
            if (ret < 0)
            {
                PVFS_perror("BMI_addr_lookup", ret);
                return EXIT_FAILURE;
            }
            if (ss != NULL)
            {
                s = ss+1;
            }
        } while (ss != NULL);
    }

    switch (mode)
    {
    case MODE_STOP:
        if (!hvalid || !serverlist)
        {
            (void)usage();
            return EXIT_FAILURE;
        }
        return stop(fs_id, addr_count, addr_array, handle);
    case MODE_START:
        if (!cmdline)
        {
            (void)usage();
            return EXIT_FAILURE;
        }
        return start(fs_id, addr_count, addr_array, cmdline);
    case MODE_STATUS:
        return status(fs_id, addr_count, addr_array);
    }

    (void)usage();
    return EXIT_FAILURE;
}
