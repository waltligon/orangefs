/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>

#include "gossip.h"
#include "pint-dev.h"
#include "job.h"
#include "acache.h"

#include "client.h"

#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-util.h"
#include "pint-bucket.h"
#include "pvfs2-sysint.h"

#define MAX_NUM_THREADS 16

#ifdef USE_MMAP_RA_CACHE
#include "mmap-ra-cache.h"
#endif

/*
  an arbitrary limit to the max number of items
  we can write into the device file as a response
*/
#define MAX_LIST_SIZE    32

#define MAX_NUM_UNEXPECTED 10

/*
  the block size to report in statfs as the blocksize (i.e. the
  optimal i/o transfer size); regardless of this value, the fragment
  size (underlying fs block size) in the kernel is fixed at 1024
*/
#define STATFS_DEFAULT_BLOCKSIZE PVFS2_BUFMAP_DEFAULT_DESC_SIZE

/* small default attribute cache timeout; effectively disabled */
#define ACACHE_TIMEOUT_MS 1

/* used for generating unique dynamic mount point names */
static int dynamic_mount_id = 1;

typedef struct
{
    job_id_t job_id;
    job_status_s jstat;
    job_context_id context;
    struct PINT_dev_unexp_info info;
    struct PVFS_dev_map_desc *desc;
    pvfs2_upcall_t in_upcall;
    pvfs2_downcall_t out_downcall; 
} vfs_request_t;

/*
  this client core *requires* pthreads now, regardless of if the pvfs2
  system interface has threading enabled or not.  we need it for async
  remounts on restart to retrieve our dynamic mount information (if
  any) from the kernel, which means we call a blocking ioctl that must
  be serviced by our regular handlers.  to do both, we use a thread
  for the blocking ioctl.
*/
static pthread_t remount_thread;
static pthread_mutex_t remount_mutex = PTHREAD_MUTEX_INITIALIZER;

void *exec_remount(void *ptr)
{
    int ret = 0;

    pthread_mutex_lock(&remount_mutex);
    /*
      when the remount mutex is unlocked, tell the kernel to remount
      any file systems that may have been mounted previously, which
      will fill in our dynamic mount information by triggering mount
      upcalls for each fs mounted by the kernel at this point
     */
    ret = PINT_dev_remount();
    if (ret)
    {
        gossip_err("*** Failed to remount filesystems!\n");
    }
    pthread_mutex_unlock(&remount_mutex);
    return (void *)0;
}

static int service_lookup_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_lookup response;
    PVFS_object_ref parent_refn;

    if (in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_lookup));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a lookup request for %s (fsid %d | parent %Lu)\n",
            in_upcall->req.lookup.d_name,
            in_upcall->req.lookup.parent_refn.fs_id,
            Lu(in_upcall->req.lookup.parent_refn.handle));

        parent_refn = in_upcall->req.lookup.parent_refn;

        ret = PVFS_sys_ref_lookup(parent_refn.fs_id,
                                  in_upcall->req.lookup.d_name,
                                  parent_refn, in_upcall->credentials,
                                  &response,
                                  in_upcall->req.lookup.sym_follow);
        if (ret < 0)
        {
            gossip_debug(
                GOSSIP_CLIENT_DEBUG,
                "Failed to lookup %s (fsid %d | parent is %Lu)!\n",
                in_upcall->req.lookup.d_name,
                parent_refn.fs_id, Lu(parent_refn.handle));
            gossip_debug(GOSSIP_CLIENT_DEBUG,
                         "Lookup returned error code %d\n", ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_LOOKUP;
            out_downcall->status = ret;
            out_downcall->resp.lookup.refn.handle = 0;
            out_downcall->resp.lookup.refn.fs_id = 0;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_LOOKUP;
            out_downcall->status = 0;
            out_downcall->resp.lookup.refn = response.ref;
            ret = 0;
        }
    }
    return ret;
}

