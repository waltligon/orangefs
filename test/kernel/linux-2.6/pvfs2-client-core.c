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

#include "gossip.h"
#include "pint-dev.h"
#include "job.h"
#include "acache.h"

#include "client.h"

#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-util.h"

/* comment out to disable the mmap readahead cache */
#define USE_MMAP_RA_CACHE

#ifdef USE_MMAP_RA_CACHE
#include "mmap-ra-cache.h"
#endif

/*
  an arbitrary limit to the max number of items
  we can write into the device file as a response
*/
#define MAX_LIST_SIZE    32

#define MAX_NUM_UNEXPECTED 10

/* the block size to report in statfs */
#define STATFS_DEFAULT_BLOCKSIZE  1024

/* small default attribute cache timeout; effectively disabled */
#define ACACHE_TIMEOUT_MS 1

static int service_lookup_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_lookup response;
    PVFS_pinode_reference parent_refn;

    if (init_response && in_upcall && out_downcall)
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
            out_downcall->resp.lookup.refn = response.pinode_refn;
            ret = 0;
        }
    }
    return ret;
}

static int service_create_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_create response;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr *attrs = NULL;

    if (init_response && in_upcall && out_downcall)
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
                              *attrs, in_upcall->credentials, &response);
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
                    response.pinode_refn = lk_response.pinode_refn;
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
            out_downcall->resp.create.refn = response.pinode_refn;
            ret = 0;
        }
    }
    return ret;
}

static int service_symlink_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_symlink response;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr *attrs = NULL;

    if (init_response && in_upcall && out_downcall)
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
            out_downcall->resp.sym.refn = response.pinode_refn;
            ret = 0;
        }
    }
    return ret;
}

#ifdef USE_MMAP_RA_CACHE
static int service_io_readahead_request(
    struct PVFS_dev_map_desc *desc,
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_io response;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    void *buf = NULL, *tmp_buf = NULL;

    if (desc && init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_io response;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    void *buf = NULL;
    size_t amt_completed = 0;

    if (desc && init_response && in_upcall && out_downcall)
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
                desc, init_response, in_upcall, out_downcall);

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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_getattr response;

    if (init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;

    if (init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_pinode_reference parent_refn;

    if (init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_mkdir response;
    PVFS_pinode_reference parent_refn;
    PVFS_sys_attr *attrs = NULL;

    if (init_response && in_upcall && out_downcall)
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
            out_downcall->resp.mkdir.refn = response.pinode_refn;
            ret = 0;
        }
    }
    return ret;
}

static int service_readdir_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_readdir response;
    PVFS_pinode_reference refn;

    if (init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_pinode_reference old_parent_refn, new_parent_refn;

    if (init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_statfs resp_statfs;

    if (init_response && in_upcall && out_downcall)
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
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;

    if (init_response && in_upcall && out_downcall)
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


int main(int argc, char **argv)
{
    int ret = 1;
    void* buffer_list[MAX_LIST_SIZE];
    int size_list[MAX_LIST_SIZE];
    int list_size = 0, total_size = 0;
    struct rlimit lim = {0,0};

    job_context_id context;

    job_id_t job_id;
    job_status_s jstat;
    struct PINT_dev_unexp_info info;
    struct PVFS_dev_map_desc desc;

    unsigned long tag = 0;
    pvfs2_upcall_t upcall;
    pvfs2_downcall_t downcall;

    const PVFS_util_tab* tab;
    PVFS_sysresp_init init_response;

    /* set rlimit to prevent core files */
    ret = setrlimit(RLIMIT_CORE, &lim);
    if(ret < 0)
    {
	perror("rlimit");
	fprintf(stderr, "continuing...\n");
    }

    tab = PVFS_util_parse_pvfstab(NULL);
    if(!tab)
    {
        fprintf(stderr, "Error parsing pvfstab!\n");
        return 1;
    }

    memset(&init_response,0,sizeof(PVFS_sysresp_init));
    if (PVFS_sys_initialize(*tab, 0, &init_response))
    {
        fprintf(stderr, "Cannot initialize system interface\n");
        return 1;
    }

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_initialize();
#endif

    PINT_acache_set_timeout(ACACHE_TIMEOUT_MS);

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if(ret < 0)
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

    while(1)
    {
        int outcount = 0;

	ret = job_dev_unexp(&info, NULL, 0, &jstat, &job_id, context);
	if(ret == 0)
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

	if(jstat.error_code != 0)
	{
	    PVFS_perror("job error code", jstat.error_code);
	    return(-1);
	}

        gossip_debug(GOSSIP_CLIENT_DEBUG,
                     "Got message: size: %d, tag: %d, payload: %p\n",
                     info.size, (int)info.tag, info.buffer);

	tag = (unsigned long)info.tag;
	if (info.size >= sizeof(pvfs2_upcall_t))
	{
	    memcpy(&upcall,info.buffer,sizeof(pvfs2_upcall_t));
	}
	else
	{
	    gossip_err("Error! Short read from device -- "
                       "What does this mean?\n");
	    return(-1);
	}

	list_size = 0;
	total_size = 0;

	switch(upcall.type)
	{
	    case PVFS2_VFS_OP_LOOKUP:
		service_lookup_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_CREATE:
		service_create_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_SYMLINK:
		service_symlink_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_GETATTR:
		service_getattr_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_SETATTR:
		service_setattr_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_REMOVE:
		service_remove_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_MKDIR:
		service_mkdir_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_READDIR:
		service_readdir_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_FILE_IO:
		service_io_request(&desc, &init_response,
                                   &upcall, &downcall);
		break;
	    case PVFS2_VFS_OP_RENAME:
		service_rename_request(&init_response, &upcall, &downcall);
		break;
	    case PVFS2_VFS_OP_STATFS:
		service_statfs_request(&init_response, &upcall, &downcall);
		break;
	    case PVFS2_VFS_OP_TRUNCATE:
		service_truncate_request(&init_response, &upcall, &downcall);
		break;
	    case PVFS2_VFS_OP_INVALID:
	    default:
		gossip_err("Got an unrecognized vfs operation of "
                           "type %x.\n", upcall.type);
	}

	PINT_dev_release_unexpected(&info);

	/* prepare to write response */
	buffer_list[0] = &downcall;
	size_list[0] = sizeof(pvfs2_downcall_t);
	total_size = sizeof(pvfs2_downcall_t);
	list_size = 1;

	if (write_device_response(buffer_list,size_list,list_size,
				  total_size,(PVFS_id_gen_t)tag,
				  &job_id,&jstat,context) < 0)
	{
	    gossip_err("write_device_response failed on tag %lu\n",tag);
	}
    }

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
