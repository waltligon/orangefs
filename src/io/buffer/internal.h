#ifndef CACHE_INTERNAL_H
#define CACHE_INTERNAL_H

#include <string.h>
#include <pthread.h>
#include "ncac-interface.h"
#include "ncac-list.h"
#include "radix.h"
#include "aiovec.h"
#include "flags.h"
#include "ncac-locks.h"


typedef cache_desc_t NCAC_desc_t;
typedef int          NCAC_optype;
typedef cache_reply_t NCAC_reply_t;

typedef pthread_mutex_t NCAC_lock;

struct NCAC_cache_info{
    int max_req_num;
    int extsize;
    int cachesize;
    void *cachespace;
};

struct cache_stack {
    struct list_head list;
    NCAC_lock 	 	 lock;
    unsigned long    nr_inactive;
    unsigned long    nr_active;
    struct list_head active_list;
    struct list_head inactive_list;

    unsigned long 	 nr_free;
    struct list_head free_extent_list;
	unsigned long    nr_dirty;
	unsigned long    nr_writeback;
	unsigned long    ratelimits;
};

typedef struct NCAC_cache_info NCAC_info_t;

struct NCAC_dev{
    unsigned long extsize;
    unsigned long extlog2;

    unsigned long cachesize;
    unsigned long extcnt;
    char *cachemem;

    unsigned long mrwusize;

    /* free req list */
    struct list_head free_req_list;
    struct NCAC_req  *free_req_src;
    unsigned int     free_req_num;

    /* a non-free request could be in one of the three lists */
    struct list_head prepare_list;
    struct list_head bufcomp_list;
    struct list_head comp_list;
    
    /* lock to manage requests in different lists */
    NCAC_lock        req_list_lock;

    /* extent list */
    struct extent *free_extent_src;
    NCAC_lock  extent_list_lock;
    

    /* cache stack */
    struct cache_stack cache_stack;

    /* for radix tree if LINUX radix tree is not used */
    unsigned long (* get_value)(const void *);
    int max_b;
    
};

typedef struct NCAC_dev NCAC_dev_t;


struct NCAC_req{
   int  id;
   int  optype;
   int  status;
   int  error;
   PVFS_fs_id   	coll_id;
   PVFS_handle  	handle;
   PVFS_context_id  context_id;

   PVFS_size usrlen;
   PVFS_size written;
   char *usrbuf;

   char ** cbufoff;
   PVFS_size *cbufsize;
   int *cbufflag;
   struct extent **cbufhash;
   int *cbufrcnt;
   int *cbufwcnt; 
   
   int  cbufcnt;
   int reserved_cbufcnt;

   PVFS_offset pos;
   PVFS_size   size;

   PVFS_offset *offvec;
   PVFS_size   *sizevec;
   int         offcnt;


   struct inode *mapping;
   struct aiovec *aiovec;
   int ioreq;

   int read_out;
   struct list_head list;

   int nr_dirty;
   //struct list_head dirty_list;
};

typedef struct NCAC_req NCAC_req_t;


#define  NCAC_READ_PENDING 1
#define  NCAC_WRITE_PENDING 2 
#define  NCAC_COMM_READ_PENDING  3 
#define  NCAC_COMM_WRITE_PENDING 4 
#define  NCAC_BLANK 		 5 
#define  NCAC_CLEAN 		 6 
#define  NCAC_UPTODATE 		 7

/* this is an inode-like structure for each
 * object <coll_id, handle>
 */
struct inode
{
    NCAC_lock  lock;

    PVFS_fs_id   	 coll_id;
    PVFS_handle      handle;
    PVFS_context_id  context_id;

    struct radix_tree_root page_tree;

    struct list_head clean_pages;
    struct list_head dirty_pages;
    unsigned long nrpages;
    int  nr_dirty;

    struct aiovec aiovec;
    struct cache_stack *cache_stack;
};


struct extent {
   unsigned long   flags;
   int 		status;
   char 	*addr;
   int  	id;
   unsigned long index;

   struct list_head  list;
   struct list_head  lru;

   unsigned int writes;
   unsigned int reads;
   unsigned int rcmp;
   unsigned int wcmp;

   struct extent *next;
   struct inode *mapping;

   int  	ioreq;
   struct extent *ioreq_next;

};



#define MAX_DELT_REQ_NUM 10000


