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

#include "client-state-machine.h"
#include "pint-sysint-utils.h"
#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "server-config.h"
#include "state-machine-fns.h"


/**
 * Print out usage information
 */
static void print_usage(int argc, char** argv)
{
    printf("Usage: %s\n", argv[0]);
    return;
}

/**
 * Compare the copy configuration to the master.  Return 0 if the configs are
 * the same, else non-zero.
 *
 * Currently implemented as a white space insensitive byte comparison.
 */
static int compare_configs(const char* master_config,
                           const char* config)
{
    printf("Config: %s", master_config);
    printf("\n");
    return 1;
}

/**
 * Populate the given config with the server's data
 */
static int get_config( PVFS_BMI_addr_t* server_addr,
                       struct PVFS_sys_mntent* mnt_entry,
                       char** fs_config_buf,
                       char** server_config_buf)
{
    int rc;
    PINT_client_sm* sm_p = NULL;
    PVFS_credentials creds;

    /* Retrieve credentials */
    PVFS_util_gen_credentials(&creds);
    
    /* Initialize the state machine */
    sm_p = malloc(sizeof(*sm_p));
    memset(sm_p, 0, sizeof(*sm_p));
    sm_p->cred_p = &creds;
    sm_p->msgarray_count = 1;
    sm_p->msgarray = &(sm_p->msgpair);
    sm_p->u.get_config.mntent = mnt_entry;
    sm_p->u.get_config.persist_config_buffers = 1;

    /* Get the config info */
    rc = PINT_client_state_machine_post(sm_p, PVFS_SERVER_GET_CONFIG);
    while (!sm_p->op_complete && (0 == rc))
    {
	rc = PINT_client_state_machine_test();
    }

    if (0 != rc)
    {
        fprintf(stderr, "Error occured while getting config.\n");
        return -1;
    }

    /* Copy the strings into outbound params */
    *fs_config_buf = malloc(sm_p->u.get_config.fs_config_buf_size + 1);
    *server_config_buf = malloc(sm_p->u.get_config.server_config_buf_size + 1);
    strncpy(*fs_config_buf,
            sm_p->u.get_config.fs_config_buf,
            sm_p->u.get_config.fs_config_buf_size + 1);
    strncpy(*server_config_buf,
            sm_p->u.get_config.server_config_buf,
            sm_p->u.get_config.server_config_buf_size + 1);

    /* Free state machine resources */
    free(sm_p->u.get_config.fs_config_buf);
    free(sm_p->u.get_config.server_config_buf);
    free(sm_p);
    
    return 0;        
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
    rc = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
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

        /* Iterate over all fsid's */
        num_mnt_entries = mnt->mntent_count;
        for (i = 0; i < num_mnt_entries; ++i)
        {
            PVFS_fs_id fs_id;
            PVFS_credentials creds;
            PVFS_BMI_addr_t* server_addrs;
            int server_count;
            char* master_fs_conf = 0;
            int j;

            /* Current fs id */
            rc = PVFS_sys_fs_add(&mnt->mntent_array[i]);
            if (0 != rc)
            {
                fprintf(stderr, "Unable to initialize target filesystem.\n");
                continue;
            }
            fs_id = mnt->mntent_array[i].fs_id;
            
            /* Retrieve the list of all servers for the fs id*/
            PVFS_util_gen_credentials(&creds);
            rc = PVFS_mgmt_count_servers(fs_id, creds, PVFS_MGMT_IO_SERVER,
                                         &server_count);

            if (0 != rc)
            {
                fprintf(stderr, "Unable to determine number of IO servers.\n");
                break;
            }
            server_addrs = malloc(server_count * sizeof(PVFS_BMI_addr_t));
            rc = PVFS_mgmt_get_server_array(fs_id, creds, PVFS_MGMT_IO_SERVER,
                                            server_addrs, &server_count);
            if (0 != rc)
            {
                fprintf(stderr, "Unable to retrieve array of server addrs.\n");
                break;
            }
            
            /* Get the server configs for each fs id */
            for (j = 0; j < server_count; j++)
            {
                char* fs_config_buf = 0;
                char* server_config_buf = 0;
                
                rc = get_config(server_addrs + j,
                                mnt->mntent_array + i,
                                &fs_config_buf,
                                &server_config_buf);

                if (0 != rc)
                {
                    fprintf(stderr, "No config for server, continuing.\n");
                    continue;
                }

                if (0 == j)
                {
                    master_fs_conf = fs_config_buf;

                    /* Compare config to the master config */
                    compare_configs(master_fs_conf, fs_config_buf);
                }
                else
                {
                    /* Compare config to the master config */
                    compare_configs(master_fs_conf, fs_config_buf);
                }
            }
        }
    }    

    printf("Check Complete.\n");
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
