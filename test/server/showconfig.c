#include <stdio.h>
#include <string.h>
#include "trove.h"
#include "pvfs2-storage.h"
#include "dotconf.h"
#include "server-config.h"
#include "gossip.h"


void print_filesystem_configuration(struct filesystem_configuration_s *fs)
{
    struct llist *cur = NULL;
    char *cur_str = (char *)0;
    struct host_handle_mapping_s *cur_h_mapping = NULL;

    if (fs)
    {
        fprintf(stderr,"File system name: %s\n",fs->file_system_name);
        fprintf(stderr,"FS Collection ID: %d\n",fs->coll_id);
        fprintf(stderr,"FS Root Handle  : %d\n",fs->root_handle);

        fprintf(stderr,"\t--- Meta Servers for %s (%d total):\n",
                fs->file_system_name,llist_count(fs->meta_server_list));
        cur = fs->meta_server_list;
        while(cur)
        {
            cur_str = llist_head(cur);
            if (!cur_str)
            {
                break;
            }
            fprintf(stderr,"\t  %s\n",cur_str);
            cur = llist_next(cur);
        }

        fprintf(stderr,"\t--- Data Servers for %s (%d total):\n",
                fs->file_system_name,llist_count(fs->data_server_list));
        cur = fs->data_server_list;
        while(cur)
        {
            cur_str = llist_head(cur);
            if (!cur_str)
            {
                break;
            }
            fprintf(stderr,"\t  %s\n",cur_str);
            cur = llist_next(cur);
        }

        fprintf(stderr,"\t--- Handle Mappings for %s:\n",fs->file_system_name);
        cur = fs->handle_ranges;
        while(cur)
        {
            cur_h_mapping = llist_head(cur);
            if (!cur_h_mapping)
            {
                break;
            }
            fprintf(stderr,"\t  %s has handle range %s\n",
                    cur_h_mapping->host_alias,cur_h_mapping->handle_range);
            cur = llist_next(cur);
        }
    }
}

int main(int argc, char **argv)
{
    struct llist *cur = NULL;
    struct server_configuration_s serverconfig;
    struct host_alias_s *cur_alias;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (argc != 3)
    {
        printf("Usage: %s <fs.conf> <server.conf>\n",argv[0]);
        return 1;
    }

    gossip_enable_stderr();

    memset(&serverconfig,0,sizeof(serverconfig));
    if (PINT_server_config(&serverconfig, argv[1], argv[2]))
    {
        printf("Failed to parse config files\n");
        return 1;
    }

    /* dump all gathered config values */
    fprintf(stderr,"--- Printing filesystem configuration\n\n");

    fprintf(stderr,"server id                : %s\n",
            serverconfig.host_id);
    fprintf(stderr,"storage space            : %s\n",
            serverconfig.storage_path);
    fprintf(stderr,"fs config file name      : %s\n",
            serverconfig.fs_config_filename);
    fprintf(stderr,"fs config file length    : %d\n",
            (int)serverconfig.fs_config_buflen);
    fprintf(stderr,"server config file name  : %s\n",
            serverconfig.server_config_filename);
    fprintf(stderr,"server config file length: %d\n",
            (int)serverconfig.server_config_buflen);
    fprintf(stderr,"initial unexp requests   : %d\n",
            serverconfig.initial_unexpected_requests);

    fprintf(stderr,"\n--- Host Aliases:\n");
    cur = serverconfig.host_aliases;
    while(cur)
    {
        cur_alias = llist_head(cur);
        if (!cur_alias)
        {
            break;
        }

        fprintf(stderr,"  %s maps to %s\n",
                cur_alias->host_alias,
                cur_alias->bmi_address);
        cur = llist_next(cur);
    }

    cur = serverconfig.file_systems;
    while(cur)
    {
        cur_fs = llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        print_filesystem_configuration(cur_fs);
        cur = llist_next(cur);
    }

    fprintf(stderr,"\n--- Analyzing filesystem configuration\n\n");
    if (PINT_server_config_is_valid_configuration(&serverconfig))
    {
        fprintf(stderr,"\nOK: File system is VALID\n");
    }
    fprintf(stderr,"\nERROR: File system is INVALID\n");

    PINT_server_config_release(&serverconfig);

    gossip_disable();
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
