#include <stdio.h>
#include <stdlib.h>

#include "internal.h"
#include "state.h"
#include "cache.h"
#include "flags.h"
#include "ncac-trove.h"


/* this file contains state transition of an extent */

int NCAC_move_inactive_to_active(struct cache_stack *cache_stack,struct extent *page);
int  data_sync_inode(struct inode *inode);

/* NCAC_extent_read_access(): when a cached extent is accessed by a read
 * request, its flags are changing.
 *
 */
int NCAC_extent_read_access(NCAC_req_t *ncac_req, struct extent *page, unsigned long offset, unsigned long size)
{ 
	int error;
	struct cache_stack *cache_stack;
	int ret;

	cache_stack = ncac_req->mapping->cache_stack;

	IncReadCount(page);

	if ( !PageActive(page) && PageLRU(page) )
	{
		cache_lock(&(cache_stack->lock));

		cache_stack = get_extent_cache_stack(page);
		NCAC_move_inactive_to_active(cache_stack, page);
		SetPageActive(page);

		cache_unlock(&(cache_stack->lock));

	}

	/* if there is any write on this extent before this access,
	 * there is nothing we can do at this moment.
	 */
	if ( page->writes > page->wcmp ) {
		ret = 0;
		goto out;
	}

	/* in active list */
	if (PageLRU(page) && PageActive(page)){

		if (PageRcomm(page)){ /* read communication pending */
			ret = 1;
			goto out;
		}

		/* write communication is pending. 
		 * A write is ahead of this read. wait!!!
		 */
		if (PageWcomm(page)) {
			ret = 0; goto out;
		}

		/************************************/
		/* The following cases are those without pending communication */

		/* clean or dirty extent */
		if (PageClean(page) || PageDirty(page) ) {
			SetPageRcomm(page);	ret = 1; 
			goto out;
		}

		/* read pending: Trove read operation is pending.  */
		/* write pending: for example flushing daemon is working on it */
		if (PageReadPending(page) || PageWritePending(page)) {
			error = NCAC_check_ioreq(page);
			if (error <0) {
				NCAC_error("%s:%d NCAC_check_ioreq error\n", __FILE__, __LINE__);
				ret = 0; goto out;
			}

			if (error) {
				/* set all other related extents */
				list_set_clean_page(page);

				/* the request is done and the read is finished.
				 * Note that NCAC_check_ioreq sets page flags in this case.*/
				SetPageRcomm(page);	
				ret = 0; 
				goto out;
			}
			ret = 0; goto out;
		}
				
	}

	/* invalid flags */
	ret = NCAC_INVAL_FLAGS;

out:
	return ret;

}

/* NCAC_extent_write_access(): when a cached extent is accessed by a write
 * request, its flags are changing.
 * According to Linux 2.6, if the extent is cached in the inactive
 * list, it is not moved to the active list.
 */
int NCAC_extent_write_access(NCAC_req_t *ncac_req, struct extent *page, unsigned long offset, unsigned long size)
{ 
	int error;

	IncWriteCount(page);

	/* the first write */
	if ( PageLRU(page) && page->writes - page->wcmp ==1 ){

		if (PageRcomm(page) || PageWcomm(page)) 
			return 0;


		if (PageReadPending(page) || PageWritePending(page)) {
			error = NCAC_check_ioreq(page);
			if (error <0) {
				NCAC_error("%s:%d NCAC_check_ioreq error\n", __FILE__, __LINE__);
				return error;
			}
			if (error) { /* completion */

			/* set all other related extents */
			list_set_clean_page(page);

			/* the request is done and the read is finished.
			 * Note that NCAC_check_ioreq sets page flags in 
			 * this case.*/

			SetPageWcomm(page);	
				return 1;
			}
			return 0;	
		}

		/* No pending comm, clean or dirty extent */
		if (PageClean(page) || PageDirty(page) ) {
			SetPageWcomm(page);	
			return 1;
		}
	}

	/* it is not the first write */
	if ( page->writes - page->wcmp >1 ){
		return 0;
	}

	/* there is pending reads */
	if ( page->reads != page->rcmp  ){
		return 0;
	}

	/* invalid flags */
	return NCAC_INVAL_FLAGS;
}


/* NCAC_extent_first_read_access(): when a new extent is allocated
 * for read, this function is called. It is called before we
 * put it into cache and after we initiate a Trove read request.
 */
int NCAC_extent_first_read_access(NCAC_req_t *ncac_req, struct extent *page)
{
    SetPageReadPending(page); 
    IncReadCount(page);
    return 0;
}

/* NCAC_extent_first_write_access(): when a new extent is allocated
 * for write, this function is called. It is called before we
 * put it into cache.
 */
int NCAC_extent_first_write_access(NCAC_req_t *ncac_req, struct extent *page)
{
	ClearPageBlank(page);
   	SetPageWcomm(page); 
	IncWriteCount(page);
	return 1;
}