#define NCAC_OK			0
#define NCAC_REQ_BUILD_ERR    -1000
#define NCAC_SUBMIT_ERR     -1001
#define NCAC_NO_REQ	    -1002
#define NCAC_NO_MEM	    -1003
#define NCAC_JOB_PREPARE_ERR	    -1004
#define NCAC_JOB_PUT_ERR    -1005
#define NCAC_JOB_DO_ERR     -1006
#define NCAC_JOB_OPTYPE_ERR -1007
#define NCAC_JOB_PROCESS_ERR  -1008
#define NCAC_NO_EXT_ERR     -1009
#define NCAC_CACHE_ERR      -1010
#define NCAC_TROVE_AIO_REQ_ERR -1011
#define NCAC_CBUF_CNT_ERR 	-1012
#define NCAC_REQ_STATUS_ERR 	-1013
#define NCAC_REQ_DONE_ERR 	-1014
#define NCAC_INVAL_FLAGS    -1021


/* request status */
#define  NCAC_ERR_STATUS	-1
#define  NCAC_REQ_UNUSED	0
#define  NCAC_REQ_SUBMITTED  	1 
#define  NCAC_PARTIAL_PROCESS 	2
#define  NCAC_BUFFER_COMPLETE   3 
#define  NCAC_COMPLETE       	4 


#define NCAC_GEN_READ   1 
#define NCAC_GEN_WRITE  2 


#define INVAL_IOREQ    -1



extern NCAC_dev_t NCAC_dev;
extern NCAC_req_t  *NCAC_req_list;


int cache_init(NCAC_info_t *info);
NCAC_req_t *NCAC_rwreq_build(NCAC_desc_t*desc, NCAC_optype optype);
int NCAC_rwjob_prepare(NCAC_req_t *ncac_req, NCAC_reply_t *reply );

int NCAC_do_jobs(struct list_head *list, struct list_head *bufcomp_list, struct list_head * comp_list, NCAC_lock *lock);
int NCAC_do_a_job(NCAC_req_t *req, struct list_head *list, struct list_head *bufcomp_list, struct list_head * comp_list, NCAC_lock *lock);

int NCAC_do_one_piece_read(NCAC_req_t *ncac_req, PVFS_offset pos,
                           PVFS_size size, char **cbufoff,
                           PVFS_size *cbufsize, struct extent *cbufhash[],
                           int *cbufflag, int *cbufrcnt, int *cbufwcnt, int *cnt);

int NCAC_do_one_piece_write(NCAC_req_t *ncac_req, PVFS_offset pos,
                           PVFS_size size, char **cbufoff,
                           PVFS_size *cbufsize, struct extent *cbufhash[],
                           int *cbufflag, int *cbufrcnt, int *cbufwcnt, int *cnt);

int NCAC_check_request(int id, struct NCAC_req **ncac_req);
int NCAC_done_request( int id );


static inline struct aiovec *get_aiovec(struct NCAC_req *ncac_req)
{
    return &(ncac_req->mapping->aiovec);
}


/*get_extent_cache_stack():
 * extent ---> inode ---> cache stack
 */
static inline struct cache_stack *get_extent_cache_stack(struct extent *page)
{
    if (page==NULL) return NULL;
    return (page->mapping->cache_stack);
}


static inline void
add_page_to_active_list(struct cache_stack  *cache_stack, struct extent *page)
{
        list_add(&page->lru, &cache_stack->active_list);
        cache_stack->nr_active++;
}

static inline void
add_page_to_inactive_list(struct cache_stack *cache_stack, struct extent *page)
{
        list_add(&page->lru, &cache_stack->inactive_list);
        cache_stack->nr_inactive++;
}

static inline void
del_page_from_active_list(struct cache_stack *cache_stack, struct extent *page)
{
        list_del(&page->lru);
        cache_stack->nr_active--;
}

static inline void
del_page_from_inactive_list(struct cache_stack *cache_stack, struct extent *page)
{
        list_del(&page->lru);
        cache_stack->nr_inactive--;
}

static inline void
del_page_from_lru(struct cache_stack *cache_stack, struct extent *page)
{
        list_del(&page->lru);
        if (PageActive(page)) {
                ClearPageActive(page);
                cache_stack->nr_active--;
        } else {
                cache_stack->nr_inactive--;
        }
}

#define get_cache_stack()  (&NCAC_dev.cache_stack)



#if defined(DEBUG) 

#define DPRINT(fmt, args...) { fprintf(stderr, "[%s:%d]", __FILE__, __LINE__ ); fprintf(stderr, fmt, ## args); fprintf(stderr, "\n"); }

#else

#define DPRINT(fmt, args...)

#endif


#define NCAC_error(fmt, args...) { fprintf(stderr, "[%s:%d]", __FILE__, __LINE__); fprintf(stderr, fmt, ## args); fprintf(stderr, "\n");}

#endif