static int service_create_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_create response;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr *attrs = NULL;

    if (in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_create));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        attrs = &in_upcall->req.create.attributes;
	attrs->atime = attrs->mtime = attrs->ctime = 
	    time(NULL);

        parent_refn = in_upcall->req.create.parent_refn;

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a create request for %s (fsid %d | parent %Lu)\n",
            in_upcall->req.create.d_name,parent_refn.fs_id,
            Lu(parent_refn.handle));

        ret = PVFS_sys_create(in_upcall->req.create.d_name, parent_refn,
                              *attrs, in_upcall->credentials, NULL, &response);
        if (ret < 0)
        {
            /*
              if the create failed because the file already exists,
              do a (hopefully (d)cached) lookup here and return the
              pinode_reference along with success.

              this is useful for the case where the file was created
              before the pvfs2-client crashes; we want to report
              success on resume to the vfs that retried the operation.
            */
            if (ret == -PVFS_EEXIST)
            {
                PVFS_sysresp_lookup lk_response;
                memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));

                ret = PVFS_sys_ref_lookup(parent_refn.fs_id,
                                          in_upcall->req.create.d_name,
                                          parent_refn,
                                          in_upcall->credentials,
                                          &lk_response,
                                          PVFS2_LOOKUP_LINK_NO_FOLLOW);
                if (ret != 0)
                {
                    gossip_err("lookup on existing file failed; "
                               "aborting create\n");
                    goto create_failure;
                }
                else
                {
                    /* copy out the looked up pinode reference */
                    response.ref = lk_response.ref;
                    goto create_lookup_success;
                }
            }
          create_failure:
            gossip_err("Failed to create %s under %Lu on fsid %d!\n",
                       in_upcall->req.create.d_name,
                       Lu(parent_refn.handle), parent_refn.fs_id);
            gossip_err("Create returned error code %d\n",ret);
            PVFS_perror("File creation failed: ", ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_CREATE;
            out_downcall->status = ret;
            out_downcall->resp.create.refn.handle = 0;
            out_downcall->resp.create.refn.fs_id = 0;
        }
        else
        {
          create_lookup_success:
            out_downcall->type = PVFS2_VFS_OP_CREATE;
            out_downcall->status = 0;
            out_downcall->resp.create.refn = response.ref;
            ret = 0;
        }
    }
    return ret;
}

static int service_symlink_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_symlink response;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr *attrs = NULL;

    if (in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_symlink));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        attrs = &in_upcall->req.sym.attributes;
	attrs->atime = attrs->mtime = attrs->ctime = 
	    time(NULL);

        parent_refn = in_upcall->req.sym.parent_refn;

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a symlink request from %s (fsid %d | parent %Lu) to %s\n",
            in_upcall->req.sym.entry_name,parent_refn.fs_id,
            Lu(parent_refn.handle), in_upcall->req.sym.target);

        ret = PVFS_sys_symlink(in_upcall->req.sym.entry_name, parent_refn,
                               in_upcall->req.sym.target, *attrs,
                               in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("Failed to symlink %s to %s under %Lu on "
                       "fsid %d!\n", in_upcall->req.sym.entry_name,
                       in_upcall->req.sym.target,
                       Lu(parent_refn.handle), parent_refn.fs_id);
            gossip_err("Symlink returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_SYMLINK;
            out_downcall->status = ret;
            out_downcall->resp.sym.refn.handle = 0;
            out_downcall->resp.sym.refn.fs_id = 0;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_SYMLINK;
            out_downcall->status = 0;
            out_downcall->resp.sym.refn = response.ref;
            ret = 0;
        }
    }
    return ret;
}

#ifdef USE_MMAP_RA_CACHE
static int service_io_readahead_request(
    struct PVFS_dev_map_desc *desc,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_io response;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    void *buf = NULL, *tmp_buf = NULL;

    if (desc && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_io));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

	file_req = PVFS_BYTE;

        assert((in_upcall->req.io.buf_index > -1) &&
               (in_upcall->req.io.buf_index < PVFS2_BUFMAP_DESC_COUNT));

        tmp_buf = malloc(in_upcall->req.io.readahead_size);
        assert(tmp_buf);

        if ((in_upcall->req.io.io_type == PVFS_IO_READ) &&
            in_upcall->req.io.readahead_size)
        {
            int val = 0;

            /* check readahead cache first */
            if ((val = pvfs2_mmap_ra_cache_get_block(
                     in_upcall->req.io.refn, in_upcall->req.io.offset,
                     in_upcall->req.io.count, tmp_buf) == 0))
            {
                goto mmap_ra_cache_hit;
            }
            else if (val == -2)
            {
                /* check if we should flush stale cache data */
                pvfs2_mmap_ra_cache_flush(in_upcall->req.io.refn);

                free(tmp_buf);
                return ret;
            }

            /* make the full-blown readahead sized request */
            ret = PVFS_Request_contiguous(
                in_upcall->req.io.readahead_size, PVFS_BYTE, &mem_req);
        }
        else
        {
            free(tmp_buf);
            return ret;
        }
	assert(ret == 0);

	ret = PVFS_sys_io(
            in_upcall->req.io.refn, file_req, 0,
	    tmp_buf, mem_req, in_upcall->credentials, &response,
            in_upcall->req.io.io_type);
	if (ret < 0)
	{
	    /* report an error */
	    out_downcall->type = PVFS2_VFS_OP_FILE_IO;
	    out_downcall->status = ret;
	}
	else
	{
            /*
              now that we've read the data, insert it into
              the mmap_ra_cache here
            */
            pvfs2_mmap_ra_cache_register(
                in_upcall->req.io.refn, tmp_buf,
                in_upcall->req.io.readahead_size);

          mmap_ra_cache_hit:

            out_downcall->type = PVFS2_VFS_OP_FILE_IO;
            out_downcall->status = 0;
            out_downcall->resp.io.amt_complete =
                in_upcall->req.io.count;

            /*
              get a shared kernel/userspace buffer for
              the I/O transfer
            */
            buf = PINT_dev_get_mapped_buffer(
                desc, in_upcall->req.io.buf_index);
            assert(buf);

            /* copy cached data into the shared user/kernel space */
            memcpy(buf, tmp_buf, in_upcall->req.io.count);
            free(tmp_buf);
	    ret = 0;
	}
    }
    return(ret);
}
#endif /* USE_MMAP_RA_CACHE */

