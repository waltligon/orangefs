/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include "pvfs2-util.h"

int main(int argc,char **argv)
{
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_readdir resp_readdir;
    int ret = -1, i = 0;
    int max_dirents_returned = 25;
    char starting_point[256] = "/";
    PVFS_fs_id fs_id;
    char* name;
    PVFS_credentials credentials;
    PVFS_object_ref pinode_refn;
    PVFS_ds_position token;
    int pvfs_dirent_incount;

    switch(argc)
    {
        case 3:
            sscanf(argv[2], "%d", &max_dirents_returned);
        case 2:
            strncpy(starting_point, argv[1], 256);
            break;
    }
    printf("no more than %d dirents should be returned per "
           "iteration\n", max_dirents_returned);

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


    name = starting_point;
    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_lookup(fs_id, name, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        return(-1);
    }

    printf("LOOKUP_RESPONSE===>\n\tresp_look.ref.handle = %Ld\n"
           "\tresp_look.ref.fs_id = %d\n",
           Ld(resp_look.ref.handle), resp_look.ref.fs_id);

    pinode_refn.handle = resp_look.ref.handle;
    pinode_refn.fs_id = fs_id;
    pvfs_dirent_incount = max_dirents_returned;

    token = 0;
    do
    {
        memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));
        ret = PVFS_sys_readdir(pinode_refn, (!token ? PVFS_READDIR_START :
                                             token), pvfs_dirent_incount, 
                               credentials, &resp_readdir);
        if (ret < 0)
        {
            printf("readdir failed with errcode = %d\n", ret);
            return(-1);
        }

        for(i = 0; i < resp_readdir.pvfs_dirent_outcount; i++)
        {
            printf("[%Lu]: %s\n",
                   Lu(resp_readdir.dirent_array[i].handle),
                   resp_readdir.dirent_array[i].d_name);
        }
        token += resp_readdir.pvfs_dirent_outcount;

        /*allocated by the system interface*/
        if (resp_readdir.pvfs_dirent_outcount)
            free(resp_readdir.dirent_array);

        /*
          if we got a short read, assume that we're finished
          readding all dirents
        */
        if (resp_readdir.pvfs_dirent_outcount < pvfs_dirent_incount)
        {
            break;
        }

    } while(resp_readdir.pvfs_dirent_outcount != 0);

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return 0;
}
