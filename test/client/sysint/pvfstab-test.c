/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pvfs2-sysint.h>
#include <pint-sysint.h>

void print_mount_entry(pvfs_mntent *mount);
void print_mount_list(pvfs_mntlist *mnt_list);

int main(int argc, char* argv[])
{
    pvfs_mntlist mnt;
    int ret;

    gossip_enable_stderr();
    gossip_set_debug_mask(1,CLIENT_DEBUG);

    ret = parse_pvfstab(argv[1],&mnt);
    if (ret < 0)
    {
	printf("Parsing error\n");
	return(-1);
    }

    print_mount_list(&mnt);

    gossip_disable();

    return(0);
}

void print_mount_list(pvfs_mntlist *mnt_list)
{
    int i;
    printf("Total Number of entries: %d\n",mnt_list->nr_entry);
    for(i = 0; i < mnt_list->nr_entry; i++)
    {
	print_mount_entry(&mnt_list->ptab_p[i]);
    }
}

void print_mount_entry(pvfs_mntent *mount)
{
    printf("\tServer address: %s\n",mount->meta_addr);
    printf("\tServce name: %s\n",mount->service_name);
    printf("\tLocal mount dir: %s\n",mount->local_mnt_dir);
    printf("\tFilesystem Type: %s\n",mount->fs_type);
    printf("\tOption1: %s\n",mount->opt1);
    printf("\tOption2: %s\n",mount->opt2);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

