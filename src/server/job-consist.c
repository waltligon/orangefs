#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <string.h>
#include <job-consist.h>


int job_check_consistency(
		PVFS_server_op op,
		PVFS_coll_id coll_id,
		PVFS_handle handle,
		void *user_ptr,
		job_status_s* out_status_p,
		job_id_t* id)
{

	return(1);

}