static int service_io_request(
    struct PVFS_dev_map_desc *desc,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_io response;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    void *buf = NULL;
    size_t amt_completed = 0;

    if (desc && in_upcall && out_downcall)
    {
#ifdef USE_MMAP_RA_CACHE
        if ((in_upcall->req.io.readahead_size ==
             PVFS2_MMAP_RACACHE_FLUSH))
        {
            pvfs2_mmap_ra_cache_flush(in_upcall->req.io.refn);
        }
        else if ((in_upcall->req.io.offset == (loff_t)0) &&
                 (in_upcall->req.io.readahead_size > 0) &&
                 (in_upcall->req.io.readahead_size <
                  PVFS2_MMAP_RACACHE_MAX_SIZE))
        {
            ret = service_io_readahead_request(
                desc, in_upcall, out_downcall);

            /*
              if the readahead request succeeds, return.
              otherwise fallback to normal servicing
            */
            if (ret == 0)
            {
                return ret;
            }
        }
#endif /* USE_MMAP_RA_CACHE */

        memset(&response,0,sizeof(PVFS_sysresp_io));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

	file_req = PVFS_BYTE;

	ret = PVFS_Request_contiguous(
            (int32_t)in_upcall->req.io.count, PVFS_BYTE, &mem_req);
	assert(ret == 0);

        assert((in_upcall->req.io.buf_index > -1) &&
               (in_upcall->req.io.buf_index < PVFS2_BUFMAP_DESC_COUNT));

        /* get a shared kernel/userspace buffer for the I/O transfer */
        buf = PINT_dev_get_mapped_buffer(
            desc, in_upcall->req.io.buf_index);
        assert(buf);

	ret = PVFS_sys_io(
            in_upcall->req.io.refn, file_req, in_upcall->req.io.offset, 
	    buf, mem_req, in_upcall->credentials, &response,
            in_upcall->req.io.io_type);

        amt_completed = (size_t)response.total_completed;

	if (ret < 0)
	{
	    /* report an error */
	    out_downcall->type = PVFS2_VFS_OP_FILE_IO;
	    out_downcall->status = ret;
	}
	else
	{
	    out_downcall->type = PVFS2_VFS_OP_FILE_IO;
	    out_downcall->status = 0;
	    out_downcall->resp.io.amt_complete = amt_completed;
	    ret = 0;
	}
    }
    return(ret);
}

static int service_getattr_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_getattr response;

    if (in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_getattr));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "got a getattr request for fsid %d | handle %Lu\n",
            in_upcall->req.getattr.refn.fs_id,
            Lu(in_upcall->req.getattr.refn.handle));

        ret = PVFS_sys_getattr(in_upcall->req.getattr.refn,
                               PVFS_ATTR_SYS_ALL,
                               in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("failed to getattr handle %Lu on fsid %d!\n",
                       Lu(in_upcall->req.getattr.refn.handle),
                       in_upcall->req.getattr.refn.fs_id);
            gossip_err("getattr returned error code %d\n",ret);
            PVFS_perror("Getattr failed ", ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_GETATTR;
            out_downcall->status = ret;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_GETATTR;
            out_downcall->status = 0;
            out_downcall->resp.getattr.attributes = response.attr;

            /*
              free allocated attr memory if required; to
              avoid copying the embedded link_target string inside
              the sys_attr object passed down into the vfs, we
              explicitly copy the link target (if any) into a
              reserved string space in the getattr downcall object
            */
            if ((response.attr.objtype == PVFS_TYPE_SYMLINK) &&
                (response.attr.mask & PVFS_ATTR_SYS_LNK_TARGET))
            {
                assert(response.attr.link_target);

                snprintf(out_downcall->resp.getattr.link_target,
                         PVFS2_NAME_LEN, "%s", response.attr.link_target);

                free(response.attr.link_target);
            }
            ret = 0;
        }
    }
    return ret;
}

static int service_setattr_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;

    if (in_upcall && out_downcall)
    {
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "got a setattr request for fsid %d | handle %Lu\n",
            in_upcall->req.setattr.refn.fs_id,
            Lu(in_upcall->req.setattr.refn.handle));

        ret = PVFS_sys_setattr(in_upcall->req.setattr.refn,
                               in_upcall->req.setattr.attributes,
                               in_upcall->credentials);
        if (ret < 0)
        {
            gossip_err("failed to setattr handle %Lu on fsid %d!\n",
                       Lu(in_upcall->req.setattr.refn.handle),
                       in_upcall->req.setattr.refn.fs_id);
            gossip_err("setattr returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_SETATTR;
            out_downcall->status = ret;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_SETATTR;
            out_downcall->status = 0;
            ret = 0;
        }
    }
    return ret;
}

