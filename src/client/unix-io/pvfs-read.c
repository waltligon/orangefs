/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/* PVFS User Library Implementation for read
 */

#include <pint-userlib.h>
#include <pvfs2-userlib.h>

int pvfs_read(int fd, char *buf, size_t count)
{
	int32_t chk_fd = 0;
	fdesc_p fd_p;
	PVFS_sysreq_getattr req_gattr;
	PVFS_sysresp_getattr resp_gattr;
	PVFS_sysreq_setattr req_sattr;
	PVFS_sysresp_setattr resp_sattr;
	PVFS_sysreq_read req_read;
	PVFS_sysresp_read resp_read;

	/* From fd figure out if its a PVFS file */
	tmp_fd = fd & (1 << ((sizeof(int) * 8) - 1)) ;
	
	/* Is fd valid?  */
	if (tmp_fd < 0 || tmp_fd >= PVFS_NR_OPEN) 
	{
		printf("PVFS_read:Invalid fd\n");	
		return(-1);
	}  

	/* Unix file read */
	if (tmp_fd == 0)
		return(unix_read(fd, buf, count));

	/* PVFS file read */
	ret = get_desc_info(&fd_p,fd);
	if (ret < 0) {
		printf("PVFS_read:Error in getting descriptor info\n");
		return(-1);
	}

	/* Do permission checks */
	uid = getuid();
	gid = getgid();
	mode_bits |= 4; /* Interested only in read */
	/* Check o,g,u in that order */
	if (((fd_p.attr.perms & 7) & mode_bits) == mode_bits)
		ret = 0;
	else if (((fd_p.attr.gid == gid && (fd_p.attr.perms >> 3) & 7)
				& mode_bits) == mode_bits)
		ret = 0;
	else if (((resp_gattr.attr.uid == uid && (fd_p.attr.perms >> 6) & 7)
				& mode_bits) == mode_bits)
		ret = 0;
	else
		ret = -1;

	if (ret)
	{
		printf("PVFS_read:Permission error\n");
		return(ret);
	}

	start_off = fd_p.off; /* current offset */

	/* Get distribution */
	req_gattr.pinode_no.handle = fd_p->handle;	
	req_gattr.attrmask = ATTR_UID + ATTR_GID + ATTR_TYPE + ATTR_META;
	ret = PVFS_sys_getattr(&req_gattr,&resp_gattr);
	if (ret < 0)
	{
		printf("PVFS_read:Getattr failed\n");
		return(-1);
	}

	/* Make read request */
	req_read.pinode_no.handle = fd_p->handle;
	req_read.pinode_no.collid = fd_p->collid;
	/* Need to fill in IO desc */
	ret = PVFS_sys_read(&req_read,&resp_read);
	if (ret < 0)
	{
		printf("PVFS_read:Read failed\n");
		return(-1);
	}
	size = ret;

	/* Modify access time for the file? */
	req_sattr.pinode_no.handle = fd_p->handle;	
	req_sattr.attrmask = ATTR_ATIME;
	req_sattr.attr.atime = time(NULL);
	ret = PVFS_sys_setattr(&req_sattr,&resp_sattr);
	if (ret < 0)
	{
		printf("PVFS_read:Setattr failed\n");
		return(-1);
	}

	/* Update the offset and write back */
	fd_p->off = start_off + ret;
	ret = set_desc_info(&fd_p,fd);
	if (ret < 0)
	{
		printf("PVFS_read:Error in setting descriptor info\n");
		return(-1);
	}

	return(size);
}

