#include <stdio.h>
#include <string.h>
#include "trove.h"
#include "pvfs2-storage.h"
#include "dotconf.h"
#include "server-config.h"


void print_filesystem_configuration(struct filesystem_configuration_s *fs)
{
    struct llist *cur = NULL;
    char *cur_str = (char *)0;
    struct host_bucket_mapping_s *cur_b_mapping = NULL;

    if (fs)
    {
        fprintf(stderr,"File system name: %s\n",fs->file_system_name);

        fprintf(stderr,"\t*** Meta Servers for %s:\n",fs->file_system_name);
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

        fprintf(stderr,"\t*** Data Servers for %s:\n",fs->file_system_name);
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

        fprintf(stderr,"\t*** Bucket Mappings for %s:\n",fs->file_system_name);
        cur = fs->bucket_ranges;
        while(cur)
        {
            cur_b_mapping = llist_head(cur);
            if (!cur_b_mapping)
            {
                break;
            }
            fprintf(stderr,"\t  %s has bucket range %s\n",
                    cur_b_mapping->host_alias,cur_b_mapping->bucket_range);
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
    if (PINT_server_config(&serverconfig,argc,argv))
    {
        printf("Failed to parse config file\n");
        return 1;
    }

    /* dump all gathered config values */
    fprintf(stderr,"server id: %s\n",serverconfig.host_id);
    fprintf(stderr,"storage space: %s\n",serverconfig.storage_path);
    fprintf(stderr,"unexp req: %d\n",
            serverconfig.initial_unexpected_requests);

    fprintf(stderr,"*** Host Aliases:\n");
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
