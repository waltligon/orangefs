/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>


extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

int main(int argc,char **argv)
{
	PVFS_sysresp_init resp_init;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysresp_readdir resp_readdir;
	int ret = -1, i = 0;
	pvfs_mntlist mnt = {0,NULL};
	int max_dirents_returned = 25;
	char starting_point[256] = "/";
	PVFS_fs_id fs_id;
	char* name;
	PVFS_credentials credentials;
	PVFS_pinode_reference pinode_refn;
	PVFS_ds_position token;
	int pvfs_dirent_incount;

	gossip_enable_stderr();
	gossip_set_debug_mask(1,CLIENT_DEBUG);

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

	/* Parse PVFStab */
	ret = parse_pvfstab(NULL,&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}

	/* lookup the directory handle */
	credentials.uid = 100;
	credentials.gid = 100;
	credentials.perms = 511;
	name = starting_point;
	fs_id = resp_init.fsid_list[0];
	ret = PVFS_sys_lookup(fs_id, name, credentials, &resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}

	printf("LOOKUP_RESPONSE===>\n\tresp_look.pinode_refn.handle = %Ld\n\tresp_look.pinode_refn.fs_id = %d\n",resp_look.pinode_refn.handle, resp_look.pinode_refn.fs_id);

	pinode_refn.handle = resp_look.pinode_refn.handle;
	pinode_refn.fs_id = fs_id;
	pvfs_dirent_incount = max_dirents_returned;

	credentials.uid = 100;
	credentials.gid = 100;
	credentials.perms = 511;

	token = 0;
        do
        {
            memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));
            ret = PVFS_sys_readdir(pinode_refn, (!token ? PVFS2_READDIR_START :
                                                 token), pvfs_dirent_incount, 
                                   credentials, &resp_readdir);
            if (ret < 0)
            {
		printf("readdir failed with errcode = %d\n", ret);
		return(-1);
            }

            for(i = 0; i < resp_readdir.pvfs_dirent_outcount; i++)
            {
                printf("name:%s\t%Ld\n",
                       resp_readdir.dirent_array[i].d_name,
                       resp_readdir.dirent_array[i].handle);
            }
            token += resp_readdir.pvfs_dirent_outcount;

            /*allocated by the system interface*/
            if (resp_readdir.pvfs_dirent_outcount)
                free(resp_readdir.dirent_array);

        } while(resp_readdir.pvfs_dirent_outcount != 0);

	/*close it down*/
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	gossip_disable();

	return(0);
}
