#ifndef __CACHE_NETWORK_H
#define __CACHE_NETWORK_H

#include "pvfs2-types.h"

/* cache operation flags */

#define     NCAC_READ   	1
#define     NCAC_WRITE  	2
#define     NCAC_BUF_READ    3
#define     NCAC_BUF_WRITE   4
#define     NCAC_QUERY          5
#define     NCAC_DEMOTE         6
#define     NCAC_SYNC           7

typedef int cache_optype;  


/* cache policy on each request */
enum {
    LRU_CACHE   = 1,
    MRU_CACHE   = 2,
    NO_CACHE_   = 3,
    ARC_CACHE   = 4,
};
 
struct cache_hints
{
    int policy;
    /* add more later */
};
typedef struct cache_hints cache_hints_t;


/**********************************************************
 * define related structures for the interactions between *
 * networks and the cache				  *
 **********************************************************/

struct cache_descriptor
{
    PVFS_fs_id  	coll_id;  
    PVFS_handle 	handle; /* these two direct to the right object */
	PVFS_context_id context_id;
    
    /* support a list of regions */
    int 	stream_array_count;
    PVFS_offset	*stream_offset_array; /* offset array in the object */
    PVFS_size	*stream_size_array;
    
    void 	*buffer; /* whether data is placed in this buffer */
    PVFS_size	len;	 /* how many data in the buffer */

    struct cache_hints chints; /* cache hints */
};
typedef struct cache_descriptor cache_desc_t;

/* read request desccriptor: read request to the cache module.
 * If "buffer" is not NULL, the caller expects that the cache
 * module to place place data into it.
 */

struct cache_read_descriptor
{
    PVFS_fs_id  coll_id;  
    PVFS_handle handle; /* these two direct to the right object */
	PVFS_context_id context_id;
    
    /* support a list of regions */
    int 	stream_array_count;
    PVFS_offset	*stream_offset_array; /* offset array in the object */
    PVFS_size	*stream_size_array;
    
    void 	*buffer; /* whether data is placed in this buffer */
    PVFS_size	len;	 /* how many data in the buffer */

    struct cache_hints chints; /* cache hints */
};
typedef struct cache_read_descriptor cache_read_desc_t;


/* write request desccriptor: write request to the cache module.
 * If "buffer" is not NULL, the caller has put the written data
 * into it. 
 */

struct cache_write_descriptor
{
    PVFS_fs_id  coll_id;  
    PVFS_handle handle;  /* these two direct to the right object */
	PVFS_context_id context_id;

    /* support a list of regions */
    int 	stream_array_count;   /* how many regions */
    PVFS_offset	*stream_offset_array; /* region address in the object */
    PVFS_size	*stream_size_array;   /* size in each region */

    void 	*buffer; /* whether data comes from this buffer */
    PVFS_size	len;	 /* how many data in the buffer */
    
    struct cache_hints chints; /* cache hints */
};
typedef struct cache_write_descriptor cache_write_desc_t;



/* sync request: to sync all cached dirty data into disk. 
 * if "coll_id" is PVFS_FS_ANY and "handle" is PVFS_HNDL_ANY,
 * it asks for "sync". Otherwise, it asks for "fdatasync"
 * on a particular "coll_id, handle" tuple.
 */ 

struct cache_sync_descriptor
{
    PVFS_fs_id  coll_id;  
    PVFS_handle handle; 
};
typedef struct cache_sync_descriptor cache_sync_desc_t;


/* cache_req_handle: after posting a request to the cache,
 * tha caller receives a request handle and then uses it to 
 * track the status of the posted request.
 * status is inidcate the request status. During the lifetime
 * of a request, the "status" changes over time.
 *
 * It is "illegal" to touch "internal_id" in the caller. 
 */ 

struct cache_req_handle
{
    volatile int status;        /* Indicate if complete */
    PVFS_size    real_len;      /* real data length in bytes */     

    cache_optype optype;	/* read/write/demotion */
    int          internal_id;	/* internal id tracked by the cache */

};
typedef struct cache_req_handle cache_request_t;


/* cache_reply_descriptor: information about buffers related to a request.
 * For read request, data are in buffers listed in "vector".
 * For write request, the network can write data into them.
 * The number of buffers is indicated by "count".
 * 
 * <count, cbuf_offset_array, cbuf_size_array> work together
 * to show how many buffers are used for read/write data for
 * the request, the buffers's adresses, and length in each buffer.
 *
 * When ``more flag'' is set, it indicates that this request is not
 * finished yet. That is, for read, only a part of requested data 
 * has been available for communication; for write, only a part of
 * buffer space is available. This "flag" gives us opportunity
 * to overlap communication and I/O. A large I/O can be divided
 * into several segments.
 */

struct cache_reply_descriptor
{
    int       count;	           /* how many noncontiguous segments */
    char      **cbuf_offset_array; /* seg. buffer address array */ 
    PVFS_size *cbuf_size_array;    /* seg. size array */
    int       *cbuf_flag;

    int       more_flag;           /* whether this is the last round */ 
    int       errval;	           /* any error code; 0 for none */
};
typedef struct cache_reply_descriptor cache_reply_t;


/* cache info structure.
 * 
 * 
 */
struct cache_info
{
    /* general information */
    int   total_size;  /* cache size in bytes */
    int   free_size;   /* free buffer size in bytes */  
    int   unit_size;   /* unit size in cache for real I/O */ 

    /* specific info for a particular request */
    int   cached_percentage;    /* the percenage of data cached (0--100) */
    int   expected_seg_size;   /* Given a request, the cache expects 
                                * that the caller can break the request 
                                * into segments with size of 
                                * "expected_seg_size". */ 

    /* add other later */
};
typedef struct cache_info cache_info_t;

/**********************************************************
 * functions in the I/O data path                         *
 **********************************************************/

/* read request submit */
int cache_read_post(cache_read_desc_t *desc, 
                    cache_request_t *request,
                    cache_reply_t *reply,
                    void *user_ptr);

/* write request submit */
int cache_write_post(cache_write_desc_t *desc,
                     cache_request_t *request,
                     cache_reply_t *reply,
                     void *user_ptr);

/* sync request submit */
int cache_sync_post(cache_sync_desc_t *desc,
                    cache_request_t *request,
		    void *user_ptr);

int cache_req_test(cache_request_t *request, 
                   int *flag,
                   cache_reply_t *reply,
                   void *user_ptr);

int cache_req_testsome(int count, 
                       cache_request_t *request, 
                       int *outcount, int *indices, 
                       cache_reply_t *reply,
                       void *user_ptr);


int cache_req_done(cache_request_t *request);

int cache_query_info(void *desc, cache_info_t *cache_info);

#endif  /* __CACHE_NETWORK_H */
