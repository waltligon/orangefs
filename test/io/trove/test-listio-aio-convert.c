
/* This is a test to show the problem of dbpf_bstream_listio_convert():
 * The problem is: for the following particular case, it cannot give us
 * what we expect.
 * soff[0]=0, soff[1] = 65536;
 * ssize[0]=0 = sssize[1] = 65536;
 * scnt =2;
 *
 * moff[0]=, moff[1] = , moff[2] = ,  moff[3]= ;
 * msize[0]=msize[1]=msize[2]=msize[3]=32768;

 * the aiocb number should be 4 in one time.
 */


#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "trove.h"
#include "dbpf-bstream.h"

#define AIOCB_ARRAY_SZ 20

int main(int argc, char *argv[])
{
	PVFS_offset soff[100];
	PVFS_size   ssize[100];
	int scnt;

	char *moff[100];
	PVFS_size  msize[100];
	int mcnt;
	char buf[1024*1024];

	struct bstream_listio_state lio_state;
	struct aiocb *aiocb_p;
	int aiocb_inuse_count = AIOCB_ARRAY_SZ;
	int i, ret;
	int cnt;

	soff[0] = 0;
	soff[1] = 65536;
	ssize[0] = ssize[1] = 65536;
	scnt = 2;
	
	moff[0] = buf;
	moff[1] = buf+65536;
	moff[2] = buf+2*65536;
	moff[3] = buf+3*65536;
	msize[0] = msize[1] = msize[2] = msize[3] = 32768;
	mcnt = 4;

	lio_state.mem_ct          = 0;
	lio_state.stream_ct       = 0;
	lio_state.cur_mem_size    = msize[0];
	lio_state.cur_mem_off     = moff[0];
	lio_state.cur_stream_size = ssize[0];
	lio_state.cur_stream_off  = soff[0];

	aiocb_p = (struct aiocb *) calloc(AIOCB_ARRAY_SZ, sizeof(struct aiocb));
	if (aiocb_p == NULL)
	{
		return -ENOMEM;
	}

	for(i = 0; i < AIOCB_ARRAY_SZ; i++)
	{
		aiocb_p[i].aio_lio_opcode = LIO_NOP;
		aiocb_p[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	}


	cnt = 0;
	ret = 0;
	while ( !ret ){
		aiocb_inuse_count = AIOCB_ARRAY_SZ;
		ret = dbpf_bstream_listio_convert( 0, LIO_READ, 
			moff, msize, mcnt, 
			soff, ssize, scnt, 
			aiocb_p, 
			&aiocb_inuse_count, 
			&lio_state);
		if ( ret < 0 ) {
			fprintf(stderr, "error in dbpf_bstream_listio_convert\n");
			return  -1;
		}

		cnt ++;

		if (ret == 1) {
			fprintf(stderr, "we get all converted: aiocb_cnt=%d\n", aiocb_inuse_count);
		}

		if (ret == 0) {
			fprintf(stderr, "we get partially converted: "
                                "aiocb_cnt=%d\n", aiocb_inuse_count);
			fprintf(stderr, "lio_state: mct=%d,sct=%d,"
                                "cur_mem_size: %d, cur_mem_off:%p, "
                                "cur_stream_size: %Ld, cur_stream_off:%Ld\n", 
                                lio_state.mem_ct, lio_state.stream_ct, 
                                lio_state.cur_mem_size, lio_state.cur_mem_off,
                                Ld(lio_state.cur_stream_size), Ld(lio_state.cur_stream_off));
		}
	}

	fprintf(stderr, "convert called: %d times\n",  cnt);

	return 0;
}
