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
#include <stdlib.h>

#include "pvfs2-util.h"
#include "str-utils.h"
#include "pvfs2-internal.h"

#define MAX_NUM_DIRENTS    32

void print_entry_attr(
    char *entry_name,
    PVFS_sys_attr *attr)
{
    char buf[128] = {0};
    PVFS_size computed_size = 0;
    time_t atime = (time_t)attr->atime;
    char f_type = '-';

    struct tm *time = gmtime(&atime);
    assert(time);

    if ((attr->objtype == PVFS_TYPE_METAFILE) &&
        (attr->mask & PVFS_ATTR_SYS_SIZE))
    {
        computed_size = attr->size;
    }
    else if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        computed_size = (PVFS_size)strlen(attr->link_target);
    }

    if (attr->objtype == PVFS_TYPE_DIRECTORY)
    {
        f_type =  'd';
    }
    else if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        f_type =  'l';
    }

    snprintf(buf,128,"%c%c%c%c%c%c%c%c%c%c    1 %d   %d\t%lld "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             f_type,
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
             lld(computed_size),
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour + 1),
             (time->tm_min + 1),
             entry_name);

    if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        assert(attr->link_target);
        printf("%s -> %s\n",buf,attr->link_target);
        free(attr->link_target);
    }
    else
    {
        printf("%s\n",buf);
    }
}

void print_entry(
    char *entry_name,
    PVFS_handle handle,
    PVFS_fs_id fs_id)
{
    PVFS_object_ref pinode_refn;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));

    PVFS_util_gen_credentials(&credentials);
    
    pinode_refn.handle = handle;
    pinode_refn.fs_id = fs_id;

    if (PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL,
                         &credentials, &getattr_response))
    {
        fprintf(stderr,"Failed to get attributes on handle 0x%08llx "
                "(fs_id is %d)\n",llu(handle),fs_id);
        return;
    }
    print_entry_attr(entry_name, &getattr_response.attr);
}

int do_list(
    PVFS_fs_id fs_id,
    char *start_dir)
{
    int i = 0;
    int pvfs_dirent_incount;
    char *name = NULL, *cur_file = NULL;
    PVFS_handle cur_handle;
    PVFS_sysresp_lookup lk_response;
    PVFS_sysresp_readdir rd_response;
    PVFS_sysresp_getattr getattr_response;
    PVFS_credentials credentials;
    PVFS_object_ref pinode_refn;
    PVFS_ds_position token;

    memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));
    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));

    name = start_dir;

    PVFS_util_gen_credentials(&credentials);

    if (PVFS_sys_lookup(fs_id, name, &credentials,
                        &lk_response, PVFS2_LOOKUP_LINK_NO_FOLLOW))
    {
        fprintf(stderr,"Failed to lookup %s on fs_id %d!\n",
                start_dir,fs_id);
        return 1;
    }

    pinode_refn.handle = lk_response.ref.handle;
    pinode_refn.fs_id = fs_id;
    pvfs_dirent_incount = MAX_NUM_DIRENTS;
    PVFS_util_gen_credentials(&credentials);

    if (PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL,
                         &credentials, &getattr_response) == 0)
    {
        if ((getattr_response.attr.objtype == PVFS_TYPE_METAFILE) ||
            (getattr_response.attr.objtype == PVFS_TYPE_SYMLINK))
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
                             (!token ? PVFS_READDIR_START : token),
                             pvfs_dirent_incount, &credentials, &rd_response))
        {
            fprintf(stderr,"readdir failed\n");
            return -1;
        }

        for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
        {
            cur_file = rd_response.dirent_array[i].d_name;
            cur_handle = rd_response.dirent_array[i].handle;

            print_entry(cur_file, cur_handle, fs_id);
        }
        token += rd_response.pvfs_dirent_outcount;

        if (rd_response.pvfs_dirent_outcount)
            free(rd_response.dirent_array);

    } while(rd_response.pvfs_dirent_outcount == pvfs_dirent_incount);

    return 0;
}

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id fs_id;

    if (argc > 2)
    {
        fprintf(stderr,"Usage: ls starting_dir\n");
        fprintf(stderr,"This is not a full featured version of LS(1)\n");
        return 1;
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }
    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
    }

    if (do_list(fs_id,((argc == 2) ? argv[1] : "/")))
    {
        return 1;
    }

    if (PVFS_sys_finalize())
    {
        fprintf(stderr,"Failed to finalize system interface\n");
        return 1;
    }

    return 0;
}
