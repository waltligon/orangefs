/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>

extern int PINT_get_base_dir(char *pathname,
                             char *out_base_dir, int out_max_len);
extern int PINT_remove_base_dir(char *pathname,
                                char *out_dir, int out_max_len);

/*why were these commented out?*/

#define ATTR_UID 1
#define ATTR_GID 2
#define ATTR_PERM 4
#define ATTR_ATIME 8
#define ATTR_CTIME 16
#define ATTR_MTIME 32
#define ATTR_TYPE 2048

void gen_rand_str(int len, char** gen_str);
extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

int main(int argc,char **argv)
{
	PVFS_sysresp_init resp_init;
	PVFS_sysreq_lookup req_look;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysreq_create req_create;
	PVFS_sysresp_create resp_create;
	char *filename = (char *)0;
	char str_buf[256] = {0};
	int ret = -1;
	pvfs_mntlist mnt = {0,NULL};

        if (argc != 2)
        {
            fprintf(stderr,"Usage: %s filename\n",argv[0]);
            return ret;
        }

	memset(&resp_init, 0, sizeof(resp_init));
	memset(&req_look, 0, sizeof(req_look));
	memset(&resp_look, 0, sizeof(resp_look));

        filename = argv[1];
        if (PINT_get_base_dir(filename,str_buf,256))
        {
            if (filename[0] != '/')
            {
                printf("You forgot to use a leading '/'; invalid dirname\n");
            }
            printf("Cannot get parent directory of %s\n",filename);
            return ret;
        }
	printf("===>Creating a file named %s in directory %s\n",
               filename, str_buf);

	/* Parse PVFStab */
	ret = parse_pvfstab(NULL,&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}

	/* lookup the root handle */
	req_look.name = str_buf;
	req_look.fs_id = resp_init.fsid_list[0];
	req_look.credentials.uid = 100;
	req_look.credentials.gid = 100;
	req_look.credentials.perms = 1877;
	printf("looking up the parent handle of %s for fsid = %d\n",
               str_buf,req_look.fs_id);
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("starting Handle:%ld\n", (long int)resp_look.pinode_refn.handle);
	
	memset(&req_create, 0, sizeof(PVFS_sysreq_create));
	memset(&resp_create, 0, sizeof(PVFS_sysresp_create));

        if (PINT_remove_base_dir(filename,str_buf,256))
        {
		printf("Cannot retrieve entry name for creation\n");
		return(-1);
        }
        printf("File to be created is %s\n",str_buf);

	// Fill in the create info 
	req_create.entry_name = str_buf;
	req_create.attrmask = (ATTR_UID | ATTR_GID | ATTR_PERM);
	req_create.attr.owner = 100;
	req_create.attr.group = 100;
	req_create.attr.perms = 1877;
	req_create.credentials.uid = 100;
	req_create.credentials.gid = 100;
	req_create.credentials.perms = 1877;
	req_create.attr.u.meta.nr_datafiles = 4;
	req_create.parent_refn.handle = resp_look.pinode_refn.handle;
	req_create.parent_refn.fs_id = req_look.fs_id;

	/* Fill in the dist -- NULL means the system interface used the 
	 * "default_dist" as the default
	 */
	req_create.attr.u.meta.dist = NULL;

	ret = PVFS_sys_create(&req_create,&resp_create);
	if (ret < 0)
	{
		printf("create failed with errcode = %d\n", ret);
		return(-1);
	}
	
	// print the handle 
	printf("--create--\n"); 
	printf("Handle:%Ld\n",resp_create.pinode_refn.handle);

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}
	return(0);
}
