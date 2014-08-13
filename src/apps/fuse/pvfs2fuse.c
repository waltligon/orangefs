/*
 *   PVFS FUSE interface
 *
 *
 *   (C) 2001 Clemson University and The University of Chicago
 *
 *   (C) 2007 University of Connecticut. All rights reserved.
 *
 *   Author: John A. Chandy
 *           Sumit Narayan
 *
 *   $Date: 2010-12-21 15:34:13 $
 *   $Revision: 1.3.8.2 $
 *
 *   Documentation: http://www.engr.uconn.edu/~sun03001/docs/pvfs2fuse-rpt.pdf
 */

/* char *pvfs2fuse_version = "$Id: pvfs2fuse.c,v 1.3.8.2 2010-12-21 15:34:13 mtmoore Exp $"; */
char *pvfs2fuse_version = "0.01";

#define FUSE_USE_VERSION 27

#include <fuse.h>
#include <fuse_opt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <utime.h>
#include <unistd.h>

#include "pvfs2-compat.h"
#include "pint-dev-shared.h"
#include "pint-util.h"
#include "str-utils.h"
#include "pvfs2-util.h"
#include "pint-security.h"
#include "security-util.h"

typedef struct {
	  PVFS_object_ref	ref;
	  PVFS_credential	cred;
} pvfs_fuse_handle_t;

struct pvfs2fuse {
	  char	*fs_spec;
	  char	*mntpoint;
	  PVFS_fs_id	fs_id;
	  struct PVFS_sys_mntent mntent;
};

static struct pvfs2fuse pvfs2fuse;

#if __LP64__
#define SET_FUSE_HANDLE( fi, pfh ) \
	fi->fh = (uint64_t)pfh
#define GET_FUSE_HANDLE( fi ) \
	(pvfs_fuse_handle_t *)fi->fh
#else
#define SET_FUSE_HANDLE( fi, pfh ) \
	*((pvfs_fuse_handle_t **)(&fi->fh)) = pfh
#define GET_FUSE_HANDLE( fi ) \
	*((pvfs_fuse_handle_t **)(&fi->fh))
#endif

#define PVFS_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#define THIS_PVFS_VERSION \
     PVFS_VERSION(PVFS2_VERSION_MAJOR, PVFS2_VERSION_MINOR, PVFS2_VERSION_SUB)

#if THIS_PVFS_VERSION > PVFS_VERSION(2,6,3)
#define PVFS_ERROR_TO_ERRNO_N(x) (-1)*PVFS_ERROR_TO_ERRNO(x)
#else
#define PVFS_ERROR_TO_ERRNO_N(x) PVFS_ERROR_TO_ERRNO(x)
#endif

#define pvfs_fuse_cleanup_credential(cred) PINT_cleanup_credential(cred)

static int pvfs_fuse_gen_credential(
   PVFS_credential *credential)
{
   struct fuse_context *ctx = fuse_get_context();
   PVFS_credential *new_cred;
   char uid[16], gid[16];
   int ret;

   /* convert uid/gid to strings */
   ret = snprintf(uid, sizeof(uid), "%u", ctx->uid);
   if (ret < 0 || ret >= sizeof(uid))
   {
      return -1;
   }

   ret = snprintf(gid, sizeof(gid), "%u", ctx->gid);
   if (ret < 0 || ret >= sizeof(gid))
   {
       return -1;
   }

   /* allocate new credential */
   new_cred = (PVFS_credential *) malloc(sizeof(PVFS_credential));
   if (!new_cred)
   {
       return -ENOMEM;
   }
   memset(new_cred, 0, sizeof(PVFS_credential));

   /* generate credential -- this process must be running as root */
   ret = PVFS_util_gen_credential(uid, 
                                  gid, 
                                  PVFS2_DEFAULT_CREDENTIAL_TIMEOUT, 
                                  NULL, NULL,
                                  new_cred);

   if (ret == 0)
   {
       /* copy credential to provided buffer */
       ret = PINT_copy_credential(new_cred, credential);      
   }

   /* free generated credential */
   pvfs_fuse_cleanup_credential(new_cred);
   free(new_cred);

   return ret;
}

static int lookup( const char *path, pvfs_fuse_handle_t *pfh, 
				   int32_t follow_link )
{
   PVFS_sysresp_lookup lk_response;
   int			ret;

   /* we don't have to do a PVFS_util_resolve
	* because FUSE resolves the path for us
	*/

   ret = pvfs_fuse_gen_credential(&pfh->cred);
   if (ret < 0)
   {
      return ret;
   }

   memset(&lk_response, 0, sizeof(lk_response));
   ret = PVFS_sys_lookup(pvfs2fuse.fs_id, 
						 (char *)path,
						 &pfh->cred, 
						 &lk_response, 
						 follow_link);
   if ( ret < 0 ) {
      pvfs_fuse_cleanup_credential(&pfh->cred);
	  return ret;
   }

   pfh->ref.handle = lk_response.ref.handle;
   pfh->ref.fs_id  = pvfs2fuse.fs_id;

   return 0;
}

