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

#include "gossip.h"
#include "pint-dev.h"
#include "job.h"

#include "client.h"

#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"

/*
  an arbitrary limit to the max number of items
  we can write into the device file as a response
*/
#define MAX_LIST_SIZE    32

#define MAX_NUM_UNEXPECTED 10

/* size of mapped region to use for I/O transfers (in bytes) */
#define MAPPED_REGION_SIZE (16*1024*1024)

extern int parse_pvfstab(char *fn, pvfs_mntlist *mnt);


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
            CLIENT_DEBUG,
            "Got a lookup request for %s (fsid %d | parent %Ld)\n",
            in_upcall->req.lookup.d_name,
            in_upcall->req.lookup.parent_refn.fs_id,
            in_upcall->req.lookup.parent_refn.handle);

        parent_refn = in_upcall->req.lookup.parent_refn;

        ret = PVFS_sys_ref_lookup(parent_refn.fs_id,
                                  in_upcall->req.lookup.d_name,
                                  parent_refn, in_upcall->credentials,
                                  &response);
        if (ret < 0)
        {
            gossip_err("Failed to lookup %s (fsid %d | parent is %Ld)!\n",
                       in_upcall->req.lookup.d_name,
                       parent_refn.fs_id,parent_refn.handle);
            gossip_err("Lookup returned error code %d\n", ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_LOOKUP;
            out_downcall->status = -1;
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
            CLIENT_DEBUG,
            "Got a create request for %s (fsid %d | parent %Ld)\n",
            in_upcall->req.create.d_name,parent_refn.fs_id,
            parent_refn.handle);

        ret = PVFS_sys_create(in_upcall->req.create.d_name, parent_refn,
                              *attrs, in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("Failed to create %s under %Ld on fsid %d!\n",
                       in_upcall->req.create.d_name,
                       parent_refn.handle,parent_refn.fs_id);
            gossip_err("Create returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_CREATE;
            out_downcall->status = -1;
            out_downcall->resp.create.refn.handle = 0;
            out_downcall->resp.create.refn.fs_id = 0;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_CREATE;
            out_downcall->status = 0;
            out_downcall->resp.create.refn = response.pinode_refn;
            ret = 0;
        }
    }
    return ret;
}

