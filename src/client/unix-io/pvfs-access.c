/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* PVFS library Access Implementation */

/* It will determine if file is UNIX or PVFS and then 		
 * make the proper request.
 */

//#include <pvfs2-userlib.h>
#include "pint-userlib.h"

/* Variable declarations */
//extern int errno;

/* Function Prototypes */
int unix_access(char *path, int mode);

/* pvfs_access
 *
 * Checks whether a file can be accessed in a specified
 * mode 
 *
 * returns 0 on success, -1 on error
 */
int pvfs_access(char* pathname, int mode)
{
	int gid = 0, ret = 0, fs = 0, uid = 0, len = 0; 		
	unsigned char mode_bits = 0;
	PVFS_sysreq_lookup req_lkp;
	PVFS_sysresp_lookup resp_lkp;
	PVFS_sysreq_getattr req_gattr;
	PVFS_sysresp_getattr resp_gattr;
	char *fname = NULL;
	PVFS_fs_id collid = 0;

	if (!pathname)
	{
		errno = EFAULT;
		return(-1);
	}

	/* Determine if file is a PVFS file */
	ret = pvfs_getfsid(pathname,&fs,&fname,&collid);
	if (ret < 0)
	{
		return(-EINVAL);
	}
	/* If UNIX file, call corresponding UNIX command */
	if (fs == FS_UNIX)   
		return access(pathname, mode);
	

	/* Need to figure out what mode is? Then call appropriate
	 * system interface call
	 */
	/* Mode consists of 
	 * R_OK, W_OK, X_OK, and F_OK 
	 */
	/* Look up mgr/mgr.c, do_access */
	uid = getuid();
	gid = getgid();
	/* Lookup to get handle */
	/* TODO: System Interface functions need fsid as a parameter.
	 */ 
	len = strlen(fname);
	req_lkp.name = (char *)malloc(len + 1);
	if (!req_lkp.name)
	{
		printf("PVFS_access:error in malloc\n");
		return(-1);
	}
	strncpy(req_lkp.name,fname,len);
	req_lkp.name[len] = '\0';
	req_lkp.fs_id = collid;
	/* TODO: Fill credentials structure */
	req_lkp.credentials.uid = uid;
	req_lkp.credentials.gid = gid;
	req_lkp.credentials.perms = mode;
	ret = PVFS_sys_lookup(&req_lkp,&resp_lkp);
	if (ret < 0)
	{
		printf("PVFS_access:Lookup failed\n");
		return(-1);
	}
	/* Get the attributes */
	req_gattr.pinode_refn.handle = resp_lkp.pinode_refn.handle;
	req_gattr.pinode_refn.fs_id = collid;
	req_gattr.attrmask = ATTR_BASIC;
	req_gattr.credentials.uid = uid;
	req_gattr.credentials.gid = gid;
	req_gattr.credentials.perms = mode;
	ret = PVFS_sys_getattr(&req_gattr,&resp_gattr);
	if (ret < 0)
	{
		printf("PVFS_access:Getattr failed\n");
		return(-1);
	}
	
	/* Match attributes */
	/* */
	if (mode & R_OK)
		mode_bits |= 4;
	if (mode & W_OK)
		 mode_bits |= 2;
	if (mode & X_OK)
		mode_bits |= 1;
	/* Check o,g,u in that order */
	if (((resp_gattr.attr.perms & 7) & mode_bits) == mode_bits)
		ret = 0;
	else if (((resp_gattr.attr.group == gid && (resp_gattr.attr.perms >> 3) & 7)
				& mode_bits) == mode_bits)
		ret = 0;
	else if (((resp_gattr.attr.owner == uid && (resp_gattr.attr.perms >> 6) & 7)
				& mode_bits) == mode_bits)
		ret = 0;
	else
		ret = -1;

	return(ret);
	
}