static int pvfs_fuse_getattr_pfhp(pvfs_fuse_handle_t *pfhp, struct stat *stbuf)
{
   PVFS_sysresp_getattr getattr_response;
   PVFS_sys_attr*	attrs;
   int			ret;
   int			perm_mode = 0;

   memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    
   ret = PVFS_sys_getattr(pfhp->ref, 
                          PVFS_ATTR_SYS_ALL_NOHINT,
                          (PVFS_credential *) &pfhp->cred, 
                          &getattr_response);
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );
   
   memset(stbuf, 0, sizeof(struct stat));

   /* Code copied from kernel/linux-2.x/pvfs2-utils.c */

   /*
	 arbitrarily set the inode block size; FIXME: we need to
	 resolve the difference between the reported inode blocksize
	 and the PAGE_CACHE_SIZE, since our block count will always
	 be wrong.

	 For now, we're setting the block count to be the proper
	 number assuming the block size is 512 bytes, and the size is
	 rounded up to the nearest 4K.  This is apparently required
	 to get proper size reports from the 'du' shell utility.

   */

   attrs = &getattr_response.attr;
   
   if (attrs->objtype == PVFS_TYPE_METAFILE)
   {
	  if (attrs->mask & PVFS_ATTR_SYS_SIZE)
	  {
		 size_t inode_size = attrs->size;
		 size_t rounded_up_size = (inode_size + (4096 - (inode_size % 4096)));

		 stbuf->st_size = inode_size;
		 stbuf->st_blocks = (unsigned long)(rounded_up_size / 512);
	  }
   }
   else if ((attrs->objtype == PVFS_TYPE_SYMLINK) &&
			(attrs->link_target != NULL))
   {
	  stbuf->st_size = strlen(attrs->link_target);
   }
   else
   {
      /* what should this be??? */
	  unsigned long PAGE_CACHE_SIZE = 4096;
	  stbuf->st_blocks = (unsigned long)(PAGE_CACHE_SIZE / 512);
	  stbuf->st_size = PAGE_CACHE_SIZE;
   }

   stbuf->st_uid = attrs->owner;
   stbuf->st_gid = attrs->group;

   stbuf->st_atime = (time_t)attrs->atime;
   stbuf->st_mtime = (time_t)attrs->mtime;
   stbuf->st_ctime = (time_t)attrs->ctime;

   stbuf->st_mode = 0;
   if (attrs->perms & PVFS_O_EXECUTE)
	  perm_mode |= S_IXOTH;
   if (attrs->perms & PVFS_O_WRITE)
	  perm_mode |= S_IWOTH;
   if (attrs->perms & PVFS_O_READ)
	  perm_mode |= S_IROTH;

   if (attrs->perms & PVFS_G_EXECUTE)
	  perm_mode |= S_IXGRP;
   if (attrs->perms & PVFS_G_WRITE)
	  perm_mode |= S_IWGRP;
   if (attrs->perms & PVFS_G_READ)
	  perm_mode |= S_IRGRP;

   if (attrs->perms & PVFS_U_EXECUTE)
	  perm_mode |= S_IXUSR;
   if (attrs->perms & PVFS_U_WRITE)
	  perm_mode |= S_IWUSR;
   if (attrs->perms & PVFS_U_READ)
	  perm_mode |= S_IRUSR;

   if (attrs->perms & PVFS_G_SGID)
      perm_mode |= S_ISGID;

   /* Should we honor the suid bit of the file? */
   /* FIXME should we check the file system suid flag */
   if ( /* get_suid_flag(inode) == 1 && */ (attrs->perms & PVFS_U_SUID))
	  perm_mode |= S_ISUID;

   stbuf->st_mode |= perm_mode;

   /* FIXME special case: mark the root inode as sticky
	  if (is_root_handle(inode))
	  {
	  inode->i_mode |= S_ISVTX;
	  }
   */
   switch (attrs->objtype)
   {
	  case PVFS_TYPE_METAFILE:
		 stbuf->st_mode |= S_IFREG;
		 break;
	  case PVFS_TYPE_DIRECTORY:
		 stbuf->st_mode |= S_IFDIR;
		 /* NOTE: we have no good way to keep nlink consistent for 
		  * directories across clients; keep constant at 1.  Why 1?  If
		  * we go with 2, then find(1) gets confused and won't work
		  * properly withouth the -noleaf option */
		 stbuf->st_nlink = 1;
		 break;
	  case PVFS_TYPE_SYMLINK:
		 stbuf->st_mode |= S_IFLNK;
		 break;
	  default:
		 break;
   }

   stbuf->st_dev = pfhp->ref.fs_id;
   stbuf->st_ino = pfhp->ref.handle;

   stbuf->st_rdev = 0;
   stbuf->st_blksize = 4096;

   PVFS_util_release_sys_attr(attrs);
    
   return 0;
}

static int pvfs_fuse_getattr(const char *path, struct stat *stbuf)
{
   int			ret;
   pvfs_fuse_handle_t	pfh;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_NO_FOLLOW );
   if ( ret < 0 )
   {
      return PVFS_ERROR_TO_ERRNO_N( ret );
   }

   ret = pvfs_fuse_getattr_pfhp( &pfh, stbuf );

   pvfs_fuse_cleanup_credential(&pfh.cred);

   return ret;
}

static int pvfs_fuse_fgetattr(const char *path, struct stat *stbuf,
							  struct fuse_file_info *fi)
{
   return pvfs_fuse_getattr_pfhp( GET_FUSE_HANDLE( fi ), stbuf );
}

