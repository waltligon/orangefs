
/* this file contains functions used by the cache to access Trove */

#include "internal.h"
#include "trove.h"
#include "aiovec.h"

static inline void  offset_shorten( int s_cnt, 
					  PVFS_offset *stream_offset_array,
					  PVFS_size *stream_size_array,
					  int m_cnt, 
					  char **mem_offset_array,
					  PVFS_size *mem_size_array,
					  int *new_s_cnt, int *new_m_cnt);


int NCAC_aio_read_ext( PVFS_fs_id coll_id, PVFS_handle handle, 
		       PVFS_context_id context, struct aiovec *aiovec, 
		       int *ioreq)
{
    char **mem_offset_array;
    PVFS_size *mem_size_array;
	PVFS_size off;
    int m_cnt;
    PVFS_offset *stream_offset_array;
    PVFS_size *stream_size_array;
    void *user_ptr_array[1] = { (char *) 13 };

    TROVE_op_id op_id;
	TROVE_size output_size;

    int s_cnt;
    int ret;

    int i;

    m_cnt = s_cnt = aiovec_count(aiovec);

    if ( !m_cnt ) {
		*ioreq = INVAL_IOREQ;
		return -1;
    }

    mem_offset_array = aiovec->mem_offset_array;
    mem_size_array   = aiovec->mem_size_array;

    stream_offset_array = aiovec->stream_offset_array;
    stream_size_array   = aiovec->stream_size_array;

    /* for debug only */

    DPRINT("NCAC_aio_read_ext: info\n");

    for (i=0; i< m_cnt; i++ ) {
        DPRINT( "off:%Ld, size=%Ld, buff:%p, size=%Ld\n", 
				stream_offset_array[i], stream_size_array[i],
				 mem_offset_array[i], mem_size_array[i] );
    }


	/* adjust the offset and length to the I/O unit.
	 * In the current implementation, we read/write over the whole extent. 
	 * At the same time, shorten the iovec list if possible. 
	 */


    for (i=0; i< m_cnt; i++ ) {
		if ( stream_size_array[i] % NCAC_dev.extsize ) {
			stream_size_array[i] = NCAC_dev.extsize;
			mem_size_array[i]    = NCAC_dev.extsize;

			off = stream_offset_array[i] & (NCAC_dev.extsize -1) ;

			stream_offset_array[i] -= off;
			mem_offset_array[i] -= off;
		}

        DPRINT( "off:%Ld, size=%Ld, buff:%p, size=%Ld\n", 
				stream_offset_array[i], stream_size_array[i],
				 mem_offset_array[i], mem_size_array[i] );

    }

    offset_shorten( s_cnt, 
					stream_offset_array,
					stream_size_array,
					m_cnt, 
					mem_offset_array,
					mem_size_array,
					&s_cnt, &m_cnt);

    DPRINT("--------------after offset_shorten: s_cnt=%d\n", s_cnt);
    for (i=0; i< s_cnt; i++ ) {
        DPRINT( "off:%Ld, size=%Ld\n", 
				stream_offset_array[i], stream_size_array[i] );
    }

    DPRINT("--------------after offset_shorten: m_cnt=%d\n", m_cnt);
    for (i=0; i< m_cnt; i++ ) {
        DPRINT( "buff:%p, size=%Ld\n", 
			    mem_offset_array[i], mem_size_array[i] );
    }

    ret = trove_bstream_write_list(coll_id,
                                  handle,
                                  mem_offset_array,
                                  mem_size_array,
                                  m_cnt,
                                  stream_offset_array,
                                  stream_size_array,
                                  s_cnt,
                                  &output_size,
                                  0, /* flags */
                                  NULL, /* vtag */
                                  user_ptr_array,
                                  context,
                                  &op_id);

    if (ret < 0) {
        NCAC_error("trove listio read failed\n");
        return -1;
    }

    *ioreq = op_id;

    DPRINT("NCAC_aio_read_ext: io request=%Ld(%d)\n", op_id, *ioreq);

    return 0;
}

