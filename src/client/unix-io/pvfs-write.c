/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * PVFS User Library implementation for write 
 *
 */

#include <pint-userlib.h>
#include <pvfs2-userlib.h>

int pvfs_write(int fd, char *buf, size_t count)
{
	int32_t chk_fd = 0;
	int tmp_fd = 0;
	fdesc_p fd_p;
	PVFS_sysreq_getattr req_gattr;
	PVFS_sysresp_getattr resp_gattr;
	PVFS_sysreq_setattr req_sattr;
	PVFS_sysresp_setattr resp_sattr;
	PVFS_sysreq_write req_write;
	PVFS_sysresp_write resp_write;

	/* From fd figure out if its a PVFS file */
	tmp_fd = fd & (1 << ((sizeof(int) * 8) - 1));

	/* Is fd valid? */
	if (tmp_fd < 0 || tmp_fd >= PVFS_NR_OPEN) {
	{
		printf("PVFS_write:Invalid fd\n");
		return(-1);
	}  

	/* Unix file write */
	if (tmp_fd == 0) 
		return(unix_write(fd, buf, count));

	/* PVFS file write */
	ret = get_desc_info(&fd_p,fd);
	if (ret < 0)
	{
		printf("PVFS_write:Error in getting descriptor info\n");
		return(-1);
	}
	/* PVFS directory write- what to do here? */
	if (fd_p->fs == FS_PDIR) 
		return(unix_write(fd, buf, count));

	/* Do permission checks */
	uid = getuid();
	gid = getgid();
	mode_bits |= 2; /* Interested only in write */
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
		printf("PVFS_write:Permission error\n");
		return(ret);
	}

	start_off = fd_p->off;

	/* Get distribution */
	req_gattr.pinode_no.handle = fd_p->handle;	
	req_gattr.attrmask = ATTR_UID + ATTR_GID + ATTR_TYPE + ATTR_META;
	ret = PVFS_sys_getattr(&req_gattr,&resp_gattr);
	if (ret < 0)
	{
		printf("PVFS_write:Getattr failed\n");
		return(-1);
	}
	/* TODO:*/
	/* Extract the distribution and fill it in write request */

	/* Make write request */
	req_write.pinode_no.handle = fd_p->handle;
	req_write.pinode_no.collid = fd_p->collid;
	/* Need to fill in IO desc */
	ret = PVFS_sys_write(&req_write,&resp_write);
	if (ret < 0)
	{
		printf("PVFS_write:Read failed\n");
		return(-1);
	}
	size = ret;

	/* Modify times for the file? */
	req_sattr.pinode_no.handle = fd_p->handle;
	req_sattr.attrmask = ATTR_MTIME + ATTR_ATIME;
	req_sattr.attr.atime = time(NULL);
	req_sattr.attr.mtime = time(NULL);

	/* update modification time meta data */
	pfd_p->fd.meta.u_stat.st_mtime = time(NULL);
	fd_p->fd.off += size;
	ret = set_desc_info(&fd_p,fd);
	if (ret < 0)
	{
		printf("PVFS_write:Error in setting descriptor info\n");
		return(-1);
	}

	return(size);
}
