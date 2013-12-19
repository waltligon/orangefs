/*
 * Copyright 2013 Omnibond Systems LLC.
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <string.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"
#include "pint-cached-config.h"

int main(int argc, char *argv[])
{
    const PVFS_util_tab *tab;
    int ret;
    PVFS_fs_id fs_id;
    char path[PVFS_PATH_MAX];
    int addr_count;
    PVFS_BMI_addr_t *addr_array;
    int *statuses;
    int i;

    ret = PVFS_util_init_defaults();
    if (ret < 0) {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return 1;
    }

    tab = PVFS_util_parse_pvfstab(NULL);
    if (tab == NULL) {
        fprintf(stderr, "Could not parse pvfstab.\n");
        return 1;
    }

    if (tab->mntent_count == 0) {
        fprintf(stderr, "There are no filesystems in the pvfstab.\n");
        return 1;
    }

    ret = PVFS_util_resolve(tab->mntent_array[0].mnt_dir, &fs_id, path, PVFS_PATH_MAX);
    if (ret < 0) {
        PVFS_perror("PVFS_util_resolve", ret);
        return 1;
    }
    if (*path == 0) {
        path[0] = '/';
        path[1] = 0;
    }

    ret = PVFS_mgmt_count_servers(fs_id, PINT_SERVER_TYPE_ALL, &addr_count);
    if (ret < 0) {
        PVFS_perror("PVFS_mgmt_count_servers", ret);
        return 1;
    }

    if (addr_count <= 0) {
        fprintf(stderr, "Unable to load any server addresses\n");
        return 1;
    }

    addr_array = malloc(addr_count*sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL) {
        perror("malloc");
        return 1;
    }

    ret = PVFS_mgmt_get_server_array(fs_id, PINT_SERVER_TYPE_ALL,
                                     addr_array, &addr_count);
    if (ret < 0) {
        PVFS_perror("PVFS_mgmt_get_server_array", ret);
        return 1;
    }

    if (strcmp(argv[1], "stop") == 0) {
        statuses = malloc(sizeof(int)*addr_count);
        ret = PVFS_mgmt_proc_stop(fs_id, strtol(argv[2], NULL, 10),
                                  addr_count, addr_array, statuses);
        if (ret < 0) {
            PVFS_perror("PVFS_mgmt_proc_stop", ret);
            return 1;
        }
    } else if (strcmp(argv[1], "start") == 0) {
        statuses = malloc(sizeof(int)*addr_count);
        ret = PVFS_mgmt_proc_start(fs_id, argv[2], addr_count, addr_array, statuses);
        if (ret < 0) {
            PVFS_perror("PVFS_mgmt_proc_start", ret);
            return 1;
        }
    }
    for (i = 0; i < addr_count; i++) {
        if (statuses[i] >= 0) {
            printf("%d %s success! %d\n", i,
                   PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                   statuses[i]);
        } else {
            printf("%d %s failure! %d %s\n", i,
                   PVFS_mgmt_map_addr(fs_id, addr_array[i], NULL),
                   statuses[i], strerror(-statuses[i]));
        }
    }

    ret = PVFS_sys_finalize();
    if (ret < 0) {
        PVFS_perror("PVFS_sys_finalize", ret);
        return 1;
    }
    return 0;
}