int NCAC_aio_write( PVFS_fs_id coll_id, 
					PVFS_handle handle,
					PVFS_context_id context,
				 	int cnt,
                    PVFS_offset *stream_offset_array, 
					PVFS_size *stream_size_array,
            		char **mem_offset_array,  
					PVFS_size *mem_size_array, 
					int *ioreq) 
{
	int m_cnt, s_cnt;
	PVFS_size off;
    void *user_ptr_array[1] = { (char *) 13 };
    TROVE_op_id op_id;
	TROVE_size output_size;
    int ret;
    int i;

    DPRINT("NCAC_aio_write: info\n");

    m_cnt = s_cnt = cnt;


	/* adjust the offset and length to the I/O unit.
	 * In the current implementation, we read/write over the whole extent. 
	 * At the same time, shorten the iovec list if possible. 
	 */

    for (i=0; i< m_cnt; i++ ) {
		if ( stream_size_array[i] % NCAC_dev.extsize ) {
			stream_size_array[i] = NCAC_dev.extsize;
			mem_size_array[i]    = NCAC_dev.extsize;

			off = stream_offset_array[i] & (NCAC_dev.extsize -1) ;

			stream_offset_array[i] -= off;
			mem_offset_array[i] -= off;
		}

        DPRINT( "off:%Ld, size=%Ld, buff:%p, size=%Ld\n", 
				stream_offset_array[i], stream_size_array[i],
				 mem_offset_array[i], mem_size_array[i] );

    }

    offset_shorten( s_cnt, 
					stream_offset_array,
					stream_size_array,
					m_cnt, 
					mem_offset_array,
					mem_size_array,
					&s_cnt, &m_cnt);


#ifdef DEBUG
    fprintf(stderr, "--------------after offset_shorten: s_cnt=%d\n", s_cnt);
    for (i=0; i< s_cnt; i++ ) {
        fprintf(stderr, "off:%Ld, size=%Ld\n", 
				stream_offset_array[i], stream_size_array[i] );
    }

    fprintf(stderr, "--------------after offset_shorten: m_cnt=%d\n", m_cnt);
    for (i=0; i< m_cnt; i++ ) {
        fprintf(stderr, "buff:%p, size=%Ld\n", 
			    mem_offset_array[i], mem_size_array[i] );
    }
#endif

    ret = trove_bstream_write_list(coll_id,
                                  handle,
                                  mem_offset_array,
                                  mem_size_array,
                                  m_cnt,
                                  stream_offset_array,
                                  stream_size_array,
                                  s_cnt,
                                  &output_size,
                                  0, /* flags */
                                  NULL, /* vtag */
                                  user_ptr_array,
                                  context,
                                  &op_id);

    if (ret < 0) {
        NCAC_error("trove listio read failed\n");
        return -1;
    }

    *ioreq = op_id;

    DPRINT("NCAC_aio_write: io request=%Ld(%d)\n", op_id, *ioreq);

    return 0;
}

/* do read for read modity write.
 * In the current implementation, we read the whole extent. But the input
 * parameters can be used to do finer read.
 */
int do_read_for_rmw(PVFS_fs_id coll_id, PVFS_handle handle, 
					PVFS_context_id context, struct extent *extent, 
					PVFS_offset pos, char * off, int size, int *ioreq)
{
	char * buf;
	TROVE_size inout_size;
    TROVE_op_id op_id;
	
	int ret;

	buf = extent->addr;
	inout_size = NCAC_dev.extsize;

    DPRINT("do_read_for_rmw; pos=%Ld, buf=%p, size=%Ld\n", pos, buf, inout_size);
    ret = trove_bstream_read_at(coll_id, handle,
                                buf, &inout_size,
                                0, 0, NULL, NULL,
                                context, &op_id);

    DPRINT("do_read_for_rmw; req=%Ld\n", op_id);

    *ioreq = op_id;
    return 0;
}



