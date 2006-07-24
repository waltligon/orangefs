/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <string.h>
#include "trove.h"
#include "pvfs2-storage.h"
#include "dotconf.h"
#include "server-config.h"
#include "gossip.h"
#include "pvfs2-internal.h"

void print_filesystem_configuration(struct filesystem_configuration_s *fs);

int main(int argc, char **argv)
{
    PINT_llist *cur = NULL;
    struct server_configuration_s serverconfig;
    struct host_alias_s *cur_alias;
    struct filesystem_configuration_s *cur_fs = NULL;
    int alias_count = 1;

    if (argc != 3)
    {
        printf("Usage: %s <fs.conf> <server.conf>\n",argv[0]);
        return 1;
    }

    gossip_enable_stderr();

    memset(&serverconfig,0,sizeof(serverconfig));
    if (PINT_parse_config(&serverconfig, argv[1], argv[2]))
    {
        printf("Failed to parse config files\n");
        return 1;
    }

    /* dump all gathered config values */
    fprintf(stderr,"--- Printing filesystem configuration\n\n");

    fprintf(stderr,"Server ID                : %s\n",
            serverconfig.host_id);
    fprintf(stderr,"Storage Space            : %s\n",
            serverconfig.storage_path);
    fprintf(stderr,"FS Config File Name      : %s (%d bytes)\n",
            serverconfig.fs_config_filename,
            (int)serverconfig.fs_config_buflen);
    fprintf(stderr,"Server Config File Name  : %s (%d bytes)\n",
            serverconfig.server_config_filename,
            (int)serverconfig.server_config_buflen);
    fprintf(stderr,"Initial Unexp Requests   : %d\n",
            serverconfig.initial_unexpected_requests);
    fprintf(stderr,"Configured Log File      : %s\n",
            serverconfig.logfile);
    fprintf(stderr,"Event Logging Mask String: %s\n",
            serverconfig.event_logging);

    fprintf(stderr,"\n--- Host Aliases (alias => address):\n");
    cur = serverconfig.host_aliases;
    while(cur)
    {
        cur_alias = PINT_llist_head(cur);
        if (!cur_alias)
        {
            break;
        }

        fprintf(stderr,"%.2d)  %s => %s\n", alias_count++,
                cur_alias->host_alias, cur_alias->bmi_address);
        cur = PINT_llist_next(cur);
    }

    fprintf(stderr,"\n");
    cur = serverconfig.file_systems;
    while(cur)
    {
        cur_fs = PINT_llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        print_filesystem_configuration(cur_fs);
        cur = PINT_llist_next(cur);
    }

    fprintf(stderr,"\n--- Analyzing filesystem configuration\n\n");
    if (PINT_config_is_valid_configuration(&serverconfig))
    {
        fprintf(stderr,"\nOK: Configuration file is VALID\n");
    }
    else
    {
        fprintf(stderr,"\nERROR: Configuration file is INVALID\n");
    }

    PINT_config_release(&serverconfig);

    gossip_disable();
    return 0;
}

void print_filesystem_configuration(struct filesystem_configuration_s *fs)
{
    PINT_llist *cur = NULL;
    struct host_handle_mapping_s *cur_h_mapping = NULL;

    if (fs)
    {
        fprintf(stderr,"=========== Reporting FS \"%s\" Information "
                "===========\n",fs->file_system_name);
        fprintf(stderr,"Collection ID         : %d\n",fs->coll_id);
        fprintf(stderr,"Root Handle           : %llu\n",
                llu(fs->root_handle));
        fprintf(stderr,"Handle Recycle Timeout: %d seconds\n",
                (int)fs->handle_recycle_timeout_sec.tv_sec);
        fprintf(stderr,"Trove Sync Meta       : %s\n",
                ((fs->trove_sync_meta == TROVE_SYNC) ?
                 "yes" : "no"));
        fprintf(stderr,"Trove Sync Data       : %s\n",
                ((fs->trove_sync_data == TROVE_SYNC) ?
                 "yes" : "no"));
        fprintf(stderr,"Flow Protocol         : ");
        switch(fs->flowproto)
        {
            case FLOWPROTO_DUMP_OFFSETS:
                fprintf(stderr,"flowproto_dump_offsets\n");
                        break;
            case FLOWPROTO_BMI_CACHE:
                fprintf(stderr,"flowproto_bmi_cache\n");
                        break;
            case FLOWPROTO_MULTIQUEUE:
                fprintf(stderr,"flowproto_multiqueue\n");
                        break;
            default:
                fprintf(stderr,"Unknown (<== ERROR!)\n");
                break;
        }

        fprintf(stderr,"\n  --- Meta Server(s) for %s (%d total):\n",
                fs->file_system_name,PINT_llist_count(fs->meta_handle_ranges));
        cur = fs->meta_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            fprintf(stderr,"    %s\n",
                    cur_h_mapping->alias_mapping->host_alias);
            cur = PINT_llist_next(cur);
        }

        fprintf(stderr,"\n  --- Data Server(s) for %s (%d total):\n",
                fs->file_system_name,PINT_llist_count(fs->data_handle_ranges));
        cur = fs->data_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            fprintf(stderr,"    %s\n",
                    cur_h_mapping->alias_mapping->host_alias);
            cur = PINT_llist_next(cur);
        }

        fprintf(stderr,"\n  --- Meta Handle Mappings for %s:\n",
                fs->file_system_name);
        cur = fs->meta_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            fprintf(stderr,"    %s has handle range %s\n",
                    cur_h_mapping->alias_mapping->host_alias,
                    cur_h_mapping->handle_range);
            cur = PINT_llist_next(cur);
        }

        fprintf(stderr,"\n  --- Data Handle Mappings for %s:\n",
                fs->file_system_name);
        cur = fs->data_handle_ranges;
        while(cur)
        {
            cur_h_mapping = PINT_llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            fprintf(stderr,"    %s has handle range %s\n",
                    cur_h_mapping->alias_mapping->host_alias,
                    cur_h_mapping->handle_range);
            cur = PINT_llist_next(cur);
        }
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
