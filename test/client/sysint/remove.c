/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "client.h"
#include "pvfs2-util.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"
#include "pvfs2.h"
#include "bmi.h"
#include "security-util.h"

/*
  arbitrarily restrict the number of paths
  that this ls version can take as arguments
*/
#define MAX_NUM_PATHS       8

int main(int argc,char **argv)
{
    int ret = -1;
    char str_buf[256] = {0};
    char *filename = (char *)0;
    char* entry_name;
    PVFS_object_ref parent_refn;
    PVFS_credential *creds;
    PVFS_credential *cred;
	PVFS_fs_id fs_id;
	char pvfs_path[MAX_NUM_PATHS][PVFS_NAME_MAX];
    const PVFS_util_tab* tab;
    int found_one = 0;
    int ncreds;
	int i = 0;
    PVFS_BMI_addr_t addr;

    if (argc != 2)
    {
        printf("usage: %s file_to_remove\n", argv[0]);
        return 1;
    }
    filename = argv[1];

    if (PINT_remove_base_dir(filename,str_buf,256))
    {
        if (filename[0] != '/')
        {
            printf("You forgot the leading '/'\n");
        }
        printf("Cannot retrieve entry name for creation on %s\n",
               filename);
        return(-1);
    }
    printf("File to be removed is %s\n",str_buf);

    entry_name = str_buf;

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
			break;
		}

        ncreds += 1;

    }

	fs_id = tab->mntent_array[i].fs_id;
	cred = &creds[i];

    ret = PINT_lookup_parent(filename, fs_id, cred,
                             &parent_refn.handle);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_lookup_parent", ret);
	return(-1);
    }
    parent_refn.fs_id = fs_id;

    ret = PVFS_sys_remove(entry_name, parent_refn, cred, NULL);
    if (ret < 0)
    {
        PVFS_perror("remove failed ", ret);
        return(-1);
    }

    printf("===================================\n");
    printf("file named %s has been removed.\n", filename);

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }

    return(0);
}
