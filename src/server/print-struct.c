#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <signal.h>
#include <rpc/rpc.h>

#include <bmi.h>
#include <gossip.h>
#include <job.h>
#include <trove.h>
#include <pvfs2-debug.h>
#include <pvfs2-storage.h>

#include <pvfs2-req-proto.h>
#if 0
#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>

#endif

// Assume Gossip Enabled.
/* TODO: Use Gossip Calls! */

void arrow(int direction)
{
	if (!direction)
	{
		printf("=<=");
	}
	else
	{
		printf("=>=");
	}
}


void dump_attribs(PVFS_object_attr a) 
{
   printf("\tUserID: %d\n",a.owner);
   printf("\tGID: %d\n",a.group);
   printf("\tPerms: %xh\n",a.perms);
   printf("\tAtime: %ld\n",a.atime);
   printf("\tMtime: %ld\n",a.mtime);
   printf("\tCtime: %ld\n",a.ctime);
   printf("\tType: %d\n",a.objtype);
}

void display_pvfs_structure(void *s,int r)
{
	int i = 0;
	if (r == 1) //Request
	{
		struct PVFS_server_req_s *p = s;
		printf("Size: %Ld\n",p->rsize);
		printf("Op: %d\n",p->op);
		switch(p->op)
		{
			case PVFS_SERV_CREATE:
				printf("Create Request Structure\n");
				printf("Requested Handle: %Ld\n",p->u.create.requested_handle);
				printf("FSid: %d\n",p->u.create.fs_id);
				printf("ObjectType: %d\n",p->u.create.object_type);
				break;
			case PVFS_SERV_REMOVE:
				printf("Remove Request Structure\n");
				printf("Handle: %Ld\n",p->u.remove.handle);
				printf("FSid: %d\n",p->u.remove.fs_id);
				break;
			case PVFS_SERV_IO:
			case PVFS_SERV_BATCH:
				printf("NOT SURE\n");
				break;
			case PVFS_SERV_GETATTR:
				printf("Get Attrib Request Struct\n");
				printf("Handle: %Ld\n",p->u.getattr.handle);
				printf("FSid: %d\n",p->u.getattr.fs_id);
				printf("AttrMsk: %Xh\n",p->u.getattr.attrmask);
				break;
			case PVFS_SERV_SETATTR:
				printf("Set Attrib Request Struct\n");
				printf("Handle: %Ld\n",p->u.setattr.handle);
				printf("FSid: %d\n",p->u.setattr.fs_id);
				printf("Attribs:\n");
				dump_attribs(p->u.setattr.attr);
				break;
			case PVFS_SERV_LOOKUP_PATH:
				printf("Lookup Path Request Struct\n");
				printf("Path: %s\n",p->u.lookup_path.path);
				printf("FSid: %d\n",p->u.lookup_path.fs_id);
				printf("Start Handle: %Ld\n",p->u.lookup_path.starting_handle);
				printf("Bitmask: %Xh\n",p->u.lookup_path.attrmask);
				break;
			case PVFS_SERV_MKDIR:
				printf("Mkdir Req\n");
				printf("Requested Handle: %Ld\n",p->u.mkdir.requested_handle);
				printf("FSid: %d\n",p->u.mkdir.fs_id);
				printf("Attribs:\n");
				dump_attribs(p->u.mkdir.attr);
				printf("Attrmask: %Xh\n",p->u.mkdir.attrmask);
				break;
			case PVFS_SERV_CREATEDIRENT:
				printf("Create Dirent Req\n");
				printf("Name: %s\n",p->u.crdirent.name);
				printf("New Handle: %Ld\n",p->u.crdirent.new_handle);
				printf("Parent Handle: %Ld\n",p->u.crdirent.parent_handle);
				printf("FSid: %d\n",p->u.crdirent.fs_id);
				break;
			case PVFS_SERV_RMDIRENT:
				printf("Remove Dir Entry Req\n");
				printf("Entry: %s\n",p->u.rmdirent.entry);
				printf("Parent Handle: %Ld\n",p->u.rmdirent.parent_handle);
				printf("FSid: %d\n",p->u.rmdirent.fs_id);
				break;
			case PVFS_SERV_GETCONFIG:
				printf("Get Config\n");
				printf("Max Size: %d\n",p->u.getconfig.max_strsize);
				break;
			case PVFS_SERV_READDIR:
				printf("Read Dir\n");
				printf("Handle: %Ld\n",p->u.readdir.handle);
				printf("FSid: %d\n",p->u.readdir.fs_id);
				printf("Token: %d\n",(int)p->u.readdir.token);
				printf("Dir Ents: %d\n",p->u.readdir.pvfs_dirent_count);
				break;
			default:
				printf("Invalid Request\n");
		}
	}
	else 
	{
		struct PVFS_server_resp_s *p = s;
		printf("Size: %lld\n",p->rsize);
		printf("Op: %d\n",p->op);
		switch(p->op)
		{
			case PVFS_SERV_GETATTR:
				printf("Get Attrib Response Struct\n");
			   printf("Attribs:\n");
				dump_attribs(p->u.getattr.attr);
				break;
			case PVFS_SERV_CREATE:
				printf("Create Resp Structure\n");
				printf("Bucket: %Ld\n",p->u.create.handle);
				break;
			case PVFS_SERV_LOOKUP_PATH:
				printf("Lookup Path Resp Struct\n");
				printf("Handle Array (Total: %d)\n",p->u.lookup_path.count);
					while(i++<p->u.lookup_path.count)
					{
						printf("%d\t%Ld\n",i,p->u.lookup_path.handle_array[i-1]);
						printf("Attribs:\n");
						dump_attribs(p->u.lookup_path.attr_array[i-1]);
					}
				break;
			case PVFS_SERV_MKDIR:
				printf("Mkdir Resp\n");
				printf("Handle: %Ld\n",p->u.mkdir.handle);
				break;
			case PVFS_SERV_RMDIRENT:
				printf("Remove Dir Entry Resp\n");
				printf("Entry Handle: %Ld\n",p->u.rmdirent.entry_handle);
				break;
			case PVFS_SERV_GETCONFIG:
				printf("Get Config Resp\n");
				printf("FS Config buffer length: %d\n",p->u.getconfig.fs_config_buflen);
				printf("FS Config buffer:\n%s\n",p->u.getconfig.fs_config_buf);
				printf("SERVER Config buffer length: %d\n",p->u.getconfig.server_config_buflen);
				printf("SERVER Config buffer:\n%s\n",p->u.getconfig.server_config_buf);
				break;
			case PVFS_SERV_READDIR:
				printf("Read dir\n");
				printf("Token: %d\n",(int)p->u.readdir.token);
				printf("Count: %d\n",p->u.readdir.pvfs_dirent_count);
				while(i++<p->u.readdir.pvfs_dirent_count)
				{
					printf("%s\t%Ld\n",p->u.readdir.pvfs_dirent_array[i-1].d_name,p->u.readdir.pvfs_dirent_array[i-1].handle);
				}
				break;

			case PVFS_SERV_REMOVE:
			case PVFS_SERV_IO:
			case PVFS_SERV_BATCH:
			case PVFS_SERV_SETATTR:
			case PVFS_SERV_CREATEDIRENT:
				printf("Shouldn't have a response\n");
				break;
			default:
				printf("Unsupported operation\n");
		}
	}
}

