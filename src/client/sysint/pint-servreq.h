
#ifndef _PINT_SERVREQ_H
#define _PINT_SERVREQ_H

#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pvfs2-req-proto.h>

/* Function Prototypes */
int pint_serv_lookup_path(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *serv_addr);
int pint_serv_getattr(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_setattr(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials, PVFS_size handlesize,\
		bmi_addr_t *serv_arg);
int pint_serv_truncate(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_mkdir(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_readdir(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_rmdir(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_crdirent(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_rmdirent(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_create(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_remove(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);
int pint_serv_getconfig(struct PVFS_server_req_s **req_job,\
		struct PVFS_server_resp_s **resp_job,void *req,\
		PVFS_credentials credentials,bmi_addr_t *server);

/* dunno where these belong, but here is better than nowhere. -- rob */
void debug_print_type(void* thing, int type);
PVFS_msg_tag_t get_next_session_tag(void);
#endif