static int pvfs_fuse_readlink(const char *path, char *buf, size_t size)
{
   PVFS_sysresp_getattr getattr_response;
   int			ret;
   size_t		len;
   pvfs_fuse_handle_t	pfh;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_NO_FOLLOW );
   if ( ret < 0 )
   {
      return PVFS_ERROR_TO_ERRNO_N( ret );
   }

   ret = PVFS_sys_getattr(pfh.ref, 
						  PVFS_ATTR_SYS_ALL_NOHINT,
						  (PVFS_credential *) &pfh.cred, 
						  &getattr_response);

   pvfs_fuse_cleanup_credential(&pfh.cred);

   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   if (getattr_response.attr.objtype != PVFS_TYPE_SYMLINK)
	  return -EINVAL;

   len = strlen( getattr_response.attr.link_target );
   if ( len < (size-1) )
	  size = len;

   bcopy( getattr_response.attr.link_target, buf, size );

   buf[len] = '\0';

   return 0;
}

static int pvfs_fuse_mkdir(const char *path, mode_t mode)
{
   int rc;
   int num_segs;
   PVFS_sys_attr attr;
   char parent[PVFS_NAME_MAX];
   char dirname[PVFS_SEGMENT_MAX];
   pvfs_fuse_handle_t	parent_pfh;

   PVFS_sysresp_mkdir resp_mkdir;

   /* Translate path into pvfs2 relative path */
   rc = PINT_get_base_dir((char *)path, parent, PVFS_NAME_MAX);
   num_segs = PINT_string_count_segments((char *)path);
   rc = PINT_get_path_element((char *)path, num_segs - 1,
							  dirname, PVFS_SEGMENT_MAX);

   if (rc)
   {
	  return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   rc = lookup( parent, &parent_pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if (rc)
   {
      return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   /* Set attributes */
   memset(&attr, 0, sizeof(PVFS_sys_attr));
   attr.owner = parent_pfh.cred.userid;
   attr.group = parent_pfh.cred.group_array[0];
   attr.perms = mode;
   attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;

   rc = PVFS_sys_mkdir(dirname,
					   parent_pfh.ref,
					   attr,
					   &parent_pfh.cred,
					   &resp_mkdir);

   pvfs_fuse_cleanup_credential(&parent_pfh.cred);

   if (rc)
   {
	  return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   return 0;
}

static int pvfs_fuse_remove( const char *path )
{
   int rc;
   int num_segs;
   char parent[PVFS_NAME_MAX];
   char filename[PVFS_SEGMENT_MAX];
   pvfs_fuse_handle_t	parent_pfh;

   /* Translate path into pvfs2 relative path */
   rc = PINT_get_base_dir((char *)path, parent, PVFS_NAME_MAX);
   num_segs = PINT_string_count_segments((char *)path);
   rc = PINT_get_path_element((char *)path, num_segs - 1,
							  filename, PVFS_SEGMENT_MAX);

   if (rc)
   {
	  return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   rc = lookup( parent, &parent_pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if (rc)
   {
      return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   rc = PVFS_sys_remove(filename, parent_pfh.ref, &parent_pfh.cred);

   pvfs_fuse_cleanup_credential(&parent_pfh.cred);

   if (rc)
   {
	  return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   return 0;
}

static int pvfs_fuse_unlink(const char *path)
{
   return pvfs_fuse_remove(path);
}

static int pvfs_fuse_rmdir(const char *path)
{
   return pvfs_fuse_remove(path);
}

static int pvfs_fuse_symlink(const char *from, const char *to)
{
   int                  ret = 0;
   PVFS_sys_attr        attr;
   PVFS_sysresp_lookup  resp_lookup;
   PVFS_object_ref      parent_ref;
   PVFS_sysresp_symlink resp_sym;
   PVFS_credential      credential;
   pvfs_fuse_handle_t	dir_pfh;
   char *tofile, *todir, *cp;

   pvfs_fuse_gen_credential(&credential);

   /* Initialize any variables */
   memset(&attr,        0, sizeof(attr));
   memset(&resp_lookup, 0, sizeof(resp_lookup));
   memset(&parent_ref,  0, sizeof(parent_ref));
   memset(&resp_sym,    0, sizeof(resp_sym));

   /* Set the attributes for the new directory */
   attr.owner = credential.userid;
   attr.group = credential.group_array[0];
   attr.perms = 0777;              
   attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

   todir = strdup( to );
   if ( todir == NULL )
	  return -ENOMEM;

   /* find the last / to get the parent directory */
   cp = rindex( todir,  '/' );
   if ( cp == NULL )
   {
	  free( todir );
	  return -ENOTDIR;
   }
   tofile = strdup( cp+1 );
   if ( cp == todir )
   {
	  /* we're creating a link at the root, so keep the slash */
	  *(cp+1) = '\0';
   }
   else
   {
	  *cp = '\0';
   }

   ret = lookup( todir, &dir_pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if(ret < 0)
   {
	  free( tofile );
	  free( todir );
	  PVFS_perror("lookup", ret);
	  return(-1);
   }

   ret = PVFS_sys_symlink(tofile, 
						  dir_pfh.ref, 
						  (char *) from,
						  attr, 
						  &credential, 
						  &resp_sym);

   pvfs_fuse_cleanup_credential(&credential);
   pvfs_fuse_cleanup_credential(&dir_pfh.cred);

   if (ret < 0)
   {
	  PVFS_perror("PVFS_sys_symlink", ret);
	  return(ret);
   }
   else
   {
	  ret = 0;
   }
    
   free( tofile );
   free( todir );
   return(ret);
}

static int pvfs_fuse_rename(const char *from, const char *to)
{
   int rc;
   int num_segs;
   char fromdir[PVFS_NAME_MAX], todir[PVFS_NAME_MAX];
   char fromname[PVFS_SEGMENT_MAX], toname[PVFS_SEGMENT_MAX];
   pvfs_fuse_handle_t	todir_pfh, fromdir_pfh;

   /* Translate path into pvfs2 relative path */
   rc = PINT_get_base_dir((char *)from, fromdir, PVFS_NAME_MAX);
   num_segs = PINT_string_count_segments((char *)from);
   rc = PINT_get_path_element((char *)from, num_segs - 1,
							  fromname, PVFS_SEGMENT_MAX);

   if (rc)
	  return PVFS_ERROR_TO_ERRNO_N( rc );

   rc = lookup( fromdir, &fromdir_pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if (rc < 0)
	  return PVFS_ERROR_TO_ERRNO_N( rc );

   /* Translate path into pvfs2 relative path */
   rc = PINT_get_base_dir((char *)to, todir, PVFS_NAME_MAX);
   num_segs = PINT_string_count_segments((char *)to);
   rc = PINT_get_path_element((char *)to, num_segs - 1,
							  toname, PVFS_SEGMENT_MAX);

   if (rc)
	  return PVFS_ERROR_TO_ERRNO_N( rc );

   lookup( todir, &todir_pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if (rc < 0)
	  return PVFS_ERROR_TO_ERRNO_N( rc );

   rc = PVFS_sys_rename(fromname,
						fromdir_pfh.ref,
						toname,
						todir_pfh.ref,
						&todir_pfh.cred);

   pvfs_fuse_cleanup_credential(&fromdir_pfh.cred);
   pvfs_fuse_cleanup_credential(&todir_pfh.cred);

   if (rc)
	  return PVFS_ERROR_TO_ERRNO_N( rc );

   return 0;
}

static int pvfs_fuse_chmod(const char *path, mode_t mode)
{
   int			ret;
   PVFS_sys_attr	new_attr;
 
   pvfs_fuse_handle_t	pfh;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );
   /* FUSE passes in 5 octets in 'mode'. However, the the first 
    * octet is not related to permissions, hence checking only
    *  the lower 4 octets */
   new_attr.perms = mode & 07777;
   new_attr.mask = PVFS_ATTR_SYS_PERM;
 
   ret = PVFS_sys_setattr(pfh.ref,new_attr,&pfh.cred);

   pvfs_fuse_cleanup_credential(&pfh.cred);

   if (ret < 0) 
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   return 0;
}

static int pvfs_fuse_chown(const char *path, uid_t uid, gid_t gid)
{
   int			ret;
   PVFS_sys_attr	new_attr;
 
   pvfs_fuse_handle_t	pfh;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );
   
   new_attr.owner = uid;
   new_attr.group = gid;
   new_attr.mask = PVFS_ATTR_SYS_UID | PVFS_ATTR_SYS_GID;
 
   ret = PVFS_sys_setattr(pfh.ref,new_attr,&pfh.cred);

   pvfs_fuse_cleanup_credential(&pfh.cred);

   if (ret < 0) 
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   return 0;
}

static int pvfs_fuse_truncate(const char *path, off_t size)
{
   int			ret;
   pvfs_fuse_handle_t	pfh;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );
   
   ret = PVFS_sys_truncate(pfh.ref,size,&pfh.cred);

   pvfs_fuse_cleanup_credential(&pfh.cred);

   if (ret < 0) 
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   return 0;
}

static int pvfs_fuse_utime(const char *path, struct utimbuf *timbuf)
{
   int			ret;
   PVFS_sys_attr	new_attr;
 
   pvfs_fuse_handle_t	pfh;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );
   
   new_attr.atime = (PVFS_time)timbuf->actime;
   new_attr.mtime = (PVFS_time)timbuf->modtime;
   new_attr.mask = PVFS_ATTR_SYS_ATIME | PVFS_ATTR_SYS_MTIME;
 
   ret = PVFS_sys_setattr(pfh.ref,new_attr,&pfh.cred);

   pvfs_fuse_cleanup_credential(&pfh.cred);

   if (ret < 0) 
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   return 0;
}

static int pvfs_fuse_open(const char *path, struct fuse_file_info *fi)
{
   pvfs_fuse_handle_t *pfhp;
   int			ret;

   pfhp = (pvfs_fuse_handle_t *)malloc( sizeof( pvfs_fuse_handle_t ) );
   if (pfhp == NULL)
   {
	  return -ENOMEM;
   }

   ret = lookup( path, pfhp, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 ) {
      free( pfhp );
      return PVFS_ERROR_TO_ERRNO_N( ret );
   }

   SET_FUSE_HANDLE( fi, pfhp );

   return 0;
}

static int pvfs_fuse_read(const char *path, char *buf, size_t size, off_t offset,
						  struct fuse_file_info *fi)
{
   PVFS_Request	mem_req, file_req;
   PVFS_sysresp_io	resp_io;
   int			ret;
   pvfs_fuse_handle_t	*pfh = GET_FUSE_HANDLE( fi );
  
   file_req = PVFS_BYTE;
   ret = PVFS_Request_contiguous(size, PVFS_BYTE, &mem_req);
   if (ret < 0)
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   ret = PVFS_sys_read(pfh->ref, file_req, offset, buf,
					   mem_req, &pfh->cred, &resp_io);

   if (ret == 0) 
   {
	  PVFS_Request_free(&mem_req);
	  return(resp_io.total_completed);
   }
   else
	  return PVFS_ERROR_TO_ERRNO_N( ret );
}

static int pvfs_fuse_write(const char *path, const char *buf, size_t size,
						   off_t offset, struct fuse_file_info *fi)
{
   PVFS_Request	mem_req, file_req;
   PVFS_sysresp_io	resp_io;
   int			ret;
   pvfs_fuse_handle_t	*pfh = GET_FUSE_HANDLE( fi );
  
   file_req = PVFS_BYTE;
   ret = PVFS_Request_contiguous(size, PVFS_BYTE, &mem_req);
   if (ret < 0)
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   ret = PVFS_sys_write(pfh->ref, file_req, offset, (char*)buf,
						mem_req, &pfh->cred, &resp_io);
   if (ret == 0) 
   {
	  PVFS_Request_free(&mem_req);
	  return(resp_io.total_completed);
   }
   else
	  return PVFS_ERROR_TO_ERRNO_N( ret );
}

static int pvfs_fuse_statfs(const char *path, struct statvfs *stbuf)
{
   int			ret;
   PVFS_credential	cred;
   PVFS_sysresp_statfs resp_statfs;

   ret = pvfs_fuse_gen_credential(&cred);
   if (ret < 0)
   {
       return PVFS_ERROR_TO_ERRNO_N(ret);
   }

   /* gather normal statfs statistics from system interface */

   ret = PVFS_sys_statfs(pvfs2fuse.fs_id, &cred, &resp_statfs);

   pvfs_fuse_cleanup_credential(&cred);

   if (ret < 0)
   {
	  if(ret != ERANGE)
		 return PVFS_ERROR_TO_ERRNO_N( ret );
   }

   memcpy(&stbuf->f_fsid, &resp_statfs.statfs_buf.fs_id, 
		  sizeof(resp_statfs.statfs_buf.fs_id));
   /* FIXME is this bsize right? */

   stbuf->f_bsize = PVFS2_BUFMAP_DEFAULT_DESC_SIZE;
   stbuf->f_frsize = PVFS2_BUFMAP_DEFAULT_DESC_SIZE;
   stbuf->f_namemax = PVFS_NAME_MAX;

   stbuf->f_blocks = resp_statfs.statfs_buf.bytes_total / stbuf->f_bsize;
   stbuf->f_bfree = resp_statfs.statfs_buf.bytes_available / stbuf->f_bsize;
   stbuf->f_bavail = resp_statfs.statfs_buf.bytes_available / stbuf->f_bsize;
   stbuf->f_files = resp_statfs.statfs_buf.handles_total_count;
   stbuf->f_ffree = resp_statfs.statfs_buf.handles_available_count;
   stbuf->f_favail = resp_statfs.statfs_buf.handles_available_count;

   stbuf->f_flag = 0;

   return 0;
}

static int pvfs_fuse_release(const char *path, struct fuse_file_info *fi)
{
   pvfs_fuse_handle_t *pfh = GET_FUSE_HANDLE( fi );
  
   if ( pfh != NULL ) {
      pvfs_fuse_cleanup_credential(&pfh->cred);
      free( pfh );
      SET_FUSE_HANDLE( fi, NULL );
   }

   return 0;
}

static int pvfs_fuse_fsync(const char *path, int isdatasync,
						   struct fuse_file_info *fi)
{
   /* Just a stub.  This method is optional and can safely be left
	  unimplemented */

   (void) path;
   (void) isdatasync;
   (void) fi;

   return 0;
}

#define MAX_NUM_DIRENTS    32

static int pvfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
							 off_t offset, struct fuse_file_info *fi)
{
   int			ret;
   PVFS_ds_position	token;
   pvfs_fuse_handle_t	pfh;
   int			pvfs_dirent_incount;
   PVFS_sysresp_readdir rd_response;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   pvfs_dirent_incount = MAX_NUM_DIRENTS;
   token = 0;
   do
   {
	  char *cur_file = NULL;
	  int i;

	  memset(&rd_response, 0, sizeof(PVFS_sysresp_readdir));
	  ret = PVFS_sys_readdir(
		 pfh.ref, (!token ? PVFS_READDIR_START : token),
		 pvfs_dirent_incount, &pfh.cred, &rd_response);
	  if(ret < 0)
      {
         pvfs_fuse_cleanup_credential(&pfh.cred);
         return PVFS_ERROR_TO_ERRNO_N( ret );
      }

	  for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
	  {
		 cur_file = rd_response.dirent_array[i].d_name;

		 if (filler(buf, cur_file, NULL, 0))
			break;
	  }
	  token = rd_response.token;

	  if (rd_response.pvfs_dirent_outcount)
	  {
		 free(rd_response.dirent_array);
		 rd_response.dirent_array = NULL;
	  }

   } while(token != PVFS_READDIR_END);

   pvfs_fuse_cleanup_credential(&pfh.cred);

   return 0;
}

static int pvfs_fuse_access(const char *path, int mask)
{
   PVFS_sysresp_getattr getattr_response;
   PVFS_sys_attr*	attrs;
   int			ret;
   pvfs_fuse_handle_t	pfh;
   int			in_group_flag = 0;
   PVFS_uid     uid;
   PVFS_gid     gid;

   ret = lookup( path, &pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( ret < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( ret );

   /* give root permission, no matter what */
   if ( pfh.cred.userid == 0 )
	  return 0;

   /* if checking for file existence, return 0 */
   if ( mask == F_OK )
	  return 0;

   ret = PVFS_sys_getattr(pfh.ref, 
                          PVFS_ATTR_SYS_ALL_NOHINT,
                          (PVFS_credential *) &pfh.cred, 
                          &getattr_response);

   /* copy uid and gid so credential can be freed */
   uid = pfh.cred.userid;
   gid = pfh.cred.group_array[0];
   pvfs_fuse_cleanup_credential(&pfh.cred);

   if ( ret < 0 )
   {            
	  return PVFS_ERROR_TO_ERRNO_N( ret );
   }

   attrs = &getattr_response.attr;

   /* basic code is copied from PINT_check_mode() */

   /* see if uid matches object owner */
   if ( attrs->owner == uid )
   {
	  /* see if object user permissions match access type */
	  if( (mask & R_OK) && (attrs->perms & PVFS_U_READ))
	  {
		 return(0);
	  }
	  if( (mask & W_OK) && (attrs->perms & PVFS_U_WRITE))
	  {
		 return(0);
	  }
	  if( (mask & X_OK) && (attrs->perms & PVFS_U_EXECUTE))
	  {
		 return(0);
	  }
   }

   /* see if other bits allow access */
   if( (mask & R_OK) && (attrs->perms & PVFS_O_READ))
   {
	  return(0);
   }
   if( (mask & W_OK) && (attrs->perms & PVFS_O_WRITE))
   {
	  return(0);
   }
   if( (mask & X_OK) && (attrs->perms & PVFS_O_EXECUTE))
   {
	  return(0);
   }

   /* see if gid matches object group 
      TODO: check all groups */
   if(attrs->group == gid)
   {
	  /* default group match */
	  in_group_flag = 1;
   }
   else
   {
#if 0
	  /* no default group match, check supplementary groups */
	  ret = PINT_check_group(uid, attrs->group);
	  if(ret == 0)
	  {
		 in_group_flag = 1;
	  }
	  else
	  {
		 if(ret != -PVFS_ENOENT)
		 {
			/* system error; not just failed match */
			return(ret);
		 }
	  }
#endif
   }

   if(in_group_flag)
   {
	  /* see if object group permissions match access type */
	  if( (mask & R_OK) && (attrs->perms & PVFS_G_READ))
	  {
		 return(0);
	  }
	  if( (mask & W_OK) && (attrs->perms & PVFS_G_WRITE))
	  {
		 return(0);
	  }
	  if( (mask & X_OK) && (attrs->perms & PVFS_G_EXECUTE))
	  {
		 return(0);
	  }
   }
  
   /* default case: access denied */
   return -EACCES;
}

static int pvfs_fuse_create(const char *path, mode_t mode,
							struct fuse_file_info *fi)
{
   int rc;
   int num_segs;
   PVFS_sys_attr attr;
   char directory[PVFS_NAME_MAX];
   char filename[PVFS_SEGMENT_MAX];
   pvfs_fuse_handle_t	dir_pfh, *pfhp;

   PVFS_sysresp_create resp_create;

   /* Translate path into pvfs2 relative path */
   rc = PINT_get_base_dir((char *)path, directory, PVFS_NAME_MAX);
   num_segs = PINT_string_count_segments((char *)path);
   rc = PINT_get_path_element((char *)path, num_segs - 1,
							  filename, PVFS_SEGMENT_MAX);

   if (rc)
   {
	  return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   rc = lookup( directory, &dir_pfh, PVFS2_LOOKUP_LINK_FOLLOW );
   if ( rc < 0 )
	  return PVFS_ERROR_TO_ERRNO_N( rc );

   /* Set attributes */
   memset(&attr, 0, sizeof(PVFS_sys_attr));
   attr.owner = dir_pfh.cred.userid;
   attr.group = dir_pfh.cred.group_array[0];
   attr.perms = mode;
   attr.atime = time(NULL);
   attr.mtime = attr.atime;
   attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
   attr.dfile_count = 0;

   rc = PVFS_sys_create(filename,
						dir_pfh.ref,
						attr,
						&dir_pfh.cred,
						NULL,
						&resp_create);
   if (rc)
   {
      /* FIXME
       * the PVFS2 server code returns a ENOENT instead of an EACCES
       * because it does a ACL lookup for the system.posix_acl_access
       * which returns a ENOENT from the TROVE DBPF and that error is
       * just passed up in prelude_check_acls (server/prelude.c).  I'm
       * not sure that's the right thing to do.
       */
	  if ( rc == -PVFS_ENOENT ) 
	  {
		 return -EACCES;
	  }
	  return PVFS_ERROR_TO_ERRNO_N( rc );
   }

   pfhp = (pvfs_fuse_handle_t *)malloc( sizeof( pvfs_fuse_handle_t ) );
   if (pfhp == NULL)
   {
	  return -ENOMEM;
   }

   pfhp->ref = resp_create.ref;
   pfhp->cred = dir_pfh.cred;

   SET_FUSE_HANDLE( fi, pfhp );

   return 0;
}

static struct fuse_operations pvfs_fuse_oper = {
   .getattr	= pvfs_fuse_getattr,
   .fgetattr	= pvfs_fuse_fgetattr,
   .readlink	= pvfs_fuse_readlink,
   .mkdir	= pvfs_fuse_mkdir,
   .unlink	= pvfs_fuse_unlink,
   .rmdir	= pvfs_fuse_rmdir,
   .symlink	= pvfs_fuse_symlink,
   .rename	= pvfs_fuse_rename,
   /* .link	= pvfs_fuse_link, */ /* hard links not supported on PVFS */
   .chmod	= pvfs_fuse_chmod,
   .chown	= pvfs_fuse_chown,
   .truncate	= pvfs_fuse_truncate,
   .utime	= pvfs_fuse_utime,
   .open	= pvfs_fuse_open,
   .read	= pvfs_fuse_read,
   .write	= pvfs_fuse_write,
   .statfs	= pvfs_fuse_statfs,
/*  .flush	= pvfs_fuse_flush, */
   .release	= pvfs_fuse_release,
   .fsync	= pvfs_fuse_fsync,
   .readdir	= pvfs_fuse_readdir,
   .access	= pvfs_fuse_access,
   .create	= pvfs_fuse_create,
};

enum {
   KEY_HELP,
   KEY_VERSION,
};

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#define PVFS2FUSE_OPT(t, p, v) { t, offsetof(struct pvfs2fuse, p), v }

static struct fuse_opt pvfs2fuse_opts[] = {
   PVFS2FUSE_OPT("fs_spec=%s",     fs_spec, 0),

   FUSE_OPT_KEY("-V",             KEY_VERSION),
   FUSE_OPT_KEY("--version",      KEY_VERSION),
   FUSE_OPT_KEY("-h",             KEY_HELP),
   FUSE_OPT_KEY("--help",         KEY_HELP),
   FUSE_OPT_END
};

static void usage(const char *progname)
{
   fprintf(stderr,
		   "usage: %s mountpoint [options]\n"
		   "\n"
		   "general options:\n"
		   "    -o opt,[opt...]        mount options\n"
		   "    -h   --help            print help\n"
		   "    -V   --version         print version\n"
		   "\n"
		   "PVFS2FUSE options:\n"
		   "    -o fs_spec=FS_SPEC     PVFS2 fs_spec URI (eg. tcp://localhost:3334/pvfs2-fs)\n"
		   "\n", progname);
}

static int pvfs_fuse_main(struct fuse_args *args)
{
#if FUSE_VERSION >= 26
   return fuse_main(args->argc, args->argv, &pvfs_fuse_oper, NULL);
#else
   return fuse_main(args->argc, args->argv, &pvfs_fuse_oper);
#endif
}

static int pvfs2fuse_opt_proc(void *data, const char *arg, int key,
							  struct fuse_args *outargs)
{
   (void) data;

   switch (key) {
	  case FUSE_OPT_KEY_OPT:
		 return 1;

	  case FUSE_OPT_KEY_NONOPT:
		 if (!pvfs2fuse.mntpoint) {
                     if(!arg)
                     {
                         fprintf(stderr, "PVFS2FUSE requires mountpoint as argument\n");
                         abort();
                     }

            pvfs2fuse.mntpoint = strdup(arg);
		 }
		 return 1;

	  case KEY_HELP:
		 usage(outargs->argv[0]);
		 /* FIXME don't show the FUSE arguments
			fuse_opt_add_arg(outargs, "-ho");
			pvfs_fuse_main(outargs); */
		 exit(1);

	  case KEY_VERSION:
		 fprintf(stderr, "PVFS2FUSE version %s (PVFS2 %s) (%s, %s)\n",
				 pvfs2fuse_version, PVFS2_VERSION, __DATE__, __TIME__);
#if FUSE_VERSION >= 25
		 fuse_opt_add_arg(outargs, "--version");
		 pvfs_fuse_main(outargs);
#endif
		 exit(0);

	  default:
		 fprintf(stderr, "internal error\n");
		 abort();
   }
}

int main(int argc, char *argv[])
{
   int ret;
   struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

   umask(0);

   if (fuse_opt_parse(&args, &pvfs2fuse, pvfs2fuse_opts,
					  pvfs2fuse_opt_proc) == -1 )
	  exit(1);

   if (pvfs2fuse.fs_spec == NULL)
   {
	  ret = PVFS_util_init_defaults();
	  if(ret < 0)
	  {
		 PVFS_perror("PVFS_util_init_defaults", ret);
		 return(-1);
	  }

	  ret = PVFS_util_get_default_fsid(&pvfs2fuse.fs_id);
	  if( ret < 0 )
	  {
		 PVFS_perror("No default PVFS2 filesystem found", ret);
		 return(-1);
	  }

	  PVFS_util_get_mntent_copy( pvfs2fuse.fs_id, &pvfs2fuse.mntent );
	  /* Set timeouts for PVFS2's name cache and attribute cache */
	  PVFS_sys_set_info(PVFS_SYS_ACACHE_TIMEOUT_MSECS, 0);
	  PVFS_sys_set_info(PVFS_SYS_NCACHE_TIMEOUT_MSECS, 0);
   }
   else
   {
	  struct PVFS_sys_mntent *me = &pvfs2fuse.mntent;
	  char *cp;
	  int cur_server;

	  /* the following is copied from PVFS_util_init_defaults()
		 in fuse/lib/pvfs2-util.c */

	  /* initialize pvfs system interface */
	  ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
	  if (ret < 0)
	  {
		 return(ret);
	  }

	  /* the following is copied from PVFS_util_parse_pvfstab()
		 in fuse/lib/pvfs2-util.c */
	  memset( me, 0, sizeof(pvfs2fuse.mntent) );

	  /* Enable integrity checks by default */
	  me->integrity_check = 1;
	  /* comma-separated list of ways to contact a config server */
	  me->num_pvfs_config_servers = 1;

	  for (cp=pvfs2fuse.fs_spec; *cp; cp++)
		 if (*cp == ',')
			++me->num_pvfs_config_servers;

	  /* allocate room for our copies of the strings */
	  me->pvfs_config_servers =
		 malloc(me->num_pvfs_config_servers *
				sizeof(*me->pvfs_config_servers));
	  if (!me->pvfs_config_servers)
		 exit(-1);
	  memset(me->pvfs_config_servers, 0,
			 me->num_pvfs_config_servers * sizeof(*me->pvfs_config_servers));

	  me->mnt_dir = strdup(pvfs2fuse.mntpoint);
	  me->mnt_opts = NULL;

	  cp = pvfs2fuse.fs_spec;
	  cur_server = 0;
	  for (;;) {
		 char *tok;
		 int slashcount;
		 char *slash;
		 char *last_slash;

		 tok = strsep(&cp, ",");
		 if (!tok) break;

		 slash = tok;
		 slashcount = 0;
		 while ((slash = index(slash, '/')))
		 {
			slash++;
			slashcount++;
		 }
		 if (slashcount != 3)
		 {
			fprintf(stderr,"Error: invalid FS spec: %s\n",
					pvfs2fuse.fs_spec);
			exit(-1);
		 }

		 /* find a reference point in the string */
		 last_slash = rindex(tok, '/');
		 *last_slash = '\0';

		 /* config server and fs name are a special case, take one 
		  * string and split it in half on "/" delimiter
		  */
		 me->pvfs_config_servers[cur_server] = strdup(tok);
		 if (!me->pvfs_config_servers[cur_server])
			exit(-1);

		 ++last_slash;

		 if (cur_server == 0) {
			me->pvfs_fs_name = strdup(last_slash);
			if (!me->pvfs_fs_name)
			   exit(-1);
		 } else {
			if (strcmp(last_slash, me->pvfs_fs_name) != 0) {
			   fprintf(stderr,
					   "Error: different fs names in server addresses: %s\n",
					   pvfs2fuse.fs_spec);
			   exit(-1);
			}
		 }
		 ++cur_server;
	  }
	
	  /* FIXME flowproto should be an option */
	  me->flowproto = FLOWPROTO_DEFAULT;

	  /* FIXME encoding should be an option */
	  me->encoding = PVFS2_ENCODING_DEFAULT;

	  /* FIXME default_num_dfiles should be an option */

	  ret = PVFS_sys_fs_add(me);
	  if( ret < 0 )
	  {
		 PVFS_perror("Could not add mnt entry", ret);
		 return(-1);
	  }
	  pvfs2fuse.fs_id = me->fs_id;
   }

   /* FIXME should we allow all the FUSE options?  Maybe we should
	* pass only some of the FUSE options to fuse_main.  For now,
	* force the direct_io and allow_other options.  Also turn off
	* multithreaded operation since it doesnt work with PVFS.
	*/

   fuse_opt_insert_arg( &args, 1, "-odirect_io" );
   fuse_opt_insert_arg( &args, 1, "-oattr_timeout=0");
   fuse_opt_insert_arg( &args, 1, "-omax_write=524288");
   if ( getuid() == 0 )
	  fuse_opt_insert_arg( &args, 1, "-oallow_other" );
   fuse_opt_insert_arg( &args, 1, "-s" );
    
   {
	  /* set the fsname and volname */
	  char name[200];
	  char *config = pvfs2fuse.mntent.the_pvfs_config_server;

	  if ( !config )
		 config = pvfs2fuse.mntent.pvfs_config_servers[0];

	  snprintf( name, 200, "-ofsname=pvfs2fuse#%s/%s", config, pvfs2fuse.mntent.pvfs_fs_name );
	  fuse_opt_insert_arg( &args, 1, name );
#if (__FreeBSD__ >= 10)
	  snprintf( name, 200, "-ovolname=%s", pvfs2fuse.mntent.pvfs_fs_name );
	  fuse_opt_insert_arg( &args, 1, name );
#endif
   }
    
#if (__FreeBSD__ >= 10)
   {
	  /* MacFUSE has a bug where cached attributes
	   * arent invalidated on direct_io writes
	   */
	  fuse_opt_insert_arg( &args, 1, "-oattr_timeout=0" );
   }
#endif

   return pvfs_fuse_main(&args);
}
