/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include "mpi.h"
#include "pts.h"

#define ATTR_UID 1
#define ATTR_GID 2
#define ATTR_PERM 4
#define ATTR_ATIME 8
#define ATTR_CTIME 16
#define ATTR_MTIME 32
#define ATTR_TYPE 2048

extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);
int compare_attribs(PVFS_object_attr attr1, PVFS_object_attr attr2);

/* files, directories, tree of directories */
int create_file(PVFS_fs_id fs_id, char *dirname, char *filename)
{

	PVFS_sysreq_lookup req_look;
	PVFS_sysresp_lookup resp_look;
	PVFS_sysreq_create *req_create = NULL;
	PVFS_sysresp_create *resp_create = NULL;
	PVFS_sysreq_getattr req_getattr;
	PVFS_sysresp_getattr resp_getattr;
	int ret;

	/* lookup the root handle */
	req_look.credentials.perms = 1877;
	req_look.name = strdup(dirname);
	req_look.fs_id = fs_id;
	ret = PVFS_sys_lookup(&req_look,&resp_look);
	if (ret < 0)
	{
		printf("Lookup failed with errcode = %d\n", ret);
		return(-1);
	}
	free(req_look.name);

	/* test create */
	req_create = (PVFS_sysreq_create *)malloc(sizeof(PVFS_sysreq_create));
	if (!req_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	resp_create = (PVFS_sysresp_create *)malloc(sizeof(PVFS_sysresp_create));
	if (!resp_create)
	{
		printf("Error in malloc\n");
		return(-1);
	}

	// Fill in the create info 
	req_create->entry_name = strdup(filename);
	if (!req_create->entry_name)
	{
		printf("Error in malloc\n");
		return(-1);
	}
	req_create->attrmask = (ATTR_UID | ATTR_GID | ATTR_PERM);
	req_create->attr.owner = 100;
	req_create->attr.group = 100;
	req_create->attr.perms = 1877;
	req_create->attr.atime = req_create->attr.mtime = req_create->attr.ctime = 0xdeadbeef;

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

	req_getattr.pinode_refn.handle = resp_create->pinode_refn.handle;
	req_getattr.pinode_refn.fs_id = fs_id;
	req_getattr.attrmask = ATTR_META;
	/* passing this stuff to the server at one point would make it
	 * segfault: it's not a propper getattr request, but segfaulting is a
	 * bit extreme */
#if 0
	req_getattr.credentials.uid = 100;
	req_getattr.credentials.gid = 100;
	req_getattr.credentials.perms = 1877;
#endif
	ret = PVFS_sys_getattr(&req_getattr, &resp_getattr);
	if (ret < 0)
	{
		printf("getattr failed with errcode = %d\n", ret);
		return(-1);
	}
	ret = compare_attribs(req_create->attr, resp_getattr.attr);
	if (ret < 0)
	{
		printf("file created has incorrect attributes\n");
		return -1;
	}
	free(req_create->entry_name);
	free(req_create);
	free(resp_create);
	return 0;
}
/*
 * compare members of two attributes structures
 * 	currently reports verbosely what members differ, but perhaps we should
 * 	do that based on a 'verbose' option 
 * attr1, attr2:  the PVFS_object_attr structures to compare
 * returns:	
 * 	0 	if equivalent	
 * 	-1 	if we found a difference
 */
int compare_attribs(PVFS_object_attr attr1, PVFS_object_attr attr2) 
{
	int i;

	if (attr1.owner != attr2.owner) {
		printf("compare_attribs: owner differs\n");
		return -1;
	}
	if (attr1.group != attr2.group) {
		printf("compare_attribs: group differs\n");
		return -1;
	}
	if (attr1.perms != attr2.perms) {
		printf("compare_attribs: perms differs\n");
		return -1;
	}
	if (attr1.atime != attr2.atime) {
		printf("compare_attribs: atime differs\n");
		return -1;
	}
	if (attr1.mtime != attr2.mtime) {
		printf("compare_attribs: mtime differs\n");
		return -1;
	}
	if (attr1.ctime != attr2.ctime) {
		printf("compare_attribs: ctime differs\n");
		return -1;
	}
	/* does it make sense to compare  the following attributes? objtype,
	 * for example, doesn't get set by the caller */
#if 0
	if (attr1.objtype != attr2.objtype) {
		printf("compare_attribs: objtype differs\n");
		return -1;
	}
	/* TODO: i know these are going to be metafiles, but if this test is to
	 * be a generic attribute compare, it should switch based on 'objtype'
	 */
	/* what metafile attributes are worth comparing? */
	if (attr1.u.meta.dist != attr2.u.meta.dist) {
		printf("compare_attribs: dist info differs\n");
		return -1;
	}
	if (attr1.u.meta.nr_datafiles != attr2.u.meta.nr_datafiles) {
		printf("compare_attribs: nr_datafiles differ\n");
		return -1;
	}
	if (attr1.u.meta.dist_size != attr2.u.meta.dist_size) {
		printf("compare_attribs: dist_size differ\n");
		return -1;
	}
	for (i=0; i<attr1.u.meta.nr_datafiles; i++) {
		if (attr1.u.meta.dfh[i] != attr2.u.meta.dfh[i]) {
			printf("compare_attribs: dfh[%d] differs\n", i);
			return -1;
		}
	}
#endif
	/* TODO: doesn't compare extended attributes */
	return 0;
}

int test_create(MPI_Comm *comm, int rank, char *buf, void *params)
{
	PVFS_sysresp_init resp_init;
	int ret = -1;
	pvfs_mntlist mnt = {0,NULL};
	generic_params *myparams = (generic_params *)params;
	char name[PVFS_NAME_MAX];
	int i, nerrs=0;

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

	for (i=0; i<myparams->mode; i++) {
		snprintf(name, PVFS_NAME_MAX, "/%d-%d-testfile", i, rank);
		nerrs += create_file(resp_init.fsid_list[0],  myparams->path, name);
	}

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return(nerrs);
}
