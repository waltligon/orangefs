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
		gossip_ldebug(SERVER_DEBUG,"=<=");
	}
	else
	{
		gossip_ldebug(SERVER_DEBUG,"=>=");
	}
}


void dump_attribs(PVFS_object_attr a) 
{
   gossip_ldebug(SERVER_DEBUG,"\tUserID: %d\n",a.owner);
   gossip_ldebug(SERVER_DEBUG,"\tGID: %d\n",a.group);
   gossip_ldebug(SERVER_DEBUG,"\tPerms: %xh\n",a.perms);
   gossip_ldebug(SERVER_DEBUG,"\tAtime: %ld\n",a.atime);
   gossip_ldebug(SERVER_DEBUG,"\tMtime: %ld\n",a.mtime);
   gossip_ldebug(SERVER_DEBUG,"\tCtime: %ld\n",a.ctime);
   gossip_ldebug(SERVER_DEBUG,"\tType: %d\n",a.objtype);
}

void display_pvfs_structure(void *s,int r)
{
	int i = 0;
	if (r == 1) //Request
	{
		struct PVFS_server_req_s *p = s;
		gossip_ldebug(SERVER_DEBUG,"Size: %lld\n",p->rsize);
		gossip_ldebug(SERVER_DEBUG,"Op: %d\n",p->op);
		switch(p->op)
		{
			case PVFS_SERV_CREATE:
				gossip_ldebug(SERVER_DEBUG,"Create Request Structure\n");
				gossip_ldebug(SERVER_DEBUG,"Bucket: %lld\n",p->u.create.bucket);
				gossip_ldebug(SERVER_DEBUG,"Mask: %lXh\n",(long unsigned int)p->u.create.handle_mask);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.create.fs_id);
				gossip_ldebug(SERVER_DEBUG,"ObjectType: %d\n",p->u.create.object_type);
				break;
			case PVFS_SERV_REMOVE:
				gossip_ldebug(SERVER_DEBUG,"Remove Request Structure\n");
				gossip_ldebug(SERVER_DEBUG,"Handle: %Ld\n",p->u.remove.handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.remove.fs_id);
				break;
			case PVFS_SERV_IO:
			case PVFS_SERV_BATCH:
				gossip_ldebug(SERVER_DEBUG,"NOT SURE\n");
				break;
			case PVFS_SERV_GETATTR:
				gossip_ldebug(SERVER_DEBUG,"Get Attrib Request Struct\n");
				gossip_ldebug(SERVER_DEBUG,"Handle: %Ld\n",p->u.getattr.handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.getattr.fs_id);
				gossip_ldebug(SERVER_DEBUG,"AttrMsk: %Xh\n",p->u.getattr.attrmask);
				break;
			case PVFS_SERV_SETATTR:
				gossip_ldebug(SERVER_DEBUG,"Set Attrib Request Struct\n");
				gossip_ldebug(SERVER_DEBUG,"Handle: %Ld\n",p->u.setattr.handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.setattr.fs_id);
				gossip_ldebug(SERVER_DEBUG,"Attribs:\n");
				dump_attribs(p->u.setattr.attr);
				break;
			case PVFS_SERV_LOOKUP_PATH:
				gossip_ldebug(SERVER_DEBUG,"Lookup Path Request Struct\n");
				gossip_ldebug(SERVER_DEBUG,"Path: %s\n",p->u.lookup_path.path);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.lookup_path.fs_id);
				gossip_ldebug(SERVER_DEBUG,"Start Handle: %Ld\n",p->u.lookup_path.starting_handle);
				gossip_ldebug(SERVER_DEBUG,"Bitmask: %Xh\n",p->u.lookup_path.attrmask);
				break;
			case PVFS_SERV_MKDIR:
				gossip_ldebug(SERVER_DEBUG,"Mkdir Req\n");
				gossip_ldebug(SERVER_DEBUG,"Bucket: %lld\n",p->u.mkdir.bucket);
				gossip_ldebug(SERVER_DEBUG,"Mask: %lXh\n",(long unsigned int)p->u.mkdir.handle_mask);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.mkdir.fs_id);
				gossip_ldebug(SERVER_DEBUG,"Attribs:\n");
				dump_attribs(p->u.mkdir.attr);
				gossip_ldebug(SERVER_DEBUG,"Attrmask: %Xh\n",p->u.mkdir.attrmask);
				break;
			case PVFS_SERV_RMDIR:
				gossip_ldebug(SERVER_DEBUG,"Remove Dir Req\n");
				gossip_ldebug(SERVER_DEBUG,"Handle: %Ld\n",p->u.rmdir.handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.rmdir.fs_id);
				break;
			case PVFS_SERV_CREATEDIRENT:
				gossip_ldebug(SERVER_DEBUG,"Create Dirent Req\n");
				gossip_ldebug(SERVER_DEBUG,"Name: %s\n",p->u.crdirent.name);
				gossip_ldebug(SERVER_DEBUG,"New Handle: %Ld\n",p->u.crdirent.new_handle);
				gossip_ldebug(SERVER_DEBUG,"Parent Handle: %Ld\n",p->u.crdirent.parent_handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.crdirent.fs_id);
				break;
			case PVFS_SERV_RMDIRENT:
				gossip_ldebug(SERVER_DEBUG,"Remove Dir Entry Req\n");
				gossip_ldebug(SERVER_DEBUG,"Entry: %s\n",p->u.rmdirent.entry);
				gossip_ldebug(SERVER_DEBUG,"Parent Handle: %Ld\n",p->u.rmdirent.parent_handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.rmdirent.fs_id);
				break;
			case PVFS_SERV_GETCONFIG:
				gossip_ldebug(SERVER_DEBUG,"Get Config\n");
				gossip_ldebug(SERVER_DEBUG,"FSname: %s\n",p->u.getconfig.fs_name);
				gossip_ldebug(SERVER_DEBUG,"Max Size: %d\n",p->u.getconfig.max_strsize);
				break;
			case PVFS_SERV_READDIR:
				gossip_ldebug(SERVER_DEBUG,"Read Dir\n");
				gossip_ldebug(SERVER_DEBUG,"Handle: %Ld\n",p->u.readdir.handle);
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.readdir.fs_id);
				gossip_ldebug(SERVER_DEBUG,"Token: %d\n",(int)p->u.readdir.token);
				gossip_ldebug(SERVER_DEBUG,"Dir Ents: %d\n",p->u.readdir.pvfs_dirent_count);
				break;
			default:
				gossip_ldebug(SERVER_DEBUG,"Invalid Request\n");
		}
	}
	else 
	{
		struct PVFS_server_resp_s *p = s;
		gossip_ldebug(SERVER_DEBUG,"Size: %lld\n",p->rsize);
		gossip_ldebug(SERVER_DEBUG,"Op: %d\n",p->op);
		switch(p->op)
		{
			case PVFS_SERV_GETATTR:
				gossip_ldebug(SERVER_DEBUG,"Get Attrib Response Struct\n");
			   gossip_ldebug(SERVER_DEBUG,"Attribs:\n");
				dump_attribs(p->u.getattr.attr);
				break;
			case PVFS_SERV_CREATE:
				gossip_ldebug(SERVER_DEBUG,"Create Resp Structure\n");
				gossip_ldebug(SERVER_DEBUG,"Bucket: %Ld\n",p->u.create.handle);
				break;
			case PVFS_SERV_LOOKUP_PATH:
				gossip_ldebug(SERVER_DEBUG,"Lookup Path Resp Struct\n");
				gossip_ldebug(SERVER_DEBUG,"Handle Array (Total: %d)\n",p->u.lookup_path.count);
					while(i++<p->u.lookup_path.count)
					{
						gossip_ldebug(SERVER_DEBUG,"%d\t%Ld\n",i,p->u.lookup_path.handle_array[i-1]);
						gossip_ldebug(SERVER_DEBUG,"Attribs:\n");
						dump_attribs(p->u.lookup_path.attr_array[i-1]);
					}
				break;
			case PVFS_SERV_MKDIR:
				gossip_ldebug(SERVER_DEBUG,"Mkdir Resp\n");
				gossip_ldebug(SERVER_DEBUG,"Handle: %Ld\n",p->u.mkdir.handle);
				break;
			case PVFS_SERV_RMDIRENT:
				gossip_ldebug(SERVER_DEBUG,"Remove Dir Entry Resp\n");
				gossip_ldebug(SERVER_DEBUG,"Entry Handle: %Ld\n",p->u.rmdirent.entry_handle);
				break;
			case PVFS_SERV_GETCONFIG:
				gossip_ldebug(SERVER_DEBUG,"Get Config Resp\n");
				gossip_ldebug(SERVER_DEBUG,"FSid: %d\n",p->u.getconfig.fs_id);
				gossip_ldebug(SERVER_DEBUG,"Root Handle: %Ld\n",p->u.getconfig.root_handle);
				gossip_ldebug(SERVER_DEBUG,"MaskBits: %Ld\n",p->u.getconfig.maskbits);
				gossip_ldebug(SERVER_DEBUG,"MetaServer Count: %d\n",p->u.getconfig.meta_server_count);
				gossip_ldebug(SERVER_DEBUG,"MetaServer Map: %s\n",p->u.getconfig.meta_server_mapping);
				gossip_ldebug(SERVER_DEBUG,"IOServer Count: %d\n",p->u.getconfig.io_server_count);
				gossip_ldebug(SERVER_DEBUG,"IOServer Map: %s\n",p->u.getconfig.io_server_mapping);
				break;
			case PVFS_SERV_READDIR:
				gossip_ldebug(SERVER_DEBUG,"Read dir\n");
				gossip_ldebug(SERVER_DEBUG,"Token: %d\n",(int)p->u.readdir.token);
				gossip_ldebug(SERVER_DEBUG,"Count: %d\n",p->u.readdir.pvfs_dirent_count);
				while(i++<p->u.readdir.pvfs_dirent_count)
				{
					gossip_ldebug(SERVER_DEBUG,"%s\t%Ld\n",p->u.readdir.pvfs_dirent_array[i-1].d_name,p->u.readdir.pvfs_dirent_array[i-1].handle);
				}
				break;

			case PVFS_SERV_REMOVE:
			case PVFS_SERV_IO:
			case PVFS_SERV_BATCH:
			case PVFS_SERV_SETATTR:
			case PVFS_SERV_RMDIR:
			case PVFS_SERV_CREATEDIRENT:
			default:
				gossip_ldebug(SERVER_DEBUG,"Unsupported or Shouldn't have a response\n");
		}
	}
}

