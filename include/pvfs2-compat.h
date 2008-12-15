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

#if PVFS2_VERSION_MAJOR == 2 && PVFS2_VERSION_MINOR == 7 && PVFS2_VERSION_SUB >= 1

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

#define PVFS_sys_io(ref,req,req_ofs,buf,mem_req,creds,resp,type)\
   PVFS_sys_io(ref,req,req_ofs,buf,mem_req,creds,resp,type,PVFS_HINT_NULL)

#define PVFS_sys_remove(entry,ref,creds)\
   PVFS_sys_remove(entry,ref,creds,PVFS_HINT_NULL)

#define PVFS_sys_mkdir(entry,ref,attr,creds,resp)\
   PVFS_sys_mkdir(entry,ref,attr,creds,resp,PVFS_HINT_NULL)

#define PVFS_sys_readdir(ref,token,count,creds,resp)\
   PVFS_sys_readdir(ref,token,count,creds,resp,PVFS_HINT_NULL)

#define PVFS_sys_truncate(ref,size,creds)\
   PVFS_sys_truncate(ref,size,creds,PVFS_HINT_NULL)

#endif


#endif
