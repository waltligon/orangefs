/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <client.h>
#include <string.h>
#include "helper.h"

void print_at_depth(char *name, PVFS_handle handle, int depth)
{
    int i = 0;

    if (name && handle)
    {
        for(i = 0; i < depth; i++)
        {
            printf("\t");
        }
        printf("****  %s\n",name);
    }
}

/*
  returns -1 on error; 0 if handle is not a directory;
  1 otherwise
*/
int is_directory(PVFS_handle handle, PVFS_fs_id fs_id)
{
    PVFS_sysreq_getattr getattr_request;
    PVFS_sysresp_getattr getattr_response;

    memset(&getattr_request,0,sizeof(PVFS_sysreq_getattr));
    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));

    getattr_request.pinode_refn.handle = handle;
    getattr_request.pinode_refn.fs_id = fs_id;
    getattr_request.attrmask = ATTR_BASIC;

    if (PVFS_sys_getattr(&getattr_request,&getattr_response))
    {
        fprintf(stderr,"Failed to get attributes on handle 0x%08Lx "
                "(fs_id is %d)\n",handle,fs_id);
        return -1;
    }
    return ((getattr_response.attr.objtype == ATTR_DIR) ? 1 : 0);
}

int directory_walk(PVFS_sysresp_init *init_response,
                   char *start_dir, char *base_dir, int depth)
{
    int i = 0;
    int is_dir = 0;
    char *cur_file = (char *)0;
    PVFS_sysreq_lookup lk_request;
    PVFS_sysresp_lookup lk_response;
    PVFS_sysreq_readdir rd_request;
    PVFS_sysresp_readdir rd_response;
    char full_path[MAX_PVFS_PATH_LEN] = {0};

    printf("DIRECTORY WALK CALLED WITH base %s | %s\n",base_dir,start_dir);

    memset(&lk_request,0,sizeof(PVFS_sysreq_lookup));
    memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));

    if (base_dir)
    {
        strncpy(full_path,base_dir,MAX_PVFS_PATH_LEN);
        if (strlen(base_dir) > 1)
        {
            strcat(full_path,"/");
        }
        strncat(full_path,start_dir,MAX_PVFS_PATH_LEN);
        lk_request.name = full_path;
    }
    else
    {
        strcpy(full_path,"/");
        lk_request.name = start_dir;
    }
    lk_request.fs_id = init_response->fsid_list[0];
    lk_request.credentials.uid = 100;
    lk_request.credentials.gid = 100;
    lk_request.credentials.perms = 1877;

    if (PVFS_sys_lookup(&lk_request,&lk_response))
    {
        fprintf(stderr,"Failed to lookup %s on fs_id %d!\n",
                start_dir,init_response->fsid_list[0]);
        return 1;
    }

    print_at_depth(lk_request.name,
                   lk_response.pinode_refn.handle,depth);

    memset(&rd_request,0,sizeof(PVFS_sysreq_readdir));
    memset(&rd_response,0,sizeof(PVFS_sysresp_readdir));

    rd_request.pinode_refn.handle = lk_response.pinode_refn.handle;
    rd_request.pinode_refn.fs_id = init_response->fsid_list[0];
    rd_request.token = PVFS2_READDIR_START;
    rd_request.pvfs_dirent_incount = MAX_NUM_DIRENTS;
    rd_request.credentials.uid = 100;
    rd_request.credentials.gid = 100;
    rd_request.credentials.perms = 1877;

    if (PVFS_sys_readdir(&rd_request,&rd_response))
    {
        fprintf(stderr,"Failed to perform readdir operation\n");
        return 1;
    }

    if (!rd_response.pvfs_dirent_outcount)
    {
        printf("No files found.\n");
        return 0;
    }

    printf("%d files found.\n",rd_response.pvfs_dirent_outcount);
    for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
    {
        cur_file = rd_response.dirent_array[i].d_name;

        print_at_depth(cur_file,rd_response.dirent_array[i].handle,depth);

        fprintf(stderr,"Got handle 0x%08Lx\n",
                rd_response.dirent_array[i].handle);

        is_dir = is_directory(rd_response.dirent_array[i].handle,
                              init_response->fsid_list[0]);
        switch(is_dir)
        {
            case -1:
                /* if we had an error, warn */
                printf("Failed to get attributes.  Skipping file %s\n",
                       cur_file);
                break;
            case 0:
                /* if we have a normal file, do nothing */
                break;
            case 1:
                /* if we have a dir, recurse */
                if(directory_walk(init_response,cur_file,full_path,depth+1))
                {
                    fprintf(stderr,"Failed directory walk at depth %d\n",
                            depth+1);
                    return 1;
                }
                break;
        }
    }
    return 0;
}


int main(int argc, char **argv)
{
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init init_response;

    if (argc != 2)
    {
        fprintf(stderr,"usage: %s start_dir\n", argv[0]);
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

    if (directory_walk(&init_response,argv[1],NULL,0))
    {
        fprintf(stderr,"Failed to do directory walk\n");
        return 1;
    }

    if (PVFS_sys_finalize())
    {
        fprintf(stderr,"Failed to finalize PVFS\n");
        return 1;
    }
    return 0;
}