/* NCAC_extent_done_access(): when communication is done, we mark
 * each extent to clean/dirty. When it is dirty, add it to the
 * inode dirty list.
 *
 * TODO: buffer information cannot not be changed by the outside.
 * */
int NCAC_extent_done_access(NCAC_req_t *ncac_req)
{
	int  i;
	struct inode *inode;
	struct list_head *dirty_list = NULL;
	struct cache_stack *cache;
	int nr_dirty = 0;
	int ret;

	inode = ncac_req->mapping;

	inode_lock (&inode->lock);

	if (ncac_req->optype == NCAC_READ)
	{
		for (i = 0; i < ncac_req->cbufcnt; i++)
		{
			if (ncac_req->cbufflag[i] && ncac_req->cbufhash[i])
			{
				NCAC_extent_read_comm_done (ncac_req->cbufhash[i]);
				//DecReadCount(ncac_req->cbufhash[i]);
				ncac_req->cbufhash[i]->rcmp++;
			}
      		}
  	}

	if (ncac_req->optype == NCAC_WRITE)
	{
		dirty_list = &(inode->dirty_pages);
		for (i = 0; i < ncac_req->cbufcnt; i++)
		{
			if (ncac_req->cbufflag[i] && ncac_req->cbufhash[i])
			{
				NCAC_extent_write_comm_done (ncac_req->cbufhash[i]);
				//DecWriteCount(ncac_req->cbufhash[i]);
				ncac_req->cbufhash[i]->wcmp++;

				/* add dirty pages */
				list_add_tail (&ncac_req->cbufhash[i]->list, dirty_list);
				nr_dirty++;

			}
		}
		inode->nr_dirty += nr_dirty;

		/* handle sync stuff: there are three cases:
		 * 1) if a file is opend with O_SYNC, we should do sync
		 * 2) if we define aggressive flush, we do sync here.
		 * 3) otherwise, sync depends on our flush policy.
		 * TODO: currently we don't know O_SYNC flag 
		 */

		/* if ( ncac_req->f_flags & O_SYNC || IS_SYNC(inode) ) {
			data_sync(inode);
		   }
		 */

#if defined(LAZY_SYNC)
		ret = balance_dirty_extents (ncac_req->mapping->cache_stack);

#else /* aggressive sync */
		ret = data_sync_inode (inode);
#endif
	}

	inode_unlock (&inode->lock);

	if (dirty_list)
	{				/* atomic add */
		cache = inode->cache_stack;

		cache_lock (&cache->lock);
		cache->nr_dirty += (nr_dirty - ret);
		cache_unlock (&cache->lock);
		DPRINT ("-------nr_dirty=%d, flush=%d\n", nr_dirty, ret);
	}

	return 0;
}

/* read commnication is done */
int NCAC_extent_read_comm_done(struct extent *page)
{
	ClearPageRcomm(page);
	return 0;
}

/* read commnication is done */
int NCAC_extent_write_comm_done(struct extent *page)
{
	ClearPageWcomm(page);
	ClearPageClean(page);
   	SetPageDirty(page); 
	return 0;
}

void list_set_clean_page(struct extent *page)

{
    struct extent *next;

    int cnt = 0;  

    DPRINT("iodone before: flags:%lx\n", page->flags);
    ClearPageDirty(page);
    SetPageClean(page);
    ClearPageReadPending(page);
    ClearPageWritePending(page);

    page->ioreq = INVAL_IOREQ; 
    next = page->ioreq_next;

    cnt ++;

    while ( next != page ) {
        SetPageClean (next);
        ClearPageDirty (next);
        ClearPageReadPending (next);
        ClearPageWritePending (next);

        next->ioreq = INVAL_IOREQ;

        cnt++;

        next = next->ioreq_next;
    }

    DPRINT("clean_page: %d\n", cnt);

    return;
}


int NCAC_move_inactive_to_active(struct cache_stack *cache_stack,struct extent *page)
{
    del_page_from_inactive_list(cache_stack, page);
    add_page_to_active_list(cache_stack, page);
    return 0;
}


int NCAC_extent_read_access_recheck(NCAC_req_t *req, struct extent *page, unsigned int offset, unsigned int size)
{
    int error;

    if ( PageLRU(page) ){
        if (PageRcomm(page)) 
            return 1;

        /* write communication is pending. 
         * A write is ahead of this read. wait!!! 
         */
        if (PageWcomm(page)) return 0;

        /* clean or dirty extent */
        if (PageClean(page) || PageDirty(page)) {
            SetPageRcomm(page); return 1;
        }

        /* read pending: Trove read operation is pending.  */
        /* write pending: for example flushing daemon is working on it */

        if (PageReadPending(page) || PageWritePending(page)) {
            error = NCAC_check_ioreq(page);
            if (error <0) {
                NCAC_error("%s:%d NCAC_check_ioreq error\n", __FILE__, __LINE__);
                return error;
            }
            if (error) {

                /* set all other related extents */
				list_set_clean_page(page);

                /* the request is done and the read is finished.
                 * Note that NCAC_check_ioreq sets page flags in this case.*/
                SetPageRcomm(page); return 1; 
            }
            return 0;
        }
    }

    /* invalid flags */
    return NCAC_INVAL_FLAGS;
}


