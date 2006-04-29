#ifndef _SYNCH_SERVER_MAPPING_H
#define _SYNCH_SERVER_MAPPING_H

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "pvfs2.h"

extern int 	PINT_initialize_synch_server_mapping_table(void);
extern void PINT_finalize_synch_server_mapping_table(void);
extern int 	PINT_synch_server_mapping(
					enum PVFS_synch_method method, 
					PVFS_handle handle, PVFS_fs_id fsid,
					struct sockaddr *synch_server);

#endif
