/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include "client.h"
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "pvfs2-util.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"

#define MAX_BUF_LEN 5

int main(int argc, char **argv)
{
    int ret = -1;
    char name[512] = {0}, buf[MAX_BUF_LEN] = {0};
    char *entry_name = NULL;
    PVFS_fs_id fs_id;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    PVFS_sysresp_io resp_io;
    PVFS_credentials credentials;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_object_ref pinode_refn;
    PVFS_Request file_req;
    PVFS_Request mem_req;

    if (argc != 2)
    {
	fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
	return -1;
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return -1;
    }

    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return -1;
    }

    if (argv[1][0] == '/')
    {
        snprintf(name, 512, "%s", argv[1]);
    }
    else
    {
        snprintf(name, 512, "/%s", argv[1]);
    }

    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_lookup(fs_id, name, &credentials,
			  &resp_lk, PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret == -PVFS_ENOENT)
    {
        PVFS_sysresp_getparent gp_resp;

	printf("IO-HOLE: lookup failed; creating new file.\n");

        memset(&gp_resp, 0, sizeof(PVFS_sysresp_getparent));
	ret = PVFS_sys_getparent(fs_id, name, &credentials, &gp_resp, NULL);
	if (ret < 0)
	{
            PVFS_perror("PVFS_sys_getparent failed", ret);
	    return ret;
	}

	attr.owner = credentials.uid;
	attr.group = credentials.gid;
	attr.perms = PVFS_U_WRITE | PVFS_U_READ;
	attr.atime = attr.ctime = attr.mtime = time(NULL);
	attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
	parent_refn = gp_resp.parent_ref;

        entry_name = rindex(name, (int)'/');
        assert(entry_name);
        entry_name++;
        assert(entry_name);

	ret = PVFS_sys_create(entry_name, parent_refn, attr,
			      &credentials, NULL, &resp_cr, NULL, NULL);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_sys_create() failure", ret);
	    return -1;
	}

	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_cr.ref.handle;
    }
    else
    {
	printf("IO-HOLE: lookup succeeded; performing I/O on "
               "existing file.\n");

	pinode_refn.fs_id = fs_id;
	pinode_refn.handle = resp_lk.ref.handle;
    }

    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(MAX_BUF_LEN, PVFS_BYTE, &mem_req);
    if (ret < 0)
    {
        PVFS_perror("PVFS_request_contiguous failure", ret);
	return -1;
    }

    memset(buf, 'A', MAX_BUF_LEN);

    ret = PVFS_sys_write(pinode_refn, file_req, 0, buf, mem_req,
			 &credentials, &resp_io, NULL);
    if ((ret < 0) || (resp_io.total_completed != MAX_BUF_LEN))
    {
        PVFS_perror("PVFS_sys_write failure", ret);
	return -1;
    }
    printf("IO-HOLE: wrote %lld bytes at offset 0\n",
           lld(resp_io.total_completed));

    ret = PVFS_sys_write(pinode_refn, file_req, 100000, buf, mem_req,
			 &credentials, &resp_io, NULL);
    if ((ret < 0) || (resp_io.total_completed != MAX_BUF_LEN))
    {
        PVFS_perror("PVFS_sys_write failure", ret);
	return -1;
    }
    printf("IO-HOLE: wrote %lld bytes at offset 100000\n",
           lld(resp_io.total_completed));

    ret = PVFS_sys_read(pinode_refn, file_req, 10, buf, mem_req,
			&credentials, &resp_io, NULL);
    if ((ret < 0) || (resp_io.total_completed != MAX_BUF_LEN))
    {
        fprintf(stderr, "Failed to read %d bytes at offset 10! %lld "
                "bytes read\n", MAX_BUF_LEN, lld(resp_io.total_completed));
	return -1;
    }
    printf("IO-HOLE: read %lld bytes at offset 10\n",
           lld(resp_io.total_completed));

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	fprintf(stderr, "Error: PVFS_sys_finalize() failed "
                "with errcode = %d\n", ret);
	return ret;
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

