/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "bmi.h"


/**
 * Print out usage information
 */
static void print_usage(int argc, char** argv)
{
    printf("Usage: %s\n", argv[0]);
    return;
}

/**
 * Main
 */
int main(int argc, char **argv)
{
    const PVFS_util_tab* mnt;
    int rc;

    /* Ensure no arguments were passed */
    if (1 < argc)
    {
        print_usage(argc, argv);
        return -1;
    }
    
    /* Initialize the PVFS System */
    rc = PVFS_sys_initialize(0);
    if (0 != rc)
    {
        fprintf(stderr, "Unable to initialize PVFS\n");
        return -1;
    }
    
    /* Construct the list of mount points */
    mnt = PVFS_util_parse_pvfstab(0);
    if (0 != mnt)
    {
        int num_mnt_entries;
        int i;
        
        num_mnt_entries = mnt->mntent_count;
        for (i = 0; i < num_mnt_entries; ++i)
        {
            char* config_server;
            PVFS_BMI_addr_t server_addr;
            PVFS_credentials creds;
            
            /* Get the server for every mount point */
            config_server = mnt->mntent_array[i].pvfs_config_server;
            rc = BMI_addr_lookup(&server_addr, config_server);
            if (0 != rc)
            {
                fprintf(stderr, "Error looking up server: %s\n",
                        config_server);
            }
            
            /* Get the config file for each server */
            PVFS_util_gen_credentials(&creds);
            /*rc =  PINT_send_req(serv_addr, &serv_req, mntent_p->encoding,
              &decoded, &encoded_resp, op_tag);*/
        }
        
    }
    

    /* Compare the configurations of each file */

    /* Notify user of inconsistencies */
    
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
