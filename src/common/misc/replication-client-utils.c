/* 
 *  (C) 2001 Clemson University 
 *  
 *   See COPYING in top-level directory.
 *  
 */

#include <stdio.h>
#include <string.h>
#include "replication-client-utils.h"
#include "replication-common-utils.h"
#include "gossip.h"
#include "server-config.h"
#include "pint-sysint-utils.h"



/* This function pulls replication information from the config file and stores the data in the
 * replication structure within the create structure (sm_p->u.create.replication).
 */
int get_replication_from_config(replication_s *replication_p, PVFS_object_ref *parent)
{
    struct server_configuration_s *server_config = PINT_get_server_config_struct(parent->fs_id);
    filesystem_configuration_s *fs               = PINT_config_find_fs_id(server_config,parent->fs_id);
    int ret;

    /* NOTE: config info cannot change until PINT_put_server_config_struct() is called. */

    if (!fs)
    {
        gossip_err("%s:Unable to retrieve information from the config file for "
                   "filesystem (%d).\n"
                   ,__func__
                   ,parent->fs_id);
        return(-PVFS_EINVAL);
    }

    ret = copy_replication_info_from_config(fs,replication_p);
    if (ret)
    {
       gossip_err("%s:Error copying replication data from the config file.\n",__func__);
       return(ret);
    }

    //if(gossip_debug_enabled(GOSSIP_CLIENT_DEBUG))
    {
       gossip_err("Printing replication info from config file.\n");
       print_replication_structure(replication_p);
    }

    PINT_put_server_config_struct(server_config);

    return(0);
}/*end get_replication_from_config*/


/* wrapper function that copies variables from filesystem config to a replication structure */
int copy_replication_info_from_config(filesystem_configuration_s *fs
                                     ,replication_s *dest_p)
{
    replication_s src = {0};

    src.replication_switch                     =  fs->replication_switch;
    src.replication_number_of_copies           =  fs->replication_number_of_copies;
    src.replication_layout                     =  fs->replication_layout;

    return(copy_replication_info(&src,dest_p));

}/*end copy_replication_info_from_config*/

/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim:  ts=8 sts=4 sw=4 expandtab
 */                                                    
