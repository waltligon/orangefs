#ifdef PVFS2_SERVER_MEMORY_CACHE_H
#define PVFS2_SERVER_MEMORY_CACHE_H

void *PINT_fetch_trove_buffer(
	int ,
	int , 
	void **, 
	int ,
	int ,
	job_id_t
	);

void PINT_checkin_trove_buffer(
	TROVE_keyval_s *,
	job_id_t
	);

#endif