static int service_remove_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_object_ref parent_refn;

    if (in_upcall && out_downcall)
    {
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        parent_refn = in_upcall->req.remove.parent_refn;

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a remove request for %s under fsid %d and "
            "handle %Lu\n", in_upcall->req.remove.d_name,
            parent_refn.fs_id, Lu(parent_refn.handle));

        ret = PVFS_sys_remove(in_upcall->req.remove.d_name,
                              parent_refn, in_upcall->credentials);
        if (ret < 0)
        {
            gossip_err("Failed to remove %s under handle %Lu "
                       "on fsid %d!\n", in_upcall->req.remove.d_name,
                       Lu(parent_refn.handle), parent_refn.fs_id);
            gossip_err("Remove returned error code %d\n",ret);
            PVFS_perror("Remove failed ",ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_REMOVE;
            out_downcall->status = ret;
        }
        else
        {
            /* we need to send a blank success response */
            out_downcall->type = PVFS2_VFS_OP_REMOVE;
            out_downcall->status = 0;
            ret = 0;
        }
    }
    return ret;
}

static int service_mkdir_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_mkdir response;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr *attrs = NULL;

    if (in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_mkdir));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        attrs = &in_upcall->req.mkdir.attributes;
	attrs->atime = attrs->mtime = attrs->ctime = 
	    time(NULL);

        parent_refn = in_upcall->req.mkdir.parent_refn;

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a mkdir request for %s (fsid %d | parent %Lu)\n",
            in_upcall->req.mkdir.d_name,parent_refn.fs_id,
            Lu(parent_refn.handle));

        ret = PVFS_sys_mkdir(in_upcall->req.mkdir.d_name, parent_refn,
                             *attrs, in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("Failed to mkdir %s under %Lu on fsid %d!\n",
                       in_upcall->req.mkdir.d_name,
                       Lu(parent_refn.handle), parent_refn.fs_id);
            gossip_err("Mkdir returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_MKDIR;
            out_downcall->status = ret;
            out_downcall->resp.mkdir.refn.handle = 0;
            out_downcall->resp.mkdir.refn.fs_id = 0;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_MKDIR;
            out_downcall->status = 0;
            out_downcall->resp.mkdir.refn = response.ref;
            ret = 0;
        }
    }
    return ret;
}

static int service_readdir_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_readdir response;
    PVFS_object_ref refn;

    if (in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_readdir));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        refn = in_upcall->req.readdir.refn;

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a readdir request for fsid %d | parent %Lu "
            "(token is %d)\n", refn.fs_id, Lu(refn.handle),
            in_upcall->req.readdir.token);

        ret = PVFS_sys_readdir(refn, in_upcall->req.readdir.token,
                               in_upcall->req.readdir.max_dirent_count,
                               in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("Failed to readdir under %Lu on fsid %d!\n",
                       Lu(refn.handle), refn.fs_id);
            gossip_err("Readdir returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_READDIR;
            out_downcall->status = ret;
            out_downcall->resp.readdir.dirent_count = 0;
        }
        else
        {
            int i = 0, len = 0;
            out_downcall->type = PVFS2_VFS_OP_READDIR;
            out_downcall->status = 0;

            out_downcall->resp.readdir.token = response.token;

            for(i = 0; i < response.pvfs_dirent_outcount; i++)
            {
                out_downcall->resp.readdir.refn[i].handle =
                    response.dirent_array[i].handle;
                out_downcall->resp.readdir.refn[i].fs_id = refn.fs_id;

                len = strlen(response.dirent_array[i].d_name);
                out_downcall->resp.readdir.d_name_len[i] = len;
                strncpy(&out_downcall->resp.readdir.d_name[i][0],
                        response.dirent_array[i].d_name,len);

                out_downcall->resp.readdir.dirent_count++;
            }

            if (out_downcall->resp.readdir.dirent_count ==
                response.pvfs_dirent_outcount)
            {
                ret = 0;
            }
            else
            {
                gossip_err("DIRENT COUNTS DON'T MATCH (%d != %d)\n",
                           out_downcall->resp.readdir.dirent_count,
                           response.pvfs_dirent_outcount);
            }
        }
    }
    return ret;
}

