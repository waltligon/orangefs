/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/* PVFS User library file descriptor management functions */

#include <pint-userlib.h>
#include <pvfs2-userlib.h>

/* The all encompassing Fd structure */
static pfd_manage pfdm;

/* We probably need a mutex? 
 * Need to grab the mutex so that getting the fd from
 * the list and initializing the array entry are atomic
 */

/*init_fds
 *
 *set up the open file table
 *
 *returns 0 on success, -errno on error
 */
int init_fds()
{
	/* Initialize the fd manage structure */
	pfdm.fd_list = NULL;
	pfdm.fdesc_p = NULL;
	pfdm.fd_lock = gen_mutex_build();
	if (!pfdm_p.fd_lock)
	{
		printf("Lock Init failed\n");
		return(-EINVAL);
	}
	
	/* Create free list for fds */
	pfdm.fd_list = llist_new();
	if (!pfdm.fd_list)
		return(-ENOMEM);

	/* Initialize the number of open files */
	pfdm.nr_fds = 0;

	/* Initialize the free list */
	for(i = 0;i < PVFS_NR_OPEN;i++)
	{
		node_p = (flist_node *)malloc(flist_node);
		if (!node_p)
		{
			printf("Error in allocating memory\n");
			/* Free the list */
			llist_free(pfdm.fd_list,node_free);
			return(-ENOMEM);
		}
		node_p->fd = i;
		/* Add to the list */
		ret = llist_add_to_head(pfdm.fd_list,node_p);
		if (ret < 0)
		{
			printf("Error in adding node to free list\n");
			/* Free the list */
			llist_free(pfdm.fd_list,node_free);
			return(-EINVAL);
		}
	}

	/* Allocate memory for array of fdesc's */
	fdesc_p = (fdesc *)malloc(PVFS_NR_OPEN * sizeof(fdesc));
	if (!fdesc_p)
	{
		/* Free the list */
		llist_free(pfdm.fd_list,node_free);
		return(-ENOMEM);
	}
	memset(pfdm.fdesc_p,0,PVFS_NR_OPEN * sizeof(fdesc));

	return(0);


}

/* set_desc_info
 *
 * Sets the fd in the descriptor structure 
 *
 * returns 0 on success, -errno on error 
 */
int set_desc_info(fdesc *fd_p, int *num)
{
	/* Lookup the first entry in the free list */
	fdesc *tmp = NULL;
	
	if (!fd_p)
		return(-EINVAL);

	/* Get the lock */
	gen_mutex_lock(pfdm.fd_lock);

	/* Pick an entry from the free list */
	tmp = llist_head(pfdm.fd_list);
	if (!tmp)
	{
		printf("Max limit for open files reached\n");
		return(-EINVAL);
	}

	/* Create and initialize the entry in the array */
	index = tmp->fd;
	/* Fill in whatever needs to be done */
	pfdm.fdesc_p[index].fd = fd_p->fd;
	pfdm.fdesc_p[index].collid = fd_p->collid;
	pfdm.fdesc_p[index].attr = fd_p->attr;
	pfdm.fdesc_p[index].flag = fd_p->flag;
	pfdm.fdesc_p[index].off = fd_p->off;
	pfdm.fdesc_p[index].ftype = fd_p->ftype;
	pfdm.fdesc_p[index].cap = fd_p->cap;

	/* Update the number of open files */
	pfdm.nr_fds++;

	/* Should we pass back the index? */
	/* Set the MSB */
	*num = index | (1 << ((sizeof(int) * 8) - 1));
	/* *num = index;*/
		
	/* Unlock */
	gen_mutex_unlock(pfdm.fd_lock);

	return(0);
}

/* get_desc_info
 *
 * Gets the fdesc in the descriptor structure 
 *
 * returns 0 on success, -errno on error 
 */
int get_desc_info(fdesc *fd_p, int num)
{

	int index = 0;
	
	/* Check fdesc ptr */
	if (!fd_p)
		return(-EINVAL);

	/* Get the lock */
	gen_mutex_lock(pfdm.fd_lock);

	/* Reset the MSB */
	index = num & ~(1 << ((sizeof(int) * 8) - 1));

	/* We need to have another entry level lock
	 * so that nobody modifies the entry while
	 * we have read it for modification 
	 */
	/* Copy the entry */
	memcpy(fd_p,&pfdm.fdesc_p[index],sizeof(fdesc));
		
	/* Unlock */
	gen_mutex_unlock(pfdm.fd_lock);

	return(0);
}

/* reset_desc_info
 *
 * reset all values associated with the fd 
 *
 * returns 0 on success, -errno on error
 */
int reset_desc_info(int num)
{
	flist_node *node_p = NULL;

	/* Get the lock */
	gen_mutex_lock(pfdm.fd_lock);

	/* Is the file descriptor array allocated */
	if (!pfdm.fdesc_p)
	{
		printf("Index into open file table invalid\n");
		return(-EINVAL);
	}
	/* Reset the MSB */
	num = num & ~(1 << ((sizeof(int) * 8) - 1));
	memset(&pfdm.fdesc_p[num],0,sizeof(fdesc));
		
	/* Add the entry to the free list */
	node_p = (flist_node *)malloc(flist_node);
	if (!node_p)
	{
		printf("Error in allocating memory\n");
		return(-ENOMEM);
	}
	node_p->fd = num;
	ret = llist_add_to_tail(pfdm.fd_list,node_p);
	if (ret < 0)
	{
		free(node_p);
		printf("Error in adding index to free list\n");
		return(-EINVAL);
	}

	/* Update the number of open files */
	pfdm.nr_fds--;

	/* Release the lock */
	gen_mutex_unlock(pfdm.fd_lock);

	return(0);

}

/* close_fds
 *
 * Release all resources allocated
 *
 * returns 0 on success, -1 on error
 */
int close_fds()
{
	/* First obtain lock and check if all files
	 * have been closed */
	gen_mutex_lock(pfdm.fd_lock)
	
	if (pfdm.nr_fds != 0)
	{
		printf("Files still open\n");
		return(-EINVAL);
	}
	/* Release the lock */
	gen_mutex_unlock(pfdm.fd_lock);

	/* Destroy the mutex */
	gen_mutex_destroy(pfdm.fd_lock);
	/* Free the list */
	llist_free(pfdm.fd_list,node_free);
	/* Deallocate the array of fdesc structures */
	free(pfdm.fdesc_p);

}
