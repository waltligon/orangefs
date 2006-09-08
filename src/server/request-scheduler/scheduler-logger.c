/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
 
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <string.h>

#include "scheduler-logger.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pvfs2-internal.h"

#include "red-black-tree.h"
#include "simplequeue.h"

/*
 * Store statistics in memory, as soon as more than MAX_LOGGED_HANDLES are 
 * there free old entries.
 */

typedef struct {
    PVFS_fs_id   fsid;
    
    PVFS_request_statistics req;

    simplequeue     lru_queue;
    int             cnt_handles;
    red_black_tree  handles;
} fs_info;

typedef struct handle_info_t {
    PVFS_handle  handle;
    
    simplequeue_elem * queue_link; 
    
    PVFS_request_statistics req;
} handle_info;


/* several compare functions:  */
static int compare2_fs(RBData * data, RBKey * key);
static int compare2_handle(RBData * data, RBKey * key);
static int compare_fs(RBData * data, RBKey * key);
static int compare_handle(RBData * data, RBKey * key);
/*******************************/

inline void increase_stats(
    PVFS_request_statistics * stats, 
    enum sched_log_type type,
    uint64_t         access_size
    ); 
    
static int print_fs_stat(RBData* data, void* funcData);

static red_black_tree   fs_tree;


void scheduler_logger_log_io(
    PVFS_fs_id   fsid,
    PVFS_handle  handle,
    PVFS_size    access_size,
    enum sched_log_type type)
{
    tree_node* fs_node;
    tree_node* handle_node;
    
    fs_info  * fs_info_p;
    handle_info * handle_info_p;
    
    fs_node = lookupTree(& fsid, & fs_tree);
    if ( fs_node == NULL )
    {
        /* create a new one */
        fs_info_p = (fs_info*) malloc(sizeof(fs_info));
        fs_info_p->fsid = fsid;
        initRedBlackTree(& fs_info_p->handles, compare_handle, compare2_handle);
        memset( & fs_info_p->req, 0, sizeof(PVFS_request_statistics) );
        fs_info_p->cnt_handles = 0;
        
        init_simple_queue(& fs_info_p->lru_queue);
        /* add node to tree */
        insertKeyIntoTree( (void *) fs_info_p, & fs_tree );
    }
    else
    {
        fs_info_p = (fs_info*) fs_node->data;
    }
    
    handle_node = lookupTree(& handle, & fs_info_p->handles);
    
    if ( handle_node == NULL )
    {
        tree_node * node;
        /* create a new one */
        handle_info_p = (handle_info *) malloc(sizeof(handle_info));
        handle_info_p->handle = handle;
        memset( & handle_info_p->req, 0, sizeof(PVFS_request_statistics) );
        
        /* add node to tree */
        node = insertKeyIntoTree( (void *) handle_info_p, & fs_info_p->handles );
        /* add to lru list and link each together */
        handle_info_p->queue_link = push_front_simple((void *) node, & fs_info_p->lru_queue );
        
        if ( fs_info_p->cnt_handles  < MAX_LOGGED_HANDLES_PER_FS )
        {
            fs_info_p->cnt_handles++;
        }
        else
        {   
            handle_info * old_info;
                        
            /* get oldest entry and remove from tree */
            node = (tree_node *) pop_back_simple(& fs_info_p->lru_queue );
            old_info = (handle_info *) node->data;
            
            /* remove from tree: */
            deleteNodeFromTree(node, & fs_info_p->handles);

            free( old_info );
        }
         
    }
    else
    {
        handle_info_p = (handle_info *)  handle_node->data;
        
        relink_front_simple(handle_info_p->queue_link, & fs_info_p->lru_queue);
    }
    
    /*
     * Add statistics now:
     */
     increase_stats( & fs_info_p->req , type, (uint64_t) access_size);
     increase_stats( & handle_info_p->req , type, (uint64_t) access_size);
}

void scheduler_logger_initalize(void)
{
    initRedBlackTree(& fs_tree, compare_fs, compare2_fs);
}

#define req_p(p) ((double) ((fs_info_p->req.acc_multiplier[p] * \
    ((int64_t)-1) + fs_info_p->req.acc_size[ p ])))/1024.0 /1024.0  