int NCAC_extent_write_access_recheck(NCAC_req_t *ncac_req, struct extent *page, unsigned int offset, unsigned int size)
{
    int error;

    if ( PageLRU(page) ){
		if ( PageRMW(page) ) {
            error = NCAC_check_ioreq(page);
            if (error <0) {
                NCAC_error("NCAC_check_ioreq error\n");
                return error;
            }
			if ( !error ) 
				return 2;  /* 2 for rmw */
			else {
				ClearPageRMW(page);
				return 1;
			}
		}

        if (PageRcomm(page) || PageWcomm(page))
            return 0;


        if (PageReadPending(page) || PageWritePending(page)) {
            error = NCAC_check_ioreq(page);
            if (error <0) {
                NCAC_error("NCAC_check_ioreq error\n");
                return error;
            }
            if (error) {
                /* set all other related extents */
				list_set_clean_page(page);

                /* the request is done and the read is finished.
                 * Note that NCAC_check_ioreq sets page flags in this case.*/
                SetPageWcomm(page);
                return 1;
            }
            return 0;
        }


        /* No pending comm, clean or dirty extent */
        if (PageClean(page) || PageDirty(page)) {
            SetPageWcomm(page);
            return 1;
        }

        return 0;
    }

    /* invalid flags */
    return NCAC_INVAL_FLAGS;
}


#if 0
static void	balance_dirty_extents( struct cache_stack *cache_stack)
{
    fprintf(stderr, "balance_dirty_extents is not implemented yet\n");
}
#endif

/* data_sync_inode(): initiate sync operation on all dirty extents so far. 
 * problem here: the file offset is increasing?
 * The caller holds the inode lock.
 * 
 * TODO: is it useful to order extents in the dirty list?
 */

int data_sync_inode(struct inode *inode)
{
	struct list_head  *list, *tail;
	struct extent *extent, *prev, *first;
	int nr_dirty;
	PVFS_offset *offset;
	char **mem;
	PVFS_size *offsize, *memsize;
	int cnt = 0;
	int ioreq;
	int ret;
	int i;

	nr_dirty = inode->nr_dirty;

	DPRINT("data_sync_inode: nr_dirty=%d\n", nr_dirty);
	if ( !nr_dirty ) return 0;

	/* allocate Trove parameters */
	offset = (PVFS_offset *)malloc(nr_dirty*(sizeof(PVFS_offset) + sizeof(char*) + 2*sizeof(PVFS_size)) );
	mem = (char**)&offset[nr_dirty];
	offsize = (PVFS_size*)&mem[nr_dirty];
	memsize = &offsize[nr_dirty];
	
	list = &(inode->dirty_pages);
	
	/* from the head to the tail to keep the order */
	tail = list->next; 
	while ( tail != list ){
		extent = list_entry(tail, struct extent, list);
		offset[cnt] = extent->index * NCAC_dev.extsize;
		offsize[cnt] = NCAC_dev.extsize;
		mem[cnt] = extent->addr;
		memsize[cnt] = NCAC_dev.extsize;

		DPRINT("to write: pos:%Ld, size=%Ld, buf=%p\n", 
			 offset[cnt], offsize[cnt], mem[cnt]);
		cnt ++;
		tail = tail->next;
	}

	ret = NCAC_aio_write(inode->coll_id, inode->handle, 
			inode->context_id, nr_dirty, 
			offset, offsize, mem, memsize, &ioreq);
	if ( ret < 0 ) {
		NCAC_error("data_sync_inode: NCAC_aio_write error \n");
		return ret;
	}else{
		/* we need to associate all extents to an IOreq */

		tail = list->next; 
		extent = list_entry(tail, struct extent, list);

		SetPageWritePending(extent);
		extent->ioreq_next = extent;
		prev = first = extent;

		extent->ioreq = ioreq;
		list_del(&extent->list);
		tail = list->next;

		for ( i = 1; i < nr_dirty; i ++ ) { 
			extent = list_entry(tail, struct extent, list);
			SetPageWritePending(extent);
			prev->ioreq_next = extent;
			extent->ioreq = ioreq;

			prev = extent;
			list_del(&extent->list);
			tail = list->next;
		}
		extent->ioreq_next = first;

		inode->nr_dirty = 0;
	}

	return nr_dirty;
}



void mark_extent_rmw_lock(struct extent *extent, int ioreq)
{
	SetPageRMW(extent);
	extent->ioreq = ioreq;
	extent->ioreq_next = extent;
}
