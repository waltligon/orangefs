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
    PVFS_credentials credentials;
    PVFS_sysresp_lookup response;
    PVFS_pinode_reference parent_refn;

    if (init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_lookup));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        credentials.uid = 100;
        credentials.gid = 100;
        credentials.perms = 511;

        printf("Got a lookup request for %s (fsid %d | parent %Ld)\n",
               in_upcall->req.lookup.d_name,
               in_upcall->req.lookup.parent_refn.fs_id,
               in_upcall->req.lookup.parent_refn.handle);

        parent_refn = in_upcall->req.lookup.parent_refn;

        ret = PVFS_sys_ref_lookup(parent_refn.fs_id,
                                  in_upcall->req.lookup.d_name,
                                  parent_refn, credentials,
                                  &response);
        if (ret < 0)
        {
            fprintf(stderr,"Failed to lookup %s (fsid %d | "
                    "parent is %Ld)!\n",in_upcall->req.lookup.d_name,
                    parent_refn.fs_id,parent_refn.handle);
            fprintf(stderr,"Lookup returned error code %d\n",ret);

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
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_create response;
    PVFS_pinode_reference parent_refn;

    if (init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_create));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
        attr.owner = 100;
        attr.group = 100;
        attr.perms = 511;
	attr.atime = attr.mtime = attr.ctime = 
	    time(NULL);

        credentials.uid = attr.owner;
        credentials.gid = attr.group;
        credentials.perms = attr.perms;

        parent_refn = in_upcall->req.create.parent_refn;

        printf("Got a create request for %s (fsid %d | parent %Ld)\n",
               in_upcall->req.create.d_name,parent_refn.fs_id,
               parent_refn.handle);

        ret = PVFS_sys_create(in_upcall->req.create.d_name, parent_refn,
                              attr, credentials, &response);
        if (ret < 0)
        {
            fprintf(stderr,"Failed to create %s under %Ld on fsid %d!\n",
                    in_upcall->req.create.d_name,
                    parent_refn.handle,parent_refn.fs_id);
            fprintf(stderr,"Create returned error code %d\n",ret);

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

static int service_read_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_sysresp_io response;
    PVFS_credentials credentials;
    PVFS_Request io_req;
    PVFS_size displacement = 0;
    int32_t blocklength = 0;

    if(init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_io));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

	credentials.uid = 100;
	credentials.gid = 100;
	credentials.perms = 511;

	displacement = in_upcall->req.read.offset;
	blocklength = in_upcall->req.read.count;

	ret = PVFS_Request_indexed(1, &blocklength, &displacement,
	    PVFS_BYTE, &io_req);
	assert(ret == 0);

	ret = PVFS_sys_io(in_upcall->req.read.refn, io_req, 0, 
	    in_upcall->req.read.buf, in_upcall->req.read.count, credentials, 
	    &response, PVFS_SYS_IO_READ);
	if(ret < 0)
	{
	    /* we need to send a blank response */
	    out_downcall->type = PVFS2_VFS_OP_FILE_READ;
	    /* TODO: what should this be set to? other examples do -1... */
	    out_downcall->status = ret;
	}
	else
	{
	    out_downcall->type = PVFS2_VFS_OP_FILE_READ;
	    out_downcall->status = 0;
	    out_downcall->resp.read.amt_read = response.total_completed;
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
    uint32_t attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr response;

    if (init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_getattr));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        credentials.uid = 100;
        credentials.gid = 100;
        credentials.perms = 511;

        printf("got a getattr request for fsid %d | handle %ld\n",
               in_upcall->req.getattr.refn.fs_id,
               in_upcall->req.getattr.refn.handle);

        ret = PVFS_sys_getattr(in_upcall->req.getattr.refn, attrmask,
                               credentials, &response);
        if (ret < 0)
        {
            fprintf(stderr,"failed to getattr handle %ld on fsid %d!\n",
                    in_upcall->req.getattr.refn.handle,
                    in_upcall->req.getattr.refn.fs_id);
            fprintf(stderr,"getattr returned error code %d\n",ret);

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

static int service_remove_request(
    PVFS_sysresp_init *init_response,
    pvfs2_upcall_t *in_upcall,
    pvfs2_downcall_t *out_downcall)
{
    int ret = 1;
    PVFS_credentials credentials;
    PVFS_pinode_reference parent_refn;

    if (init_response && in_upcall && out_downcall)
    {
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        credentials.uid = 100;
        credentials.gid = 100;
        credentials.perms = 511;

        parent_refn = in_upcall->req.remove.parent_refn;

        printf("Got a remove request for %s under fsid %d and "
               "handle %Ld\n", in_upcall->req.remove.d_name,
               parent_refn.fs_id, parent_refn.handle);

        ret = PVFS_sys_remove(in_upcall->req.remove.d_name,
                              parent_refn, credentials);
        if (ret < 0)
        {
            fprintf(stderr,"Failed to remove %s under handle %Ld "
                    "on fsid %d!\n", in_upcall->req.remove.d_name,
                    parent_refn.handle, parent_refn.fs_id);
            fprintf(stderr,"Remove returned error code %d\n",ret);

            /* we need to send a blank error response */
            out_downcall->type = PVFS2_VFS_OP_GETATTR;
            out_downcall->status = -1;
        }
        else
        {
            /* we need to send a blank success response */
            out_downcall->type = PVFS2_VFS_OP_GETATTR;
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
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_mkdir response;
    PVFS_pinode_reference parent_refn;

    if (init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_mkdir));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        attr.owner = 100;
        attr.group = 100;
        attr.perms = 511;
	attr.atime = attr.mtime = attr.ctime = 
	    time(NULL);
	attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;

        credentials.uid = attr.owner;
        credentials.gid = attr.group;
        credentials.perms = attr.perms;

        parent_refn = in_upcall->req.mkdir.parent_refn;

        printf("Got a mkdir request for %s (fsid %d | parent %Ld)\n",
               in_upcall->req.mkdir.d_name,parent_refn.fs_id,
               parent_refn.handle);

        ret = PVFS_sys_mkdir(in_upcall->req.mkdir.d_name, parent_refn,
                             attr, credentials, &response);
        if (ret < 0)
        {
            fprintf(stderr,"Failed to mkdir %s under %Ld on fsid %d!\n",
                    in_upcall->req.mkdir.d_name,
                    parent_refn.handle,parent_refn.fs_id);
            fprintf(stderr,"Mkdir returned error code %d\n",ret);

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
    PVFS_credentials credentials;
    PVFS_sysresp_readdir response;
    PVFS_pinode_reference refn;

    if (init_response && in_upcall && out_downcall)
    {
        memset(&response,0,sizeof(PVFS_sysresp_readdir));
        memset(out_downcall,0,sizeof(pvfs2_downcall_t));

        credentials.uid = 100;
        credentials.gid = 100;
        credentials.perms = 511;

        refn = in_upcall->req.readdir.refn;

        printf("Got a readdir request for fsid %d | parent %Ld\n",
               refn.fs_id, refn.handle);

        ret = PVFS_sys_readdir(refn, in_upcall->req.readdir.token,
                               in_upcall->req.readdir.max_dirent_count,
                               credentials, &response);
        if (ret < 0)
        {
            fprintf(stderr,"Failed to readdir under %Ld on fsid %d!\n",
                    refn.handle, refn.fs_id);
            fprintf(stderr,"Readdir returned error code %d\n",ret);

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

                /* DIRENT SHOULD HAVE AN FSID, NO?? */
                out_downcall->resp.readdir.refn[i].fs_id = refn.fs_id;

                len = strlen(response.dirent_array[i].d_name);
                out_downcall->resp.readdir.d_name_len[i] = len;
                strncpy(&out_downcall->resp.readdir.d_name[i][0],
                        response.dirent_array[i].d_name,len);

                printf("COPIED OVER DIRENT %s (%s)\n",
                       response.dirent_array[i].d_name,
                       &out_downcall->resp.readdir.d_name[i][0]);
                out_downcall->resp.readdir.dirent_count++;
            }

            if (out_downcall->resp.readdir.dirent_count ==
                response.pvfs_dirent_outcount)
            {
                ret = 0;
            }
            else
            {
                fprintf(stderr,"DIRENT COUNTS DON'T MATCH (%d != %d)\n",
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
            printf("no immed completion.\n");
	    ret = job_test(*job_id, &outcount, NULL, jstat, -1, context);
            if (ret < 0)
            {
                PVFS_perror("job_test()", ret);
                goto exit_point;
            }
        }
        else
        {
            printf("immed completion.\n");
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

    gossip_enable_stderr();
    gossip_set_debug_mask(1, CLIENT_DEBUG);

    if (parse_pvfstab(NULL,&mnt))
    {
        fprintf(stderr,"Error parsing pvfstab!\n");
        return 1;
    }

    memset(&init_response,0,sizeof(PVFS_sysresp_init));
    if (PVFS_sys_initialize(mnt, &init_response))
    {
        fprintf(stderr,"Cannot initialize system interface\n");
        return 1;
    }

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if(ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return(-1);
    }

    /* setup a mapped region for I/O transfers */
    ret = PINT_dev_get_mapped_region(&mapped_region,
	MAPPED_REGION_SIZE);
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

	printf("Got message: size: %d, tag: %d, payload: %s\n", 
	       info.size, (int)info.tag, (char*)info.buffer);

	tag = (unsigned long)info.tag;
	if (info.size >= sizeof(pvfs2_upcall_t))
	{
	    printf("Copying upcall from device\n");
	    memcpy(&upcall,info.buffer,sizeof(pvfs2_upcall_t));

	    printf("upcall Type is %d\n",upcall.type);
	}
	else
	{
	    printf("What does this mean?\n");
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
	    case PVFS2_VFS_OP_REMOVE:
		service_remove_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_MKDIR:
		service_mkdir_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_READDIR:
		service_readdir_request(&init_response,&upcall,&downcall);
		break;
	    case PVFS2_VFS_OP_FILE_READ:
		service_read_request(&init_response, &upcall, &downcall);
		break;
	    case PVFS2_VFS_OP_FILE_WRITE:
		/* do a file write */
		break;
	    case PVFS2_VFS_OP_INVALID:
	    default:
		printf("Got an unrecognized vfs operation.\n");
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
	    fprintf(stderr,"write_device_response failed on tag %lu\n",tag);
	}
    }

    job_close_context(context);

    if (PVFS_sys_finalize())
    {
        fprintf(stderr,"Failed to finalize PVFS\n");
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