static int print_fs_stat(RBData* data, void* funcData)
{
    fs_info  * fs_info_p = (fs_info *) data; 

    gossip_debug(GOSSIP_REQ_SCHED_DEBUG, " FS:%ld active_handles:%d Read_no:%lld Write_no:%lld \n"
        "\tTotal accessed MByte: Read:%f Write:%f\n", 
        (long)(fs_info_p->fsid),
        fs_info_p->cnt_handles,
        lld(fs_info_p->req.io_number[SCHED_LOG_READ]),
        lld(fs_info_p->req.io_number[SCHED_LOG_WRITE]),
        req_p(SCHED_LOG_READ),
        req_p(SCHED_LOG_WRITE)
        );        
    return 0;
}

void scheduler_logger_finalize(void)
{
    int debug_on;
    uint64_t mask;

    gossip_get_debug_mask(&debug_on, &mask);
    if ( debug_on && (mask & GOSSIP_REQ_SCHED_DEBUG) ){
        iterateRedBlackTree(print_fs_stat, & fs_tree , NULL);
    }
}

int scheduler_logger_fetch_data(PVFS_fs_id fsid, int * inout_count,
    PVFS_request_statistics * fs_stat, 
    PVFS_handle_request_statistics * h_stats)
{
    int cnt = *inout_count;
    int i;
    simplequeue_elem * act;
    tree_node* node;
    handle_info * handle_info_p;
    
    tree_node* fs_node;
    fs_info  * fs_info_p;
    
    fs_node = lookupTree(& fsid, & fs_tree);
    if ( ! fs_node ) {
        * inout_count = 0;
        return 0;
    }
    fs_info_p = (fs_info*) fs_node->data;
    
    /* first: fs stats */
    memcpy(fs_stat, & fs_info_p->req, sizeof(PVFS_request_statistics));
    
    /* handle stats */
    if( cnt > fs_info_p->cnt_handles )
    {
        /* not so much entries available */
        cnt = fs_info_p->cnt_handles;
        *inout_count = cnt;
    }

    act = fs_info_p->lru_queue.head;
    for( i=0 ; i < cnt; i++)
    {
        node =  (tree_node*) act->data;
        handle_info_p = (handle_info *) node->data;
        memcpy( & h_stats[i].stat, 
            & handle_info_p->req, sizeof(PVFS_request_statistics));
        h_stats[i].handle = handle_info_p->handle;
        act = act->next;
    }
    
    return 0;    
}


static int compare2_fs(RBData * data, RBKey * key)
{
    fs_info * ikey1=((fs_info *) data);
    fs_info * ikey2=((fs_info *) key);       
    
    if(ikey1->fsid > ikey2->fsid ){ /*take right neigbour*/
        return +1;
    }else if(ikey1->fsid < ikey2->fsid ){/*take left neigbour*/
        return -1;
    }   
    return 0;
}

static int compare2_handle(RBData * data, RBKey * key)
{
    handle_info * ikey1=((handle_info *) data);
    handle_info * ikey2=((handle_info *) key);       
    
    if(ikey1->handle > ikey2->handle ){ /*take right neigbour*/
        return +1;
    }else if(ikey1->handle < ikey2->handle ){/*take left neigbour*/
        return -1;
    }
    return 0;
}

static int compare_fs(RBData * data, RBKey * key)
{
    fs_info * ikey1=((fs_info *) data);
    PVFS_fs_id ikey2= *((PVFS_fs_id *) key);       
    
    if(ikey1->fsid > ikey2 ){ /*take right neigbour*/
        return +1;
    }else if(ikey1->fsid < ikey2 ){/*take left neigbour*/
        return -1;
    }   
    return 0;
}

static int compare_handle(RBData * data, RBKey * key)
{
    handle_info * ikey1=((handle_info *) data);
    PVFS_handle ikey2= *((PVFS_handle *) key);       
    
    if(ikey1->handle > ikey2 ){ /*take right neigbour*/
        return +1;
    }else if(ikey1->handle < ikey2 ){/*take left neigbour*/
        return -1;
    }
    return 0;
}


inline void increase_stats(
    PVFS_request_statistics * stats, 
    enum sched_log_type type,
    uint64_t         access_size
    )
{
    stats->io_number[type]++;
    
    if ( (stats->acc_size[type] + access_size) >= stats->acc_size[type] ) 
    {
        stats->acc_size[type]+= access_size;
    }
    else
    /* rollover */
    {
        stats->acc_multiplier[type]++;
        stats->acc_size[type]+= access_size;
    }
}


 
 /*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
