/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <client.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "helper.h"

void print_entry_attr(
    char *entry_name,
    PVFS_sys_attr *attr)
{
    char buf[128] = {0};

    struct tm *time = gmtime((time_t *)&attr->ctime);
    assert(time);

    snprintf(buf,128,"%c%c%c%c%c%c%c%c%c%c    1 %d   %d\t0 "
             "%.4d-%.2d-%.2d %.2d:%.2d %s\n",
             ((attr->objtype == PVFS_TYPE_DIRECTORY) ? 'd' : '-'),
             ((attr->perms & PVFS_U_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_U_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_U_EXECUTE) ? 'x' : '-'),
             ((attr->perms & PVFS_G_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_G_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_G_EXECUTE) ? 'x' : '-'),
             ((attr->perms & PVFS_O_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_O_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_O_EXECUTE) ? 'x' : '-'),
             attr->owner,
             attr->group,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour + 1),
             (time->tm_min + 1),
             entry_name);
    printf("%s",buf);
}

void print_entry(
    char *entry_name,
    PVFS_handle handle,
    PVFS_fs_id fs_id)
{
    PVFS_pinode_reference pinode_refn;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));
    memset(&credentials,0,sizeof(PVFS_credentials));

    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = 511;
    
    pinode_refn.handle = handle;
    pinode_refn.fs_id = fs_id;

    if (PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL_NOSIZE,
                         credentials, &getattr_response))
    {
        fprintf(stderr,"Failed to get attributes on handle 0x%08Lx "
                "(fs_id is %d)\n",handle,fs_id);
        return;
    }
    print_entry_attr(entry_name, &getattr_response.attr);
}

int do_list(
    PVFS_sysresp_init *init_response,
    char *start_dir)
{
    int i = 0;
    int pvfs_dirent_incount;
    char *name = NULL, *cur_file = NULL;
    PVFS_handle cur_handle;
    PVFS_sysresp_lookup lk_response;
    PVFS_sysresp_readdir rd_response;
    PVFS_sysresp_getattr getattr_response;
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;
    PVFS_pinode_reference pinode_refn;
    PVFS_ds_position token;

    memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));
    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));

    name = start_dir;
    fs_id = init_response->fsid_list[0];
    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = 511;

    if (PVFS_sys_lookup(fs_id, name, credentials, &lk_response))
    {
        fprintf(stderr,"Failed to lookup %s on fs_id %d!\n",
                start_dir,init_response->fsid_list[0]);
        return 1;
    }

    pinode_refn.handle = lk_response.pinode_refn.handle;
    pinode_refn.fs_id = init_response->fsid_list[0];
    pvfs_dirent_incount = MAX_NUM_DIRENTS;
    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = 511;

    if (PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL_NOSIZE,
                         credentials, &getattr_response) == 0)
    {
        if (getattr_response.attr.objtype == PVFS_TYPE_METAFILE)
        {
            char segment[128] = {0};
            PINT_remove_base_dir(name, segment, 128);
            print_entry_attr(segment, &getattr_response.attr);
            return 0;
        }
    }

    token = 0;
    do
    {
        memset(&rd_response,0,sizeof(PVFS_sysresp_readdir));
        if (PVFS_sys_readdir(pinode_refn,
                             (!token ? PVFS2_READDIR_START : token),
                             pvfs_dirent_incount, credentials, &rd_response))
        {
            fprintf(stderr,"readdir failed\n");
            return -1;
        }

        for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
        {
            cur_file = rd_response.dirent_array[i].d_name;
            cur_handle = rd_response.dirent_array[i].handle;

            print_entry(cur_file, cur_handle, init_response->fsid_list[0]);
        }
        token += rd_response.pvfs_dirent_outcount;

        if (rd_response.pvfs_dirent_outcount)
            free(rd_response.dirent_array);

    } while(rd_response.pvfs_dirent_outcount != 0);

    return 0;
}

int main(int argc, char **argv)
{
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init init_response;

/*     gossip_enable_stderr(); */
/*     gossip_set_debug_mask(1, CLIENT_DEBUG); */

    if (argc > 2)
    {
        fprintf(stderr,"Usage: ls starting_dir\n");
        fprintf(stderr,"This is not a full featured version of LS(1)\n");
        return 1;
    }

    if (parse_pvfstab(NULL,&mnt))
    {
        fprintf(stderr,"Error parsing pvfstab!\n");
        return 1;
    }

    memset(&init_response,0,sizeof(PVFS_sysresp_init));
    if (PVFS_sys_initialize(mnt, &init_response))
    {
        fprintf(stderr,"Cannot initialize system interface\n");
        return 1;
    }

    if (do_list(&init_response,((argc == 2) ? argv[1] : "/")))
    {
        return 1;
    }

    if (PVFS_sys_finalize())
    {
        fprintf(stderr,"Failed to finalize system interface\n");
        return 1;
    }

/*     gossip_disable(); */

    return 0;
}
