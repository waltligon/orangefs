/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* PVFS Library Implementation for open */

#include <pint-userlib.h>
#include <pvfs2-userlib.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

#ifndef PVFS_NR_OPEN
#define PVFS_NR_OPEN (NR_OPEN)
#endif


int pvfs_open64(const char* pathname, int flag, mode_t mode,
	void *ptr1, void *ptr2)
{
	return(pvfs_open(pathname, flag, mode | O_LARGEFILE, ptr1, ptr2));
}

/* pvfs_open() - PVFS open command.
 * pathname - file to be opened
 * flag - flags (O_RDONLY, etc.) to be used in open
 * mode - if creating file, mode to be used
 * ptr1,ptr2,ptr3,ptr4 - these should point to attr,attrmask,dist and 
 * partition information.
 * ------------------Info below is deprecated-----------------
 * ptr1, ptr2 - these point to metadata and partition information,
 *    if provided.  The flags (O_META and O_PART) must be set if one
 *    is to be used, and if both metadata and partition information
 *    are provided then the metadata pointer MUST PRECEDE the partition
 *    information.
 */
int pvfs_open(const char* pathname, int flag, mode_t mode)
{
	int i, fd = 0;
	PVFS_obj_attr attr;
	fdesc tmp_fdesc;
	int fs = 0;
	PVFS_sysreq_lookup req_lkp;
	PVFS_sysresp_lookup resp_lkp;
	PVFS_sysreq_getattr req_gattr;
	PVFS_sysresp_getattr resp_gattr;
	PVFS_sysreq_create req_create;
	PVFS_sysresp_create resp_create;
	finfo info;					/* File info--name,collid */

	/* Check pathname */
	if (!pathname) 
	{
		printf("PVFS_OPEN:Invalid pathname\n");
		return(-EINVAL);
	}

	/* Check to see if file is a PVFS file */
	ret = pvfs_getfsid(pathname,&fs,&info);
	if (ret < 0)
	{
		printf("PVFS_OPEN:Getfsid failed\n");
		return(-EINVAL);
	}

	/* UNIX file open */
	if (fs = FS_UNIX)
	{
		if ((fd = open(pathname,flag, mode)) < 0)
			return(fd);
		memset(&tmp_fdesc,0,sizeof(fdesc));
		/* Stash the fd in the open file table */
		/* No need to do this !!! */
		tmp_fdesc.fd = fd;
		tmp_fdesc.flag = flag;
		tmp_fdesc.mode = mode;
		ret = set_desc_info(&tmp_fdesc,&no);
		if (ret < 0)
		{
			printf("PVFS_OPEN:Error in setting descriptor info\n");
			return(ret);
		}
		return(fd);

	}

	/* Lookup to get handle */
	/* System Interface functions need Collection ID as a parameter.
	 * Needs to be done
	 */ 
	req_lkp->name = (char *)malloc(strlen(info.fname) + 1);
	if (!req_lkp->name)
	{
		printf("PVFS_open:error in malloc\n");
		return(-1);
	}
	strncpy(req_lkp->name,info.fname,strlen(info.fname));
	req_lkp->collid = info.collid;
	ret = PVFS_sys_lookup(&req_lkp,&resp_lkp);
	if (ret == -ENOENT)
	{
		/* File does not exist */
		if (!(flags & O_CREAT))
		{
			printf("PVFS_open:File does not exist\n");
			return(-1);
		}
		file_create = 1; /* Create required */

	}
	else if (ret < 0)
	{
		printf("PVFS_open:lookup failed\n");
		return(-1);
	}

	/* PVFS filesystem object exists */
	/* Disallow write operations on dirs */
	req_gattr.pinode_no.handle = resp_lkp.pinode_no.handle;
	req_gattr.attrmask = ATTR_UID + ATTR_GID + ATTR_TYPE;
	ret = PVFS_sys_getattr(&req_gattr,&resp_gattr);
	if (ret < 0)
	{
		printf("PVFS_open:Getattr failed\n");
		return(-1);
	}
	if (resp_gattr.attr.objtype == PVFS_DIR)
	{
		if (flag & O_WRONLY|O_RDWR)
		{
			printf("PVFS_open:Can't write to a directory\n");
			return(-1);
		}
	}

	/* If file does not exist do a create */
	/* call it with a null distribution */
	req_create->name = (char *)malloc(strlen(info.fname) + 1);
	if (!req_create->name)
	{
		printf("PVFS_open:error in malloc\n");
		return(-1);
	}
	strncpy(req_create->name,info.fname,strlen(info.fname));
	req_create.attr.gid = getgid();
	req_create.attr.uid = getuid();
	req_create.attr = 0;
	req_create.attrmask = 0;
	memset(&req_create.dist,0,sizeof(PVFS_dist));
	ret = PVFS_sys_create(&req_create,&resp_create);
	if (ret < 0)
	{
		printf("PVFS_open:Error in create\n");
		return(-1);
	}
	/* Stash the fd in the open file table */
	tmp_fdesc.fd = resp_create.pinode_no.handle;
	fd = tmp_fdesc.fd;
	tmp_fdesc.flag = flag;
	tmp_fdesc.mode = mode;
	ret = set_desc_info(&tmp_fdesc,&no);
	if (ret < 0)
	{
		printf("PVFS_OPEN:Error in setting descriptor info\n");
		return(ret);
	}
	/* Move to end of file (if necessary) */
	if (flag & O_APPEND)
		pvfs_lseek(tmp_fdesc.fd,0,SEEK_END);

	return(fd);
}

/* pvfs_creat
 *
 * creates a PVFS file
 *
 * returns fd on success, -1 on error
 */
int pvfs_creat(const char* pathname, mode_t mode)
{
	int flags;
	
	/* call pvfs_open with correct flags            */
	flags  =  O_CREAT|O_WRONLY|O_TRUNC;
	return(pvfs_open(pathname, flags, mode, NULL, NULL)); 
}

