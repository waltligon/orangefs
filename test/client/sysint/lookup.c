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
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "client.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"
#include "pvfs2.h"
#include "str-utils.h"
#include "bmi.h"
#include "security-util.h"

/*
  arbitrarily restrict the number of paths
  that this ls version can take as arguments
*/
#define MAX_NUM_PATHS       8

void gen_rand_str(int len, char** gen_str);

int main(int argc,char **argv)
{
    int ret = -1;
    int follow_link = PVFS2_LOOKUP_LINK_NO_FOLLOW;
    PVFS_sysresp_lookup resp_lk;
    PVFS_fs_id fs_id;
    PVFS_credential *creds;
	PVFS_credential *cred;
    char *filename = NULL;
    char pvfs_path[MAX_NUM_PATHS][PVFS_NAME_MAX];
    const PVFS_util_tab* tab;
    int found_one = 0;
    int ncreds;
	int i = 0;
    PVFS_BMI_addr_t addr;

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

    filename = argv[1];
    printf("lookup up path %s\n", filename);

    tab = PVFS_util_parse_pvfstab(NULL);
    if (!tab)
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
        return(-1);
    }

    for(i = 0; i < MAX_NUM_PATHS; i++)
    {
        memset(pvfs_path[i],0,PVFS_NAME_MAX);
    }

    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	return(-1);
    }

    /* initialize each file system that we found in the tab file */
    for(i = 0; i < tab->mntent_count; i++)
    {
	ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
	if (ret == 0)
        {
	    found_one = 1;
        }
    }

    if (!found_one)
    {
	fprintf(stderr, "Error: could not initialize any file systems "
                "from %s\n", tab->tabfile_name);
	PVFS_sys_finalize();
	return(-1);
    }

    /* generate a credential for each known file system */

    creds = calloc(tab->mntent_count, sizeof(PVFS_credential));
	cred = calloc(1, sizeof(PVFS_credential));
    if (!creds || !cred)
    {
        perror("calloc");
        PVFS_sys_finalize();
        exit(EXIT_FAILURE);
    }
    ncreds = 0;

    for (i = 0; i < tab->mntent_count; i++)
    {

        ret = BMI_addr_lookup(&addr, 
                              tab->mntent_array[i].the_pvfs_config_server);
        if (ret < 0)
        {
            fprintf(stderr, "Failed to resolve BMI address %s\n",
                    tab->mntent_array[i].the_pvfs_config_server);
			return (ret);
        }

        ret = PVFS_util_gen_credential(tab->mntent_array[i].fs_id,
                                       addr,
                                       NULL,
                                       NULL,
                                       &creds[i]);
        if (ret < 0)
        {
            fprintf(stderr, "Failed to generate credential for fsid %d\n",
                    tab->mntent_array[i].fs_id);
        }
		else
		{
			fs_id = tab->mntent_array[i].fs_id;
			cred = &creds[i];

			memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));

    		ret = PVFS_sys_lookup(fs_id, filename, cred,
                          &resp_lk, follow_link, NULL);
    
    		if (ret < 0)
    		{
      	  		printf("Lookup failed with errcode = %d\n", ret);
        		PVFS_perror("PVFS_perror says", ret);
    		}
			else
			{
    			printf("Handle     : %llu\n", llu(resp_lk.ref.handle));
    			printf("FS ID      : %d\n", resp_lk.ref.fs_id);
			}
		}

        ncreds += 1;

    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return 0;
}
