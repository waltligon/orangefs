/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "pvfs2-util.h"

void gen_rand_str(int len, char** gen_str);

int main(int argc,char **argv)
{
	PVFS_sysresp_init resp_init;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysresp_lookup resp_lk;
	char *filename;
	int name_sz;
	int ret = -1;
        int follow_link = PVFS2_LOOKUP_LINK_NO_FOLLOW;
	pvfs_mntlist mnt = {0,NULL};
	PVFS_fs_id fs_id;
	char* name;
	PVFS_credentials credentials;

	PVFS_handle lk_handle;
	PVFS_handle lk_fsid;

	if (argc != 2)
	{
            if ((argc == 3) && (atoi(argv[2]) == 1))
            {
                follow_link = PVFS2_LOOKUP_LINK_FOLLOW;
                goto lookup_continue;
            }
            printf("USAGE: %s /path/to/lookup [ 1 ]\n", argv[0]);
            printf(" -- if '1' is the last argument, links "
                   "will be followed\n");
            return 1;
	}
  lookup_continue:
	name_sz = strlen(argv[1]) + 1; /*include null terminator*/
	filename = malloc(name_sz);
        assert(filename);

	memcpy(filename, argv[1], name_sz);
	printf("lookup up path %s\n", filename);

	/* Parse PVFStab */
	ret = PVFS_util_parse_pvfstab(&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt, CLIENT_DEBUG, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}

	/* lookup the root handle */
	name = malloc(2);/*null terminator included*/
	name[0] = '/';
	name[1] = '\0';
	fs_id = resp_init.fsid_list[0];
	printf("looking up the root handle for fsid = %d\n", fs_id);
	ret = PVFS_sys_lookup(fs_id, name, credentials,
                              &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		PVFS_perror("PVFS_perror says", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("ROOT Handle:%ld\n", (long int)resp_look.pinode_refn.handle);
	
	/* test the lookup function */
        memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

	credentials.uid = 100;
	credentials.gid = 100;

	ret = PVFS_sys_lookup(fs_id, filename, credentials,
                              &resp_lk, follow_link);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		PVFS_perror("PVFS_perror says", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("\tHandle:%Ld\n",Ld(resp_lk.pinode_refn.handle));
	printf("\tFSID:%d\n",resp_lk.pinode_refn.fs_id);

	lk_handle = resp_lk.pinode_refn.handle;
	lk_fsid = resp_lk.pinode_refn.fs_id;

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	free(filename);
	free(name);
	return(0);
}
