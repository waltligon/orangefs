/* This file contains functions to perform reads/writes with
 * the caller supplied buffers.
 * For read, the data is copied from the cache to the caller
 * supplied buffers.
 * For write, the data is copied into the cache from the caller
 * supplied bufers.
 * In both cases, the cache buffer will not be used for 
 * communication.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "internal.h"
#include "state.h"


int NCAC_do_a_bufread_job(struct NCAC_req *ncac_req)
{

#if 0
    int ret;
    int seg, cnt;
    int rcomm=0;

	int i;
	unsigned long total_len, copied_len, len;
	char * copied_buf;

    /* only one contiguous segment */
    if ( !ncac_req->offcnt ) { 
        ret = NCAC_do_one_piece_read( ncac_req, ncac_req->pos, 
									  ncac_req->size, 
									  ncac_req->foff, 
                                        ncac_req->cbufoff, 
									  ncac_req->cbufsize, ncac_req->cbufhash,
 									  ncac_req->cbufflag, 
									  ncac_req->cbufrcnt,
									  ncac_req->cbufwcnt,
									  &cnt);
        if ( ret < 0) {
            ncac_req->error = NCAC_JOB_PROCESS_ERR;
            ncac_req->status = NCAC_ERR_STATUS;
            return ret;
        }
    }else{

        /* Handle each contiguous piece one by one. */
        
        cnt = 0;
        for (seg = 0; seg < ncac_req->offcnt; seg ++) {
            ret = NCAC_do_one_piece_read( ncac_req, ncac_req->offvec[seg],
                                          ncac_req->sizevec[seg],
									        ncac_req->foff + cnt, 
                                          ncac_req->cbufoff + cnt, 
                                          ncac_req->cbufsize + cnt, 
                                          ncac_req->cbufhash + cnt, 
                                          ncac_req->cbufflag + cnt,
									      ncac_req->cbufrcnt + cnt,
									      ncac_req->cbufwcnt + cnt, 
										  &seg );
            if ( ret < 0) {
            	ncac_req->error = NCAC_JOB_PROCESS_ERR;
            	ncac_req->status = NCAC_ERR_STATUS;
            	return ret;
            }
            cnt += seg;
        }
    }

    for (seg = 0; seg < ncac_req->cbufcnt; seg ++)
         if (ncac_req->cbufflag[seg] == 1) rcomm++;
         
    if (rcomm == ncac_req->cbufcnt) ncac_req->status = NCAC_COMPLETE;
    else if (!rcomm) ncac_req->status = NCAC_REQ_SUBMITTED;
    else ncac_req->status = NCAC_PARTIAL_PROCESS;

    if ( ncac_req->status == NCAC_COMPLETE && ncac_req->usrlen ){

		/* copy data into the caller supplied buffers. */
 		total_len = ncac_req->usrlen;
		copied_len = 0;
		copied_buf = ncac_req->usrbuf;
		
		for ( i = 0; i < ncac_req->cbufcnt; i ++ ) {
			len = ncac_req->cbufsize[i];
			if ( len > total_len - copied_len ) 
				len = total_len - copied_len;
			memcpy( copied_buf + copied_len, ncac_req->cbufoff[i], len);
			copied_len += len;
			if ( copied_len == total_len ) break;
        }

		/* release all cache extents */
		ret = NCAC_extent_done_access( ncac_req );
		if ( ret < 0 ) {
            ncac_req->error = NCAC_REQ_DONE_ERR;
            ncac_req->status = NCAC_ERR_STATUS;
			return ret;
		}
		
	}
#endif

    return 0;

}

int NCAC_do_a_bufwrite_job(struct NCAC_req *ncac_req)
{
	
#if 0
    int ret;
    int seg, cnt;
    int rcomm=0;

	int i;
	unsigned long total_len, copied_len, len;
	char * copied_buf;

    /* only one contiguous segment */
    if ( !ncac_req->offcnt ) { 
        ret = NCAC_do_one_piece_write(  ncac_req, ncac_req->pos, 
										ncac_req->size, 
										ncac_req->cbufoff, ncac_req->cbufsize, 
										ncac_req->cbufhash, ncac_req->cbufflag, 
									    ncac_req->cbufrcnt,
									    ncac_req->cbufwcnt,
										&cnt );
        if ( ret < 0) {
            ncac_req->error = NCAC_JOB_PROCESS_ERR;
            ncac_req->status = NCAC_ERR_STATUS;
            return ret;
        }
    }else{

        /* Handle each contiguous piece one by one. */
        
        cnt = 0;
        for (seg = 0; seg < ncac_req->offcnt; seg ++) {
            ret = NCAC_do_one_piece_write( ncac_req, ncac_req->offvec[seg],
                                           ncac_req->sizevec[seg],
                                           ncac_req->cbufoff + cnt, 
                                           ncac_req->cbufsize + cnt, 
                                           ncac_req->cbufhash + cnt, 
                                           ncac_req->cbufflag + cnt, 
							    		   ncac_req->cbufrcnt + cnt,
									       ncac_req->cbufwcnt + cnt,
										   &seg );
            if ( ret < 0) {
            	ncac_req->error = NCAC_JOB_PROCESS_ERR;
            	ncac_req->status = NCAC_ERR_STATUS;
            	return ret;
            }
            cnt += seg;
        }
    }

    for (seg = 0; seg < ncac_req->cbufcnt; seg ++)
         if (ncac_req->cbufflag[seg] == 1 ) rcomm++;
         
    if (rcomm == ncac_req->cbufcnt) ncac_req->status = NCAC_COMPLETE;
    else if (!rcomm) ncac_req->status = NCAC_REQ_SUBMITTED;
    else ncac_req->status = NCAC_PARTIAL_PROCESS;

    if ( ncac_req->status == NCAC_COMPLETE && ncac_req->usrlen ){

		/* copy data from the caller supplied buffers to the extent buffers. */
 		total_len = ncac_req->usrlen;
		copied_len = 0;
		copied_buf = ncac_req->usrbuf;
		
		for ( i = 0; i < ncac_req->cbufcnt; i ++ ) {
			len = ncac_req->cbufsize[i];
			if ( len > total_len - copied_len ) 
				len = total_len - copied_len;
			memcpy( ncac_req->cbufoff[i], copied_buf + copied_len, len);
			copied_len += len;
			if ( copied_len == total_len ) break;
        }

		/* release all cache extents */
		ret = NCAC_extent_done_access( ncac_req );
		if ( ret < 0 ) {
            ncac_req->error = NCAC_REQ_DONE_ERR;
            ncac_req->status = NCAC_ERR_STATUS;
			return ret;
		}
		
	}
#endif

    return 0;
}

