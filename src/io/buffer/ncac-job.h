#ifndef _NCAC_JOB_H
#define _NCAC_JOB_H

/* average size of requests in extents */
#define DELT_DISCARD_NUM 5

int NCAC_do_a_read_job(struct NCAC_req *ncac_req);
int NCAC_do_a_write_job(struct NCAC_req *ncac_req);

int NCAC_do_a_bufread_job(struct NCAC_req *ncac_req);
int NCAC_do_a_bufwrite_job(struct NCAC_req *ncac_req);

int NCAC_do_a_query_job(struct NCAC_req *ncac_req);

int NCAC_do_a_demote_job(struct NCAC_req *ncac_req);

int NCAC_do_a_sync_job(struct NCAC_req *ncac_req);


#endif
