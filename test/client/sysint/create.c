/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>

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
	PVFS_sysreq_create *req_create = NULL;
	PVFS_sysresp_create *resp_create = NULL;
	char *filename;
	char *starting_point;
	int ret = -1;
	pvfs_mntlist mnt = {0,NULL};
	int name_sz;

	memset(&resp_init, 0, sizeof(resp_init));
	memset(&req_look, 0, sizeof(req_look));
	memset(&resp_look, 0, sizeof(resp_look));

	switch(argc)
	{
		case 3:
			name_sz = strlen(argv[2]) + 1; /*include null terminator*/
			filename = malloc(name_sz);
			memcpy(filename, argv[2],name_sz);

			name_sz = strlen(argv[1]) + 1; /*include null terminator*/
			starting_point = malloc(name_sz);
			memcpy(starting_point, argv[1],name_sz);
			break;
		case 2:
			name_sz = strlen(argv[1]) + 1; /*include null terminator*/
			filename = malloc(name_sz);
			memcpy(filename, argv[1],name_sz);

			starting_point = malloc(2);/*null terminator included*/
			starting_point[0] = '/';
			starting_point[1] = '\0';
			break;
		case 1:
			gen_rand_str(10,&filename);

			starting_point = malloc(2);/*null terminator included*/
			starting_point[0] = '/';
			starting_point[1] = '\0';
			break;
		default:
			printf("usage: create <optional_starting_point> name_to_create\n");
			return(-1);
	}

	printf("===>Creating a file named %s in directory %s\n", filename, starting_point);

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
	printf("SYSTEM INTERFACE INITIALIZED\n");

	/* lookup the root handle */
	req_look.credentials.perms = 1877;
	req_look.name = starting_point;
	req_look.fs_id = resp_init.fsid_list[0];
	printf("looking up the root handle for fsid = %d\n", req_look.fs_id);
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	// print the handle 
	printf("--lookup--\n"); 
	printf("starting Handle:%ld\n", (long int)resp_look.pinode_refn.handle);
	

	/* test create */
	req_create = (PVFS_sysreq_create *)malloc(sizeof(PVFS_sysreq_create));
	if (!req_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	memset(req_create, 0, sizeof(PVFS_sysreq_create));

	resp_create = (PVFS_sysresp_create *)malloc(sizeof(PVFS_sysresp_create));
	if (!resp_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	memset(resp_create, 0, sizeof(PVFS_sysresp_create));

	// Fill in the create info 
	req_create->entry_name = (char *)malloc(strlen(filename) + 1);
	if (!req_create->entry_name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	memcpy(req_create->entry_name,filename,strlen(filename) + 1);
	req_create->attrmask = (ATTR_UID | ATTR_GID | ATTR_PERM);
	req_create->attr.owner = 100;
	req_create->attr.group = 100;
	req_create->attr.perms = 1877;

	req_create->credentials.uid = 100;
	req_create->credentials.gid = 100;
	req_create->credentials.perms = 1877;

	req_create->attr.u.meta.nr_datafiles = 4;

	req_create->parent_refn.handle = resp_look.pinode_refn.handle;
	req_create->parent_refn.fs_id = req_look.fs_id;

	
	/* Fill in the dist -- NULL means the system interface used the 
	 * "default_dist" as the default
	 */
	req_create->attr.u.meta.dist = NULL;

	// call create 
	ret = PVFS_sys_create(req_create,resp_create);
	if (ret < 0)
	{
		printf("create failed with errcode = %d\n", ret);
		return(-1);
	}
	
	// print the handle 
	printf("--create--\n"); 
	printf("Handle:%ld\n",(long int)resp_create->pinode_refn.handle);

	free(req_create);
	free(resp_create);

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	free(filename);
	free(starting_point);
	return(0);
}

/* generate random filenames cause ddd sucks and doesn't like taking cmd line
 * arguments (and remove doesn't work yet so I can't cleanup the crap I already
 * created)
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
