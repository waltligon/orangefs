/* this file includes a couple functions to initiate all kinds resources and free them
 * when needed. */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "ncac-list.h"

#include "internal.h"
#include "radix.h"

extern int posix_memalign(void **memptr, size_t alignment, size_t size);

/* global variable */
NCAC_dev_t NCAC_dev;
struct inode *inode_arr[1000];

static inline void init_free_extent_list(int num);
static inline void init_free_req_list(int num);
static inline void init_cache_stack_list(void);

unsigned long radix_get_value(const void *item);


/* cache_init(): initiate cache. */

int cache_init(NCAC_info_t *info)
{
    int reqnum;
    void *tmp;

    if ( info->max_req_num == -1 ) reqnum = MAX_DELT_REQ_NUM;
    else reqnum = info->max_req_num;

    NCAC_dev.free_req_src = (NCAC_req_t*) malloc(reqnum*sizeof(struct NCAC_req));
    if ( NCAC_dev.free_req_src == NULL){
        fprintf(stderr, "cache_init: cannot allocate request memory\n");
        return -ENOMEM;
    }
    memset( NCAC_dev.free_req_src, 0, reqnum*sizeof(struct NCAC_req) );
    init_free_req_list(reqnum);

    NCAC_dev.extsize = 32768;
    NCAC_dev.extlog2 = 15;

    NCAC_dev.cachesize = info->cachesize;
    //NCAC_dev.cachemem  = (char *) malloc(NCAC_dev.cachesize);

    posix_memalign( &tmp, getpagesize(), NCAC_dev.cachesize);
    if ( tmp == NULL ) {
        fprintf(stderr, "cache_init: cannot allocate cache buffers.\n");
        return -ENOMEM;
    }
    NCAC_dev.cachemem = tmp;

    /* we expect extcnt is a power of two number */
    NCAC_dev.extcnt = (NCAC_dev.cachesize/NCAC_dev.extsize);

    NCAC_dev.free_extent_src = (struct extent *)malloc(NCAC_dev.extcnt*sizeof(struct extent) );
    memset(NCAC_dev.free_extent_src, 0, NCAC_dev.extcnt*sizeof(struct extent) );
    if ( NCAC_dev.free_extent_src == NULL){
        fprintf(stderr, "cache_init: cannot allocate extent memory\n");
        return -ENOMEM;
    }

    init_free_extent_list(NCAC_dev.extcnt);
    init_cache_stack_list();


    INIT_LIST_HEAD( &NCAC_dev.prepare_list);
    INIT_LIST_HEAD( &NCAC_dev.bufcomp_list);
    INIT_LIST_HEAD( &NCAC_dev.comp_list);


    memset( inode_arr, 0, sizeof(struct inode*)*1000 );

    NCAC_dev.get_value = radix_get_value;
    NCAC_dev.max_b     = RADIX_MAX_BITS;

    fprintf(stderr, "CACHE SUMMARY:\n");
    fprintf(stderr, "---- req num:%d\t	ext num: %ld\t\n", reqnum, NCAC_dev.extcnt);

    return 0;
}


/* for radix tree if linux radix tree is not used */
unsigned long radix_get_value(const void *item)
{
    return ((struct extent *)item)->index;
}

static inline void init_free_extent_list(int num)
{
    int i;
    struct extent *start;
    struct cache_stack *cache;
    struct list_head *head;

    cache = &NCAC_dev.cache_stack;
    start = NCAC_dev.free_extent_src;


    head = &cache->free_extent_list;
    INIT_LIST_HEAD( head );

    for (i = 0; i < num; i++ ){
        list_add_tail( &start[i].list, head );
        //DPRINT("%d: %p\n", i, start+i);
        start[i].addr = NCAC_dev.cachemem+i*NCAC_dev.extsize;
        start[i].ioreq = INVAL_IOREQ;
    }
    cache->nr_free = num;
    cache->nr_dirty = 0;
}

static inline void init_free_req_list(int num)
{
    int i;
    struct NCAC_req *start;
    struct list_head *head;

    head = &NCAC_dev.free_req_list;
    start = NCAC_dev.free_req_src;

    INIT_LIST_HEAD(head);
    for (i = 0; i < num; i++ ){
        list_add_tail(&start[i].list, head);
        //DPRINT("%d: %p\n", i, start+i);

        start[i].id = i;
        start[i].reserved_cbufcnt = 0;
    }
    NCAC_dev.free_req_num = num;
	spin_lock_init(&NCAC_dev.req_list_lock);
	
}

static inline void init_cache_stack_list()
{
    struct cache_stack *cache;

    cache = &NCAC_dev.cache_stack;

    cache->nr_active = cache->nr_inactive = 0;
    INIT_LIST_HEAD( &cache->active_list);
    INIT_LIST_HEAD( &cache->inactive_list);
	spin_lock_init(&cache->lock);
}