static int service_rename_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_object_ref old_parent_refn, new_parent_refn;

    if (in_upcall && out_downcall)
    {
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        old_parent_refn = in_upcall->req.rename.old_parent_refn;
        new_parent_refn = in_upcall->req.rename.new_parent_refn;

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a rename request for %s under fsid %d and "
            "handle %Lu to be %s under fsid %d and handle %Lu\n",
            in_upcall->req.rename.d_old_name,
            old_parent_refn.fs_id, Lu(old_parent_refn.handle),
            in_upcall->req.rename.d_new_name,
            new_parent_refn.fs_id, Lu(new_parent_refn.handle));

        ret = PVFS_sys_rename(in_upcall->req.rename.d_old_name,
                              in_upcall->req.rename.old_parent_refn,
                              in_upcall->req.rename.d_new_name,
                              in_upcall->req.rename.new_parent_refn,
                              in_upcall->credentials);
        if (ret < 0)
        {
            gossip_err("Failed to rename %s to %s\n",
                       in_upcall->req.rename.d_old_name,
                       in_upcall->req.rename.d_new_name);
            gossip_err("Rename returned error code %d\n", ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_RENAME;
            out_downcall->status = ret;
        }
        else
        {
            /* we need to send a blank success response */
            out_downcall->type = PVFS2_VFS_OP_RENAME;
            out_downcall->status = 0;
            ret = 0;
        }
    }
    return ret;
}

static int service_statfs_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_statfs resp_statfs;

    if (in_upcall && out_downcall)
    {
        memset(&resp_statfs,0,sizeof(PVFS_sysresp_statfs));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(GOSSIP_CLIENT_DEBUG, "Got a statfs request for fsid %d\n",
                     in_upcall->req.statfs.fs_id);

        ret = PVFS_sys_statfs(in_upcall->req.statfs.fs_id,
                              in_upcall->credentials,
                              &resp_statfs);
        if (ret < 0)
        {
            gossip_err("Failed to statfs fsid %d\n",
                       in_upcall->req.statfs.fs_id);
            gossip_err("Statfs returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_STATFS;
            out_downcall->status = ret;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_STATFS;
            out_downcall->status = 0;

            out_downcall->resp.statfs.block_size =
                STATFS_DEFAULT_BLOCKSIZE;
            out_downcall->resp.statfs.blocks_total = (long)
                (resp_statfs.statfs_buf.bytes_total /
                 out_downcall->resp.statfs.block_size);
            out_downcall->resp.statfs.blocks_avail = (long)
                (resp_statfs.statfs_buf.bytes_available /
                 out_downcall->resp.statfs.block_size);
            /*
              these values really represent handle/inode counts
              rather than an accurate number of files
            */
            out_downcall->resp.statfs.files_total = (long) 
                resp_statfs.statfs_buf.handles_total_count;
            out_downcall->resp.statfs.files_avail = (long)
                resp_statfs.statfs_buf.handles_available_count;

            ret = 0;
        }
    }
    return ret;
}

static int service_truncate_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;

    if (in_upcall && out_downcall)
    {
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a truncate request for %Lu under fsid %d to be "
            "size %Ld\n", Lu(in_upcall->req.truncate.refn.handle),
            in_upcall->req.truncate.refn.fs_id,
            Ld(in_upcall->req.truncate.size));

        ret = PVFS_sys_truncate(in_upcall->req.truncate.refn,
                                in_upcall->req.truncate.size,
                                in_upcall->credentials);
        if (ret < 0)
        {
            gossip_err("Failed to truncate %Lu on %d\n",
                       Lu(in_upcall->req.truncate.refn.handle),
                       in_upcall->req.truncate.refn.fs_id);
            gossip_err("Truncate returned error code %d\n", ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_TRUNCATE;
            out_downcall->status = ret;
        }
        else
        {
            /* we need to send a blank success response */
            out_downcall->type = PVFS2_VFS_OP_TRUNCATE;
            out_downcall->status = 0;
            ret = 0;
        }
    }
    return ret;
}

#ifdef USE_MMAP_RA_CACHE
static int service_mmap_ra_flush_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;

    if (in_upcall && out_downcall)
    {
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Got a mmap racache flush request for %Lu under fsid %d\n",
            Lu(in_upcall->req.ra_cache_flush.refn.handle),
            in_upcall->req.ra_cache_flush.refn.fs_id);

        /* flush cached data if any */
        pvfs2_mmap_ra_cache_flush(in_upcall->req.io.refn);

        /* we need to send a blank success response */
        out_downcall->type = PVFS2_VFS_OP_MMAP_RA_FLUSH;
        out_downcall->status = 0;
        ret = 0;
    }
    return ret;
}
#endif

