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
	int ret = -1;
	char *dirname = (char *)0;
        char str_buf[256] = {0};
	PVFS_sysresp_init resp_init;
	PVFS_sysreq_lookup req_look;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysreq_mkdir req_mkdir;
	PVFS_sysresp_mkdir resp_mkdir;
	pvfs_mntlist mnt = {0,NULL};

	if (argc != 2)
	{
		gen_rand_str(10,&dirname);
	}
	else
	{
		dirname = argv[1];
	}
	printf("creating a directory named %s\n", dirname);

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

        if (PINT_get_base_dir(dirname,str_buf,256))
        {
		printf("Failed to get parent directory of %s\n",dirname);
		if (dirname[0] != '/')
                {
                    printf("You forgot to use a leading '/'; invalid dirname\n");
                }
		return -1;
        }
        printf("Parent directory is %s\n",str_buf);

	/* lookup the parent directory handle */
	req_look.credentials.uid = 100;
	req_look.credentials.gid = 100;
	req_look.credentials.perms = 1877;
	req_look.name = str_buf;
	req_look.fs_id = resp_init.fsid_list[0];
	printf("looking up the handle for %s on fsid = %d\n",
               str_buf,req_look.fs_id);
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("PARENT Handle:%ld\n", (long int)resp_look.pinode_refn.handle);

	memset(&req_mkdir, 0, sizeof(PVFS_sysreq_mkdir));
	memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));

        if (PINT_remove_base_dir(dirname,str_buf,256))
        {
		printf("Cannot retrieve entry name for creation\n");
		return(-1);
        }
        printf("New Directory component is %s\n",str_buf);

	/* update the pointer to the string that was passed in */
	req_mkdir.entry_name = str_buf;
	req_mkdir.parent_refn.handle = resp_look.pinode_refn.handle;
	req_mkdir.parent_refn.fs_id = resp_look.pinode_refn.fs_id;
	req_mkdir.attrmask = ATTR_BASIC;
	req_mkdir.attr.owner = 100;
	req_mkdir.attr.group = 100;
	req_mkdir.attr.perms = 1877;
	req_mkdir.attr.objtype = ATTR_DIR;
	req_mkdir.credentials.perms = 1877;
	req_mkdir.credentials.uid = 100;
	req_mkdir.credentials.gid = 100;

	// call mkdir 
	ret = PVFS_sys_mkdir(&req_mkdir,&resp_mkdir);
	if (ret < 0)
	{
		printf("mkdir failed\n");
		return(-1);
	}
	// print the handle 
	printf("--mkdir--\n"); 
	printf("Handle:%Ld\n",resp_mkdir.pinode_refn.handle);
	printf("FSID:%d\n",req_mkdir.parent_refn.fs_id);

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}
	return(0);
}

/* generate random filenames cause ddd sucks and doesn't like taking cmd line
 * arguments (and remove doesn't work yet so I can't cleanup the crap I already
 * created)

NOTE: ddd isn't handicapped like this.  Try Program -> Run
and enter something into the 'run with arguments' box.
When you use 'Run Again', you're able to re-use those args.
-N.M.
 */
void gen_rand_str(int len, char** gen_str)
{
	static char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
	int i;
	struct timeval poop;
	int newchar = 0;
	gettimeofday(&poop, NULL);

	*gen_str = malloc(len + 1);
	for(i = 0; i < len; i++)
	{
		newchar = ((1+(rand() % 26)) + poop.tv_usec) % 26;
		(*gen_str)[i] = alphabet[newchar];
	}
	(*gen_str)[len] = '\0';
}
