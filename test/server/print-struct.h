/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


#ifndef __PRINT_STRUCT_H
#define __PRINT_STRUCT_H

#include "pvfs2-req-proto.h"

static inline void dump_attribs(
    PVFS_object_attr a)
{
    printf("\tUserID: %d\n", a.owner);
    printf("\tGID: %d\n", a.group);
    printf("\tPerms: %xh\n", a.perms);
    printf("\tAtime: %lld\n", (long long) a.atime);
    printf("\tMtime: %lld\n", (long long) a.mtime);
    printf("\tCtime: %lld\n", (long long) a.ctime);
    printf("\tType: %d\n", a.objtype);
    printf("\tMask: %d\n", (int) a.mask);
}

static inline void display_pvfs_structure(
    void *s,
    int r)
{
    int i = 0;
    if (r == 1)	//Request
    {
	struct PVFS_server_req *p = s;
	printf("Op: %d\n", p->op);
	switch (p->op)
	{
	case PVFS_SERV_CREATE:
	    printf("Create Request Structure\n");
	    printf("Num Requested Handle Ranges: %d\n",
		   p->u.create.handle_extent_array.extent_count);
	    /* todo: print the ranges */
	    printf("FSid: %d\n", p->u.create.fs_id);
	    printf("ObjectType: %d\n", p->u.create.object_type);
	    break;
	case PVFS_SERV_REMOVE:
	    printf("Remove Request Structure\n");
	    printf("Handle: %Ld\n", p->u.remove.handle);
	    printf("FSid: %d\n", p->u.remove.fs_id);
	    break;
	case PVFS_SERV_IO:
	    printf("NOT SURE\n");
	    break;
	case PVFS_SERV_GETATTR:
	    printf("Get Attrib Request Struct\n");
	    printf("Handle: %Ld\n", p->u.getattr.handle);
	    printf("FSid: %d\n", p->u.getattr.fs_id);
	    printf("AttrMsk: %Xh\n", p->u.getattr.attrmask);
	    break;
	case PVFS_SERV_SETATTR:
	    printf("Set Attrib Request Struct\n");
	    printf("Handle: %Ld\n", p->u.setattr.handle);
	    printf("FSid: %d\n", p->u.setattr.fs_id);
	    printf("Attribs:\n");
	    dump_attribs(p->u.setattr.attr);
	    break;
	case PVFS_SERV_LOOKUP_PATH:
	    printf("Lookup Path Request Struct\n");
	    printf("Path: %s\n", p->u.lookup_path.path);
	    printf("FSid: %d\n", p->u.lookup_path.fs_id);
	    printf("Start Handle: %Ld\n", p->u.lookup_path.starting_handle);
	    printf("Bitmask: %Xh\n", p->u.lookup_path.attrmask);
	    break;
	case PVFS_SERV_MKDIR:
	    printf("Mkdir Req\n");
	    printf("Num Requested Handle Ranges: %d\n",
		   p->u.mkdir.handle_extent_array.extent_count);
	    /* todo: print the ranges */
	    printf("FSid: %d\n", p->u.mkdir.fs_id);
	    printf("Attribs:\n");
	    dump_attribs(p->u.mkdir.attr);
	    break;
	case PVFS_SERV_CREATEDIRENT:
	    printf("Create Dirent Req\n");
	    printf("Name: %s\n", p->u.crdirent.name);
	    printf("New Handle: %Ld\n", p->u.crdirent.new_handle);
	    printf("Parent Handle: %Ld\n", p->u.crdirent.parent_handle);
	    printf("FSid: %d\n", p->u.crdirent.fs_id);
	    break;
	case PVFS_SERV_RMDIRENT:
	    printf("Remove Dir Entry Req\n");
	    printf("Entry: %s\n", p->u.rmdirent.entry);
	    printf("Parent Handle: %Ld\n", p->u.rmdirent.parent_handle);
	    printf("FSid: %d\n", p->u.rmdirent.fs_id);
	    break;
	case PVFS_SERV_GETCONFIG:
	    printf("Get Config\n");
	    break;
	case PVFS_SERV_READDIR:
	    printf("Read Dir\n");
	    printf("Handle: %Ld\n", p->u.readdir.handle);
	    printf("FSid: %d\n", p->u.readdir.fs_id);
	    printf("Token: %d\n", (int) p->u.readdir.token);
	    printf("Dir Ents: %d\n", p->u.readdir.dirent_count);
	    break;
	default:
	    printf("Invalid Request\n");
	}
    }
    else
    {
	struct PVFS_server_resp *p = s;
	printf("Op: %d\n", p->op);
	switch (p->op)
	{
	case PVFS_SERV_GETATTR:
	    printf("Get Attrib Response Struct\n");
	    printf("Attribs:\n");
	    dump_attribs(p->u.getattr.attr);
	    break;
	case PVFS_SERV_CREATE:
	    printf("Create Resp Structure\n");
	    printf("Bucket: %Ld\n", p->u.create.handle);
	    break;
	case PVFS_SERV_LOOKUP_PATH:
	    printf("Lookup Path Resp Struct\n");
	    printf("Handle Array (Total: %d)\n", p->u.lookup_path.handle_count);
	    while (i++ < p->u.lookup_path.handle_count)
	    {
		printf("%d\t%Ld\n", i, p->u.lookup_path.handle_array[i - 1]);
		printf("Attribs:\n");
		dump_attribs(p->u.lookup_path.attr_array[i - 1]);
	    }
	    break;
	case PVFS_SERV_MKDIR:
	    printf("Mkdir Resp\n");
	    printf("Handle: %Ld\n", p->u.mkdir.handle);
	    break;
	case PVFS_SERV_RMDIRENT:
	    printf("Remove Dir Entry Resp\n");
	    printf("Entry Handle: %Ld\n", p->u.rmdirent.entry_handle);
	    break;
	case PVFS_SERV_GETCONFIG:
	    printf("Get Config Resp\n");
	    printf("FS Config buffer length: %d\n",
		   p->u.getconfig.fs_config_buf_size);
	    printf("FS Config buffer:\n%s\n", p->u.getconfig.fs_config_buf);
	    printf("SERVER Config buffer length: %d\n",
		   p->u.getconfig.server_config_buf_size);
	    printf("SERVER Config buffer:\n%s\n",
		   p->u.getconfig.server_config_buf);
	    break;
	case PVFS_SERV_READDIR:
	    printf("Read dir\n");
	    printf("Token: %d\n", (int) p->u.readdir.token);
	    printf("Count: %d\n", p->u.readdir.dirent_count);
	    while (i++ < p->u.readdir.dirent_count)
	    {
		printf("%s\t%Ld\n", p->u.readdir.dirent_array[i - 1].d_name,
		       p->u.readdir.dirent_array[i - 1].handle);
	    }
	    break;

	case PVFS_SERV_REMOVE:
	case PVFS_SERV_IO:
	case PVFS_SERV_SETATTR:
	case PVFS_SERV_CREATEDIRENT:
	    printf("Shouldn't have a response\n");
	    break;
	default:
	    printf("Unsupported operation\n");
	}
    }
}

#endif /* __PRINT_STRUCT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
