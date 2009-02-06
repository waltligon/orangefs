/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* 
 * This file provides macro definitions that can help ease the 
 * transition to the new function prototypes introduced in 
 * version 2.7.1
 */

#ifndef PVFS2_COMPAT_H
#define PVFS2_COMPAT_H

#include "pvfs2.h"
#include "pvfs2-sysint.h"

#if PVFS2_VERSION_MAJOR == 2 && PVFS2_VERSION_MINOR > 7

#define PVFS_sys_create(entry_name,ref,attr,credentials,dist,resp)\
   PVFS_sys_create(entry_name,ref,attr,credentials,dist,resp,\
         PVFS_SYS_LAYOUT_DEFAULT,PVFS_HINT_NULL)

#define PVFS_sys_lookup(fs_id,name,credentials,resp,follow_link)\
   PVFS_sys_lookup(fs_id,name,credentials,resp,follow_link,PVFS_HINT_NULL)


#define PVFS_sys_getattr(ref,attrmask,credentials,resp) \
   PVFS_sys_getattr(ref,attrmask,credentials,resp,PVFS_HINT_NULL)

#define PVFS_sys_setattr(ref,attr,credentials) \
   PVFS_sys_setattr(ref,attr,credentials,PVFS_HINT_NULL)

#define PVFS_sys_ref_lookup(fs_id,pathname,parent_ref,\
      credentials,resp, follow_link) \
   PVFS_sys_ref_lookup(fs_id,pathname,parent_ref,credentials,resp,\
         follow_link, PVFS_HINT_NULL)

#undef PVFS_sys_read
#define PVFS_sys_read(ref,req,off,buf,mem_req,creds,resp) \
   PVFS_sys_io(ref,req,off,buf,mem_req,creds,resp,PVFS_IO_READ,PVFS_HINT_NULL)

#undef PVFS_sys_write
#define PVFS_sys_write(ref,req,off,buf,mem_req,creds,resp) \
   PVFS_sys_io(ref,req,off,buf,mem_req,creds,resp,PVFS_IO_WRITE,PVFS_HINT_NULL)

#undef PVFS_isys_read
#define PVFS_isys_read(ref,req,off,buf,mem_req,creds,resp,opid,ptr)\
	PVFS_isys_io(ref,req,off,buf,mem_req,creds,resp,PVFS_IO_READ,opid,PVFS_HINT_NULL,ptr)

#undef PVFS_isys_write
#define PVFS_isys_write(ref,req,off,buf,mem_req,creds,resp,opid,ptr)\
	PVFS_isys_io(ref,req,off,buf,mem_req,creds,resp,PVFS_IO_WRITE,opid,PVFS_HINT_NULL,ptr)

#define PVFS_sys_remove(entry,ref,creds)\
   PVFS_sys_remove(entry,ref,creds,PVFS_HINT_NULL)

#define PVFS_sys_mkdir(entry,ref,attr,creds,resp)\
   PVFS_sys_mkdir(entry,ref,attr,creds,resp,PVFS_HINT_NULL)

#define PVFS_sys_readdir(ref,token,count,creds,resp)\
   PVFS_sys_readdir(ref,token,count,creds,resp,PVFS_HINT_NULL)

#define PVFS_sys_truncate(ref,size,creds)\
   PVFS_sys_truncate(ref,size,creds,PVFS_HINT_NULL)

#define PVFS_sys_getparent(ref,name,cred,resp) \
	PVFS_sys_getparent(ref,name,cred,resp,PVFS_HINT_NULL)

#define PVFS_sys_flush(ref,cred)\
	PVFS_sys_flush(ref,cred,PVFS_HINT_NULL)

#define PVFS_sys_symlink(tof,ref,from,attr,cred,resp) \
        PVFS_sys_symlink(tof,ref,from,attr,cred,resp,PVFS_HINT_NULL)

#define PVFS_sys_rename(from,fref,to,tref,cred) \
        PVFS_sys_rename(from,fref,to,tref,cred,PVFS_HINT_NULL)

#define PVFS_sys_statfs(fsid,creds,resp) \
        PVFS_sys_statfs(fsid,creds,resp,PVFS_HINT_NULL)
#endif


#endif