static int service_io_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_io response;
    PVFS_Request io_req;
    PVFS_size displacement = 0;
    int32_t blocklength = 0;

    if(init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_io));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

	displacement = in_upcall->req.io.offset;
	blocklength = in_upcall->req.io.count;

	ret = PVFS_Request_indexed(1, &blocklength, &displacement,
                                   PVFS_BYTE, &io_req);
	assert(ret == 0);

	ret = PVFS_sys_io(
            in_upcall->req.io.refn, io_req, 0, 
	    in_upcall->req.io.buf, in_upcall->req.io.count,
            in_upcall->credentials, &response, in_upcall->req.io.io_type);
	if(ret < 0)
	{
	    /* report an error */
	    out_downcall->type = PVFS2_VFS_OP_FILE_IO;
	    out_downcall->status = ret;
	}
	else
	{
	    out_downcall->type = PVFS2_VFS_OP_FILE_IO;
	    out_downcall->status = 0;
	    out_downcall->resp.io.amt_complete = response.total_completed;
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
    uint32_t attrmask = PVFS_ATTR_SYS_ALL;
    PVFS_sysresp_getattr response;

    if (init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_getattr));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        gossip_debug(
            CLIENT_DEBUG,
            "got a getattr request for fsid %d | handle %Ld\n",
            in_upcall->req.getattr.refn.fs_id,
            in_upcall->req.getattr.refn.handle);

        ret = PVFS_sys_getattr(in_upcall->req.getattr.refn, attrmask,
                               in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("failed to getattr handle %Ld on fsid %d!\n",
                       in_upcall->req.getattr.refn.handle,
                       in_upcall->req.getattr.refn.fs_id);
            gossip_err("getattr returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_GETATTR;
            out_downcall->status = -1;
        }
        else
        {
            out_downcall->type = PVFS2_VFS_OP_GETATTR;
            out_downcall->status = 0;
            out_downcall->resp.getattr.attributes = response.attr;
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
            CLIENT_DEBUG,
            "got a setattr request for fsid %d | handle %Ld\n",
            in_upcall->req.setattr.refn.fs_id,
            in_upcall->req.setattr.refn.handle);

        ret = PVFS_sys_setattr(in_upcall->req.setattr.refn,
                               in_upcall->req.setattr.attributes,
                               in_upcall->credentials);
        if (ret < 0)
        {
            gossip_err("failed to setattr handle %Ld on fsid %d!\n",
                       in_upcall->req.setattr.refn.handle,
                       in_upcall->req.setattr.refn.fs_id);
            gossip_err("setattr returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_SETATTR;
            out_downcall->status = -1;
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
            CLIENT_DEBUG,
            "Got a remove request for %s under fsid %d and "
            "handle %Ld\n", in_upcall->req.remove.d_name,
            parent_refn.fs_id, parent_refn.handle);

        ret = PVFS_sys_remove(in_upcall->req.remove.d_name,
                              parent_refn, in_upcall->credentials);
        if (ret < 0)
        {
            gossip_err("Failed to remove %s under handle %Ld "
                       "on fsid %d!\n", in_upcall->req.remove.d_name,
                       parent_refn.handle, parent_refn.fs_id);
            gossip_err("Remove returned error code %d\n",ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_REMOVE;
            out_downcall->status = -1;
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
            CLIENT_DEBUG,
            "Got a mkdir request for %s (fsid %d | parent %Ld)\n",
            in_upcall->req.mkdir.d_name,parent_refn.fs_id,
            parent_refn.handle);

        ret = PVFS_sys_mkdir(in_upcall->req.mkdir.d_name, parent_refn,
                             *attrs, in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("Failed to mkdir %s under %Ld on fsid %d!\n",
                       in_upcall->req.mkdir.d_name,
                       parent_refn.handle,parent_refn.fs_id);
            gossip_err("Mkdir returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_MKDIR;
            out_downcall->status = -1;
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
            CLIENT_DEBUG,
            "Got a readdir request for fsid %d | parent %Ld\n",
            refn.fs_id, refn.handle);

        ret = PVFS_sys_readdir(refn, in_upcall->req.readdir.token,
                               in_upcall->req.readdir.max_dirent_count,
                               in_upcall->credentials, &response);
        if (ret < 0)
        {
            gossip_err("Failed to readdir under %Ld on fsid %d!\n",
                       refn.handle, refn.fs_id);
            gossip_err("Readdir returned error code %d\n",ret);

            /* we need to send a blank response */
            out_downcall->type = PVFS2_VFS_OP_READDIR;
            out_downcall->status = -1;
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
                                 NULL, jstat, job_id, context);
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
    void* mapped_region = NULL;

    job_context_id context;

    job_id_t job_id;
    job_status_s jstat;
    struct PINT_dev_unexp_info info;

    unsigned long tag = 0;
    pvfs2_upcall_t upcall;
    pvfs2_downcall_t downcall;

    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init init_response;

    if (parse_pvfstab(NULL,&mnt))
    {
        fprintf(stderr, "Error parsing pvfstab!\n");
        return 1;
    }

    memset(&init_response,0,sizeof(PVFS_sysresp_init));
    if (PVFS_sys_initialize(mnt, CLIENT_DEBUG, &init_response))
    {
        fprintf(stderr, "Cannot initialize system interface\n");
        return 1;
    }

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if(ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return(-1);
    }

    /* setup a mapped region for I/O transfers */
    ret = PINT_dev_get_mapped_region(
        &mapped_region, MAPPED_REGION_SIZE);
    if(ret < 0)
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

	ret = job_dev_unexp(&info, NULL, &jstat, &job_id, context);
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

        gossip_debug(CLIENT_DEBUG,
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
		service_io_request(&init_response, &upcall, &downcall);
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

    if (PVFS_sys_finalize())
    {
        gossip_err("Failed to finalize PVFS\n");
        return 1;
    }
    gossip_disable();
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
