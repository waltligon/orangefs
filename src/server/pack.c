#include <stdio.h>
#include <string.h>
#include <pvfs2-req-proto.h>
#include <state-machine.h>
#include <pack.h>
#include <bmi.h>

void *pack_pvfs_struct(void *p,int type,bmi_addr_t q,int z)
{
	void *myBuff;
	if(type == 1) //req
	{
		struct PVFS_server_req_s *r = p;
		switch(r->op)
		{
			case PVFS_SERV_CREATEDIRENT:
				myBuff = BMI_memalloc(q,
											 sizeof(struct PVFS_server_req_s)+
											 	strlen(r->u.crdirent.name)+1,
											 BMI_SEND_BUFFER);
				memcpy(myBuff,r,sizeof(struct PVFS_server_req_s));
				strcpy((char *)myBuff+sizeof(struct PVFS_server_req_s),
							r->u.crdirent.name);
				strcpy((char *)myBuff+sizeof(struct PVFS_server_req_s)+
							strlen(r->u.crdirent.name)+1,"\0");
				((struct PVFS_server_req_s *)myBuff)->u.crdirent.name = 
					(char *)myBuff+sizeof(struct PVFS_server_req_s);
				return(myBuff);
				break;
			case PVFS_SERV_GETCONFIG:
				/* TODO: FREE THE FS PTR */
				myBuff = BMI_memalloc(q,
											 sizeof(struct PVFS_server_req_s)+
											 	strlen(r->u.getconfig.fs_name)+1,
											 BMI_SEND_BUFFER);
				memcpy(myBuff,r,sizeof(struct PVFS_server_req_s));
				strcpy((char *)myBuff+sizeof(struct PVFS_server_req_s),
							r->u.getconfig.fs_name);
				strcpy((char *)myBuff+sizeof(struct PVFS_server_req_s)+
							strlen(r->u.getconfig.fs_name)+1,"\0");
				((struct PVFS_server_req_s *)myBuff)->u.getconfig.fs_name = 
						(char *)myBuff+sizeof(struct PVFS_server_req_s);
				return(myBuff);
				break;
			default:
				gossip_ldebug(SERVER_DEBUG,"Packing Req Op: %d Not Supported\n",r->op);
				return p;
		}
	}
	else // Response
	{
		struct PVFS_server_resp_s *r = p;
		switch(r->op)
		{
			case PVFS_SERV_GETCONFIG:
				myBuff = BMI_memalloc(q,
											 sizeof(struct PVFS_server_req_s)+
											 strlen(r->u.getconfig.meta_server_mapping)+
											 strlen(r->u.getconfig.io_server_mapping)+2,
											 BMI_SEND_BUFFER);
				memcpy(myBuff,r,sizeof(struct PVFS_server_resp_s));
				strcpy((char *)myBuff+sizeof(struct PVFS_server_resp_s),r->u.getconfig.meta_server_mapping);
				strcpy((char *)myBuff+
						    sizeof(struct PVFS_server_resp_s)+
							 strlen(r->u.getconfig.meta_server_mapping)+1,
							 r->u.getconfig.io_server_mapping);
				strcpy((char *)myBuff+
						    sizeof(struct PVFS_server_resp_s)+
							 strlen(r->u.getconfig.meta_server_mapping)+
							 strlen(r->u.getconfig.io_server_mapping)+1,
							 "\0");
				return(myBuff);
				break;
			default:
				gossip_ldebug(SERVER_DEBUG,"Packing Resp Op: %d Not Supported\n",r->op);
				return p;
				break;
		}
	}
	return p;
}

void *unpack_pvfs_struct(void *p,int type,int q,int z)
{
	if(type == 1) //req
	{
		struct PVFS_server_req_s *r = p;
		switch(r->op)
		{
			case PVFS_SERV_CREATEDIRENT:
				r->u.crdirent.name = (char*)r + sizeof(struct PVFS_server_req_s);
				break;
			case PVFS_SERV_GETCONFIG:
				r->u.getconfig.fs_name = (char*)r + sizeof(struct PVFS_server_req_s);
				break;
			default:
				gossip_ldebug(SERVER_DEBUG,"Unpacking Req Op: %d Not Supported\n",r->op);
				return p;
		}
		return p;
	}
	else // Response
	{
		struct PVFS_server_resp_s *r = p;
		switch(r->op)
		{
			case PVFS_SERV_GETCONFIG:
				r->u.getconfig.meta_server_mapping = (char*)r + sizeof(struct PVFS_server_resp_s);
				r->u.getconfig.io_server_mapping = (char*)r + sizeof(struct PVFS_server_resp_s)
																+ strlen(r->u.getconfig.meta_server_mapping)+1;
				return r;
				break;
			default:
				gossip_ldebug(SERVER_DEBUG,"Unpacking Resp Op: %d Not Supported\n",r->op);
				return p;
			break;
		}
		return r;
	}
	return p;
}