#define generate_upcall_mntent(mntent, in_upcall, mount)              \
do {                                                                  \
    /*                                                                \
      generate a unique dynamic mount point; the id will be passed to \
      the kernel via the downcall so we can match it with a proper    \
      unmount request at unmount time.  if we're unmounting, use the  \
      passed in id from the upcall                                    \
    */                                                                \
    if (mount)                                                        \
        snprintf(buf, PATH_MAX, "<DYNAMIC-%d>", dynamic_mount_id);    \
    else                                                              \
        snprintf(buf, PATH_MAX, "<DYNAMIC-%d>",                       \
                 in_upcall->req.fs_umount.id);                        \
                                                                      \
    mntent.mnt_dir = strdup(buf);                                     \
    if (!mntent.mnt_dir)                                              \
    {                                                                 \
        ret = -PVFS_ENOMEM;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
                                                                      \
    gossip_debug(GOSSIP_CLIENT_DEBUG, "Using %s Point %s\n",          \
                 (mount ? "Mount" : "Unmount"), mntent.mnt_dir);      \
                                                                      \
    if (mount)                                                        \
        ptr = rindex(in_upcall->req.fs_mount.pvfs2_config_server,     \
                     (int)'/');                                       \
    else                                                              \
        ptr = rindex(in_upcall->req.fs_umount.pvfs2_config_server,    \
                     (int)'/');                                       \
                                                                      \
    if (!ptr)                                                         \
    {                                                                 \
        gossip_err("Configuration server MUST be of the form "        \
                   "protocol://address/fs_name\n");                   \
        ret = -PVFS_EINVAL;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
    *ptr = '\0';                                                      \
    ptr++;                                                            \
                                                                      \
    if (mount)                                                        \
        mntent.pvfs_config_server = strdup(                           \
            in_upcall->req.fs_mount.pvfs2_config_server);             \
    else                                                              \
        mntent.pvfs_config_server = strdup(                           \
            in_upcall->req.fs_umount.pvfs2_config_server);            \
                                                                      \
    if (!mntent.pvfs_config_server)                                   \
    {                                                                 \
        ret = -PVFS_ENOMEM;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
                                                                      \
    gossip_debug(                                                     \
        GOSSIP_CLIENT_DEBUG, "Got Configuration Server: %s "          \
        "(len=%d)\n", mntent.pvfs_config_server,                      \
        strlen(mntent.pvfs_config_server));                           \
                                                                      \
    mntent.pvfs_fs_name = strdup(ptr);                                \
    if (!mntent.pvfs_config_server)                                   \
    {                                                                 \
        ret = -PVFS_ENOMEM;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
                                                                      \
    gossip_debug(                                                     \
        GOSSIP_CLIENT_DEBUG, "Got FS Name: %s (len=%d)\n",            \
        mntent.pvfs_fs_name, strlen(mntent.pvfs_fs_name));            \
                                                                      \
    mntent.encoding = ENCODING_DEFAULT;                               \
    mntent.flowproto = FLOWPROTO_DEFAULT;                             \
                                                                      \
    /* also fill in the fs_id for umount */                           \
    if (!mount)                                                       \
        mntent.fs_id = in_upcall->req.fs_umount.fs_id;                \
                                                                      \
    ret = 0;                                                          \
} while(0)

static int service_fs_mount_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    struct PVFS_sys_mntent mntent;
    PVFS_handle root_handle;
    char *ptr = NULL;
    char buf[PATH_MAX] = {0};

    if (in_upcall && out_downcall)
    {
        /*
          since we got a mount request from the vfs, we know that some
          mntent entries are not filled in, so add some defaults here
          if they weren't passed in the options.
        */
        memset(&mntent, 0, sizeof(struct PVFS_sys_mntent));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(GOSSIP_CLIENT_DEBUG,
                     "Got an fs mount request via host %s\n",
                     in_upcall->req.fs_mount.pvfs2_config_server);

        generate_upcall_mntent(mntent, in_upcall, 1);

        ret = PVFS_sys_fs_add(&mntent);

        if (ret < 0)
        {
          fail_downcall:
            gossip_err("Failed to mount via host %s\n",
                       in_upcall->req.fs_mount.pvfs2_config_server);
            PVFS_perror("Mount failure", ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_FS_MOUNT;
            out_downcall->status = ret;
        }
        else
        {
            /*
              we need to send a blank success response, but first we
              need to resolve the root handle, given the previously
              resolved fs_id
            */
            if (PINT_bucket_get_root_handle(mntent.fs_id, &root_handle))
            {
                gossip_err("Failed to retrieve root handle for "
                           "resolved fs_id %d\n",mntent.fs_id);
                goto fail_downcall;
            }
            
            gossip_debug(GOSSIP_CLIENT_DEBUG,
                         "FS mount got root handle %Lu on fs id %d\n",
                         root_handle, mntent.fs_id);

            out_downcall->type = PVFS2_VFS_OP_FS_MOUNT;
            out_downcall->status = 0;
            out_downcall->resp.fs_mount.fs_id = mntent.fs_id;
            out_downcall->resp.fs_mount.root_handle = root_handle;
            out_downcall->resp.fs_mount.id = dynamic_mount_id++;
            ret = 0;
        }

        PVFS_sys_free_mntent(&mntent);
    }
    return ret;
}

static int service_fs_umount_request(
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = -PVFS_ENODEV;
    struct PVFS_sys_mntent mntent;
    char *ptr = NULL;
    char buf[PATH_MAX] = {0};

    if (in_upcall && out_downcall)
    {
        /*
          since we got a umount request from the vfs, we know that
          some mntent entries are not filled in, so add some defaults
          here if they weren't passed in the options.
        */
        memset(&mntent, 0, sizeof(struct PVFS_sys_mntent));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(GOSSIP_CLIENT_DEBUG,
                     "Got an fs umount request via host %s\n",
                     in_upcall->req.fs_umount.pvfs2_config_server);

        generate_upcall_mntent(mntent, in_upcall, 0);

        ret = PVFS_sys_fs_remove(&mntent);

        if (ret < 0)
        {
          fail_downcall:
            gossip_err("Failed to umount via host %s\n",
                       in_upcall->req.fs_umount.pvfs2_config_server);
            PVFS_perror("UMOUNT failure", ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_FS_UMOUNT;
            out_downcall->status = ret;
        }
        else
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG, "FS umount ok\n");

            out_downcall->type = PVFS2_VFS_OP_FS_MOUNT;
            out_downcall->status = 0;
            ret = 0;
        }

        PVFS_sys_free_mntent(&mntent);
    }
    return ret;
}

int write_device_response(
    void *buffer_list,
    int *size_list,
    int list_size,
    int total_size,
    PVFS_id_gen_t tag,
    job_id_t *job_id,
    job_status_s *jstat,
    job_context_id context)
{
    int ret = -1;
    int outcount = 0;

    if (buffer_list && size_list && list_size &&
        total_size && (list_size < MAX_LIST_SIZE))
    {
        ret = job_dev_write_list(buffer_list, size_list, list_size,
                                 total_size, tag, PINT_DEV_EXT_ALLOC,
                                 NULL, 0, jstat, job_id, context);
        if (ret < 0)
        {
            PVFS_perror("job_dev_write_list()", ret);
            goto exit_point;
        }
        else if (ret == 0)
        {
	    ret = job_test(*job_id, &outcount, NULL, jstat, -1, context);
            if (ret < 0)
            {
                PVFS_perror("job_test()", ret);
                goto exit_point;
            }
        }

        if (jstat->error_code != 0)
        {
            PVFS_perror("job_bmi_write_list() error code",
                        jstat->error_code);
            ret = -1;
        }
    }
  exit_point:
    return ret;
}

void *vfs_request_handler(void *ptr)
{
    void *buffer_list[MAX_LIST_SIZE];
    int size_list[MAX_LIST_SIZE];
    int list_size = 0, total_size = 0;
    unsigned long tag = 0;
    vfs_request_t *vfs_request = (vfs_request_t *)ptr;
    assert(vfs_request);

    gossip_debug(GOSSIP_CLIENT_DEBUG, "Got message: size: %d, tag: %d, "
                 "payload: %p\n", vfs_request->info.size,
                 (int)vfs_request->info.tag, vfs_request->info.buffer);

    tag = (unsigned long)vfs_request->info.tag;
    if (vfs_request->info.size >= sizeof(pvfs2_upcall_t))
    {
        memcpy(&vfs_request->in_upcall, vfs_request->info.buffer,
               sizeof(pvfs2_upcall_t));
    }
    else
    {
        gossip_err("Error! Short read from device -- "
                   "What does this mean?\n");
        free(vfs_request);
        return (void *)-1;
    }

    list_size = 0;
    total_size = 0;

    switch(vfs_request->in_upcall.type)
    {
        case PVFS2_VFS_OP_LOOKUP:
            service_lookup_request(&vfs_request->in_upcall,
                                   &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_CREATE:
            service_create_request(&vfs_request->in_upcall,
                                   &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_SYMLINK:
            service_symlink_request(&vfs_request->in_upcall,
                                    &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_GETATTR:
            service_getattr_request(&vfs_request->in_upcall,
                                    &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_SETATTR:
            service_setattr_request(&vfs_request->in_upcall,
                                    &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_REMOVE:
            service_remove_request(&vfs_request->in_upcall,
                                   &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_MKDIR:
            service_mkdir_request(&vfs_request->in_upcall,
                                  &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_READDIR:
            service_readdir_request(&vfs_request->in_upcall,
                                    &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_FILE_IO:
            service_io_request(vfs_request->desc,
                               &vfs_request->in_upcall,
                               &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_RENAME:
            service_rename_request(&vfs_request->in_upcall,
                                   &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_STATFS:
            service_statfs_request(&vfs_request->in_upcall,
                                   &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_TRUNCATE:
            service_truncate_request(&vfs_request->in_upcall,
                                     &vfs_request->out_downcall);
            break;
#ifdef USE_MMAP_RA_CACHE
        case PVFS2_VFS_OP_MMAP_RA_FLUSH:
            service_mmap_ra_flush_request(&vfs_request->in_upcall,
                                          &vfs_request->out_downcall);
            break;
#endif
        case PVFS2_VFS_OP_FS_MOUNT:
            service_fs_mount_request(&vfs_request->in_upcall,
                                     &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_FS_UMOUNT:
            service_fs_umount_request(&vfs_request->in_upcall,
                                      &vfs_request->out_downcall);
            break;
        case PVFS2_VFS_OP_INVALID:
        default:
            gossip_err("Got an unrecognized vfs operation of "
                       "type %x.\n", vfs_request->in_upcall.type);
    }

    PINT_dev_release_unexpected(&vfs_request->info);

    buffer_list[0] = &vfs_request->out_downcall;
    size_list[0] = sizeof(pvfs2_downcall_t);
    total_size = sizeof(pvfs2_downcall_t);
    list_size = 1;

    if (write_device_response(buffer_list,size_list,list_size,
                              total_size,(PVFS_id_gen_t)tag,
                              &vfs_request->job_id,
                              &vfs_request->jstat,
                              vfs_request->context) < 0)
    {
        gossip_err("write_device_response failed on tag %lu\n",tag);
    }
    free(vfs_request);

    gossip_debug(GOSSIP_CLIENT_DEBUG, "threaded handler returning\n");
    return NULL;
}


int main(int argc, char **argv)
{
    int ret = 1, i = 0, err = 0;
    struct rlimit lim = {0,0};
    job_context_id context;
    job_id_t job_id;
    job_status_s jstat;
    struct PVFS_dev_map_desc desc;
    vfs_request_t *vfs_request = NULL;
    pthread_t thread_ids[MAX_NUM_THREADS];
    int thread_index = 0;

    /* set rlimit to prevent core files */
    ret = setrlimit(RLIMIT_CORE, &lim);
    if (ret < 0)
    {
	perror("rlimit");
	fprintf(stderr, "continuing...\n");
    }

    /*
      initialize pvfs system interface

      NOTE: we do not rely on a pvfstab file at all in here, as
      mounting a pvfs2 volume through the kernel interface now
      requires you to specify a server and fs name in the form of:

      protocol://server/fs_name

      At mount time, we dynamically resolve and add the file
      systems/mount information
    */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if(ret < 0)
    {
        return(ret);
    }

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_initialize();
#endif

    PINT_acache_set_timeout(ACACHE_TIMEOUT_MS);

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if (ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return(-1);
    }

    /* setup a mapped region for I/O transfers */
    memset(&desc, 0 , sizeof(struct PVFS_dev_map_desc));
    ret = PINT_dev_get_mapped_region(&desc, PVFS2_BUFMAP_TOTAL_SIZE);
    if (ret < 0)
    {
	PVFS_perror("PINT_dev_get_mapped_region", ret);
	return(-1);
    }

    ret = job_open_context(&context);
    if (ret < 0)
    {
	PVFS_perror("job_open_context", ret);
	return(-1);
    }

    /*
      lock the remount mutex to make sure the remount isn't started
      until we're ready
    */
    pthread_mutex_lock(&remount_mutex);

    if (pthread_create(&remount_thread, NULL, exec_remount, NULL))
    {
	gossip_err("Cannot create remount thread!");
	return(-1);
    }

    while(1)
    {
        int outcount = 0;
        static int remounted_already = 0;

        /*
          signal the remount thread to go ahead with the remount
          attempts (and make sure it's done only once)
        */
        if (!remounted_already)
        {
            pthread_mutex_unlock(&remount_mutex);
            remounted_already = 1;
        }

        vfs_request = (vfs_request_t *)malloc(sizeof(vfs_request_t));
        assert(vfs_request);

        memset(vfs_request, 0, sizeof(vfs_request));

	ret = job_dev_unexp(&vfs_request->info, NULL, 0,
                            &jstat, &job_id, context);
	if (ret == 0)
	{
	    ret = job_test(job_id, &outcount, NULL, &jstat, -1, context);
            if (ret < 0)
            {
                PVFS_perror("job_test()", ret);
                return(-1);
            }
	}
	else if (ret < 0)
	{
	    PVFS_perror("job_dev_unexp()", ret);
	    return(-1);
	}

	if (jstat.error_code != 0)
	{
	    PVFS_perror("job error code", jstat.error_code);
	    return(-1);
	}

        vfs_request->job_id = job_id;
        vfs_request->jstat = jstat;
        vfs_request->context = context;
        vfs_request->desc = &desc;

        if (thread_index == MAX_NUM_THREADS)
        {
            gossip_debug(GOSSIP_CLIENT_DEBUG,
                         "*** Waiting for threads to return\n");

            for(i = 0; i < MAX_NUM_THREADS; i++)
            {
                ret = pthread_join(thread_ids[i], NULL);
                err = errno;
                if (ret && (err != ESRCH))
                {
                    gossip_err("Failed to join handler thread %d\n",i);
                    break;
                }
            }
            thread_index = 0;
        }
        ret = pthread_create(&thread_ids[thread_index++], NULL,
                             vfs_request_handler, (void *)vfs_request);
        if (ret)
        {
            gossip_err("Failed to spawn new request handler thread\n");
            break;
        }
    }

    /* join the remount thread, which should long be done by now */
    pthread_join(remount_thread, NULL);

    job_close_context(context);

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_finalize();
#endif

    if (PVFS_sys_finalize())
    {
        gossip_err("Failed to finalize PVFS\n");
        return 1;
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
