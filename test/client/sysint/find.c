/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <client.h>
#include <string.h>
#include "helper.h"
#include "pvfs2-util.h"

void print_at_depth(char *name, int depth)
{
    /* we ignore depth for now */
    if (name)
    {
        printf("****  %s\n",name);
    }
}

/*
  returns -1 on error; 0 if handle is not a directory;
  1 otherwise
*/
int is_directory(PVFS_handle handle, PVFS_fs_id fs_id)
{
    PVFS_pinode_reference pinode_refn;
    uint32_t attrmask;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));
    memset(&credentials,0,sizeof(PVFS_credentials));

    pinode_refn.handle = handle;
    pinode_refn.fs_id = fs_id;
    attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;

    if (PVFS_sys_getattr(pinode_refn, attrmask, credentials, &getattr_response))
    {
        fprintf(stderr,"Failed to get attributes on handle 0x%08Lx "
                "(fs_id is %d)\n",handle,fs_id);
        return -1;
    }
    return ((getattr_response.attr.objtype == PVFS_TYPE_DIRECTORY) ? 1 : 0);
}

int directory_walk(PVFS_sysresp_init *init_response,
                   char *start_dir, char *base_dir, int depth)
{
    int i = 0;
    int is_dir = 0;
    char *cur_file = (char *)0;
    PVFS_handle cur_handle;
    PVFS_sysresp_lookup lk_response;
    PVFS_sysresp_readdir rd_response;
    char full_path[PVFS_NAME_MAX] = {0};
    PVFS_fs_id fs_id;
    char* name;
    PVFS_credentials credentials;
    PVFS_pinode_reference pinode_refn;
    PVFS_ds_position token;
    int pvfs_dirent_incount;

    gossip_debug(CLIENT_DEBUG, "DIRECTORY WALK CALLED WITH "
                 "base %s | %s\n",base_dir,start_dir);

    memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));

    if (base_dir)
    {
        strncpy(full_path,base_dir,PVFS_NAME_MAX);
        if (strlen(base_dir) > 1)
        {
            strcat(full_path,"/");
        }
        strncat(full_path,start_dir,PVFS_NAME_MAX);
    }
    else
    {
        strcpy(full_path,start_dir);
    }
    name = full_path;
    fs_id = init_response->fsid_list[0];
    credentials.uid = 100;
    credentials.gid = 100;

    if (PVFS_sys_lookup(fs_id, name, credentials, &lk_response))
    {
        fprintf(stderr,"Failed to lookup %s on fs_id %d!\n",
                start_dir,init_response->fsid_list[0]);
        return 1;
    }

    print_at_depth(name,depth);

    memset(&rd_response,0,sizeof(PVFS_sysresp_readdir));

    pinode_refn.handle = lk_response.pinode_refn.handle;
    pinode_refn.fs_id = init_response->fsid_list[0];
    token = PVFS2_READDIR_START;
    pvfs_dirent_incount = MAX_NUM_DIRENTS;
    credentials.uid = 100;
    credentials.gid = 100;

    if (PVFS_sys_readdir(pinode_refn, token, pvfs_dirent_incount, credentials,
			 &rd_response))
    {
        fprintf(stderr,"Failed to perform readdir operation\n");
        return 1;
    }

    if (!rd_response.pvfs_dirent_outcount)
    {
        gossip_debug(CLIENT_DEBUG,"No files found.\n");
        return 0;
    }

    gossip_debug(CLIENT_DEBUG, "%d files found.\n",
                 rd_response.pvfs_dirent_outcount);
    for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
    {
        cur_file = rd_response.dirent_array[i].d_name;
        cur_handle = rd_response.dirent_array[i].handle;

        gossip_debug(CLIENT_DEBUG,"Got handle 0x%08Lx\n",cur_handle);

        is_dir = is_directory(cur_handle,
                              init_response->fsid_list[0]);
        switch(is_dir)
        {
            case -1:
                /* if we had an error, warn */
                gossip_err("Failed to get attributes.  Skipping file %s\n",
                       cur_file);
                break;
            case 0:
                /* if we have a normal file, print it */
                {
                    char buf[PVFS_NAME_MAX] = {0};
                    snprintf(buf,PVFS_NAME_MAX,"%s/%s",
                             ((full_path && (strcmp(full_path,"/"))) ?
                              full_path : ""),cur_file);
                    print_at_depth(buf,depth);
                }
                break;
            case 1:
                /* if we have a dir, recurse */
                if (directory_walk(init_response,cur_file,full_path,depth+1))
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
        fprintf(stderr,"usage: %s <starting dir>\n",argv[0]);
        fprintf(stderr,"This is not a full featured version of FIND(1L)\n");
        return 1;
    }

    if (PVFS_util_parse_pvfstab(NULL,&mnt))
    {
        fprintf(stderr,"Error parsing pvfstab!\n");
        return 1;
    }

    memset(&init_response,0,sizeof(PVFS_sysresp_init));
    if (PVFS_sys_initialize(mnt, 0, &init_response))
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
