/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include <stdio.h>
#include "mpi.h"
#include "pts.h"

extern int parse_pvfstab(char *fn,
			 pvfs_mntlist * mnt);
int compare_attribs(PVFS_sys_attr attr1,
		    PVFS_sys_attr attr2);

/* files, directories, tree of directories */
int create_file(PVFS_fs_id fs_id,
		char *dirname,
		char *filename)
{
    int ret;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_create resp_create;
    PVFS_sysresp_getattr resp_getattr;

    credentials.uid = 100;
    credentials.gid = 100;

    ret = PVFS_sys_lookup(fs_id, dirname, credentials, &resp_look);
    if (ret < 0)
    {
	printf("Lookup failed with errcode = %d\n", ret);
	return (-1);
    }

    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = 100;
    attr.group = 100;
    attr.perms = 1877;
    attr.atime = attr.mtime = attr.ctime = 0xdeadbeef;

    memset(&resp_create,0,sizeof(resp_create));
    ret = PVFS_sys_create(filename, resp_look.pinode_refn,
                          attr, credentials, &resp_create);
    if (ret < 0)
    {
	printf("create failed with errcode = %d\n", ret);
	return (-1);
    }

    ret = PVFS_sys_getattr(resp_create.pinode_refn, attr.mask,
                           credentials, &resp_getattr);
    if (ret < 0)
    {
	printf("getattr failed with errcode = %d\n", ret);
	return (-1);
    }
    ret = compare_attribs(attr, resp_getattr.attr);
    if (ret < 0)
    {
	printf("file created has incorrect attributes\n");
	return -1;
    }
    return 0;
}

/*
 * compare members of two attributes structures
 * 	currently reports verbosely what members differ, but perhaps we should
 * 	do that based on a 'verbose' option 
 * attr1, attr2:  the PVFS_sys_attr structures to compare
 * returns:	
 * 	0 	if equivalent	
 * 	-1 	if we found a difference
 */
int compare_attribs(PVFS_sys_attr attr1,
		    PVFS_sys_attr attr2)
{
    if (attr1.owner != attr2.owner)
    {
	printf("compare_attribs: owner differs\n");
	return -1;
    }
    if (attr1.group != attr2.group)
    {
	printf("compare_attribs: group differs\n");
	return -1;
    }
    if (attr1.perms != attr2.perms)
    {
	printf("compare_attribs: perms differs\n");
	return -1;
    }
    if (attr1.atime != attr2.atime)
    {
	printf("compare_attribs: atime differs\n");
	return -1;
    }
    if (attr1.mtime != attr2.mtime)
    {
	printf("compare_attribs: mtime differs\n");
	return -1;
    }
    if (attr1.ctime != attr2.ctime)
    {
	printf("compare_attribs: ctime differs\n");
	return -1;
    }
    /* does it make sense to compare  the following attributes? objtype,
     * for example, doesn't get set by the caller */
#if 0
    if (attr1.objtype != attr2.objtype)
    {
	printf("compare_attribs: objtype differs\n");
	return -1;
    }
    /* TODO: i know these are going to be metafiles, but if this test is to
     * be a generic attribute compare, it should switch based on 'objtype'
     */
    /* what metafile attributes are worth comparing? */
    if (attr1.u.meta.dist != attr2.u.meta.dist)
    {
	printf("compare_attribs: dist info differs\n");
	return -1;
    }
    if (attr1.u.meta.nr_datafiles != attr2.u.meta.nr_datafiles)
    {
	printf("compare_attribs: nr_datafiles differ\n");
	return -1;
    }
    if (attr1.u.meta.dist_size != attr2.u.meta.dist_size)
    {
	printf("compare_attribs: dist_size differ\n");
	return -1;
    }
    for (i = 0; i < attr1.u.meta.nr_datafiles; i++)
    {
	if (attr1.u.meta.dfh[i] != attr2.u.meta.dfh[i])
	{
	    printf("compare_attribs: dfh[%d] differs\n", i);
	    return -1;
	}
    }
#endif
    /* TODO: doesn't compare extended attributes */
    return 0;
}

int test_create(MPI_Comm * comm,
		int rank,
		char *buf,
		void *params)
{
    PVFS_sysresp_init resp_init;
    int ret = -1;
    pvfs_mntlist mnt = { 0, NULL };
    generic_params *myparams = (generic_params *) params;
    char name[PVFS_NAME_MAX];
    int i, nerrs = 0;

    /* Parse PVFStab */
    ret = parse_pvfstab(NULL, &mnt);
    if (ret < 0)
    {
	printf("Parsing error\n");
	return (-1);
    }

    /*Init the system interface */
    ret = PVFS_sys_initialize(mnt, &resp_init);
    if (ret < 0)
    {
	printf("PVFS_sys_initialize() failure. = %d\n", ret);
	return (ret);
    }

    for (i = 0; i < myparams->mode; i++)
    {
	snprintf(name, PVFS_NAME_MAX, "%d-%d-testfile", i, rank);
	nerrs += create_file(resp_init.fsid_list[0], myparams->path, name);
    }

    //close it down
    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	printf("finalizing sysint failed with errcode = %d\n", ret);
	return (-1);
    }

    return (nerrs);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