/* NCAC_check_ioreq(): check pending Trove request on an extent.
 * Since a list of extents may be associated with one Trove request,
 * and Trove completion is one time, when a completion occurs, the
 * status of all extents should be changed. This is done by a link
 * of extent->ioreq_next. Using ioreq_list allows us to have
 * more than one Trove requests for a job.
 */

int NCAC_check_ioreq(struct extent *extent)
{
    TROVE_op_id  op_id;
    TROVE_coll_id coll_id;
    TROVE_context_id context_id;
    TROVE_ds_state state;
    int count;
    int ret;

    op_id = extent->ioreq;

    if ( op_id == INVAL_IOREQ ) {
		NCAC_error("invalid trove io req id");	
		return -1;
	}

    coll_id = extent->mapping->coll_id;
    context_id = extent->mapping->context_id;

    DPRINT("NCAC_check_ioreq: req=%Ld, coll_id=%d, context=%d, index=%ld\n", op_id, coll_id, context_id, extent->index);

    ret = trove_dspace_test(coll_id, op_id, context_id, &count, NULL, NULL, &state, TROVE_DEFAULT_TEST_TIMEOUT);

    if ( ret > 0 ) {
    	fprintf(stderr, "++++++++++++NCAC_check_ioreq: finished %Ld\n", op_id);
        extent->ioreq = INVAL_IOREQ;
    }

    return ret;
}


/*
 * Reduce <file offset, len> and <mem offset, len>  pairs in-place.  
 */

static inline void  offset_shorten( int s_cnt, 
					  PVFS_offset *stream_offset_array,
					  PVFS_size *stream_size_array,
					  int m_cnt, 
					  char **mem_offset_array,
					  PVFS_size *mem_size_array,
					  int *new_s_cnt, int *new_m_cnt)
{

    int i = 0;
    int seg = 0;

    /* shorten stream offset */
    while ( i < s_cnt - 1 ) {
        if ( stream_offset_array[seg] + stream_size_array[seg] ==
            stream_offset_array[i+1] ) {

                stream_size_array[seg] += stream_size_array[i+1];

                stream_size_array[i+1] = 0;
        }else {
            seg = i+1;
        }
        i ++ ;
    }

    i = 1; seg = 1;
    while ( i < s_cnt ) {
        if ( stream_size_array[i] != 0 ) {
            if ( i != seg ) {
                stream_size_array[seg] = stream_size_array[i];
                stream_offset_array[seg] = stream_offset_array[i];
            }
            seg ++;
        }
        i++;
    }

    *new_s_cnt = seg;


    i = 0;
    seg = 0;

    /* shorten stream offset */
    while ( i < m_cnt - 1 ) {
        if ( mem_offset_array[seg] + mem_size_array[seg] ==
             mem_offset_array[i+1] ) {

                mem_size_array[seg] += mem_size_array[i+1];

                mem_size_array[i+1] = 0;
        }else {
            seg = i+1;
        }
        i ++ ;
    }

    i = 1; seg = 1;
    while ( i < s_cnt ) {
        if ( mem_size_array[i] != 0 ) {
            if ( i != seg ) {
                mem_size_array[seg] = mem_size_array[i];
                mem_offset_array[seg] = mem_offset_array[i];
            }
            seg ++;
        }
        i++;
    }

    *new_m_cnt = seg;

    return;
}

int init_io_read( PVFS_fs_id coll_id, PVFS_handle handle, 
        PVFS_context_id context, PVFS_offset foffset, 
        PVFS_size size, void *buf, TROVE_op_id *ioreq)
{
    void *user_ptr_array[1] = { (char *) 13 };
    int ret;

    ret = trove_bstream_read_at(coll_id,
                               handle,
                               buf,
                                &size,
                                foffset,
                                  0, /* flags */
                                  NULL, /* vtag */
                                  user_ptr_array,
                                  context,
                                  ioreq);

    if (ret < 0) {
        NCAC_error("trove read at failed\n");
        return -1;
    }
    return 0;
}
